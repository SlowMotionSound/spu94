---
gsd_state_version: 1.0
milestone: v1.2
milestone_name: milestone
status: executing
stopped_at: Phase 9 context gathered
last_updated: "2026-04-30T17:29:37.906Z"
last_activity: 2026-04-30 -- Phase 9 planning complete
progress:
  total_phases: 5
  completed_phases: 4
  total_plans: 12
  completed_plans: 9
  percent: 75
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-28)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.2 DAC Modeling — Phase 8 (I/O Surface) complete, Phase 9 next

## Current Position

Phase: 8 of 9 (I/O Surface) -- COMPLETE
Plan: 3 of 3 complete
Status: Ready to execute
Last activity: 2026-04-30 -- Phase 9 planning complete

Progress: [#########-] 90%

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
| 08-i-o-surface | 3/3 | 49min | 16min |

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
- JUCE GUI combined mixer strip + DAC toggles into single bottom row (original 4-zone overlapped register panel)
- ADPCM auto-enables when patina fader or ADPCM send > 0 (replaces removed manual toggle)
- Reverb Sends border removed from toolbar for cleaner layout -- three inline send knobs

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

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators (5 LEDs showing active filter pair, auto/manual toggle) | 2026-04-29 |
| Performance | Memory Flush button — instant spu94_reset to kill feedback runaway | 2026-04-29 |
| Performance | Smooth Memory Drain — gradual buffer decay with user-controlled drain rate (fast kill → slow tail-off → freeze) | 2026-04-29 |
| Performance | Parameter Slew Control — knob controlling parameter transition speed (raw digital crunch ↔ smooth). M4 lever layer. | 2026-04-29 |
| UI Enhancement | Stereo Link toggle — lock/unlock L/R register values for controllable vs asymmetric feedback | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override — force specific filter pairs for tonal control | 2026-04-29 |
| Distribution | Static linking for standalone (done — single 7.6MB executable, no .so dependency) | 2026-04-29 |
| Distribution | Cross-platform build guides (Mac + Windows) — audience TBD, waiting on Anthony | 2026-04-29 |
| Distribution | Visual signal flow diagram as GUI — Ensoniq/ASM Hydrasynth style panel layout. Future UI overhaul. | 2026-04-29 |
| Feature | Preset save/load system — spu94_preset_save/load in C core, plain text key=value format, .spu94 files, JUCE Save/Load buttons, CLI preset-dump/preset-load. Principled foundation needed before custom presets accumulate. | 2026-04-29 |
| Feature | Anthony has a screenshot of preset #1 to capture once save system exists | 2026-04-29 |
| Feature | Real oversampling engine — current DAC FIR approximates at 44.1kHz, real impl would zero-stuff and run cascade at elevated rate. Variable 1x-128x knob (stepped powers of 2). Start practical (hard-switch + slew), then luxury (parallel crossfade). | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) — crossfade two adjacent rates for smooth knob feel. Runs both engines in parallel during transition. Desktop-only (too expensive for MCU). | 2026-04-29 |

## Session Continuity

Last session: 2026-04-30T17:18:00.879Z
Stopped at: Phase 9 context gathered
Resume file: .planning/phases/09-verification-documentation/09-CONTEXT.md
