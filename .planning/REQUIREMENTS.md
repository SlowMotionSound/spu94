# Requirements: SPU-94 v1.9 Complete Voice

**Defined:** 2026-05-21
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.9 Requirements

### ADSR Correction

- [ ] **ADSR-FIX-01**: Sustain-decrease step formula uses `-(8 - step)` producing steps -8, -7, -6, -5 per spec (currently uses `-(7 - step)` producing -7, -6, -5, -4)
- [ ] **ADSR-FIX-02**: Release step formula audited against spec and corrected if off-by-one (currently uses base 7, spec may require 8)
- [ ] **ADSR-FIX-03**: ADR documenting the correction, spec source, and any golden file changes
- [ ] **ADSR-FIX-04**: ADSR golden files regenerated and regression suite updated to match corrected behavior

### Signed Volume

- [ ] **SVOL-01**: Per-voice volume L/R accepts the full signed range (-0x4000..+0x3FFF, effective -0x8000..+0x7FFE) through the C API
- [ ] **SVOL-02**: All call sites that set vol_l/vol_r audited -- positive-only clamping removed
- [ ] **SVOL-03**: Negative volume produces phase-inverted output (sample-by-sample exact negation vs positive volume)
- [ ] **SVOL-04**: VxOUTX (for PMON) is unaffected by volume sign (captured pre-volume)
- [ ] **SVOL-05**: GUI updated to expose the signed volume range on sampler voice controls

### Pitch Modulation (PMON)

- [ ] **PMON-01**: PMON 24-bit bitmask register enables pitch modulation per voice (bits 1..23; bit 0 ignored)
- [ ] **PMON-02**: VxOUTX stored per voice after ADSR multiply, before volume multiply -- used as PMON factor for the next voice
- [ ] **PMON-03**: PMON formula: Factor = VxOUTX(N-1) + 0x8000; Step = (Step * Factor) >> 15; clamp to 0x4000 if Step > 0x3FFF
- [ ] **PMON-04**: Silent modulator (output = 0) produces Factor = 0x8000, halving the carrier pitch (authentic behavior, no special-casing)
- [ ] **PMON-05**: Voice processing order 0..23 sequential with VxOUTX written immediately after each voice (no batch delay)
- [ ] **PMON-06**: PMON chain stacking works (voice 0->1->2 produces cascading modulation)
- [ ] **PMON-07**: ADR documenting VxOUTX capture point (post-ADSR, pre-volume) with DuckStation as behavioral witness

### Noise Generator (NON)

- [ ] **NON-01**: Single global LFSR noise generator with polynomial taps at bits 15, 12, 11, 10 XOR 1 (XNOR), initial seed = 1, left-shift
- [ ] **NON-02**: Noise timer mechanism: decrement by NoiseStep (4-7) per tick, shift LFSR on underflow, double-reload if still negative
- [ ] **NON-03**: Noise frequency controlled by SPUCNT bits 13..10 (NoiseShift) and bits 9..8 (NoiseStep) -- per-voice pitch register has no effect on noise
- [ ] **NON-04**: NON 24-bit bitmask register selects which voices output noise instead of ADPCM/Gaussian interpolation
- [ ] **NON-05**: All NON-enabled voices read the same NoiseLevel value per tick (one generator, not per-voice)
- [ ] **NON-06**: ADPCM decode still runs for NON voices (flag byte side effects: loop mechanics, ENDX status)
- [ ] **NON-07**: ADSR envelope still applies to noise output (noise * adsr_level)
- [ ] **NON-08**: Noise generator ticks once globally before the voice loop, not once per voice
- [ ] **NON-09**: ADR documenting noise initial state, ADPCM-fetch-during-NON decision, and LFSR polynomial source

### Volume Sweep

