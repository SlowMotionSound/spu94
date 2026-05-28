# Feature Landscape: v1.11.0 Live Input Sampling

**Domain:** Hardware-faithful PSX sampler with live audio recording into 512KB ADPCM voice RAM
**Researched:** 2026-05-28
**Overall confidence:** HIGH (hardware sampler patterns well-documented; JUCE audio input verified via Context7; existing codebase integration points confirmed via direct inspection)

---

## How Live Recording Fits the Existing Sampler

The v1.8-v1.10 sampler path currently works like this:

```
[WAV file on disk]
       |
       v
[WavLoader: SRC to 44.1kHz int16, channel adapt]
       |
       v
[spu94_sample_encode_to_ram: brute-force ADPCM encode]
       |
       v
[voice_ram: 512KB ADPCM blocks]
       |
       v
[spu94_voice_tick: decode, Gauss interp, ADSR, volume, sweep]
       |
       v
[mixer accumulator -> reverb -> output]
```

Live recording replaces the top of this chain: instead of loading a WAV file, audio arrives from the JUCE audio input in real time, gets downsampled to the target rate, ADPCM-encoded, and written to voice RAM. Everything below `voice_ram` is unchanged.

```
[Audio input from processBlock]
       |
       v
[SRC: host rate -> target sample rate]
       |
       v
[PCM staging buffer (accumulates during recording)]
       |
       v  (on stop)
[spu94_sample_encode_to_ram: same encoder as WAV path]
       |
       v
[voice_ram: 512KB ADPCM blocks]  <-- existing playback path unchanged
```

---

## Table Stakes

Features users expect from any sampler with live recording. Missing = product feels incomplete.

### 1. Manual Record (Start/Stop)

| Aspect | Detail |
|--------|--------|
| **Why expected** | Every sampler since the Fairlight CMI (1979) has a record button. Akai S1000: press ARM then START. MPC: tap ARM then RECORD. Digitakt: shift-tap SAMPLING. Without this the feature does not exist. |
| **Complexity** | Low |
| **Dependencies** | JUCE audio input callback (already receives input in `processBlock` on both standalone and plugin paths), write path to voice RAM |
| **What to build** | Recording state machine: IDLE -> RECORDING -> STOPPED. A single button toggles IDLE->RECORDING and RECORDING->STOPPED. Audio thread accumulates incoming PCM into a staging buffer. On STOPPED, run the existing `spu94_sample_encode_to_ram` encoder on a worker thread. |
| **Hardware reference** | The Akai S1000 has three start modes: INPUT LEVEL (threshold), MIDI, and FOOTSWITCH. Manual start is the simplest -- press the button, recording begins. SPU-94 starts here. |

### 2. Input Level Meter

| Aspect | Detail |
|--------|--------|
| **Why expected** | The S1000 shows a VU-style meter on its REC screen with the warning "if the signal level touches the top line you may get distortion." The MPC shows level bars. The Digitakt shows input meters. Recording without seeing the level is blind. |
| **Complexity** | Low |
| **Dependencies** | Existing `inputLevel` atomic in PluginProcessor; audio data already flows through `processBlock` |
| **What to build** | Peak-hold meter in the sampler window. Track peak amplitude of the input signal in the audio callback (single `std::atomic<float>`). Display as a vertical bar with clip indicator. No new DSP needed -- just observe what is already there. |
| **Hardware reference** | Every hardware sampler since the S900 has had an input level display. The S1000 added a REC LEVEL knob for analog gain staging. SPU-94 already has an Input Gain control that serves this purpose. |

### 3. RAM Usage / Remaining Time Display

| Aspect | Detail |
|--------|--------|
| **Why expected** | 512KB is finite. The S1000 shows "xx.xx seconds remaining" on its REC page. The MPC shows a sample length counter. The SP-1200 has 10 seconds total. Users need to know how much space is left and how much time they can record. |
| **Complexity** | Low |
| **Dependencies** | `ramUsed` atomic already exists in PluginProcessor; sample rate determines time-per-byte conversion |
| **What to build** | Numeric readout or progress bar showing: (a) bytes used / total bytes, (b) seconds recorded, (c) seconds remaining at current sample rate. Math: ADPCM blocks are 16 bytes for 28 samples. At sample rate R, one second uses R/28 blocks * 16 bytes. At 22.05kHz: 22050/28 * 16 = ~12,600 bytes/second. 512KB / 12,600 = ~41 seconds. |
| **Recording time at each preset rate** | 44.1kHz: ~20.7s, 22.05kHz: ~41.4s, 11.025kHz: ~82.8s, 5.5125kHz: ~165.6s |

