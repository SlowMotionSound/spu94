---
phase: 1
slug: foundation-fixed-point-math-build-infrastructure
status: draft
nyquist_compliant: false
wave_0_complete: false
created: 2026-04-18
---

# Phase 1 — Validation Strategy

> Per-phase validation contract for feedback sampling during execution.

---

## Test Infrastructure

| Property | Value |
|----------|-------|
| **Framework** | Unity (vendored, C unit tests) via CTest |
| **Config file** | `CMakeLists.txt` + `tests/unit/CMakeLists.txt` (Wave 0 installs Unity vendor + CTest wiring) |
| **Quick run command** | `cmake --build build --target test_q15 && ctest --test-dir build -R q15 --output-on-failure` |
| **Full suite command** | `cmake --build build && ctest --test-dir build --output-on-failure && bash scripts/ci/grep-guard.sh && bash scripts/ci/verify-flags.sh` |
| **Estimated runtime** | ~15 seconds (unit tests < 1s; clang-tidy/cppcheck/UBSan build dominate) |

---

## Sampling Rate

- **After every task commit:** Run `cmake --build build --target test_q15 && ctest --test-dir build -R q15 --output-on-failure` (for Q15 tasks) OR `cmake --build build` (for CMake/CI tasks)
- **After every plan wave:** Run full suite (build + ctest + grep guard + flag verification)
- **Before `/gsd-verify-work`:** Full suite green AND GitHub Actions workflow dry-run green AND grep guard + flag verification both firing correctly on positive + negative fixtures
- **Max feedback latency:** 20 seconds

---

## Per-Task Verification Map

| Task ID | Plan | Wave | Requirement | Threat Ref | Secure Behavior | Test Type | Automated Command | File Exists | Status |
|---------|------|------|-------------|------------|-----------------|-----------|-------------------|-------------|--------|
| *Planner fills* | *Planner fills* | *Planner fills* | *from phase_req_ids* | — / N/A (no external inputs in Phase 1) | — | unit / build / static-analysis / grep-guard | *per task* | ❌ W0 | ⬜ pending |

*Planner MUST populate this table when producing PLAN files. Every task MUST map to at least one of: (a) unit test under `tests/unit/`, (b) CMake build assertion verifiable in `compile_commands.json`, (c) CI static-analysis job, (d) grep-guard fixture test, (e) a `<manual>` verify block with explicit justification in Manual-Only Verifications below.*

*Status: ⬜ pending · ✅ green · ❌ red · ⚠️ flaky*

---

## Wave 0 Requirements

Phase 1 is greenfield — no existing test framework, no build system, no CI. Wave 0 installs everything. The planner MUST ensure these land before any subsequent wave runs:

- [ ] `CMakeLists.txt` (top-level) — declares `libspu94` OBJECT library with determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) applied PRIVATE; shared + static consumers built from the OBJECT library for flag-identical artifacts.
- [ ] `include/spu94/spu94_q15.h` — header-only `static inline` Q15 helpers with the exact names from CONTEXT.md (`q15_mul_truncate`, `sat_s16`, `q15_add_sat`).
- [ ] `tests/unit/q15/test_q15.c` — Unity-based tests with inline `{input_a, input_b, expected}` tables (NO fixture loader).
- [ ] `tests/unit/CMakeLists.txt` — CTest registration; Unity vendored at `tests/unit/vendor/Unity/` with pinned version.
- [ ] `scripts/ci/grep-guard.sh` — portable grep invocation that fails on `float|double|malloc|calloc|realloc|free|unqualified long` in `src/` and `include/`. Word-boundary aware. Allows `long long`.
- [ ] `scripts/ci/verify-flags.sh` — parses `compile_commands.json` to assert determinism flags are present on every core TU.
- [ ] `.github/workflows/ci.yml` — GitHub Actions job matrix running: build (Clang + GCC), ctest, clang-tidy, cppcheck, UBSan build with `no_sanitize("integer")` surgical annotations, grep guard, flag verification.
- [ ] `.clang-tidy` — minimal embedded-C ruleset (planner to define explicit check list).
- [ ] `cppcheck.cfg` (or equivalent CLI args in CI) — suppressions documented inline.
- [ ] `docs/DECISIONS.md` — ADR-0001 (Q15 multiply semantics) and ADR-0002 (vIIR = -0x8000) seeded; ADR-0003 likely seeded for UBSan `no_sanitize` function list.
- [ ] `LICENSE` — placeholder noting MIT/Apache-2.0 pick deferred to end of M1.

---

## Manual-Only Verifications

| Behavior | Requirement | Why Manual | Test Instructions |
|----------|-------------|------------|-------------------|
| Hand-computed Q15 reference table audited by human | CORE-01 | The whole point of Phase 1 is that *humans* can verify the truncation-direction reference. The test suite asserts against a table; the table itself must be reviewed by a human who understands Q15 + ASR semantics before ADR-0001 is marked `Accepted`. | Open `tests/unit/q15/test_q15.c`, read the `{input_a, input_b, expected_q15_mul_truncate}` table, spot-check at least 5 entries against ADR-0001's documented rule (ASR `>> 15` with truncation toward −∞). Confirm `INT16_MIN × INT16_MIN` saturates to `INT16_MAX`. |
| `LICENSE` wording captures deferred MIT/Apache-2.0 stance | DOCS-05 | Legal-adjacent prose; requires human read. | Read `LICENSE`, confirm it states the final pick is deferred to end of M1 and notes the candidate licenses. No automated check. |
| ADR-0001 and ADR-0002 prose quality | DOCS-01 | ADR entries are prose + reasoning; automated test can only confirm file existence and section headings, not decision quality. | Human review of `docs/DECISIONS.md` ADR-0001 and ADR-0002 for: Status, Context, Decision, Consequences, Sources all present; rationale sound; no GPL source cited. |

---

## Validation Sign-Off

- [ ] All tasks have `<automated>` verify or Wave 0 dependencies
- [ ] Sampling continuity: no 3 consecutive tasks without automated verify
- [ ] Wave 0 covers all MISSING references (CMake, Unity, CTest, GHA, DECISIONS.md, LICENSE)
- [ ] No watch-mode flags (ctest + cmake --build only)
- [ ] Feedback latency < 20s
- [ ] `nyquist_compliant: true` set in frontmatter after planner fills per-task map

**Approval:** pending
