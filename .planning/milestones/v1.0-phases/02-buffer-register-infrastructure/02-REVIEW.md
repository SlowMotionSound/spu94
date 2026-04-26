---
phase: 02-buffer-register-infrastructure
reviewed: 2026-04-19T00:00:00Z
depth: standard
files_reviewed: 31
files_reviewed_list:
  - .github/workflows/ci.yml
  - docs/DECISIONS.md
  - include/spu94/spu94.h
  - include/spu94/spu94_q15.h
  - include/spu94/spu94_register_facade.h
  - include/spu94/spu94_registers.h
  - scripts/ci/verify-no-heap-symbols.sh
  - src/spu94/CMakeLists.txt
  - src/spu94/spu94_buffer.c
  - src/spu94/spu94_pending.c
  - src/spu94/spu94_register_io.c
  - src/spu94/spu94_registers.c
  - src/spu94/spu94_state.c
  - src/spu94/spu94_state_internal.h
  - src/spu94/spu94_tick.c
  - src/spu94/spu94_write_policy.c
  - tests/CMakeLists.txt
  - tests/api/CMakeLists.txt
  - tests/api/c99_consumer.c
  - tests/api/cxx_consumer.cpp
  - tests/python/CMakeLists.txt
  - tests/python/fuzz_buffer.py
  - tests/unit/CMakeLists.txt
  - tests/unit/buffer/CMakeLists.txt
  - tests/unit/buffer/test_buffer_basic.c
  - tests/unit/buffer/test_buffer_mbase.c
  - tests/unit/buffer/test_buffer_wrap.c
  - tests/unit/q15/test_q15.c
  - tests/unit/registers/CMakeLists.txt
  - tests/unit/registers/test_register_edges.c
  - tests/unit/registers/test_register_facade.c
  - tests/unit/registers/test_register_identity.c
  - tests/unit/registers/test_register_io.c
  - tests/unit/registers/test_register_policy.c
  - tests/unit/registers/test_register_roundtrip.c
  - tests/unit/registers/test_register_types.c
  - tests/unit/state/CMakeLists.txt
  - tests/unit/state/test_state_lifecycle.c
findings:
  critical: 0
  warning: 2
  info: 6
  total: 8
status: issues_found
---

# Phase 2: Code Review Report

**Reviewed:** 2026-04-19
**Depth:** standard
**Files Reviewed:** 38 (per the explicit scope list)
**Status:** issues_found

## Summary

Phase 2 lands the BufferAddress wrap arithmetic, the 35-register identity surface, the typed engine-layer setters/getters with split write-timing policy, the 105-wrapper hand-written facade, and the `spu94_tick` ordering contract. The implementation is bit-faithful to ADR-0005 (split write policy) and ADR-0006 (mBASE snap-on-write, including the odd-pass-through corner). Null-safety, type-mismatch enforcement, out-of-range handling, and the heap-free invariant are all systematically guarded. Test coverage is excellent: per-register batteries (round-trip, types, policy, edges), buffer wrap-formula corners, snap-on-write semantics, work-buffer untouched invariant, tick-order observability, and a 10^6-step Python ctypes fuzz harness with an independent Python reference model.

No correctness or security issues were found in the production code. Two Warning-level findings are documentation/test-naming drift that obscures intent for future readers; the remaining six Info items are minor comment/dead-code polish.

## Warnings

### WR-01: Misleading test name and comment in `test_advance_from_top_wraps_to_zero`

