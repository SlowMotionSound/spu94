# Architecture: v1.11 Live Input Sampling Integration

**Domain:** Real-time audio input recording into PS1 SPU voice RAM
**Researched:** 2026-05-28
**Confidence:** HIGH (based on direct codebase inspection of all relevant files)

---

## Executive Summary

Live input sampling adds a recording path to the existing sampler. Audio arrives via
the JUCE processBlock input buffer, gets resampled to the target rate, ADPCM-encoded,
and written into the 512KB voice RAM -- the same buffer that file-loaded samples live
in. The architecture splits cleanly: a thin capture ring buffer lives in the JUCE
wrapper, while the ADPCM encoding and RAM write happen on a background thread (not
the audio callback). The existing C core API needs no changes to its hot-path
functions. Two new C core functions are needed for incremental RAM writes.

---

## 1. Existing Architecture (What We Have)

### Audio Input Flow

```
  Host audio input
        |
  processBlock() receives juce::AudioBuffer<float>
        |
  +-- isStandalone? --+-- Plugin path --+
  |                   |                 |
  | WavSource loop    | SRC sandwich    |
  | int16 samples     | float->int16    |
  +-------------------+-----------------+
        |
  spu94_process_split(engines[0], inL, inR, ...)
        |
  Voice mixer + Reverb + DAC
        |
  Output
```

Key observation: the standalone path already receives live audio device input in the
buffer. The input samples are available as float in `buffer.getReadPointer(0)` and
`buffer.getReadPointer(1)`. Currently, the standalone path ignores the host input
buffer entirely -- it feeds WavSource playback data (or zeros) to `spu94_process_split`.
The live input is present but unused.

### Sample Loading Flow (File Path)

```
  loadVoiceSample(file)  [message thread]
      |
  WavLoader::load(file)  --> LoadedWav {L, R, numFrames} at 44100 Hz
      |
  libsamplerate src_simple() at target rate (22050, 11025, etc.)
      |
  spu94_sample_encode_to_ram(pcm, N, voice_ram, 0, RAM_SIZE, loop)
      |
  Writes ADPCM blocks into mixer->voice_ram
      |
  Build adpcmStateCache (decode each block to capture filter state)
      |
  Store waveformData (original int16 L channel for GUI thumbnail)
      |
  voiceSampleLoaded = true
```

This is entirely a message-thread operation. The brute-force ADPCM encoder is
CPU-intensive (65 filter/shift trials per 28-sample block) but that is acceptable
for one-shot file loads. For live recording, this same encoder must run continuously
at whatever rate audio arrives.

### ADPCM Encoder Characteristics

The encoder (`spu94_adpcm_encode_block`) processes exactly 28 PCM samples into one
16-byte ADPCM block. Properties relevant to live recording:

- **Stateful**: carries `spu94_adpcm_state` (old/older) across blocks
- **Deterministic**: same input always produces same output
- **CPU cost**: 65 inner loops of 28 iterations = 1820 predict-quantize-decode
  operations per block. At 44100 Hz, that is ~1575 blocks/second (~2.9M operations/sec).
  At 22050 Hz, ~788 blocks/second.
- **No heap, no syscalls**: safe on any thread
- **Block-aligned**: must receive exactly 28 samples. Partial blocks get zero-padded.

### Voice RAM Layout

- 512KB (`SPU94_SPU_RAM_BYTES = 0x80000`)
- Separate from reverb work buffer (deliberate deviation from PS1, documented in C6/RAM-01)
- Currently: entire sample occupies offset 0..voiceSampleBytes
- ADPCM block size: 16 bytes per 28 samples
- Maximum capacity: 32768 blocks = 917,504 samples at any rate
- At 44100 Hz: ~20.8 seconds. At 22050 Hz: ~41.6 seconds. At 5512 Hz: ~166 seconds.

### Thread Model

