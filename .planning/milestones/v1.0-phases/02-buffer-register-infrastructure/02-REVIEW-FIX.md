---
phase: 02-buffer-register-infrastructure
fixed_at: 2026-04-19T00:00:00Z
review_path: .planning/phases/02-buffer-register-infrastructure/02-REVIEW.md
iteration: 2
findings_in_scope: 8
fixed: 8
skipped: 0
status: all_fixed
---

# Phase 2: Code Review Fix Report

**Fixed at:** 2026-04-19
**Source review:** `.planning/phases/02-buffer-register-infrastructure/02-REVIEW.md`
**Iteration:** 2 (combined report — covers iteration 1 Warnings + iteration 2 Info findings)

**Summary:**
- Findings in scope: 8 (Critical=0, Warning=2, Info=6)
- Fixed: 8 (WR-01, WR-02 in iteration 1; IN-01..IN-06 in iteration 2)
- Skipped: 0

## Fixed Issues

### WR-01: Misleading test name and comment in `test_advance_from_top_wraps_to_zero` *(iteration 1)*

**Files modified:** `tests/unit/buffer/test_buffer_basic.c`
**Commit:** `87d9e0f`
**Applied fix:** Renamed the test function from `test_advance_from_top_wraps_to_zero` to `test_advance_from_max_u16_mbase_with_floor_active` so the name matches what is actually verified (the floor-active arm of the formula at the upper end of the u16 mBASE range, NOT the wrap-from-top corner). Replaced the inaccurate inline comment that claimed `(0x7FFFE+2)&0x7FFFE=0; MAX(0,0)=0` with a description of the actual arithmetic (`MAX(0xFFFE, (0xFFFE+2)&0x7FFFE) = 0x10000`) and added a cross-reference to the Python ctypes fuzz harness (`tests/python/fuzz_buffer.py`) and `test_buffer_wrap.c` lines 1-10 as the place where the true wrap-from-top corner is exercised. Updated the matching `RUN_TEST(...)` registration in `main()`.

### WR-02: `verify-no-heap-symbols.sh` swallows `nm`/`readelf` failures under pipefail *(iteration 1)*

**Files modified:** `scripts/ci/verify-no-heap-symbols.sh`
**Commit:** `103f551`
**Applied fix:** Refactored both forbidden-symbol checks to capture `nm -u` and `readelf -r` output once into intermediate variables (`NM_OUT`, `READELF_OUT`), then grep the captured contents. Tool-failure exit codes are now distinct from the "no forbidden symbols found" path: a non-zero `nm`/`readelf` exit aborts with exit code 2 (matching the existing "library not found" exit code) and prints the stderr/stdout it produced, rather than silently falling through to `OK: ... is heap-free`. Removed the `2>/dev/null` redirects on the diagnostic branches per the review guidance, so any tool stderr is surfaced rather than hidden. Added an inline comment explaining why the capture-then-grep pattern is required under `set -euo pipefail` (the previous `if nm | grep -q` form masked pipeline failure entirely). The forbidden-symbol match still sets `FAIL=1` and exits 1, preserving the original exit-code contract documented in the script header.

### IN-01: Dead `char msg[64]` buffer in `test_mbase_does_not_mutate_work_buf` *(iteration 2)*

**Files modified:** `tests/unit/buffer/test_buffer_mbase.c`
**Commits:** `fef66a3` (initial fix), `3f308f1` (buffer-size bump for `-Wformat-truncation`)
**Applied fix:** Replaced the `char msg[64]; (void)msg;` dead pair with a real `snprintf` call that formats the offending `i` index plus the observed byte value, so a regression points straight at the corrupted offset rather than the generic "work_buf was mutated" string. Added `#include <stdio.h>` for `snprintf`. The first attempt used `char msg[96]` but gcc's `-Wformat-truncation=` warned that `%zu` on a 64-bit `size_t` could produce up to 99 bytes of output; a follow-on commit bumped the buffer to `char msg[128]` with an inline comment explaining the headroom calculation, satisfying the warning. The buffer-test target does NOT use `-Werror` (only `tests/api/CMakeLists.txt` does), so the original commit was not a build break, but the cleanup keeps the build warning-free.

