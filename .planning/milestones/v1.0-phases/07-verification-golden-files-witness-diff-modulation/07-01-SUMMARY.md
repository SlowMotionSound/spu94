---
phase: 07-verification-golden-files-witness-diff-modulation
plan: 01
subsystem: verification-foundation
tags: [coverage, ci, verification, test-infrastructure, threat-mitigation]
requires:
  - phase-5-public-api
  - phase-6-python-binding
  - existing-ctest-registry-66-tests
provides:
  - docs-coverage-md-three-section-map
  - ci-enforced-coverage-validator
  - coverage-map-check-ci-job
  - t-07-01-a-mitigation-at-source
affects:
  - future-phase-07-plans-append-rows-to-coverage-md
  - docs-decisions-md-d-01-through-d-04-landed
tech-stack:
  added:
    - lilv-utils
    - lv2-dev
    - python3-scipy
    - python3-pytest-benchmark
    - strace
  patterns:
    - positive-allowlist-regex-over-metacharacter-blocklist
    - single-path-ctest-dispatch-for-c-python-shell-tests
    - fast-path-skip-ctest-in-negative-meta-tests
    - minimal-fixture-covering-only-bad-row-for-fake-ctest-name
key-files:
  created:
    - scripts/ci/install-phase7-deps.sh
    - scripts/ci/check_coverage.py
    - scripts/ci/test_check_coverage.py
    - docs/COVERAGE.md
    - tests/conformance/test_coverage_map_integrity.py
    - tests/conformance/CMakeLists.txt
  modified:
    - tests/CMakeLists.txt
    - .github/workflows/ci.yml
decisions:
  - "D-02 validator treats every backticked cell containing '::' as a coverage reference (not just tests/-prefixed ones) so metacharacter injection and malformed shapes fail loudly instead of being silently dropped"
  - "Single-path ctest dispatch: .c, .sh, and .py test files all run via ctest -R <name> rather than branching on extension — matches how the project already registers python fuzz harnesses and shell-based rt-safety tests"
  - "T-07-01-A mitigation uses a positive allowlist regex [A-Za-z0-9_] on ctest names rather than a blocklist of shell metacharacters — harder to bypass with unicode tricks or future metacharacter additions"
  - "--skip-ctest flag added to check_coverage.py so negative meta-tests (missing-file / empty-cell / metacharacter-injection) exercise the rejection path without the 14-minute full-suite ctest cost"
  - "Test 3 (fake ctest name) uses a minimal 1-row COVERAGE.md fixture rather than injecting into a copy of the committed file, so it runs in 0.07s instead of 2m47s"
  - "COVERAGE.md per-register rows cite the narrowest existing ctest per register (e.g., test_reverb_comb.c::reverb_comb for the four vCOMB* gains and four mL/RCOMB* addresses); fuzz_process (ctest #50) remains the wide-net 10^6-step backstop documented in the section prologue"
  - "RT-safety row points at tests/rt_safety/test_no_syscalls.sh (the wrapper shell script) rather than the .c TU it compiles — ctest registers the shell script via rt_no_syscalls and that is the single-path dispatch we want"
