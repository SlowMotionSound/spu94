# Requirements: SPU-94 v1.10.0 Voice Dynamics & Stereo Effects

**Defined:** 2026-05-24
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.10.0 Requirements

### Retrigger Engine (Foundation)

- [x] **RTR-01**: C core retrigger mechanism that auto-reverses VCA ramp direction when a ramp reaches its limit (0 or max)
- [ ] **RTR-02**: Independent L/R retrigger rates — each channel can retrigger at a different speed for polyrhythmic modulation
- [x] **RTR-03**: Retrigger rate range covers sub-Hz (~0.5 Hz) through audio-rate (~7350 Hz) using the existing sweep shift/step infrastructure
- [x] **RTR-04**: Retrigger enable/disable per voice per channel — when disabled, ramp behaves as one-shot (existing v1.9 behavior preserved)
- [ ] **RTR-05**: Retrigger state resets on KON (new note starts fresh, no stale phase from previous note)

### Tremolo

- [ ] **TREM-01**: Both L and R channels retrigger in sync (same rate, same phase) producing periodic volume oscillation
- [ ] **TREM-02**: Speed control labeled in Hz or seconds, covering musically useful range (~0.5 Hz to ~19 Hz)
- [ ] **TREM-03**: Depth control (0-100%) determines how far the ramp swings from the current volume level
- [ ] **TREM-04**: Curve selection (Linear vs Exponential) — linear produces triangle-wave tremolo, exponential produces asymmetric Uni-Vibe character
- [ ] **TREM-05**: L/R rate ratio parameter for polyrhythmic mode (1:1 = standard, other ratios = phase-drifting stereo)
- [ ] **TREM-06**: Tremolo visible in sampler GUI as a dedicated effect section with labeled controls

### Auto-Pan

- [ ] **PAN-01**: L and R channels retrigger in opposition (L increases while R decreases, then swap)
- [ ] **PAN-02**: Speed control labeled in Hz or seconds, same range as tremolo
- [ ] **PAN-03**: Depth control determines how far the pan sweeps from center (100% = full L-to-R)
- [ ] **PAN-04**: PS1-faithful linear crossfade (volume dip at center is the intended character, no equal-power option)
- [ ] **PAN-05**: L/R rate ratio for asymmetric auto-pan (different rates create evolving stereo patterns)
- [ ] **PAN-06**: Auto-pan visible in sampler GUI alongside tremolo controls

### Sidechain Duck

- [ ] **DUCK-01**: Per-voice duck source selector — pick which voice's KON triggers a volume drop on this voice
- [ ] **DUCK-02**: Duck uses exponential decrease mode (fast attack, slow release — natural compressor behavior)
- [ ] **DUCK-03**: Release speed control determines how quickly the ducked voice recovers (maps to sweep shift value)
- [ ] **DUCK-04**: Depth control determines how far the volume drops (partial duck vs full silence)
- [ ] **DUCK-05**: Automatic recovery — after decrease ramp completes, increase ramp triggers to restore original level
- [ ] **DUCK-06**: Duck source voice picker visible in sampler GUI per voice

### Stereo Widener

- [ ] **WIDE-01**: L and R channels diverge from a common level (one increases, other decreases) to create stereo width
- [ ] **WIDE-02**: Width amount control with a mono-safety cap that prevents full mono cancellation
- [ ] **WIDE-03**: Mono-safety cap value tuned so that summing to mono loses no more than ~3 dB at maximum width
- [ ] **WIDE-04**: Stereo widener visible in sampler GUI with width control and mono-safety indicator

### AM Synthesis

- [ ] **AM-01**: Audio-rate retrigger of VCA ramp (shift values 0-8 producing ~37 Hz to ~7350 Hz modulation)
- [ ] **AM-02**: Rate control labeled in Hz, covering the full audio-rate range
- [ ] **AM-03**: Depth control (0-100%) determines modulation intensity — low depth = subtle ring, high depth = full metallic sidebands
- [ ] **AM-04**: Curve selection affects harmonic content (linear = sawtooth-like spectrum, exponential = different harmonic series)
- [ ] **AM-05**: AM synthesis visible in sampler GUI with rate/depth/curve controls

### Phase Modulator

- [ ] **PMOD-01**: Retriggered polarity oscillation — volume cycles between positive and negative on one or both channels
- [ ] **PMOD-02**: Rate control covering slow stereo breathing (~0.5 Hz) through ring-mod territory (~15+ Hz)
- [ ] **PMOD-03**: Depth control determines how far into negative volume the oscillation goes
- [ ] **PMOD-04**: Prototype and verify zero-crossing behavior — document whether volume passing through zero creates clicks/pops
- [ ] **PMOD-05**: Phase modulator visible in sampler GUI (may be labeled experimental)

### Internal Mod Bus