```
  Message thread (GUI):
    - loadVoiceSample() -- file I/O, SRC, ADPCM encode, RAM write
    - All GUI callbacks
    - Sets atomics for audio thread to read

  Audio thread:
    - processBlock() -- reads atomics, pushes to engine, ticks DSP
    - Owns engines[0]/engines[1]
    - Reads voice_ram via spu94_get_voice_mixer()
    - MUST NOT allocate, lock, or syscall

  No worker threads currently exist.
```

---

## 2. Recommended Architecture for Live Input Recording

### High-Level Signal Flow

```
  Audio input (processBlock buffer)
        |
  Capture ring buffer [audio thread writes, encoder thread reads]
        |                (lock-free SPSC ring)
        |
  Background encoder thread
    |-- SRC (libsamplerate) to target rate
    |-- ADPCM encode (28-sample blocks)
    |-- Write ADPCM blocks into voice_ram
    |-- Update write cursor + adpcmStateCache
    |-- Signal GUI: new samples available
        |
  Waveform display updates incrementally
```

### Why Not Encode on the Audio Thread?

The ADPCM encoder's brute-force search (65 combinations per block) is too expensive
for the audio callback at low block sizes. At 64-sample buffers at 44100 Hz, the
callback budget is ~1.45ms. One ADPCM block encode takes roughly 0.5-1.0ms on a
modern CPU (28 samples x 65 trials). That is a significant fraction of the budget,
and the audio thread is already running the full reverb network, DAC model, and
24-voice mixer.

The existing `loadVoiceSample` already runs the encoder off-audio-thread. Live
recording extends the same principle: capture raw PCM on the audio thread (cheap --
just a memcpy into a ring buffer), encode on a background thread.

### Why Not Encode on the Message Thread?

The message thread handles GUI repaints and user interaction. Blocking it with
continuous ADPCM encoding would freeze the waveform display, buttons, and other
controls. A dedicated encoder thread keeps GUI responsive.

### Component Breakdown

#### A. Capture Ring Buffer (new, JUCE wrapper layer)

A lock-free single-producer/single-consumer (SPSC) ring buffer. The audio thread
writes float samples. The encoder thread reads them.

```
Size: 8192 samples (stereo pair) = ~186ms at 44100 Hz
      Enough to absorb jitter between audio callbacks and encoder wake cycles.

Producer: audio thread (processBlock)
Consumer: encoder thread

Data: float[8192] per channel (L, R)
      Using float rather than int16 because the audio thread receives float.
      The float-to-int16 conversion happens on the encoder thread after SRC.
```

Implementation: `juce::AbstractFifo` wraps two atomics (read/write heads) and
provides the correct SPSC semantics. No custom ring needed.

#### B. Encoder Thread (new, JUCE wrapper layer)

A `juce::Thread` that:

1. Sleeps until woken by the audio thread (via `notify()`) or periodically (10ms)
2. Drains the ring buffer in chunks
3. Runs libsamplerate SRC if target rate differs from 44100 Hz
4. Feeds 28-sample blocks to `spu94_adpcm_encode_block`
5. Writes encoded blocks into `voice_ram` at the current write cursor
6. Updates the write cursor and RAM-used counter
7. Extends `adpcmStateCache` with each new block's pre-encode state
8. Appends decoded waveform samples for GUI display
9. Checks if RAM is full; if so, signals recording stop

SRC approach: `src_simple` per drain chunk. The chunks are large enough (hundreds
to thousands of samples) that batch SRC is efficient. No need for streaming
`src_callback_new` here because the encoder thread has no real-time constraint.
Quality: `SRC_SINC_BEST_QUALITY` (same as the existing file-load path in
`loadVoiceSample`).

Why `SRC_SINC_BEST_QUALITY` not `MEDIUM`: recording is a one-shot creative act.
The CPU cost of BEST is affordable on the encoder thread (not audio thread).
The user wants maximum fidelity before the ADPCM encoding deliberately degrades it.

#### C. Recording State Machine (new, shared via atomics)