### 4. Auto-Stop on RAM Full

| Aspect | Detail |
|--------|--------|
| **Why expected** | When the buffer fills, recording must stop cleanly. The SP-1200 stops at 10 seconds. The S1000 stops at its memory limit. The Digitakt stops at 6:06. Overrunning the buffer would corrupt data or crash. Not optional. |
| **Complexity** | Low |
| **Dependencies** | RAM bounds check already in `spu94_sample_encode_to_ram` (returns -1 on overflow) |
| **What to build** | During recording, track accumulated PCM sample count. Before the next audio callback writes to the staging buffer, check if encoding the accumulated PCM would exceed remaining RAM. If yes, stop recording, encode what fits, transition to STOPPED state. |

### 5. Sample Rate Presets (Four PS1 Native Rates)

| Aspect | Detail |
|--------|--------|
| **Why expected** | PS1 SPU pitch register defines playback rate relative to 44.1kHz. The four clean octave-halving rates (44100, 22050, 11025, 5512.5 Hz) correspond to pitch values 0x1000, 0x0800, 0x0400, 0x0200. These are how PS1 game developers authored samples. Users need to select these without doing hex math. |
| **Complexity** | Low |
| **Dependencies** | `encodeRate` atomic already exists (defaults to 22050); SRC infrastructure exists via `SrcChain` |
| **What to build** | Dropdown or radio buttons with four preset rates. Each maps to a pitch register value and determines (a) the SRC target rate for downsampling input, (b) the playback pitch for the recorded sample, (c) the max recording duration display. |
| **Hardware reference** | The S1000 offered exactly two rates: 44.1kHz and 22.05kHz. The S900 offered variable rates from 7.5kHz to 40kHz. PS1 games typically used 22.05kHz for most instrument samples and 44.1kHz for CD-quality streaming. |

### 6. Waveform Display Updates After Recording

| Aspect | Detail |
|--------|--------|
| **Why expected** | After recording stops, the new sample must appear in the existing waveform display with S/L/E markers. Every sampler with a screen does this -- MPC, Digitakt, any DAW sampler. Users expect to see what they just recorded. |
| **Complexity** | Low |
| **Dependencies** | `WaveformDisplay::setSample()` already accepts int16 PCM data and a frame count |
| **What to build** | After encoding completes, decode the ADPCM back to PCM for display (or stash the pre-encode PCM from the staging buffer). Feed to existing `setSample()`. Markers auto-set: S=0.0, E=1.0, L=0.0. The display just works because the existing waveform infrastructure is format-agnostic. |

### 7. Input Source Selection (Standalone)

| Aspect | Detail |
|--------|--------|
| **Why expected** | Standalone app must let the user pick which audio input device and channels to record from. Users with multi-input interfaces need to select the right source (mic input 1 vs instrument input 2 vs line input, etc.). |
| **Complexity** | Low |
| **Dependencies** | JUCE `AudioDeviceManager` already manages devices in standalone mode; JUCE provides `AudioDeviceSelectorComponent` out of the box |
| **What to build** | Settings button in the sampler window that opens the existing JUCE audio settings dialog (or a compact version showing only input device/channel selection). The standalone wrapper already initializes `AudioDeviceManager`. |
| **Plugin context** | In a DAW plugin, the input comes from the DAW's routing. No device selection needed -- the DAW handles it. The feature is standalone-only. |

---

## Table Stakes -- Moderate Complexity

### 8. Threshold-Triggered Auto-Record

