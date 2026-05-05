---
phase: 23-gui-overlay-display
plan: 02
subsystem: ui
tags: [juce, macro-panel, rotary-knobs, unit-conversion, timer-poll, ps1-palette]

# Dependency graph
requires:
  - phase: 23-gui-overlay-display
    plan: 01
    provides: "Lock-free atomic bridge: 17 macro knob positions, 6 toggles, 6 snap dropdowns, 17 derived positions"
  - phase: 21-macro-controls
    provides: "spu94_macro_apply_* and spu94_macro_derive_* C core API"
  - phase: 22-echo-speed-diffusion-snap
    provides: "spu94_snap_* toggle/subdivision/apply C core API"
provides:
  - "MacroPanel JUCE component: 22 rotary knobs, 8 toggles, 6 dropdowns organized into 7 labeled sections"
  - "Unit conversion display: register values shown as ms, meters, percent below each knob"
  - "Timer-driven update methods: updateKnobPositions, updateUnitLabels, updateSnapDropdowns"
  - "Feedback-loop-safe atomic callbacks with isUpdatingFromTimer guard"
affects: [23-03, 23-04]

# Tech tracking
tech-stack:
  added: []
  patterns: ["timer-poll ComboBox: no onChange callback, poll in timer when !isPopupActive (Linux hover-trigger workaround)", "isUpdatingFromTimer guard: prevents onValueChange from writing back to atomics during timer-driven knob updates", "bipolar center detent: QuantizedSlider snapFunction returns 0.0 when |v| < 0.02"]

key-files:
  created:
    - "src/standalone/MacroPanel.h"
    - "src/standalone/MacroPanel.cpp"
  modified:
    - "src/standalone/CMakeLists.txt"

key-decisions:
  - "Section ordering: Room Size+Buffer at top (primary access), then Walls, Echo Physics, Taps, Diffusion, Decay/Reflectivity, Early Reflections"
  - "Wall distance knobs are read-only (disabled, lighter outline) since they are driven by Room Size macro"
  - "Wall echo speed knobs write directly to RegisterBridge (individual register controls, not macro)"
  - "Unit labels use representative registers per section (e.g., mLSAME for room size, vAPF1 for diffusion amount)"

patterns-established:
  - "setupRotaryKnob helper: Rotary style, TextBoxBelow, psxDarkGray outline, psxTeal thumb, double-click-return-to-default"
  - "setupUnitLabel helper: blue text, 10pt font, centred, empty initial text"
  - "populateSubdivisionDropdown: 1-based ComboBox IDs from spu94_subdivision_to_string loop"

requirements-completed: [GUI-01, GUI-04, GUI-05, UNIT-01]

# Metrics
duration: 785s
completed: 2026-05-05
---

# Phase 23 Plan 02: MacroPanel Component Summary

**Self-contained JUCE MacroPanel with 22 rotary knobs, 8 toggles, 6 snap dropdowns across 7 labeled sections, PS1 color palette, unit conversion display, and feedback-safe atomic callbacks**

## Performance

- **Duration:** 13 min 5s
- **Started:** 2026-05-05T17:00:02Z
- **Completed:** 2026-05-05T17:13:07Z
- **Tasks:** 2
- **Files created:** 2 (MacroPanel.h, MacroPanel.cpp)
- **Files modified:** 1 (CMakeLists.txt)
- **Total new LOC:** 1,059

## Accomplishments

- MacroPanel.h: 136-line class declaration with all 22 knobs (QuantizedSlider for bipolar), 8 toggles, 6 dropdowns, 7 section headers, unit labels, timer update methods, and 4 unit conversion statics
- MacroPanel.cpp: 923-line implementation with full constructor (all 36 controls initialized with correct ranges, defaults, atomic callbacks), manual setBounds layout in resized(), paint() with section separator lines, three timer-driven update methods
- All 17 macro knob onValueChange callbacks guarded by isUpdatingFromTimer flag to prevent feedback loops (Pitfall 3 from RESEARCH.md)
- Bipolar center detent on Decay (-1 to +1), Reflectivity (-1 to +1), and Early Reflections Sweep (-1 to +1) using QuantizedSlider snapFunction with 0.02 dead zone
- Timer-poll pattern for all 6 ComboBox dropdowns (no onChange, only polls when !isPopupActive) -- Linux hover-trigger workaround
- Unit conversion: regToMs (22050 Hz sample rate), regToMeters (343 m/s speed of sound), regToPercent (Q15 full scale), regToPercentBipolar (signed with floor/ceiling)
- Wall distance knobs set to disabled/read-only with lighter outline color (driven by Room Size macro, not user-controllable)
- Wall echo speed knobs write directly to RegisterBridge (individual register controls, indices 14-17)
- updateSnapDropdowns() syncs toggle visual state from processor atomics with dontSendNotification
- Added MacroPanel.cpp to CMakeLists.txt target_sources
- Build succeeds with zero errors on full project (including all existing tests)

## Task Commits

Each task was committed atomically:

1. **Task 1: Create MacroPanel.h with full class declaration** - `19b638a` (feat)
2. **Task 2: Implement MacroPanel.cpp with construction, layout, callbacks, and unit conversion** - `4f42017` (feat)

## Files Created/Modified

- `src/standalone/MacroPanel.h` - Class declaration: MacroPanel : public juce::Component with all member knobs, labels, toggles, dropdowns, section headers, update methods, unit conversion statics
- `src/standalone/MacroPanel.cpp` - Full implementation: constructor (36 control init), paint (section separators), resized (manual layout), updateKnobPositions (17 derived + 8 shadow reads), updateUnitLabels (register-to-human conversion), updateSnapDropdowns (6 dropdown timer-poll + toggle sync)
- `src/standalone/CMakeLists.txt` - Added MacroPanel.cpp to target_sources list

## Decisions Made

- Section ordering places Room Size + Buffer at top for primary access, matching "Room Designer" concept where the room itself is the first thing you adjust
- Wall distance knobs are read-only indicators (disabled, lighter psxLightGray outline) since Room Size macro owns the m-prefix registers
- Wall echo speed knobs bypass the macro system entirely, writing directly to RegisterBridge for individual register control
- Unit labels show representative register values per section rather than trying to display all register values (e.g., mLSAME represents room dimension, vAPF1 represents diffusion amount)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- MacroPanel component is complete and compiles cleanly
- Plans 03 and 04 can now instantiate MacroPanel in PluginEditor, wire the Advanced toggle, and call updateKnobPositions/updateUnitLabels/updateSnapDropdowns from timerCallback
- All 17+6+6+17 processor atomics from Plan 01 are connected to their corresponding GUI controls

## Self-Check: PASSED

- FOUND: src/standalone/MacroPanel.h
- FOUND: src/standalone/MacroPanel.cpp
- FOUND: 19b638a (Task 1 commit)
- FOUND: 4f42017 (Task 2 commit)

---
*Phase: 23-gui-overlay-display*
*Completed: 2026-05-05*
