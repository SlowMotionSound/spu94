---
phase: 5
slug: public-api-presets-integration
status: ready
nyquist_compliant: true
wave_0_complete: true
created: 2026-04-20
updated: 2026-04-20
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

*Every plan task appears as a row. Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky. `File Exists` column records whether the target test/source file is delivered by that task (all Phase 5 plans deliver Wave 0 infrastructure inline — no separate Wave 0 wave needed).*

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 5-01-01 | 01 | 1 | CORE-09 | T-5-5 / T-5-PRESET-01 | Human-transcribed BIB-011 nocash preset matrix; facts-only provenance preserved | manual + grep | `test $(wc -l < .planning/research/05-preset-values-audit-nocash.csv) -ge 351 && head -1 .planning/research/05-preset-values-audit-nocash.csv \| grep -Fxq "preset_name,reg_idx,reg_name,hex_value"` | ✅ delivered by task | ⬜ pending |
| 5-01-02 | 01 | 1 | CORE-09 | T-5-5 / T-5-PRESET-01 | Independent BIB-012 hitmen c02 transcription for two-way cell-equality audit | manual + grep | `test $(wc -l < .planning/research/05-preset-values-audit-hitmen.csv) -ge 351 && head -1 .planning/research/05-preset-values-audit-hitmen.csv \| grep -Fxq "preset_name,reg_idx,reg_name,hex_value"` | ✅ delivered by task | ⬜ pending |
| 5-01-03 | 01 | 1 | CORE-09 | T-5-PRESET-01 | verify_preset_sources.py is the cell-equality regression gate | audit (Python) | `python3 -m py_compile tests/python/verify_preset_sources.py && python3 tests/python/verify_preset_sources.py; EC=$?; test $EC -ne 2 && grep -q verify_preset_sources tests/python/CMakeLists.txt` | ✅ delivered by task | ⬜ pending |
| 5-01-04 | 01 | 1 | CORE-09 | T-5-PRESET-01 / T-5-PRESET-03 | Disagreement-resolution checkpoint; honest provenance lineage documented | manual | `python3 tests/python/verify_preset_sources.py \|\| test -f .planning/research/05-preset-values-audit-resolutions.md` | ✅ delivered by task (checkpoint artifact) | ⬜ pending |
| 5-01-05 | 01 | 1 | CORE-09 / API-05 | T-5-PRESET-02 | `spu94_presets[]` in .rodata + table-integrity unit test (count, names, Off-zero, enum pins) | unit | `cmake --build build 2>&1 \| tee /tmp/p5p1-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p1-build.log && ctest --test-dir build -R "test_preset_table_integrity\|verify_preset_sources" --output-on-failure && nm build/src/spu94/libspu94.so \| grep -qE " [DR] spu94_presets$" && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-01-06 | 01 | 1 | CORE-09 | T-5-5 | BIBLIOGRAPHY BIB-011/012/013 entries document lineage honestly | audit (grep) | `COUNT=$(grep -cE "^### BIB-01[1-3]:" docs/BIBLIOGRAPHY.md) && test "$COUNT" = "3" && grep -q "^## Preset Sources" docs/BIBLIOGRAPHY.md && grep -q "hitmen.c02.at" docs/BIBLIOGRAPHY.md && grep -q "LIBSND" docs/BIBLIOGRAPHY.md` | ✅ delivered by task | ⬜ pending |
| 5-02-01 | 02 | 2 | API-03 / API-06 | T-5-MIXBUS-01 | Mailbox fields + reverb.c mailbox read + `spu94_process` / `spu94_flush` prototypes | linksym + unit | `cmake --build build 2>&1 \| tee /tmp/p5p2a-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p2a-build.log && nm build/src/spu94/libspu94.so \| grep -qE " T spu94_process$" && nm build/src/spu94/libspu94.so \| grep -qE " T spu94_flush$" && grep -q "state->mix_bus_l" src/spu94/spu94_reverb.c && ctest --test-dir build --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-02-02 | 02 | 2 | API-03 / API-06 | T-5-MIXBUS-01 | test_process_basic + test_process_flush + test_process_mix_bus; no TEST_IGNORE stubs | unit | `cmake --build build 2>&1 \| tee /tmp/p5p2b-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p2b-build.log && ctest --test-dir build -R "test_process_basic\|test_process_flush\|test_process_mix_bus" --output-on-failure && ! grep -r TEST_IGNORE tests/unit/process/ && test $(grep -cE "^\s*RUN_TEST\(" tests/unit/process/test_process_basic.c) -ge 5 && test $(grep -cE "^\s*RUN_TEST\(" tests/unit/process/test_process_flush.c) -ge 3 && test $(grep -cE "^\s*RUN_TEST\(" tests/unit/process/test_process_mix_bus.c) -ge 3 && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-03-01 | 03 | 3 | API-05 / CORE-09 | T-5-PRESET-04 | `spu94_load_preset` prototype + engine-layer iteration; split-policy respected | linksym + unit | `cmake --build build 2>&1 \| tee /tmp/p5p3a-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p3a-build.log && nm build/src/spu94/libspu94.so \| grep -qE " T spu94_load_preset$" && ctest --test-dir build -R "test_preset_table_integrity\|verify_preset_sources" --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-03-02 | 03 | 3 | API-05 / CORE-09 | T-5-PRESET-04 | test_preset_load_all proves D-08 split-policy across 10 presets × 35 regs (active + pending + post-tick) | unit | `cmake --build build 2>&1 \| tee /tmp/p5p3b-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p3b-build.log && ctest --test-dir build -R test_preset_load_all --output-on-failure && test $(grep -cE "^\s*RUN_TEST\(" tests/unit/preset/test_preset_load_all.c) -ge 6 && bash scripts/ci/grep-guard.sh` | ✅ delivered by task | ⬜ pending |
| 5-03-03 | 03 | 3 | CORE-09 | T-5-PRESET-04 | test_preset_nonzero_tail proves SC-2 (9 non-Off presets non-zero; Off silent) | unit (behavioral) | `cmake --build build 2>&1 \| tee /tmp/p5p3c-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p3c-build.log && ctest --test-dir build -R test_preset_nonzero_tail --output-on-failure && ctest --test-dir build --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-04-01 | 04 | 3 | API-08 | T-5-RT-04 | rt_no_heap + rt_no_locks linksym gates + Phase 5 linksym binary | linksym (shell) | `cmake --build build 2>&1 \| tee /tmp/p5p4a-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p4a-build.log && ctest --test-dir build -R "rt_no_heap\|rt_no_locks" --output-on-failure && test -x tests/rt_safety/test_no_heap.sh && test -x tests/rt_safety/verify-no-locks.sh && ctest --test-dir build --output-on-failure` | ✅ delivered by task | ⬜ pending |
| 5-04-02 | 04 | 3 | API-08 | T-5-RT-02 / T-5-RT-03 / T-5-RT-05 | rt_no_syscalls (strace signal-bracketed) + rt_bench_latency (ctypes p99/median) | strace + bench | `cmake --build build 2>&1 \| tee /tmp/p5p4b-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p4b-build.log && python3 -m py_compile tests/rt_safety/bench_latency.py && test -x tests/rt_safety/test_no_syscalls.sh && ctest --test-dir build -L rt_safety --output-on-failure && ctest --test-dir build --output-on-failure` | ✅ delivered by task | ⬜ pending |
| 5-05-01 | 05 | 4 | API-06 / CORE-09 | T-5-FUZZ-01 / T-5-FUZZ-02 / T-5-FUZZ-03 / T-5-FUZZ-04 | fuzz_process.py 10^6-step harness with FIR-index + pending_mask bound invariants; struct-offset guard mirrors fuzz_fir.py WR-02 | fuzz (Python) | `python3 -m py_compile tests/python/fuzz_process.py && SPU94_LIB=build/src/spu94/libspu94.so python3 tests/python/fuzz_process.py --steps 10000 && test $(grep -c "fir_idx" tests/python/fuzz_process.py) -ge 8 && test $(grep -c "pending_mask" tests/python/fuzz_process.py) -ge 3 && grep -q "FIR_DELAY_LEN" tests/python/fuzz_process.py && grep -q "PENDING_MASK_WIDTH" tests/python/fuzz_process.py && grep -q "spu94_state_size" tests/python/fuzz_process.py && ctest --test-dir build -R fuzz_process --output-on-failure && ctest --test-dir build --output-on-failure` | ✅ delivered by task | ⬜ pending |
| 5-05-02 | 05 | 4 | API-03 | — | test_process_block_size ({1..4096} sweep bit-identical) + test_process_in_place (alias-safe bit-identical) | unit | `cmake --build build 2>&1 \| tee /tmp/p5p5a-build.log && ! grep -E "(error\|-Werror\|warning:)" /tmp/p5p5a-build.log && ctest --test-dir build -R "test_process_block_size\|test_process_in_place" --output-on-failure && ctest --test-dir build --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-no-heap-symbols.sh` | ✅ delivered by task | ⬜ pending |
| 5-05-03 | 05 | 4 | API-06 / API-08 / CORE-09 | T-5-ADR-01 / T-5-ADR-02 | ADR-Phase-5-A..F prepended; BIB-011/012/013 cross-refs; latency threshold substituted | audit (grep) | `test $(grep -cE "^## ADR-Phase-5-" docs/DECISIONS.md) -ge 6 && grep -q "BIB-011" docs/DECISIONS.md && grep -q "BIB-012" docs/DECISIONS.md && grep -q "spu94_fir_chain_step" docs/DECISIONS.md && grep -q "mix_bus_l" docs/DECISIONS.md && grep -q "1000000\|100000\|10\^" docs/DECISIONS.md && ! grep -qE "<observed_ratio>\|<calibrated_value>" docs/DECISIONS.md` | ✅ delivered by task | ⬜ pending |

*Sixteen rows cover every Phase 5 task across Plans 01-05. Wave column reflects the plan's wave in the phase DAG. Threat Ref column points at the STRIDE row(s) in the plan's `<threat_model>` block that the task's automated verification addresses.*

---

## Wave 0 Requirements

**All Wave 0 infrastructure is delivered inline by plan tasks — no separate Wave 0 phase needed.** Each plan task creates its own test files and wires them into ctest; there are no `<automated>MISSING — Wave 0 must create ...</automated>` placeholders in any of Plans 01-05. The list below records which task delivers each legacy Wave 0 artifact for traceability.

- `tests/unit/process/test_process_basic.c` — delivered by Task 5-02-02 (Unity TU for `spu94_process` / `spu94_flush`; no IGNORE stubs)
- `tests/unit/preset/test_preset_load_all.c` — delivered by Task 5-03-02 (Unity TU; 6 sub-tests covering D-08 split policy)
- `tests/unit/preset/test_preset_table_integrity.c` — delivered by Task 5-01-05 (Unity TU; count, names, Off-zero, enum pins)
- `tests/unit/preset/test_preset_nonzero_tail.c` — delivered by Task 5-03-03 (SC-2 behavioral proof)
- `tests/unit/mix_bus` TUs — delivered by Task 5-02-02 (test_process_mix_bus.c mailbox behavior)
- `tests/python/fuzz_process.py` — delivered by Task 5-05-01 (10⁶-step random-walk with FIR-index + pending_mask bound invariants)
- `tests/python/verify_preset_sources.py` — delivered by Task 5-01-03 (cell-equality audit gate)
- `tests/rt_safety/verify-no-locks.sh` — delivered by Task 5-04-01
- `tests/rt_safety/test_no_syscalls.sh` + C harness — delivered by Task 5-04-02
- `tests/rt_safety/bench_latency.py` — delivered by Task 5-04-02

*All covered by Unity + pytest + ctest already in the build. No framework install needed.*

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| 35×10 preset matrix provenance audit (three-source byte-for-byte cross-reference per D-07) | CORE-09 | Human judgment required when sources disagree; each disagreement resolved via documented rationale in ADR + `docs/BIBLIOGRAPHY.md` | Tasks 5-01-01 + 5-01-02 + 5-01-04 (human transcription + disagreement-resolution checkpoint); research doc § "The 35×10 Preset Register Matrix" P-R1..P-R5 audit protocol; commit + ADR required before `spu94_presets.c` ships |
| RT-safety bench latency threshold calibration | API-08 | Threshold must be measured on actual CI host; 3× is first-pass only — D-09d | Task 5-04-02 runs `bench_latency.py` on CI runner; Task 5-05-03 substitutes the observed p99/median ratio into ADR-Phase-5-E and pins the THRESHOLD = max(2.0, 2 × observed_ratio) |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies (16/16 tasks have inline `<automated>` blocks; zero MISSING tags; zero deferred Wave 0 stubs)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify
- [x] Wave 0 covers all MISSING references (no MISSING references in any plan; Wave 0 infra delivered inline per task)
- [x] No watch-mode flags
- [x] Feedback latency < 90s (quick command filters to phase5 tests; fuzz + bench excluded from per-commit sampling via LABELS)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** ready — every task's automated verify is directly executable against the live build; manual-only rows are isolated to the two checkpoints and the threshold-calibration substitution.