| Aspect | Detail |
|--------|--------|
| **Why expected** | The Akai S1000 (1988) had INPUT LEVEL trigger mode. The SP-1200 had threshold-based recording via the SAMPLE 4 function. The MPC has a threshold slider. The Digitakt has threshold-based auto-start. This is a 37-year-old expectation for any sampler recording feature. |
| **Complexity** | Medium |
| **Dependencies** | Input level tracking (feature 2), recording state machine (feature 1) |
| **What to build** | Extended state machine: IDLE -> ARMED -> RECORDING -> STOPPED. When ARMED, monitor input level against a user-set threshold (dB or linear). When input exceeds threshold, transition to RECORDING. Need hysteresis to prevent false triggers -- standard approach: dual-threshold (attack threshold higher, release threshold lower) or a hold-off timer (ignore re-triggers for N ms after stop). |
| **Threshold UX** | Single knob or slider for threshold level. Visual indicator showing the threshold level on the input meter. State display showing "ARMED" vs "RECORDING" vs "IDLE". |

### 9. ADPCM Encoding on Intake

| Aspect | Detail |
|--------|--------|
| **Why expected** | This is the entire point of SPU-94's sampler: audio passes through the PS1 codec on the way in, baking in the ADPCM character. The existing WAV loader already does this via `spu94_sample_encode_to_ram`. Live recording must do the same. |
| **Complexity** | Medium |
| **Dependencies** | `spu94_adpcm_encode_block`, SRC from host rate to target rate |
| **Implementation decision** | Two approaches -- see Architectural Constraint section below for detailed analysis. Recommended: buffer raw PCM during recording, encode entire buffer on stop (Approach A). This matches the S1000's workflow where recording and processing were separate operations. |
| **Why buffer-then-encode** | The brute-force ADPCM encoder tests 65 filter/shift combinations per 28-sample block. At 44.1kHz, that is ~635 blocks/second. While likely fast enough on modern CPUs, encoding on a worker thread after recording eliminates all real-time pressure and simplifies the state machine. The encoding pause is proportional to recording length -- at 512KB max (~29,000 blocks), well under a second on any modern machine. |

### 10. Save/Export Sample to WAV

| Aspect | Detail |
|--------|--------|
| **Why expected** | Users record samples to build libraries or share. The S-series saved to floppy. Modern samplers export WAV. Any recording workflow that produces keepable material needs export. |
| **Complexity** | Medium |
| **Dependencies** | ADPCM decode path (exists in `spu94_adpcm.c`), WAV writer (JUCE `WavAudioFormat`), trim markers (existing S/E markers) |
| **What to build** | Save button that: (a) decodes ADPCM from voice RAM to PCM, (b) applies S/E marker trim, (c) writes 16-bit mono WAV at the sample's native rate. Key creative decision: export the ADPCM-degraded version (what the sample actually sounds like during playback), not the original clean input. The degradation IS the character. |
| **Format** | 16-bit mono WAV at the recording sample rate. This is the universal interchange format. Not 24-bit (ADPCM is 4-bit, upsampled to 16-bit -- 24 bits would be wasted zeros). Not stereo (PS1 voices are mono with per-voice stereo panning). |

---

## Differentiators

Features that set SPU-94's live recording apart from other samplers. Not expected, but genuinely valuable.

### D1. Variable Sample Rate (Continuous Pitch Register Control)

| Aspect | Detail |
|--------|--------|
| **Value** | Beyond the four PS1 preset rates, the SPU pitch register supports any value from 0x0001 to 0x3FFF, meaning any effective sample rate from ~2.7 Hz to ~176.4 kHz (theoretical). The Akai S900 offered variable rates (7.5-40kHz) and users valued the creative tradeoff. SPU-94 can go further: record at ANY rate, including "wrong" ones that produce aliasing and pitched artifacts as creative texture. |
| **Complexity** | Medium |
| **Dependencies** | SRC ratio calculation at arbitrary rates, pitch register mapping |
| **What to build** | A "Variable" mode with a knob that maps the full pitch register range. The knob displays the resulting Hz value and approximate max recording time. Lower rates = more aliasing = more lo-fi character = longer recording time. This is the S900's bandwidth-vs-duration tradeoff taken to its extreme. |

### D2. Pre-Roll Buffer (Capture Before Threshold Trigger)

