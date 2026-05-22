---
gsd_state_version: 1.0
milestone: v1.9
milestone_name: Complete Voice
status: planning
last_updated: "2026-05-22"
last_activity: 2026-05-22
progress:
  total_phases: 6
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-21)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.9 Complete Voice -- Phase 33 ADSR Correction (ready to plan)

## Current Position

Phase: 33 of 38 (ADSR Correction)
Plan: 0 of TBD in current phase
Status: Ready to plan
Last activity: 2026-05-22 -- Roadmap created for v1.9 Complete Voice (6 phases, 37 requirements)

Progress: [░░░░░░░░░░] 0%

## v1.9 Phase Map

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 33 | ADSR Correction | ADSR-FIX-01..04 | Not started |
| 34 | Signed Volume | SVOL-01..05 | Not started |
| 35 | Pitch Modulation (PMON) | PMON-01..07 | Not started |
| 36 | Noise Generator (NON) | NON-01..09 | Not started |
| 37 | Volume Sweep | SWEEP-01..10 | Not started |
| 38 | Integration & Cross-Feature Verification | INT-01..04 | Not started |

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

The v1.8 ADSR code uses `-(7-step)` for sustain-decrease, producing steps -7,-6,-5,-4.
The spec says `-(8-step)` producing -8,-7,-6,-5. This is an off-by-one making sustain
decay slightly slower than real PS1 hardware. Phase 33 fixes this before Volume Sweep
(Phase 37) reuses the same step formula for its own decrease path.

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
- Total plans completed: 0 (v1.9)
- Average duration: --
- Total execution time: 0 hours

**By Phase:**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| - | - | - | - |

*Updated after each plan completion*

## Session Continuity

Last session: 2026-05-22
Stopped at: v1.9 roadmap created -- 6 phases, 37 requirements mapped
Resume file: None
