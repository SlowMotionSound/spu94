---
gsd_state_version: 1.0
milestone: v1.6
milestone_name: User Programmable Waypoints
status: milestone_shipped
stopped_at: "v1.6 shipped and archived -- ready for next milestone"
last_updated: "2026-05-10T22:00:00Z"
last_activity: 2026-05-10 -- v1.6 milestone archived and tagged
progress:
  total_phases: 3
  completed_phases: 3
  total_plans: 4
  completed_plans: 4
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-06)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.6 shipped -- planning next milestone

## Current Position

Milestone: v1.6 shipped and archived
Status: Ready for next milestone
Last activity: 2026-05-10 -- v1.6 archived, tag created

Progress: Milestone complete

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- 8 user-programmable waypoint slots at midpoints between Sony's 9 anchors (positions 1/16..15/16) — turns the morph dial into a 17-position continuum
- Empty slots are transparent — fresh project bit-identical to v1.5
- user_slots are per-engine state — multi-engine consumers MUST mirror writes (audible-glide root cause)
- Three per-tick action buttons (EDIT/EXPORT/LOAD) replace the floating Sony-preset dropdown
- REVERT = clear slot entirely (not 'discard edits') — user-requested emergency clear
- LOAD ignores file's slot index — drop-anywhere copy/paste between beta-tester slot files
- State management hoisted above audio-I/O gate — sliders/ticks reflect engine state regardless of WAV/playback
- Forced re-applies (LOAD, SAVE, REVERT) snap regardless of Morph Speed
- Empty slots omitted from preset serialization — byte-identical back-compat with pre-feature presets
- Default Morph Speed lowered 1.0 → 0.5 (mid-glide launch default)

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

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Performance | Smooth Memory Drain -- gradual buffer decay | 2026-04-29 |
| Performance | Parameter Slew Control | 2026-04-29 |
| UI Enhancement | Stereo Link toggle -- lock/unlock L/R values | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override | 2026-04-29 |
| Distribution | Cross-platform build guides (Mac + Windows) | 2026-04-29 |
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

Last session: 2026-05-10T22:00:00Z
Stopped at: v1.6 milestone archived and tagged; ready for next milestone selection
Resume file: none -- clean close