| Aspect | Detail |
|--------|--------|
| **Value** | When using threshold trigger, the transient that CROSSES the threshold IS the sound you want. Without pre-roll, you chop the attack. The S1000 had adjustable pre-trigger time. Studio One and Bitwig have "retrospective recording." A small circular buffer (50-200ms) captures audio before the threshold fires. |
| **Complexity** | Medium-High |
| **Dependencies** | Circular buffer running continuously when ARMED, splice logic when trigger fires |
| **What to build** | Ring buffer of N milliseconds of raw PCM maintained whenever state is ARMED. When threshold fires, prepend ring buffer contents to the recording. Memory cost: 100ms at 44.1kHz mono 16-bit = ~8.8 KB. Trivial. The complexity is in the state machine splice, not memory. |
| **UX** | Knob or dropdown for pre-roll time: 0ms (off), 25ms, 50ms, 100ms, 200ms. |

### D3. Input Monitoring with ADPCM Preview

| Aspect | Detail |
|--------|--------|
| **Value** | Standard input monitoring lets you hear yourself. SPU-94's twist: route the input through the ADPCM codec and PS1 reverb in real time so you hear what the recorded sample WILL sound like, not the clean input. "What you hear is what you get." No other sampler does this because no other sampler has a PS1 reverb engine inline. |
| **Complexity** | Medium |
| **Dependencies** | Real-time ADPCM encode+decode on monitor path (existing codec functions), reverb engine routing |
| **Caution** | The monitoring path must not interfere with the recording path (both use the same input signal). ADPCM block latency: 28 samples = ~0.6ms at 44.1kHz. Acceptable. Total round-trip monitoring latency depends on audio buffer size (typically 128-512 samples = 3-12ms). |

### D4. Normalize After Recording

| Aspect | Detail |
|--------|--------|
| **Value** | Scale the recorded sample to full 16-bit range. The MPC added normalize in firmware 1.3. The Digitakt normalizes automatically. For SPU-94: decode ADPCM, find peak, scale, re-encode. The re-encoding subtly changes the codec artifacts -- double-encoding produces richer distortion texture. |
| **Complexity** | Medium |
| **Dependencies** | Decode -> scale -> re-encode pipeline (all functions exist individually) |
| **What to build** | One-click button post-recording. Operates on voice RAM: decode all blocks, find peak, compute gain factor, scale PCM, re-encode. The ADPCM re-encoding changes artifacts slightly -- document this as a creative feature, not a bug. |

### D5. Auto-Trim Silence

| Aspect | Detail |
|--------|--------|
| **Value** | Detect the first non-silent sample and set S marker there. Detect the last non-silent sample and set E marker there. Saves manual trimming of dead air, especially useful after threshold-triggered recordings where there may be pre-roll silence. |
| **Complexity** | Low |
| **Dependencies** | Threshold-based scan of decoded waveform (operates on display data, not voice RAM) |
| **What to build** | Walk from start forward until amplitude exceeds ~-60dB. Walk from end backward until amplitude exceeds ~-60dB. Set S and E markers. Single button. |

---

## Anti-Features

