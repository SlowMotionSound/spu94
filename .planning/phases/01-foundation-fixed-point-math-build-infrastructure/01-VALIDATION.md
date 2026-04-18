---
phase: 1
slug: foundation-fixed-point-math-build-infrastructure
status: planned
nyquist_compliant: true
wave_0_complete: false
created: 2026-04-18
plan_populated: 2026-04-18
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (vendored, C unit tests) via CTest |
| **Config file** | `CMakeLists.txt` + `tests/unit/CMakeLists.txt` (Plan 01 installs Unity vendor + CTest wiring) |
| **Quick run command** | `cmake --build build --target test_q15 && ctest --test-dir build -R q15 --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-flags.sh && bash scripts/ci/test-grep-guard.sh` |
| **Estimated runtime** | ~15 seconds (unit tests < 1s; clang-tidy/cppcheck/UBSan build dominate when run in CI) |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target test_q15 && ctest --test-dir build -R q15 --output-on-failure` (for Q15 tasks) OR `cmake --build build` (for CMake/CI tasks)
- **After every plan wave:** Run full suite (build + ctest + grep guard + flag verification + test-grep-guard)
- **Before `/gsd-verify-work`:** Full suite green AND GitHub Actions workflow dry-run green AND grep guard + flag verification both firing correctly on positive + negative fixtures
- **Max feedback latency:** 20 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| 01-01.T1 | 01 | 1 | BUILD-01, BUILD-02 | T-01-03, T-01-04 | Determinism flags present PRIVATE on `spu94_obj`; shared+static both produced; scaffold dirs untouched | build + grep-on-CDB | `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && cmake --build build && test -f build/src/spu94/libspu94.so && test -f build/src/spu94/libspu94.a && grep -q -- '-ffp-contract=off' build/compile_commands.json && grep -q -- '-fno-fast-math' build/compile_commands.json && grep -q -- '-Werror' build/compile_commands.json` | ❌ W0 | ⬜ pending |
| 01-01.T2 | 01 | 1 | CORE-01, BUILD-01 | T-01-01 (Unity vendor pin) | Q15 helpers match hand-computed reference table; `_Static_assert` confirms ASR; Unity SHA-256 recorded | unit | `ctest --test-dir build -R q15 --output-on-failure && grep -q 'static inline int16_t q15_mul_truncate(' include/spu94/spu94_q15.h && grep -q '_Static_assert' include/spu94/spu94_q15.h && grep -q 'SHA-256' tests/unit/vendor/Unity/README.md` | ❌ W0 | ⬜ pending |
| 01-02.T1 | 02 | 2 | BUILD-02, BUILD-07 | T-01-03, T-01-04 | Grep-guard fires on forbidden tokens + allows `long long`; verify-flags parses CDB and rejects missing flags | grep-guard + fixture meta-test | `bash scripts/ci/grep-guard.sh && cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && bash scripts/ci/verify-flags.sh && bash scripts/ci/test-grep-guard.sh` | ❌ W0 | ⬜ pending |
| 01-02.T2 | 02 | 2 | BUILD-04, BUILD-05, BUILD-07 | T-01-01 (GHA action SHA pins), T-01-02 (UBSan hard abort) | CI runs matrix gcc+clang build, clang-tidy, cppcheck, UBSan with fno-sanitize-recover, grep-guard, verify-flags; actions pinned to full commit SHAs | static-analysis + CI config | `test -f .clang-tidy && test -f .github/workflows/ci.yml && grep -q 'matrix:' .github/workflows/ci.yml && grep -q 'fno-sanitize-recover=undefined' .github/workflows/ci.yml && ! grep -E '^\s*uses:\s+\S+@v[0-9]' .github/workflows/ci.yml` | ❌ W0 | ⬜ pending |
| 01-03.T1 | 03 | 1 | DOCS-01 | T-01-05 (decision-loss discipline), T-01-02 (pre-authorized UBSan macro) | ADR-0001, ADR-0002, ADR-0003 present with five-section format; SPU94_NO_SANITIZE_INTEGER macro defined; no transcribed nocash prose (human-review gate noted) | file-check + content-grep | `test -f docs/DECISIONS.md && [ "$(grep -c '^## ADR-' docs/DECISIONS.md)" = "3" ] && grep -q 'SPU94_NO_SANITIZE_INTEGER' docs/DECISIONS.md && grep -q '\*\*Sources:\*\*' docs/DECISIONS.md` | ❌ W0 | ⬜ pending |
| 01-03.T2 | 03 | 1 | DOCS-05 | T-01-06 (license ambiguity) | LICENSE placeholder explicitly deferred to end of M1; no "All rights reserved"; no copyright line | file-check + content-grep | `test -f LICENSE && grep -q 'deferred' LICENSE && grep -q 'MIT' LICENSE && grep -q 'Apache' LICENSE && grep -q 'Milestone 1' LICENSE && ! grep -qi 'all rights reserved' LICENSE && ! grep -qE '^Copyright \(c\)' LICENSE` | ❌ W0 | ⬜ pending |

*Every task maps to at least one of: (a) unit test under `tests/unit/`, (b) CMake build assertion verifiable in `compile_commands.json`, (c) CI static-analysis job, (d) grep-guard fixture test, (e) a `<manual>` verify block with explicit justification in Manual-Only Verifications below.*

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Phase 1 is greenfield — no existing test framework, no build system, no CI. Wave 0 installs everything. The planner MUST ensure these land before any subsequent wave runs:

- [ ] `CMakeLists.txt` (top-level) — declares `libspu94` OBJECT library with determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) applied PRIVATE; shared + static consumers built from the OBJECT library for flag-identical artifacts. **(Plan 01, Task 1)**
- [ ] `include/spu94/spu94_q15.h` — header-only `static inline` Q15 helpers with the exact names from CONTEXT.md (`q15_mul_truncate`, `sat_s16`, `q15_add_sat`). **(Plan 01, Task 2)**
- [ ] `tests/unit/q15/test_q15.c` — Unity-based tests with inline `{input_a, input_b, expected}` tables (NO fixture loader). **(Plan 01, Task 2)**
- [ ] `tests/unit/CMakeLists.txt` — CTest registration; Unity vendored at `tests/unit/vendor/Unity/` with pinned version. **(Plan 01, Task 2)**
- [ ] `scripts/ci/grep-guard.sh` — portable grep invocation that fails on `float|double|malloc|calloc|realloc|free|unqualified long` in `src/` and `include/`. Word-boundary aware. Allows `long long`. **(Plan 02, Task 1)**
- [ ] `scripts/ci/verify-flags.sh` — parses `compile_commands.json` to assert determinism flags are present on every core TU. **(Plan 02, Task 1)**
- [ ] `scripts/ci/test-grep-guard.sh` — positive+negative fixture meta-test for the grep guard. **(Plan 02, Task 1)**
- [ ] `.github/workflows/ci.yml` — GitHub Actions job matrix running: build (Clang + GCC), ctest, clang-tidy, cppcheck, UBSan build with `no_sanitize("integer")` surgical annotations (macro authorized by ADR-0003), grep guard, flag verification. Third-party actions pinned to full commit SHAs per T-01-01. **(Plan 02, Task 2)**
- [ ] `.clang-tidy` — minimal embedded-C ruleset with `WarningsAsErrors: '*'` and `HeaderFilterRegex` scoped to `include|src/spu94`. **(Plan 02, Task 2)**
- [ ] cppcheck CLI args in CI — suppressions documented inline (vendored Unity suppressed). **(Plan 02, Task 2)**
- [ ] `docs/DECISIONS.md` — ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR = -0x8000), ADR-0003 (UBSan `no_sanitize` surgical policy + macro template). **(Plan 03, Task 1)**
- [ ] `LICENSE` — placeholder noting MIT/Apache-2.0 pick deferred to end of M1; no copyright line; no "All rights reserved" phrase. **(Plan 03, Task 2)**

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Hand-computed Q15 reference table audited by human | CORE-01 | The whole point of Phase 1 is that *humans* can verify the truncation-direction reference. The test suite asserts against a table; the table itself must be reviewed by a human who understands Q15 + ASR semantics before ADR-0001 is marked `Accepted`. | Open `tests/unit/q15/test_q15.c`, read the `{input_a, input_b, expected_q15_mul_truncate}` table, spot-check at least 5 entries against ADR-0001's documented rule (ASR `>> 15` with truncation toward −∞). Confirm `INT16_MIN × INT16_MIN` saturates to `INT16_MAX`. |
| `LICENSE` wording captures deferred MIT/Apache-2.0 stance | DOCS-05 | Legal-adjacent prose; requires human read. | Read `LICENSE`, confirm it states the final pick is deferred to end of M1 and notes the candidate licenses. No automated check for the prose quality itself; grep checks only keyword presence. |
| ADR-0001, ADR-0002, ADR-0003 prose quality + nocash paraphrase discipline | DOCS-01 | ADR entries are prose + reasoning; automated test can only confirm file existence and section headings, not decision quality or that prose was paraphrased (not transcribed) from nocash. | Human review of `docs/DECISIONS.md` ADR-0001, ADR-0002, ADR-0003 for: (a) all five required sections present, (b) rationale sound, (c) no GPL source cited as primary, (d) nocash references are paraphrased, not transcribed, (e) `BIB-NNN` placeholder citations are used for nocash facts. |
| GitHub Actions SHA pins verified against upstream | T-01-01 | `gh api` lookup at implementation time; cannot be static-checked at plan time. | At implementation, for each `uses:` line, run `gh api repos/<owner>/<repo>/git/refs/tags/<tag> --jq .object.sha` and confirm the pinned SHA matches. Record the SHAs in `01-02-SUMMARY.md`. |

---

## Validation Sign-Off

- [x] All tasks have `<automated>` verify or Wave 0 dependencies — every task's Automated Command column is populated
- [x] Sampling continuity: no 3 consecutive tasks without automated verify — all 6 tasks have automated verify
- [x] Wave 0 covers all MISSING references (CMake, Unity, CTest, GHA, DECISIONS.md, LICENSE) — mapped to Plan 01 / Plan 02 / Plan 03 as annotated above
- [x] No watch-mode flags (ctest + cmake --build only) — commands use `ctest --test-dir build` and `cmake --build build`; no `--rerun-failed --watch` or similar
- [x] Feedback latency < 20s — Q15 unit suite is < 1s; build dominates at ~15s
- [x] `nyquist_compliant: true` set in frontmatter after planner fills per-task map — see frontmatter above

**Approval:** planner-complete 2026-04-18; awaits executor wave runs.
