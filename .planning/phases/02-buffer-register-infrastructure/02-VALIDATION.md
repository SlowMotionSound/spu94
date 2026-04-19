---
phase: 2
slug: buffer-register-infrastructure
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-19
---

# Phase 2 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
> Derived from `02-RESEARCH.md § Validation Architecture` and the 5 PLAN.md task breakdowns.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework (C unit tests)** | Unity (vendored in Phase 1 at `tests/vendor/unity/`) |
| **Framework (linker-symbol checks)** | POSIX shell + `nm`/`readelf` (`scripts/ci/verify-no-heap-symbols.sh`) |
| **Framework (consumer-compile checks)** | CMake targets `test_c99_consumer` + `test_cxx_consumer` |
| **Framework (fuzz harness)** | Python 3.10+ stdlib (ctypes + random) — no Hypothesis dependency |
| **Config file** | `tests/CMakeLists.txt` → `tests/unit/CMakeLists.txt` → per-module `CMakeLists.txt` |
| **Quick run command** | `cmake --build build --target test_q15 test_state_lifecycle test_register_roundtrip test_register_types test_register_policy test_register_edges test_buffer_wrap test_buffer_mbase test_q15_with_err && ctest --test-dir build --output-on-failure -R 'q15\|buffer\|registers\|state'` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure && python3 tests/python/fuzz_buffer.py && bash scripts/ci/verify-no-heap-symbols.sh` |
| **Estimated runtime (quick)** | ~15 seconds |
| **Estimated runtime (full)** | ~90 seconds (dominated by the 10⁶-step Python fuzz) |

---

## Sampling Rate

- **After every task commit:** Run quick command for the module(s) touched by the task (e.g., `ctest -R registers` after a register-surface task).
- **After every plan wave:** Run full quick command above (all C unit tests + state lifecycle).
- **Before `/gsd-verify-work`:** Full suite must be green — includes Python fuzz + heap-free linker check + Phase 1 grep-guard/verify-flags/UBSan jobs (inherited).
- **Max feedback latency:** 15s for quick; 90s for full. No 3 consecutive tasks without at least one automated verify.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 02-01-01 | 01 | 1 | API-01, API-02, API-07, API-09 | T-02-01 | `spu94_init` rejects NULL/undersized/misaligned state_buf | unit (C) | `ctest --test-dir build -R state_lifecycle` | ❌ W0 (Plan 01 Task 3) | ⬜ pending |
| 02-01-02 | 01 | 1 | API-01, API-09 | T-02-02 | No `malloc`/`free`/`calloc`/`realloc` symbols in `libspu94.so` | linker (shell) | `bash scripts/ci/verify-no-heap-symbols.sh` | ❌ W0 (Plan 01 Task 2) | ⬜ pending |
| 02-01-03 | 01 | 1 | API-07 | T-02-03 | `spu94.h` compiles under `-std=c99 -pedantic` and `extern "C"` C++ consumer | compile-only (CMake) | `cmake --build build --target test_c99_consumer test_cxx_consumer` | ❌ W0 (Plan 01 Task 3) | ⬜ pending |
| 02-02-01 | 02 | 2 | CORE-04, API-04 | T-02-06, T-02-09, T-02-10 | `spu94_reg_hw_offset`/`spu94_reg_name` bounded, `SPU94_REG__COUNT==35` static-asserted | unit (C) | `ctest --test-dir build -R register_identity` | ❌ W0 (Plan 02 Task 1 — verified in Plan 05 Task 1) | ⬜ pending |
| 02-02-02 | 02 | 2 | — (extensibility tap for Phase 3) | T-02-07 | `q15_mul_truncate_with_err` NULL-guards `err_out`; `q15_mul_truncate` remains bit-identical via thin wrapper | unit (C) | `ctest --test-dir build -R test_q15_with_err` | ❌ W0 (Plan 05 Task 4) | ⬜ pending |
| 02-02-03 | 02 | 2 | DOCS-01 (Phase 1 req; Phase 2 contributes ADR-0004) | — | ADR-0004 paraphrases psx-spx with URL citation; no transcription | doc check | `grep -q 'ADR-0004' docs/DECISIONS.md && grep -q 'psx-spx.consoledev.net' docs/DECISIONS.md` | ❌ W0 (Plan 02 Task 3) | ⬜ pending |
| 02-03-01 | 03 | 3 | CORE-04, API-04 | T-02-06, T-02-09 | Engine-layer typed setters/getters reject type-mismatched access without mutating state | unit (C) | `ctest --test-dir build -R register_roundtrip\|register_types` | ❌ W0 (Plan 05 Task 1) | ⬜ pending |
| 02-03-02 | 03 | 3 | CORE-10 | T-02-09, T-02-10 | Split policy: `v*` immediate, `d*`/`m*` tick-latched; pending shadow readable; tick flush clears mask | unit (C) | `ctest --test-dir build -R register_policy` | ❌ W0 (Plan 05 Task 1) | ⬜ pending |
| 02-03-03 | 03 | 3 | CORE-04, API-04 | T-02-10 | 35 facade wrappers compile `static inline` with zero symbols in `libspu94.so` | compile + nm check | `cmake --build build && nm --defined-only build/libspu94.so \| grep -cE 'spu94_(set\|get)_v[A-Z]\|spu94_(set\|get)_d[A-Z]\|spu94_(set\|get)_m[A-Z]'` returns 0 | ❌ W0 (Plan 03 Task 3) | ⬜ pending |
| 02-03-04 | 03 | 3 | DOCS-01 (Phase 2 contributes ADR-0005) | — | ADR-0005 lists all 35 registers' timing assignments; paraphrases psx-spx | doc check | `grep -q 'ADR-0005' docs/DECISIONS.md && grep -cE 'vIIR\|dCOMB\|dAPF\|dLSAME\|dLDIFF\|vLIN\|vRIN\|mBASE' docs/DECISIONS.md` ≥ 35 | ❌ W0 (Plan 03 Task 4) | ⬜ pending |
| 02-04-01 | 04 | 4 | CORE-03, CORE-10 | T-02-27 | `BufferAddress = MAX(mBASE, (addr+2) AND 0x7FFFE)`; `mBASE` write snaps `BufferAddress := mBASE` immediately | unit (C) + property (Python) | `ctest --test-dir build -R buffer_wrap\|buffer_mbase && python3 tests/python/fuzz_buffer.py` | ❌ W0 (Plan 05 Tasks 2, 3) | ⬜ pending |
| 02-04-02 | 04 | 4 | DOCS-01 (Phase 2 contributes ADR-0006) | — | ADR-0006 paraphrases psx-spx mBASE-write sentence with URL citation; documents D-11 seam | doc check | `grep -q 'ADR-0006' docs/DECISIONS.md && grep -q 'snap-on-write' docs/DECISIONS.md && grep -q 'psx-spx' docs/DECISIONS.md` | ❌ W0 (Plan 04 Task 2) | ⬜ pending |
| 02-05-01 | 05 | 5 | CORE-04, API-04, CORE-10, TEST-02 | T-02-10 | All 35 registers round-trip; signed/unsigned preserved; policy latching verified | unit (C) | `ctest --test-dir build -R 'register_(roundtrip\|types\|policy\|edges)'` | ❌ W0 (self) | ⬜ pending |
| 02-05-02 | 05 | 5 | CORE-03 | T-02-27 | Wrap formula edges + mBASE snap + work-buf sentinel invariants | unit (C) | `ctest --test-dir build -R 'buffer_(wrap\|mbase)'` | ❌ W0 (self) | ⬜ pending |
| 02-05-03 | 05 | 5 | CORE-03 | T-02-24, T-02-28 | 10⁶ random steps preserve wrap invariant; odd-mBASE exception allowed for 1 step | integration (Python) | `python3 tests/python/fuzz_buffer.py` (exit 0) | ❌ W0 (self) | ⬜ pending |
| 02-05-04 | 05 | 5 | — (ADR-0004 coverage) | T-02-07 | `q15_mul_truncate_with_err` remainder matches hand-computed table incl. `INT16_MIN²` edge | unit (C) | `ctest --test-dir build -R test_q15_with_err` | ❌ W0 (self) | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Requirement → Task Coverage

| Req | Covered by tasks | Verification command(s) |
|-----|------------------|-------------------------|
| CORE-03 | 02-04-01, 02-05-02, 02-05-03 | `ctest -R buffer_wrap\|buffer_mbase` + `python3 tests/python/fuzz_buffer.py` |
| CORE-04 | 02-02-01, 02-03-01, 02-03-03, 02-05-01 | `ctest -R register_(roundtrip\|types\|identity\|edges)` |
| CORE-10 | 02-03-02, 02-04-01, 02-05-01 | `ctest -R register_policy\|buffer_mbase` |
| API-01 | 02-01-01, 02-01-02 | `ctest -R state_lifecycle` + `scripts/ci/verify-no-heap-symbols.sh` |
| API-02 | 02-01-01 | `ctest -R state_lifecycle` |
| API-04 | 02-02-01, 02-03-01, 02-03-03, 02-05-01 | (same as CORE-04) |
| API-07 | 02-01-03, 02-02-01 | `cmake --build build --target test_c99_consumer test_cxx_consumer` |
| API-09 | 02-01-02 | Phase 1's `scripts/ci/grep-guard.sh` + `verify-no-heap-symbols.sh` |
| TEST-02 | 02-05-01 (aggregate of per-register suites) | `ctest -R register` |

All 9 phase requirements have automated verification. Zero manual-only gaps.

---

## Wave 0 Requirements (test scaffolding installed before implementation)

- [ ] `tests/unit/state/CMakeLists.txt` + `tests/unit/state/test_state_lifecycle.c` — Plan 01 Task 3
- [ ] `scripts/ci/verify-no-heap-symbols.sh` + CI wiring — Plan 01 Task 2
- [ ] `tests/api/CMakeLists.txt` + `tests/api/c99_consumer.c` + `tests/api/cxx_consumer.cpp` — Plan 01 Task 3
- [ ] `tests/unit/registers/CMakeLists.txt` + `tests/unit/registers/test_register_roundtrip.c` — Plan 05 Task 1
- [ ] `tests/unit/registers/test_register_types.c` — Plan 05 Task 1
- [ ] `tests/unit/registers/test_register_policy.c` — Plan 05 Task 1
- [ ] `tests/unit/registers/test_register_edges.c` — Plan 05 Task 1
- [ ] `tests/unit/buffer/CMakeLists.txt` + `tests/unit/buffer/test_buffer_wrap.c` — Plan 05 Task 2
- [ ] `tests/unit/buffer/test_buffer_mbase.c` — Plan 05 Task 2
- [ ] `tests/python/fuzz_buffer.py` — Plan 05 Task 3
- [ ] `tests/unit/q15/test_q15_with_err.c` (or extension to existing `test_q15.c`) — Plan 05 Task 4
- [ ] `tests/unit/CMakeLists.txt` — append `add_subdirectory(buffer)`, `add_subdirectory(registers)`, `add_subdirectory(state)`
- [ ] `tests/CMakeLists.txt` — append `add_subdirectory(api)`

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|

*All phase behaviors have automated verification.* The ADR doc-entry checks (ADR-0004 / 0005 / 0006) are grep-verifiable as shown in the per-task map; no human review gating is required for Phase 2 completion.

---

## Validation Sign-Off

- [ ] All tasks have `<acceptance_criteria>` referencing the command(s) in the per-task map
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify — confirmed (every task maps to a `ctest`/`cmake`/`bash` command)
- [ ] Wave 0 covers all ❌ references in the per-task map — tracked above; checked before execution starts
- [ ] No watch-mode flags — confirmed (all commands are single-shot)
- [ ] Feedback latency < 90s full suite (dominated by 10⁶-step fuzz)
- [ ] `nyquist_compliant: true` will be set in frontmatter after Wave 0 scaffolding lands and the plan-checker confirms per-task verify coverage

**Approval:** pending
