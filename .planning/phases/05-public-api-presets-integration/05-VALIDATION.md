---
phase: 5
slug: public-api-presets-integration
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-20
---

# Phase 5 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C unit) + pytest (Python ctypes fuzz / benchmarks) + CMake/ctest (orchestrator) |
| **Config file** | `CMakeLists.txt`, `tests/python/pytest.ini`, `tests/rt_safety/` |
| **Quick run command** | `cmake --build build && ctest --test-dir build -R "phase5" --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure && pytest tests/python/fuzz_process.py -x` |
| **Estimated runtime** | ~90 seconds (quick) / ~10 minutes (full; fuzz_process.py dominates) |

---

## Sampling Rate

- **After every task commit:** Run quick command (targeted test for the task just committed)
- **After every plan wave:** Run full suite command
- **Before `/gsd-verify-work`:** Full suite must be green; RT-safety CI gates green; `verify-no-heap-symbols.sh` + `verify-no-locks.sh` + `test_no_syscalls.sh` + `bench_latency.py` all pass
- **Max feedback latency:** ~90 seconds (quick) — fuzz is waved to end of phase

---

## Per-Task Verification Map

*Filled in by planner at plan-write time. Every task gets a row; `File Exists` column set per Wave 0 output.*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 5-01-01 | 01 | 1 | CORE-09 / API-05 | — | Preset-value audit commit lands 35×10 matrix with provenance | manual + grep | `grep -c "^static const int16_t preset_" src/spu94/spu94_presets.c` == 10 | ❌ W0 | ⬜ pending |
| 5-01-02 | 01 | 1 | API-03 | — | spu94_process prototype + block-loop calls spu94_fir_chain_step | unit | `ctest -R test_process_basic` | ❌ W0 | ⬜ pending |

*Expand at plan-write time. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] `tests/unit/process/test_process_basic.c` — Unity TU stubs for spu94_process / spu94_flush
- [ ] `tests/unit/preset/test_preset_load.c` — Unity TU stubs for spu94_load_preset atomicity + non-zero-tail (10 presets × check)
- [ ] `tests/unit/mix_bus/test_mix_bus.c` — Unity TU stubs for D-05 mailbox field behavior
- [ ] `tests/python/fuzz_process.py` — 10⁶-step random-walk fuzz harness (stub + skeleton with invariant asserts)
- [ ] `tests/rt_safety/verify-no-locks.sh` — linker-symbol pthread-absent assertion (stub)
- [ ] `tests/rt_safety/test_no_syscalls.sh` + C harness — strace signal-bracketed steady-state assertion (stub)
- [ ] `tests/rt_safety/bench_latency.py` — ctypes p99/median latency benchmark (stub)
- [ ] `tests/python/derive_preset_reference.py` — optional helper script if planner chooses to derive non-zero-tail expectations programmatically (stub)

*Covered by Unity + pytest + ctest already in the build. No framework install needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| 35×10 preset matrix provenance audit (three-source byte-for-byte cross-reference per D-07) | CORE-09 | Human judgment required when sources disagree; each disagreement resolved via documented rationale in ADR + `docs/BIBLIOGRAPHY.md` | Research doc § "The 35×10 Preset Register Matrix" P-R1..P-R5 audit protocol; commit + ADR required before `spu94_presets.c` ships |
| RT-safety bench latency threshold calibration | API-08 | Threshold must be measured on actual CI host; 3× is first-pass only — D-09d | Run `bench_latency.py` on CI runner; record p99/median; tune threshold to measured value + 50% headroom; document in ADR |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 90s
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
