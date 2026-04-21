---
phase: 05-public-api-presets-integration
plan: 04
subsystem: rt-safety

tags: [rt-safety, linker-symbols, strace, ctypes, benchmark, ctest, D-09, API-08, regression-gate]

requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: scripts/ci/verify-no-heap-symbols.sh (Phase 1 precedent -- nm -u pattern for the rt_no_heap extension)
  - phase: 05-02
    provides: spu94_process + spu94_flush public T-symbols on libspu94.so (targets of the four new RT-safety gates)
  - phase: 05-03
    provides: spu94_load_preset public T-symbol (third Phase 5 public symbol audited by rt_no_heap + rt_no_locks via the linksym binary)

provides:
  - tests/rt_safety/ subdirectory with four permanent ctest regression gates labeled "rt_safety"
  - rt_no_heap ctest target (D-09a): libspu94.so + test_phase5_linksym are heap-free on nm -u audit
  - rt_no_locks ctest target (D-09b): libspu94.so + test_phase5_linksym are pthread/sem/futex-free on nm -u audit
  - rt_no_syscalls ctest target (D-09c): zero non-scaffolding syscalls in 10^5-iter spu94_process loop under strace
  - rt_bench_latency ctest target (D-09d): (p99-median)/median ratio bounded under RT_LATENCY_THRESHOLD (default 3.0)
  - test_phase5_linksym binary: static-linked Phase 5 public-symbol harness for nm-audit link-closure coverage
  - Measured first-pass ratio on Linux dev workstation (Ryzen-class): ratio = 0.741 (well under 3.0 budget); median = 536 us, p99 = 934 us for 1024-sample blocks with Hall preset loaded

affects: [05-05, phase-5-adr-landings, phase-8-mcu-smoke, permanent-ci-gates]

tech-stack:
  added:
    - POSIX sigaction (via _POSIX_C_SOURCE=199309L feature-test macro, scoped to test_no_syscalls.c only)
    - strace 6.16 dependency for rt_no_syscalls (conditional; skips gracefully if absent)
    - Python 3.10+ ctypes for rt_bench_latency (conditional; skips if absent)
  patterns:
    - "Per-axis RT-safety ctest gate pattern (D-09e): one target per property so failures point at the specific broken axis"
    - "Static-linked linksym binary pattern: extends Phase 1's nm audit to cover reachable-from-public-symbols helpers not on libspu94.so's dynamic undefined-symbol list"
    - "Signal-bracketed strace harness pattern: raise(SIGUSR1) around the steady-state region; shell parser locates '--- SIGUSR1 ---' markers and filters scaffolding syscalls (rt_sigreturn + raise() implementation)"
    - "ctypes latency benchmark pattern: 1000-call warmup + 10^5-call measurement window + statistics-library median/p99 extraction + CLI-overridable threshold"
    - "EXISTS()-guarded CMakeLists.txt pattern: Task 1 scaffolds Task 2's gates behind file-existence checks so intermediate commits configure cleanly on their own"

key-files:
  created:
    - tests/rt_safety/CMakeLists.txt
    - tests/rt_safety/test_phase5_linksym.c
    - tests/rt_safety/test_no_heap.sh
    - tests/rt_safety/verify-no-locks.sh
    - tests/rt_safety/test_no_syscalls.c
    - tests/rt_safety/test_no_syscalls.sh
    - tests/rt_safety/bench_latency.py
  modified:
    - tests/CMakeLists.txt

key-decisions:
  - "SIGUSR1 handler installed (sigaction no-op) before the first marker -- raise() without a handler terminates the process per POSIX default; plan as written would have never reached the steady state. Rule 1 plan-level fix."
  - "scaffolding-syscall filter expanded to {rt_sigreturn, gettid, getpid, tgkill} -- glibc's raise() implementation for the END marker inherently lands inside the [START+1, END-1] window. Filter remains tight: any DSP-introduced per-call syscall (futex, read, clock_gettime, etc.) would still surface. Rule 1 plan-level fix."
  - "CMakeLists.txt scaffolds Task 2's axes (rt_no_syscalls, rt_bench_latency) behind EXISTS() guards so Task 1 configures and tests cleanly on its own; Task 2 drops the .c/.sh/.py files and cmake re-activation wires them in. Rule 3 auto-fix to make per-task commits stand alone."
  - "_POSIX_C_SOURCE=199309L required on test_no_syscalls.c for sigaction (project is C11 strict, no compiler extensions). Scoped to that single TU via #define before any include."
  - "Measured ratio 0.741 on dev host -- Plan 05 ADR can either keep 3.0 first-pass target (generous headroom, low false-positive risk) or pin to max(2.0, 2*0.741)=2.0 for tighter regression detection. Planner discretion."

