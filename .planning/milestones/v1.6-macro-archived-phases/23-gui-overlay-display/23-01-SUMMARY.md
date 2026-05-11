---
phase: 23-gui-overlay-display
plan: 01
subsystem: ui
tags: [juce, atomics, lock-free, macro-bridge, processblock]

# Dependency graph
requires:
  - phase: 21-macro-controls
    provides: "spu94_macro_apply_* and spu94_macro_derive_* C core API"
  - phase: 22-echo-speed-diffusion-snap
    provides: "spu94_snap_* toggle/subdivision/apply C core API"
provides:
  - "Lock-free atomic bridge: 17 macro knob positions, 6 toggles, 6 snap dropdowns, 17 derived positions, 1 derive flag"
  - "processBlock macro apply section: reads all macro atomics, calls C core when values change"
  - "Derive-all handler: preset load and view-switch trigger full position re-derivation"
  - "Decay-Reflectivity coupling writeback in processBlock (Pitfall 4 prevention)"
affects: [23-02, 23-03, 23-04]

# Tech tracking
tech-stack:
  added: []
  patterns: ["macro atomic bridge: GUI stores float/bool atomics, processBlock reads with if-changed guards and calls C core", "derive-all pattern: requestDeriveAll flag triggers read-only derivation on audio thread, writes positions back to GUI atomics"]

key-files:
  created: []
  modified:
    - "src/standalone/PluginProcessor.h"
    - "src/standalone/PluginProcessor.cpp"

key-decisions:
  - "Macro atomics use relaxed memory ordering (consistent with existing project pattern for non-fence-critical GUI-audio handoff)"
  - "Derive-all writes both derived atomics AND macro GUI atomics (prevents stale knob positions after preset load)"
  - "Toggle pushes execute before knob position pushes (routing mode affects how knob values are interpreted)"

patterns-established:
  - "Macro if-changed pattern: load atomic, compare to lastPushed, call C core apply, update lastPushed, syncShadows"
  - "Derive-all pattern: flag-triggered, derives all positions, syncs lastPushed + GUI atomics, clears flag"
  - "Coupling writeback: after Decay apply, re-derive Reflectivity and store to derived atomic"

requirements-completed: [GUI-03, SAFE-05]

# Metrics
duration: 1666s
completed: 2026-05-05
---

# Phase 23 Plan 01: Macro Atomic Bridge Summary

**Lock-free atomic bridge for all 10 macro groups: 17 knob positions, 6 toggles, 6 snap dropdowns, 17 derived positions, and processBlock apply/derive logic connecting GUI knobs to C core macro/snap API**

## Performance

- **Duration:** 27 min 46s
- **Started:** 2026-05-05T16:27:10Z
- **Completed:** 2026-05-05T16:54:56Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- PluginProcessor.h expanded with ~135 lines: 17 macro knob atomics, 6 toggle atomics, 6 snap dropdown atomics (4 echo + 2 diff), 17 derived position atomics, requestDeriveAll flag, 17 lastPushed tracking fields, 6 lastPushed toggle/link fields, and matching public accessor methods
- processBlock macro apply section: ~200 lines following the existing tempo if-changed pattern for all macro groups including modal routing (echo speed and diff texture route through snap composite apply), Decay-Reflectivity coupling writeback (Pitfall 4), and toggle state pushes
- Derive-all handler reads all C core positions after preset load, writes to both derived and GUI atomics, resets all lastPushed fields to prevent false triggers
- Both factory and file preset drain paths now trigger requestDeriveAll

## Task Commits

Each task was committed atomically:

1. **Task 1: Add macro atomic fields and accessors to PluginProcessor.h** - `49a768e` (feat)
2. **Task 2: Add processBlock macro apply and derive-all logic to PluginProcessor.cpp** - `51d0cb5` (feat)

## Files Created/Modified
- `src/standalone/PluginProcessor.h` - Added #include spu94_macro.h/spu94_snap.h, 17+6+6+17+1 atomic fields, 17+6+6+17+1 public accessors, 17+6+6 lastPushed tracking fields
- `src/standalone/PluginProcessor.cpp` - Constructor initialization for snap/link arrays, processBlock macro apply section (derive-all, toggle push, snap dropdown push, knob position push for all 10 groups), requestDeriveAll triggers in both preset drain paths

## Decisions Made
- Relaxed memory ordering for all macro atomics (consistent with existing project pattern)
- Derive-all writes GUI atomics too (prevents one-frame stale knob display after preset load)
- Toggle pushes before knob pushes in processBlock ordering (toggles affect routing mode interpretation)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Atomic bridge is complete: Plans 02 and 04 (MacroPanel GUI) can now store knob positions via getMacro*() accessors and read derived positions via getDerived*() accessors
- All 18 macro/snap/safety C tests pass
- 4 pre-existing test failures (1 CLI preset list count, 2 packaging timeouts, 1 related CLI test) are unrelated to this plan's changes

## Self-Check: PASSED

- FOUND: src/standalone/PluginProcessor.h
- FOUND: src/standalone/PluginProcessor.cpp
- FOUND: .planning/phases/23-gui-overlay-display/23-01-SUMMARY.md
- FOUND: 49a768e (Task 1 commit)
- FOUND: 51d0cb5 (Task 2 commit)

---
*Phase: 23-gui-overlay-display*
*Completed: 2026-05-05*
