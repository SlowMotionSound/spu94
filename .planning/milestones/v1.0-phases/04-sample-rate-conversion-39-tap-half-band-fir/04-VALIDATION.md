---
phase: 04
slug: sample-rate-conversion-39-tap-half-band-fir
status: approved
nyquist_compliant: true
wave_0_complete: true
created: 2026-04-20
---

# Phase 04 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (C unit tests) + pytest-adjacent (Python ctypes fuzz + reference generators) |
| **Config file** | `tests/unit/fir/CMakeLists.txt` + `tests/python/CMakeLists.txt` + root `CMakeLists.txt` |
| **Quick run command** | `cmake --build build && ctest --output-on-failure --test-dir build -L fir -j` |
| **Full suite command** | `cmake --build build && ctest --output-on-failure --test-dir build -j` (includes fuzz_buffer + fuzz_reverb + fuzz_fir) |
| **Estimated runtime** | Unit ~5 s; full suite with 3 fuzz harnesses ~15 s (each fuzz harness ~3–5 s @ 10⁶ steps) |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build && ctest --output-on-failure --test-dir build -L fir -j` (FIR subset, ~5 s)
- **After every plan wave:** Run full `ctest --output-on-failure --test-dir build -j` (includes fuzz harnesses, ~15 s)
- **Before `/gsd-verify-work`:** Full suite must be green; `scripts/ci/grep-guard.sh` + `scripts/ci/verify-no-heap-symbols.sh` must pass; UBSan CI job must pass
- **Max feedback latency:** 15 s (full suite)

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 04-01 T1 — Coef TU + internal header + CMake | 01 | 1 | CORE-06 / CORE-07 | T-COEF (Pitfall 4) | Coefficient transcription integrity (facts-only; no prose) | unit (grep + compile) | `cmake --build build --target spu94_obj && bash scripts/ci/grep-guard.sh` | `src/spu94/spu94_fir_coef.c`, `src/spu94/spu94_fir_internal.h` | ⬜ pending |
| 04-01 T2 — struct state + BIBLIOGRAPHY + Python reference | 01 | 1 | CORE-06 / CORE-07 | T-STATE (struct layout) | state extension + bibliography + Python reference | unit (compile + doc) | `cmake --build build --target spu94_obj && test -s docs/BIBLIOGRAPHY.md && python3 tests/python/derive_fir_reference.py --help` | `src/spu94/spu94_state_internal.h`, `docs/BIBLIOGRAPHY.md`, `tests/python/derive_fir_reference.py` | ⬜ pending |
| 04-01 T3 — test_fir_coef_table (length/symmetry/SHA-256) | 01 | 1 | CORE-06 / CORE-07 | T-COEF (SHA-256 pin) | Coefficient table bit-integrity (Pitfall 4 sidecar) | unit | `ctest --output-on-failure --test-dir build -R "^fir_coef_table$"` | `tests/unit/fir/test_fir_coef_table.c` | ⬜ pending |
| 04-02 T1 — spu94_fir.c (folded-form + literal reference + accumulator proof + err/overflow + #ifdef seam) | 02 | 2 | CORE-06 / CORE-07 | T-ACC (int32 margin); T-CLAMP (D-04 seam) | Folded arithmetic correctness; accumulator no-overflow; cascade-clamp seam presence | unit (bit-exact) | `cmake --build build && ctest --output-on-failure --test-dir build -R "^fir_(decimate|interpolate)$"` | `src/spu94/spu94_fir.c` | ⬜ pending |
| 04-02 T2 — test_fir_bit_identity + test_fir_overflow_proof | 02 | 2 | CORE-06 / CORE-07 | T-ACC (worst-case) | D-01 folded == literal across 10⁵ random + adversarial; SC-3 accumulator hits `0x5CD2632E` without overflow | unit + property | `ctest --output-on-failure --test-dir build -R "^fir_(bit_identity|overflow_proof)$"` | `tests/unit/fir/test_fir_bit_identity.c`, `tests/unit/fir/test_fir_overflow_proof.c` | ⬜ pending |
| 04-02 T3 — test_fir_decimate + test_fir_interpolate + --dump-test-tables | 02 | 2 | CORE-06 / CORE-07 | T-FOLDED (Pattern 1) | Per-stage bit-exactness vs Python reference | unit (bit-exact vs reference table) | `ctest --output-on-failure --test-dir build -R "^fir_(decimate|interpolate)$"` + `python3 tests/python/derive_fir_reference.py --dump-test-tables /tmp/regen.h && diff -q /tmp/regen.h tests/unit/fir/test_fir_test_tables.h` | `tests/unit/fir/test_fir_decimate.c`, `tests/unit/fir/test_fir_interpolate.c` | ⬜ pending |
| 04-03 T1 — spu94_io_chain.c + SPU94_LATENCY_SAMPLES + spu94_get_latency_samples | 03 | 3 | CORE-06 / CORE-07 | T-04-CHAIN-01 (phase drift); T-04-API-01 (latency drift) | Wrapper state-machine integrity; public latency symbol | unit (compile + symbol export) | `cmake --build build && nm build/src/spu94/libspu94.so \| grep -qE " T spu94_get_latency_samples$"` | `src/spu94/spu94_io_chain.c`, `include/spu94/spu94.h` | ⬜ pending |
| 04-03 T2 — test_fir_chain_latency + test_fir_impulse + test_fir_dc | 03 | 3 | CORE-06 / CORE-07 | T-04-LATENCY-01 (latency oracle decoupling) | SC-1 chain-level + SC-2 DC + D-09 latency pin | unit | `ctest --output-on-failure --test-dir build -R "^fir_(chain_latency\|impulse\|dc)$"` | `tests/unit/fir/test_fir_chain_latency.c`, `tests/unit/fir/test_fir_impulse.c`, `tests/unit/fir/test_fir_dc.c` | ⬜ pending |
| 04-03 T3 — test_fir_err_overflow_taps | 03 | 3 | CORE-06 / CORE-07 | T-04-BUF-03; T-04-STATE-02 | D-05 + D-06 invariants (zero on clean, monotonic on stress, reset-clears) | unit | `ctest --output-on-failure --test-dir build -R "^fir_err_overflow_taps$"` | `tests/unit/fir/test_fir_err_overflow_taps.c` | ⬜ pending |
| 04-04 T1 — test_fir_frequency_sweep + test_fir_round_trip_transparency + derive_fir_reference.py --dump-sweep-reference / --dump-band-limited-fixture | 04 | 4 | CORE-06 / CORE-07 | T-04-COEF-02 (generated header tampering) | Frequency-domain spot-check via Python-derived bins; round-trip residual ≤ 80 LSB (−52 dB) | unit (bit-exact vs Python) + property (residual threshold) | `ctest --output-on-failure --test-dir build -R "^fir_(frequency_sweep\|round_trip_transparency)$" && python3 tests/python/derive_fir_reference.py --dump-sweep-reference /tmp/sweep.h && diff -q /tmp/sweep.h tests/unit/fir/test_fir_frequency_sweep_reference.h` | `tests/unit/fir/test_fir_frequency_sweep.c`, `tests/unit/fir/test_fir_round_trip_transparency.c` | ⬜ pending |
| 04-04 T2 — tests/python/fuzz_fir.py | 04 | 4 | CORE-06 / CORE-07 | T-04-FUZZ-01 (bounded runtime) | D-16: 10⁶-step output-range + latency-pin + canary + monotonic err/overflow | fuzz | `ctest --output-on-failure --test-dir build -R "^fuzz_fir$"` | `tests/python/fuzz_fir.py` | ⬜ pending |
| 04-04 T3 — witness-captures/README.md (Mednafen/DuckStation empirical pass OR graceful deferral) | 04 | 4 | CORE-06 / CORE-07 (SC-4 support) | T-04-WITNESS-01 (protocol integrity) | D-14 empirical witness classification; lv2-psx-reverb OUT-OF-AXIS primary-source attested | manual / semi-auto | `test -s .planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/witness-captures/README.md && grep -q "OUT-OF-AXIS" .planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/witness-captures/README.md` | `.planning/phases/04-.../witness-captures/README.md` | ⬜ pending |
| 04-04 T4 — docs/DECISIONS.md ADR-0012..0020 + 04-04-SUMMARY.md | 04 | 4 | CORE-06 / CORE-07 (SC-4 closure) | T-04-ADR-01 (ADR ↔ research evidence) | 9 new ADRs with research-backed decisions; margin disclosure (ADR-0014); aggregate-err interpretation (ADR-0017); coef provenance audit (ADR-0020) | doc (grep) | `grep -cE "^## ADR-00(1[2-9]\|20)" docs/DECISIONS.md \| grep -q "^9$" && grep -q "2.79" docs/DECISIONS.md && grep -q "bannister" docs/DECISIONS.md && test -s .planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-04-SUMMARY.md` | `docs/DECISIONS.md`, `.planning/phases/04-.../04-04-SUMMARY.md` | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

Nyquist compliance note: every task row above has an `<automated>` verify command except `04-04 T3` (witness empirical pass), which is semi-manual — covered by Wave 0 below via the graceful-deferral branch that still produces an automated-verifiable artifact (witness-captures/README.md grep check). Task 3 is listed under Manual-Only Verifications for transparency; the header `nyquist_compliant: true` is set because Task 3's deferral path produces an automated-checkable artifact and its classification is not a Phase 4 completion blocker (lv2-psx-reverb primary-source attestation is sufficient for SC-4 per ADR-0012).

---

## Wave 0 Requirements

- [x] `tests/unit/fir/` directory scaffold — created in Plan 01 Task 3; Plans 02/03/04 append test TUs
- [x] `tests/python/derive_fir_reference.py` — pure-Python integer reference model created in Plan 01 Task 2; extended by Plan 02 (`--dump-test-tables`) and Plan 04 Task 1 (`--dump-sweep-reference`, `--dump-band-limited-fixture`)
- [x] `tests/python/fuzz_fir.py` — 10⁶-step ctypes fuzz harness landed in Plan 04 Task 2 (follows `fuzz_buffer.py` / `fuzz_reverb.py` template)
- [x] `docs/BIBLIOGRAPHY.md` — created in Plan 01 Task 2 with BIB-005/006/007 (coefficient sources) + BIB-008/009/010 (witness emulators)
- [x] Internal header `src/spu94/spu94_fir_internal.h` — created in Plan 01 Task 1; bodies filled in Plans 02 (stages) and 03 (chain wrapper)
- [x] `.planning/phases/04-.../witness-captures/` directory — created in Plan 04 Task 3 with README.md protocol + classification table

All Wave 0 deliverables either exist after Plan 01 or are scheduled within Plans 02/03/04. No `<automated>MISSING</automated>` references in any task.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Mednafen / DuckStation FIR-implementation empirical classification | CORE-06 / CORE-07 witness axis (D-14, SC-4 support) | Requires installed third-party emulator binaries + PSX test ROM + FFT inspection of audio output; PSX test ROM is not part of this repo | Full protocol in `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/witness-captures/README.md`. Four probe signals (unit impulse / 20 kHz sine / white-noise burst / band-limited sweep) fed into each emulator; WAV capture via `-sound.driver file` (Mednafen) or `--dump-audio` (DuckStation); numpy FFT analysis; classification per 04-RESEARCH § Witness Analysis table. Graceful deferral branch: ADR-0012 lands with "classification pending — protocol documented"; Phase 7 TEST-03 picks up. |
| UBSan clean under adversarial FIR input | SC-3 regression cover | UBSan instrumentation requires a separate build variant (`-fsanitize=undefined`); CI runs this automatically | `cmake -S . -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=undefined -fno-sanitize-recover=undefined" && cmake --build build-ubsan && ctest --test-dir build-ubsan -L fir`. The Phase 1 CI UBSan job covers this; local testing is optional. |

Only these two verifications are not directly wired as `<automated>` in per-task verify blocks. All other Phase 4 work has automated verification.

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify OR Wave 0 dependencies (Task 04-04 T3 is semi-manual but produces an automated-checkable artifact; deferral is explicit, non-blocking)
- [x] Sampling continuity: no 3 consecutive tasks without automated verify (every task in every plan has a concrete ctest / grep command)
- [x] Wave 0 covers all MISSING references (none — all fixtures, Python references, bibliography, and witness infrastructure land in Plans 01/04)
- [x] No watch-mode flags (all verifies are one-shot)
- [x] Feedback latency < 15 s (full suite including 3 fuzz harnesses)
- [x] `nyquist_compliant: true` set in frontmatter

**Approval:** approved (planner, 2026-04-20)
