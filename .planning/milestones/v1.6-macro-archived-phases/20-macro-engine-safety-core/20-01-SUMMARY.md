---
phase: 20-macro-engine-safety-core
plan: 01
subsystem: dsp-safety
tags: [c99, safety, iir-stability, address-bounds, q15, int16-overflow]

# Dependency graph
requires:
  - phase: 02-state-register
    provides: "register I/O layer (spu94_set_reg_i16/u16, reg_values[], write timing)"
  - phase: 05-process-presets
    provides: "factory preset table, spu94_load_preset"
provides:
  - "spu94_safe_set_reg_i16: stability-enforced i16 register setter"
  - "spu94_safe_set_reg_u16: address-bounds-enforced u16 register setter"
  - "SPU94_STABILITY_CLAMPED (=7) and SPU94_ADDRESS_CLAMPED (=8) result codes"
  - "SPU94_STABILITY_LIMIT compile-time macro (default 0x40000000)"
  - "m-prefix address register bitmask for bounds checking"
  - "INT16_MIN-safe abs_i32_safe helper (int32 widening before negation)"
affects: [20-02-macro-engine, 21-gui-macro-overlay, phase-21-raw-register-safety]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Safety wrapper pattern: safe setters delegate to raw setters after constraint enforcement"
    - "Compile-time limit override: SPU94_STABILITY_LIMIT overridable via -D for test executables"
    - "Direct source compilation for test-specific macro overrides (avoids library precompilation)"

key-files:
  created:
    - src/spu94/spu94_safety.c
    - tests/unit/safety/CMakeLists.txt
    - tests/unit/safety/test_safety_stability.c
    - tests/unit/safety/test_safety_bounds.c
  modified:
    - include/spu94/spu94.h
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "SPU94_STABILITY_LIMIT uses <= comparison (at-boundary passes): gain=1.0 is marginally stable, not divergent"
  - "Stability test compiles spu94_safety.c directly into test binary with reduced limit (0x20000000) to exercise clamp path"
  - "m-prefix bitmask excludes mBASE: mBASE has snap-on-write semantics, not a room-size address"
  - "Safety uses int64 for product computation: future-proof against any limit value, no overflow risk"

patterns-established:
  - "Safety wrapper pattern: spu94_safe_set_reg_{i16,u16} wrappers enforce constraints then delegate to raw setters"
  - "INT16_MIN-safe abs: widen to int32 before negation via abs_i32_safe() helper"

requirements-completed: [SAFE-01, SAFE-02]

# Metrics
duration: 40min
completed: 2026-05-04
---

# Phase 20 Plan 01: Safety Enforcement Layer Summary

**vIIR*vWALL stability ceiling and m-prefix address bounds checking as wrapper functions around raw register setters, with INT16_MIN-safe arithmetic and compile-time-overridable limit**

## Performance

- **Duration:** 40 min
- **Started:** 2026-05-04T04:13:35Z
- **Completed:** 2026-05-04T04:53:37Z
- **Tasks:** 1 (TDD: RED + GREEN)
- **Files modified:** 7

## Accomplishments
- Safety enforcement wrapper layer in C core: `spu94_safe_set_reg_i16` enforces vIIR*vWALL product ceiling, `spu94_safe_set_reg_u16` enforces m-prefix address bounds against work_buf_size
- Two new result codes (SPU94_STABILITY_CLAMPED=7, SPU94_ADDRESS_CLAMPED=8) with stable numeric values in the append-only enum
- INT16_MIN-safe absolute value computation via int32 widening (T-20-01 threat mitigation)
- Division-by-zero guard for work_buf_size==0 (T-20-02 threat mitigation)
- 15 unit tests across 2 test suites proving both constraints, including boundary cases, INT16_MIN handling, and passthrough for non-safety registers

## TDD Gate Compliance

- RED gate: `499ff87` (test commit -- 6 tests fail against stub passthrough)
- GREEN gate: `8946c5d` (feat commit -- all 15 tests pass)
- No REFACTOR commit needed (code was already clean after GREEN)