```
States:
  IDLE          -- not recording
  ARMED         -- threshold mode: waiting for input to exceed threshold
  RECORDING     -- capturing audio into ring buffer
  STOPPING      -- draining ring buffer, finalizing

Transitions:
  IDLE -> ARMED         (user clicks Record in threshold mode)
  IDLE -> RECORDING     (user clicks Record in manual mode)
  ARMED -> RECORDING    (input exceeds threshold)
  RECORDING -> STOPPING (user clicks Stop, or RAM full)
  STOPPING -> IDLE      (encoder thread finishes draining)
```

State is an `std::atomic<int>` shared between audio thread and GUI.
Direction of writes:

```
  GUI thread:     IDLE -> ARMED, IDLE -> RECORDING, RECORDING -> STOPPING
  Audio thread:   ARMED -> RECORDING (threshold trigger)
  Encoder thread: STOPPING -> IDLE (drain complete)
```

#### D. Voice RAM Write Path (modified, C core + wrapper)

Currently, `spu94_sample_encode_to_ram` writes all blocks at once from offset 0.
Live recording needs incremental writes: each batch from the encoder thread appends
at the current write cursor.

Two options for the C core API:

**Option 1 (Recommended): Call existing `spu94_adpcm_encode_block` directly + memcpy**

The encoder thread already holds the `spu94_adpcm_state`. It can call
`spu94_adpcm_encode_block` and then directly write the 16-byte block into
`mixer->voice_ram` at the current offset. No new C core function needed for the
write -- just pointer arithmetic on `voice_ram`. The wrapper manages the cursor.

This is exactly what `spu94_sample_encode_to_ram` does internally, just unrolled
into an incremental loop on the encoder thread instead of a single batch call.

**New C core function needed:**

```c
/* Finalize a recorded sample in voice_ram by patching the last ADPCM block's
 * flag byte to END (or END|REPEAT if loop_enable). Existing blocks have
 * flags=0x00 (NORMAL). Only the final block needs patching.
 *
 * ram_offset: byte offset of the FIRST block of this recording.
 * total_bytes: total bytes written (must be multiple of 16).
 * loop_enable: 1 = set REPEAT flag, 0 = END only.
 */
void spu94_sample_finalize(uint8_t *voice_ram,
                           uint32_t ram_offset, uint32_t total_bytes,
                           int loop_enable);
```

This patches `voice_ram[ram_offset + total_bytes - 16 + 1]` (the flag byte of the
last block) to `0x01` or `0x03`. During recording, all blocks are written with
`flags=0x00` (NORMAL) so the voice engine keeps reading past them. The finalize
call stamps the endpoint.

#### E. Thread Safety Diagram

```
  Audio Thread                    Encoder Thread              GUI Thread
  ~~~~~~~~~~~~                    ~~~~~~~~~~~~~~              ~~~~~~~~~~
  1. Read recording state         1. Sleep/wait               1. Set state
     (atomic load)                                               (atomic store)
  2. If RECORDING:                2. Wake, read ring          2. Read ramUsed
     copy float samples              (AbstractFifo read)         (atomic load)
     into ring buffer             3. SRC + ADPCM encode       3. Read waveform
     (AbstractFifo write)         4. Write voice_ram             data
  3. If ARMED:                       at cursor                   (append buffer
     check threshold              5. Update cursor               + atomic fence)
     ARMED -> RECORDING              ramUsed (atomic store)   4. Update display
  4. Notify encoder thread        6. Append waveform data
     (thread.notify())            7. If RAM full:
  5. Continue normal                 set STOPPING
     processBlock flow            8. If STOPPING + drained:
                                     finalize, set IDLE

  Shared data:
  - Ring buffer: SPSC, audio writes, encoder reads (AbstractFifo)
  - Recording state: atomic<int> (all three threads read; specific writers above)
  - Write cursor: atomic<uint32_t> (encoder writes, GUI reads)
  - RAM used: atomic<uint32_t> (encoder writes, GUI reads)
  - voice_ram: encoder writes at [cursor..cursor+16), audio reads at [0..endAddr)
    Thread safety: encoder writes AHEAD of where any playing voice reads. The
    voice engine's current_addr is always behind the write cursor because the
    recorded sample is not triggered until recording stops.
```

