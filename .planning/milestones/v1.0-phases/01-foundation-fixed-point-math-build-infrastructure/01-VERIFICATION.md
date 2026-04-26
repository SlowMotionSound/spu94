---
phase: 01-foundation-fixed-point-math-build-infrastructure
verified: 2026-04-18T23:05:00Z
status: human_needed
score: 5/5 must-haves verified
overrides_applied: 0
re_verification:
  previous_status: gaps_found
  previous_score: 4/5
  gaps_closed:
    - "The grep guard's line-granular 'long long' subtraction is documented as a known limitation; a fixture case asserts the documented behavior on a same-line 'long long X; long Y;' mix so future regressions surface immediately"
  gaps_remaining: []
  regressions: []
human_verification:
  - test: "Push the branch to GitHub and confirm the CI workflow runs green across all 5 jobs (build-and-test gcc, build-and-test clang, grep-guard, clang-tidy, cppcheck, ubsan)"
    expected: "All 5 CI jobs pass. clang-tidy reports no findings on src/spu94/ + include/spu94/ with WarningsAsErrors='*'; cppcheck reports no findings; UBSan build compiles and ctest passes clean under -fsanitize=undefined -fno-sanitize-recover=undefined with clang"
    why_human: "clang, clang-tidy, cppcheck, and ninja are not installed on the build host. Local dry-run covered only gcc build + ctest + bash scripts. The clang matrix leg, static-analysis jobs, and UBSan job are exercised for the first time on GitHub Actions."
  - test: "Read tests/unit/q15/test_q15.c mul_cases[] table and spot-check at least 5 entries against ADR-0001's rule: sat_s16((int32_t)a * (int32_t)b >> 15)"
    expected: "All entries match. Confirm in particular: (a) INT16_MIN * INT16_MIN == INT16_MAX (saturated from +32768), (b) -1 * 1 == -1 (not 0 as C division would give), (c) -100 * 100 == -1 (ASR of -10000 >> 15 rounds toward -inf), (d) INT16_MIN * INT16_MAX == -32767"
    why_human: "Per VALIDATION.md Manual-Only Verifications: the test suite passes green but the table's values are what prove correctness, not the test runner. Human arithmetic audit is the stated verification method."
  - test: "Read docs/DECISIONS.md ADR-0001, ADR-0002, ADR-0003 in full for prose quality and nocash paraphrase discipline"
    expected: "(a) Five required sections in order per ADR. (b) Rationale is sound. (c) No GPL source cited as primary. (d) Nocash references are paraphrased, not transcribed. (e) All external citations use BIB-NNN placeholder keys."
    why_human: "Automated checks confirm file structure and keyword presence only. The licensing posture, reasoning quality, and paraphrase discipline are matters of human judgment."
  - test: "Run: gh api repos/actions/checkout/git/refs/tags/v4.2.2 --jq .object.sha and confirm the result matches 11bd71901bbe5b1630ceea73d27597364c9af683 (the SHA pinned in .github/workflows/ci.yml)"
    expected: "The API returns exactly 11bd71901bbe5b1630ceea73d27597364c9af683"
    why_human: "The two-part automated grep confirms a 40-hex SHA is present and no floating tags exist, but cannot verify the SHA value is correct for the claimed tag without a network call to the GitHub refs API."
---

# Phase 1: Foundation — Fixed-Point Math + Build Infrastructure Verification Report

**Phase Goal:** Every downstream module can rely on correct Q15 arithmetic, a deterministic build, and a committed place to log gray-area decisions.
**Verified:** 2026-04-18T23:05:00Z
**Status:** human_needed
**Re-verification:** Yes — after gap closure (Plan 01-04 closed the sole gap from the prior run)

## Re-Verification Summary

The single gap identified in the prior run (2026-04-18T20:47:06Z) was:

- `scripts/ci/grep-guard.sh` missing a formal KNOWN LIMITATIONS block
- `scripts/ci/test-grep-guard.sh` missing fixture case 7 ("known limitation: mixed long long + long on one line")

Plan 01-04 (commit `6adabf8`) closed both items exactly as specified. This re-verification confirms all 5 success criteria pass locally. The remaining `human_needed` items are unchanged from the prior run — they are CI-execution and human-review items that are not automatable, not new gaps.

## Goal Achievement

