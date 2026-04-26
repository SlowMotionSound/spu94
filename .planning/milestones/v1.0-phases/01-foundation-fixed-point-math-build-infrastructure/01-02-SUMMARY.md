---
phase: 01-foundation-fixed-point-math-build-infrastructure
plan: 02
subsystem: ci-infrastructure
tags: [github-actions, ci, grep-guard, clang-tidy, cppcheck, ubsan, supply-chain, determinism]

# Dependency graph
requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    plan: 01
    provides: "CMake build producing libspu94.{so,a}, compile_commands.json with determinism flags, q15_unit ctest target, clean src/ + include/ baseline"
provides:
  - ".github/workflows/ci.yml — 5-job GitHub Actions workflow (build-and-test matrix gcc+clang, grep-guard, clang-tidy, cppcheck, ubsan); every third-party action pinned to full commit SHA (T-01-01)"
  - ".clang-tidy — Phase 1 ruleset with WarningsAsErrors='*' and HeaderFilterRegex scoped to include/src spu94 headers (excludes vendored Unity)"
  - "scripts/ci/grep-guard.sh — GNU-grep two-pass forbidden-token guard; scans src/ + include/ *.c *.h; allows 'long long'"
  - "scripts/ci/verify-flags.sh — jq-based BUILD-02 flag assertion over compile_commands.json; catches empty scan via core-TU count"
  - "scripts/ci/test-grep-guard.sh — 6-case positive+negative fixture meta-test of the grep guard itself (T-01-04 second line of defense)"
affects: [phase-02-reverb-state, phase-03-reverb-algorithm, phase-04-fir, phase-06-python-wheel, phase-08-mcu-cross-compile, all-future-commits]

# Tech tracking
tech-stack:
  added:
    - "GitHub Actions (ubuntu-latest runners)"
    - "clang-tidy (CI-only; not a local build dep)"
    - "cppcheck (CI-only)"
    - "UBSan via clang -fsanitize=undefined (CI-only)"
    - "jq (used by verify-flags.sh; available on ubuntu-latest)"
  patterns:
    - "SHA-pinned third-party actions (T-01-01 supply-chain): comment names the tag, SHA is authoritative"
    - "Meta-test for a guard script (fixture-driven positive+negative cases) — the guard that guards the guard"
    - "Determinism-flag drift detector: parse compile_commands.json per core TU and assert each required flag as a standalone token"
    - "Two-pass GNU-grep for 'allow long long, forbid long' — avoids grep -P (BSD-incompatible)"
    - "UBSan with -fno-sanitize-recover=undefined: UB becomes hard abort; no fsanitize-recover escape hatch"

key-files:
  created:
    - ".github/workflows/ci.yml"
    - ".clang-tidy"
    - "scripts/ci/grep-guard.sh"
    - "scripts/ci/verify-flags.sh"
    - "scripts/ci/test-grep-guard.sh"
  modified:
    - "src/spu94/spu94_placeholder.c (banner comment reworded to drop literal forbidden tokens that tripped the new grep-guard — see Deviations)"

key-decisions:
  - "SHA pin chosen: actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683 (v4.2.2, resolved from GitHub refs API at 2026-04-18). Latest v6.0.2 available but v4.2.2 is the conservative stable late-v4 pick; bumps are a security-gated action, not an auto-update."
  - "clang-tidy check list uses explicit Checks string with -* baseline + enabled groups + explicit disables. readability-magic-numbers OMITTED (not subtracted via CheckOptions, which doesn't disable — only tunes). bugprone-easily-swappable-parameters explicitly DISABLED because Q15 helpers take (int16_t, int16_t) by design."
  - "grep-guard is coarse by design: a comment saying 'no float, no double' will trip it. Per RESEARCH Pitfall 4 this is a feature, not a bug — the comment is a hint someone was thinking about those tokens. Plan 01's placeholder banner was therefore reworded; the guard itself was not relaxed."
  - "verify-flags.sh asserts a minimum core-TU count > 0 to catch the 'empty scan' false pass: a regex that matches nothing would otherwise report success with zero TUs inspected."
  - "UBSan job uses Debug-like -g -O1 (per research sketch) rather than -O0 to keep builds fast while preserving useful diagnostics. LDFLAGS also carry -fno-sanitize-recover=undefined so linker-driven instrumentation is consistent with CFLAGS."