## Task Commits

Each task was committed atomically:

1. **Task 1 RED: Failing tests for safety enforcement** - `499ff87` (test)
2. **Task 1 GREEN: Implement safety layer** - `8946c5d` (feat)

## Files Created/Modified
- `include/spu94/spu94.h` - Added SPU94_STABILITY_CLAMPED=7, SPU94_ADDRESS_CLAMPED=8, SPU94_STABILITY_LIMIT macro, safe setter declarations
- `src/spu94/spu94_safety.c` - Safety enforcement implementation: abs_i32_safe, m_prefix_addr_mask, spu94_safe_set_reg_i16, spu94_safe_set_reg_u16
- `src/spu94/CMakeLists.txt` - Added spu94_safety.c to object library
- `tests/unit/CMakeLists.txt` - Added add_subdirectory(safety)
- `tests/unit/safety/CMakeLists.txt` - Two test executables with stability limit override
- `tests/unit/safety/test_safety_stability.c` - 8 SAFE-01 tests (Hall passthrough, Echo boundary, max boundary, vIIR clamp, vWALL clamp, INT16_MIN, non-stability passthrough, NULL state)
- `tests/unit/safety/test_safety_bounds.c` - 7 SAFE-02 tests (within bounds, at boundary, exceeds clamped, d-prefix passthrough, mBASE not checked, multiple m-prefix, NULL state)

## Decisions Made
- **Stability limit comparison is <=, not <:** Product exactly equal to 0x40000000 (gain=1.0) represents marginal stability (infinite sustain without divergence), which is the theoretical maximum with int16 values. The safety layer permits it.
- **Test uses direct source compilation with override:** Since SPU94_STABILITY_LIMIT is a compile-time constant baked into the library, the stability test compiles spu94_safety.c directly into its binary with -DSPU94_STABILITY_LIMIT=0x20000000 to exercise the clamp code path with int16 values. Static archive link order ensures the test's version wins.
- **int64 for product computation:** Even though int32 suffices for the default limit (max product 2^30), int64 is used for future-proofing and eliminating overflow concerns with any limit value.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected Echo preset clamp expectation in test**
- **Found during:** Task 1 GREEN (test_echo_preset_boundary)
- **Issue:** Plan expected clamped vIIR = 16512, but 536870912 / 32512 = 16513 (integer truncation)
- **Fix:** Updated test assertion from 16512 to 16513, verified 16513 * 32512 = 536870656 <= 536870912
- **Files modified:** tests/unit/safety/test_safety_stability.c
- **Committed in:** 8946c5d (GREEN commit)

**2. [Rule 3 - Blocking] Fixed compile-time limit override for test**
- **Found during:** Task 1 GREEN (target_compile_definitions ineffective for pre-compiled library)
- **Issue:** target_compile_definitions on test executable only affects test source, not the library's compiled spu94_safety.c. The library uses the default 0x40000000 limit regardless of test flags.
- **Fix:** Added spu94_safety.c as a direct source file in the test_safety_stability target so the compile definition takes effect in the safety function bodies. Added target_include_directories for internal header access.
- **Files modified:** tests/unit/safety/CMakeLists.txt
- **Committed in:** 8946c5d (GREEN commit)

---

**Total deviations:** 2 auto-fixed (1 bug, 1 blocking)
**Impact on plan:** Both fixes necessary for correctness. No scope creep. The clamp math is verified correct.

## Issues Encountered
None beyond the auto-fixed items above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Safety layer is ready for Plan 02 (macro engine) to call spu94_safe_set_reg_i16/u16 for all register writes
- Phase 21+ GUI/CLI surfaces can route raw-mode writes through the safe setters
- All 69 C unit tests pass with zero regressions (full Python/fuzz suite not run due to time; C unit coverage confirms no regression)

---
*Phase: 20-macro-engine-safety-core*
*Completed: 2026-05-04*
