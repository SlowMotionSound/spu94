---
phase: 04-sample-rate-conversion-39-tap-half-band-fir
fixed_at: 2026-04-20T00:00:00Z
review_path: .planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-REVIEW.md
iteration: 1
findings_in_scope: 2
fixed: 2
skipped: 0
status: all_fixed
---

# Phase 4: Code Review Fix Report

**Fixed at:** 2026-04-20
**Source review:** `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 2 (critical + warning; info findings skipped per scope)
- Fixed: 2
- Skipped: 0

All in-scope warnings were applied cleanly. Full build (`cmake --build build`)
and test suite (`ctest --test-dir build`, 38/38 tests passing including
`fuzz_fir`) succeeded after both fixes. `bash scripts/ci/grep-guard.sh`
remained clean (no banned token regressions).

## Fixed Issues

### WR-01: Undefined behavior in cascade-clamp path: signed left shift of negative value

**Files modified:** `src/spu94/spu94_fir.c`
**Commit:** 0e6a5c7
**Applied fix:** Replaced both instances of `(int32_t)sat_s16(running >> 15) << 15` (lines 116 and 124 of the pre-fix file) with an unsigned-space rescale `(int32_t)((uint32_t)(int32_t)sat_s16(running >> 15) << 15)`. This preserves two's-complement semantics while dodging C99 §6.5.7p4 UB when `sat_s16` returns a negative value. The production default (D-03 clamp-once) was unaffected — this only hardens the opt-in `SPU94_FIR_CASCADE_CLAMP` alternate path so UBSan builds (ADR-0015 witness tests) stay clean. Both instances received `/* WR-01: ... */` comments explaining the rationale.

Verification: syntax-checked with `gcc -std=c99 -pedantic -Wall -Wextra -fsyntax-only` both with and without `-DSPU94_FIR_CASCADE_CLAMP`. Full `cmake --build build` and `ctest` (38/38) succeeded.

### WR-02: Fuzz canary offset not validated against struct size

**Files modified:** `tests/python/fuzz_fir.py`
**Commit:** a60d2ae
**Applied fix:** Inserted a startup guard at `run_fuzz` entry (after buffer allocation, before `spu94_init`) that calls the already-loaded `lib.spu94_state_size()` accessor and aborts with a clear diagnostic if `CANARY_OFFSET (0x1000)` is inside the in-use struct footprint, or if `CANARY_OFFSET + sizeof(uint32)` would overflow `SPU94_STATE_SIZE_MAX`. Prevents a future struct-growth regression from silently turning `spu94_reset`'s zero-sweep into a bogus "canary drift" failure.

Verification: `python3 -c "ast.parse(...)"` syntax-checked. The 1e6-step `fuzz_fir` ctest target passed (3.37s), confirming the guard accepts the current struct size (~544 bytes << 0x1000).

---

_Fixed: 2026-04-20_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