patterns-established:
  - "Pattern: SHA-pinned-action — every `uses:` reference uses a full 40-hex commit SHA with a trailing `# vX.Y.Z` comment for human readability. Tag float is impossible."
  - "Pattern: guard-plus-meta-test — any CI-enforcing script ships with a fixture test that covers positive and negative cases; the meta-test runs alongside the guard in CI."
  - "Pattern: jq-over-compile-commands — structured parsing of compile_commands.json with per-TU assertions, not whole-file regex. Scales when multiple TUs land in Phase 2+."
  - "Pattern: hardened UBSan — -fno-sanitize-recover=undefined (not the looser -fsanitize-recover default) is the discipline. Phase 3 adds surgical SPU94_NO_SANITIZE_INTEGER per ADR-0003; never a blanket disable."

requirements-completed: [BUILD-04, BUILD-05, BUILD-07]

# Metrics
duration: ~4min
completed: 2026-04-18
---

# Phase 01 Plan 02: CI Workflow + Grep Guard + Flag Verification Summary

**CI is now the enforcement layer for Phase 1 success criteria 3 and 4: a 5-job GitHub Actions workflow (matrix build gcc+clang, grep-guard, clang-tidy, cppcheck, UBSan-hard-abort) gates every future commit, with a positive+negative fixture meta-test guarding the grep guard itself and a jq-based flag-drift detector over `compile_commands.json`. Every third-party action is pinned to a full 40-hex commit SHA per T-01-01.**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-04-18T20:32:40Z
- **Completed:** 2026-04-18T20:36:17Z
- **Tasks:** 2
- **Files created:** 5 (3 scripts + .clang-tidy + ci.yml)
- **Files modified:** 1 (placeholder banner comment)

## Accomplishments

- `scripts/ci/grep-guard.sh` exits 0 on Plan 01's (post-comment-fix) tree, scanning 3 files (`src/spu94/spu94_placeholder.c`, `include/spu94/spu94.h`, `include/spu94/spu94_q15.h`).
- `scripts/ci/verify-flags.sh` inspects the single Phase 1 core TU (`spu94_placeholder.c`) and confirms all three determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) are present as standalone tokens.
- `scripts/ci/test-grep-guard.sh` runs 6 fixture cases and passes them all: clean tree, `float` in src, `malloc` in include, `long long` allowed, unqualified `long` forbidden, empty tree.
- `.clang-tidy` carries `WarningsAsErrors: '*'` with a conservative Phase 1 check set and a `HeaderFilterRegex` that excludes vendored Unity headers from analysis.
- `.github/workflows/ci.yml` defines exactly 5 jobs (`build-and-test`, `grep-guard`, `clang-tidy`, `cppcheck`, `ubsan`); the build job runs as a gcc+clang matrix; the UBSan job runs with `-fsanitize=undefined -fno-sanitize-recover=undefined` so UB is an immediate abort; every `uses:` line references a full 40-hex commit SHA.
- Plan-level verification block (all 7 checks) passes on this worktree.

## Task Commits

Each task was committed atomically (parallel-wave protocol, --no-verify):

1. **Task 1: grep-guard + verify-flags + fixture meta-test** — `7300c66` (feat) — 4 files changed, 183 insertions (3 scripts + placeholder comment reword).
2. **Task 2: .clang-tidy + GitHub Actions CI workflow** — `8ab2289` (feat) — 2 files changed, 162 insertions.

_Plan metadata commit (SUMMARY.md) is owned by the orchestrator per parallel-wave protocol._

## Exact Third-Party Action SHAs (T-01-01 audit record)

| Action | Tag | Pinned SHA | Resolved via |
|---|---|---|---|
| `actions/checkout` | `v4.2.2` | `11bd71901bbe5b1630ceea73d27597364c9af683` | `curl https://api.github.com/repos/actions/checkout/git/refs/tags/v4.2.2` at 2026-04-18; `object.type == "commit"` (direct, no tag-object deref needed) |

v4.1.7 (`692973e3d937129bcbf40652eb9f2f61becf3332`) was also resolved and rejected in favor of v4.2.2 as a later stable v4.x release. v6.0.2 is the current latest release but has not been audited for this project — v4.2.2 is the conservative pick. Future bumps will add new rows to this table, not edit existing ones.