### IN-02: `spu94.h` comment cites stale location for the size `_Static_assert` *(iteration 2)*

**Files modified:** `include/spu94/spu94.h`
**Commit:** `c47eca2`
**Applied fix:** Updated the documentation comment over `SPU94_STATE_SIZE_MAX` to cite `src/spu94/spu94_state_internal.h` (the ODR-safe single home for the struct definition) instead of the obsolete `src/spu94/spu94_state.c` location. The `_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX, ...)` was relocated when the internal header was extracted; the public-header comment had not followed. Also added the parenthetical "the internal header that owns the struct definition" so a future relocation is less likely to leave the comment stale again.

### IN-03: `spu94_state.c` init comment overstates what happens *(iteration 2)*

**Files modified:** `src/spu94/spu94_state.c`
**Commit:** `b0c9cf1`
**Applied fix:** Replaced the misleading inline comment (`mBASE = 0 at init; snap-on-write makes addr=0`) with an accurate multi-line comment that cites the D-14 invariant (`BufferAddress = mBASE = 0 post-init`) and explicitly notes that `spu94_zero_bytes` already cleared the mBASE storage cell — the explicit `s->buffer_address = 0u` assignment makes the invariant obvious without routing through the snap-on-write handler, which is NOT invoked during init.

### IN-04: `_Static_assert` macro definition uses reserved identifier *(iteration 2)*

**Files modified:** `include/spu94/spu94_q15.h`
**Commit:** `97500d1`
**Applied fix:** Replaced the C++ `#define _Static_assert(cond, msg) static_assert(cond, msg)` (which redefines a C reserved identifier — `_` followed by uppercase is reserved to the implementation per C17 §7.1.3 / C++17 [reserved.names]) with a project-prefixed `SPU94_STATIC_ASSERT(cond, msg)` macro. The new macro expands to `static_assert` in C++ and `_Static_assert` (the language keyword, not a redefinition) in C. Updated the single in-header use site (`SPU94_STATIC_ASSERT((((int16_t)-1) >> 1) == -1, ...)`) and the two prose comment references (`ASR, verified by SPU94_STATIC_ASSERT`, `ASR, per SPU94_STATIC_ASSERT above`). Verified both C11 and C++11 syntax-clean. Other source/header files in the codebase use `_Static_assert` as the C language keyword inside `.c` translation units and are untouched — only the header's macro REDEFINITION was the reserved-identifier concern.

### IN-05: `verify-no-heap-symbols.sh` `\b` boundaries miss underscore-adjacent symbols *(iteration 2)*

**Files modified:** `scripts/ci/verify-no-heap-symbols.sh`
**Commit:** `a12f3ff`
**Applied fix:** Replaced both `\b(malloc|calloc|realloc|free)\b` patterns with `(^|[^A-Za-z0-9_])(malloc|calloc|realloc|free)([^A-Za-z0-9_]|$)`. POSIX/PCRE treats `_` as a word character, so `\b` would not anchor against an underscore — meaning a future widening of the forbidden list (e.g., `aligned_alloc`, `posix_memalign`) would silently miss `__aligned_alloc_hook` or `xmalloc`. The explicit non-`[A-Za-z0-9_]` boundary pattern future-proofs the gate. Also expanded the Pass 1 inline comment to document the rationale ("we deliberately do NOT use `\b` here ...") and added a back-reference in the Pass 2 comment so the two checks share a regex contract. Smoke-tested the new pattern: it correctly matches `malloc`, `calloc`, `free@@GLIBC_2.2.5` (the `@` is not `[A-Za-z0-9_]`); correctly rejects `__malloc_hook`, `xmalloc`, `callocish`. The current `libspu94.so` still passes the gate (`OK: ... is heap-free`).

### IN-06: `spu94_pending.c` loop uses `int` index against an enum count cast to `int` *(iteration 2)*

