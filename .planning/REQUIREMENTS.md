# Requirements: SPU-94 v1.11.0 Live Input Sampling

**Defined:** 2026-05-28
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.11.0 Requirements

### Recording Core

- [x] **REC-01**: User can start and stop recording live audio input with a record button
- [x] **REC-02**: Recording auto-stops when the 512KB voice RAM buffer is full
- [x] **REC-03**: Recorded audio is ADPCM-encoded into voice RAM on recording stop
- [x] **REC-04**: Waveform display updates with the recorded sample after encoding completes
- [x] **REC-05**: Input level meter shows live input signal strength during recording
- [x] **REC-06**: RAM usage display shows bytes used, seconds recorded, and time remaining at current rate

### Sample Rate

- [x] **RATE-01**: User can select from four PS1 preset sample rates (44.1 / 22.05 / 11.025 / 5.5125 kHz)
- [x] **RATE-02**: User can dial a variable sample rate across the full pitch register range
- [x] **RATE-03**: Input audio is sample-rate-converted to the selected target rate before staging
- [x] **RATE-04**: Recording time display updates based on the selected sample rate

### Triggering

- [ ] **TRIG-01**: User can arm threshold-triggered recording
- [ ] **TRIG-02**: Recording starts automatically when input signal exceeds the user-set threshold
- [ ] **TRIG-03**: User can adjust the threshold level
- [ ] **TRIG-04**: Sampler displays armed/recording/idle state clearly

### Export

- [ ] **EXP-01**: User can save the recorded sample as a 16-bit mono WAV file
- [ ] **EXP-02**: Export respects the current S/E marker positions (trim)
- [ ] **EXP-03**: WAV file is written at the recording sample rate

## Future Requirements

### Differentiators (deferred)

- **DIFF-01**: Pre-roll buffer captures attack transient before threshold trigger fires
- **DIFF-02**: ADPCM monitoring — hear the PS1-degraded signal in real time while recording
- **DIFF-03**: Normalize — decode, scale to full range, re-encode
- **DIFF-04**: Auto-trim silence — scan for first/last non-silent sample, set S/E markers

## Out of Scope

| Feature | Reason |
|---------|--------|
| Multi-track / stereo recording | PS1 voices are mono; SPU has no stereo sampling |
| Destructive waveform editing (cut/copy/paste) | Different product — sampler uses marker-based non-destructive trim |
| Time-stretching | PS1 pitch-shifts by changing playback rate; time-stretch contradicts hardware model |
| BPM detection / beat slicing | MPC territory, not SPU-94's character |
| Streaming to disk during recording | 512KB RAM limit IS the creative constraint |
| Free-form Hz text input | Variable rate uses a tactile knob, not a text field |
| Undo/redo for recording | Record again to overwrite; export first to keep a take |
| Background encoder thread | Buffer-then-encode approach chosen for v1.11.0 simplicity |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| REC-01 | Phase 56 | Complete |
| REC-02 | Phase 56 | Complete |
| REC-03 | Phase 56 | Complete |
| REC-04 | Phase 56 | Complete |
| REC-05 | Phase 56 | Complete |
| REC-06 | Phase 56 | Complete |
| RATE-01 | Phase 57 | Complete |
| RATE-02 | Phase 57 | Complete |
| RATE-03 | Phase 57 | Complete |
| RATE-04 | Phase 57 | Complete |
| TRIG-01 | Phase 58 | Pending |
| TRIG-02 | Phase 58 | Pending |
| TRIG-03 | Phase 58 | Pending |
| TRIG-04 | Phase 58 | Pending |
| EXP-01 | Phase 59 | Pending |
| EXP-02 | Phase 59 | Pending |
| EXP-03 | Phase 59 | Pending |

**Coverage:**
- v1.11.0 requirements: 17 total
- Mapped to phases: 17
- Unmapped: 0

---
*Requirements defined: 2026-05-28*
*Last updated: 2026-05-29 after Phase 57 execution*
