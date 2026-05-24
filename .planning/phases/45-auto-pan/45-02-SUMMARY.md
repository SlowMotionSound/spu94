---
phase: 45-auto-pan
plan: 02
subsystem: gui
tags: [auto-pan, gui, sampler-window, mutual-exclusion, knobs, toggle]

# Dependency graph
requires:
  - phase: 45-auto-pan
    plan: 01
    provides: "Auto-pan atomics and getters (getAutoPanEnabled/SpeedHz/Depth/Ratio)"
  - phase: 44-tremolo
    plan: 02
    provides: "Tremolo GUI pattern (section label, toggle, knobs, layout)"
provides:
  - "Auto-pan GUI section in sampler panel (Enable, Speed, Depth, L/R Ratio)"
  - "Bidirectional mutual exclusion between tremolo and auto-pan"
  - "Three-way VCA ramp ARM gating (disabled when either tremolo or auto-pan active)"
affects: [51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["bidirectional mutual exclusion via toggle onStateChange lambdas", "conditional rampArmButton re-enable (only when BOTH effects off)"]

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h

key-decisions:
  - "No curve button for auto-pan -- PAN-04 mandates linear-only (mode=0 always)"
  - "Default speed 2.0 Hz (slower than tremolo's 5.0 Hz since panning is more noticeable)"
  - "Panel height increased from 920px to 1030px to accommodate new section"
  - "VCA ramp ARM only re-enabled when BOTH tremolo and auto-pan are off (three-way guard)"
  - "Ratio knob placed at x=200 (shifted left vs tremolo since no curve button occupies that slot)"

patterns-established:
  - "Conditional rampArmButton enable: check other toggle state before re-enabling"

requirements-completed: [PAN-06]

# Metrics
duration: 3min
completed: 2026-05-24
---

# Phase 45 Plan 02: Auto-Pan GUI Controls Summary

**Auto-pan section in sampler panel with 3 rotary knobs and bidirectional mutual exclusion with tremolo**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-24T20:50:06Z
- **Completed:** 2026-05-24T20:53:21Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments
- Added "Auto-Pan" labeled section below tremolo in sampler window with teal-ticked Enable toggle
- Speed knob: 0.5-19.0 Hz, skew midpoint 4.0, default 2.0 Hz, stores to getAutoPanSpeedHz()
- Depth knob: 0-100%, default 100%, stores value/100.0 to getAutoPanDepth()
- L/R Ratio knob: 0.5-4.0, default 1.0, displays "1:X.X" format, stores to getAutoPanRatio()
- Bidirectional mutual exclusion: enabling auto-pan disables tremolo toggle + VCA ARM; enabling tremolo disables auto-pan toggle
- Three-way VCA ARM guard: rampArmButton only re-enables when both tremolo AND auto-pan are off
- All knobs start disabled, enable only when auto-pan toggle is checked
- Sampler window height increased from 920px to 1030px to fit new section
- No curve button (linear-only per PAN-04) -- faithful to PS1 hardware crossfade
- Full project builds clean with no warnings

## Task Commits

Each task was committed atomically:

1. **Task 1: Auto-pan GUI member declarations and initialization** - `de85746` (feat)

## Files Created/Modified
- `src/plugin/PluginEditor.h` - Added 8 auto-pan GUI member declarations (section label, toggle, 3 knobs, 3 labels)
- `src/plugin/PluginEditor.cpp` - Added auto-pan init block (70 lines) + mutual exclusion in tremolo lambda + layout bounds in resized()
- `src/plugin/SamplerWindow.h` - Increased panel height from 920 to 1030px

## Decisions Made
- Auto-pan defaults to 2.0 Hz (slower than tremolo) because panning movement is more perceptually obvious than amplitude modulation
- No curve button -- the linear crossfade IS the PS1 character; equal-power would be unfaithful
- Ratio knob placed at x=200 instead of tremolo's x=300 since no curve button occupies the middle position
- Panel height increased by 110px (exactly the vertical space consumed by the auto-pan section)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 51 (GUI Integration) can connect host automation parameters to auto-pan atomics
- Manual verification: launch standalone, open sampler panel, see "Auto-Pan" section below "Tremolo"
- Toggle auto-pan on -> tremolo toggle grays out, VCA ARM grays out
- Toggle tremolo on -> auto-pan toggle grays out

## Self-Check: PASSED