Features to explicitly NOT build. These would add complexity without serving the product's character.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Multi-track recording** | SPU-94 is a sampler, not a DAW. Recording multiple simultaneous tracks into separate voice regions adds DAW-like complexity (track arming, routing matrix, headphone mix). The PS1 never recorded audio in real time at all. | Record mono into one voice slot at a time. For stereo source material, record the left channel. PS1 voices are mono with per-voice stereo panning -- that is the architecture. |
| **Destructive sample editing (cut/copy/paste/reverse)** | Full waveform editing is a different product. Adding cut/copy/paste turns the sampler window into an audio editor. | Keep the existing marker-based trim. S marker = start, E marker = end, L marker = loop. Markers are non-destructive and already draggable. Export respects markers. |
| **Time-stretching / pitch-independent stretching** | The PS1 SPU does not time-stretch. It pitch-shifts by changing playback rate (like a turntable). Time-stretching is a modern DSP operation that contradicts the hardware-faithful philosophy. | The pitch register IS the speed control. Lower pitch = slower + lower. Higher pitch = faster + higher. This is the PS1 sound. |
| **Automatic BPM detection / beat slicing** | MPC/Ableton territory. SPU-94 is about character, not workflow automation. BPM detection requires onset detection algorithms -- significant complexity for marginal value in this context. | Manual loop point placement via L marker. The user sets loop points by ear, which is exactly how PS1 game developers worked. |
| **Streaming from disk during recording** | Recording always goes to RAM, never to disk. The PS1 had finite RAM and no disk streaming during sample playback. Disk streaming eliminates the creative constraint of 512KB. | Record to RAM. Export to disk after recording completes. The RAM limit IS a feature -- it forces the same economy that defined PS1 game audio. |
| **Free-form Hz text input** | While the pitch register supports arbitrary values, a free-form Hz input invites confusion. "Recording at 13,847 Hz" is meaningless to a musician. | Four preset rates as primary; a continuous knob for Variable mode. The knob shows Hz and approximate recording time, but the control is tactile, not a text field. |
| **Stereo recording** | PS1 voices are mono. The SPU has no stereo sampling capability. Adding stereo recording would require allocating two voice slots and keeping them synchronized -- complexity that contradicts the hardware model. | Record mono. Use per-voice L/R volume panning to position in the stereo field after recording. |
| **Undo/redo for recording** | A single "last recording" undo would require keeping two copies of the sample in RAM (doubling memory). Multiple undo levels would require disk-backed history. Both add significant complexity for a niche workflow. | Record again to overwrite. The recording is fast (press record, play, stop). If you want to keep a take, export it first. |

---

## Feature Dependencies

```
Input Level Meter -------> Manual Record (start/stop)
                                |
                                v
                      ADPCM Encoding on Intake
                                |
                                v
                      Auto-Stop on RAM Full
                                |
                                v
                      Waveform Display Update
                                |
                                +---> Save/Export to WAV
                                |
                                +---> Auto-Trim Silence (D5)
                                |
                                +---> Normalize (D4)

Sample Rate Presets -----> Variable Sample Rate (D1, extends presets)

Threshold Trigger -------> Pre-Roll Buffer (D2, enhances trigger)
       |
       +---> requires Input Level Meter

Input Source Selection --- independent, standalone-only

RAM Usage Display -------- required by Manual Record (progress feedback)

Input Monitoring (D3) ---- independent, enhances recording experience
```

---

## Key Architectural Constraint: Encoding Timing

The existing `spu94_sample_encode_to_ram` takes a complete PCM buffer and encodes it all at once. For live recording, two approaches exist:

**Approach A -- Buffer then Encode (RECOMMENDED):**
Accumulate raw PCM in a `std::vector<int16_t>` during recording. When recording stops, call `spu94_sample_encode_to_ram` with the complete buffer on a worker thread. Simple, always works, brief pause after stop. This is how the Akai S1000 worked -- recording and encoding were separate operations.

- Pro: No real-time encoder pressure. Reuses existing code path exactly.
- Pro: Can check total encoded size before writing (no partial-buffer corruption risk).
- Con: Brief pause after stop while encoding runs (~200ms for a full 512KB at worst).
- Con: Needs a staging buffer large enough for raw PCM (at 44.1kHz, full 512KB ADPCM = ~14.5 million PCM samples = ~27.5 MB of int16). In practice, max recording time at 44.1kHz is ~20 seconds = ~1.7 MB.

**Approach B -- Encode Incrementally:**
Encode each 28-sample block as it arrives and write directly to voice RAM. Eliminates post-record pause but requires encoder to keep up with incoming rate in real time.

- Pro: No post-record pause. RAM usage updates in real-time during recording.
- Con: Brute-force encoder tests 65 filter/shift combinations per block. Needs profiling.
- Con: If encoder falls behind, audio drops out or recording corrupts.
- Con: Encoder state must persist across audio callbacks (currently single-shot).

**Recommendation: Approach A for v1.11.0.** The pause is proportional to recording length. At 512KB max (~29,000 ADPCM blocks), the entire buffer encodes in well under a second on modern hardware. Incremental encoding can be explored later if the pause bothers users.

---

## PS1 Hardware Context

On the real PS1, samples were never "recorded" through the SPU in real time. Game developers pre-encoded ADPCM samples offline using Sony's SDK tools (`AIFF2VAG`, `WAV2VAG`) and uploaded them to SPU RAM via DMA transfer (DMA channel 4). The SPU had capture buffers for voices 1 and 3 (first 4KB of SPU RAM), but these captured the processed output of those voices, not external input audio.