### Observable Truths (Success Criteria from ROADMAP.md)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `libspu94` builds on Linux as shared and static library artifacts with determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) locked in and verifiable in the compile command line | ✓ VERIFIED | `build/src/spu94/libspu94.so` and `build/src/spu94/libspu94.a` both present. All three flags confirmed in `build/compile_commands.json` on the `spu94_placeholder.c` TU command line. Regression check: unchanged from prior run. |
| 2 | A developer can call `q15_mul_truncate`, `sat_s16`, and related fixed-point helpers with negative operands, `INT16_MIN`, and boundary values, and the outputs match a hand-computed truncation-direction reference (never rounding) | ✓ VERIFIED | All three `static inline` functions present in `include/spu94/spu94_q15.h`. `_Static_assert` guard present. `ctest --test-dir build -R q15 --output-on-failure` exits 0: `1/1 Test #1: q15_unit PASSED`. 17 `q15_mul_truncate` cases including INT16_MIN^2 saturation and ASR-vs-division distinguisher. Human review of table required (see Human Verification). |
| 3 | CI runs clang-tidy, cppcheck, compiler-warnings-as-errors, and UBSan with `no_sanitize("integer")` annotations on documented SPU saturation functions, and the suite goes green on an empty reverb core | ? HUMAN NEEDED | All 5 CI jobs wired in `.github/workflows/ci.yml`. Compiler warnings-as-errors enforced via `cmake/spu94_warnings.cmake`. UBSan job uses `-fno-sanitize-recover=undefined`. Phase 1 has zero no_sanitize annotations (correct per ADR-0003). "Goes green on empty reverb core" requires first GitHub Actions run — clang, clang-tidy, cppcheck not installed locally. |
| 4 | A CI grep guard fails the build if `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, or unqualified `long` appear in core library sources | ✓ VERIFIED | `bash scripts/ci/grep-guard.sh` exits 0 on real tree (3 files scanned). `bash scripts/ci/test-grep-guard.sh` exits 0 with all 7 PASS lines including case 7 ("known limitation: mixed long long + long on one line"). KNOWN LIMITATIONS block present in grep-guard.sh with "line-granular" term and explicit fixture pointer. Gap from prior run is fully closed. |
| 5 | `DECISIONS.md` exists and contains resolved entries for (a) Q15 multiply semantics and (b) vIIR = -0x8000 policy; `LICENSE` placeholder notes the MIT/Apache-2.0 pick is deferred to end of M1 | ✓ VERIFIED | `docs/DECISIONS.md` has exactly 3 `## ADR-` headings. ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR = -0x8000 anomaly), ADR-0003 (UBSan no_sanitize policy). All five sections per ADR. `LICENSE` contains "deferred", "MIT", "Apache", "Milestone 1"; no "All rights reserved"; no copyright line. Regression check: unchanged from prior run. |

