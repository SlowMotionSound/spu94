# Domain Pitfalls: v1.11.0 Live Input Sampling

**Domain:** Real-time audio recording into a fixed-size ADPCM sampler engine
**Researched:** 2026-05-28
**Overall confidence:** HIGH (verified against existing codebase + established real-time audio patterns)

---

## Critical Pitfalls

Mistakes that cause audible glitches, data corruption, or rewrites.

---

### Pitfall 1: ADPCM Encoder on the Audio Thread

**What goes wrong:** The existing `spu94_adpcm_encode_block` brute-forces 65 (filter, shift) combinations per 28-sample block. Each combination runs 28 inner-loop iterations with int64 multiply-accumulate. At 44.1 kHz, a 128-sample audio callback has a 2.9 ms deadline. One encode call for 28 samples is fast in isolation, but stacking 4-5 blocks per callback (128 / 28) plus the reverb engine plus voice playback will blow the deadline on some hosts, especially at low buffer sizes (64 samples = 1.45 ms).

**Why it happens:** The encoder was designed for batch processing (WAV load on message thread). It has no concept of time budgets. Attempting to call it directly from `processBlock` treats an offline function as a real-time one.

**Consequences:** Audio dropouts (glitches, clicks, silence gaps) during recording. pluginval strictness-7 failures. Some hosts will kill the plugin for missed deadlines.

**Prevention:** Architecture research already determined this: run the ADPCM encoder on a dedicated background thread fed by a lock-free SPSC ring buffer from the audio callback. The audio thread only performs a memcpy into the ring buffer (bounded, predictable cost). The encoder thread pulls PCM data, encodes, and writes ADPCM blocks into voice RAM at its own pace.

**Detection:** pluginval at low buffer sizes (64/128 samples). The existing `rt_bench_latency` test can be extended to include a recording path stress test.

**Phase relevance:** Must be solved in the very first recording implementation phase. Cannot be deferred.

---

### Pitfall 2: Ring Buffer Overrun Silently Drops Audio

**What goes wrong:** If the ADPCM encoder thread falls behind the audio callback's production rate, the SPSC ring buffer fills up. Without explicit handling, the audio thread either (a) blocks waiting for space (real-time safety violation), (b) overwrites unread data (corrupts encoder state), or (c) drops samples silently (gap in recording the user never knows about).

**Why it happens:** The encoder's worst-case throughput varies with input signal complexity (some signals take more iterations to find optimal filter/shift). CPU scheduling delays on the encoder thread (preemption, thermal throttling) create bursts of backpressure. The ring buffer is sized for average throughput, not worst case.

**Consequences:** Option (a) causes audio glitches. Option (b) produces corrupted ADPCM with decoder state discontinuities (clicks, tonal artifacts). Option (c) produces a shorter-than-expected sample with a gap the user cannot hear during recording but discovers on playback.

**Prevention:**
- Size the ring buffer generously: at least 1 second of PCM at the highest supported rate (44100 samples * 2 bytes = ~86 KB). This is separate from the 512 KB voice RAM.
- When the ring buffer is full, the audio thread returns false (write fails) and increments an atomic overflow counter. Do NOT block, do NOT overwrite.
- The GUI polls the overflow counter and displays a warning ("Recording buffer overflow -- encoder fell behind"). The recording continues with the gap.
- Consider signaling the encoder thread with a semaphore or condition variable wake-up (NOT from the audio thread -- use a lightweight atomic flag the encoder thread polls with a short sleep, or better, a platform event/futex that does not involve the audio thread in the wait).

**Detection:** Stress test: record while all 24 voices play simultaneously + reverb + DAC model. Monitor the overflow counter.

**Phase relevance:** Same phase as ring buffer implementation. The overflow counter and GUI warning should ship in the first recording phase.

---

### Pitfall 3: Encoder State Discontinuity at Block Boundaries

**What goes wrong:** ADPCM encoding is stateful -- each block's prediction depends on the previous block's final two reconstructed samples (`state->old`, `state->older`). If the ring buffer drains in non-block-aligned chunks, or if the encoder thread is interrupted mid-block and restarted, the state chain breaks. The result is a click or tonal discontinuity at the block boundary.

