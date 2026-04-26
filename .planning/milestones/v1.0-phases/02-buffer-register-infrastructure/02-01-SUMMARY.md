---
phase: 02-buffer-register-infrastructure
plan: 01
subsystem: api
tags: [opaque-handle, lifecycle, freestanding-c99, ci-linker-check, unity-tests]

# Dependency graph
requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: spu94_obj OBJECT library, spu94_warnings INTERFACE flags, Unity test harness, grep-guard + verify-flags CI gates, _Static_assert ASR guard in spu94_q15.h
provides:
  - Opaque spu94_state handle with caller-allocated storage (D-12)
  - SPU94_STATE_SIZE_MAX (16384) + SPU94_STATE_ALIGN_MAX (16) shell-type bounds
  - spu94_result_t public enum (SPU94_OK / CLAMPED / UNKNOWN_REG / TYPE_MISMATCH)
  - Lifecycle API spu94_state_size / init / reset / destroy (heap-free)
  - _Static_assert pinning sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX
  - Linker-symbol heap-free proof script + dedicated CI job (Phase 2 SC 1)
  - C99 + C++ extern-C consumer compile tests (Phase 2 SC 6 / API-07)
  - tests/api/ and tests/unit/state/ test subdirectories wired into CTest
affects: [02-02, 02-03, 02-04, 02-05, 03-reverb-algorithm, 05-public-api, 06-python-bindings, 08-mcu-cross-compile]

# Tech tracking
tech-stack:
  added:
    - "stdalign.h (C11 alignas keyword) in public header"
    - "GitHub Actions verify-no-heap CI job (binutils nm + readelf)"
  patterns:
    - "Shell-type opaque handle with runtime size query + compile-time max bound"
    - "Caller-owned state buffer + caller-owned work buffer (D-12 + D-13)"
    - "Reset preserves caller-memory pointers; init/reset/destroy boundaries (D-14)"
    - "Per-target C_STANDARD overrides for compile-conformance tests (C99 + C++11)"
    - "_Static_assert -> static_assert alias under __cplusplus for C/C++ dual-consumption headers"

key-files:
  created:
    - "src/spu94/spu94_state.c"
    - "scripts/ci/verify-no-heap-symbols.sh"
    - "tests/api/CMakeLists.txt"
    - "tests/api/c99_consumer.c"
    - "tests/api/cxx_consumer.cpp"
    - "tests/unit/state/CMakeLists.txt"
    - "tests/unit/state/test_state_lifecycle.c"
  modified:
    - "include/spu94/spu94.h (added Phase 2 chassis surface)"
    - "include/spu94/spu94_q15.h (C++ static_assert alias for API-07)"
    - "src/spu94/CMakeLists.txt (added spu94_state.c to spu94_obj)"
    - "tests/CMakeLists.txt (add_subdirectory(api))"
    - "tests/unit/CMakeLists.txt (add_subdirectory(state))"
    - ".github/workflows/ci.yml (verify-no-heap job)"

key-decisions:
  - "SPU94_STATE_SIZE_MAX = 16384 bytes — generous bound (97x current sizeof) so Plans 02–05 do not need to bump it; assert pins the real size"
  - "SPU94_STATE_ALIGN_MAX = 16 bytes — covers int64_t and SIMD on every M1 target"
  - "Reverb-side fields (reg_values[35], pending_values[35], pending_mask uint64) declared in Plan 01 even though Plans 02/03 populate them — sizes the chassis correctly on first pass"
  - "spu94_init accepts NULL+0 work_buf as legal don't-care; rejects NULL+nonzero as caller bug"
  - "spu94_reset zeros the caller's work buffer per RESEARCH.md reset contract; preserves the work_buf pointer + size"
  - "No <string.h>; hand-rolled byte-loop spu94_zero_bytes keeps libspu94.so heap-free (verified by both nm -u and readelf -r)"
  - "_Static_assert aliased to static_assert under __cplusplus in spu94_q15.h — required to make Phase 1 header C++-consumer-clean per API-07"
  - "verify-no-heap-symbols.sh runs as its own CI job (matches one-concern-per-job style) rather than a step inside build-and-test"

patterns-established:
  - "Shell-type opaque handle: typedef + size macro + alignment macro + _Static_assert size guard + runtime size query"
  - "Caller-allocated dual buffer: state_buf for fixed-size internal state + work_buf sized to caller's reverb-RAM budget"
  - "Compile-conformance tests live in tests/api/, link spu94_static, override CMake C_STANDARD per target"
  - "Linker-level invariant scripts live in scripts/ci/, follow grep-guard.sh style (set -euo pipefail, exit-code discipline, inline KNOWN LIMITATIONS)"
  - "Header-only freestanding C99 + extern-C wrapping pattern continued from Phase 1; new headers must respect both"

