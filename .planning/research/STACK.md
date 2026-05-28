# Technology Stack: v1.11.0 Live Input Sampling

**Project:** SPU-94
**Researched:** 2026-05-28
**Mode:** Subsequent milestone -- stack additions only

## Executive Summary

Live input sampling adds zero new external dependencies. Every capability required
-- real-time ADPCM encoding, input SRC, audio device selection, threshold detection,
WAV export, and waveform display updates -- is achievable with the existing stack
(JUCE 8.0.12, libsamplerate, libspu94 C core). The work is architecture and
integration, not library shopping.

The one non-trivial technical question is whether the brute-force ADPCM encoder
(65-combination search per 28-sample block) is fast enough to run inside the audio
callback. Analysis below concludes YES with comfortable margin.

## Existing Stack (No Changes Needed)

These are already integrated and validated. Listed for completeness and to clarify
exactly which existing facility covers each v1.11.0 requirement.

| Technology | Version | Existing Role | v1.11.0 Role |
|------------|---------|---------------|--------------|
| JUCE | 8.0.12 (SHA-pinned) | GUI, audio I/O, plugin formats | Audio input routing, WAV export, waveform display updates |
| libsamplerate | HEAD (SHA-pinned) | Host SR <-> 44.1 kHz SRC in plugin wrapper | Input path SRC for recording at sub-44.1 kHz target rates |
| libspu94 (C core) | current | ADPCM encode/decode, voice RAM, voice engine | Real-time ADPCM encode, voice RAM write target |
| dr_wav | vendored (CLI only) | CLI WAV I/O | NOT used -- JUCE handles WAV export in plugin/standalone |

## New Libraries Required

**None.**

## Detailed Analysis Per Feature

### 1. Real-Time ADPCM Encoding in the Audio Callback

**Existing:** `spu94_adpcm_encode_block()` in `src/spu94/spu94_adpcm_encode.c`.
Brute-force 65-combination (5 filters x 13 shifts) search per 28-sample block.
Zero heap, integer-only, deterministic. Currently called only from
`spu94_sample_encode_to_ram()` on the message thread during WAV file load.

**What changes:** Call the same encoder from the audio thread, feeding it live
input samples instead of pre-loaded PCM.

**CPU budget analysis:**

The encoder's inner loop is 28 iterations of: one multiply-add prediction
(2 int32 muls + shift + add), one quantize (shift + clamp), one reconstruction
(shift + add + clamp), one squared-error accumulate (int64 mul + add), two state
updates. That is roughly 28 x 12 = 336 integer operations per (filter, shift)
trial, times 65 trials = ~21,840 integer ops per block of 28 samples.

At 44.1 kHz, 28 samples = 0.635 ms of audio. On a modern x86 core at 3+ GHz
executing 2+ integer ops per cycle, 21,840 ops takes roughly 5-10 us -- well
under 1% of the available 635 us. Even at the lowest PS1 sample rate
(5.5125 kHz, meaning the encoder receives 1/8 as many samples after SRC
decimation), the per-block budget is 8x larger (5.08 ms) while the work per
block stays the same.

**Verdict:** The existing encoder is fast enough for real-time use. No
optimization, no reduced-search fast path, no background-thread offloading
needed. The encoder is already rt-safe (no heap, no locks, no syscalls).

**Confidence:** HIGH (verified by reading the source code; integer-only
arithmetic on a tight loop is dominated by branch prediction and cache
locality, both favorable for this code pattern).

**Integration point:** Direct use of `spu94_adpcm_encode_block()` from
the JUCE audio callback, accumulating 28-sample blocks into a small
stack-local buffer and writing encoded 16-byte blocks directly into
`voice_ram[]` at the current write offset.

**ADPCM state management:** The encoder carries state (`spu94_adpcm_state`)
across blocks. For live recording this state lives in the recording context,
initialized to {0,0} at record start, carried across consecutive blocks
until record stop.

