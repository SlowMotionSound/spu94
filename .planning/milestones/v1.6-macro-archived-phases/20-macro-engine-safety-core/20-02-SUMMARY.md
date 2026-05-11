---
phase: 20-macro-engine-safety-core
plan: 02
subsystem: dsp-macro-engine
tags: [c99, macro-engine, proportional-scaling, gang-clamp, re-derivation, ratio-preservation]

# Dependency graph
requires:
  - phase: 20-macro-engine-safety-core
    plan: 01
    provides: "spu94_safe_set_reg_i16/u16 safety wrappers, SPU94_STABILITY_CLAMPED/ADDRESS_CLAMPED result codes"
  - phase: 02-state-register
    provides: "register I/O layer (spu94_set_reg_i16/u16, reg_values[], write timing)"
provides:
  - "spu94_macro_register_group: register a group definition with the engine"
  - "spu94_macro_derive: re-derive knob position from current register state"
  - "spu94_macro_apply: proportional scaling with gang clamping via safe setters"
  - "spu94_macro_get_range: query effective knob range [0, 1]"
  - "spu94_macro_group_registered: check if group is registered"
  - "spu94_macro_group_t: group definition type with member array"
  - "spu94_macro_member_t: per-register floor/ceiling range type"
  - "spu94_macro_group_id_t: 8-group enum (Phase 21 populates definitions)"
  - "macro_writing re-entrancy guard in state struct"
  - "macro_base_values snapshot array in state struct"
affects: [21-gui-macro-overlay, phase-21-macro-definitions]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Offset-then-scale proportional model: base_offset = current - floor, scale_factor = position * max_scale"
    - "Gang clamping via min(headroom/offset) max_scale computation"
    - "Re-derivation as position = 1.0/max_scale (inverse of gang-clamp ceiling)"
    - "Zero-offset skip: members at floor have infinite headroom and stay at floor during sweep"
    - "Re-entrancy guard pattern (macro_writing) matching tempo_writing precedent"

key-files:
  created:
    - include/spu94/spu94_macro.h
    - src/spu94/spu94_macro.c
    - tests/unit/macro/CMakeLists.txt
    - tests/unit/macro/test_macro_group.c
    - tests/unit/macro/test_macro_gang.c
    - tests/unit/macro/test_macro_derive.c
    - tests/unit/macro/test_macro_range.c
  modified:
    - src/spu94/spu94_state_internal.h
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Proportional model uses offset-then-scale: new_value = floor + base_offset * scale_factor (preserves ratios because all members scale uniformly)"
  - "Gang clamping is implicit via max_scale: position 1.0 always means most-constrained member at ceiling"
  - "Derive captures new base values (not cumulative): raw edits change the base ratios, future sweeps use new ratios"
  - "Knob range is always [0, 1] externally; dynamic range is internal to max_scale factor"
  - "Zero-offset members stay at floor during sweep (you cannot scale zero proportionally)"
  - "No umbrella include of spu94_macro.h in spu94.h: avoids circular include chain through state_internal.h"

patterns-established:
  - "Macro engine pattern: register group -> derive -> apply (caller workflow)"
  - "Proportional scaling: offset-then-scale with float scale_factor and int32_t arithmetic"

requirements-completed: [MACRO-01, MACRO-02, MACRO-03, MACRO-04, MACRO-05]

# Metrics
duration: 48min
completed: 2026-05-04
---

# Phase 20 Plan 02: Macro Engine Summary

**Offset-then-scale proportional engine with gang clamping via min(headroom/offset) max_scale, re-derivation as inverse max_scale position, and 22 unit tests across 4 suites proving ratio preservation**

## Performance

- **Duration:** 48 min
- **Started:** 2026-05-04T05:51:18Z
- **Completed:** 2026-05-04T06:39:10Z
- **Tasks:** 2 (1 standard + 1 TDD RED/GREEN)
- **Files created:** 7
- **Files modified:** 3

## Accomplishments