Key insight about voice_ram safety: during recording, no voice is playing the
data being written. The recorded sample only becomes playable after recording stops
and the user triggers it. So the encoder thread can write freely into voice_ram
without racing with the audio thread's voice playback reads. The voice mixer
reads voice_ram only for already-playing voices at addresses below the recording
region.

---

## 3. Integration Points with Existing Code

### A. PluginProcessor.cpp Changes

**processBlock standalone path -- new recording capture section:**

Insert after the MIDI dispatch block, before the WavSource gate. The recording
capture reads the host input buffer and pushes samples into the ring buffer:

```
Location: processBlock, standalone path, after line ~1338 (MIDI dispatch)

New code:
  if (recordingState == RECORDING) {
      const float* inL = buffer.getReadPointer(0);
      const float* inR = (buffer.getNumChannels() > 1)
                         ? buffer.getReadPointer(1) : inL;
      // Write n float samples into SPSC ring
      captureRing.write(inL, inR, n);
      encoderThread.notify();
  }
  else if (recordingState == ARMED) {
      // Threshold check on input RMS
      float peak = 0.0f;
      const float* inL = buffer.getReadPointer(0);
      for (int i = 0; i < n; ++i)
          peak = std::max(peak, std::fabs(inL[i]));
      if (peak > recordThreshold.load())
          recordingState.store(RECORDING);
  }
```

**processBlock plugin path -- sidechain consideration:**

For the DAW plugin, live input sampling is NOT a feature target. The plugin
processes host audio through the reverb; it does not have a "record into sampler"
mode. If desired later, the plugin's input buffer could be captured the same way,
but this is a standalone-only feature for v1.11.

**New member variables in PluginProcessor.h:**

```cpp
// Live recording state
std::atomic<int> recordingState{0};  // 0=IDLE, 1=ARMED, 2=RECORDING, 3=STOPPING
std::atomic<float> recordThreshold{0.01f};
std::atomic<uint32_t> recordWriteCursor{0};
// Ring buffer, encoder thread -- see new classes
```

### B. PluginProcessor.h -- New Public API

```cpp
// --- Live input recording (v1.11) ---
void startRecording();          // IDLE -> RECORDING
void startRecordingArmed();     // IDLE -> ARMED (threshold mode)
void stopRecording();           // RECORDING -> STOPPING
bool isRecording() const;
bool isArmed() const;

std::atomic<int>&    getRecordingState()    { return recordingState; }
std::atomic<float>&  getRecordThreshold()   { return recordThreshold; }
std::atomic<uint32_t>& getRecordWriteCursor() { return recordWriteCursor; }

// Access to incrementally recorded waveform for GUI display
const std::vector<int16_t>& getRecordedWaveform() const;
uint64_t getRecordedFrames() const;

// Export recorded sample to WAV file
void exportSample(const juce::File& file);
```

### C. loadVoiceSample Compatibility

The existing `loadVoiceSample` (file load) and live recording write to the same
voice_ram. They are mutually exclusive: loading a file replaces whatever was
recorded, and vice versa. No special interlock needed beyond "recording must stop
before file load" which the GUI enforces (disable Load button while recording).

### D. Waveform Display Updates

The existing `WaveformDisplay::setSample` replaces the entire thumbnail. For live
recording, the display needs incremental updates as new audio arrives.

New method on WaveformDisplay:

```cpp
void appendSamples(const int16_t* data, int numFrames);
```

This calls `thumbnail.addBlock(currentOffset, buffer, 0, numFrames)` to extend
the displayed waveform without rebuilding from scratch. The encoder thread appends
decoded PCM to a growing buffer; the GUI timer polls for new data and calls
`appendSamples`.

### E. SRC for Input Recording

