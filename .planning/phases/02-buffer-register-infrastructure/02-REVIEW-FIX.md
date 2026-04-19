---
phase: 02-buffer-register-infrastructure
fixed_at: 2026-04-19T00:00:00Z
review_path: .planning/phases/02-buffer-register-infrastructure/02-REVIEW.md
iteration: 1
findings_in_scope: 2
fixed: 2
skipped: 0
status: all_fixed
---

# Phase 2: Code Review Fix Report

**Fixed at:** 2026-04-19
**Source review:** `.planning/phases/02-buffer-register-infrastructure/02-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 2 (Critical=0, Warning=2; Info=6 deferred per `fix_scope=critical_warning`)
- Fixed: 2
- Skipped: 0

## Fixed Issues

### WR-01: Misleading test name and comment in `test_advance_from_top_wraps_to_zero`

**Files modified:** `tests/unit/buffer/test_buffer_basic.c`
**Commit:** `87d9e0f`
**Applied fix:** Renamed the test function from `test_advance_from_top_wraps_to_zero` to `test_advance_from_max_u16_mbase_with_floor_active` so the name matches what is actually verified (the floor-active arm of the formula at the upper end of the u16 mBASE range, NOT the wrap-from-top corner). Replaced the inaccurate inline comment that claimed `(0x7FFFE+2)&0x7FFFE=0; MAX(0,0)=0` with a description of the actual arithmetic (`MAX(0xFFFE, (0xFFFE+2)&0x7FFFE) = 0x10000`) and added a cross-reference to the Python ctypes fuzz harness (`tests/python/fuzz_buffer.py`) and `test_buffer_wrap.c` lines 1-10 as the place where the true wrap-from-top corner is exercised. Updated the matching `RUN_TEST(...)` registration in `main()`.

### WR-02: `verify-no-heap-symbols.sh` swallows `nm`/`readelf` failures under pipefail

**Files modified:** `scripts/ci/verify-no-heap-symbols.sh`
**Commit:** `103f551`
**Applied fix:** Refactored both forbidden-symbol checks to capture `nm -u` and `readelf -r` output once into intermediate variables (`NM_OUT`, `READELF_OUT`), then grep the captured contents. Tool-failure exit codes are now distinct from the "no forbidden symbols found" path: a non-zero `nm`/`readelf` exit aborts with exit code 2 (matching the existing "library not found" exit code) and prints the stderr/stdout it produced, rather than silently falling through to `OK: ... is heap-free`. Removed the `2>/dev/null` redirects on the diagnostic branches per the review guidance, so any tool stderr is surfaced rather than hidden. Added an inline comment explaining why the capture-then-grep pattern is required under `set -euo pipefail` (the previous `if nm | grep -q` form masked pipeline failure entirely). The forbidden-symbol match still sets `FAIL=1` and exits 1, preserving the original exit-code contract documented in the script header.

## Build & Test Verification

**Rebuild (`cmake --build build`):** clean — all 21 targets built (spu94 obj/shared/static, unity, and 17 test binaries) with no warnings or errors.

**Test suite (`ctest --test-dir build --output-on-failure`):**
```
100% tests passed, 0 tests failed out of 15
Total Test time (real) =   2.74 sec
```

All 15 ctest entries passed:
- q15_unit, state_lifecycle
- register_identity_unit, register_io_unit, register_facade_unit, register_roundtrip, register_types, register_policy, register_edges
- buffer_basic_unit (covers the renamed `test_advance_from_max_u16_mbase_with_floor_active`), buffer_wrap, buffer_mbase
- api_c99_consumer, api_cxx_consumer
- fuzz_buffer (2.72s — the 10^6-step Python ctypes harness)

No regressions introduced by either fix. The renamed buffer test still asserts the same arithmetic (the post-tick `buffer_address == 0x10000` invariant); only the function name and comment changed. The heap-symbols script still returns `OK` when run against the current `libspu94.so` build artifact.

## Skipped Issues

None — both in-scope Warning findings were applied cleanly with verification passing on the first attempt. Six Info findings (IN-01 through IN-06) remain deferred per the `fix_scope=critical_warning` policy.

---

_Fixed: 2026-04-19_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
