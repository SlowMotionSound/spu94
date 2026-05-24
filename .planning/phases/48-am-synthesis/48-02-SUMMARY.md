---
phase: 48-am-synthesis
plan: 02
subsystem: gui
tags: [am-synthesis, gui, controls, mutual-exclusion, sampler-window]

# Dependency graph
requires:
  - phase: 48-am-synthesis
    plan: 01
    provides: "AM processor atomics (amEnabled, amRateHz, amDepth, amCurve) and DSP activation"
provides:
  - "AM synthesis GUI section in sampler window (enable toggle, rate knob, depth knob, curve button)"
  - "Bidirectional mutual exclusion between AM, tremolo, auto-pan, and sidechain duck"
affects: [51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["Same effect section layout pattern as tremolo/auto-pan (section label + enable toggle + knob row)"]

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h

key-decisions:
  - "AM section placed below Stereo Width, before Sampler Mixer knobs (maintains vertical effect stack order)"
  - "No L/R ratio knob for AM (both channels always at same rate for coherent metallic sidebands)"
  - "Panel height increased from 1250 to 1360 to fit AM section without cramping"
  - "Rate knob uses setSkewFactorFromMidPoint(500) for musical logarithmic feel across 37-7350 Hz"

patterns-established:
  - "Bidirectional mutual exclusion now covers 4 effects: tremolo <-> auto-pan <-> AM <-> sidechain duck"

requirements-completed: [AM-05]

# Metrics
duration: 3min
completed: 2026-05-24
---

# Phase 48 Plan 02: AM Synthesis GUI Controls Summary

**Enable toggle, Rate knob (37-7350 Hz log), Depth knob (0-100%), Curve button (Linear/Exponential) with full mutual exclusion in sampler window**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-24T23:05:09Z
- **Completed:** 2026-05-24T23:08:37Z
- **Tasks:** 1
- **Files modified:** 3

## Accomplishments
- Added AM Synthesis section to sampler window with section label, enable toggle, rate knob, depth knob, and curve toggle button
- Rate knob range 37-7350 Hz with logarithmic skew (midpoint 500 Hz) for musical sweep across the audio-rate spectrum
- Depth knob 0-100% normalized to 0.0-1.0 before atomic store (matches tremolo pattern)
- Curve button toggles Linear/Exponential, writes 0/1 to processor amCurve atomic
- Bidirectional mutual exclusion: enabling AM disables tremolo and auto-pan toggles; enabling tremolo or auto-pan disables AM toggle; duck active disables AM toggle
- Controls start disabled until Enable toggle is clicked (consistent with tremolo/auto-pan UX)
- Sampler window panel height increased from 1250px to 1360px to fit new section
- All plugin targets (Standalone, VST3, CLAP, LV2) build clean

## Task Commits

Each task was committed atomically:

1. **Task 1: AM section GUI components and layout** - `26f257d` (feat)

## Files Created/Modified
- `src/plugin/PluginEditor.h` - Added 7 AM GUI component declarations (label, toggle, 2 knobs, 2 labels, curve button)
- `src/plugin/PluginEditor.cpp` - AM section constructor initialization (73 lines), resized() layout (8 lines), mutual exclusion updates in tremolo/auto-pan/duck handlers
- `src/plugin/SamplerWindow.h` - Panel height 1250->1360, ensureMinimumSize threshold updated

## Decisions Made
- Rate knob midpoint 500 Hz (not 440) for the skew factor -- gives better knob feel since 440 is slightly below psychoacoustic center of the useful AM range
- AM mutual exclusion integrates into existing hierarchy (tremolo > auto-pan > AM > VCA ramp > sidechain duck on voice 0)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - launch standalone, load sample, trigger voice, click AM Enable to hear metallic sidebands.

## Self-Check: PASSED
