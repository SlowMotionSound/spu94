---
phase: 07-verification-golden-files-witness-diff-modulation
fixed_at: 2026-04-23
review_path: .planning/phases/07-verification-golden-files-witness-diff-modulation/07-REVIEW.md
iteration: 1
findings_in_scope: 10
fixed: 10
skipped: 0
status: all_fixed
---

# Phase 7: Code Review Fix Report

**Fixed at:** 2026-04-23
**Source review:** `.planning/phases/07-verification-golden-files-witness-diff-modulation/07-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 10 (WR-01 through WR-10; 8 Info items deferred per `fix_scope: critical_warning`)
- Fixed: 10
- Skipped: 0

Regression suite `ctest -LE "fuzz|witness|benchmark|report_only"`: 72/73 pass.
The single failing test (`levers_catalog_writer`) was **already failing at the pre-fix HEAD** (`2a9bcd9`, before any WR-NN work) — it expects the `vLOUT` HAND cell in `docs/LEVERS-CATALOG.md` to be empty, but the committed catalog has `"Master Output"` authored in that cell. Unrelated to any fix in this iteration.

## Fixed Issues

### WR-01: witness_diff.py does not pin determinism env when shelling to SPU-94 CLI

**Files modified:** `scripts/ci/witness_diff.py`
**Commit:** `79b190c`
**Applied fix:** `invoke_spu94()` now builds an env dict pinning `LC_ALL=C`, `TZ=UTC`, `SOURCE_DATE_EPOCH=1704067200` before `subprocess.run`, mirroring `scripts/regenerate_goldens.py:render_golden`.

### WR-02: check_coverage.py ctest "no tests were found" matcher is locale-fragile

**Files modified:** `scripts/ci/check_coverage.py`
**Commit:** `f81e9bd`
**Applied fix:** `run_ctest()` now passes `env={**os.environ, LC_ALL=C, LANG=C}` to `subprocess.run`. Added the `import os` at module scope. Prose-matcher downstream is now locale-stable.

### WR-03: test_hotpath_alloc_gate_meta._find() can pick wrong binary from stale build dirs

**Files modified:** `tests/python/test_hotpath_alloc_gate_meta.py`
**Commit:** `66c8b55`
**Applied fix:** `_find()` now prefers the canonical `build/` tree (falls back to a repo-wide `build*/` scan only when `build/` is absent) and asserts exactly one hit. Stale `build-ubsan/` / `build-clang-*/` trees can no longer silently shadow via lexicographic sort order.

### WR-04: write_levers_catalog.py declares main(argv) but never parses argv

**Files modified:** `scripts/write_levers_catalog.py`
**Commit:** `8abd6af`
**Applied fix:** Dropped the `argv` parameter from `main()`; updated the call site to `main()`. Added a comment explaining why the writer has no CLI surface (fixed REPORT + CATALOG paths).

### WR-05: hotpath_alloc_gate.sh misdiagnoses strace failures

**Files modified:** `tests/rt_safety/hotpath_alloc_gate.sh`
**Commit:** `c861163`
**Applied fix:** Captures strace return code separately and branches on log-file state: empty/absent log + `rc != 0` → strace failure (missing, YAMA-blocked, no `CAP_SYS_PTRACE`, seccomp denial, unwritable log path); non-empty log + `rc != 0` → target crash. Preserves `WILL_FAIL TRUE` semantics for `hotpath_alloc_gate_negative`; both alloc_gate ctest entries still pass (verified locally: 2/2).

### WR-06: test_regenerate_goldens sparse env can mask locale-sensitive bugs

**Files modified:** `scripts/test_regenerate_goldens.py`
**Commit:** `4ddc1ce`
**Applied fix:** Env dict in `test_check_fails_on_mutated_wav` now starts from `{**os.environ, ...}` and only overrides `LC_ALL/TZ/SOURCE_DATE_EPOCH/SPU94_BIN`. Preserves `HOME/USER/TMPDIR/LD_LIBRARY_PATH` so runtime-library failures surface as link errors rather than "wav mismatch".

### WR-07: test_witness_report_records_lv2_commit_pin hardcodes pin, duplicating constant

**Files modified:** `tests/python/_constants.py` (new), `scripts/ci/witness_diff.py`, `tests/python/test_witness_determinism.py`, `scripts/ci/witness_diff_build.sh`
**Commit:** `0121541` + `c7ed588` (shell pointer-comment folded into WR-08 commit)
**Applied fix:** Canonical pin value lives in `tests/python/_constants.py`. `witness_diff.py` loads it via `importlib.util`; `test_witness_determinism.py` imports it directly via `sys.path` prepend. `witness_diff_build.sh` retains the default-value fallback (shell can't import Python) with a prominent comment flagging the lockstep-bump requirement.

### WR-08: witness_diff_build.sh picks first .lv2 bundle from unordered find

**Files modified:** `scripts/ci/witness_diff_build.sh`
**Commit:** `c7ed588`
**Applied fix:** Replaced `find ... | head -1` with `find ... | LC_ALL=C sort` and added a `BUNDLE_COUNT -ne 1` assertion. Fails loudly with the list of matches if upstream ever ships more than one `.lv2` bundle.

### WR-09: check_coverage.py subprocess has no timeout

**Files modified:** `scripts/ci/check_coverage.py`
**Commit:** `4db7af9`
**Applied fix:** Added `timeout=1800` to the `subprocess.run(ctest ...)` call and a `TimeoutExpired` handler that returns `rc=1` with an error message naming the test and tailing captured stdout/stderr.

### WR-10: witness_diff.py doesn't validate lv2 port indices at runtime

**Files modified:** `scripts/ci/witness_diff.py`
**Commit:** `87fd750`
**Applied fix:** Added `_parse_lv2info_ports()` (parses `Port N:` blocks into `symbol/kind/direction` tuples), `_absorb_type_uri()` helper, and `validate_lv2_port_layout()`, which asserts each of the 8 ports matches the hardcoded `_EXPECTED_PORTS` tuple. `main()` calls the validator immediately after `read_lv2_context()` and returns 1 on drift. Soft-skip with a `WARN:` on stderr when `lv2info.txt` is absent (fresh checkout — keeps harness usable before `witness_diff_build.sh` runs); hard-fails when the file IS present. Parser verified end-to-end against the live `lv2info.txt` for the pinned SHA: all 8 ports match.

---

_Fixed: 2026-04-23_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