`actions/setup-python` was referenced in the plan template but is not used by this workflow (no Python in Phase 1's CI surface) — it was dropped.

## Ubuntu-latest Package Versions Resolved at CI Time

Not yet observed (no CI run has been pushed from this worktree — parallel-wave protocol means the orchestrator merges and the first CI run is the merged-branch run). The `apt-get install -y` steps in the workflow resolve whatever `ubuntu-latest` (currently Ubuntu 24.04 as of 2026-04-18) ships for `cmake`, `ninja-build`, `clang-tidy`, `cppcheck`, `clang`, `jq`. Pinning these to specific versions is out of scope for Phase 1 and is a future consideration if reproducibility bites.

## Findings from Local Dry-Run

**Tools available on build host:** `cmake` (3.31.6), `gcc` (15.2.0), `jq` (1.8.1), `python3`. **Tools missing:** `clang`, `clang-tidy`, `cppcheck`, `ninja`.

**Dry-runs executed locally:**
- `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` → configure clean (no warnings)
- `cmake --build build` → `libspu94.so`, `libspu94.a`, `test_q15` built with `-Werror`; zero warnings
- `ctest --test-dir build --output-on-failure` → `q15_unit` 1/1 PASSED
- `bash scripts/ci/grep-guard.sh` → `OK (scanned 3 files)`
- `bash scripts/ci/verify-flags.sh` → `OK (inspected 1 core TUs, all flags present)`
- `bash scripts/ci/test-grep-guard.sh` → `OK (all fixture cases passed)` (6/6)
- YAML parseable by `python3 -c yaml.safe_load(...)`; 5 expected jobs present.
- `grep -E '^\s*uses:\s+\S+@v[0-9]' .github/workflows/ci.yml` → no matches (no floating-tag pins leaked).

**Dry-runs deferred to CI (unavailable locally):**
- `clang-tidy -p build --warnings-as-errors='*'` over `src/spu94` + `include/spu94`
- `cppcheck --project=build/compile_commands.json --enable=warning,performance,portability --error-exitcode=1`
- UBSan build + ctest under `clang` with `-fsanitize=undefined -fno-sanitize-recover=undefined`

All three are expected to be clean on Plan 01's minimal surface (a placeholder TU that only declares `const int32_t spu94_internal_version = 0;`, a Q15 header with three `static inline` helpers and a `_Static_assert` guard). If any of them fire on the first GitHub Actions run, findings will be addressed in a follow-up commit on this plan's branch (or a hot-fix plan if the finding is architectural).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Reworded `src/spu94/spu94_placeholder.c` banner comment to drop literal forbidden tokens**

- **Found during:** Task 1 `<verify>` (running `bash scripts/ci/grep-guard.sh` on the checked-in tree).
- **Issue:** Plan 01's placeholder comment read `Uses only int16_t/int32_t/void — no float, no double, no malloc, no unqualified long (BUILD-07 compliance).` The grep guard is a pure text-match pattern (it must be, per RESEARCH Pitfall 5: no `grep -P`), so the literal words `float`, `double`, `malloc`, and `long` in that comment tripped it on exit 1, even though the code itself is clean. RESEARCH Pitfall 4 explicitly calls this out as "a feature, not a bug — the comment is a hint someone was thinking about those tokens." Plan 01 claimed the tree was grep-guard-clean, but it was not.
- **Fix:** Replaced the enumerated-forbidden-token comment with a reference to the guard script itself: `Uses only int16_t / int32_t / void — no forbidden tokens per BUILD-07. (See scripts/ci/grep-guard.sh and Plan 01-02 for the forbidden-token list.)` Same semantic (self-documents BUILD-07 compliance); no banned tokens appear literally.
- **Files modified:** `src/spu94/spu94_placeholder.c` (comment only; the `const int32_t spu94_internal_version = 0;` declaration is untouched).
- **Commit:** `7300c66` (bundled with Task 1, since without the fix Task 1's `<verify>` block would fail and block Task 1 entirely).
- **Precedence decision:** Rule 1 (auto-fix bug) applies because the discrepancy prevents completing the current task (Task 1 `<verify>` requires grep-guard exit 0) and was caused by prior work being inconsistent with the new guard, not by a structural design question. Rule 4 (architectural ask) does NOT apply — no API, schema, or interface changed.

**2. [Plan refinement - Language] `readability-magic-numbers` OMITTED from `.clang-tidy` rather than "disabled via CheckOptions"**

- **Found during:** Task 2 authoring of `.clang-tidy`.
- **Issue:** The plan template said to "list `readability-magic-numbers` in Checks then subtract under CheckOptions — this is idiomatic." That's not actually how clang-tidy works: `CheckOptions` tunes a check's *configuration* (thresholds, allowed values), it does not disable a check. The idiomatic disable is either omission from `Checks:` or prefixing with `-`.
- **Fix:** Simply omitted `readability-magic-numbers` from the `Checks:` list. Same intent (magic-numbers not enforced in Phase 1); the `CheckOptions` block was used only to tune `readability-function-size` thresholds, which is its correct use.
- **Files modified:** `.clang-tidy` (authored directly this way in Task 2; never had the incorrect form).
- **Commit:** `8ab2289` (Task 2).
- **Precedence decision:** Rule 3 (blocking-issue-style clarification in plan language). No architectural question.

**3. [Plan refinement - Dependency] `actions/setup-python` reference removed from `ci.yml`**

- **Found during:** Task 2 authoring.
- **Issue:** The plan template mentioned "SHAs for the current releases of `actions/checkout` and `actions/setup-python`" but Phase 1's CI has no Python — neither ctypes bindings nor pytest are wired yet (Phase 6 introduces them). A `uses: actions/setup-python@SHA` would be dead code that needs maintenance.
- **Fix:** `setup-python` not referenced. Only `actions/checkout` is pinned.
- **Files modified:** `.github/workflows/ci.yml` (authored without setup-python; never had it).
- **Commit:** `8ab2289`.
- **Precedence decision:** Rule 3. Future Python phase (Phase 6) will add `setup-python` with its own SHA pin; pinning it now would be premature.

### Auth gates

None.

### Out-of-scope items deferred

None encountered. The only scope question was the `spu94_placeholder.c` comment, and per deviation-rules SCOPE BOUNDARY that file was directly in scope (Task 1's verify block REQUIRES the tree to be grep-guard-clean, and Plan 01 claims it already is).

## Issues Encountered

**Host tooling gap:** `clang`, `clang-tidy`, `cppcheck`, and `ninja` are not installed on this build host. Local dry-run was therefore limited to the `gcc`-only build + ctest + grep-guard + verify-flags + fixture test subset. The clang-tidy, cppcheck, UBSan, and clang-in-matrix jobs will be exercised for the first time when the workflow runs on GitHub Actions. All four are expected to be clean on Plan 01's minimal surface; failures would be addressed in follow-up commits on this plan's branch.

**No CI run URL available:** This worktree is a parallel executor; the orchestrator merges to master. The first CI run is the post-merge run and will be recorded in STATE.md by the orchestrator.

## Next Phase Readiness

- Phase 1 success criteria 3 and 4 are now CI-enforced, not just code-honored. From the next push onward, removing `-ffp-contract=off` / `-fno-fast-math` / `-Werror` from a core TU, or reintroducing `float`/`double`/`malloc`/`calloc`/`realloc`/`free`/unqualified `long` into `src/` or `include/`, is a build-breaker.
- T-01-01 mitigation is in place and documented: every current third-party action is SHA-pinned with its tag commented. When bumping, append a row to the audit record in this SUMMARY.
- Phase 2 (reverb state + APIs) can land new core TUs without ceremony — `verify-flags.sh` automatically asserts every new `src/spu94/*.c` has the determinism flags, and `grep-guard.sh` automatically scans every new `.c` / `.h` under `src/` and `include/`.
- Phase 3 is pre-authorized to adopt `SPU94_NO_SANITIZE_INTEGER` from ADR-0003 on specific functions that model intentional SPU wrap/saturation. The UBSan CI job is configured to hard-abort on any UB, so those annotations will be load-bearing in Phase 3.
- One implicit follow-up for a future plan (not this one): if macOS ever becomes a supported dev platform (CONTEXT.md currently says Linux-only for M1), `grep-guard.sh` needs a BSD-grep alternate path. Tracked by RESEARCH.md Pitfall 5; not blocking today.

## Verification Evidence (Plan-Level, 7/7)

```
=== 1. grep-guard on clean tree + fixtures ===
grep-guard: OK (scanned 3 files).
PASS: clean tree (expected exit 0, got 0)
PASS: float in src (expected exit 1, got 1)
PASS: malloc in include (expected exit 1, got 1)
PASS: long long allowed (expected exit 0, got 0)
PASS: unqualified long forbidden (expected exit 1, got 1)
PASS: empty tree (exit 0)
test-grep-guard: OK (all fixture cases passed).

=== 2. Flag verification post-configure ===
verify-flags: OK (inspected 1 core TUs, all flags present).

=== 3. CI workflow YAML valid ===
jobs: ['build-and-test', 'grep-guard', 'clang-tidy', 'cppcheck', 'ubsan']
(all 5 required jobs present)

=== 4. Actions pinned to SHAs ===
no floating version tags
(grep -E '^\s*uses:\s+\S+@v[0-9]' returned no matches)

=== 5. UBSan hard-abort ===
'fno-sanitize-recover=undefined' present in ci.yml

=== 6. clang-tidy HeaderFilterRegex scoped ===
HeaderFilterRegex: '^(include|src)/spu94/.*\.h$'

=== 7. Local dry-run build+ctest ===
[100%] Built target test_q15
100% tests passed, 0 tests failed out of 1
```

## Threat Flags

None. This plan's `<threat_model>` enumerated T-01-01 (supply chain), T-01-02 (UB masked), T-01-03 (flag drift), T-01-04 (token reintroduction); all four are mitigated as specified:

- **T-01-01:** `actions/checkout` pinned to full SHA `11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2); comment names the tag for humans, SHA is what CI resolves. No other third-party actions in the workflow.
- **T-01-02:** `ubsan` job sets `CFLAGS` and `LDFLAGS` to include `-fno-sanitize-recover=undefined`; no `-fsanitize-recover` escape hatch anywhere in the workflow.
- **T-01-03:** `verify-flags.sh` parses `compile_commands.json` per-TU and asserts all three required flags are present on every `src/spu94/*.c` entry; also asserts `core_count > 0` to catch empty-scan false passes.
- **T-01-04:** `grep-guard.sh` implements the two-pass GNU-grep pattern from RESEARCH; `test-grep-guard.sh` proves via fixtures that (a) clean sources pass, (b) `float` fails, (c) `malloc` in include fails, (d) `long long` is allowed, (e) unqualified `long` fails, (f) empty tree passes.

No new threat surface introduced beyond the model's enumeration.

## Known Stubs

None introduced by this plan. The workflow's `clang-tidy`, `cppcheck`, and `ubsan` jobs are fully wired; there are no "TODO: enable later" steps. `.clang-tidy` intentionally disables `readability-magic-numbers` and `bugprone-easily-swappable-parameters` with documented rationale (not stubs — deliberate Phase 1 scoping).

## Self-Check: PASSED

- [x] `scripts/ci/grep-guard.sh` — FOUND (executable, 1931 bytes)
- [x] `scripts/ci/verify-flags.sh` — FOUND (executable, 2146 bytes)
- [x] `scripts/ci/test-grep-guard.sh` — FOUND (executable, 2337 bytes)
- [x] `.clang-tidy` — FOUND (WarningsAsErrors: '*' present)
- [x] `.github/workflows/ci.yml` — FOUND (5 jobs, YAML parses, all `uses:` lines SHA-pinned)
- [x] `src/spu94/spu94_placeholder.c` — still contains `spu94_internal_version` symbol (unchanged); only banner comment was reworded
- [x] Commit `7300c66` — FOUND in `git log` (Task 1: scripts + placeholder comment rewording)
- [x] Commit `8ab2289` — FOUND in `git log` (Task 2: .clang-tidy + ci.yml)
- [x] Local dry-run of all available CI steps (configure, build, ctest, grep-guard, verify-flags, test-grep-guard) — all exit 0
- [x] No floating-tag action references in ci.yml (`grep -E '^\s*uses:\s+\S+@v[0-9]'` returns empty)

---
*Phase: 01-foundation-fixed-point-math-build-infrastructure*
*Completed: 2026-04-18*