**Why it happens:** The existing batch encoder in `spu94_sample_encode_to_ram` processes all blocks sequentially with a single `spu94_adpcm_state` struct carried across blocks. The background encoder thread must replicate this exact carry pattern, but the ring buffer's granularity is arbitrary (audio callbacks produce 64-1024 samples, not multiples of 28).

**Consequences:** Audible clicks at ADPCM block boundaries in the recorded sample. The clicks are baked into voice RAM permanently.

**Prevention:**
- The encoder thread must accumulate PCM samples until it has a full 28-sample block before calling `spu94_adpcm_encode_block`. Use a small staging buffer (28 int16_t samples) as an accumulator.
- Never encode a partial block mid-stream. Only encode partial (zero-padded) for the very last block when recording stops.
- The `spu94_adpcm_state` struct must persist across the encoder thread's entire recording session, never reset mid-recording.
- Zero-initialize the state at recording start (matching `spu94_sample_encode_to_ram` behavior).

**Detection:** Golden-file regression: encode a known PCM buffer via the batch encoder, then again via the streaming encoder path, and bit-compare the ADPCM output. They must be identical.

**Phase relevance:** Core encoder-thread architecture phase. The 28-sample accumulator is a design requirement, not an optimization.

---

### Pitfall 4: Recording State Machine Races Between Threads

**What goes wrong:** Recording has at least four state transitions: IDLE -> ARMED -> RECORDING -> STOPPING -> IDLE (or ARMED -> RECORDING for manual start). The GUI thread initiates transitions (button press, threshold arm). The audio thread produces samples. The encoder thread consumes them. If these three threads read/write recording state without a coherent protocol, you get:
- GUI shows "Recording" while audio thread hasn't started feeding the ring buffer yet.
- Audio thread continues feeding the ring buffer after the encoder thread thinks recording stopped.
- Encoder thread writes past the end of voice RAM because it didn't see the stop signal.

**Why it happens:** Multiple threads sharing mutable state without a clear ownership protocol. The existing codebase uses atomic booleans for simpler state (e.g., `pendingGuiStop`, `voiceSampleLoaded`), but a multi-state recording machine is more complex than a single flag.

**Consequences:** Orphaned data in the ring buffer. Corrupted final ADPCM block (wrong flags). Voice RAM written past bounds. GUI out of sync with actual recording state.

**Prevention:**
- Use a single atomic enum for recording state (IDLE, ARMED, RECORDING, STOPPING). Transitions follow a strict state machine:
  - GUI thread sets ARMED or STOPPING (never RECORDING directly).
  - Audio thread transitions ARMED -> RECORDING on first sample (or threshold trigger).
  - Audio thread transitions RECORDING -> STOPPING when it sees the stop flag or RAM is full.
  - Encoder thread transitions STOPPING -> IDLE after draining the ring buffer and writing the final block.
- Each thread only writes states it owns: GUI owns IDLE->ARMED and RECORDING->STOPPING requests. Audio thread owns ARMED->RECORDING detection. Encoder thread owns STOPPING->IDLE finalization.
- Use `std::memory_order_release` on writes and `std::memory_order_acquire` on reads for the state enum.

**Detection:** State machine unit test that simulates rapid start/stop/start sequences. Fuzz test with random timing between GUI, audio, and encoder threads.

**Phase relevance:** Recording state machine must be designed before any recording code is written. Retrofitting a state machine onto ad-hoc booleans is a rewrite.

---

### Pitfall 5: Voice RAM Overflow During Recording

**What goes wrong:** The encoder thread writes ADPCM blocks into `mixer->voice_ram` at increasing offsets. If it doesn't track the write position against `SPU94_SPU_RAM_BYTES` (512 KB = 0x80000), it writes past the end of the buffer. Unlike batch encoding where the total is known upfront, real-time recording has an unknown duration.

**Why it happens:** The batch path in `spu94_sample_encode_to_ram` does a single upfront bounds check (`bytes_needed > ram_size - ram_offset`). The streaming path can't do this because total size is unknown. Without per-block bounds checking, the encoder keeps writing.