patterns-established:
  - "RT-safety per-axis gate: one ctest target per invariant (no-heap, no-locks, no-syscalls, bounded-latency); failure messages encode which property broke, not which test-bucket failed"
  - "Link-closure audit via static-linked harness: a minimal main() references every public symbol of interest, static-links against spu94_static, and is audited by nm -u; any reachable-from-public-symbol helper requesting heap or lock primitives surfaces here even if invisible on the .so"
  - "Strace-isolated steady-state measurement: signal bracketing (raise(SIGUSR1) before + after the hot region) + grep -n '--- SIGUSR1' + sed windowing lets the shell parser ignore init/teardown noise without running strace in exclusion mode"

requirements-completed:
  - API-08

duration: ~17 min
completed: 2026-04-21
---

# Phase 5 Plan 04: RT-Safety Regression Gates Summary

**Four permanent ctest regression gates under `tests/rt_safety/` (label `rt_safety`) prove API-08 at the contract level: `libspu94.so` + the Phase 5 static-link closure reference no heap, no locks, no syscalls, and exhibit bounded (ratio=0.741, budget=3.0) per-block latency variance across 10^5 consecutive `spu94_process` blocks.**

## Performance

- **Duration:** ~17 min
- **Started:** 2026-04-21T03:41:15Z
- **Completed:** 2026-04-21T03:58:11Z
- **Tasks:** 2 (`type="auto"`, both green first-pass after Rule 1 fixes)
- **Files created:** 7 (CMakeLists.txt + 2 C harnesses + 3 shell/python scripts + 1 test binary wiring)
- **Files modified:** 1 (`tests/CMakeLists.txt` — `add_subdirectory(rt_safety)`)

## Accomplishments

- Four permanent RT-safety regression ctest targets land under label `rt_safety`, each a distinct per-axis failure mode (D-09e preserved). Any future change that introduces a malloc-linkage, pthread-linkage, steady-state syscall, or cache-dependent branch surfaces at the specific axis that broke.
- `test_phase5_linksym` binary statically references all three Phase 5 public symbols (`spu94_process`, `spu94_flush`, `spu94_load_preset`) so `nm -u` auditing sees every Phase 5 code path + transitive helpers, even helpers that wouldn't appear on `libspu94.so`'s dynamic undefined-symbol list.
- `rt_no_heap` (D-09a): Phase-5-aware extension of the existing Phase-1 `scripts/ci/verify-no-heap-symbols.sh` — same regex family, now audits both the shared lib AND the static-link closure. Also widens the forbidden list beyond Phase 1's `{malloc,calloc,realloc,free}` to include `aligned_alloc` and `posix_memalign`.
- `rt_no_locks` (D-09b): asserts `pthread_mutex_*`/`rwlock_*`/`cond_*`/`spin_*`/`barrier_*`/`sem_*`/`futex` are unreferenced.
- `rt_no_syscalls` (D-09c): signal-bracketed strace harness. 10^5 iterations of `spu94_process(state, L, R, Lout, Rout, 1024)` between two `raise(SIGUSR1)` markers; shell wrapper windows the log between `--- SIGUSR1 ---` lines; asserts zero non-scaffolding syscalls. Scaffolding filter excludes `rt_sigreturn` + the three marker-implementation syscalls (`gettid`, `getpid`, `tgkill`) that are part of glibc's `raise()` expansion.
- `rt_bench_latency` (D-09d): ctypes + `perf_counter_ns` benchmark. 1000-call warmup + 10^5-call measurement. Prints `median`/`mean`/`p99`/`max` + the `ratio=(p99-median)/median` value for Plan 05 ADR calibration. Threshold overridable via `-DRT_LATENCY_THRESHOLD=` cache variable (default 3.0).
- Graceful skips: hosts without `strace` skip `rt_no_syscalls`; hosts without Python 3.10+ skip `rt_bench_latency`. The two linker-symbol axes (`rt_no_heap`, `rt_no_locks`) run everywhere `nm` is available.
- Full test suite: **49/49 green** (45 baseline + 4 new rt_safety). Build warnings-free under `-Werror`. Phase 1 `scripts/ci/grep-guard.sh` + `scripts/ci/verify-no-heap-symbols.sh` both still clean.

