# v1.8 Requirements: PSX Voice Engine

**Milestone:** v1.8
**Status:** Approved
**Drafted:** 2026-05-16
**Inputs:** `.planning/research/{STACK,FEATURES,ARCHITECTURE,PITFALLS,SUMMARY}-v1.8.md`

## Milestone Goal

Build a spec-faithful PS1 SPU voice playback engine (24-voice ADPCM sampler), starting as a single monophonic voice in the standalone testbed, then expanding to 24 voices. Voices route through a PS1-faithful mixer with dry output to DAC and per-voice optional reverb send. One deliberate deviation: voice RAM and reverb RAM are separate 512 KB buffers (no shared address space collision risk).

## Locked Decisions (inputs, not requirements)

| Topic | Decision |
|---|---|
| Voice count | 24 (PS1 spec) |
| Sample format | 4-bit Sony ADPCM (existing encoder/decoder) |
| Interpolation | 4-tap Gaussian (existing, per-voice instance) |
| Pitch architecture | Single-counter (bits 12+ = sample, bits 4-11 = Gauss index) |
| RAM model | Two separate 512 KB buffers (voice + reverb) — deliberate deviation |
| Envelope | PS1 ADSR (counter-accumulate, fake exponential attack, real exponential decay) |
| Mixer topology | Voices → int32 sum → sat_s16 → master volume → dry out + reverb send |
| Reverb integration | Per-voice EON flag gates reverb send; reverb engine unchanged |
| Development target | Standalone testbed (no plugin UX changes) |
| Volume model | Unsigned per-voice L/R (no phase inversion — deferred) |
| Deferred features | PMON, NON, volume sweep, signed volume, CD/external input, SPUIRQ, DMA |

## Requirements

### Voice Playback

- [ ] **VOICE-01**: ADPCM decode from dedicated 512 KB voice RAM buffer (decode-only, no encode in hot path)
- [ ] **VOICE-02**: Single-counter Gaussian interpolation per voice (bits 12+ = sample index, bits 4-11 = Gauss index)
- [ ] **VOICE-03**: Pitch register per voice with hardware-mandated 0x3FFF clamp
- [ ] **VOICE-04**: Per-voice unsigned L/R volume (0-32767 range, no phase inversion)
- [ ] **VOICE-05**: WAV-to-ADPCM encode at sample load time (using existing encoder)
- [ ] **VOICE-06**: 24 independent voice state structs with isolated Gaussian ring buffers

### ADSR Envelope

- [x] **ADSR-01**: Four-phase envelope (Attack, Decay, Sustain, Release) per voice
- [x] **ADSR-02**: Counter-accumulate stepping mechanism (bit-15 trigger, not fixed-rate ramp)
- [x] **ADSR-03**: Fake exponential attack (step halves above 0x6000)
- [x] **ADSR-04**: Real exponential decay (step proportional to current level: step * level / 0x8000)
- [x] **ADSR-05**: Sustain target is (N+1) * 0x800, not zero
- [x] **ADSR-06**: Release phase triggered by KOFF, decays to zero then silences voice

### Loop Mechanics

- [x] **LOOP-01**: Loop start/end flags read from ADPCM block header byte 1 (bits 0/1/2)
- [x] **LOOP-02**: Loop-start address auto-latches as playback cursor passes a flagged block
- [x] **LOOP-03**: ADPCM filter state snapshot at loop-start block, restore on every loop-end jump
- [x] **LOOP-04**: One-shot termination when loop-end flag has repeat bit clear
- [x] **LOOP-05**: ENDX status bit set when voice reaches end (queryable per voice)

### Mixer & Routing

- [ ] **MIX-01**: 24-voice int32 accumulator with sat_s16 at final output stage
- [ ] **MIX-02**: Per-voice reverb-on flag (EON) — voice contributes to reverb send when enabled
- [ ] **MIX-03**: Master Volume L/R applied after voice summation
- [ ] **MIX-04**: KON/KOFF with pending-tick semantics (apply at start of next sample tick)
- [ ] **MIX-05**: Dry voice sum routes to DAC section; reverb-send sum routes to existing reverb engine input
- [ ] **MIX-06**: Voice engine and coloration bus (ADPCM patina) are independent — both can be active simultaneously

### SPU RAM

