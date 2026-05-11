---
phase: 21-macro-controls
plan: 01
subsystem: dsp-core
tags: [macro-engine, spread-sweep, bipolar, gang-clamp, reference-values, c99]

# Dependency graph
requires:
  - phase: 20-macro-engine-safety-core
    provides: macro engine (register_group, apply, derive, get_range), safety layer (SAFE-01, SAFE-02), write_member_value/read_member_value helpers
provides:
  - Spread+Sweep dual-axis apply function (spu94_macro_apply_spread_sweep)
  - Bipolar single-register apply function (spu94_macro_apply_bipolar)
  - Reference value storage and management (spu94_macro_set_reference)
  - Derive functions for both modes (spread_sweep, bipolar)
  - Expanded 10-group enum with 16-slot headroom
  - is_bipolar field on group struct
  - State fields for spread/sweep positions, link toggles, constrained mode
affects: [21-02-PLAN (group definitions), 21-03-PLAN (coupling/controls), gui-phase]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Spread+Sweep dual-axis: spread_factor = spread * 2.0 (center=identity), sweep headroom gang-clamp"
    - "Bipolar center-detent: left half [floor,0], right half [0,ceiling], position [-1,1]"
    - "Reference values separate from base values: ref_values for Spread ratios, base_values for Phase 20 derive"

key-files:
  created:
    - tests/unit/macro/test_macro_spread_sweep.c
    - tests/unit/macro/test_macro_bipolar.c
  modified:
    - include/spu94/spu94_macro.h
    - src/spu94/spu94_macro.c
    - src/spu94/spu94_state_internal.h
    - tests/unit/macro/test_macro_gang.c
    - tests/unit/macro/CMakeLists.txt

key-decisions:
  - "Spread factor: 0->collapse to mean, 0.5->identity (reference), 1.0->2x exaggeration from mean"
  - "Sweep headroom: computed per-direction (up vs down) from spread-adjusted positions, gang-clamped"
  - "Bipolar rounding: +0.5 bias for positive, -0.5 bias for negative, with safety clamp afterward"
  - "Derive spread: mean-absolute-distance ratio (cur_mad / ref_mad) / 2.0 recovers spread"
  - "Derive sweep: (cur_mean - ref_mean) / headroom maps to sweep position"

patterns-established:
  - "Spread+Sweep apply: compute spread_values from ref_mean + offset*factor, compute sweep headroom, gang-clamp sweep offset, write via write_member_value"
  - "Bipolar apply: separate left/right half mapping with center detent at zero, safety floor/ceiling on group member definition"
  - "TDD commit sequence: test(RED) -> feat(GREEN) for engine functions"

requirements-completed: [SS-01, SS-02, SS-03, CTRL-01, SAFE-03, SAFE-04]

# Metrics
duration: 32min
completed: 2026-05-04
---

# Phase 21 Plan 01: Macro Engine Extension Summary

**Spread+Sweep dual-axis apply and bipolar center-detent apply with reference-value storage, expanding the Phase 20 unipolar engine to support the full Room Designer control surface**

## Performance

- **Duration:** 32 min
- **Started:** 2026-05-04T20:10:20Z
- **Completed:** 2026-05-04T20:43:02Z
- **Tasks:** 2
- **Files modified:** 7 (3 source/header, 2 test files created, 2 test files modified)

## Accomplishments

- Expanded macro group enum from 8 to 10 active groups (16 slots) with is_bipolar field: Echo Speed, Tap Position, Diff Amount/Texture/Position, Early Reflections, Room Size, Buffer, Decay, Reflectivity
- Implemented Spread+Sweep dual-axis apply: Spread adjusts proportional spacing from reference (0.5=identity), Sweep translates uniformly with gang-clamped headroom. Handles both unipolar [0,1] and bipolar [-1,1] sweep ranges.
- Implemented bipolar single-register apply: [-1,1] position with center detent at zero. Left half maps [floor,0], right half maps [0,ceiling]. Decay macro floor=-0x1000 enforces SAFE-03; exhaustive sweep test proves SAFE-04 (-0x8000 unreachable).
- Added reference value storage separate from base values: ref_values anchor Spread proportions to preset or user-overridden state, base_values continue to serve Phase 20 derive.
- State struct expanded from 1896 to 3664 bytes (12,720 bytes headroom remaining within 16384 max). New fields: macro_ref_values, macro_spread_pos, macro_sweep_pos, wall_link, same_cross_link, tap_constrained, diff_pos_constrained.

## Task Commits

Each task was committed atomically:

1. **Task 1: Expand macro header, enum, struct, and state** - `00bbdf4` (feat)
2. **Task 2 RED: Failing tests for Spread+Sweep and bipolar** - `20ad65b` (test)
3. **Task 2 GREEN: Implement engine functions** - `e694654` (feat)

## Files Created/Modified

- `include/spu94/spu94_macro.h` - Expanded enum (10 groups), is_bipolar field, 5 new API declarations
- `src/spu94/spu94_macro.c` - Spread+Sweep apply, bipolar apply, derive functions, reference management (+363 lines)
- `src/spu94/spu94_state_internal.h` - Phase 21 state block (ref values, spread/sweep positions, link/constrain flags)
- `tests/unit/macro/test_macro_spread_sweep.c` - 7 test functions covering dual-axis behavior
- `tests/unit/macro/test_macro_bipolar.c` - 8 test functions including SAFE-03/SAFE-04 exhaustive proof
- `tests/unit/macro/test_macro_gang.c` - Updated enum references (ECHO_PHYSICS->ECHO_SPEED, WIDTH->EARLY_REFL)
- `tests/unit/macro/CMakeLists.txt` - 2 new test targets

## Decisions Made

- **Spread factor mapping:** spread * 2.0, so center (0.5) gives factor 1.0 (identity). This gives equal travel for collapse (0->0.5) and exaggeration (0.5->1.0).
- **Sweep headroom:** Computed per-direction (max_shift_up, max_shift_down) from spread-adjusted positions. The sweep offset is scaled by whichever direction it's moving. This avoids the two-pass gang clamp pitfall identified in 21-RESEARCH.md.
- **Bipolar rounding:** Positive half uses `+0.5f` bias, negative half uses `-0.5f` bias (toward zero). Safety clamp afterward ensures no value escapes [floor, ceiling].
- **Derive spread via MAD ratio:** Mean-absolute-distance (MAD) of current spacing vs reference spacing, divided by 2.0 to map back to [0,1]. Robust against sign cancellation.
- **Updated test_macro_gang.c enum references** rather than adding backward-compat aliases. Tests are internal; clean enum is preferable.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## TDD Gate Compliance

- RED gate: `20ad65b` (test commit with link-time failures confirming functions not yet implemented)
- GREEN gate: `e694654` (feat commit with all 15 new tests passing)
- REFACTOR gate: not needed (implementation is clean, follows established patterns)

## Next Phase Readiness

- Engine foundation complete for all Phase 21 controls
- Plans 02 and 03 can now define concrete group tables, register them, implement coupling/controls, and wire preset derivation
- All 6 macro test suites pass (4 Phase 20 + 2 Phase 21), zero regressions
- State struct has 12,720 bytes headroom for remaining Phase 21 fields

---
*Phase: 21-macro-controls*
*Completed: 2026-05-04*