**Sample accumulation:** The audio callback delivers blocks at the host
buffer size (commonly 64-1024 samples). After SRC to the target encode
rate, these arrive in irregular counts. An accumulator buffer of 28
int16_t samples collects incoming samples, and when full, fires one
`spu94_adpcm_encode_block()` call. The accumulator is a simple
stack-local array with an index counter -- no ring buffer, no FIFO, no
allocation.

### 2. Sample Rate Conversion on the Input Path

**Existing:** Two SRC facilities already in the project:

1. `SrcChain` (plugin path): libsamplerate Sinc-Medium, bidirectional,
   host SR <-> 44.1 kHz. Runs in processBlock. RT-safe.

2. `loadVoiceSample()`: libsamplerate `src_simple()` with `SRC_SINC_BEST_QUALITY`,
   44.1 kHz -> target encode rate. Runs on message thread.

**What changes for live recording:**

The input path for recording is: host/device audio (at host SR) -> SRC down to
target PS1 rate (44.1/22.05/11.025/5.5125 kHz) -> ADPCM encode -> voice RAM.

Two SRC stages may be needed:

- **Stage 1 (already exists):** Host SR -> 44.1 kHz. This is `SrcChain::processIn()`,
  already running. At 44.1 kHz host SR, this is the fast path (no SRC at all).

- **Stage 2 (new):** 44.1 kHz -> target encode rate. This decimation only runs
  during recording. For the four standard PS1 rates, the ratios are exact
  power-of-two divisions (44100/44100=1, 44100/22050=2, 44100/11025=4,
  44100/5512.5=8). For arbitrary pitch-register rates, ratios are non-integer.

**Recommended approach:** Use libsamplerate (already linked) with a dedicated
`SRC_STATE` for the recording decimation path. Create it at record-start,
destroy at record-stop. Use `SRC_SINC_FASTEST` quality because:

- The input is about to be ADPCM-encoded (lossy compression at 4 bits/sample),
  so Sinc-Medium/Best quality SRC is wasted effort -- the ADPCM quantization
  noise floor dominates any SRC quality difference.
- Lower quality = lower latency = less buffering complexity.
- This matches the PS1's own internal conversion quality.

For exact power-of-two ratios (the four standard PS1 rates), the SRC_SINC_FASTEST
filter is lightweight enough that there is no benefit to implementing a separate
box-average decimator. libsamplerate handles all ratios uniformly, including
arbitrary pitch-register rates, avoiding two code paths.

**SRC_STATE lifecycle:** Create on record-start (non-RT allocation is fine --
it happens once, on the message thread, before recording begins). Cache the
handle for the duration of recording. Destroy on record-stop. The existing
`SrcChain::prepare()` pattern demonstrates this lifecycle.

**Confidence:** HIGH. libsamplerate is already linked, the callback API is
used identically in the existing SrcChain, and creating additional SRC_STATE
handles is a documented operation.

### 3. Audio Input Device Selection and Routing

**Existing:** The plugin constructor already declares a stereo input bus:

```cpp
AudioProcessor(BusesProperties()
    .withInput("Input", juce::AudioChannelSet::stereo(), true)
    .withOutput("Output", juce::AudioChannelSet::stereo(), true))
```

`isBusesLayoutSupported()` accepts mono-in and stereo-in configurations.
In processBlock, the input buffer already contains live audio from the host
or standalone audio device.

**Standalone vs. Plugin differences:**

| Aspect | Standalone | DAW Plugin |
|--------|-----------|------------|
| Input source | System audio device (mic, interface) | DAW track bus (whatever is routed to the plugin) |
| Device selection | JUCE `StandalonePluginHolder` -> `AudioDeviceManager` -> system audio settings dialog | Not applicable (DAW handles routing) |
| Input availability | Always available if device has inputs | Always available (processBlock buffer has input data) |
| Settings UI | JUCE's built-in audio settings dialog (accessible via StandalonePluginHolder) | None needed |

**What changes:**

