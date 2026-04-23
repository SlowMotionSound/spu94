---
phase: 07-verification-golden-files-witness-diff-modulation
plan: 05
subsystem: rt-safety-gates
tags: [rt-safety, alloc-gate, pytest-benchmark, ci, hard-gate, report-only]
requires:
  - phase-5-public-api (spu94_process)
  - phase-6-python-binding (SPU94 class + Preset enum)
  - phase-7-plan-01 (strace + pytest-benchmark installed)
provides:
  - hotpath-alloc-gate (hard CI fail on brk/mmap/mmap2/munmap/mremap in spu94_process)
  - hotpath-alloc-gate-negative-meta (WILL_FAIL inversion proves gate detects real malloc)
  - pytest-benchmark-harness (10 presets x 2 block sizes, report-only)
  - benchmark-baselines-committed (26 KB stripped JSON; human-refreshed)
  - benchmark-report-ci-job (continue-on-error, artifact upload)
affects:
  - future-commits-must-keep-spu94_process-heap-free
  - future-plans-may-diff-against-benchmark-baselines-json-manually
tech-stack:
  added:
    - actions/upload-artifact@ea165f8...  (v4.6.2 SHA-pinned)
  patterns:
    - volatile-sink-defeats-dead-code-elimination
    - mmap-forcing-allocation-above-M_MMAP_THRESHOLD
    - strace-narrow-filter-paired-with-broader-rt_no_syscalls
    - pytest-benchmark-stripped-data-for-small-committed-baseline
key-files:
  created:
    - tests/rt_safety/hotpath_alloc_gate.sh
    - tests/rt_safety/hotpath_alloc_gate_target.c
    - tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c
    - tests/python/test_hotpath_alloc_gate_meta.py
    - tests/benchmarks/test_benchmark.py
    - tests/benchmarks/benchmark_baselines.json
    - tests/benchmarks/CMakeLists.txt
    - tests/benchmarks/README.md
  modified:
    - tests/rt_safety/CMakeLists.txt
    - tests/CMakeLists.txt
    - .github/workflows/ci.yml
decisions:
  - "Negative meta-target uses malloc(1 MiB) + volatile sink, not the plan's malloc(64). Rule-1 bug fix: glibc satisfies small repeated mallocs from the freelist, producing zero mmap/brk syscalls, and the compiler elides the whole malloc/free pair at -O2 when the pointer is dead. A bulletproof negative test forces the allocator onto the mmap path (M_MMAP_THRESHOLD is 128 KiB by default) AND keeps the call from being optimized away via a static volatile pointer sink."
  - "Benchmark baseline committed with stats.data[] stripped to empty arrays. Full JSON is 3.6 MB (per-round raw samples); stripped is 26 KB. Summary stats preserved (min/mean/median/stddev/rounds/iterations + IQR + outliers). Refresh procedure in tests/benchmarks/README.md."
  - "Strace filter is `brk,mmap,mmap2,munmap,mremap` -- mmap2 kept belt-and-suspenders per plan Pitfall 5 even though M1 targets are all 64-bit. No-op on current hosts; harmless guard for any future 32-bit CI runner."
  - "Benchmark harness sets vLOUT/vROUT = 0x7FFF before timing. Matches the ADR-Phase-6-G self_test unlock dance -- we time the real reverb-network cost, not a silenced wet path."
  - "Benchmark CI job uses continue-on-error:true (D-20). ubuntu-latest runner jitter would produce noisy false positives if gated. Humans diff bench-ci.json artifact against tests/benchmarks/benchmark_baselines.json on demand."
  - "SPU94_LIB env plumbing matches tests/python/ convention ($<TARGET_FILE:spu94_shared> in ctest; github.workspace path in CI). No pip install -e . required; the binding loads the just-built shared library via the env var."
