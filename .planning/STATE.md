---
gsd_state_version: 1.0
milestone: v1.7
milestone_name: DAW Plugin Port
status: planning
stopped_at: "Phase 22 plan APPROVED; paused before execute per user request (will /clear before executing)"
last_updated: "2026-05-11T19:00:00Z"
last_activity: 2026-05-11 -- Phase 22 PLAN.md drafted + plan-checker APPROVED first pass (4 cosmetic fixes applied); commit 68dfdce
progress:
  total_phases: 6
  completed_phases: 1
  total_plans: 2
  completed_plans: 1
  percent: 17
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-10)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.7 DAW Plugin Port -- defining requirements

## Current Position

Phase: 22 (Sample-Rate Conversion & Latency Reporting) -- planning complete, execute pending. v1.7 engineering risk hotspot.
Plan: 22-PLAN.md (4 atomic tasks + manual UAT) -- APPROVED by gsd-plan-checker (first pass, 4 cosmetic fixes applied)
Status: Paused before execute. User will /clear context before /gsd-execute-phase 22.
Last activity: 2026-05-11 -- Phase 22 PLAN.md committed (68dfdce); STATE rolled forward

Progress: 17% execution (1/6 firm phases complete; 1/? plans drafted for Phase 22)

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.7 locked decisions (as of requirements draft, 2026-05-11):

- 4 plugin formats (VST3 + AU + LV2 + CLAP) across 3 OSes (Linux + macOS + Windows); AU is macOS-only and LV2 is dropped on Windows -- 11 unique binaries total
- Bit-faithful C core (libspu94) stays untouched -- SR and bit-depth compatibility is wrapper-only
- SRC library: libsamplerate (BSD-2), Sinc Medium quality preset
- Dither at float->int16 boundary: truncate (no dither) -- period-faithful per North Star
- Source folder split: src/standalone/ -> src/plugin/; new small src/standalone/ holds testbed-only files (WavLoader)
- Channel buses: mono->mono, mono->stereo, stereo->stereo; mono duplicated into both reverb inputs (engine always stereo internally)
- 9 host-automatable parameters: Input Gain, ADPCM Send, Dry Send, Morph Position, Morph Speed, Morph Grit, Dry Level, ADPCM Level, Reverb Level
- Standalone reframed: internal dev/test tool, no longer a user deliverable from v1.7 onward
- Code signing deferred for beta -- testers click through Gatekeeper / SmartScreen; reactive re-evaluation only
- Custom plugin UI redesign out of scope -- current standalone GUI reused inside plugin window
- Beta-tester preset return-loop mechanism out of scope -- handled out-of-band
- UAT (User Acceptance Testing) DAW matrix deferred to packaging-and-testing phase

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

Last session: 2026-05-11T19:00:00Z
Stopped at: Phase 22 (SRC integration) PLAN.md drafted and APPROVED by plan-checker. CONTEXT + PLAN committed as 68dfdce. User asked to pause before execute and will /clear context first. Next step is /gsd-execute-phase 22 in a fresh session.
Resume file: .planning/phases/22-src-latency-reporting/22-PLAN.md