**Files modified:** `src/spu94/spu94_pending.c`
**Commit:** `0d1a633`
**Applied fix:** Changed `for (int i = 0; i < (int)SPU94_REG__COUNT; ++i)` to `for (size_t i = 0; i < (size_t)SPU94_REG__COUNT; ++i)`, matching the `spu94_zero_bytes` sibling style in `spu94_state.c` and removing the signed/unsigned mismatch against the enum count. Added `#include <stddef.h>` for `size_t`. Inline comment notes that the `UINT64_C(1) << i` shift remains well-defined: `i` is in `[0, SPU94_REG__COUNT) = [0, 35)`, well below the 64-bit operand width, so no UB. Note the codebase still has the other style (`int i`) in `src/spu94/spu94_registers.c::spu94_snapshot_registers` — review notes both styles already coexist; this fix applies the size_t style only at the call site flagged by IN-06 and defers the broader codebase sweep to a future cleanup pass.

## Build & Test Verification

**Rebuild (`cmake --build build`):** clean — all 21 targets (spu94 obj/shared/static, unity, and 17 test binaries) built without warnings or errors after the IN-01 buffer-size cleanup commit (`3f308f1`). The earlier intermediate state had a `-Wformat-truncation=` warning on `tests/unit/buffer/test_buffer_mbase.c:97`; the buffer was bumped from 96B to 128B to satisfy the gcc warning analysis (max possible output: 99 bytes for a 20-char `%zu` on 64-bit). The buffer-test target does NOT enable `-Werror`, so this was not a build break — only a cleanup.

**Test suite (`ctest --test-dir build --output-on-failure`):**
```
100% tests passed, 0 tests failed out of 15
Total Test time (real) =   2.53 sec
```

All 15 ctest entries passed:
- q15_unit, state_lifecycle
- register_identity_unit, register_io_unit, register_facade_unit, register_roundtrip, register_types, register_policy, register_edges
- buffer_basic_unit (covers the iteration-1 renamed `test_advance_from_max_u16_mbase_with_floor_active`)
- buffer_wrap, buffer_mbase (covers the iteration-2 IN-01 snprintf-formatted failure path)
- api_c99_consumer, api_cxx_consumer (covers the iteration-2 IN-04 `SPU94_STATIC_ASSERT` macro through both C99 + pedantic + Werror and C++ + pedantic + Werror)
- fuzz_buffer (2.51s — the 10^6-step Python ctypes harness)

**No regressions** introduced by any of the 8 fixes. Direct verification per finding:

- **WR-01:** renamed buffer test still asserts `buffer_address == 0x10000` post-tick (function rename only).
- **WR-02 / IN-05:** heap-symbols script still exits `OK` against the current `libspu94.so`; the stricter regex still matches the canonical `malloc/calloc/realloc/free` glibc imports (none of which are present).
- **IN-01:** the `test_mbase_does_not_mutate_work_buf` test still passes — work_buf remains untouched by mBASE writes; the snprintf path is dormant unless a regression is introduced.
- **IN-02 / IN-03:** comment-only changes; no behavioral change.
- **IN-04:** `api_cxx_consumer` builds with `-Werror` and exercises the macro through C++; `api_c99_consumer` builds with `-Werror` and exercises it through C99 + pedantic — both paths use the new `SPU94_STATIC_ASSERT` shim.
- **IN-06:** `spu94_apply_pending_writes` is exercised by every register-write test that uses TICK_LATCHED policy plus `register_policy` directly; all pass.

## Skipped Issues

None — all 8 in-scope findings (2 Warning + 6 Info) were applied cleanly across both iterations with verification passing. Iteration 1 (commits `87d9e0f`, `103f551`) cleared the Warning findings; iteration 2 (commits `fef66a3`, `c47eca2`, `b0c9cf1`, `97500d1`, `a12f3ff`, `0d1a633`, `3f308f1`) cleared the Info findings.

---

_Fixed: 2026-04-19_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2 (combined: WR fixes from iteration 1 + IN fixes from iteration 2)_