Nothing in the audio processing path. The input data is already in the
processBlock buffer. The recording logic reads from the same buffer that
currently feeds the reverb engine.

For standalone, the existing audio settings dialog (JUCE provides this
automatically for standalone plugins) already handles device selection.
No custom device-picker UI needed.

The one new GUI element: a selector for which input channel(s) to record
from (left, right, or mono-sum). This is a simple atomic<int> read in the
audio callback, same pattern as every other GUI control in the project.

**Confidence:** HIGH. The input bus is already declared and functional.
Verified by reading the constructor and isBusesLayoutSupported() source.

### 4. Threshold-Based Auto-Record Triggering

**No library needed.** This is a trivial DSP operation: compare abs(sample)
against a threshold value on each audio callback, transition from ARMED to
RECORDING when threshold is exceeded.

**Implementation pattern:**

```
State machine: IDLE -> ARMED -> RECORDING -> STOPPED
Threshold: user-configurable float, 0.0..1.0 (fraction of int16 full scale)
Detection: per-sample abs() comparison on the post-SRC input signal
```

All state lives in the recording context struct, managed by atomics for
the IDLE->ARMED and RECORDING->STOPPED transitions (message thread writes,
audio thread reads/transitions).

**Pre-roll consideration:** A small pre-roll buffer (64-128 samples at the
target encode rate) would prevent chopping the attack transient. This is a
fixed-size circular buffer of int16_t, trivially rt-safe, reset on ARM.
Whether to include pre-roll is a product decision, not a stack decision --
the implementation cost is a 56-byte array and an index counter.

**Confidence:** HIGH. Standard audio recording pattern, no library needed.

### 5. WAV File Export (Save Sample)

**Existing:** JUCE's `WavAudioFormat` and `AudioFormatWriter` are available
via `juce_audio_formats` (already linked as part of `juce_audio_utils`).

**What changes:** Add a "Save Sample" function that:

1. Reads the current ADPCM data from `voice_ram[]`
2. Decodes it back to PCM using `spu94_adpcm_decode_block()`
3. Writes to a WAV file via JUCE's `WavAudioFormat::createWriterFor()`

**Sample rate in the WAV header:** Write the encode rate (e.g., 22050 Hz).
The WAV file plays back at the rate the sample was recorded at. This is
the correct behavior -- a sample recorded at 22050 Hz should be a 22050 Hz
WAV file.

**File format:** 16-bit mono WAV. The ADPCM data in voice_ram is mono (one
channel recorded at a time). 16-bit matches the int16 PCM output of the
ADPCM decoder. No reason to upsample or expand to stereo.

**Export runs on the message thread** (triggered by button click, opens
JUCE native file dialog). No rt-safety concerns. The decode is fast
(28 integer ops per block, no brute-force search like encoding).

**Decode budget:** 512 KB voice_ram / 16 bytes per block = 32,768 blocks.
Each block decodes in ~28 integer ops = ~917K total ops. Under 1 ms on
any modern CPU. The user sees no delay.

**Pattern:**

```cpp
juce::WavAudioFormat wavFormat;
auto stream = std::make_unique<juce::FileOutputStream>(outputFile);
auto writer = wavFormat.createWriterFor(
    stream.release(), encodeSampleRate, 1, 16, {}, 0);
// write decoded PCM to writer
```

**Confidence:** HIGH. JUCE's WAV writer is well-documented and already
available in the link target. Verified via JUCE docs and the existing
WavLoader integration which uses the same `juce_audio_formats` module.

### 6. Waveform Display Updating After Recording

**Existing:** `WaveformDisplay` uses `juce::AudioThumbnail` with `addBlock()`
for progressive waveform building. Currently fed once at sample load time via
`setSample()`.

**What changes:**

**Post-recording update (recommended for v1.11.0):** After recording stops,
decode the entire voice_ram ADPCM content back to PCM, call `setSample()`
on the WaveformDisplay. Same pattern as current WAV file load.

