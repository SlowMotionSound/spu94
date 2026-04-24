---
status: issues_found
phase: 07-verification-golden-files-witness-diff-modulation
depth: standard
reviewed: 2026-04-23
files_reviewed: 38
findings:
  critical: 0
  warning: 10
  info: 8
  total: 18
---

# Phase 7 Code Review

**Depth:** standard
**Files Reviewed:** 38
**Status:** issues_found — 0 critical, 10 warning, 8 info

## Summary

Phase 7's verification infrastructure is generally careful and well-commented. The CI gates have good separation-of-concerns, threat mitigations (T-07-01-A metacharacter allowlist, T-07-03-A supply-chain pin verification) are in the right places, and the positive/negative meta-test pattern (hotpath alloc gate, coverage validator, bibliography checker) is consistent.

No Critical issues found. The main patterns to address are:
1. Determinism env (`LC_ALL` / `TZ` / `SOURCE_DATE_EPOCH`) is inconsistently plumbed across subprocess invocations.
2. ctest output string matching is fragile to locale.
3. A couple of meta-test file-discovery patterns can match ambiguous build artifacts.
4. Pin/constant duplication across test + harness + build script without a single source of truth.

## Warnings

### WR-01: witness_diff.py does not pin determinism env when shelling to SPU-94 CLI
**File:** `scripts/ci/witness_diff.py:534-545`
`invoke_spu94()` runs `subprocess.run([...])` with no `env=` argument — inherits parent shell's `LC_ALL` / `TZ` / `SOURCE_DATE_EPOCH`. `scripts/regenerate_goldens.py:125-136` deliberately pins these. Cross-host drift risk.

**Fix:** Build env dict pinning `LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH=1704067200` before the subprocess call.

### WR-02: check_coverage.py ctest "no tests were found" matcher is locale-fragile
**File:** `scripts/ci/check_coverage.py:195`
String-matches `"No tests were found"` / `"No tests to run"` — English ctest prose. Under non-English locale, matcher becomes a false-pass.

**Fix:** Pin `LC_ALL=C`, `LANG=C` in `run_ctest()` or use `--show-only=json-v1`.

### WR-03: test_hotpath_alloc_gate_meta._find() can pick wrong binary from stale build dirs
**File:** `tests/python/test_hotpath_alloc_gate_meta.py:30-40`
`rglob("build*/**/{name}")` matches `build/`, `build-ubsan/`, `build-clang-*/`, etc. Sort order is lexicographic — a `build-release/` tree can shadow `build/` silently.

**Fix:** Prefer explicit `build/`, fail loudly on ambiguity.

### WR-04: write_levers_catalog.py declares main(argv) but never parses argv
**File:** `scripts/write_levers_catalog.py:100-121`
`main(argv)` takes the parameter but body ignores it — `--help`, `--dry-run`, typos all silently accepted.

**Fix:** Either drop the parameter or wire through argparse.

### WR-05: hotpath_alloc_gate.sh misdiagnoses strace failures
**File:** `tests/rt_safety/hotpath_alloc_gate.sh:41-46`
`strace ... "$SYSCALLS_BIN" || echo "FAIL: target binary exited non-zero"` conflates (a) target exits non-zero, (b) strace fails (missing, YAMA-blocked, no `CAP_SYS_PTRACE`), (c) strace can't write log. Containerized CI hits cases b/c with misleading message.

**Fix:** Branch on empty vs non-empty log to distinguish strace-failure from target-failure.

### WR-06: test_regenerate_goldens sparse env can mask locale-sensitive bugs
**File:** `scripts/test_regenerate_goldens.py:84-90`
Env dict drops `HOME`, `USER`, `TMPDIR`, `LD_LIBRARY_PATH`. A missing runtime library for spu94 CLI would surface as "wav mismatch" instead of link error.

**Fix:** Start from `{**os.environ, ...}` and only override determinism keys.

### WR-07: test_witness_report_records_lv2_commit_pin hardcodes pin, duplicating constant
**File:** `tests/python/test_witness_determinism.py:114`
`EXPECTED = "424e1e8ee7..."` is a literal, duplicating `LV2_COMMIT_PIN` in `witness_diff.py:105` and `LV2_COMMIT` in `witness_diff_build.sh:27`. Pin bump at one site creates silent drift.

