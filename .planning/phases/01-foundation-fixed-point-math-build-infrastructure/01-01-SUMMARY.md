---
phase: 01-foundation-fixed-point-math-build-infrastructure
plan: 01
subsystem: build-infrastructure
tags: [cmake, c11, q15, fixed-point, unity, ctest, determinism]

requires:
  - phase: 00-bootstrap
    provides: scaffold directories (agents/, prompts/, services/, etc.) left intact per D-04
provides:
  - libspu94.so + libspu94.a built from a single spu94_obj OBJECT library (flag-identical)
  - cmake/spu94_warnings.cmake INTERFACE target as the single source of truth for determinism + warning flags
  - include/spu94/spu94_q15.h header-only Q15 helpers (q15_mul_truncate, sat_s16, q15_add_sat)
  - include/spu94/spu94.h umbrella public header (extern "C", includes spu94_q15.h)
  - _Static_assert compile-time guard that confirms ASR semantics on signed right shift (ADR-0001)
  - tests/unit/vendor/Unity/ Unity v2.6.1 vendored with SHA-256 pins (T-01-01 supply-chain mitigation)
  - Unity-based ctest harness passing 3 test functions against a hand-computed Q15 reference table
  - compile_commands.json emitting -Werror -ffp-contract=off -fno-fast-math on every src/spu94/ TU
  - python/spu94/ reserved Phase 6 scikit-build-core binding location (.gitkeep tracked)
affects: [phase-01-plan-02-ci, phase-02-reverb-state, phase-03-reverb-algorithm, phase-04-fir, phase-06-python-wheel, phase-08-mcu-cross-compile]

tech-stack:
  added:
    - CMake 3.20+ (tested on 3.31.6)
    - C11 (CMAKE_C_STANDARD=11, extensions OFF)
    - Unity v2.6.1 (vendored, not a submodule)
    - CTest (via include(CTest) + enable_testing())
  patterns:
    - "Dual shared + static library from a single OBJECT library (Pattern 1)"
    - "Determinism/warning flags as an INTERFACE target consumed PRIVATE (Pattern 2 + Pitfall 7 avoidance)"
    - "Header-only static inline Q15 helpers (D-05, D-07)"
    - "Hand-computed inline reference table for fixed-point truth (D-10)"
    - "Vendored third-party test framework with SHA-256 pinning (T-01-01 mitigation)"

key-files:
  created:
    - CMakeLists.txt
    - cmake/spu94_warnings.cmake
    - src/spu94/CMakeLists.txt
    - src/spu94/spu94_placeholder.c
    - include/spu94/spu94.h
    - include/spu94/spu94_q15.h
    - tests/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/unit/q15/CMakeLists.txt
    - tests/unit/q15/test_q15.c
    - tests/unit/vendor/Unity/unity.c
    - tests/unit/vendor/Unity/unity.h
    - tests/unit/vendor/Unity/unity_internals.h
    - tests/unit/vendor/Unity/README.md
    - python/spu94/.gitkeep
    - .gitignore
  modified: []

key-decisions:
  - "Single spu94_obj OBJECT library feeds both spu94_shared (SHARED) and spu94_static (STATIC); guarantees flag-identical artifacts without double-compiling."
  - "spu94_warnings is an INTERFACE target linked PRIVATE into spu94_obj — flags never leak to downstream consumers (Pitfall 7)."
  - "Q15 helpers live in include/spu94/spu94_q15.h as static inline functions — no separate .c file, no portability macro wrappers in Phase 1."
  - "q15_mul_truncate uses ASR (>> 15) on signed int32 intermediate; _Static_assert at header-load time catches any compiler that fails to emit ASR."
  - "INT16_MIN * INT16_MIN explicitly saturates to INT16_MAX (the mathematically-correct +2^15 does not fit in int16_t) — enforced by sat_s16 after ASR."
  - "Unity v2.6.1 vendored as three files (unity.c, unity.h, unity_internals.h) with SHA-256 pins — NOT a git submodule."
  - "Unity's own TU is compiled without spu94_warnings (strict warning set would fail on third-party code); core spu94_obj compilation remains strict."
  - "BUILD_TESTING gates add_subdirectory(tests) at top level so a library-only build (e.g. MCU cross-compile) does not require Unity."

patterns-established:
  - "Pattern: OBJECT-library-as-flag-source-of-truth — core flags and includes go on spu94_obj PUBLIC/PRIVATE as appropriate; shared/static consumers just link."
  - "Pattern: vendored-test-framework-with-pins — vendor/{Tool}/{files} + README.md with SHA-256 table; version bump requires simultaneous file + hash update."
  - "Pattern: inline-reference-table — Q15 truth lives in the test TU as a {a, b, expected, why} struct array, audited at planning time and re-audited at implementation time."
  - "Pattern: compile-time ABI guard — _Static_assert in the public header catches compiler-dependent behavior (ASR) before runtime."