## Measured Latency Benchmark (Plan 05 ADR Calibration Input)

Dev workstation (Linux, Ryzen-class desktop), Hall preset loaded, 1024-sample blocks, 10^5-call measurement window after 1000-call warmup:

| Metric | Value |
|---|---|
| median | 536,389 ns (~0.54 ms/block) |
| mean | 560,008 ns |
| p99 | 933,797 ns |
| max | 1,350,529 ns |
| **ratio = (p99 − median) / median** | **0.741** |
| threshold (first-pass default) | 3.0 |

The measured ratio 0.741 sits comfortably under the 3.0 first-pass target. Plan 05's ADR landing should either:
- **Keep 3.0** — generous headroom; minimal CI false-positive risk; accepts future host noise.
- **Pin to 2.0** (≈ `max(2.0, 2 × 0.741)` per the measure-then-pin pattern) — tighter regression detection; still 2.7× the observed variance; minor false-positive risk if CI host load spikes.

Planner decision gate. Both are defensible.

## Task Commits

1. **Task 1: RT-safety linker-symbol gates (no-heap + no-locks, D-09a/b)** — `5c6b5ab` (test)
2. **Task 2: RT-safety no-syscalls strace harness + latency benchmark (D-09c/d)** — `56c5549` (test)

## Files Created/Modified

- **`tests/rt_safety/CMakeLists.txt`** — new. Wires four `add_test()` entries under label `rt_safety`; conditional blocks for `rt_no_syscalls` (needs `strace` + Task 2 files) and `rt_bench_latency` (needs Python 3.10+ + Task 2 files). `RT_LATENCY_THRESHOLD` CMake cache variable exposes the D-09d threshold for host-specific calibration.
- **`tests/rt_safety/test_phase5_linksym.c`** — new. Static-linked harness referencing `spu94_process` + `spu94_flush` + `spu94_load_preset` so `nm -u` sees every Phase 5 code path in the binary's link closure.
- **`tests/rt_safety/test_no_heap.sh`** — new. D-09a: `nm -u` + `grep -qE HEAP_PATTERN` on both `SPU94_LIB` and `PHASE5_BIN`; extends the Phase 1 precedent with the Phase 5 static-link binary + adds `aligned_alloc`/`posix_memalign` to the forbidden list.
- **`tests/rt_safety/verify-no-locks.sh`** — new. D-09b: same `nm -u` + grep pattern, lock-symbol family.
- **`tests/rt_safety/test_no_syscalls.c`** — new. Signal-bracketed 10^5-iter `spu94_process` harness. Installs a no-op `SIGUSR1` handler before the first marker (plan-level Rule 1 fix). `_POSIX_C_SOURCE=199309L` for `sigaction` on the project's strict C11 build.
- **`tests/rt_safety/test_no_syscalls.sh`** — new. Runs `strace -f -ttt -o`, windows the log between the two `--- SIGUSR1 ---` markers, filters scaffolding syscalls (`rt_sigreturn` + `gettid`/`getpid`/`tgkill` from the END `raise()`), asserts zero remaining.
- **`tests/rt_safety/bench_latency.py`** — new. ctypes driver; binds `spu94_state_size` / `spu94_init` / `spu94_load_preset` / `spu94_process` / `spu94_destroy`; 1000-call warmup + 10^5-call measurement with `perf_counter_ns`; prints ratio + threshold; exit 1 on threshold exceed.
- **`tests/CMakeLists.txt`** — modified. Added `add_subdirectory(rt_safety)` after `add_subdirectory(python)`.