SPU-94's live recording feature is therefore a creative extension beyond what the hardware did -- using the hardware's codec and playback engine in a way the original designers never intended. This is consistent with SPU-94's North Star: "faithful to the algorithm, creative with the instrument."

The four preset sample rates (44.1k, 22.05k, 11.025k, 5.5125k) correspond to clean octave relationships in the pitch register (0x1000, 0x0800, 0x0400, 0x0200). PS1 game developers typically authored instrument samples at 22.05kHz (good quality, reasonable RAM usage) and streaming audio at 44.1kHz.

---

## MVP Recommendation

Build in this order, with each step producing a usable increment:

1. **Manual Record + Auto-Stop + Input Level Meter + RAM Display** -- the core recording pipeline
   - Record button in sampler window
   - PCM accumulation in processBlock
   - ADPCM encode on stop (worker thread)
   - Auto-stop when RAM full
   - Waveform display update after encoding completes
   - Input level meter and remaining-time readout
   - Addresses: features 1, 2, 3, 4, 6

2. **Sample Rate Presets** -- four PS1 native rates as a dropdown
   - SRC from host rate to target rate (can reuse libsamplerate from SrcChain)
   - RAM remaining display updates based on selected rate
   - Addresses: feature 5

3. **Threshold Trigger** -- arm mode with level detection
   - State machine extension: IDLE -> ARMED -> RECORDING -> STOPPED
   - Threshold knob on the sampler window
   - Addresses: feature 8

4. **Save/Export** -- WAV file export with trim markers
   - Decode ADPCM from voice RAM, apply S/E markers, write 16-bit WAV
   - Addresses: feature 10

5. **Variable Sample Rate** (differentiator) -- continuous pitch register knob
   - Addresses: D1

6. **Pre-Roll Buffer** (differentiator) -- circular buffer when armed
   - Addresses: D2

Defer: Record Into Voice Slot (needs RAM allocator redesign), Input Monitoring with ADPCM Preview (needs careful routing), Auto-Trim (low priority), Normalize (low priority).

---

## Sources

- Akai S1000 manual / Sound On Sound (May 1989): two sample rates (44.1/22.05kHz), INPUT LEVEL threshold trigger, MIDI trigger, footswitch trigger, REC LEVEL monitoring with VU meter, ED1 trim/loop, gain normalization in v2.0 firmware [HIGH confidence -- primary documentation]
- Akai S900: variable sample rate 7.5kHz-40kHz, 12-bit, 750KB RAM, up to 63 seconds [HIGH confidence]
- E-MU SP-1200: fixed 26.04kHz, 12-bit, 10 seconds max (2.5s per pad), threshold trigger [HIGH confidence]
- Akai MPC (current): arm button, threshold slider, "In" monitoring (zero-latency pre-sampler) vs processed monitoring, sample length counter, normalize in firmware 1.3 [HIGH confidence -- Akai support docs]
- Elektron Digitakt: threshold auto-start, auto-normalize on stop, waveform editor with start/end, up to 6:06 [MEDIUM confidence -- review-sourced]
- Elektron Tonverk: threshold arming, tempo-synced recording length, post-record trim/loop/normalize [MEDIUM confidence -- synthmagazine.com]
- Fairlight CMI: variable sample rate (8-100kHz), bandwidth-vs-duration tradeoff, waveform drawing [HIGH confidence]
- nocash psx-spx: SPU pitch register (0x1000 = 44100Hz base), ADPCM 28-sample blocks, 512KB RAM, DMA channel 4 transfers, capture buffers for voices 1/3 [HIGH confidence]
- JUCE AudioDeviceManager / AudioProcessor: processBlock receives input audio buffer, AudioDeviceSelectorComponent for device selection [HIGH confidence -- Context7 verified]
- Existing SPU-94 codebase: `spu94_sample_encode_to_ram`, `WaveformDisplay::setSample()`, `encodeRate` atomic, `ramUsed` atomic, `SrcChain` for sample rate conversion, `inputLevel` atomic [HIGH confidence -- direct code inspection]
