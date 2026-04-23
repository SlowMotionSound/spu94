#!/usr/bin/env python3
"""scripts/ci/test_check_coverage.py — Phase 7 Plan 01 Task 3 meta-tests.

Positive + three negative meta-tests for scripts/ci/check_coverage.py
(D-02 CI-enforced coverage validator). Threat T-07-01-A mitigation is
verified at the source-code level (grep for shell=False + metacharacter
rejection) rather than by injecting malicious test names — the validator
rejects shell-metacharacter names up front without ever invoking the
subprocess, so a positive negative-test would require exercising the
rejection path via a crafted tmp file.

Run via ctest (test_check_coverage target wired in Task 3 Step 3) or
directly: `pytest scripts/ci/test_check_coverage.py -q`.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER = REPO_ROOT / "scripts" / "ci" / "check_coverage.py"
COVERAGE_MD = REPO_ROOT / "docs" / "COVERAGE.md"


def _run_checker(argv_extra, cwd=None):
    """Invoke check_coverage.py with extra argv; return CompletedProcess."""
    cmd = [sys.executable, str(CHECKER), *argv_extra]
    return subprocess.run(
        cmd,
        capture_output=True,
        text=True,
        cwd=str(cwd or REPO_ROOT),
        check=False,
    )


def _tmp_coverage_copy(tmp_path: Path) -> Path:
    """Copy the committed COVERAGE.md into tmp_path; return its path."""
    dst = tmp_path / "COVERAGE.md"
    shutil.copy(COVERAGE_MD, dst)
    return dst


def _inject_row(cov_path: Path, before_section: str, new_row: str) -> None:
    """Insert `new_row` as a line immediately before the named `## section`."""
    text = cov_path.read_text()
    marker = f"\n## {before_section}"
    assert marker in text, f"marker not found: {before_section}"
    # Insert the row just before the marker with a newline.
    text = text.replace(marker, f"\n{new_row}{marker}", 1)
    cov_path.write_text(text)


# --------------------------------------------------------------------------
# Test 1 — positive: committed COVERAGE.md green
# --------------------------------------------------------------------------


def test_positive_committed_coverage_md_is_green():
    """Every row in the committed docs/COVERAGE.md references an existing
    test that passes under ctest. Exit 0; final stdout line matches
    'PASS: <N> COVERAGE.md rows all green' with N >= 50."""
    assert CHECKER.exists(), f"validator missing: {CHECKER}"
    result = _run_checker([])
    assert result.returncode == 0, (
        f"validator exited {result.returncode} on committed COVERAGE.md.\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    last_line = result.stdout.strip().splitlines()[-1]
    assert last_line.startswith("PASS: "), last_line
    assert "COVERAGE.md rows all green" in last_line, last_line
    # Extract row count from "PASS: <N> COVERAGE.md rows all green"
    parts = last_line.split()
    n = int(parts[1])
    assert n >= 50, f"expected >=50 green rows, got {n}"


# --------------------------------------------------------------------------
# Test 2 — negative: row names a file that does not exist
# --------------------------------------------------------------------------


def test_negative_missing_file(tmp_path):
    """Inject a row whose path points at a nonexistent file; validator
    exits 1 and stderr mentions 'file not found'."""
    cov = _tmp_coverage_copy(tmp_path)
    bogus_row = (
        "| bogus | I16 | "
        "`tests/unit/nonexistent_file.c::test_fake_never_registered` | |"
    )
    _inject_row(cov, before_section="Per-Behavior Coverage", new_row=bogus_row)
    result = _run_checker(["--file", str(cov)])
    assert result.returncode == 1, (
        f"validator must exit 1 on missing file; got {result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    assert "file not found" in result.stderr, result.stderr
    assert "tests/unit/nonexistent_file.c" in result.stderr, result.stderr


# --------------------------------------------------------------------------
# Test 3 — negative: row names a real file but a fake ctest test name
# --------------------------------------------------------------------------


def test_negative_missing_ctest_name(tmp_path):
    """Inject a row with a real file but a nonexistent ctest registration
    name; validator exits 1. Runs against the local build/ directory."""
    cov = _tmp_coverage_copy(tmp_path)
    # Use a path that definitely exists (the validator itself).
    # The test name has no registration in ctest -N, so ctest -R returns
    # zero tests matched (exit code 0 from ctest with a warning), OR the
    # validator's own ctest-empty-result check catches it.
    fake_row = (
        "| bogus2 | I16 | "
        "`scripts/ci/check_coverage.py::nonexistent_ctest_registration_xyz` "
        "| |"
    )
    _inject_row(cov, before_section="Per-Behavior Coverage", new_row=fake_row)
    result = _run_checker(["--file", str(cov)])
    assert result.returncode == 1, (
        f"validator must exit 1 on fake ctest name; got {result.returncode}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )
    # stderr mentions either 'not found' (ctest matched zero) or
    # 'returned non-zero' (ctest itself failed).
    stderr_low = result.stderr.lower()
    assert (
        "not found" in stderr_low
        or "returned non-zero" in stderr_low
        or "no tests matched" in stderr_low
    ), result.stderr


# --------------------------------------------------------------------------
# Test 4 — negative: empty test: cell outside Known Gaps
# --------------------------------------------------------------------------


def test_negative_empty_test_field_outside_known_gaps(tmp_path):
    """Inject a row with an empty backticked test: cell outside the
    Known Gaps section; validator exits 1 with 'empty test:' in stderr."""
    cov = _tmp_coverage_copy(tmp_path)
    empty_row = "| bogus_empty | I16 | `` | placeholder |"
    _inject_row(cov, before_section="Per-Behavior Coverage", new_row=empty_row)
    result = _run_checker(["--file", str(cov)])
    assert result.returncode == 1, (
        f"validator must exit 1 on empty test field outside Known Gaps; "
        f"got {result.returncode}\nstdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    assert "empty test:" in result.stderr, result.stderr


# --------------------------------------------------------------------------
# Test 5 — negative: shell-metacharacter injection in test name (T-07-01-A)
# --------------------------------------------------------------------------


def test_negative_shell_metacharacter_injection_rejected(tmp_path):
    """Inject a row whose test_name contains a shell metacharacter (`;`);
    validator rejects up front (exit 1, stderr mentions 'metacharacter'
    or 'reject')."""
    cov = _tmp_coverage_copy(tmp_path)
    # Backticks inside the cell are tricky; use a plain semicolon injection
    # which is the T-07-01-A canonical vector.
    evil_row = (
        "| evil | I16 | "
        "`tests/unit/q15/test_q15.c::q15_unit; rm -rf /tmp/xyz` | |"
    )
    _inject_row(cov, before_section="Per-Behavior Coverage", new_row=evil_row)
    result = _run_checker(["--file", str(cov)])
    assert result.returncode == 1, (
        f"validator must reject shell-metacharacter test names; "
        f"got {result.returncode}\nstdout:\n{result.stdout}\n"
        f"stderr:\n{result.stderr}"
    )
    stderr_low = result.stderr.lower()
    assert (
        "metacharacter" in stderr_low
        or "reject" in stderr_low
        or "invalid" in stderr_low
    ), result.stderr