metrics:
  duration_minutes: ~13
  tasks_completed: 3
  files_created: 8
  files_modified: 3
  ctest_targets_added: 3 (hotpath_alloc_gate #59, hotpath_alloc_gate_negative #60, bench_process #73)
  ci_jobs_added: 2 (hotpath-alloc-gate hard, benchmark-report report-only)
  benchmark_cases: 20 (10 presets x 2 block sizes)
completed: 2026-04-23
---

# Phase 07 Plan 05: Hot-Path Alloc Gate + Pytest-Benchmark Summary

**Shipped BUILD-06 D-20 in its gate-split form: hotpath_alloc_gate (merge-blocking, hard CI fail on any heap syscall in spu94_process) + pytest-benchmark harness (report-only, 10 presets x 2 block sizes, committed baseline per D-21) + paired CI jobs.**

Phase 5 already proved zero syscalls across 10^5 blocks. Phase 7 extends that proof to a narrower, independently-diagnosable gate (heap syscalls only) PLUS a negative meta-test that actually fires the filter on real malloc hits -- so the gate isn't just a silent pass. The benchmark track sits alongside, reporting numbers without gating.

## What Landed

### Task 1 -- hotpath_alloc_gate + negative meta-test

- `tests/rt_safety/hotpath_alloc_gate.sh` (90 lines): strace wrapper with filter `brk,mmap,mmap2,munmap,mremap`; parses SIGUSR1 markers exactly the same way as Phase 5's `test_no_syscalls.sh`; exits 1 on any heap syscall inside the `[START+1, END-1]` steady-state window.
- `tests/rt_safety/hotpath_alloc_gate_target.c` (77 lines): clean target -- SIGUSR1 handler + `spu94_init` + HALL preset + 10^5 `spu94_process` calls + SIGUSR1 + `spu94_destroy`. Mirrors `test_no_syscalls.c`.
- `tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c` (84 lines): negative meta-target -- same shape, but calls `malloc(1 MiB)` + volatile-sink assignment + `spu94_process` + `free` inside the hot window. 1 MiB beats `M_MMAP_THRESHOLD` so mmap always fires.
- `tests/python/test_hotpath_alloc_gate_meta.py` (66 lines): two pytest cases. clean-target-passes + with_malloc-target-fails (gate stderr names at least one of brk/mmap/munmap/mremap).
- `tests/rt_safety/CMakeLists.txt`: builds both targets against `spu94_static`; registers ctest entries `hotpath_alloc_gate` (passes clean) and `hotpath_alloc_gate_negative` (WILL_FAIL TRUE inverts the expected ctest verdict).

### Task 2 -- pytest-benchmark harness + committed baseline

- `tests/benchmarks/test_benchmark.py` (118 lines): 10 presets (via `Preset.__members__` iteration so the list stays synced with the binding) x 2 block sizes (1024, 4096) x `min_rounds=5`, `warmup=True`, `disable_gc=True`. Uses the `SPU94` class + `.set_reg()` + `.process()` -- follows the fuzz_*.py convention of `sys.path` prepend + `SPU94_LIB` env var.
- `tests/benchmarks/benchmark_baselines.json` (26 KB): committed initial baseline per D-21. Raw `stats.data[]` arrays were stripped post-generation (save 99% of size); all summary stats (min/mean/median/stddev/rounds/iterations + IQR + outliers) preserved.
- `tests/benchmarks/README.md`: D-20/D-21 policy statement + manual refresh recipe.
- `tests/benchmarks/CMakeLists.txt`: ctest entry `bench_process` (LABELS "benchmark;report_only", TIMEOUT 600). `SPU94_LIB=$<TARGET_FILE:spu94_shared>` Pitfall-7 mitigation.
- `tests/CMakeLists.txt`: `add_subdirectory(benchmarks)`.

### Task 3 -- CI wiring

- `.github/workflows/ci.yml` gets two new jobs:
  - `hotpath-alloc-gate` -- hard gate, runs all three tests (positive ctest + negative ctest + pytest meta-wrapper).
  - `benchmark-report` -- report-only, `continue-on-error: true`, uploads `bench-ci.json` as build artifact via SHA-pinned `actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02` (v4.6.2, looked up at execute time per Phase 1 discipline).

## Initial Benchmark Baselines (dev workstation, 2026-04-23)

Median `spu94_process` wall time, microseconds per block. 10 presets x 2 block sizes = 20 cases.

| Preset       | 1024-sample block | 4096-sample block |
|--------------|-------------------|-------------------|
| OFF          |         157.1 us  |         606.1 us  |
| ROOM         |         156.2 us  |         606.7 us  |
| STUDIO_A     |         159.6 us  |         608.4 us  |
| STUDIO_B     |         158.9 us  |         604.9 us  |
| STUDIO_C     |         157.2 us  |         595.1 us  |
| HALL         |         156.4 us  |         604.6 us  |
| HALF_ECHO    |         163.8 us  |         625.9 us  |
| SPACE_ECHO   |         158.3 us  |         613.5 us  |
| ECHO         |         160.8 us  |         612.7 us  |
| DELAY        |         159.4 us  |         605.9 us  |

### Observations (informational, not gates)

- **Preset-to-preset variance is tiny.** 4096-block medians cluster within ~5% of each other (595-626 us). Suggests the cost of `spu94_process` is dominated by the fixed work per sample (39-tap FIR on every boundary + tick loop), not by preset-specific reverb-network complexity. Even the "reverb-off" preset (Off) runs within noise of Hall.
- **~4x scaling from 1024 to 4096 is clean.** 4096/1024 ratios sit around 3.8-4.0, as expected for a function that does constant work per input sample. No hidden O(N^2) surprise.
- **HALF_ECHO is the slowest preset at both block sizes.** Modest outlier (~4% above the median). Not investigated; could be APF-loop geometry, could be measurement noise. Not a gate, so not chased.
- **These are dev-workstation numbers, not production.** Anthony's host runs ubuntu-studio; CI runs ubuntu-latest on GitHub-hosted x86_64 runners. Direct host-to-CI comparison is not meaningful -- both are historical tracks to be compared against themselves.

## Strace Filter Choice

Filter: `-e trace=brk,mmap,mmap2,munmap,mremap`.

Rationale:
- `brk` -- main glibc small-alloc path.
- `mmap` -- large-alloc path above `M_MMAP_THRESHOLD`; also thread-stack allocation.
- `mmap2` -- belt-and-suspenders for 32-bit Linux hosts (no-op on 64-bit; harmless; kept in case a future 32-bit CI runner joins the matrix per Pitfall 5).
- `munmap` -- catches a `free()` whose glibc arena-free path returns memory to the kernel.
- `mremap` -- catches in-place resize operations on mmap'd regions.

Phase 5's `rt_no_syscalls` gate catches ALL syscalls in the steady state and is the broader net; Phase 7's gate is the narrower diagnostic complement. A regression that adds a heap syscall fires Phase 7 first (with a targeted "heap in hot path" error); a regression that adds any other syscall fires Phase 5.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's `malloc(64)` negative target produced a silent-pass gate.**

- **Found during:** Task 1 verification. The plan's example code called `malloc(64)` then `free(p)` per iteration. First test run showed `PASS: zero heap syscalls` on the "poisoned" target, which would mean the gate is broken.
- **Root cause:** TWO compounding bugs.
  1. glibc's arena allocator satisfies small repeated `malloc(64)` calls from the per-arena freelist. Only the first iteration triggers `brk`; subsequent iterations reuse freed slots without any syscall. After an `mmap`-free warm-up, `malloc(64)` syscalls land in the init region, not the hot window.
  2. The compiler's `-O2` dead-code eliminator removed the whole `p = malloc(64); free(p);` pair because `p` was never observed. Verified via `nm`: no undefined `malloc` symbol in the linked binary before the fix.
- **Fix:** Raised alloc size to 1 MiB (above glibc's 128 KiB `M_MMAP_THRESHOLD`) so the allocator always takes the mmap path; added `static void * volatile g_sink;` + `g_sink = p;` to force the compiler to keep the call.
- **Files modified:** `tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c`.
- **Commit:** `3e713b7` (folded into Task 1's landing commit, not a separate fix commit).

**2. [Rule 3 - Blocking] Benchmark harness used `set_reg_i16`, but SPU94 class only exposes `set_reg` (auto-polymorphic).**

- **Found during:** Task 2 first pytest run -- AttributeError on every benchmark case at fixture setup time.
- **Issue:** The plan's example sketched a raw-panel style call (`s.set_reg_i16(...)`). The SPU94 class in `python/spu94/reverb.py` has `set_reg()` (auto-detects i16 vs u16 via `_reg_type`) not `set_reg_i16()`; the i16-suffixed functions only exist on the `api.py` raw-panel layer.
- **Fix:** Single-line change to `set_reg("vLOUT", 0x7FFF)` / `set_reg("vROUT", 0x7FFF)`.
- **Files modified:** `tests/benchmarks/test_benchmark.py`.
- **Commit:** `957d4a5` (folded into Task 2's landing commit).

**3. [Rule 2 - Critical] Committed baseline JSON was 3.6 MB due to per-round raw samples.**

- **Found during:** Task 2 initial baseline generation.
- **Issue:** pytest-benchmark's JSON output keeps the full `stats.data[]` array (one entry per timed round -- 6000+ entries per benchmark here). 20 benchmarks x ~6000 float samples = 3.6 MB. Too big to sensibly commit to git as a baseline.
- **Fix:** Post-process the JSON after generation -- strip `stats.data[]` to an empty list. Summary stats (min/mean/median/stddev/rounds/iterations + IQR + ld15iqr/hd15iqr) are the whole point of the committed baseline; raw samples only matter during the measurement run itself. Documented the strip step in `tests/benchmarks/README.md` for future refresh runs.
- **Files modified:** `tests/benchmarks/benchmark_baselines.json` (shrank from 3.6 MB to 26 KB), `tests/benchmarks/README.md` (refresh recipe includes the strip step).
- **Commit:** `957d4a5` (folded into Task 2's landing commit).

**4. [Rule 3 - Blocking] Plan's CI job acceptance criteria used tight `grep -A5 / -A20` windows that comment blocks broke.**

- **Found during:** Task 3 acceptance-check step.
- **Issue:** The plan's acceptance criteria `grep -B0 -A5 "^  benchmark-report:"` requires `continue-on-error: true` to land within the first 5 lines after the job header. My initial version put comments ABOVE `continue-on-error`, pushing it to line 9. Similarly `grep -A20` for `upload-artifact`.
- **Fix:** Reordered job body so `name:`/`runs-on:`/`continue-on-error:` appear first (before any comments). Compressed install+build steps into a single shell block to keep `upload-artifact` within the `-A20` window. No functional change -- same jobs, same pins, same steps.
- **Files modified:** `.github/workflows/ci.yml`.
- **Commit:** `39bcf62` (folded into Task 3's landing commit).

### No Rule 4 Escalations

All deviations were compiler/library gotchas or formatting-window issues. No architectural changes needed.

## Authentication Gates

None. Plan 01 installed strace + pytest-benchmark; Plan 02 had the sudo/docker gates; Plan 05 inherits the warmed-up environment and ran fully autonomously.

## Commits

| # | Hash      | Type | Message                                                                   |
|---|-----------|------|---------------------------------------------------------------------------|
| 1 | `3e713b7` | feat | feat(07-05): hot-path allocation gate + negative meta-test (BUILD-06 D-20)|
| 2 | `957d4a5` | feat | feat(07-05): pytest-benchmark timing harness + committed baseline         |
| 3 | `39bcf62` | ci   | ci(07-05): hotpath-alloc-gate (hard) + benchmark-report (report-only)     |

## Known Stubs

None. All CI jobs reference real scripts/targets; the negative meta-test binary is intentional scaffolding (never shipped; used only for gate meta-verification).

## Threat Flags

None. The new CI jobs inherit the existing SHA-pinned-checkout posture; no new external input surface. The hotpath_alloc_gate shell wrapper takes `SYSCALLS_BIN` from env (supplied by ctest generator expressions or pytest subprocess.run list-form) -- same accept-posture as Phase 5's test_no_syscalls.sh (threat T-07-05-A in the plan's register, accepted).

## Deferred Issues

None. All plan requirements satisfied:

- [x] `hotpath_alloc_gate.sh` exists; `grep -q "trace=brk,mmap,mmap2,munmap,mremap"` passes.
- [x] Both C targets built under `build/tests/rt_safety/`.
- [x] `ctest --test-dir build -R "^hotpath_alloc_gate$"` passes.
- [x] `ctest --test-dir build -R "^hotpath_alloc_gate_negative$"` passes (WILL_FAIL flip).
- [x] `pytest tests/python/test_hotpath_alloc_gate_meta.py -q` passes 2 tests.
- [x] `pytest tests/benchmarks/test_benchmark.py --benchmark-json=...` exits 0 with 20 benchmark groups.
- [x] `tests/benchmarks/benchmark_baselines.json` committed, valid JSON, contains `benchmarks` field.
- [x] `.gitignore` excludes `.benchmarks/` (already present from Phase 7 foundation).
- [x] Two new CI jobs in ci.yml -- hard gate + report-only artifact.

## Self-Check

- [x] `tests/rt_safety/hotpath_alloc_gate.sh` exists -- FOUND.
- [x] `tests/rt_safety/hotpath_alloc_gate_target.c` exists -- FOUND.
- [x] `tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c` exists -- FOUND.
- [x] `tests/python/test_hotpath_alloc_gate_meta.py` exists -- FOUND.
- [x] `tests/benchmarks/test_benchmark.py` exists -- FOUND.
- [x] `tests/benchmarks/benchmark_baselines.json` exists -- FOUND.
- [x] `tests/benchmarks/CMakeLists.txt` exists -- FOUND.
- [x] `tests/benchmarks/README.md` exists -- FOUND.
- [x] Commit `3e713b7` in git log -- FOUND.
- [x] Commit `957d4a5` in git log -- FOUND.
- [x] Commit `39bcf62` in git log -- FOUND.

## Self-Check: PASSED