**Fix:** Import from harness module or factor to shared `tests/python/_constants.py`.

### WR-08: witness_diff_build.sh picks first .lv2 bundle from unordered find
**File:** `scripts/ci/witness_diff_build.sh:82-87`
`find ... | head -1` — order not guaranteed. Fine today (one bundle) but upstream could ship a debug variant.

**Fix:** `LC_ALL=C sort` + assert count == 1.

### WR-09: check_coverage.py subprocess has no timeout
**File:** `scripts/ci/check_coverage.py:181-188`
`subprocess.run` with no `timeout=`. A hung ctest waits up to GitHub Actions' 360-minute job cap.

**Fix:** `timeout=1800`, catch `TimeoutExpired`, report offending test.

### WR-10: witness_diff.py doesn't validate lv2 port indices at runtime
**File:** `scripts/ci/witness_diff.py:131-140`
Port indices (0-7) are hardcoded constants. `.LV2_URI` is read but port discovery from `lv2info.txt` never re-validated. SHA bump without constant update → silent audio/control port cross-wiring.

**Fix:** Parse saved `lv2info.txt` at runtime and assert port layout matches constants.

## Info

### IN-01: _REJECT_METACHARS flagged with W605 silencer
**File:** `scripts/ci/check_coverage.py:52`
Variable is intentional (self-documenting). `# noqa: W605` silences a real warning about backtick in raw string — regex is fine but silencer worth a re-read.

### IN-02: BIB-\d{3} regex `\b` anchors partially redundant
**File:** `scripts/check_bibliography_refs.py:34`
Trailing `\b` after `\d{3}` correctly rules out `BIB-0001`. Leading `\b` load-bearing (blocks `FOOBIB-001`). No change required.

### IN-03: regenerate_goldens.py sets determinism env twice
**File:** `scripts/regenerate_goldens.py:49-51, 126-129`
Module-level `os.environ.setdefault` block is dead code once explicit `env=` override is in place in `render_golden()`. Harmless but confusing.

### IN-04: Dockerfile.repro runs as root
**File:** `Dockerfile.repro:67-80`
No `USER` directive. Common for CI-only images, low-risk. Optional hardening.

### IN-05: witness_diff_build.sh does full git clone instead of shallow
**File:** `scripts/ci/witness_diff_build.sh:39`
Full history fetched when only pinned commit needed. Save ~2-3s per CI run with `git init` + `fetch --depth 1` pattern. Skip if complexity not worth it.

### IN-06: goldens_present.py sha256 file count check is correct
**File:** `tests/conformance/test_goldens_present.py:79-82`
Glob `*/*.wav.sha256` resolves correctly via pathlib (not shell). No change required — noted for reviewer confidence.

### IN-07: ci.yml matrix installs clang-tidy only in dedicated job
**File:** `.github/workflows/ci.yml:39-41`
Conditional apt install on `matrix.compiler == 'clang'` is intentional and correct. Reader-unfriendly but functional.

### IN-08: modulation_harness report path is relative to cwd
**File:** `tests/python/test_modulation_harness.py:41`
`REPORT = Path("tests/python/modulation_report.json")` resolved against cwd at write time. Works from repo root (ctest/CI), breaks from other cwd.

**Fix:** `REPORT = _REPO_ROOT / "tests" / "python" / "modulation_report.json"`.

## Files Reviewed

38 source files across: CI workflows (ci.yml), build configuration (Dockerfile.repro, .dockerignore, .gitignore, CMakeLists.txt files), CI scripts (check_coverage.py, install-phase7-deps.sh, witness_diff.py, witness_diff_build.sh), utility scripts (regenerate_goldens.py, write_levers_catalog.py, check_bibliography_refs.py), real-time safety (hotpath_alloc_gate.sh + targets), test harnesses (modulation, benchmark, conformance), and documentation (BIBLIOGRAPHY.md, COVERAGE.md, DECISIONS.md, LEVERS-CATALOG.md).

No architectural concerns. Remediation surface is defense-in-depth hardening around locale plumbing, pin single-source-of-truth, and subprocess timeouts.