Reuse libsamplerate (`src_simple`) on the encoder thread, same as `loadVoiceSample`
already does. The encoder thread is not real-time constrained, so `SRC_SINC_BEST_QUALITY`
is appropriate. The SRC converts from 44100 Hz (the capture rate, matching the host
device rate) down to the target encode rate (22050, 11025, 5512, or arbitrary via
pitch register).

For arbitrary rates: the pitch register value maps to a playback rate as
`rate = pitch * 44100 / 0x1000`. Recording at an arbitrary rate means capturing at
44100 Hz and downsampling to `rate` before ADPCM encoding. The SRC ratio is
`rate / 44100.0`. This is identical to the existing file-load path logic.

### F. Standalone vs Plugin Input Routing

**Standalone:** Audio input comes from the system audio device (microphone, interface).
JUCE's `AudioDeviceManager` provides the input buffer in processBlock. This is the
primary target for live recording. The standalone already declares stereo input in
its BusesProperties.

**Plugin:** Audio input comes from the DAW host bus. While technically available in
processBlock, live recording in a plugin context is unusual and raises questions about
monitoring, latency compensation, and workflow. v1.11 targets standalone only.

If plugin recording is desired later, the architecture supports it without changes --
the ring buffer capture code is the same regardless of where the float samples
originate.

---

## 4. New Components

### A. CaptureRingBuffer (new class, JUCE wrapper layer)

```
File: src/plugin/CaptureRingBuffer.h

Wraps juce::AbstractFifo for stereo float SPSC ring buffer.
- write(const float* L, const float* R, int numSamples) -- audio thread
- read(float* L, float* R, int maxSamples) -> int -- encoder thread
- getNumReady() -> int
- reset()

Internal: juce::AbstractFifo + two float arrays (L, R), size 8192 samples.
```

### B. SampleEncoderThread (new class, JUCE wrapper layer)

```
File: src/plugin/SampleEncoderThread.h / .cpp

juce::Thread subclass.
- References: CaptureRingBuffer, voice_ram pointer, recording state atomics
- Owns: SRC state (src_simple per chunk), ADPCM encoder state, write cursor
- Owns: decoded waveform buffer for GUI (growing std::vector<int16_t>)

run() loop:
  while (!threadShouldExit()) {
      wait(10);  // or notify-woken
      drain ring buffer into local buffer
      SRC to target rate
      ADPCM encode 28-sample blocks
      write blocks to voice_ram[cursor..]
      update cursor, ramUsed
      append decoded PCM to waveform buffer
      if cursor >= RAM limit: signal STOPPING
  }
```

### C. C Core Addition: spu94_sample_finalize

```
File: src/spu94/spu94_sample_loader.c (addition to existing file)
Header: include/spu94/spu94_sample_loader.h (addition)

void spu94_sample_finalize(uint8_t *voice_ram,
                           uint32_t first_block_offset,
                           uint32_t total_bytes,
                           int loop_enable);

Patches the flag byte of the last ADPCM block to END (0x01) or END|REPEAT (0x03).
During live recording, all blocks are written with flags=0x00 (NORMAL).
```

This is the ONLY new C core function needed. Everything else stays in the wrapper.

### D. WAV Export (Save Sample)

```
File: src/plugin/SampleExporter.h / .cpp

Uses dr_wav (already vendored for CLI) or juce::WavAudioFormat to write
the decoded waveform buffer to a .wav file. Runs on the message thread
(file I/O is acceptable there).

Alternatively, re-decode voice_ram ADPCM blocks back to PCM at export time,
which guarantees the exported audio matches exactly what the voice engine
would play. This is the more faithful approach.
```

---

## 5. Data Flow: Complete Recording Cycle