- [ ] **SWEEP-01**: Per-voice volume sweep with independent L/R state machines (2 sweep states per voice)
- [ ] **SWEEP-02**: Sweep modes: linear increase, linear decrease, exponential increase (fake exp above 0x6000), exponential decrease (proportional to level)
- [ ] **SWEEP-03**: Sweep step values: increase +7,+6,+5,+4 via (7-step); decrease -8,-7,-6,-5 via -(8-step)
- [ ] **SWEEP-04**: Sweep uses counter-accumulate mechanism identical to ADSR (shared math helper, separate state storage)
- [ ] **SWEEP-05**: Sweep modifies vol_l/vol_r directly -- not a separate multiplier (the volume register IS the sweep's working state)
- [ ] **SWEEP-06**: Sweep and ADSR run concurrently as independent envelopes (both multiply into the signal chain)
- [ ] **SWEEP-07**: KON resets sweep state (counter = 0, level set to initial value); KOFF does not affect sweep
- [ ] **SWEEP-08**: Anti-stall guard for exponential decrease near zero (if scaled_step == 0 && level > 0, step = -1)
- [ ] **SWEEP-09**: Negative-phase sweep mode (Phase bit = 1): sweep operates on negative volume values, clamps to -0x8000..0
- [ ] **SWEEP-10**: ADR (low-confidence) documenting negative-phase behavior, Phase bit interaction with exponential decrease, spec uncertainty

### Integration

- [ ] **INT-01**: Voice mixer tick restructured: noise tick globally -> sweep per voice -> PMON pitch modify -> ADPCM decode -> noise/Gauss branch -> ADSR -> store VxOUTX -> volume multiply -> accumulate
- [ ] **INT-02**: PMON + NON interaction: noise voice output feeds PMON factor for next voice (random pitch jitter -- spec-orthogonal)
- [ ] **INT-03**: All existing voice features unbroken (ADSR, loop mechanics, EON reverb send, Gaussian interpolation, anti-aliasing toggle, MIDI dispatch)
- [ ] **INT-04**: rt_safety gates pass with all new features enabled (no heap, no locks, no syscalls, bounded latency)

## Future Requirements

### Multi-Timbral Voice Control (v2.0 candidate)

- **MULTI-01**: Per-voice sample assignment (different WAV per voice slot)
- **MULTI-02**: Per-voice MIDI channel (24 channels for independent triggering)
- **MULTI-03**: 24-channel mixer UI (per-voice faders, EON/PMON/NON toggles)
- **MULTI-04**: Per-voice ADSR configuration

### Additional SPU Features (v2.0+ candidate)

- **SPU-01**: ENDX voice status register (read-only flags for voices that hit loop-end)
- **SPU-02**: SPU IRQ (interrupt on voice reading specific RAM address)
- **SPU-03**: CD/External input mixing (stereo line-in with volume and optional reverb send)
- **SPU-04**: Master volume sweep (global L/R volume with hardware-driven ramp)

## Out of Scope

| Feature | Reason |
|---------|--------|
| Per-voice noise frequency | PS1 has exactly one noise generator shared by all 24 voices; per-voice frequency is unfaithful |
| PMON depth/mix parameter | PS1 PMON is binary on/off; depth is controlled by the modulator voice's ADSR and volume |
| Smooth PMON interpolation | Raw per-sample modulation IS the PS1 FM character; smoothing hides the aliasing that defines the sound |
| Volume sweep auto-oscillation | PS1 sweep is one-directional until it hits the limit and stops; auto-reverse is not hardware behavior |
| Configurable LFSR taps | PS1 LFSR polynomial is fixed hardware; changing taps loses authentic noise character |
| VxOUTX readable register API | Internal storage for PMON only; no musical benefit from external read access |
| Master volume sweep | Only per-voice volumes have hardware sweep on PS1; master volume is direct-set only |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| ADSR-FIX-01 | Phase 33 | Pending |
| ADSR-FIX-02 | Phase 33 | Pending |
| ADSR-FIX-03 | Phase 33 | Pending |
| ADSR-FIX-04 | Phase 33 | Pending |
| SVOL-01 | Phase 34 | Pending |
| SVOL-02 | Phase 34 | Pending |
| SVOL-03 | Phase 34 | Pending |
| SVOL-04 | Phase 34 | Pending |
| SVOL-05 | Phase 34 | Pending |
| PMON-01 | Phase 35 | Pending |
| PMON-02 | Phase 35 | Pending |
| PMON-03 | Phase 35 | Pending |
| PMON-04 | Phase 35 | Pending |
| PMON-05 | Phase 35 | Pending |
| PMON-06 | Phase 35 | Pending |
| PMON-07 | Phase 35 | Pending |
| NON-01 | Phase 36 | Pending |
| NON-02 | Phase 36 | Pending |
| NON-03 | Phase 36 | Pending |
| NON-04 | Phase 36 | Pending |
| NON-05 | Phase 36 | Pending |
| NON-06 | Phase 36 | Pending |
| NON-07 | Phase 36 | Pending |
| NON-08 | Phase 36 | Pending |
| NON-09 | Phase 36 | Pending |
| SWEEP-01 | Phase 37 | Pending |
| SWEEP-02 | Phase 37 | Pending |
| SWEEP-03 | Phase 37 | Pending |
| SWEEP-04 | Phase 37 | Pending |
| SWEEP-05 | Phase 37 | Pending |
| SWEEP-06 | Phase 37 | Pending |
| SWEEP-07 | Phase 37 | Pending |
| SWEEP-08 | Phase 37 | Pending |
| SWEEP-09 | Phase 37 | Pending |
| SWEEP-10 | Phase 37 | Pending |
| INT-01 | Phase 38 | Pending |
| INT-02 | Phase 38 | Pending |
| INT-03 | Phase 38 | Pending |
| INT-04 | Phase 38 | Pending |

**Coverage:**
- v1.9 requirements: 37 total
- Mapped to phases: 37
- Unmapped: 0

---
*Requirements defined: 2026-05-21*
*Last updated: 2026-05-22 -- traceability populated during roadmap creation*