**File:** `tests/unit/buffer/test_buffer_basic.c:89-102`
**Issue:** The test is named `test_advance_from_top_wraps_to_zero` and its inline comment claims `(0x7FFFE+2)&0x7FFFE=0; MAX(0,0)=0`, but the test does not exercise that case. Because `mBASE` is a `uint16_t` register, `0x7FFFE` cannot be set through the public API — the test sets `mBASE=0xFFFE`, which makes the floor `0xFFFE`. The test then asserts `buffer_address == 0x10000` after one tick (i.e., the floor-active arm of the formula, not the top-of-window wrap). The wrap-from-top corner is in fact only reachable via the Python fuzz harness (262K-tick brute force) — `test_buffer_wrap.c` documents this constraint correctly at lines 1-10. The combination of misleading name + inaccurate body comment gives a future reviewer the wrong mental model of what is verified by the C suite.
**Fix:** Rename the test to reflect what it actually checks (e.g., `test_advance_from_max_u16_mbase_with_floor_active`) and replace the contradictory inline comment with one that matches the actual arithmetic, mirroring the language already used in `test_buffer_wrap.c::test_advance_at_max_u16_mbase` (lines 107-118). Optionally add a one-line cross-reference pointing to the Python fuzz as the place where the true wrap-from-top corner is exercised.

### WR-02: `verify-no-heap-symbols.sh` swallows `nm`/`readelf` failures under pipefail

**File:** `scripts/ci/verify-no-heap-symbols.sh:39, 47`
**Issue:** The script runs `set -euo pipefail`, then uses `nm -u "$LIB" 2>/dev/null | grep -qE '\b(malloc|...)\b'` inside an `if` condition. With `pipefail`, the pipeline's exit status reflects the rightmost non-zero command — but `if` masks the exit status entirely, so a silent `nm`/`readelf` failure (corrupt library, missing tool, permission error) is indistinguishable from "no forbidden symbols found." The script then prints `OK: ...` and exits 0, defeating the SC-1 guarantee. The `2>/dev/null` redirect makes the failure mode invisible. Because this is a CI gate for the no-heap invariant (API-01), a false negative undermines a load-bearing safety property.
**Fix:** Capture the tool output once, then grep its contents, so tool failure is a separate signal from "no match." Example:
```bash
NM_OUT=$(nm -u "$LIB" 2>&1) || { echo "FAIL: nm failed on $LIB" >&2; echo "$NM_OUT" >&2; exit 2; }
if echo "$NM_OUT" | grep -qE '\b(malloc|calloc|realloc|free)\b'; then
    echo "FAIL: $LIB references heap functions via undefined symbols:" >&2
    echo "$NM_OUT" | grep -E '\b(malloc|calloc|realloc|free)\b' >&2
    FAIL=1
fi
# repeat the same pattern for readelf -r
```
Also remove the `2>/dev/null` in the test branches — if the tools are emitting stderr noise under normal operation, the cause should be diagnosed, not hidden.

## Info

### IN-01: Dead `char msg[64]` buffer in `test_mbase_does_not_mutate_work_buf`

**File:** `tests/unit/buffer/test_buffer_mbase.c:91-95`
**Issue:** A local `char msg[64];` is declared and immediately silenced with `(void)msg;`, but `TEST_FAIL_MESSAGE` is invoked with a literal string. The buffer was likely intended for a `snprintf` formatting call that was never written. Reads as "I started to format something, then gave up" rather than deliberate.
**Fix:** Either remove the dead `msg`/`(void)msg` lines entirely, or actually format the failure with the index `i` and the observed byte (which would be more useful for diagnosis):
```c
char msg[80];
snprintf(msg, sizeof msg,
    "work_buf[%zu] = 0x%02X (expected 0xAB) — ADR-0006 violation",
    i, g_work_buf[i]);
TEST_FAIL_MESSAGE(msg);
```

### IN-02: `spu94.h` comment cites stale location for the size `_Static_assert`

**File:** `include/spu94/spu94.h:67-72`
**Issue:** The block comment over `SPU94_STATE_SIZE_MAX` documents that the bound is enforced by `_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX, ...) in src/spu94/spu94_state.c`. The assertion was relocated to `src/spu94/spu94_state_internal.h:47` (the ODR-safe single home for the struct definition). `spu94_state.c:34-37` even calls out that move ("the alignment guard remains here because alignof() on the struct type is naturally checked in the TU that owns the lifecycle code"). The public header's documentation was not updated.
**Fix:** Update the comment to read `... in src/spu94/spu94_state_internal.h` (or "in the internal header that owns the struct definition"). Alternatively, drop the file path and just describe the invariant — file paths in public-header comments are easy to drift.