## Decisions Made

- **Per-axis ctest target granularity preserved (D-09e).** Four distinct `add_test(NAME rt_no_* ...)` entries instead of one monolithic audit. A failure now points directly at the broken property: heap-linkage regression shows up as `rt_no_heap` red (not as "some rt_safety test failed, check the log").
- **Widened heap-symbol list beyond Phase 1.** D-09a forbids `malloc`/`calloc`/`realloc`/`free` + `aligned_alloc`/`posix_memalign` (Phase 1's `verify-no-heap-symbols.sh` only checks the first four). Minor widening for completeness; no practical effect on current code since neither list hits anything.
- **Linksym binary is static-linked against `spu94_static`.** Critical: dynamic linking against `libspu94.so` would only expose the .so's already-audited symbol table. Static linking forces the linker to pull every reachable-from-public-symbol helper into the binary; `nm -u` on that binary sees the full Phase 5 code-path closure.
- **Scaffolding-syscall filter: `rt_sigreturn|gettid|getpid|tgkill`.** The END `raise(SIGUSR1)` expands via glibc into three syscalls that inherently fall inside the `[START+1, END-1]` window. Filter is tight enough that any legitimate DSP-path syscall (futex, read, write, clock_gettime, anything else) would still surface.
- **CMakeLists.txt gated behind `EXISTS()` for Task 2 axes.** Task 1's commit needed to configure + build cleanly on its own; Task 2's .c/.sh/.py files arrive later. The `EXISTS()` guards let cmake emit `STATUS: Task 2 source files not yet present; rt_no_syscalls test deferred` at Task 1 boundary, then auto-activate at Task 2 re-configure.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 — Bug] Install SIGUSR1 handler in `test_no_syscalls.c`**
- **Found during:** Task 2 first test run — `ctest -R rt_no_syscalls` reported the process killed by `User defined signal 1`.
- **Issue:** Plan's `<action>` for Task 2 Step A has `raise(SIGUSR1)` with no handler installed. POSIX default action for SIGUSR1 is process termination. With no handler, the first `raise()` ends the process before the 10^5-iter loop runs; strace sees only init-region activity; the two-marker assertion fails.
- **Fix:** Added `static void sigusr1_noop(int sig) { (void)sig; }` + a `sigaction(SIGUSR1, &sa, NULL)` call at the start of `main()` (before the first marker, so its `rt_sigaction` syscall lands in the init-exclude region). Required `#define _POSIX_C_SOURCE 199309L` before any include because the project compiles with `-std=c11 -pedantic` + `CMAKE_C_EXTENSIONS OFF`, so `sigaction` is not visible under bare C11.
- **Files modified:** `tests/rt_safety/test_no_syscalls.c`
- **Verification:** Binary runs to completion under strace; both markers fire; process exits 0.
- **Committed in:** `56c5549` (Task 2 commit).