metrics:
  duration_minutes: ~180 (includes one 14-min full-suite ctest run from an earlier meta-test iteration)
  tasks_completed: 3
  files_created: 6
  files_modified: 2
  net_lines_added: ~940
  ctest_targets_added: 2 (coverage_map_integrity #67, test_check_coverage #68)
  ci_jobs_added: 1 (coverage-map-check)
  coverage_md_rows: 77 test: references / 35 unique (path, name) pairs
  meta_tests_added: 5 pytest + parametrized (~30 logical cases counting per-register parametrization)
completed: 2026-04-23
---

# Phase 07 Plan 01: Foundation — Toolchain, COVERAGE.md, Validator Summary

One-liner: shipped Phase 7 host toolchain installer + the three-section
`docs/COVERAGE.md` spec-conformance map + a `scripts/ci/check_coverage.py`
validator with shell-injection mitigation + two ctest-registered meta-test
suites + a new `coverage-map-check` GitHub CI job — every remaining Phase 7
plan now has a forcing function for "new behavior implies new row implies
new passing test."

## What Landed

### Task 1 — Phase 7 host toolchain install script

`scripts/ci/install-phase7-deps.sh` (80 lines, 5 verify checks, idempotent
via `apt-get install -y`). Installs `lilv-utils` + `lv2-dev` (witness-diff
needs `lv2apply` + LV2 headers to compile lv2-psx-reverb fresh each CI run),
`python3-scipy` (band-split + chirp generation + alignment), `python3-
pytest-benchmark` (Plan 07-06 timing harness), `strace` (Plan 07-06 hot-
path alloc gate), `coreutils` (sha256sum), `git` (witness clone).

Post-install verification lines: `command -v lv2apply`, scipy import +
version print, pytest_benchmark import + version print, `command -v strace`,
`command -v sha256sum`. Each failure prints `FAIL: <tool> not available
after install` to stderr and exits 1. Final success line: `PASS: Phase 7
host toolchain installed`.

Header comment documents the pip fallback (`pip install --user
scipy>=1.11 pytest-benchmark>=5.2`) for apt package-name drift on future
distros; this fallback was not needed on the current Debian/Ubuntu host.

**Human-verified end-to-end** on dev workstation prior to commit
(`bash scripts/ci/install-phase7-deps.sh` → PASS observed).

### Task 2 — `docs/COVERAGE.md` three-section map

`docs/COVERAGE.md` (137 lines, 77 backticked test refs, 35 unique (path,
name) pairs — zero missing files, zero missing ctest names on the dev
build at commit time).

Structure (D-01):
- **Pinned wayback URL** at the top (D-04): `web.archive.org/web/
  20260114082525/...`.
- **Per-Register Coverage** — all 35 `spu94_reg_t` entries in enum order
  (vLOUT → vRIN), each citing the narrowest ctest that exercises it.
  Section prologue names `fuzz_process` (ctest #50) as the 10⁶-step
  wide-net backstop.
- **Per-Behavior Coverage** — ~36 rows covering SAME/DIFF IIR, 4-tap comb,
  APF1/2, input/output scale, hard clip, reverb body composition,
  BufferAddress wrap, mBASE snap-on-write, 39-tap FIR
  decimate/interpolate/bit-identity/coef-table/chain-latency/round-trip,
  split write-timing policy, Q15 truncation, register I/O types + facade
  + identity + roundtrip, preset table integrity + load + tail, process
  block-size + in-place + flush, four 10⁶-step fuzz harnesses, RT-safety
  syscall gate.
- **Per-Spec-Paragraph Coverage** — 6 rows, one per psx-spx anchor used
  so far (`#spureverbregisters`, `#reverb-processing`, `#reverb-buffer`,
  `#reverb-buffer-resampling`, `#spu-fixed-point`, `#reverb-examples`).
- **Known Gaps** — 4 empty-test: rows for later plans (07-03 goldens,
  07-04 witness, 07-05 modulation, 07-06 benchmark).

Audit methodology: grepped all `RUN_TEST` / `void test_` functions in
`tests/unit/**/*.c`, then mapped ctest registration names (via
`ctest -N`). Where the plan's scaffold suggested a test name that doesn't
exist (e.g., `test_roundtrip_vLOUT`, `test_same_iir_basic`), substituted
the nearest existing ctest (e.g., `register_roundtrip`, `reverb_same_iir`)
per the plan's fallback instruction. No test names were invented.

### Task 3 — Validator + meta-tests + conformance integrity + CI job

**`scripts/ci/check_coverage.py`** (303 lines, `#!/usr/bin/env python3`
shebang, executable). Parses COVERAGE.md; for each unique (path, name)
pair verifies file existence + metacharacter allowlist + ctest -R pass.
`--file <path>` for meta-tests; `--skip-ctest` for fast negative-path
exercises. Single-path dispatch via `ctest` for all source types
(matches project convention where .py fuzz harnesses and .sh rt-safety
tests are already ctest-registered).

**`scripts/ci/test_check_coverage.py`** (202 lines, 5 meta-tests):
1. `test_positive_committed_coverage_md_is_green` — full ctest run,
   final stdout `PASS: 77 COVERAGE.md rows all green`. (~14 min)
2. `test_negative_missing_file` — injected bogus path, uses
   `--skip-ctest` for speed; stderr contains `file not found`. (<1s)
3. `test_negative_missing_ctest_name` — minimal 1-row fixture with a
   real path + unregistered ctest name; full ctest dispatch; stderr
   contains `not found` / `returned non-zero`. (<1s)
4. `test_negative_empty_test_field_outside_known_gaps` — empty
   backtick pair under Per-Behavior Coverage; stderr contains
   `empty test:`. (<1s)
5. `test_negative_shell_metacharacter_injection_rejected` — canonical
   T-07-01-A vector `q15_unit; rm -rf /tmp/xyz`; stderr contains
   `metacharacter rejected`. (<1s)

**`tests/conformance/test_coverage_map_integrity.py`** (186 lines, ~9
test functions + 35 parametrized register-presence cases). Structural
gate: three section headers each appear exactly once, pinned wayback
URL verbatim, ≥35 per-register rows, every non-Known-Gaps coverage row
has a non-empty `<path>::<name>` cell, all 12 v + 17 m + 6 d register
names present in the Per-Register section.

**`tests/conformance/CMakeLists.txt`** registers two ctest targets:
`coverage_map_integrity` (#67) and `test_check_coverage` (#68). Both
run under `WORKING_DIRECTORY=${CMAKE_SOURCE_DIR}` so repo-relative paths
resolve when ctest invokes from `build/`.

**`tests/CMakeLists.txt`** — appended `add_subdirectory(conformance)`.

**`.github/workflows/ci.yml`** — new `coverage-map-check` job after
`ubsan`. Reuses `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683`
(v4.2.2, SHA-pinned per Phase 1). Installs Phase 7 deps + cmake +
ninja-build, configures + builds, runs `check_coverage.py`, runs both
meta-test suites.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `_TEST_REF_RE` silently dropped malformed test refs**

- **Found during:** Task 3 meta-test run (`test_negative_shell_metacharacter_injection_rejected` observed validator exit 0 instead of 1).
- **Issue:** The original parser's `_TEST_REF_RE` regex required both the path and the test name to contain only `[^\s\`]+`. Cells with shell metacharacters (spaces in the injected `q15_unit; rm -rf /tmp/xyz`) failed the match and were silently skipped, so the validator never saw them and never invoked the metacharacter allowlist. Same silent-drop for `scripts/ci/check_coverage.py::nonexistent_xyz` (path didn't start with `tests/` so the regex rejected it).
- **Fix:** Parser rewritten to flag any backticked cell containing `::` as a coverage reference. Well-shaped cells go through the normal (path, name) pipeline; malformed cells are split on the first `::` and handed to the same validation pipeline so file-existence + metacharacter-allowlist + ctest-dispatch all fire against them.
- **Files modified:** `scripts/ci/check_coverage.py` (parse loop).
- **Commit:** `fbd9a98`.

**2. [Rule 3 - Blocking] `test_negative_missing_ctest_name` took 2m47s on real COVERAGE.md**

- **Found during:** First full meta-test run (total pytest runtime was 13m55s; test 3 alone was ~2m47s).
- **Issue:** The plan's default approach was to `_tmp_coverage_copy` the full committed COVERAGE.md and inject a bad row. But the validator then ran ctest for all 77 OTHER (valid) rows before hitting the injected bad one — turning a 1-row integrity check into a full-suite invocation.
- **Fix:** Rewrote test 3 to use a minimal 1-row COVERAGE.md fixture (one Per-Register row + required section headers + pinned URL). Test now runs in 0.07s. This also keeps CI runtime reasonable — the full-suite cost only lands once, in the positive test.
- **Files modified:** `scripts/ci/test_check_coverage.py`.
- **Commit:** `fbd9a98`.

**3. [Rule 2 - Critical] Fast-path negative tests needed `--skip-ctest`**

- **Found during:** Same meta-test run as Deviation 2.
- **Issue:** Negative meta-tests for missing file / empty cell / metacharacter injection all fire the relevant validator branch BEFORE ctest dispatch. Running them against the full committed COVERAGE.md still triggered the 77-row ctest run for all the other (valid) rows, wasting ~14 minutes per test iteration.
- **Fix:** Added `--skip-ctest` argparse flag to `check_coverage.py` (validates parsing + file-existence + metacharacter allowlist only; skips dispatch). Meta-tests 2, 4, 5 pass `--skip-ctest` and run in <0.2s total. Test 1 (positive) and test 3 (missing ctest name) still exercise the full ctest dispatch path.
- **Files modified:** `scripts/ci/check_coverage.py` (add arg), `scripts/ci/test_check_coverage.py` (pass arg).
- **Commit:** `fbd9a98`.

**4. [Rule 1 - Bug] RT-safety row pointed at nonexistent `.py` file**

- **Found during:** Task 2 integrity cross-check (first pass of the
  path-existence verify).
- **Issue:** The COVERAGE.md scaffold I wrote cited
  `tests/rt_safety/test_no_syscalls.py` but the actual project file is
  `test_no_syscalls.c` + `test_no_syscalls.sh` (ctest wraps them as
  `rt_no_syscalls`).
- **Fix:** Changed to `tests/rt_safety/test_no_syscalls.sh::rt_no_syscalls`
  — the `.sh` wrapper is what ctest executes and `rt_no_syscalls` is the
  registered name.
- **Files modified:** `docs/COVERAGE.md`.
- **Commit:** `db9c45c` (Task 2 commit, fix landed pre-commit).

### No Rule 4 Escalations

No architectural changes were needed. All deviations stayed within
Rule 1 / Rule 2 / Rule 3 scope.

## Authentication Gates

**One gate consumed at the start of this agent's run, before this
summary was written:** Task 1's `sudo apt-get install` needed the user
to enter a sudo password. The prior executor stopped at that gate and
returned a checkpoint; the user resolved it manually by running
`bash scripts/ci/install-phase7-deps.sh` on the dev host and confirming
`PASS: Phase 7 host toolchain installed`. This continuation agent then
committed Task 1's script and proceeded. Documented here as normal flow
(the gate was a one-time CI-environment provisioning step; not a bug or
surprise).

## Key Behaviors and Invariants Locked In

1. **Every populated `test:` cell in COVERAGE.md names an existing
   ctest target.** Proven at commit time by
   `check_coverage.py --skip-ctest` (77/77 file paths exist) + full
   `check_coverage.py` run (77/77 ctest targets pass).
2. **Empty `test:` cells are allowed only inside `## Known Gaps`.**
   Validator emits `FAIL: empty test: field outside Known Gaps` on
   violation; conformance integrity test catches structural regression.
3. **Shell-metacharacter injection in test names is rejected at parse
   time.** Positive allowlist `[A-Za-z0-9_]` on the ctest-name portion;
   `subprocess.run([...], shell=False)` everywhere a test is invoked.
   T-07-01-A mitigation is source-level, not runtime-level.
4. **CI gate is a dedicated job, not a step in an existing job.**
   `coverage-map-check` runs on every push and pull-request and fails
   the build on any gap. SHA-pinned checkout action preserved.
5. **ctest registration names are the single dispatch path for .c,
   .sh, and .py tests.** Simplifies the validator, matches how the
   project already registers its python fuzz harnesses and shell-based
   rt-safety tests, and keeps the T-07-01-A attack surface single-path.

## Known Stubs

None. All rows reference real tests; all infrastructure is wired.

## Threat Flags

None. The `coverage-map-check` CI job introduces no new external-input
surface (reads only committed `docs/COVERAGE.md`); the existing
`actions/checkout` SHA-pin discipline is preserved. T-07-01-A from the
plan's threat register is mitigated as specified.

## Deferred Issues

None. All plan requirements satisfied:

- [x] Host toolchain installed; install script idempotent and committed
  (commit `4a042e2`).
- [x] `docs/COVERAGE.md` exists with three sections citing the pinned
  wayback URL (commit `db9c45c`).
- [x] All 35 registers appear in per-register section in `spu94_reg_t`
  enum order.
- [x] `check_coverage.py` validator ships + passes on committed
  COVERAGE.md (77 rows green).
- [x] 4 meta-tests (1 positive + 3 negative per plan spec; 5 total
  including the extra shell-metacharacter test) all pass.
- [x] `coverage-map-check` CI job added with SHA-pinned checkout
  action.
- [x] Shell-injection threat T-07-01-A mitigated at source level
  (grep hits: `shell=False` x2, metacharacter/allowlist refs x14).

## Commits

| # | Hash      | Type | Message                                                                                     |
|---|-----------|------|---------------------------------------------------------------------------------------------|
| 1 | `4a042e2` | feat | feat(07-01): phase 7 host toolchain install script                                          |
| 2 | `db9c45c` | docs | docs(07-01): add COVERAGE.md three-section spec conformance map                             |
| 3 | `5cb5136` | test | test(07-01): add failing meta-tests for check_coverage.py (RED)                             |
| 4 | `fbd9a98` | feat | feat(07-01): ship check_coverage.py + conformance meta-tests + CI job (GREEN)               |

## Self-Check

- [x] `scripts/ci/install-phase7-deps.sh` exists — FOUND.
- [x] `scripts/ci/check_coverage.py` exists and executable — FOUND.
- [x] `scripts/ci/test_check_coverage.py` exists — FOUND.
- [x] `docs/COVERAGE.md` exists — FOUND.
- [x] `tests/conformance/test_coverage_map_integrity.py` exists — FOUND.
- [x] `tests/conformance/CMakeLists.txt` exists — FOUND.
- [x] Commit `4a042e2` in git log — FOUND.
- [x] Commit `db9c45c` in git log — FOUND.
- [x] Commit `5cb5136` in git log — FOUND.
- [x] Commit `fbd9a98` in git log — FOUND.

## Self-Check: PASSED