**Consequences:** Buffer overwrite -> memory corruption. On the PS1, this would corrupt reverb delay lines (shared RAM). In SPU-94 with separate buffers, it still corrupts whatever is adjacent in memory.

**Prevention:**
- Before writing each ADPCM block, check: `write_offset + SPU94_ADPCM_BLOCK_BYTES <= SPU94_SPU_RAM_BYTES`. If false, stop recording immediately.
- Transition to STOPPING state. Set the final block's flag byte to `SPU94_VAG_FLAG_END` (or `END | REPEAT` if loop is enabled).
- Report the "RAM full" stop reason to the GUI via an atomic flag so it can display "Recording stopped -- RAM full".
- Calculate and display remaining recording time in the GUI: `remaining_seconds = (SPU94_SPU_RAM_BYTES - current_offset) / (SPU94_ADPCM_BLOCK_BYTES * blocks_per_second)`.

**Detection:** Test: record at 44.1 kHz until RAM fills. Verify the last block has correct END flag. Verify no bytes were written past 0x80000. Verify the sample plays back correctly including loop-back.

**Phase relevance:** Same phase as encoder thread. The per-block bounds check is a one-liner but forgetting it is catastrophic.

---

### Pitfall 6: GUI Waveform Updates Block the Audio Thread

**What goes wrong:** During recording, the waveform display needs to show incoming audio in real-time. The naive approach copies decoded PCM from the encoder thread into `waveformData` (a `std::vector<int16_t>`) and triggers a GUI repaint. If the GUI thread reads `waveformData` while the encoder thread is appending to it, you get a data race. If you add a mutex, the audio thread (which feeds the encoder) might contend with the GUI thread, causing audio glitches.

**Why it happens:** The existing codebase sets `waveformData` once during `loadVoiceSample` (message thread, no contention). Live recording creates a continuous producer-consumer relationship between the encoder thread and the GUI thread that didn't exist before.

**Consequences:** Data races (undefined behavior). Mutex contention causing audio glitches. GUI showing stale or corrupted waveform.