- [ ] **MOD-01**: Per-voice noise-to-pitch modulation depth knob — global noise LFSR output scales pitch deviation
- [ ] **MOD-02**: Per-voice noise-to-volume modulation depth knob — noise output scales volume deviation
- [ ] **MOD-03**: Per-voice noise-to-pan modulation depth knob — noise output scales L/R balance deviation
- [ ] **MOD-04**: All three mod destinations operate independently (any combination of noise→pitch, noise→vol, noise→pan active simultaneously)
- [ ] **MOD-05**: Mod bus processing runs inside the C core voice tick at sample rate (RT-safe, no host-layer latency)
- [ ] **MOD-06**: Internal mod bus controls visible in sampler GUI as a dedicated section with three depth knobs

### GUI & Integration

- [x] **GUI-01**: Sampler window resized to accommodate new effect sections without cramming
- [x] **GUI-02**: Effect selector or section layout that clearly organizes tremolo, auto-pan, sidechain, widener, AM, phase mod, and mod bus
- [x] **GUI-03**: Existing v1.9 VCA ramp controls (direction/speed/curve/ARM) remain accessible as raw register access
- [x] **GUI-04**: All new effects coexist with existing voice features (ADSR, PMON, NON, pan/level) without regression
- [x] **GUI-05**: rt_safety gates pass with all effects enabled (no heap, no locks, no syscalls, bounded latency)

## Future Requirements

### BPM Sync (v1.11+ candidate)

- **SYNC-01**: Effect speeds optionally lock to DAW host tempo
- **SYNC-02**: Musical subdivisions (1/4, 1/8, 1/16, dotted, triplet) as rate presets

### Additional Mod Sources (v1.11+ candidate)

- **MODSRC-01**: LFO as mod source (sine, triangle, square, S&H) in addition to noise
- **MODSRC-02**: Mod matrix expansion — multiple sources x multiple destinations

## Out of Scope

| Feature | Reason |
|---------|--------|
| One-shot fade in/out as named effect | Already covered by v1.9 VCA ramp controls (ARM button) |
| Equal-power auto-pan crossfade | PS1 linear crossfade dip at center IS the character; faithful-only per North Star |
| True phaser (all-pass filters) | Not present in PS1 hardware; departure from spec |
| Per-voice noise frequency for mod bus | PS1 has one global noise generator; per-voice freq is unfaithful |
| Mod matrix UI | Overkill for 1 source x 3 destinations; fixed knobs are simpler |
| BPM sync | Host-layer concern; build effects first, add sync later |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| RTR-01 | Phase 43 | Complete |
| RTR-02 | Phase 43 | Pending |
| RTR-03 | Phase 43 | Complete |
| RTR-04 | Phase 43 | Complete |
| RTR-05 | Phase 43 | Pending |
| TREM-01 | Phase 44 | Pending |
| TREM-02 | Phase 44 | Pending |
| TREM-03 | Phase 44 | Pending |
| TREM-04 | Phase 44 | Pending |
| TREM-05 | Phase 44 | Pending |
| TREM-06 | Phase 44 | Pending |
| PAN-01 | Phase 45 | Pending |
| PAN-02 | Phase 45 | Pending |
| PAN-03 | Phase 45 | Pending |
| PAN-04 | Phase 45 | Pending |
| PAN-05 | Phase 45 | Pending |
| PAN-06 | Phase 45 | Pending |
| DUCK-01 | Phase 46 | Pending |
| DUCK-02 | Phase 46 | Pending |
| DUCK-03 | Phase 46 | Pending |
| DUCK-04 | Phase 46 | Pending |
| DUCK-05 | Phase 46 | Pending |
| DUCK-06 | Phase 46 | Pending |
| WIDE-01 | Phase 47 | Pending |
| WIDE-02 | Phase 47 | Pending |
| WIDE-03 | Phase 47 | Pending |
| WIDE-04 | Phase 47 | Pending |
| AM-01 | Phase 48 | Pending |
| AM-02 | Phase 48 | Pending |
| AM-03 | Phase 48 | Pending |
| AM-04 | Phase 48 | Pending |
| AM-05 | Phase 48 | Pending |
| PMOD-01 | Phase 49 | Pending |
| PMOD-02 | Phase 49 | Pending |
| PMOD-03 | Phase 49 | Pending |
| PMOD-04 | Phase 49 | Pending |
| PMOD-05 | Phase 49 | Pending |
| MOD-01 | Phase 50 | Pending |
| MOD-02 | Phase 50 | Pending |
| MOD-03 | Phase 50 | Pending |
| MOD-04 | Phase 50 | Pending |
| MOD-05 | Phase 50 | Pending |
| MOD-06 | Phase 50 | Pending |
| GUI-01 | Phase 51 | Pending |
| GUI-02 | Phase 51 | Pending |
| GUI-03 | Phase 51 | Pending |
| GUI-04 | Phase 51 | Pending |
| GUI-05 | Phase 51 | Pending |

**Coverage:**
- v1.10.0 requirements: 44 total
- Mapped to phases: 44/44
- Unmapped: 0

---
*Requirements defined: 2026-05-24*
*Traceability updated: 2026-05-24*
