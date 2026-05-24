---
phase: 49-phase-modulator
plan: 02
subsystem: gui
tags: [phase-modulator, gui, controls, mutual-exclusion, sampler-window]

# Dependency graph
requires:
  - phase: 49-phase-modulator
    plan: 01
    provides: "Phase mod processor atomics (getPhaseModEnabled/SpeedHz/Depth)"
  - phase: 48-am-synthesis
    plan: 02
    provides: "AM section GUI pattern as template"
provides:
  - "Phase Mod section in sampler GUI (enable toggle, rate knob, depth knob)"
  - "Bidirectional mutual exclusion visual feedback with tremolo/auto-pan/AM"
  - "Sampler window resized to 1470 for new section"
affects: [51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["bidirectional mutual exclusion with timerCallback alpha + enabled state"]

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h

key-decisions:
  - "No curve button -- linear only per ADR-0060 (exponential ignores phase bit)"
  - "Rate range 0.5-19 Hz with skew midpoint at 4.0 Hz (same as tremolo)"
  - "Depth displayed as 0-100% but stored as 0.0-1.0 float (same pattern as tremolo/auto-pan)"
  - "Phase Mod has lowest priority in mutual exclusion hierarchy -- greys out when any higher-priority sweep active"
  - "Sampler window grows 110px from 1360 to 1470 to fit new section"

patterns-established:
  - "Phase mod toggle uses bidirectional exclusion: both directions grey out via timerCallback + onStateChange"

requirements-completed: [PMOD-05]

# Metrics
duration: 4min
completed: 2026-05-24
---

# Phase 49 Plan 02: Phase Modulator GUI Controls Summary

**Phase Mod section with enable/rate/depth in sampler window, bidirectional mutual exclusion with tremolo/auto-pan/AM**

## Performance

- **Duration:** 4 min
- **Started:** 2026-05-24T23:26:00Z
- **Completed:** 2026-05-24T23:29:59Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments
- Phase Mod section added to sampler GUI below AM Synthesis section
- Enable toggle with teal tick color, wires to processor.getPhaseModEnabled()
- Rate knob: rotary, 0.5-19 Hz, default 4.0 Hz, skew at 4.0, wires to getPhaseModSpeedHz()
- Depth knob: rotary, 0-100%, default 100%, wires to getPhaseModDepth() as 0.0-1.0
- No curve control (linear-only per ADR-0060 -- exponential ignores phase bit)
- Mutual exclusion (forward): toggle greys out + alpha 0.4 when tremolo/auto-pan/AM active
- Mutual exclusion (reverse): phase mod active greys out tremolo/auto-pan/AM toggles
- Tremolo, auto-pan, AM, and duck onStateChange handlers updated to include phaseModEnableToggle
- Sampler window height increased 1360 -> 1470 to accommodate new section
- ensureMinimumSize() updated to match new dimensions
- All plugin targets build clean (Standalone, VST3, CLAP, LV2)

## Task Commits

Each task was committed atomically:

1. **Task 1: Phase modulator GUI section with rate, depth, and mutual exclusion feedback** - `fd0bb68` (feat)

## Files Created/Modified
- `src/plugin/PluginEditor.h` - Added 6 phase mod GUI members (section label, enable toggle, rate knob+label, depth knob+label)
- `src/plugin/PluginEditor.cpp` - Phase mod constructor setup, resized layout, timerCallback bidirectional exclusion, updated tremolo/auto-pan/AM/duck handlers
- `src/plugin/SamplerWindow.h` - Panel height 1360->1470, ensureMinimumSize threshold updated

## Decisions Made
- Linear only (no curve button) per ADR-0060
- Same rate range as tremolo (0.5-19 Hz) for consistency
- Default depth 100% to immediately hear effect on enable
- timerCallback handles bidirectional exclusion visibility (alpha + enabled state)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 51 (GUI Integration) can wire host automation parameters to these atomics
- Manual verification: launch standalone, load sample, trigger voice, enable Phase Mod toggle -> hear polarity cycling

## Self-Check: PASSED