### IN-03: `spu94_state.c` init comment overstates what happens

**File:** `src/spu94/spu94_state.c:88`
**Issue:** The line `s->buffer_address = 0u; /* mBASE = 0 at init; snap-on-write makes addr=0 */` implies the snap-on-write side effect fires during init. It does not — `spu94_zero_bytes` zeros the whole state (including the mBASE storage cell), then `buffer_address` is written explicitly. No call to `spu94_mbase_on_write` occurs in `spu94_init`.
**Fix:** Adjust the comment to reflect what is actually happening:
```c
s->buffer_address = 0u; /* invariant from D-14: BufferAddress = mBASE = 0 post-init */
```

### IN-04: `_Static_assert` macro definition uses reserved identifier

**File:** `include/spu94/spu94_q15.h:12-14`
**Issue:** `#define _Static_assert(cond, msg) static_assert(cond, msg)` redefines a name in the reserved identifier space (`_` followed by uppercase). The `#ifdef __cplusplus` guard scopes the redefinition to C++ consumers (where `_Static_assert` is not a keyword), so the practical risk is low — but technically this collides with names reserved to the implementation in both C and C++. A pedantic C++ standard library implementation that uses `_Static_assert` as an internal macro could clash.
**Fix:** Two clean alternatives, in order of disruption:
1. Use a compatibility shim with a project-prefixed name throughout the header — replace each `_Static_assert(...)` site with `SPU94_STATIC_ASSERT(...)` and define that macro to expand to either `_Static_assert` (in C) or `static_assert` (in C++). Keeps every site portable without polluting the reserved namespace.
2. Leave as is and add a one-line code comment acknowledging the reserved-identifier tradeoff so a future reviewer knows it was considered.

### IN-05: `verify-no-heap-symbols.sh` `\b` boundaries miss underscore-adjacent symbols

**File:** `scripts/ci/verify-no-heap-symbols.sh:39, 47`
**Issue:** Per POSIX/PCRE, `\b` is a transition between a word char and a non-word char, and `_` is a word char. So a symbol like `__malloc_hook` or `xmalloc` does not register a `\b` adjacent to `malloc`, and the grep would NOT flag it. For the no-heap invariant the relevant target is bare `malloc`/`calloc`/`realloc`/`free` imports (which is what glibc actually exports), so today the check is sufficient. Worth noting because the matching style is shared with `scripts/ci/grep-guard.sh` per the file header — a future widening of the forbidden list (e.g., `aligned_alloc`, `posix_memalign`) might run into this.
**Fix:** No code change required for the current word list. If the list grows, prefer a stricter anchor pattern such as `(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free|aligned_alloc)([^A-Za-z0-9_]|$)` and verify against a representative symbol-naming corpus (e.g., glibc + musl + bionic).

### IN-06: `spu94_pending.c` loop uses `int` index against an enum count cast to `int`

**File:** `src/spu94/spu94_pending.c:30-34`
**Issue:** `for (int i = 0; i < (int)SPU94_REG__COUNT; ++i)` mixes signed `int` against an enum value, then shifts `UINT64_C(1) << i`. The shift amount is in `[0, 34]` so the operation is well-defined, and the same idiom is used throughout the codebase. Mentioned only because the surrounding code consistently uses `(int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT` for argument validation; `spu94_pending.c` could match the rest by using `for (size_t i = ...)` (matching `spu94_state.c`'s `spu94_zero_bytes` style) or by using a typed enum index — both are stylistic, not correctness, choices.
**Fix:** Optional. If consistency with `spu94_zero_bytes`/`spu94_registers.c::spu94_snapshot_registers` is preferred, change to:
```c
for (size_t i = 0; i < (size_t)SPU94_REG__COUNT; ++i) {
    if (mask & (UINT64_C(1) << i)) {
        state->reg_values[i] = state->pending_values[i];
    }
}
```
Note `spu94_registers.c::spu94_snapshot_registers` itself uses `int i` — both styles already coexist; pick one and apply uniformly across the four call sites in a future cleanup pass.

---

_Reviewed: 2026-04-19_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