requirements-completed: [CORE-01, BUILD-01, BUILD-02, BUILD-07]

duration: ~4min
completed: 2026-04-18
---

# Phase 1 Plan 01: Project Scaffold, CMake Build, Q15 Helpers Summary

**libspu94 now builds as shared+static from a single OBJECT library with determinism flags (-Werror, -ffp-contract=off, -fno-fast-math) locked in and verifiable in compile_commands.json, and header-only Q15 helpers (q15_mul_truncate, sat_s16, q15_add_sat) pass 3 Unity test functions against a hand-audited reference table including the INT16_MIN² saturation case and ASR-vs-C-division distinguishers.**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-04-18T20:23:57Z
- **Completed:** 2026-04-18T20:27:34Z
- **Tasks:** 2
- **Files created:** 16

## Accomplishments

- Full CMake build on Linux produces `build/src/spu94/libspu94.so` and `build/src/spu94/libspu94.a` with zero warnings under `-Werror -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wstrict-prototypes -Wmissing-prototypes -ffp-contract=off -fno-fast-math`.
- `cmake/spu94_warnings.cmake` stands up as the single source of truth for the determinism/warning flag bundle; linked PRIVATE into `spu94_obj` so flags do not leak to downstream consumers (Pitfall 7 avoidance).
- `q15_mul_truncate`, `sat_s16`, `q15_add_sat` implemented as `static inline` in `include/spu94/spu94_q15.h` with a compile-time `_Static_assert` guard that fires if the compiler does not emit arithmetic right shift on signed negatives (ADR-0001 pre-condition).
- Unity v2.6.1 vendored as three files with SHA-256 pins recorded in `tests/unit/vendor/Unity/README.md` (T-01-01 supply-chain mitigation).
- 3 Unity test functions covering 17 Q15-multiply cases, 7 `sat_s16` boundary cases, and 7 `q15_add_sat` saturation cases — all pass under `ctest --test-dir build -R q15 --output-on-failure`.
- `python/spu94/.gitkeep` reserves the Phase 6 scikit-build-core binding location without adding any binding surface yet.

## Task Commits

Each task was committed atomically:

1. **Task 1: Scaffold directories, CMake skeleton, and placeholder sources** — `a6cc745` (feat)
2. **Task 2: Q15 header-only helpers with hand-computed reference tests** — `1fb0442` (feat)

## Files Created/Modified

### Top-level
- `CMakeLists.txt` — C11 project, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, `CMAKE_POSITION_INDEPENDENT_CODE=ON`, `include(CTest)` with `BUILD_TESTING` gate.
- `.gitignore` — build tree, object/library artifacts, IDE/editor caches, Python bytecode.

### cmake/
- `cmake/spu94_warnings.cmake` — INTERFACE target `spu94_warnings` carrying the full flag bundle.

### src/
- `src/spu94/CMakeLists.txt` — `spu94_obj` OBJECT + `spu94_shared` SHARED + `spu94_static` STATIC (both OUTPUT_NAME `spu94`); includes `PUBLIC`, warnings `PRIVATE`.
- `src/spu94/spu94_placeholder.c` — minimal TU; only `int16_t`/`int32_t`/`void` (BUILD-07 compliant).

### include/
- `include/spu94/spu94.h` — umbrella header, `extern "C"` guard, includes `spu94_q15.h`.
- `include/spu94/spu94_q15.h` — Q15 helpers + ASR `_Static_assert` guard.

### tests/
- `tests/CMakeLists.txt` — `add_subdirectory(unit)`.
- `tests/unit/CMakeLists.txt` — builds Unity as a static lib (no spu94_warnings); `add_subdirectory(q15)`.
- `tests/unit/q15/CMakeLists.txt` — `test_q15` executable linking `unity` + `spu94_static`; `add_test(NAME q15_unit ...)`.
- `tests/unit/q15/test_q15.c` — Unity TU with three tests against hand-audited reference table.
- `tests/unit/vendor/Unity/unity.c` — pinned Unity v2.6.1 source.
- `tests/unit/vendor/Unity/unity.h` — pinned Unity v2.6.1 header.
- `tests/unit/vendor/Unity/unity_internals.h` — pinned Unity v2.6.1 internals.
- `tests/unit/vendor/Unity/README.md` — SHA-256 pin table + upgrade discipline.

### python/
- `python/spu94/.gitkeep` — empty marker; Phase 6 entry point.

## Final Q15 Function Signatures (match `<interfaces>` block verbatim)

```c
static inline int16_t q15_mul_truncate(int16_t a, int16_t b);
static inline int16_t sat_s16(int32_t x);
static inline int16_t q15_add_sat(int16_t a, int16_t b);
```

ASR guard:

```c
_Static_assert((((int16_t)-1) >> 1) == -1,
    "SPU-94 assumes arithmetic right shift (ASR) for signed negative shifts. "
    "Target compiler does not satisfy this; see DECISIONS.md ADR-0001.");
```

## Unity v2.6.1 SHA-256 Pins

| File | SHA-256 |
|------|---------|
| unity.c | `b90e735a54cf3b3765ab6caa955d11a1488ee73d9c6152cdc98576c2d17cb871` |
| unity.h | `9db174d3c2c6424fd35c0980c5941d124c5ebb0f48e8172f997a2aa9554b64ea` |
| unity_internals.h | `fcd8b3f6b412ac0ab599547eb8a30b6d7f3f0af77aab31f7a1822a2a8fc9a2b2` |

## Reference Table — Audit Trail

Every row in `mul_cases[]` in `tests/unit/q15/test_q15.c` was independently recomputed at implementation time (not just trusted from the planning-time table). Spot-check of the critical cases:

| a | b | (int32) a*b | `>>15` (ASR) | expected (after `sat_s16`) | matches table |
|---|---|---|---|---|---|
| `INT16_MAX` (32767) | `INT16_MAX` | 1073676289 | 32766 | 32766 | yes |
| `INT16_MIN` (-32768) | `INT16_MAX` | -1073709056 | -32767 | -32767 | yes |
| `INT16_MIN` | `INT16_MIN` | 1073741824 (=2^30) | 32768 | INT16_MAX (saturated from +2^15) | yes |
| -1 | 1 | -1 | -1 (ASR; C division would give 0) | -1 | yes |
| -32 | 1024 | -32768 | -1 (ASR; boundary) | -1 | yes |
| -100 | 100 | -10000 | -1 (ASR; negative non-exact) | -1 | yes |
| 16384 | 16384 | 268435456 | 8192 | 8192 | yes |
| -16384 | 16384 | -268435456 | -8192 | -8192 | yes |

**No corrections required** — every planning-time value survived implementation-time re-audit.

## `_Static_assert` Outcome on Build Host