**Score: 5/5 truths verified locally.** SC3 routes to human verification (CI run required). Status is `human_needed`.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `CMakeLists.txt` | Top-level CMake, C11, EXPORT_COMPILE_COMMANDS, include(CTest), add_subdirectory | ✓ VERIFIED | Present. All required elements confirmed. |
| `cmake/spu94_warnings.cmake` | INTERFACE target with determinism + warning flags | ✓ VERIFIED | `add_library(spu94_warnings INTERFACE)` present. All three determinism flags present. |
| `src/spu94/CMakeLists.txt` | spu94_obj OBJECT + spu94_shared + spu94_static | ✓ VERIFIED | `add_library(spu94_obj OBJECT` present. Both consumer targets wired. `target_link_libraries(spu94_obj PRIVATE spu94_warnings)` present. |
| `src/spu94/spu94_placeholder.c` | Non-empty TU, BUILD-07 compliant | ✓ VERIFIED | File exists, BUILD-07 compliant (no forbidden tokens). |
| `include/spu94/spu94.h` | Umbrella header with `#include <spu94/spu94_q15.h>` | ✓ VERIFIED | `#include <spu94/spu94_q15.h>` present. `extern "C"` guards present. |
| `include/spu94/spu94_q15.h` | Q15 static inline helpers + `_Static_assert` | ✓ VERIFIED | All three function signatures present as `static inline`. `_Static_assert` present. ADR-0001 referenced in comments. |
| `tests/unit/q15/test_q15.c` | Unity TU with hand-computed reference table | ✓ VERIFIED | Present with `TEST_ASSERT_EQUAL_INT16` assertions. 17 `mul_cases[]` entries. INT16_MIN^2 saturation case and ASR-vs-division distinguisher present. |
| `tests/unit/vendor/Unity/unity.c` | Vendored Unity v2.6.1 source | ✓ VERIFIED | Present. README.md has SHA-256 pins and v2.6.1 reference. |
| `python/spu94/.gitkeep` | Reserved Phase 6 location | ✓ VERIFIED | File exists. |
| `.github/workflows/ci.yml` | 5-job CI workflow | ✓ VERIFIED | All 5 jobs present: `build-and-test` (matrix gcc+clang), `grep-guard`, `clang-tidy`, `cppcheck`, `ubsan`. SHA-pinned (both positive and negative grep conditions pass). UBSan with `-fno-sanitize-recover=undefined`. |
| `.clang-tidy` | clang-tidy ruleset with WarningsAsErrors | ✓ VERIFIED | Present. `WarningsAsErrors: '*'` present. `HeaderFilterRegex` scoped to `include\|src/spu94`. |
| `scripts/ci/grep-guard.sh` | Forbidden-token guard + KNOWN LIMITATIONS block | ✓ VERIFIED | Functional guard works (exit 0 on clean tree). KNOWN LIMITATIONS block present (lines 13-39, 27 lines). Uses term "line-granular". Explicitly names `scripts/ci/test-grep-guard.sh` and the case 7 label. Gap from prior run is closed. |
| `scripts/ci/verify-flags.sh` | jq-based flag assertion over compile_commands.json | ✓ VERIFIED | Present. Uses jq to parse compile_commands.json. Exits 0 on Phase 1 tree. |
| `scripts/ci/test-grep-guard.sh` | 7-case fixture meta-test including case 7 | ✓ VERIFIED | All 7 fixture cases pass. Case 7 present: `run_case "known limitation: mixed long long + long on one line" 0 "src/edge.c" 'long long ll; long n;'`. Final OK message reads "all 7 fixture cases passed, incl. case 7 known-limitation pin." Gap from prior run is closed. |
| `docs/DECISIONS.md` | ADR log with ADR-0001, ADR-0002, ADR-0003 | ✓ VERIFIED | Present. 3 ADR headings. All five sections per ADR. SPU94_NO_SANITIZE_INTEGER macro. Accepted dates 2026-04-18. |
| `LICENSE` | Deferred-pick placeholder | ✓ VERIFIED | Contains "deferred", "MIT", "Apache", "Milestone 1". No "All rights reserved". No copyright line. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/spu94/CMakeLists.txt` | `cmake/spu94_warnings.cmake` | `target_link_libraries(spu94_obj PRIVATE spu94_warnings)` | ✓ WIRED | Flags confirmed in compile_commands.json on actual TU command line. |
| `tests/unit/q15/CMakeLists.txt` | `include/spu94/spu94_q15.h` | `target_link_libraries(test_q15 PRIVATE spu94_static)` + include dirs | ✓ WIRED | ctest passes clean. |
| `CMakeLists.txt` | `src/spu94/CMakeLists.txt` and `tests/CMakeLists.txt` | `add_subdirectory` | ✓ WIRED | Both add_subdirectory calls present. Build confirms. |
| `.github/workflows/ci.yml` | `scripts/ci/grep-guard.sh` | `bash scripts/ci/grep-guard.sh` step in grep-guard job | ✓ WIRED | Both grep-guard.sh and test-grep-guard.sh invoked in grep-guard CI job. |
| `.github/workflows/ci.yml` | `scripts/ci/verify-flags.sh` | `bash scripts/ci/verify-flags.sh build/compile_commands.json` in build-and-test job | ✓ WIRED | Present in ci.yml, runs after cmake configure before cmake build. |
| `docs/DECISIONS.md ADR-0001` | `include/spu94/spu94_q15.h` | Code comment in q15_mul_truncate header block references ADR-0001 | ✓ WIRED | Multiple ADR-0001 references in spu94_q15.h header block and function comments. |
| `scripts/ci/grep-guard.sh KNOWN LIMITATIONS` | `scripts/ci/test-grep-guard.sh case 7` | Explicit textual pointer naming test-grep-guard.sh and the case 7 label | ✓ WIRED | Block names "scripts/ci/test-grep-guard.sh" and the exact label "known limitation: mixed long long + long on one line". New link — closed the gap from prior run. |

### Data-Flow Trace (Level 4)

Not applicable. This phase produces a C library with unit tests, CI scripts, and documentation. No components render dynamic data to a UI or external service. The Q15 arithmetic pipeline correctness is covered by the behavioral spot-checks.

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| libspu94 builds shared + static | `cmake --build build` (artifact check) | `libspu94.so` and `libspu94.a` present | ✓ PASS |
| Determinism flags in compile_commands.json | `grep -q -- '-ffp-contract=off' build/compile_commands.json` (x3) | All three flags present | ✓ PASS |
| Q15 unit tests pass | `ctest --test-dir build -R q15 --output-on-failure` | `1/1 Test #1: q15_unit PASSED 0.00 sec` — exit 0 | ✓ PASS |
| Grep guard exits 0 on clean tree | `bash scripts/ci/grep-guard.sh` | `grep-guard: OK (scanned 3 files).` — exit 0 | ✓ PASS |
| Test-grep-guard all 7 cases pass | `bash scripts/ci/test-grep-guard.sh` | 7 PASS lines including case 7; exit 0 | ✓ PASS |
| CI workflow SHA pins (two-part) | positive + negative grep on ci.yml | Both conditions pass | ✓ PASS |
| CI workflow 5 jobs present | job name check | All 5 present: build-and-test, grep-guard, clang-tidy, cppcheck, ubsan | ✓ PASS |
| UBSan hard-abort configured | `grep -q 'fno-sanitize-recover=undefined' .github/workflows/ci.yml` | Present | ✓ PASS |
| DECISIONS.md has 3 ADRs | `grep -c '^## ADR-' docs/DECISIONS.md` | Returns 3 | ✓ PASS |
| LICENSE is defensively worded | Required/forbidden phrase checks | All pass | ✓ PASS |
| clang-tidy + cppcheck + UBSan CI green | GitHub Actions run | Not yet run — deferred to human verification | ? SKIP |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| CORE-01 | 01-01 | Fixed-point Q15 arithmetic with integer truncation, matching SPU saturation and overflow semantics | ✓ SATISFIED | `q15_mul_truncate`, `sat_s16`, `q15_add_sat` in `include/spu94/spu94_q15.h`. `_Static_assert` for ASR. ctest passes with hand-computed reference table including INT16_MIN^2 edge case. |
| BUILD-01 | 01-01 | CMake-based build producing shared and static library artifacts on Linux | ✓ SATISFIED | `libspu94.so` and `libspu94.a` produced and confirmed present. |
| BUILD-02 | 01-01 | Determinism compiler flags locked in (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) | ✓ SATISFIED | All three flags confirmed in `build/compile_commands.json`. Set PRIVATE via INTERFACE target. |
| BUILD-04 | 01-02 | Static analysis in CI — clang-tidy, cppcheck, compiler warnings as errors | ? HUMAN NEEDED | Both jobs wired in ci.yml with correct configuration. First GitHub Actions run required. |
| BUILD-05 | 01-02 | UndefinedBehaviorSanitizer in CI with surgical no_sanitize annotations | ? HUMAN NEEDED | UBSan job configured with `-fsanitize=undefined -fno-sanitize-recover=undefined`. SPU94_NO_SANITIZE_INTEGER macro pre-authorized in ADR-0003. No annotations needed in Phase 1. First GitHub Actions run required. |
| BUILD-07 | 01-01, 01-02, 01-04 | CI grep guard prohibiting forbidden tokens in core library sources | ✓ SATISFIED | Guard works correctly. KNOWN LIMITATIONS block present. 7-case fixture suite passes including case 7 known-limitation pin. Gap from prior run fully closed by Plan 01-04. |
| DOCS-01 | 01-03 | `DECISIONS.md` begun and maintained — gray-area log | ✓ SATISFIED | `docs/DECISIONS.md` with 3 complete ADRs in Nygard format with added Sources section. |
| DOCS-05 | 01-03 | `LICENSE` placeholder noting the final permissive-license pick is deferred to end of M1 | ✓ SATISFIED | `LICENSE` file with deferred-pick stance, MIT and Apache-2.0 named, no accidental grant or copyright claim. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/spu94/spu94_placeholder.c` | filename | "placeholder" in filename and comment | ℹ️ INFO | Intentional — plan explicitly calls for this as a Phase 2 placeholder TU. Has a real symbol `spu94_internal_version`. Not a stub. |

No blockers. No new anti-patterns introduced by Plan 01-04 (the diff was pure comment + fixture content, with zero changes to pattern logic or production code).

### Human Verification Required

### 1. GitHub Actions first CI run

**Test:** Push the current branch to GitHub and observe the Actions run result for all 5 jobs.
**Expected:** All 5 jobs green. `clang-tidy` job reports zero findings on `src/spu94/` + `include/spu94/` with `--warnings-as-errors='*'`; `cppcheck` job reports zero findings; `ubsan` job builds with clang `-fsanitize=undefined -fno-sanitize-recover=undefined` and `ctest` passes clean (q15_unit 1/1 passed with zero sanitizer reports).
**Why human:** clang, clang-tidy, cppcheck, and ninja are not installed on the build host. The first GitHub Actions run is the real validation point for Success Criterion 3.

### 2. Hand-computed Q15 reference table audit

**Test:** Open `tests/unit/q15/test_q15.c`, read the `mul_cases[]` array, and independently compute at least 5 entries against the ADR-0001 rule: `sat_s16((int32_t)a * (int32_t)b >> 15)`.
**Expected:** All entries match. Confirm in particular: (a) `INT16_MIN * INT16_MIN == INT16_MAX` (saturated from +32768), (b) `-1 * 1 == -1` (not 0 as C division would give), (c) `-100 * 100 == -1` (ASR of -10000 >> 15 rounds toward -inf), (d) `INT16_MIN * INT16_MAX == -32767`.
**Why human:** Per VALIDATION.md Manual-Only Verifications. The test suite passes green, but the table's values are what prove correctness.

### 3. DECISIONS.md prose quality review

**Test:** Read `docs/DECISIONS.md` ADR-0001, ADR-0002, ADR-0003 in full.
**Expected:** (a) Five required sections in order per ADR. (b) Rationale is sound and internally consistent. (c) No GPL source cited as primary. (d) Nocash references are paraphrases, not transcriptions. (e) All external citations use `BIB-NNN` placeholder keys.
**Why human:** Automated checks confirm file structure and keyword presence only. Paraphrase discipline and reasoning quality require human judgment.

### 4. GitHub Actions SHA pin last-mile check

**Test:** Run `gh api repos/actions/checkout/git/refs/tags/v4.2.2 --jq .object.sha` and confirm the result matches `11bd71901bbe5b1630ceea73d27597364c9af683` (the SHA pinned in `.github/workflows/ci.yml`).
**Expected:** The API returns exactly `11bd71901bbe5b1630ceea73d27597364c9af683`.
**Why human:** The two-part automated grep confirms a 40-hex SHA is present and no floating tags exist, but cannot verify the SHA corresponds to the claimed tag without a network call.

### Gap Closure Confirmation

The single gap from the prior verification run is fully closed:

**Gap was:** "The grep guard's line-granular 'long long' subtraction is documented as a known limitation; a fixture case asserts the documented behavior on a same-line 'long long X; long Y;' mix so future regressions surface immediately"

**Now verified:**
- `grep -q 'KNOWN LIMITATIONS' scripts/ci/grep-guard.sh` — PASS (27-line formal block at lines 13-39)
- `grep -q 'line-granular' scripts/ci/grep-guard.sh` — PASS
- `grep -q 'test-grep-guard.sh' scripts/ci/grep-guard.sh` — PASS (explicit fixture pointer)
- `grep -q 'known limitation: mixed long long + long on one line' scripts/ci/test-grep-guard.sh` — PASS
- `grep -q 'long long ll; long n;' scripts/ci/test-grep-guard.sh` — PASS
- `grep -q 'all 7 fixture cases' scripts/ci/test-grep-guard.sh` — PASS
- `bash scripts/ci/grep-guard.sh` — exit 0, "grep-guard: OK (scanned 3 files)."
- `bash scripts/ci/test-grep-guard.sh` — exit 0, all 7 PASS lines including case 7

All 5 phase success criteria pass locally. The 4 human verification items are not new gaps — they are inherently non-automatable checks (CI run, arithmetic audit, prose review, SHA network lookup) that were identified in the initial verification and remain unchanged. Status is `human_needed`, not `gaps_found`.

---

_Verified: 2026-04-18T23:05:00Z_
_Verifier: Claude (gsd-verifier)_
_Re-verification after Plan 01-04 gap closure_