The decode is fast: 32,768 ADPCM blocks max = 917,504 decoded samples.
At 28 integer ops per sample, this completes in under 1 ms. The decoded
PCM is also the data source for the waveform data vector that drives the
existing markers (start/end/loop) and playback position display.

**AudioThumbnail::addBlock() integration:**

```cpp
// After recording stops, on message thread:
thumbnail.reset(1, encodeSampleRate, totalDecodedFrames);
thumbnail.addBlock(0, decodedBuffer, 0, totalDecodedFrames);
```

This is exactly what the existing `setSample()` method does. The only
change is the data source (voice_ram decode instead of WAV file load).

**Live waveform update during recording (future polish):** Could call
`thumbnail.addBlock()` from a timer callback, feeding decoded chunks of
recently-encoded ADPCM. Requires a thread-safe read cursor into voice_ram.
More complex but gives real-time visualization. Not needed for v1.11.0 --
the recording fills 512KB RAM in seconds at most, so the post-recording
update is near-instant.

**Confidence:** HIGH. The existing `setSample()` method demonstrates the
exact pattern needed. Verified by reading WaveformDisplay.h source.

## What NOT to Add

| Library/Approach | Why Not |
|-----------------|---------|
| PortAudio / RtAudio | JUCE already handles all audio I/O. Adding another audio backend creates conflicts and doubles the testing surface. |
| Background thread for ADPCM encoding | The encoder is fast enough for the audio callback (~5-10 us per block vs 635 us budget). A background thread adds ring buffer complexity, latency, and thread sync for no benefit. |
| libsndfile for WAV export | JUCE's WavAudioFormat handles WAV writing. Adding libsndfile would be a third WAV library alongside dr_wav (CLI) and JUCE (plugin/standalone). |
| Custom polyphase decimator for SRC | libsamplerate handles all target rates uniformly. A custom decimator only saves CPU for exact power-of-two rates, and the saving is negligible next to ADPCM encoder cost. |
| FIFO / ring buffer between audio and encode threads | Only needed if encoding moves off the audio thread. Since encoding stays on the audio thread, the write path is voice_ram[offset++] -- no FIFO needed. |
| AudioFormatWriter::ThreadedWriter | Designed for streaming-to-disk during recording. SPU-94 records into 512KB RAM, not to disk. WAV export happens after recording stops, on the message thread. |
| APVTS integration | Existing architecture uses atomic scalar bridge (v1.7 precedent). All v1.11.0 controls (threshold, record state, channel select, encode rate) follow the same atomic pattern. |
| juce::AudioAppComponent | SPU-94 uses AudioProcessor, not AudioAppComponent. Switching would require rewriting the entire audio pipeline. The AudioProcessor processBlock already receives input audio. |

## Recording Architecture (No New Components)

```
Audio Thread (processBlock):
  host audio buffer (host SR, float)
    |
    v
  [SrcChain::processIn]  -- existing, host SR -> 44.1 kHz
    |
    v
  core-rate int16 samples (44.1 kHz)
    |
    +---> [reverb engine]  -- existing path, unmodified
    |
    +---> [record path]    -- NEW, only active when recording
           |
           v
         [input channel select]  -- L / R / mono-sum
           |
           v
         [SRC to encode rate]  -- new SRC_STATE, 44.1k -> target
           |                      (skipped when target == 44.1k)
           v
         [28-sample accumulator]  -- stack-local int16[28] + index
           |
           v (fires when 28 samples collected)
         [spu94_adpcm_encode_block]  -- existing, rt-safe
           |
           v
         [voice_ram[write_offset]]  -- existing buffer
           |
           write_offset += 16
           if write_offset >= SPU94_SPU_RAM_BYTES: stop recording

Message Thread (on record stop):
  voice_ram ADPCM data
    |
    v
  [spu94_adpcm_decode_block loop]  -- existing decoder
    |
    v
  [WaveformDisplay::setSample()]  -- existing method
```

