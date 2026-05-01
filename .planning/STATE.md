---
gsd_state_version: 1.0
milestone: v1.4
milestone_name: Preset System
status: executing
stopped_at: Phase 13 complete (2/2 plans), ready for Phase 14
last_updated: "2026-05-01T21:53:52Z"
last_activity: 2026-05-01 -- Phase 13 Plan 02 executed (preset load parser + round-trip tests)
progress:
  total_phases: 3
  completed_phases: 1
  total_plans: 2
  completed_plans: 2
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-01)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.4 Preset System -- Phase 13 Core Preset API

## Current Position

Phase: 13 of 15 (Core Preset API)
Plan: 02 of 02 (complete)
Status: Phase 13 complete, ready for Phase 14
Last activity: 2026-05-01 -- Phase 13 Plan 02 executed (preset load parser + round-trip tests)

Progress: [██████████] 100%

## Performance Metrics

**Velocity:**

- Total plans completed: 8 (v1.3), 12 (v1.2), 10 (v1.1), 37 (v1.0)
- Prior milestone: v1.3 shipped 8 plans across 3 phases

**By Phase (v1.3):**

| Phase | Plans | Total | Avg/Plan |
|-------|-------|-------|----------|
| 10-core-polyphase-fir-cascade | 4/4 | -- | -- |
| 11-noise-recalibration-integration | 2/2 | -- | -- |
| 12-verification-characterization | 2/2 | -- | -- |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
Recent decisions affecting current work:

- All DSP signal flow lives in C core -- JUCE/CLI/Python are thin wrappers with no DSP logic
- Preset save/load is a C core responsibility (consistent with above)
- Send/return mixer architecture: 3 buses, 6 faders -- all must be captured in preset state
- v1.3 DAC mode (A/B toggle) must be captured in preset state alongside v1.2 DAC toggle
- EMIT macro pattern for overflow-safe snprintf buffer writes (Phase 13 Plan 01)
- Hand-rolled parse_hex_u16 avoids strtol/long grep-guard ban (Phase 13 Plan 01)
- strchr-based key=value splitting for const char* parser (Plan 02 -- strtok modifies strings)
- 512-byte stack line buffer with truncation for parser line safety (Plan 02)

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
| Performance | Memory Flush button -- instant spu94_reset to kill feedback runaway | 2026-04-29 |
| Performance | Smooth Memory Drain -- gradual buffer decay with user-controlled drain rate (fast kill -> slow tail-off -> freeze) | 2026-04-29 |
| Performance | Parameter Slew Control -- knob controlling parameter transition speed (raw digital crunch <-> smooth). M4 lever layer. | 2026-04-29 |
| UI Enhancement | Stereo Link toggle -- lock/unlock L/R register values for controllable vs asymmetric feedback | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override -- force specific filter pairs for tonal control | 2026-04-29 |
| Distribution | Static linking for standalone (done -- single 7.6MB executable, no .so dependency) | 2026-04-29 |
| Distribution | Cross-platform build guides (Mac + Windows) -- audience TBD, waiting on Anthony | 2026-04-29 |
| Distribution | Visual signal flow diagram as GUI -- Ensoniq/ASM Hydrasynth style panel layout. Future UI overhaul. | 2026-04-29 |
| Feature | Anthony has a screenshot of preset #1 to capture once save system exists | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) -- crossfade two adjacent rates for smooth knob feel. Desktop-only. | 2026-04-29 |

## Session Continuity

Last session: 2026-05-01T21:53:52Z
Stopped at: Phase 13 complete (2/2 plans), ready for Phase 14
Resume file: None
