---
gsd_state_version: 1.0
milestone: v1.9
milestone_name: Complete Voice
status: executing
stopped_at: Phase 39, Plan 01 Task 1 complete — awaiting human verification checkpoint (Task 2)
last_updated: "2026-05-23T21:22:00Z"
last_activity: 2026-05-23 -- Phase 39-01 Task 1 committed (e819e3f)
progress:
  total_phases: 10
  completed_phases: 5
  total_plans: 13
  completed_plans: 9
  percent: 50
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-21)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 39 — Pan & Level Controls

## Current Position

Phase: 39 (Pan & Level Controls) — EXECUTING
Plan: 1 of 1
Status: Awaiting human verification checkpoint (Task 2)
Last activity: 2026-05-23 -- Phase 39-01 Task 1 complete, checkpoint reached

Progress: [██████░░░░] 60%

## v1.9 Phase Map

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 33 | ADSR Correction | ADSR-FIX-01..04 | **Complete** |
| 34 | Signed Volume | SVOL-01..05 | **Complete** |
| 35 | Pitch Modulation (PMON) | PMON-01..07 | **Complete** |
| 36 | Noise Generator (NON) | NON-01..09 | **Complete** |
| 37 | Volume Sweep | SWEEP-01..10 | **Complete** |
| 38 | Integration & Cross-Feature Verification | INT-01..04 | **Complete** |
| 39 | Pan & Level Controls | PAN-01..04 | **Task 1 done, checkpoint** |
| 40 | Voice Feature Toggles | TOG-01..04 | Not started |
| 41 | VCA Ramp Controls | RAMP-01..05 | Not started |
| 42 | Voice GUI Integration | VGUI-01..03 | Not started |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.9 research decisions (from FEATURES.md / PITFALLS.md):

- VxOUTX capture point: post-ADSR, pre-volume (DuckStation-confirmed, HIGH confidence)
- Noise LFSR: taps at bits 15,12,11,10 XOR 1, seed = 1, left-shift (nocash + DuckStation)
- Noise generator: single global instance on mixer struct, ticked ONCE before voice loop
- Sweep: separate state struct from ADSR (shared math helper, separate storage)
- Sweep IS the volume register (modifies vol_l/vol_r directly, not a separate multiplier)
- ADSR sustain-decrease off-by-one: pre-existing bug from v1.8, fixed first in Phase 33
- Negative-phase sweep: LOW confidence (nocash "not yet tested"), needs ADR

### ADSR Bug Context

RESOLVED in Phase 33. Sustain-decrease and release formulas corrected from base 7 to base 8
per nocash spec (ADR-0056). Volume Sweep (Phase 37) can now reuse the corrected formula.

### Blockers/Concerns

None.

### Pending Todos

None.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Cleanup | REVIEW-cli-python.md M/L/N findings | Carried from v1.0 | 2026-04-26 |
| Paperwork | Phase 6/7 Nyquist validation | Carried from v1.0 | 2026-04-26 |
| License | MIT vs Apache-2.0 pick | Carried from M1 | 2026-04-25 |
| Archive cleanup | v1.6 phase file organization | Flagged | 2026-05-10 |
| UI gate | Hide preset Save/Load in plugin formats | Flagged | 2026-05-11 |

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Creative | Codec re-sync effect | 2026-05-01 |
| Visualization | Real-time room geometry visualizer | 2026-05-03 |
| Idea | Unified Morph Control | 2026-05-10 |
| Creative effect | "Bit Corrupt" mode | 2026-05-11 |

## Performance Metrics

**Velocity:**

- Total plans completed: 9 (v1.9)
- Average duration: 14min
- Total execution time: 0.35 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 33 - ADSR Correction | 1 | 16min | 16min |
| 34 | 2 | - | - |
| 35 | 2 | - | - |
| 36 | 2 | - | - |
| 38 - Integration | 2/2 | 11min | 5.5min |

*Updated after each plan completion*

## Session Continuity

Last session: 2026-05-23
Stopped at: Phase 39-01 Task 2 checkpoint — awaiting human visual verification
Resume file: .planning/phases/39-pan-level-controls/39-01-PLAN.md
