---
phase: 08-m4-juce-plugin-product-v1-0
plan: 04
subsystem: dsp, ui
tags: [juce, wet-dry, crossfade, sqrt-pan-law, atomic, standalone]

# Dependency graph
requires:
  - phase: 08-03
    provides: "RegisterPanel with 18 sliders, PresetCommandQueue, processBlock with SPU passthrough"
provides:
  - "Wet/Dry rotary knob with equal-power crossfade in processBlock"
  - "Complete v1.0 standalone application: WAV load + playback + 18 register sliders + 10 presets + Wet/Dry mix"
affects: []

# Tech tracking
tech-stack:
  added: []
  patterns: ["Equal-power crossfade via sqrt pan law (std::sqrt, no allocation)", "Atomic float for lock-free GUI-to-audio parameter passing"]

key-files:
  created: []
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp

key-decisions:
  - "Wet/Dry default 0.5 (50/50 blend) matches user expectation for A/B evaluation"
  - "sqrt pan law chosen over linear for perceptually constant loudness across sweep"
  - "Toolbar layout extended: Wet/Dry knob positioned right of preset selector at x=590"
  - "Register panel y-offset lowered from 50 to 75 to accommodate knob height"

patterns-established:
  - "Atomic float for real-time-safe parameter passing (std::memory_order_relaxed for single-writer/single-reader)"

requirements-completed: [STANDALONE-06]

# Metrics
duration: 77min
completed: 2026-04-25
---

# Phase 8 Plan 04: Wet/Dry Mix Summary

**Equal-power Wet/Dry crossfade (sqrt pan law) with rotary knob, completing the full v1.0 standalone application**

## Performance

- **Duration:** 77 min
- **Started:** 2026-04-25T23:23:54Z
- **Completed:** 2026-04-26T00:41:00Z
- **Tasks:** 1 of 2 (Task 2 is human-verify checkpoint)
- **Files modified:** 4

## Accomplishments
- Wet/Dry rotary knob visible in the toolbar with range [0.0, 1.0], default 0.5
- Equal-power crossfade in processBlock using sqrt pan law: wetGain=sqrt(wet), dryGain=sqrt(1-wet)
- At 0% Wet: user hears unprocessed dry input only; at 100% Wet: SPU reverb only; at 50%: constant-power blend
- No new allocations in the audio thread -- std::sqrt is a pure math op, atomic float load is lock-free
- Build succeeds, all 66 C/C++ ctests pass with zero regressions
- Complete v1.0 standalone feature set: Load WAV, Play/Stop, 10-preset dropdown, 18 register sliders, Wet/Dry knob

## Task Commits

Each task was committed atomically:

1. **Task 1: Add Wet/Dry knob and equal-power crossfade** - `1c2e08c` (feat)

Task 2 is a `checkpoint:human-verify` gate -- awaiting Anthony's live UAT of all 9 STANDALONE criteria.

## Files Created/Modified
- `src/standalone/PluginProcessor.h` - Added std::atomic<float> wetDry{0.5f} and getWetDry() accessor
- `src/standalone/PluginProcessor.cpp` - Replaced direct SPU output with equal-power crossfade loop; added <cmath> include
- `src/standalone/PluginEditor.h` - Added wetDryKnob (juce::Slider) and wetDryLabel (juce::Label) members
- `src/standalone/PluginEditor.cpp` - Configured rotary knob (Rotary style, TextBoxBelow, range 0-1, default 0.5), wired onChange to atomic store, positioned in toolbar at x=590

## Decisions Made
- sqrt pan law (equal-power) chosen over linear crossfade to preserve perceived loudness at the midpoint -- standard audio engineering practice for wet/dry mixing
- Toolbar layout accommodates the rotary knob by shifting the register panel down from y=50 to y=75 -- keeps all controls visible without scrolling
- No custom painting on the knob -- stock JUCE Rotary style per D-04/STANDALONE-07

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- JUCE FetchContent shallow clone was incomplete in the worktree build (juce_graphics module missing). Resolved by copying the working JUCE source tree from the main repo's build directory and reconfiguring with FETCHCONTENT_FULLY_DISCONNECTED=ON. Build infrastructure issue only -- no code impact.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Task 2 (human-verify checkpoint) is the final gate: Anthony launches the standalone and verifies all 9 STANDALONE success criteria in person
- Upon checkpoint approval, Phase 8 and v1.0 standalone are complete
- No blockers remaining

## Self-Check: PASSED

- All 4 modified source files exist
- SUMMARY.md exists at expected path
- Commit 1c2e08c found in git log
- Build target spu94_standalone_Standalone compiles successfully
- 66/66 ctests pass

---
*Phase: 08-m4-juce-plugin-product-v1-0*
*Plan: 04*
*Completed: 2026-04-25 (Task 1); Task 2 checkpoint pending*