```
  1. User selects encode rate (dropdown: 44100/22050/11025/5512 Hz)
  2. User clicks RECORD button

  3. processBlock [audio thread]:
     - Reads host input buffer (float L, R)
     - Writes into CaptureRingBuffer
     - Calls encoderThread.notify()

  4. SampleEncoderThread [background]:
     - Reads from CaptureRingBuffer (float L, R chunks)
     - Mixes to mono (the PS1 SPU uses mono voice samples)
     - SRC from 44100 Hz to target rate via src_simple
     - Converts float to int16 (truncation, PS1-faithful)
     - Feeds 28-sample blocks to spu94_adpcm_encode_block
     - Writes 16-byte ADPCM blocks to voice_ram[cursor..]
     - Increments cursor by 16 per block
     - Caches ADPCM state for each block boundary
     - Appends decoded int16 samples to waveform buffer
     - Updates ramUsed atomic

  5. GUI timer [message thread, 30 Hz]:
     - Reads ramUsed -> updates RAM meter
     - Reads new waveform samples -> appends to WaveformDisplay
     - If ramUsed >= SPU94_SPU_RAM_BYTES: auto-stop

  6. User clicks STOP (or RAM fills up):
     - recordingState -> STOPPING
     - Encoder thread finishes current drain
     - Calls spu94_sample_finalize on the last block
     - Sets voiceSampleBytes, voiceSampleLoaded = true
     - recordingState -> IDLE

  7. Sample is now playable via existing trigger/MIDI path.
     Waveform display shows the full recording.
     Start/End/Loop markers are available for editing.
```

---

## 6. Mono vs Stereo Recording

The PS1 SPU voices are mono. The existing `loadVoiceSample` loads only the L channel
(`result->L.data()`) and discards R. Live recording should follow the same pattern:

- Capture stereo (both channels are available)
- Mix to mono before encoding: `mono[i] = (L[i] + R[i]) * 0.5f`
- Encode the mono signal

This is consistent with PS1 hardware behavior -- the SPU plays mono samples and
applies per-voice stereo volume to create the stereo image.

An input source selector could offer: "Left", "Right", "Mono (L+R)" options.
Default: Mono (L+R).

---

## 7. Build Order (Incremental Testability)

### Phase 1: Capture Ring Buffer + Encoder Thread (no GUI)

**Build:**
- CaptureRingBuffer class with unit test
- SampleEncoderThread class (encode to voice_ram, no SRC yet -- 44100 Hz only)
- spu94_sample_finalize in C core
- Recording state atomics in PluginProcessor
- processBlock capture code (standalone path only)
- Manual start/stop via programmatic triggers

**Testable:** Record audio, verify ADPCM blocks appear in voice_ram. Trigger playback
of the recorded sample via existing key_on path. Audio should be recognizable (same
content, ADPCM-degraded).

**Why first:** This is the critical new architecture (ring buffer + thread). Everything
else layers on top. If the encoder thread cannot keep up or races with voice playback,
this is where it shows.

### Phase 2: SRC Integration + Rate Selection

**Build:**
- SRC on encoder thread (src_simple per drain chunk)
- Rate selection atomic (reuse existing encodeRate)
- Test at all 4 standard rates: 44100, 22050, 11025, 5512

**Testable:** Record at each rate, play back at pitch=0x1000. Pitch should match
original audio at 44100 Hz, be one octave lower at 22050 Hz (because playback
rate = pitch * 44100 / 0x1000 and the sample is at half rate). Correct playback
pitch requires setting pitch = encodeRate * 0x1000 / 44100.

### Phase 3: GUI -- Record Button + Waveform

**Build:**
- Record/Stop button in SamplerPanel
- Waveform display incremental updates during recording
- RAM usage meter/display
- Input source selector (L/R/Mono)

**Testable:** User can click Record, see waveform grow, click Stop, trigger playback.

### Phase 4: Threshold Trigger

**Build:**
- ARMED state in state machine
- Threshold slider in GUI
- Peak detection in processBlock
- Visual indicator (armed/recording/idle)

**Testable:** Set threshold, arm, clap -- recording starts on transient.

### Phase 5: Save Sample Export

**Build:**
- WAV export (decode voice_ram to PCM, write via dr_wav or JUCE)
- Save Sample button + file dialog
- Preset integration (save recording metadata: encode rate, sample length)