**Prevention:**
- The encoder thread writes decoded preview samples (the reconstructed output from `spu94_adpcm_encode_block`'s internal decoder) into a separate preview ring buffer.
- The GUI timer callback (30 Hz, already exists for playback position updates) reads from this preview ring buffer and appends to its own local waveform buffer for drawing.
- The encoder thread and GUI thread never share the same `std::vector`. The ring buffer is the only shared data structure, and it's SPSC (encoder produces, GUI consumes).
- The waveform display can downsample for drawing (show min/max per pixel column), so the preview buffer can be much smaller than full-resolution audio.

**Detection:** Run with ThreadSanitizer (TSAN) during a recording session. Verify zero data races reported.

**Phase relevance:** GUI update mechanism should be designed alongside the encoder thread, not bolted on afterward.

---

## Moderate Pitfalls

Mistakes that cause incorrect behavior or poor user experience but don't corrupt data.

---

### Pitfall 7: Input Monitoring Feedback Loop

**What goes wrong:** In standalone mode, SPU-94 routes audio input through the reverb/sampler engine to audio output. If the user's speakers feed back into the microphone, adding input monitoring during recording creates a feedback loop. JUCE's standalone wrapper has built-in feedback detection on iOS/Android but not on desktop.

**Why it happens:** Desktop standalone apps get full control of audio I/O via `AudioDeviceManager`. There is no host to manage routing. If input and output use the same device (e.g., built-in laptop audio), monitoring creates a direct feedback path.

**Prevention:**
- Input monitoring should be a toggle, OFF by default. When recording starts, the input is captured but not necessarily routed to output.
- If monitoring IS enabled, add a limiter/gate on the monitoring path (not on the recording path -- the recording should capture the raw input). Even a simple hard clip at 0 dBFS prevents runaway feedback.
- Display a warning when input and output device are the same and monitoring is enabled: "Monitoring with speakers may cause feedback. Use headphones."
- In plugin mode, the DAW handles monitoring -- the plugin should NOT add its own monitoring path.

**Detection:** Manual test: enable monitoring with built-in speakers. Verify no runaway feedback. Verify the limiter engages.

**Phase relevance:** Input monitoring is a later convenience feature, but the recording path must be designed so monitoring can be added without restructuring.

---

### Pitfall 8: SRC Quality Mismatch Between Offline and Real-Time Paths

**What goes wrong:** The existing `loadVoiceSample` uses `src_simple` with `SRC_SINC_BEST_QUALITY` for offline WAV loading. Live recording will use a streaming SRC (either libsamplerate's `src_process` or `src_callback_read`). The quality modes have different SNR and bandwidth characteristics (BEST = 144 dB SNR / 96% BW, MEDIUM = 121 dB / 90%). A sample loaded from a file and the same audio recorded live will sound subtly different.

**Why it happens:** Offline encoding can afford the highest quality SRC. Real-time SRC must meet the audio callback deadline. The quality gap is small but audible on high-frequency content.

**Consequences:** User records a sample, then loads the same audio from a WAV file, and they sound different. This breaks the mental model that "recording == loading the same audio."

**Prevention:**
- Use `SRC_SINC_MEDIUM_QUALITY` for the recording path (matches what the existing `SrcChain` uses for playback -- consistency is more important than maximum quality).
- Document the quality difference in an ADR if a different quality level is chosen.
- The real SRC concern is the TARGET rate, not the SRC quality: recording at PS1 rates (22.05 kHz, 11.025 kHz, 5.5125 kHz) means downsampling from the host rate (44.1/48/96 kHz). This SRC runs on the audio thread or encoder thread, NOT in the audio callback if it would blow the deadline.

**Detection:** Record a sweep tone at each PS1 target rate. Compare spectral content with the same sweep loaded from a WAV file. The cutoff frequency should be within 1% of Nyquist for the target rate.

**Phase relevance:** SRC integration phase. The quality level decision should be made early and documented.

---

### Pitfall 9: Threshold Trigger False Positives and Missed Triggers

**What goes wrong:** Threshold-triggered auto-record starts recording when the input signal exceeds a user-set level. Without hysteresis, noise near the threshold causes rapid start/stop oscillation ("chatter"). Without a hold-off period, a loud transient triggers recording, then a brief dip below threshold stops it immediately, then the next transient starts a new recording -- fragmenting what should be one continuous sample.

**Why it happens:** Simple threshold comparison (`abs(sample) > threshold`) is a comparator without hysteresis. Real audio signals are noisy and have fast transients followed by decays.

**Consequences:** False triggers: recording starts on noise, wastes RAM. Missed triggers: threshold set too high, nothing records. Fragmented recordings: rapid start/stop produces many tiny unusable samples.

**Prevention:**
- Implement Schmitt trigger hysteresis: arm threshold (higher) to start recording, disarm threshold (lower) to allow re-arming. Use a fixed ratio (e.g., disarm = arm - 6 dB).
- Add a minimum recording duration hold-off: once recording starts, it continues for at least N milliseconds (e.g., 100 ms) regardless of signal level. This prevents transient-dip fragmentation.
- Operate threshold detection on RMS over a short window (e.g., 10 ms), not on individual samples. Individual sample peaks are noisy; RMS is stable.
- Display the current input level on a meter so the user can set the threshold visually. This is far more usable than guessing a number.
- "Pre-roll" buffer: keep a small circular buffer of the last N ms of input. When threshold triggers, prepend the pre-roll to the recording. This captures the attack transient that triggered the threshold, which otherwise gets clipped because the trigger fires AFTER the transient.

**Detection:** Test with white noise at various levels near the threshold. Count false triggers per minute. With hysteresis and RMS: should be zero. Without: will be many.

**Phase relevance:** Threshold detection is a separate sub-feature from manual recording. Can be a later phase, but the pre-roll buffer design impacts the ring buffer architecture (it must support rewinding the read pointer or maintaining a separate pre-roll ring).

---

### Pitfall 10: Host Rate != Recording Target Rate -- Double SRC

**What goes wrong:** The host runs at 48 kHz. The user wants to record at 22.05 kHz (PS1 standard rate). The naive approach: SRC from 48 kHz to 44.1 kHz (existing SrcChain), then SRC from 44.1 kHz to 22.05 kHz (recording target). Two cascaded SRCs degrade quality more than one direct SRC from 48 kHz to 22.05 kHz.

**Why it happens:** The existing architecture converts everything to 44.1 kHz at the plugin boundary (SrcChain), because the SPU core runs at 44.1 kHz. Recording at a sub-44.1 kHz rate requires a SECOND downsampling step. Cascading two SRCs is architecturally convenient but acoustically wasteful.

**Consequences:** Slightly degraded high-frequency content. Extra CPU load from two SRC passes. Potential phase/latency artifacts from cascaded filters.

**Prevention:**
- For recording, the SRC from host rate to target rate should happen in the encoder thread, not the audio thread. The audio thread pushes host-rate (or 44.1 kHz post-SrcChain) PCM into the ring buffer. The encoder thread does the final downsampling to the target rate before ADPCM encoding.
- This means the ring buffer carries audio at the SrcChain output rate (44.1 kHz or host rate in fast-path mode), and the encoder thread does one SRC to the target rate plus ADPCM encoding. Total: one SRC in the encoder thread (or two if host != 44.1 kHz, but the first is already in the audio path for the reverb engine).
- Accept the double-SRC as the correct architecture: the first SRC (host->44.1 kHz) is mandatory for the reverb engine regardless. The second (44.1 kHz -> target) is recording-specific. The alternative (bypassing SrcChain for recording) would require a separate input tap before the SrcChain, adding complexity.
- Use SRC_SINC_MEDIUM_QUALITY for the recording SRC. The PS1's own DAC introduces far more coloration than the SRC quality difference.

**Detection:** Spectrogram comparison: record a sweep at 22.05 kHz via double-SRC path. Compare with direct offline SRC from the same source file. Verify the aliasing mirror frequency is correct.

**Phase relevance:** Encoder thread design phase. The ring buffer data format (host-rate vs 44.1 kHz) must be decided upfront.

---

### Pitfall 11: Plugin vs. Standalone Input Routing Asymmetry

**What goes wrong:** In standalone mode, audio input comes from the system's audio device (selected via `AudioDeviceManager`). The user picks a specific input. In plugin mode, audio input comes from the DAW's bus routing. The plugin currently declares stereo input (line 196 of PluginProcessor.cpp: `.withInput("Input", juce::AudioChannelSet::stereo(), true)`). But:
- Not all DAW configurations route audio to the plugin's input bus (e.g., instrument tracks vs. effect tracks).
- The DAW may provide mono input on an insert channel.
- Some hosts don't enable the input bus by default (Ableton Live requires explicit routing).
- JUCE's standalone wrapper uses the same `processBlock` buffer for both input and output, meaning the input buffer IS the output buffer. Writing recording data from it after the reverb engine has already written output into it overwrites the input.

**Why it happens:** SPU-94 was designed as an effect processor (input -> reverb -> output). Recording adds a second consumer of the input signal. The plugin architecture assumes one consumer.

**Consequences:** Recording captures the processed output instead of the raw input. Plugin mode shows no audio input on some DAW configurations. Standalone mode captures correct input but only if the tap point is before processBlock modifies the buffer.

**Prevention:**
- Tap the input BEFORE any processing in `processBlock`. Copy the raw input into a staging buffer (or directly into the recording ring buffer) as the very first operation. The existing `inputGainScratch_` pattern shows how to do this safely.
- In plugin mode, check `getTotalNumInputChannels()` and handle mono gracefully (duplicate to stereo for the ring buffer, or record mono).
- Add a "No Input" indicator in the GUI when the input bus is silent (all zeros for > 100 ms). This helps users diagnose routing issues.
- Document that in plugin mode, the DAW must route audio to the plugin's input bus for recording to work. This is a DAW configuration issue, not a plugin bug.

**Detection:** Test in at least three DAWs (Reaper, Ableton, Ardour/Mixbus). Verify input signal reaches the recording path in each. Test mono and stereo input configurations.

**Phase relevance:** Must be handled in the initial recording architecture. The input tap point in processBlock is a load-bearing decision.

---

### Pitfall 12: WAV Export on the Audio Thread

**What goes wrong:** After recording, the user clicks "Save Sample" to export the recorded ADPCM as a WAV file. Writing a file involves `fopen`, `fwrite`, `fclose` -- all syscalls. If any part of this export path touches the audio thread (e.g., triggered by a callback that runs on the audio thread), it violates real-time safety.

**Why it happens:** In the existing codebase, file I/O happens on the message thread (e.g., `loadVoiceSample` calls `WavLoader::load` which uses JUCE's `AudioFormatReader`). But if the export function is carelessly wired to an audio-thread callback, or if it reads from `mixer->voice_ram` while the audio thread is using it for playback, you get either RT violations or data races.

**Consequences:** Audio glitches during export. Potential data race if exporting while playing back the same sample.

**Prevention:**
- Export is message-thread only. The GUI button handler calls the export function directly on the message thread.
- Use JUCE's `AudioFormatWriter::ThreadedWriter` if streaming large exports, or simply write the entire file synchronously on the message thread (512 KB max = trivial for modern I/O).
- If the sample is actively playing during export, take a snapshot: copy the voice RAM region to a temporary buffer on the message thread, then write from the copy. The voice RAM is read-only during playback (the audio thread reads but never writes), so a memcpy from the message thread is safe as long as the audio thread doesn't modify the same region simultaneously (which it doesn't in the current architecture -- voice RAM is written only during loading/recording, not during playback).
- For export format: decode the ADPCM back to PCM first, then write as WAV. This gives the user an interoperable file. Optionally also offer raw VAG/ADPCM export for PS1 toolchain interop.

**Detection:** Run with TSAN during an export-while-playing scenario. Verify zero races. Verify the exported WAV matches a golden decode of the voice RAM content.

**Phase relevance:** Export is a later phase, but the voice RAM access pattern must be designed to allow safe concurrent read (playback) and sequential copy (export).

---

## Minor Pitfalls

Issues that cause user confusion or suboptimal results but don't break functionality.

---

### Pitfall 13: Recording Duration Display Shows Wrong Time

**What goes wrong:** The GUI shows elapsed recording time. But the elapsed time depends on the sample rate: 512 KB of ADPCM at 44.1 kHz is ~9.3 seconds, at 22.05 kHz it's ~18.6 seconds. If the display calculates from wall-clock time instead of from ADPCM blocks written * samples-per-block / target-sample-rate, it drifts.

**Why it happens:** Wall-clock time and audio-clock time diverge because: (a) the encoder thread may lag behind real-time, (b) the audio device clock and system clock aren't the same clock.

**Prevention:** Derive recording time from `blocks_written * 28 / target_sample_rate`, not from a wall-clock timer. Display both the time and the RAM usage percentage.

**Phase relevance:** GUI phase.

---

### Pitfall 14: Existing adpcmStateCache Invalidation

**What goes wrong:** The existing `loadVoiceSample` builds an `adpcmStateCache` (decoder state at each block boundary) for waveform marker positioning. Live recording must also build this cache, but incrementally (one block at a time as each block is encoded). If the cache isn't extended during recording, waveform markers will be wrong for the recorded portion.

**Why it happens:** The batch path builds the entire cache in one loop after encoding. The streaming path must append to the cache after each block.

**Prevention:** The encoder thread appends the pre-encode decoder state to the cache after each block. Use a lock-free mechanism (e.g., a separate SPSC queue for state snapshots) or build the cache from the encoder thread and let the GUI thread read the committed portion (write index is atomic, reads are always behind writes).

**Phase relevance:** Waveform display integration phase.

---

### Pitfall 15: ADPCM Flag Byte Management During Recording

**What goes wrong:** ADPCM blocks have a flag byte (byte 1) that controls loop behavior: 0x00 = normal, 0x01 = END, 0x02 = LOOP_START, 0x03 = END|REPEAT. During batch encoding, the last block gets END (and optionally REPEAT). During streaming, you don't know which block is last until recording stops. If you write 0x00 for every block during recording, the last block won't have END. If the user plays the sample before the flag is patched, the voice engine reads past the end of valid data.

**Why it happens:** The voice engine trusts flag bytes. A missing END flag means "keep reading the next block." Past the recorded data, voice RAM contains either zeros (silence, harmless) or stale data from a previous recording (noise, harmful).

**Prevention:**
- Strategy: "last-block rewrite." Each time a new block is written, go back and ensure the PREVIOUS block has flags 0x00 (normal). Then write the new block with flags 0x01 (END). When the next block arrives, rewrite the current block to 0x00 and write the new one with 0x01.
- This way, the sample is ALWAYS playable -- even if recording is interrupted abruptly, the last written block has END set.
- When recording completes normally, patch the final block with the correct loop flags if loop mode is enabled.
- The "rewrite previous block" is a single byte write (`voice_ram[prev_block_offset + 1] = 0x00`). The audio thread reads voice RAM for playback, but byte 1 of a block that was already decoded won't be re-read until the voice loops back (and by then the flag is correct).

**Detection:** Test: start recording, record 3 blocks, hard-kill the encoder thread. Play the sample. It should play 3 blocks and stop cleanly (END flag on block 3).

**Phase relevance:** Encoder thread implementation phase. The flag management strategy is a few lines of code but must be correct from the start.

---

### Pitfall 16: Pre-roll Buffer Complicates Ring Buffer Design

**What goes wrong:** The threshold-triggered auto-record feature needs a pre-roll buffer (last N ms of audio before the threshold event) to capture the attack transient. This means the ring buffer must support "rewinding" -- when the threshold fires, the last N ms of data already in the ring buffer should be fed to the encoder. But in a standard SPSC ring buffer, the consumer's read pointer only moves forward.

**Why it happens:** A pre-roll buffer is conceptually a separate circular buffer from the recording ring buffer. If you try to implement both in a single ring buffer, the read semantics become complex (sometimes the consumer skips data, sometimes it rewinds).

**Prevention:**
- Use TWO ring buffers:
  1. Pre-roll ring buffer: small (e.g., 50 ms = ~2205 samples at 44.1 kHz = ~4.3 KB). Continuously written by the audio thread, overwriting old data. Circular, no consumer.
  2. Recording ring buffer: standard SPSC. Audio thread produces, encoder thread consumes.
- When threshold triggers: copy the entire pre-roll ring buffer into the recording ring buffer (one bulk write), then switch to normal continuous writing.
- The pre-roll ring buffer is always active while ARMED. The recording ring buffer is active only while RECORDING.

**Detection:** Record a drum hit with threshold trigger. Verify the attack transient is fully captured (not truncated at the trigger point).

**Phase relevance:** Threshold trigger phase. Can be designed independently from manual recording, but the ring buffer architecture should leave room for it.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Ring buffer + encoder thread | Pitfall 1 (encoder on audio thread), Pitfall 2 (overrun), Pitfall 3 (block alignment), Pitfall 5 (RAM overflow) | Design the ring buffer, encoder thread, 28-sample accumulator, and per-block bounds check as one coherent unit. Don't ship partial solutions. |
| Recording state machine | Pitfall 4 (state races) | Define the state enum and ownership rules BEFORE writing any recording code. Test the state machine in isolation with a mock encoder. |
| Input routing | Pitfall 11 (plugin vs standalone), Pitfall 7 (feedback) | Tap input at the top of processBlock before any processing. Test in plugin AND standalone. |
| SRC for recording | Pitfall 8 (quality mismatch), Pitfall 10 (double SRC) | Put recording SRC in the encoder thread, not the audio thread. Use MEDIUM quality. Accept the double-SRC trade-off. |
| Threshold trigger | Pitfall 9 (false triggers), Pitfall 16 (pre-roll) | Implement hysteresis + RMS + hold-off. Use a separate pre-roll ring buffer. |
| GUI updates during recording | Pitfall 6 (waveform blocks audio), Pitfall 13 (time display) | Separate preview ring buffer for GUI. Derive time from block count, not wall clock. |
| WAV export | Pitfall 12 (export on audio thread) | Message-thread only. Snapshot voice RAM if playback is active. |
| ADPCM encoding correctness | Pitfall 3 (state discontinuity), Pitfall 15 (flag bytes) | 28-sample accumulator with persistent state. Last-block-rewrite flag strategy. Golden-file regression test vs batch encoder. |
| adpcmStateCache | Pitfall 14 (cache invalidation) | Incremental cache append from encoder thread. Atomic write index. |

---

## Integration Pitfalls (SPU-94-Specific)

These are pitfalls specific to integrating live recording into the existing SPU-94 architecture.

### The Existing `loadVoiceSample` Path Must Coexist

The file-load path (`loadVoiceSample`) and the live-record path both write to `mixer->voice_ram`. They must not run simultaneously. The recording state machine must prevent loading a WAV while recording is active, and vice versa. Use the recording state: if state != IDLE, disable the Load button in the GUI.

### Voice Playback During Recording is a Read-Write Conflict

The audio thread reads `mixer->voice_ram` for voice playback. The encoder thread writes to `mixer->voice_ram` during recording. These accesses are to DIFFERENT regions (playback reads previously-recorded data from offset 0; recording writes new data at increasing offsets). But if the user triggers playback of the currently-recording sample (partial playback), the voice engine might read blocks that the encoder thread is in the middle of writing. Prevention: either (a) prohibit playback during recording, or (b) use the ADPCM flag management (Pitfall 15) to ensure the voice engine stops at the last fully-written block.

### The 6 RT-Safety Gates Must Not Regress

The existing rt_safety test suite (rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency, hotpath_alloc_gate, hotpath_alloc_gate_with_malloc) validates the C core hot path. The recording feature adds C++ code in the JUCE layer, not the C core. But if any recording helper is accidentally linked into libspu94 (e.g., a ring buffer utility), it will show up in the `nm -u` gate. Keep all recording infrastructure in the JUCE layer, not in libspu94.

### Encoder Thread Must Not Be Created on the Audio Thread

`std::thread` construction calls `pthread_create`, which is a syscall. The encoder thread must be created in `prepareToPlay` (message thread), not in `processBlock` (audio thread). Similarly, joining or destroying the thread must happen in `releaseResources`, not in the audio callback.

---

## Sources

- [LMMS real-time recording with ring buffer PR](https://github.com/LMMS/lmms/pull/7903) -- real-world implementation of the same pattern
- [Using locks in real-time audio processing, safely](https://timur.audio/using-locks-in-real-time-audio-processing-safely) -- mutex vs spinlock vs lock-free analysis
- [Four common mistakes in audio development](https://atastypixel.com/four-common-mistakes-in-audio-development/) -- classic reference on RT audio pitfalls
- [SPSCQueue lock-free ring buffer](https://github.com/rigtorp/SPSCQueue) -- reference SPSC implementation with cache-line padding
- [ringbuf C11 atomics SPSC](https://github.com/szanni/ringbuf) -- C11-compatible SPSC suitable for the C core if needed
- [JUCE feedback loop warning](https://forum.juce.com/t/audio-input-is-muted-to-avoid-feedback-loop/52851) -- JUCE's built-in feedback detection
- [JUCE AudioFormatWriter::ThreadedWriter](https://docs.juce.com/master/classAudioFormatWriter_1_1ThreadedWriter.html) -- background file writing
- [libsamplerate FAQ](https://libsndfile.github.io/libsamplerate/faq.html) -- streaming vs batch SRC caveats
- [Schmitt trigger hysteresis for noise-free switching](https://resources.pcb.cadence.com/blog/2021-schmitt-trigger-hysteresis-provides-noise-free-switching-and-output) -- hysteresis design for threshold detection
- SPU-94 codebase: `spu94_adpcm_encode.c`, `spu94_sample_loader.c`, `SrcChain.cpp`, `PluginProcessor.cpp` -- existing architecture analyzed directly
