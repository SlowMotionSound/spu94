# Research Summary: v1.11.0 Live Input Sampling

**Project:** SPU-94
**Synthesized:** 2026-05-28
**Sources:** STACK.md, FEATURES.md, ARCHITECTURE-v1.11.md, PITFALLS-v1.11.md

---

## Stack Additions

**None.** All four researchers agree: zero new dependencies. JUCE 8.0.12 handles audio I/O and WAV export. libsamplerate handles input SRC. libspu94 handles ADPCM encoding and voice RAM. The work is integration, not library shopping.

## Feature Table Stakes

From 37 years of hardware sampler precedent (Fairlight, S1000, SP-1200, MPC, Digitakt):

1. Manual record button (start/stop)
2. Input level meter
3. RAM usage / remaining time display
4. Auto-stop when 512KB buffer is full
5. Sample rate presets (four PS1 native rates)
6. Waveform display updates after recording
7. Input source selection (standalone — JUCE handles device selection already)
8. Threshold-triggered auto-record (with hysteresis)
9. ADPCM encoding on intake (bakes in PS1 character)
10. Save/Export sample to WAV

## Differentiators

- **Variable sample rate** — continuous pitch register knob beyond the four presets
- **Pre-roll buffer** — captures the attack transient before threshold trigger fires
- **ADPCM monitoring** — hear the PS1-degraded signal in real time (unique to SPU-94)
- **Normalize** — decode, scale to full range, re-encode (double-encoding adds texture)
- **Auto-trim silence** — scan for first/last non-silent sample, set S/E markers

## Key Decision: ADPCM Encoding Strategy

The four researchers proposed three different approaches. This is the milestone's central architectural decision.

| Approach | Where | Complexity | Tradeoff |
|----------|-------|-----------|----------|
| A. Buffer-then-encode | Message thread, after stop | Lowest | ~200ms pause after recording stops; reuses existing `spu94_sample_encode_to_ram` unchanged |
| B. Background encoder thread | Dedicated thread + SPSC ring buffer | Highest | No pause; introduces threading, ring buffer overrun handling, state machine complexity |
| C. Encode on audio thread | processBlock | Medium | No pause, no threading; researchers disagree on CPU feasibility (5μs vs 1ms estimates) |

**Recommendation: Approach A (buffer-then-encode) for v1.11.0.**

Rationale:
- Simplest — zero threading complexity, zero ring buffer management, zero overrun handling
- Reuses the exact existing code path (`spu94_sample_encode_to_ram`)
- The ~200ms worst-case pause matches how the Akai S1000 worked (recording and encoding were separate operations)
- The encoder's real-time cost is disputed between researchers — Approach A sidesteps the entire debate
- Can upgrade to Approach B or C later if users find the pause annoying (unlikely at these durations)

**Staging buffer sizing:** At 44.1 kHz, full 512KB ADPCM = ~917K PCM samples = ~1.7 MB of int16. At lower rates, proportionally less. Acceptable for a desktop application.

## Recording Architecture (Approach A)

```
Audio Thread (processBlock):
  host audio buffer (float, host SR)
    |
    v
  [existing SrcChain]  host SR -> 44.1 kHz
    |
    +---> [reverb engine]  existing path, unmodified
    |
    +---> [record path]    NEW, only active when recording
           |
           v
         [input channel select]  L / R / mono-sum
           |
           v
         [SRC to target rate]  new SRC_STATE via libsamplerate
           |
           v
         [staging buffer]  pre-allocated std::vector<int16_t>
           |
           v (on stop, message thread)
         [spu94_sample_encode_to_ram]  existing batch encoder
           |
           v
         [voice_ram]  512KB ADPCM
           |
           v
         [WaveformDisplay::setSample]  existing method
```

## Watch Out For

From pitfalls research (severity-ordered):

1. **Staging buffer allocation** — pre-allocate based on max recording size at the selected sample rate before recording starts. Never grow the buffer on the audio thread.

2. **Recording state machine races** — GUI, audio, and message threads all touch recording state. Use a single atomic enum with strict ownership: GUI sets ARM/STOP, audio thread detects threshold + feeds staging buffer, message thread encodes after stop.

3. **SRC_STATE lifecycle** — create on the message thread before recording starts (non-RT allocation is fine). Destroy after recording stops. Never create/destroy on the audio thread.

4. **Input monitoring feedback** — if the user monitors through the reverb while recording, the reverb output could feed back into the recording input (standalone with speakers). Not a code bug, but document the risk and recommend headphones.

5. **Plugin vs standalone** — standalone receives system audio device input; plugin receives DAW bus audio. Both paths deliver input in the same processBlock buffer. Recording works in both contexts without code changes.

## Build Order

All researchers converge on the same phasing:

1. **Core recording pipeline** — manual record/stop, staging buffer, ADPCM encode on stop, waveform display update, auto-stop on RAM full, input level meter, RAM usage display
2. **Sample rate selection** — four PS1 presets + variable rate + SRC on input path
3. **Threshold trigger** — state machine extension (ARMED state), threshold knob, pre-roll buffer
4. **Save Sample export** — decode from voice RAM, write WAV via JUCE
5. **GUI polish** — recording controls integrated into sampler panel

## Anti-Features (Do Not Build)

- Multi-track / stereo recording (PS1 voices are mono)
- Destructive waveform editing (cut/copy/paste — different product)
- Time-stretching (PS1 pitch-shifts by changing playback rate, like a turntable)
- BPM detection / beat slicing (MPC territory, not SPU-94's character)
- Streaming to disk during recording (512KB RAM limit IS the creative constraint)
- Free-form Hz text input (knob shows Hz, but control is tactile)
- Undo/redo for recording (record again to overwrite; export first if you want to keep)

## PS1 Hardware Context

The real PS1 never recorded audio through the SPU in real time. Game devs pre-encoded ADPCM offline using Sony SDK tools and uploaded to SPU RAM via DMA. SPU-94's live recording is a creative extension — using the hardware's codec and playback engine in a way the original designers never intended. Consistent with the North Star: "faithful to the algorithm, creative with the instrument."

## Recording Time at Each Rate

| Rate | Pitch Register | Max Time | Character |
|------|---------------|----------|-----------|
| 44,100 Hz | 0x1000 | ~20.7s | Full fidelity |
| 22,050 Hz | 0x0800 | ~41.4s | PS1 workhorse rate |
| 11,025 Hz | 0x0400 | ~82.8s | Lo-fi, crunchy |
| 5,512.5 Hz | 0x0200 | ~165.6s | Very lo-fi, nearly 3 minutes |

---
*Research completed: 2026-05-28*
*Ready for requirements: yes*