## Recording Context Struct (New, C++ Side)

All recording state lives in a single struct inside PluginProcessor:

```cpp
struct RecordContext {
    // State machine: IDLE=0, ARMED=1, RECORDING=2, STOPPED=3
    std::atomic<int> state {0};

    // Configuration (set by message thread before ARMED)
    int    targetRate;      // encode sample rate in Hz
    float  threshold;       // auto-record threshold (0.0-1.0)
    int    channelMode;     // 0=left, 1=right, 2=mono-sum

    // Audio-thread-only state (not atomic)
    spu94_adpcm_state encState;   // encoder filter state
    int16_t  accumBuf[28];        // 28-sample accumulator
    int      accumCount;          // samples in accumulator
    uint32_t writeOffset;         // byte offset into voice_ram
    SRC_STATE* srcState;          // decimation SRC (null when rate==44100)

    // Result (written by audio thread, read by message thread after STOPPED)
    uint32_t bytesRecorded;       // total ADPCM bytes written
};
```

## Build System Impact

No changes to CMakeLists.txt dependency declarations. No new FetchContent
entries. No new link targets.

If the recording SRC logic is factored into a separate class (e.g.,
`RecordSrc.h`), it gets added to `src/plugin/CMakeLists.txt`'s
`target_sources` list -- a one-line change.

## Version Compatibility

| Dependency | Current Version | v1.11.0 Compatible | Notes |
|------------|----------------|-------------------|-------|
| JUCE | 8.0.12 | Yes | Audio input, WAV export, AudioThumbnail all stable since JUCE 5.x |
| libsamplerate | HEAD (SHA pinned) | Yes | Callback API identical for additional SRC_STATE handles |
| libspu94 | current | Yes | ADPCM encode_block is rt-safe, voice_ram directly writable |
| CMake | 3.22+ | Yes | No new CMake features needed |

## Sources

- JUCE AudioProcessor bus layout: [JUCE Bus Layouts Tutorial](https://juce.com/tutorials/tutorial_audio_bus_layouts/) -- verified current, HIGH confidence
- JUCE WavAudioFormat: [JUCE WavAudioFormat Class Reference](https://docs.juce.com/master/classjuce_1_1WavAudioFormat.html) -- verified current, HIGH confidence
- JUCE AudioThumbnail::addBlock: [JUCE AudioThumbnail Class Reference](https://docs.juce.com/master/classjuce_1_1AudioThumbnail.html) -- verified current, HIGH confidence
- JUCE AudioRecordingDemo: [JUCE AudioRecordingDemo.h on GitHub](https://github.com/juce-framework/JUCE/blob/master/examples/Audio/AudioRecordingDemo.h) -- reference pattern for ThreadedWriter (not used, but evaluated), HIGH confidence
- JUCE StandalonePluginHolder: [JUCE StandalonePluginHolder Class Reference](https://docs.juce.com/master/classStandalonePluginHolder.html) -- verified AudioDeviceManager integration, HIGH confidence
- JUCE AudioDeviceManager: [JUCE AudioDeviceManager Tutorial](https://docs.juce.com/master/tutorial_audio_device_manager.html) -- verified standalone input selection, HIGH confidence
- libsamplerate API: verified via existing SrcChain.cpp source (lines 115-120, callback mode), HIGH confidence
- spu94_adpcm_encode_block: verified via source reading of src/spu94/spu94_adpcm_encode.c (65-combination brute force, integer-only), HIGH confidence
- spu94_sample_encode_to_ram: verified via source reading of src/spu94/spu94_sample_loader.c (message-thread-only loader), HIGH confidence
- spu94_voice_mixer_t: verified via include/spu94/spu94_voice.h (voice_ram[SPU94_SPU_RAM_BYTES] directly accessible), HIGH confidence
- Existing loadVoiceSample: verified via src/plugin/PluginProcessor.cpp lines 1918-1992 (SRC + encode + waveform stash pattern), HIGH confidence