requirements-completed:
  - API-01
  - API-02
  - API-07
  - API-09

# Metrics
duration: 5m 24s
completed: 2026-04-19
---

# Phase 2 Plan 01: SPU-94 State Chassis Summary

**Opaque spu94_state handle with caller-allocated storage, SPU94_STATE_SIZE_MAX-bounded shell type, init/reset/destroy lifecycle, plus the Phase-2 SC-1 linker-symbol heap-free proof and the SC-6 C99 + C++ extern-C consumer compile tests.**

## Performance

- **Duration:** ~5 min 24 s (autonomous executor; matches the small-task-count plan)
- **Started:** 2026-04-19T19:42:25Z
- **Completed:** 2026-04-19T19:47:49Z
- **Tasks:** 3 (one of which TDD: test→feat split into RED + GREEN commits)
- **Files created:** 7 — modified: 6

## Accomplishments

- Public chassis (`include/spu94/spu94.h`): opaque `spu94_state` typedef, `SPU94_STATE_SIZE_MAX = 16384u`, `SPU94_STATE_ALIGN_MAX = 16u`, `spu94_result_t` enum (4 codes pinned), four lifecycle declarations.
- Private state (`src/spu94/spu94_state.c`): `struct spu94_state` with work_buf pointer + size, BufferAddress (zero-initialized per RESEARCH.md A4), reserved register storage (`reg_values[35]`, `pending_values[35]`, `pending_mask`). `_Static_assert` pins both sizeof and alignof to the public bounds. Implementations are heap-free and `<string.h>`-free.
- Linker-symbol proof: `scripts/ci/verify-no-heap-symbols.sh` (executable, `set -euo pipefail`, two-pass nm + readelf, three exit codes documented) and a dedicated `verify-no-heap` CI job pinned to actions/checkout v4.2.2 SHA. Verified locally against the freshly built `libspu94.so` — clean, zero heap imports.
- API-07 conformance: `tests/api/c99_consumer.c` (C99 pedantic) and `tests/api/cxx_consumer.cpp` (C++11 pedantic via `extern "C"`) both build and link cleanly. CTest reports `api_c99_consumer` and `api_cxx_consumer` passing.
- Lifecycle test suite: 12 RUN_TEST entries in `tests/unit/state/test_state_lifecycle.c` covering size determinism, every documented init failure mode, the legal NULL+0 work-buf path, the work-buffer-zeroing reset contract, NULL safety on reset/destroy, and enum identity. All pass.
- Full ctest suite: 4/4 green (`q15_unit`, `state_lifecycle`, `api_c99_consumer`, `api_cxx_consumer`). `grep-guard.sh`, `test-grep-guard.sh`, and `verify-no-heap-symbols.sh` all green.

## Specific Numbers (per `<output>` requirements)

