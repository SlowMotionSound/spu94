---
gsd_state_version: 1.0
milestone: v1.7
milestone_name: DAW Plugin Port
status: planning
stopped_at: "Milestone opened -- defining requirements"
last_updated: "2026-05-10T23:00:00Z"
last_activity: 2026-05-10 -- v1.7 DAW Plugin Port milestone started
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-10)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.7 DAW Plugin Port -- defining requirements

## Current Position

Phase: Not started (defining requirements)
Plan: --
Status: Defining requirements
Last activity: 2026-05-10 -- v1.7 milestone started

Progress: 0% (planning)

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Early decisions affecting v1.7:

- DAW plugin port packages SPU-94 in four formats (VST3 + AU + LV2 + CLAP) across three OSes (Linux + macOS + Windows); AU is macOS-only, so 10 unique binaries total
- Bit-faithful C core (libspu94) stays untouched -- SR and bit-depth compatibility is a wrapper concern only
- Real-time sample-rate conversion in the plugin wrapper (host any-rate <-> 44.1k core); JUCE interpolator pick TBD during requirements
- Real-time bit-depth conversion (host float32 <-> int16 core)
- Standalone (v1.6) continues to ship alongside the plugin formats
- Custom plugin UI redesign is out of scope -- current standalone GUI reused
- Beta-tester preset return-loop mechanism is out of scope -- handled via whatever channel already in use
- Code signing / notarization may be deferred for beta phase; revisit on install friction

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
| Archive cleanup | `.planning/milestones/v1.6-phases/` currently contains the abandoned master-branch macro work, not the live v1.6 user-waypoint phases; live phases 10-20 (v1.3 through v1.6) sit in `.planning/phases/` rather than archived. Separate cleanup pass. | Flagged | 2026-05-10 |

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Performance | Smooth Memory Drain -- gradual buffer decay | 2026-04-29 |
| Performance | Parameter Slew Control | 2026-04-29 |
| UI Enhancement | Stereo Link toggle -- lock/unlock L/R values | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override | 2026-04-29 |
| Distribution | Visual signal flow diagram as GUI | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) | 2026-04-29 |
| Performance | Buffer Base step-lock | 2026-05-01 |
| Performance | Resistance to Feedback meta-control | 2026-05-01 |
| Creative | Codec re-sync effect | 2026-05-01 |
| Experiment | Independent clamping (ratio drift) | 2026-05-03 |
| Visualization | Real-time room geometry visualizer | 2026-05-03 |
| Visualization | Polytope room shapes | 2026-05-03 |
| Documentation | Register reference manual | 2026-05-03 |
| Idea | Unified Morph Control -- merge Morph Speed + Morph Grit into one encoder | 2026-05-10 |

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 18 | 01 | retroactive | 4 | 4 |
| 19 | 01 | retroactive | 4 | 4 |
| 19 | 02 | retroactive | 8 | 9 |
| 20 | 01 | retroactive | 4 | 3 |
| 17 | 02 | (v1.5)   | -   | -   |
| 16 | 01 | 50m 19s | 2 | 6 |

## Session Continuity

Last session: 2026-05-10T23:00:00Z
Stopped at: v1.7 milestone opened; PROJECT.md and STATE.md updated; about to define requirements
Resume file: none