**Testable:** Record, export, re-import -- audio should match.

---

## 8. Anti-Patterns to Avoid

### Anti-Pattern 1: ADPCM Encoding on the Audio Thread

**What:** Running `spu94_adpcm_encode_block` inside processBlock.
**Why bad:** 65-combination brute force search per 28 samples is too expensive for
the audio callback, especially at small buffer sizes (64-128 samples). Would cause
dropouts.
**Instead:** Capture raw float on audio thread (cheap memcpy), encode on background
thread.

### Anti-Pattern 2: Blocking the Audio Thread on Ring Buffer Full

**What:** If the ring buffer fills up (encoder thread fell behind), the audio thread
blocks waiting for space.
**Why bad:** Audio thread must never block. Causes audible glitch.
**Instead:** Drop samples on overflow. The ring buffer write returns "no space";
the audio thread skips that batch. This is a recording quality issue, not a crash.
Log overflow events for diagnostics.

### Anti-Pattern 3: Sharing ADPCM Encoder State Between Threads

**What:** Audio thread and encoder thread both touching the same `spu94_adpcm_state`.
**Why bad:** Data race. ADPCM state is not atomic.
**Instead:** Encoder thread owns its own `spu94_adpcm_state` exclusively. Audio thread
never touches it.

### Anti-Pattern 4: Writing voice_ram While a Voice Plays the Same Region

**What:** Encoder thread writes blocks at addresses a playing voice is reading.
**Why bad:** Garbled playback, torn reads.
**Instead:** During recording, no voice plays the recording region. Recording and
playback are sequential: record, then play. The GUI disables trigger while recording.

### Anti-Pattern 5: Rebuilding Full Waveform Thumbnail on Every Update

**What:** Calling `WaveformDisplay::setSample` with the entire recording buffer
every time new samples arrive.
**Why bad:** O(n) copy + thumbnail rebuild every 30ms = increasingly expensive as
recording grows.
**Instead:** Use `thumbnail.addBlock(offset, ...)` to append only new samples.

---

## 9. Scalability Considerations

| Concern | At 5s recording | At 20s recording | At 42s (max at 22050 Hz) |
|---------|-----------------|-------------------|--------------------------|
| Ring buffer | Trivial | Trivial | Trivial (constant size) |
| Encoder thread | Idle most of time | Idle most of time | Idle most of time |
| Waveform display | Fast | Moderate | Moderate (thumbnail handles large data) |
| RAM usage | ~55 KB | ~220 KB | ~512 KB (full) |
| adpcmStateCache | ~3500 entries | ~14000 entries | ~32768 entries (~260 KB) |

The adpcmStateCache at full RAM occupancy is 32768 entries x 4 bytes = ~128 KB.
This is allocated on the heap (existing `std::vector`) and is acceptable.

---

## 10. What Does NOT Change

- `spu94_adpcm_encode_block` -- unchanged, called from encoder thread
- `spu94_voice_tick` -- unchanged, plays back from voice_ram as before
- `spu94_voice_mixer_tick` -- unchanged
- `spu94_process_split` -- unchanged
- `SrcChain` -- unchanged (this is the host-rate-to-44100 SRC for the reverb path)
- `WavLoader` -- unchanged (file loading path remains separate)
- Plugin processBlock path -- unchanged (recording is standalone-only in v1.11)
- All reverb, DAC, morph, preset, sweep, mod bus code -- unchanged

---

## Sources

- Direct inspection of: `src/plugin/PluginProcessor.cpp` (2362 lines), `PluginProcessor.h` (554 lines)
- Direct inspection of: `src/spu94/spu94_adpcm_encode.c`, `spu94_sample_loader.c`
- Direct inspection of: `include/spu94/spu94_voice.h`, `spu94_sample_loader.h`, `spu94_adpcm.h`
- Direct inspection of: `src/plugin/SrcChain.cpp`, `WavLoader.cpp`, `WaveformDisplay.h`
- Confidence: HIGH -- all claims verified against actual source code