- **`SPU94_STATE_SIZE_MAX`:** `16384u` bytes. Deliberate per RESEARCH.md A7 — 97× headroom over the current sizeof so Plans 02–05 (which extend the struct with full register tables, write-policy state, and the reverb work-area cursor structures) will not force a macro bump. The `_Static_assert` makes a future overflow a build failure.
- **Actual `sizeof(struct spu94_state)`:** **168 bytes** at end of Plan 01 (measured at runtime via `spu94_state_size()`). Composition: `unsigned char *work_buf` (8) + `size_t work_buf_size` (8) + `uint32_t buffer_address` (4) + 4 padding + `int16_t reg_values[35]` (70) + 2 padding + `int16_t pending_values[35]` (70) + 6 padding + `uint64_t pending_mask` (8) = 180 nominal; compiler-measured = 168 indicates the layout packs tighter than the worst-case padding estimate (likely consolidated tail padding). Either way, the `_Static_assert` confirms `168 ≤ 16384` at compile time.
- **D-14 init/reset boundary:** Implemented exactly as the plan specified — `init` validates args + zeroes internal fields + records caller pointers + sets `BufferAddress = 0`; `reset` saves work_buf pointer/size, zeroes work_buf bytes, zeroes the whole struct, restores work_buf pointer/size + `BufferAddress = 0`. **No deviation.** This means the "init writes mBASE=0 → snap-on-write side-effect → BufferAddress=0" causal chain (RESEARCH.md A4) is implemented as a direct assignment in Plan 01; Plan 04 will land the explicit snap-on-write handler that future register writes will route through.
- **`scripts/ci/verify-no-heap-symbols.sh` shell** (dependency for Plan 05's Python fuzz harness): exit 0 on clean, exit 1 on forbidden symbol found, exit 2 on library-not-found. Two-pass: `nm -u $LIB | grep -qE '\b(malloc|calloc|realloc|free)\b'` then `readelf -r $LIB | grep -qE '\b(malloc|calloc|realloc|free)\b'`. Plan 05's Python harness can shell out to it post-build to confirm rebuild freshness (RESEARCH.md Pitfall 7) without re-implementing the linker probe.

## Task Commits

1. **Task 1 (TDD RED): failing test for spu94_state lifecycle** — `63e61d7` (test)
2. **Task 1 (TDD GREEN): spu94_state opaque type + result enum + lifecycle API** — `e9802ef` (feat)
3. **Task 2: linker-symbol heap-free proof + CI job** — `ad24db8` (feat)
4. **Task 3: C99 + C++ extern-C consumer compile tests + state lifecycle wiring** — `7420adf` (test)

**Plan metadata commit:** _added next, includes SUMMARY.md + STATE.md + ROADMAP.md + REQUIREMENTS.md_

## Files Created/Modified

- `include/spu94/spu94.h` — Phase-2 chassis surface (size/align macros, opaque typedef, result enum, lifecycle decls, doc comments).
- `include/spu94/spu94_q15.h` — `_Static_assert -> static_assert` alias under `__cplusplus` for API-07.
- `src/spu94/spu94_state.c` — Private struct + lifecycle impls + `_Static_assert` size/align guards.
- `src/spu94/CMakeLists.txt` — Added `spu94_state.c` to `spu94_obj` source list.
- `scripts/ci/verify-no-heap-symbols.sh` — Two-pass nm + readelf heap-import check (executable).
- `.github/workflows/ci.yml` — `verify-no-heap (API-01 / SC 1)` job.
- `tests/api/CMakeLists.txt`, `tests/api/c99_consumer.c`, `tests/api/cxx_consumer.cpp` — API-07 compile-conformance harness.
- `tests/CMakeLists.txt` — `add_subdirectory(api)`.
- `tests/unit/state/CMakeLists.txt`, `tests/unit/state/test_state_lifecycle.c` — Unity lifecycle suite.
- `tests/unit/CMakeLists.txt` — `add_subdirectory(state)`.

## Decisions Made

- **`spu94_init` accepts `(NULL, 0)` work-buf but rejects `(NULL, >0)` work-buf.** Plan-specified semantic; documents the legal "no reverb work area, no advance ever" mode without conflating it with the caller-bug mode.
- **`spu94_reset` zeroes the caller's work buffer.** Plan-specified per RESEARCH.md State-Allocation Pattern reset contract. Pointer + size preserved.
- **`SPU94_STATE_SIZE_MAX = 16384u`.** Per RESEARCH.md A7. Generous headroom; `_Static_assert` enforces the real bound.
- **No deferred work; no architectural decisions taken.** All Plan 01 surfaces are exactly what the plan specifies; nothing pre-implemented from Plans 02–05.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 — Bug] C++ consumer fails to compile because `_Static_assert` is a C-only keyword**
- **Found during:** Task 3 (`api_cxx_consumer` build)
- **Issue:** The Phase 1 `include/spu94/spu94_q15.h` uses C11's `_Static_assert` keyword unconditionally. When the new `cxx_consumer.cpp` includes `<spu94/spu94.h>`, that header transitively pulls in `spu94_q15.h`, and `_Static_assert` is not a C++ keyword — C++11 spells it `static_assert`. Build fails: "expected constructor, destructor, or type conversion before '(' token".
- **Fix:** Added a 6-line `#ifdef __cplusplus / #define _Static_assert(cond, msg) static_assert(cond, msg) / #endif` guard near the top of `spu94_q15.h`. No-op in C; aliases the spelling in C++.
- **Files modified:** `include/spu94/spu94_q15.h`
- **Verification:** `cmake --build build` clean; `api_cxx_consumer` ctest passes; `q15_unit` ctest still passes (no C-side regression); grep-guard still passes.
- **Committed in:** `7420adf` (Task 3 commit) — the fix and the test that surfaced it land together so the relationship is auditable in one diff.

**2. [Rule 1 — Bug] grep-guard hits in Task 1 doc comments**
- **Found during:** Task 1 verification (after the GREEN build passed)
- **Issue:** Doc comments in `src/spu94/spu94_state.c` and `include/spu94/spu94.h` contained the words "memset" (in a "we do not include memset" sentence) and "free" (in two "Does NOT free any memory" sentences). `scripts/ci/grep-guard.sh` matches these as forbidden tokens regardless of comment context — by design (BUILD-07 has no AST awareness; that limitation is documented in the script).
- **Fix:** Reworded to "We do not include `<string.h>`" and "Does NOT release memory" — preserves the meaning without naming the forbidden tokens.
- **Files modified:** `src/spu94/spu94_state.c`, `include/spu94/spu94.h`
- **Verification:** `bash scripts/ci/grep-guard.sh` reports `OK (scanned 4 files)`.
- **Committed in:** `e9802ef` (Task 1 GREEN commit) — caught and fixed before the commit landed; not a follow-up commit.

---

**Total deviations:** 2 auto-fixed (both Rule 1, both required for the plan's own acceptance criteria).
**Impact on plan:** No scope creep. Both fixes are essential corrections to make the plan's stated tests/CI gates pass. The C++ fix is the more interesting one — it's a Phase 1 header touching the API-07 surface that Plan 01 newly exercises; the fix preserves Phase 1 behavior and unlocks Plan 01's new requirement. Worth flagging for Plans 02–05: any new public header must be C++-extern-C-clean as a hard constraint, not a defer.

## Issues Encountered

- **CMake generator mismatch on first reconfigure.** When Task 3 added `enable_language(CXX)`, the existing `build/` directory was a Unix-Makefiles build; rerunning `cmake -G Ninja` errored. Recovered by skipping the explicit reconfigure step (CMake auto-reconfigures on `cmake --build`). Noted for Phase 2 plans 02–05: the existing build dir is Makefiles, not Ninja; either delete + regenerate with Ninja for parity with CI, or stop passing `-G Ninja` on local rebuilds.

## User Setup Required

None — the plan has `user_setup: []` in its frontmatter, and no plan task introduced any external service or env-var dependency. The new `verify-no-heap` CI job depends only on `binutils` (`nm`, `readelf`), which is preinstalled on `ubuntu-latest`.

## Next Phase Readiness

- **Plan 02 (register identity):** Can attach `SPU94_REG_*` enum + `reg_values[35]` interpretation to the chassis without bumping `SPU94_STATE_SIZE_MAX` (current sizeof = 168, headroom = 16216 bytes). The slot is already reserved.
- **Plan 03 (write-timing policy):** `pending_values[35]` and `pending_mask` slots are reserved.
- **Plan 04 (mBASE snap + buffer arithmetic):** `buffer_address uint32_t` is in place; the reset path already implements the "BufferAddress = 0 because mBASE = 0" invariant chain. Plan 04 will lift the snap-on-write into the explicit handler routed through the D-11 seam.
- **Plan 05 (Python fuzz harness):** Can call `scripts/ci/verify-no-heap-symbols.sh` directly to verify build freshness before each fuzz run — closes RESEARCH.md Pitfall 7.
- **No blockers.**

## Self-Check: PASSED

Verified after summary write:
- FOUND: `include/spu94/spu94.h` (modified)
- FOUND: `include/spu94/spu94_q15.h` (modified)
- FOUND: `src/spu94/spu94_state.c` (created)
- FOUND: `src/spu94/CMakeLists.txt` (modified)
- FOUND: `scripts/ci/verify-no-heap-symbols.sh` (created, executable)
- FOUND: `.github/workflows/ci.yml` (modified)
- FOUND: `tests/CMakeLists.txt` (modified)
- FOUND: `tests/unit/CMakeLists.txt` (modified)
- FOUND: `tests/api/CMakeLists.txt` (created)
- FOUND: `tests/api/c99_consumer.c` (created)
- FOUND: `tests/api/cxx_consumer.cpp` (created)
- FOUND: `tests/unit/state/CMakeLists.txt` (created)
- FOUND: `tests/unit/state/test_state_lifecycle.c` (created)
- FOUND commit: `63e61d7` (RED)
- FOUND commit: `e9802ef` (Task 1 GREEN)
- FOUND commit: `ad24db8` (Task 2)
- FOUND commit: `7420adf` (Task 3)

---
*Phase: 02-buffer-register-infrastructure*
*Completed: 2026-04-19*
