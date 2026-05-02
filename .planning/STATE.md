---
gsd_state_version: 1.0
milestone: v1.4
milestone_name: Preset System
status: executing
stopped_at: Phase 14 planned — 2 plans in 1 wave, ready to execute
last_updated: "2026-05-02T13:00:00Z"
last_activity: 2026-05-02 -- Phase 14 planned (2 plans, verification passed)
progress:
  total_phases: 3
  completed_phases: 1
  total_plans: 4
  completed_plans: 2
  percent: 50
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-01)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.4 Preset System -- Phase 14 I/O Surfaces

## Current Position

Phase: 14 of 15 (I/O Surfaces)
Plan: 2 plans (0/2 complete), Wave 1
Status: Ready to execute
Last activity: 2026-05-02 -- Phase 14 planned (2 plans, 1 wave, verification passed)

Progress: [███░░░░░░░] 33%

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
- Init preset (slot 10): Hall coefficients with L/R geometry collapsed to mono -- user expands stereo from scratch
- Scale slider: ratio-locked register scaling from preset baseline, excludes vLIN/vRIN/vLOUT/vROUT
- syncShadowsFromSPU reads pending values (not active) so tick-latched registers show correctly after preset load

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
| Performance | Tap tempo sync -- quantize d*/m* registers to BPM subdivisions for rhythmic reverb | 2026-05-01 |
| Performance | Buffer Base step-lock -- mBASE snaps to discrete lockable positions for deliberate buffer jumps | 2026-05-01 |
| Performance | Resistance to Feedback -- meta-control inversely linking pro/anti-feedback registers with adjustable ceiling | 2026-05-01 |
| Creative | Codec re-sync effect -- deliberate frame boundary misalignment in codec decode for glitch textures | 2026-05-01 |

## Session Continuity

Last session: 2026-05-02T13:00:00Z
Stopped at: Phase 14 planned — ready to execute
Resume file: .planning/phases/14-i-o-surfaces/14-01-PLAN.md