- **gcc 15.2.0 (Ubuntu 15.2.0-4ubuntu4), x86_64:** `_Static_assert` passes (compiler emits ASR on signed negative right shift, as expected on two's-complement hardware).
- **clang:** Not installed on build host; will be verified in Plan 02 CI which adds a clang job.

No other compilers tested at this time; ARM cross-compile is Phase 8.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added `-DBUILD_TESTING=OFF` during Task 1's isolated configure check**

- **Found during:** Task 1 verify
- **Issue:** Top-level `CMakeLists.txt` calls `add_subdirectory(tests)` when `BUILD_TESTING` is ON (its default), but Task 1 does not yet create `tests/CMakeLists.txt` — Task 2 does. So `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (exactly the verify command in Task 1) would fail with "add_subdirectory given source 'tests' which is not an existing directory" between Tasks 1 and 2.
- **Fix:** During Task 1's isolated configure verification, ran `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=OFF`. Task 2's configure re-enables tests (the default). No source-file change was needed; the gate was already written correctly in the top-level CMakeLists.txt. The plan-level final verification (after Task 2) runs with tests ON and succeeds.
- **Files modified:** None (behavior change is only in how the Task 1 verify step is invoked, which is a one-time bootstrap detail; the committed code is the plan-specified code.)
- **Commit:** N/A (not a code change)

**2. [Rule 2 - Critical functionality] Added `#include <stdio.h>` and `#include <stddef.h>` to `tests/unit/q15/test_q15.c`**

- **Found during:** Task 2
- **Issue:** The planning-time test source uses `snprintf` (requires `<stdio.h>`) and `size_t` (requires `<stddef.h>`); the planning-time includes list was `#include "unity.h"`, `<spu94/spu94_q15.h>`, `<stdint.h>`, `<limits.h>`. Under `-Werror -Wall` this would either warn-as-error (implicit declaration of `snprintf`) or build by accident on a compiler that happens to forward-declare via Unity's header. Safer to include explicitly.
- **Fix:** Added `#include <stdio.h>` and `#include <stddef.h>` to the test TU's includes.
- **Files modified:** `tests/unit/q15/test_q15.c`
- **Commit:** `1fb0442` (Task 2 commit — the fix is embedded in the initial write, not a follow-up).

### Auth gates

None.

### Out-of-scope items deferred

None. Every issue encountered was within-task scope.

## Verification Evidence

### Plan-level verification (all 8 checks)

```
=== 1. Build artifacts ===
libspu94.so: OK
libspu94.a: OK
=== 2. Determinism flags ===
-ffp-contract=off: OK
-fno-fast-math: OK
-Werror: OK
=== 3. Q15 helpers callable ===
q15_mul_truncate: OK
sat_s16: OK
q15_add_sat: OK
=== 4. Tests ===
100% tests passed, 0 tests failed out of 1
=== 5. Saturation case ===
INT16_MIN^2: OK
=== 6. Unity pinned ===
Unity pinned: OK
=== 7. Scaffold untouched ===
scaffold dirs: OK
=== 8. Python location ===
python/spu94/.gitkeep: OK
```

### Unity test breakdown

```
/.../tests/unit/q15/test_q15.c:94:test_q15_mul_truncate_table:PASS
/.../tests/unit/q15/test_q15.c:95:test_sat_s16_boundaries:PASS
/.../tests/unit/q15/test_q15.c:96:test_q15_add_sat_table:PASS
3 Tests 0 Failures 0 Ignored
OK
```

### Compile command for spu94_placeholder.c (from compile_commands.json)

```
/usr/bin/cc
  -I".../include"
  -std=c11
  -fPIC
  -Werror -Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion
  -Wstrict-prototypes -Wmissing-prototypes
  -ffp-contract=off -fno-fast-math
  -o ...spu94_placeholder.c.o
  -c .../spu94_placeholder.c
```

All three determinism tokens (`-Werror`, `-ffp-contract=off`, `-fno-fast-math`) land on the core TU as required by BUILD-02.

## File Tree (as committed)

```
CMakeLists.txt
.gitignore
cmake/
  spu94_warnings.cmake
include/
  spu94/
    spu94.h
    spu94_q15.h
src/
  spu94/
    CMakeLists.txt
    spu94_placeholder.c
tests/
  CMakeLists.txt
  unit/
    CMakeLists.txt
    q15/
      CMakeLists.txt
      test_q15.c
    vendor/
      Unity/
        README.md
        unity.c
        unity.h
        unity_internals.h
python/
  spu94/
    .gitkeep
```

## Known Stubs

- `src/spu94/spu94_placeholder.c` exposes only `const int32_t spu94_internal_version = 0;` — intentional; Phase 2 replaces this TU with real reverb state machinery (CORE-03, CORE-04). Placeholder is required so the library has something to compile until then.
- `include/spu94/spu94.h` is minimal (just `extern "C"` + `#include <spu94/spu94_q15.h>`). Phase 2+ extends this with `spu94_state`, `spu94_init`/`reset`/`destroy`, register read/write, and `spu94_process`.

Both stubs are documented in the plan's `<interfaces>` block as intentional Phase 1 scope.

## Threat Flags

None. The plan enumerated threats T-01-01 (Unity supply chain) and T-01-03 (determinism flag drift) and T-01-04 (forbidden-token reintroduction); all three are mitigated as specified:

- **T-01-01:** Unity vendored at v2.6.1, SHA-256 pinned in `tests/unit/vendor/Unity/README.md`.
- **T-01-03:** Flags set via `target_compile_options(... INTERFACE ...)` on `spu94_warnings`, linked PRIVATE into `spu94_obj`. Never set on `CMAKE_C_FLAGS`. Plan 02 will add the CI grep check on `compile_commands.json`.
- **T-01-04:** `src/spu94/spu94_placeholder.c` uses only `int16_t`/`int32_t`/`void`/`const`. No `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, or unqualified `long`. `include/spu94/spu94_q15.h` uses only `int16_t`, `int32_t`, and `<limits.h>` constants.

No new threat surface introduced beyond what the plan's `<threat_model>` already enumerated.

## Self-Check: PASSED

- [x] `CMakeLists.txt` exists (verified)
- [x] `cmake/spu94_warnings.cmake` exists (verified)
- [x] `src/spu94/CMakeLists.txt` exists (verified)
- [x] `src/spu94/spu94_placeholder.c` exists (verified)
- [x] `include/spu94/spu94.h` exists (verified)
- [x] `include/spu94/spu94_q15.h` exists with all three function signatures (verified via grep)
- [x] `tests/unit/q15/test_q15.c` exists with 3 test functions (verified via run)
- [x] `tests/unit/vendor/Unity/{unity.c,unity.h,unity_internals.h,README.md}` exist (verified)
- [x] `python/spu94/.gitkeep` exists (verified)
- [x] `.gitignore` exists (verified)
- [x] Commit `a6cc745` exists in `git log` (verified)
- [x] Commit `1fb0442` exists in `git log` (verified)
- [x] `build/src/spu94/libspu94.so` + `libspu94.a` produced by `cmake --build build` (verified)
- [x] `ctest --test-dir build -R q15 --output-on-failure` exits 0 (verified, 3/3 tests pass)
- [x] `compile_commands.json` contains `-ffp-contract=off`, `-fno-fast-math`, `-Werror` on the `spu94_placeholder.c` command (verified)
- [x] Scaffold directories (`agents/`, `prompts/`, `evaluation/`, `observability/`, `services/`, `security/`) untouched (verified)