- [ ] **RAM-01**: Dedicated 512 KB voice RAM buffer (separate from reverb work buffer — deliberate deviation from shared-RAM PS1 spec)
- [ ] **RAM-02**: Dedicated 512 KB reverb RAM buffer (existing work buffer, unchanged)
- [ ] **RAM-03**: Sample load validates address bounds within voice RAM
- [ ] **RAM-04**: Sample addressing uses PS1-style byte offsets within the voice RAM space

### Standalone Testbed

- [ ] **TEST-01**: Load WAV file into voice RAM (encode to ADPCM on load)
- [ ] **TEST-02**: Trigger single voice playback from GUI (pitch control)
- [ ] **TEST-03**: MIDI note input triggers voices in standalone (JUCE native MIDI)
- [ ] **TEST-04**: Standalone remains the development testbed — no plugin UX changes in v1.8

## Traceability

| Requirement | Phase | Phase Description | Status |
|---|---|---|---|
| VOICE-01 | Phase 27 | Single Voice Playback | Pending |
| VOICE-02 | Phase 27 | Single Voice Playback | Pending |
| VOICE-03 | Phase 27 | Single Voice Playback | Pending |
| VOICE-04 | Phase 27 | Single Voice Playback | Pending |
| VOICE-05 | Phase 27 | Single Voice Playback | Pending |
| VOICE-06 | Phase 27 | Single Voice Playback | Pending |
| RAM-01 | Phase 27 | Single Voice Playback | Pending |
| RAM-02 | Phase 27 | Single Voice Playback | Pending |
| RAM-03 | Phase 27 | Single Voice Playback | Pending |
| RAM-04 | Phase 27 | Single Voice Playback | Pending |
| ADSR-01 | Phase 28 | ADSR Envelope | Complete |
| ADSR-02 | Phase 28 | ADSR Envelope | Complete |
| ADSR-03 | Phase 28 | ADSR Envelope | Complete |
| ADSR-04 | Phase 28 | ADSR Envelope | Complete |
| ADSR-05 | Phase 28 | ADSR Envelope | Complete |
| ADSR-06 | Phase 28 | ADSR Envelope | Complete |
| LOOP-01 | Phase 29 | Loop Mechanics | Complete |
| LOOP-02 | Phase 29 | Loop Mechanics | Complete |
| LOOP-03 | Phase 29 | Loop Mechanics | Complete |
| LOOP-04 | Phase 29 | Loop Mechanics | Complete |
| LOOP-05 | Phase 29 | Loop Mechanics | Complete |
| MIX-01 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| MIX-02 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| MIX-03 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| MIX-04 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| MIX-05 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| MIX-06 | Phase 30 | 24-Voice Polyphony + Mixer | Pending |
| TEST-01 | Phase 31 | Standalone Testbed UX | Pending |
| TEST-02 | Phase 31 | Standalone Testbed UX | Pending |
| TEST-03 | Phase 31 | Standalone Testbed UX | Pending |
| TEST-04 | Phase 31 | Standalone Testbed UX | Pending |

**Coverage: 31/31 requirements mapped. No orphans.**

## Out of Scope (v1.8)

- Pitch modulation (PMON) — voice-to-voice pitch FM; deferred to v1.9+
- Noise generator (NON) — LFSR replacing ADPCM for one voice slot; deferred to v1.9+
- Volume sweep mode — automatic volume ramp per voice; deferred to v1.9+
- Per-voice signed volume / phase inversion — faithfulness feature, deferred
- CD audio input — PS1 system feature, not musical
- External audio input — PS1 system feature, not musical
- SPUIRQ — system interrupt signaling, no audio effect
- DMA emulation — bus plumbing, no audio effect
- Plugin UX changes — standalone is the v1.8 testbed; plugin updates are a future milestone

## Open Items / Known Unknowns

| # | Item | When |
|---|---|---|
| 1 | Loop seam filter state: snapshot after or before decode of loop-start block? | Phase 29 |
| 2 | 24-voice mixer saturation: confirm int32 accumulate + single final sat_s16 vs per-voice clip | Witness test during Phase 30 |
| 3 | ADSR exponential attack boundary: strictly > 0x6000 or >= 0x6000? | Phase 28 |
| 4 | KON timing: first output sample in same tick as KON or following tick? | Phase 30 |
| 5 | Voice engine + coloration bus coexistence: stackable or mutually exclusive? | Architecture decision during Phase 30 (MIX-06) |
