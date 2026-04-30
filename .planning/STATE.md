---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: True Oversampled DAC
status: planning
stopped_at: null
last_updated: "2026-04-30T21:00:00.000Z"
last_activity: 2026-04-30 -- Milestone v1.3 started
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-30)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Defining requirements for v1.3

## Current Position

Phase: Not started (defining requirements)
Plan: —
Status: Defining requirements
Last activity: 2026-04-30 — Milestone v1.3 started

## Performance Metrics

**Velocity:**

- Total plans completed: 12 (v1.2), 10 (v1.1), 37 (v1.0)
- Prior milestone: v1.2 shipped 12 plans across 5 phases

**By Phase (v1.2):**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 05-interpolation-filter-design | 1 | -- | -- |
| 06-dac-core-implementation | 2/2 | 55min | 28min |
| 07-pipeline-integration | 3/3 | 82min | 27min |
| 08-i-o-surface | 3/3 | 49min | 16min |
| 09-verification-documentation | 3/3 | -- | -- |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- v1.2 DAC FIR approximates 8x oversampling at 44.1kHz — v1.3 replaces this with true 352.8kHz processing
- v1.2 AK4309 cascade is 55+11+7 taps (minimum-order, folded-form + zero-skip: 22 multiplies/sample at 44.1kHz)
- DAC model position in chain: after spu94_fir_chain_step output at 44.1kHz
- All DSP signal flow lives in C core — JUCE/CLI/Python are thin wrappers with no DSP logic

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
| Feature | Preset save/load system — spu94_preset_save/load in C core, plain text key=value format, .spu94 files, JUCE Save/Load buttons, CLI preset-dump/preset-load | 2026-04-29 |
| Feature | Anthony has a screenshot of preset #1 to capture once save system exists | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) — crossfade two adjacent rates for smooth knob feel. Desktop-only. | 2026-04-29 |

## Session Continuity

Last session: 2026-04-30T21:00:00.000Z
Stopped at: Milestone v1.3 started — defining requirements
Resume file: .planning/ROADMAP.md
