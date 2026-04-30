---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: milestone
status: in_progress
stopped_at: Phase 8 Plan 02 complete (Python bindings), Plan 03 next
last_updated: "2026-04-30T01:33:47Z"
last_activity: 2026-04-29 -- Phase 8 Plan 02 complete (Python mixer/DAC bindings)
progress:
  total_phases: 5
  completed_phases: 3
  total_plans: 10
  completed_plans: 8
  percent: 80
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-28)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.2 DAC Modeling — Phase 8 (I/O Surface) in progress, Plan 02 complete

## Current Position

Phase: 8 of 9 (I/O Surface) -- IN PROGRESS
Plan: 2 of 3 complete
Status: CLI flags + Python bindings shipped, JUCE GUI next
Last activity: 2026-04-29 -- Plan 02 complete (2 tasks, 2min)

Progress: [########--] 80%

## Performance Metrics

**Velocity:**

- Total plans completed: 10 (v1.1), 37 (v1.0)
- Prior milestone: v1.1 shipped 10 plans across 4 phases

**By Phase (v1.1):**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 01-core-codec | 2 | 48min | 24min |
| 02-pipeline-integration | 2 | 72min | 36min |

**By Phase (v1.2):**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 05-interpolation-filter-design | 1 | -- | -- |
| 06-dac-core-implementation | 2/2 | 55min | 28min |
| 07-pipeline-integration | 3/3 | 82min | 27min |
| 08-i-o-surface | 2/3 | 4min | 2min |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- v1.2 targets the AKM AK4309AVM 1-bit delta-sigma DAC (SCPH-1001/5501 era — Anthony's PS1)
- Three modelable artifacts: interpolation filter passband ripple, delta-sigma noise shaping, reconstruction rolloff
- DAC model goes AFTER spu94_fir_chain_step output at 44.1kHz (matches hardware: SPU serial output feeds DAC)
- Filter design in scipy BEFORE C implementation (Phase 5 then Phase 6)
- Noise model: LFSR + 2nd-order HP shaping, calibrated to ~90dB DR
- Out of scope: full delta-sigma simulation at 352.8kHz, analog output stage, ZOH, idle tones
- ADR-0054: AK4309B datasheet is authoritative for digital filter passband ripple; Stereophile's ripple attributed to composite analog chain
- Stopband measurement at Stage 1 stopband edge (24100 Hz), not Nyquist (22050 Hz)
- Minimum-order cascade 55+11+7 taps (41 non-zero multiplies) meets all specs with margin
- Coefficient hex notation: signed form (-0x005C not 0xFFA4) for -Werror compliance; bit patterns identical
- Folded-form + zero-skip: 22 multiplies per sample (not 41 unoptimized); int32 accumulators proven safe
- DAC_NOISE_SHIFT tuned from 9 (research estimate) to 14 for correct -90 dB amplitude calibration
- All DSP signal flow lives in C core — JUCE/CLI/Python are thin wrappers with no DSP logic
- Send/return mixer architecture: input gain → dry bus + patina (ADPCM) bus → two reverb sends (dry + patina) → reverb (100% wet) → three-fader master mixer (dry/patina/reverb) → DAC model (on/off) → output
- Six controls: input gain, dry fader, patina fader, dry reverb send, patina reverb send, reverb fader, plus DAC toggle
- ADPCM position may become movable in future — avoid hardwiring it
- WR-02 fix: spu94_dac_noise_init now accepts per-channel seed parameter (L=0xACE1u, R=0x1ECAu)
- latency_comp defaults ON (set explicitly in init/reset after zero-fill, per D-07)
- Off preset stays silent: CLI/tests do NOT set mixer faders for Off, preserving Off=silence contract
- Golden files regenerated and witness thresholds widened after mixer architecture Q15 truncation changes
- Python binding now has full 22 ctypes declarations for all mixer/DAC setter/getter functions (Plan 02 complete)
- 24 integration tests cover mixer bus routing, DAC toggle hierarchy, and latency compensation
- CLI --latency-comp flag accepted but is a no-op (default ON per D-07); only --no-latency-comp calls setter
- CLI fader overrides apply even for Off preset (user explicitly requested values override automatic defaults)

### Blockers/Concerns

None.

### Pending Todos

None yet.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Cleanup | REVIEW-cli-python.md M/L/N findings | Carried from v1.0 | 2026-04-26 |
| Paperwork | Phase 6/7 Nyquist validation | Carried from v1.0 | 2026-04-26 |
| License | MIT vs Apache-2.0 pick | Carried from M1 | 2026-04-25 |

## Session Continuity

Last session: 2026-04-29
Stopped at: Completed 08-02-PLAN.md (Python mixer/DAC bindings)
Resume file: .planning/phases/08-i-o-surface/08-03-PLAN.md