- Macro engine public header (`spu94_macro.h`) defining group types, member ranges, 8-group enum, and 5 API function signatures
- State struct additions: macro_writing guard, group_defs pointer array, base_values snapshot, knob_pos tracking
- Full implementation in `spu94_macro.c`: register_group, derive, apply, get_range, group_registered
- Proportional scaling model preserves register ratios at every knob position (MACRO-05)
- Gang clamping stops all registers when most-constrained hits ceiling (MACRO-02)
- Re-derivation captures raw register state and computes position (MACRO-04)
- Dynamic knob range reflected in derive position (MACRO-03)
- All register writes go through spu94_safe_set_reg_{i16,u16} (safety enforcement from Plan 01)
- Re-entrancy guard prevents hook interference during batch writes
- 22 unit tests across 4 suites with concrete numeric assertions

## TDD Gate Compliance

- RED gate: `0ebf628` (test commit -- 4 suites fail against stub)
- GREEN gate: `24cc177` (feat commit -- all 22 tests pass)
- No REFACTOR commit needed (code clean after GREEN)

## Task Commits

Each task was committed atomically:

1. **Task 1: Macro engine public header and state struct** - `f12679c` (feat)
2. **Task 2 RED: Failing macro tests with stub** - `0ebf628` (test)
3. **Task 2 GREEN: Full implementation** - `24cc177` (feat)

## Files Created/Modified

- `include/spu94/spu94_macro.h` - Public API: spu94_macro_group_t, spu94_macro_member_t, group_id enum, 5 function declarations
- `src/spu94/spu94_macro.c` - Engine implementation: register_group, derive (position = 1/max_scale), apply (scale_factor = pos * max_scale), get_range, group_registered
- `src/spu94/spu94_state_internal.h` - Added #include spu94_macro.h, macro_writing, macro_group_defs[], macro_base_values[][], macro_knob_pos[]
- `src/spu94/CMakeLists.txt` - Added spu94_macro.c to object library
- `tests/unit/CMakeLists.txt` - Added add_subdirectory(macro)
- `tests/unit/macro/CMakeLists.txt` - 4 test executables linked to unity + spu94_static
- `tests/unit/macro/test_macro_group.c` - 7 tests: register_and_derive, apply_proportional, ratio_preserved, floor_offset, zero_base_stays_zero, null_state, unregistered_group
- `tests/unit/macro/test_macro_gang.c` - 4 tests: gang_clamp_ceiling, no_change_above_clamp, ratios_at_clamp, gang_clamp_with_i16
- `tests/unit/macro/test_macro_derive.c` - 6 tests: derive_returns_fraction, derive_after_raw_edit, apply_at_derived_position_is_identity, ratio_preserved_after_raw_edit, derive_all_at_floor, derive_captures_flushed_values
- `tests/unit/macro/test_macro_range.c` - 5 tests: range_default, range_after_derive_near_ceiling, range_null_state, range_unregistered, derive_position_reflects_headroom

## Decisions Made

- **Offset-then-scale model:** new_value = floor + (base - floor) * scale_factor. This is cleaner than ratio-of-fractions because it reduces to uniform multiplication after subtracting floor.
- **max_scale is the gang-clamp ceiling:** min over all members of (ceiling - floor) / (base - floor). The most constrained member defines the travel.
- **Derive = inverse max_scale:** position = 1.0/max_scale represents the fraction of total travel at the current state. Intuitive: low position = lots of room to grow, high position = near ceiling.
- **Zero-offset skip:** Members at floor (base == floor) have offset 0, scale by 0, stay at floor. No division by zero. No need for special-casing.
- **No umbrella include:** spu94_macro.h is NOT included via spu94.h to avoid circular dependency through spu94_state_internal.h. Consumers include both headers directly.
- **Float arithmetic for scale factor:** Integer-only arithmetic would require fixed-point division and careful overflow handling for a 0.0001% accuracy improvement that doesn't matter for control surface math (audio correctness is in the safety layer).

## Deviations from Plan

None -- plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None -- no external service configuration required.

## Next Phase Readiness

- Macro engine is ready for Phase 21 to define 8 specific macro group tables (Room Size, Echo Physics, Decay, Reflectivity, Width, Early Reflections, Diffusion Amount, Diffusion Texture)
- Phase 21 GUI can wire knobs to spu94_macro_apply with derive on preset load
- Safety layer (Plan 01) is exercised by every macro apply call -- no unsafe register writes possible

---
*Phase: 20-macro-engine-safety-core*
*Completed: 2026-05-04*