**2. [Rule 1 — Bug] Expand scaffolding-syscall filter from `{rt_sigreturn}` to `{rt_sigreturn, gettid, getpid, tgkill}`**
- **Found during:** Task 2 second test run (after fix #1), which reported `FAIL: 3 syscalls in steady-state region` with the three lines being `gettid()` + `getpid()` + `tgkill(..., SIGUSR1)` at the END marker.
- **Issue:** Plan's shell-parser filter subtracts only `rt_sigreturn`. glibc's `raise(int)` for the END marker expands into three syscalls (`gettid`, `getpid`, `tgkill`) that execute BEFORE the `--- SIGUSR1 ---` line strace emits on signal delivery. Those three end up inside the steady-state `[START+1, END-1]` window as marker scaffolding, not DSP work. The plan's filter leaves them in the count; the gate fails unconditionally.
- **Fix:** Expanded the scaffolding pattern to `^[0-9]+\s+[0-9.]+\s+(rt_sigreturn|gettid|getpid|tgkill)\(`. None of those four is a legitimate hot-path DSP syscall — any DSP-introduced `futex`/`read`/`write`/`clock_gettime`/etc. still surfaces. Updated the failure message to say "non-scaffolding syscalls" for clarity; updated the success message to report the scaffolding count observed.
- **Files modified:** `tests/rt_safety/test_no_syscalls.sh`
- **Verification:** `rt_no_syscalls` green in 54 s.
- **Committed in:** `56c5549` (Task 2 commit).

**3. [Rule 3 — Blocking] `EXISTS()`-guarded CMakeLists.txt for Task-1-alone build**
- **Found during:** Task 1 Step E smoke check — `cmake -S . -B build` failed with `Cannot find source file: test_no_syscalls.c` because the plan's Step A CMakeLists.txt declares all four axes up-front, but Task 1 only creates the heap + locks files.
- **Issue:** Plan's Task 1 acceptance requires `cmake --build build` to succeed at the Task 1 commit boundary. The CMakeLists.txt's `add_executable(test_no_syscalls test_no_syscalls.c)` inside the `if(STRACE_EXE)` block fires (strace IS installed) and then fails to find `test_no_syscalls.c` because Task 2 creates it.
- **Fix:** Added `AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.c" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.sh"` to the `rt_no_syscalls` branch; same pattern for `rt_bench_latency` gated on `bench_latency.py`. Emits `STATUS: Phase 5: Task 2 source files not yet present; rt_no_syscalls test deferred` at Task 1 boundary; auto-activates at Task 2 re-configure.
- **Files modified:** `tests/rt_safety/CMakeLists.txt`
- **Verification:** Task 1 cmake + build + rt_no_heap + rt_no_locks all green with only 2 axes registered. Task 2's re-configure registers all 4.
- **Committed in:** `5c6b5ab` (Task 1 commit).

---

**Total deviations:** 3 auto-fixed (2 Rule 1 plan-level test-design bugs + 1 Rule 3 blocking CMake issue).
**Impact on plan:** All three fixes scope-preserving. The plan's structural intent (four permanent ctest gates, one per property, Linux-conditional skips, measure-then-pin calibration for latency) is fully realized. The two Rule 1 fixes are discoveries that would have been caught on any first-pass run of this plan on any Linux host — they're not workstation-specific; every future executor would hit them. The Rule 3 guard improves commit hygiene (per-task commits are now standalone-buildable) and costs nothing at steady state.

## Issues Encountered

- None beyond the three auto-fixed items above.
- The `rt_no_syscalls` test takes ~54 seconds due to strace overhead on 100,000 `spu94_process` calls. Not a pass/fail issue but worth noting: the full `ctest` suite wall-clock went from ~9 s (Plan 03 end) to ~117 s with rt_safety included. Running `ctest -E rt_safety` restores the fast-path ~13 s for iterative dev; rt_safety is a CI gate, not a per-save validator.

## User Setup Required

None. All four RT-safety gates are host-self-describing:
- `rt_no_heap` + `rt_no_locks` require only `nm` (standard binutils on every dev host).
- `rt_no_syscalls` requires `strace`; skipped if absent (macOS / *BSD / Windows have different tracing tools; Phase 5 does not pursue portability for the syscalls gate in M1 — Linux-only is sufficient for the regression-insurance mission).
- `rt_bench_latency` requires Python 3.10+; skipped if absent.

## Next Phase Readiness

- **Plan 05 (fuzz_process.py + ADR landings):** Plan 04 delivers the `tests/rt_safety/` infrastructure that Plan 05's ADR will cite. Plan 05's ADR needs to make two calibration decisions: (a) keep the 3.0 threshold or pin to 2.0 per measure-then-pin; (b) whether to widen the scaffolding-syscall filter list with any additional glibc artifacts discovered on other CI hosts. The measured 0.741 ratio from this plan's verification run is the headline evidence.
- **Phase 5 SC-4 (ROADMAP):** Plan 04 makes SC-4 TRUE at the gate-exists level. "Benchmark-audit confirms no-heap/no-locks/no-syscalls/no-variable-latency across 10^5 consecutive blocks" — each axis is now a green ctest target on this host. Plan 05's ADR closes the documentation loop; M5 hardware capture (deferred) validates the bound on embedded targets.
- **Phase 6 (Python + CLI):** The ctypes pattern established in `bench_latency.py` (bind `spu94_state_size` / `spu94_init` / `spu94_load_preset` / `spu94_process` / `spu94_destroy` + use addressof() for aligned state buffers) is directly reusable for Phase 6's Python wheel. Phase 6's module init can even reuse the `rt_bench_latency` as a runtime self-check ("does the installed wheel still hit sub-1ms per 1024-sample block?").
- **Phase 8 (MCU cross-compile):** The nm/readelf patterns in `test_no_heap.sh` + `verify-no-locks.sh` translate directly to the Cortex-M7 smoke test (cross-compile -> `arm-none-eabi-nm -u` -> same regex family). Phase 8 inherits the linker-symbol discipline Phase 5 pins.
- **Requirements closed:** API-08 (no heap, no locks, no syscalls, no variable-latency — verified via static analysis + benchmark) is fully satisfied at the gate level on the primary Linux target.

## Threat Flags

None. Plan 04's threat register (T-5-RT-01..05 in the PLAN.md `<threat_model>`) is fully mitigated as specified:

- **T-5-RT-01 (accepted):** `SPU94_LIB`/`PHASE5_BIN` env vars come from CMake generator expressions `$<TARGET_FILE:...>`, not user input. Scripts quote every use. Dev-time only.
- **T-5-RT-02 (mitigated):** `--- SIGUSR1` grep matches strace 4.x+ format. If a future strace changes the marker format, the sh script fails loudly with "expected 2 markers, got N" diagnostic. Verified on strace 6.16.
- **T-5-RT-03 (mitigated):** `bench_latency.py` uses fixed 100,000-iteration loop + stack-size-resident ctypes arrays. No growth. Wall-clock 54 s on dev host.
- **T-5-RT-04 (mitigated):** CMake `$<TARGET_FILE:test_phase5_linksym>` + `$<TARGET_FILE:spu94_shared>` generator expressions force fresh artifact re-evaluation on every test run. `nm -u` is idempotent on identical binaries.
- **T-5-RT-05 (mitigated):** `trap 'rm -f "$LOG"' EXIT` in `test_no_syscalls.sh` removes the strace log on both success and failure paths. No secrets in the log.

No new file-access, network, or privileged surface introduced beyond the threat register's enumeration.

## Self-Check: PASSED

All key-files verified on disk via `[ -f ]`:
- `tests/rt_safety/CMakeLists.txt` FOUND
- `tests/rt_safety/test_phase5_linksym.c` FOUND
- `tests/rt_safety/test_no_heap.sh` FOUND (executable)
- `tests/rt_safety/verify-no-locks.sh` FOUND (executable)
- `tests/rt_safety/test_no_syscalls.c` FOUND
- `tests/rt_safety/test_no_syscalls.sh` FOUND (executable)
- `tests/rt_safety/bench_latency.py` FOUND
- `tests/CMakeLists.txt` FOUND (modified — `grep -q rt_safety` succeeds)

All task commits verified via `git log --oneline`:
- `5c6b5ab` (Task 1: test) FOUND
- `56c5549` (Task 2: test) FOUND

`ctest --test-dir build -L rt_safety -N | grep -c "Test #"` returns **4** (all four axes registered).

Plan-level verification block (PLAN.md `<verification>`):
- `cmake --build build` + warnings grep — CLEAN
- `ctest --test-dir build -L rt_safety --output-on-failure` — 4/4 green (108.03 s)
- `ctest --test-dir build --output-on-failure` — 49/49 green (116.81 s)
- `nm -u build/src/spu94/libspu94.so | grep -cE "^\s+U\s+(malloc|...|futex)"` — **0**
- `nm -u build/tests/rt_safety/test_phase5_linksym | grep -cE "^\s+U\s+(malloc|...|futex)"` — **0**
- `bash scripts/ci/grep-guard.sh` — `grep-guard: OK (scanned 20 files)`
- `bash scripts/ci/verify-no-heap-symbols.sh` — `OK: libspu94.so is heap-free`
- Benchmark output prints observed ratio — **0.741** (printed during every run for Plan 05 ADR calibration)

---
*Phase: 05-public-api-presets-integration*
*Plan: 04*
*Completed: 2026-04-21*
