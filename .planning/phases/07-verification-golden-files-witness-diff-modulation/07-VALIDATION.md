---
phase: 7
slug: verification-golden-files-witness-diff-modulation
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-23
---

# Phase 7 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.
>
> Phase 7 *is* the project's validation layer for Phases 1–6. Each harness (witness-diff, golden-file regression, modulation, benchmark, coverage-check, bibliography-ref-check) therefore needs its own **meta-test** proving the harness itself behaves correctly. Rows below map harnesses + meta-tests to the tasks that ship them.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | pytest 7.x (Python harnesses), ctest (C unit tests), bash (shell harnesses) |
| **Config file** | `tests/python/pytest.ini` (existing), `CMakeLists.txt` + `tests/CMakeLists.txt` (existing) |
| **Quick run command** | `cmake --build build --target test` |
| **Full suite command** | `cmake --build build --target test && pytest tests/python/ -x -q` |
| **Estimated runtime** | ~90 seconds (current Phase 1–6 unit suite); Phase 7 adds ~60s (goldens) + ~120s (witness build+run) + ~180s (modulation 35×3) + ~60s (benchmark) ⇒ full Phase-7 suite ~8–10 minutes end-to-end |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target test` (C unit suite + fast python subset).
- **After every plan wave:** Run the relevant new harness end-to-end (e.g., after the goldens wave, `python scripts/regenerate_goldens.py --check`; after the witness wave, the witness-diff meta-test).
- **Before `/gsd-verify-work`:** Full Phase-7 suite green: unit + coverage-check + goldens-check + witness-diff meta-test + modulation stability+determinism + benchmark-no-heap + bibliography-cross-ref.
- **Max feedback latency:** 90s for unit / coverage / goldens-check; 180s for modulation; 300s for witness-diff meta-test.

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 07-01-01 | 01 | 0 | BUILD-08 | — | N/A | env-prep | `command -v lv2apply && python3 -c "import scipy, pytest_benchmark"` | ❌ W0 | ⬜ pending |
| 07-01-02 | 01 | 1 | TEST-01 | — | COVERAGE.md parser rejects rows with missing tests | unit | `pytest scripts/test_check_coverage.py -q` | ❌ W0 | ⬜ pending |
| 07-01-03 | 01 | 1 | TEST-01 | — | Coverage-check fails if any row names a missing or failing test (negative meta-test) | unit | `pytest scripts/test_check_coverage.py::test_fails_on_gap -q` | ❌ W0 | ⬜ pending |
| 07-02-01 | 02 | 1 | TEST-04 | — | Golden `.wav` + `.sha256` sidecar regenerate byte-identically on host | integration | `python scripts/regenerate_goldens.py --check` | ❌ W0 | ⬜ pending |
| 07-02-02 | 02 | 2 | TEST-08 / BUILD-08 | — | Docker-pinned container regenerates goldens byte-identically vs committed SHA-256 sidecars | ci-integration | `docker build -f Dockerfile.repro -t spu94-repro . && docker run --rm spu94-repro scripts/regenerate_goldens.py --check` | ❌ W0 | ⬜ pending |
| 07-03-01 | 03 | 1 | TEST-03 | — | lv2-psx-reverb builds at pinned commit; `lv2apply` renders each preset × standard-input once per run | ci-integration | `bash scripts/witness_diff_build.sh` | ❌ W0 | ⬜ pending |
| 07-03-02 | 03 | 2 | TEST-03 | — | Split-band aligned-RMS divergence numbers for each preset × input are reported to `witness_report.json` | integration | `python scripts/witness_diff.py --report .artifacts/witness_report.json` | ❌ W0 | ⬜ pending |
| 07-03-03 | 03 | 2 | TEST-03 | — | Witness-diff harness is self-consistent: running twice on same source produces identical `witness_report.json` (meta-test for determinism of the *measurement*, not the tolerance) | meta-test | `pytest tests/python/test_witness_determinism.py -q` | ❌ W0 | ⬜ pending |
| 07-04-01 | 04 | 1 | TEST-05 | — | Modulation harness exercises all 35 registers × 3 modes without crash/NaN/unbounded output | integration | `pytest tests/python/test_modulation_harness.py -q` | ❌ W0 | ⬜ pending |
| 07-04-02 | 04 | 1 | TEST-05 | — | Same register modulation stream + same input produces bit-identical output across two runs (determinism gate; Phase 2 D-08 write policy verification) | integration | `pytest tests/python/test_modulation_harness.py::test_determinism -q` | ❌ W0 | ⬜ pending |
| 07-04-03 | 04 | 2 | DOCS-02 | — | Mechanical columns in `docs/LEVERS-CATALOG.md` are populated by the harness idempotently (running harness twice leaves file byte-identical; human-authored subjective columns preserved) | integration | `pytest tests/python/test_levers_catalog_writer.py -q` | ❌ W0 | ⬜ pending |
| 07-05-01 | 05 | 1 | BUILD-06 | — | `spu94_process` call tree has zero heap syscalls across all 10 presets (strace filter: `brk`, `mmap`, `mmap2`, `munmap`) | ci-integration | `bash tests/rt_safety/hotpath_alloc_gate.sh` | ❌ W0 | ⬜ pending |
| 07-05-02 | 05 | 1 | BUILD-06 | — | pytest-benchmark produces timing JSON artifact for each preset × block-size combo | ci-artifact | `pytest tests/python/test_benchmark.py --benchmark-json=.benchmarks/out.json` | ❌ W0 | ⬜ pending |
| 07-05-03 | 05 | 1 | BUILD-06 | — | Allocation gate meta-test: intentionally-inserted `malloc` triggers gate failure (negative meta-test, run in isolated fixture) | meta-test | `pytest tests/python/test_hotpath_alloc_gate_meta.py -q` | ❌ W0 | ⬜ pending |
| 07-06-01 | 06 | 1 | DOCS-03 | — | Every claim in `docs/DECISIONS.md` that cites `BIB-nnn` resolves to an existing entry in `docs/BIBLIOGRAPHY.md` | integration | `python scripts/check_bibliography_refs.py` | ❌ W0 | ⬜ pending |
| 07-06-02 | 06 | 1 | DOCS-03 | — | Bibliography-ref-check fails when a `BIB-nnn` reference points to a missing entry (negative meta-test) | meta-test | `pytest scripts/test_check_bibliography_refs.py::test_fails_on_dangling_ref -q` | ❌ W0 | ⬜ pending |
| 07-06-03 | 06 | 2 | DOCS-03 | — | Eight ADR-Phase-7-A..H entries present in `docs/DECISIONS.md` with required header + rationale fields | integration | `python scripts/check_adr_format.py docs/DECISIONS.md --require ADR-Phase-7-A,ADR-Phase-7-B,ADR-Phase-7-C,ADR-Phase-7-D,ADR-Phase-7-E,ADR-Phase-7-F,ADR-Phase-7-G,ADR-Phase-7-H` | ❌ W0 | ⬜ pending |

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

- [ ] **Host & container tooling installs** — `lilv-utils` (`lv2apply`), `python3-scipy`, `pytest-benchmark`, `strace` present on both host and in `Dockerfile.repro` base.
- [ ] `scripts/check_coverage.py` — COVERAGE.md parser + test-existence + test-pass verifier (rejects rows with empty `test:` field, missing test, or failing test).
- [ ] `scripts/test_check_coverage.py` — positive + negative meta-tests for the coverage checker.
- [ ] `scripts/regenerate_goldens.py` — shells out to `spu94` CLI; writes 50 `.wav` + 50 `.sha256` pairs under `tests/golden/<preset>/<input>.{wav,sha256}`; supports `--check` mode that diffs SHA-256 sidecars.
- [ ] `Dockerfile.repro` — pinned `debian:bookworm-slim@sha256:…` base; installs gcc + cmake + ninja + python3 + python3-pip + python3-numpy + python3-scipy + pytest + pytest-benchmark + strace + git + lilv-utils + lv2-dev + coreutils; sets `SOURCE_DATE_EPOCH`, `LC_ALL=C`, `TZ=UTC`.
- [ ] `scripts/witness_diff_build.sh` — clones lv2-psx-reverb at pinned SHA, builds the LV2 bundle, verifies `lv2info` discovery.
- [ ] `scripts/witness_diff.py` — renders each preset × standard-input through both SPU-94 (via CLI) and lv2-psx-reverb (via `lv2apply`), band-splits with `scipy.signal.butter(8, 10000/22050, ..., output='sos')` + `sosfiltfilt`, computes aligned-RMS via `scipy.signal.correlate(..., method='fft')`, writes `.artifacts/witness_report.json`.
- [ ] `tests/python/test_witness_determinism.py` — runs `witness_diff.py` twice on same source and compares output JSON byte-wise (modulo timestamp normalization).
- [ ] `tests/python/test_modulation_harness.py` — `pytest.parametrize("reg", list(spu94.Register))` × `pytest.parametrize("mode", ["sine", "sweep", "random_walk"])`, with explicit `test_determinism` case that runs same modulation stream twice and asserts byte-identical output.
- [ ] `tests/python/test_levers_catalog_writer.py` — verifies mechanical-column writer is idempotent and preserves human-authored subjective columns.
- [ ] `tests/rt_safety/hotpath_alloc_gate.sh` — strace wrapper filtering `brk,mmap,mmap2,munmap`, masking warm-up syscalls before the marker, fails on any remaining.
- [ ] `tests/python/test_benchmark.py` — pytest-benchmark harness parametrized over 10 presets × 2 block sizes; `min_rounds=5`, `warmup=True`.
- [ ] `tests/python/test_hotpath_alloc_gate_meta.py` — negative meta-test that injects a heap call and asserts the gate fires.
- [ ] `tests/python/benchmark_baselines.json` — committed baseline (formatted pytest-benchmark JSON extract); refreshed via explicit human action, not CI.
- [ ] `scripts/check_bibliography_refs.py` — cross-reference DECISIONS.md `BIB-nnn` mentions against BIBLIOGRAPHY.md entries.
- [ ] `scripts/test_check_bibliography_refs.py` — positive + negative meta-tests for the ref checker.
- [ ] `scripts/check_adr_format.py` — validates ADR-Phase-X-Y headers and required fields; existing or new per planner.
- [ ] `.github/workflows/ci.yml` — new `reproducibility` job that builds `Dockerfile.repro` and runs `regenerate_goldens.py --check` inside the container; new `witness_diff` job that runs the witness harness on the host; existing `unit` and `python` jobs extend to include the Phase 7 additions.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Subjective columns in `docs/LEVERS-CATALOG.md` (musical role, M4 lever grouping) | DOCS-02 | These are hand-written by Anthony; the harness only writes mechanical columns. | Review the 35-row LEVERS-CATALOG.md, confirm every row has a non-empty `musical_role` and `m4_grouping` field, and spot-check three rows for plausibility against the register's semantics. |
| BIBLIOGRAPHY.md tone match to README | DOCS-03 | Polish is subjective editorial judgment; an automated linter can't grade prose quality. | Read the new/edited entries top-to-bottom; confirm tone matches `README.md`; no transcribed passages (compare any quoted phrases against source to ensure paraphrase). |
| Wayback snapshot citation correctness | DOCS-03 | Manual confirmation that the pinned archive.org URL still resolves and contains every reverb section cited in COVERAGE.md's per-spec-paragraph rows. | Open each cited snapshot URL in a browser; verify the anchor/section exists and the text matches what the row claims. |
| Per-preset tolerance-policy ADR (D-06 deferred) | TEST-03 | Requires human review of measured divergence numbers before a tolerance is written. | After Plan 07-03 produces `witness_report.json`, Anthony + Claude review numbers, decide: frozen per-preset value / single engineering threshold / report-only forever. Land as ADR-Phase-7-B-addendum or Phase 7.1. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references
- [ ] No watch-mode flags
- [ ] Feedback latency < 300s (witness-diff is the slowest; other gates < 90s)
- [ ] `nyquist_compliant: true` set in frontmatter

**Approval:** pending
