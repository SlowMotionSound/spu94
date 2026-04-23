#!/usr/bin/env python3
"""scripts/test_regenerate_goldens.py — Phase 7 Plan 02 Task 2 meta-tests.

Two meta-tests for scripts/regenerate_goldens.py:

  1. `test_check_passes_on_committed` — positive path. The committed
     tests/golden/ corpus + regenerate_goldens.py --check exit 0 with
     "PASS: 50/50 goldens match" on stdout.

  2. `test_check_fails_on_mutated_wav` — negative path. Copy the
     committed tests/golden/ into pytest tmp_path, flip one byte in
     one .wav, run --check against that mutated tree, assert returncode
     == 1 and stderr mentions the mutated preset/input pair.

Run directly: pytest scripts/test_regenerate_goldens.py -q
Via ctest:    ctest --test-dir build -R regenerate_goldens_meta
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = REPO_ROOT / "scripts" / "regenerate_goldens.py"
GOLDEN_ROOT = REPO_ROOT / "tests" / "golden"

# The regenerate_goldens.py script locates the spu94 CLI relative to its
# CWD ("build/src/cli/spu94"). The repo's build tree lives there; for the
# mutated-tree test we run the script from a tmp parent dir that contains
# a fake tests/golden/ subtree, so we pass the real CLI path through
# SPU94_BIN to skip the relative-path lookup.
SPU94_BIN = REPO_ROOT / "build" / "src" / "cli" / "spu94"


def _skip_if_cli_missing():
    if not SPU94_BIN.is_file():
        pytest.skip(
            f"spu94 CLI not built at {SPU94_BIN}; run cmake --build build first."
        )


def test_check_passes_on_committed():
    """--check against the committed corpus exits 0 with PASS: 50/50."""
    _skip_if_cli_missing()
    r = subprocess.run(
        [sys.executable, str(SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(REPO_ROOT),
        check=False,
    )
    assert r.returncode == 0, (
        f"--check on committed corpus failed (rc={r.returncode})\n"
        f"stdout: {r.stdout}\nstderr: {r.stderr}"
    )
    assert "PASS: 50/50 goldens match" in r.stdout, (
        f"expected 'PASS: 50/50 goldens match' in stdout, got:\n{r.stdout}"
    )


def test_check_fails_on_mutated_wav(tmp_path: Path):
    """One flipped byte in one golden .wav causes --check to exit 1 and
    stderr to identify the mutated <preset>/<input> pair."""
    _skip_if_cli_missing()

    # Build a sibling tree at tmp_path/tests/golden/ by copying the committed one.
    tmp_tests = tmp_path / "tests"
    tmp_tests.mkdir()
    shutil.copytree(GOLDEN_ROOT, tmp_tests / "golden")

    # Flip one byte in hall/impulse.wav at offset 1024 (past the RIFF header
    # so the WAV header integrity check in dr_wav still parses the file).
    mutated = tmp_tests / "golden" / "hall" / "impulse.wav"
    raw = bytearray(mutated.read_bytes())
    raw[1024] ^= 0xFF
    mutated.write_bytes(bytes(raw))

    # Run the script from tmp_path so its relative "tests/golden" picks up
    # the mutated copy. Point SPU94_BIN at the real CLI binary explicitly.
    env = {
        "SPU94_BIN": str(SPU94_BIN),
        "PATH": "/usr/bin:/bin",
        "LC_ALL": "C",
        "TZ": "UTC",
        "SOURCE_DATE_EPOCH": "1704067200",
    }
    r = subprocess.run(
        [sys.executable, str(SCRIPT), "--check"],
        capture_output=True,
        text=True,
        cwd=str(tmp_path),
        env=env,
        check=False,
    )

    assert r.returncode == 1, (
        f"--check should fail on mutated wav; rc={r.returncode}\n"
        f"stdout: {r.stdout}\nstderr: {r.stderr}"
    )
    assert "hall/impulse" in r.stderr, (
        f"expected 'hall/impulse' in stderr, got:\n{r.stderr}"
    )
    assert "FAIL" in r.stderr, (
        f"expected 'FAIL' prefix in stderr, got:\n{r.stderr}"
    )


if __name__ == "__main__":
    sys.exit(pytest.main([__file__, "-q"]))
