"""tests/python/test_witness_determinism.py — D-06 determinism meta-test.

The witness-diff harness measures numbers that will become the basis for a
deferred tolerance-policy ADR. For those numbers to be trustworthy they must
themselves be reproducible: two consecutive runs on the same host, same
lv2 build, same SPU-94 build, must produce identical witness_report.json
bytes (or numerically-identical contents modulo last-bit IEEE variance).

Preconditions:
  - `scripts/ci/witness_diff_build.sh` must have been run (LV2_PATH sidecar
    exists under .artifacts/lv2-psx-reverb/).
  - `build/src/cli/spu94` must exist (the SPU-94 native CLI).

Cost: running witness_diff.py twice takes ~30 s per run on the dev
workstation (50 pairs × ~0.3 s each). The test is registered with CTest
label `witness` so it can be skipped/included selectively.
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

# Import the canonical pin so the test and the witness_diff.py harness
# bump together (WR-07: single source of truth for LV2_COMMIT_PIN).
sys.path.insert(0, str(Path(__file__).resolve().parent))
from _constants import LV2_COMMIT_PIN  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT = REPO_ROOT / ".artifacts" / "witness_report.json"
LV2_SIDECAR = REPO_ROOT / ".artifacts" / "lv2-psx-reverb" / ".LV2_PATH"
SPU94_CLI = REPO_ROOT / "build" / "src" / "cli" / "spu94"


def _skip_if_preconditions_missing() -> None:
    if not LV2_SIDECAR.exists():
        pytest.skip(
            f"{LV2_SIDECAR} not found — "
            "run scripts/ci/witness_diff_build.sh first"
        )
    if not SPU94_CLI.exists():
        pytest.skip(
            f"{SPU94_CLI} not built — "
            "cmake --build build first"
        )


@pytest.fixture(scope="module")
def fresh_witness_run() -> tuple[bytes, bytes]:
    """Run witness_diff.py twice on the same source; return both report bodies."""
    _skip_if_preconditions_missing()

    harness = REPO_ROOT / "scripts" / "ci" / "witness_diff.py"

    r1 = subprocess.run(
        [sys.executable, str(harness)],
        cwd=REPO_ROOT, capture_output=True,
    )
    assert r1.returncode == 0, (
        f"first witness_diff.py run failed (rc={r1.returncode}):\n"
        f"{r1.stderr.decode('utf-8', errors='replace')[-800:]}"
    )
    assert REPORT.exists(), "first run did not produce witness_report.json"
    first = REPORT.read_bytes()

    r2 = subprocess.run(
        [sys.executable, str(harness)],
        cwd=REPO_ROOT, capture_output=True,
    )
    assert r2.returncode == 0, (
        f"second witness_diff.py run failed (rc={r2.returncode}):\n"
        f"{r2.stderr.decode('utf-8', errors='replace')[-800:]}"
    )
    second = REPORT.read_bytes()
    return first, second


def test_witness_diff_is_deterministic(fresh_witness_run):
    """Two runs → identical report (byte-equal or numerically equal to 1e-9)."""
    first, second = fresh_witness_run
    if first == second:
        return  # best case — byte-identical

    # Fall back: numerical near-equality (tolerate platform FP last-bit variance)
    f = json.loads(first)
    s = json.loads(second)
    assert len(f) == len(s), f"row count differs: {len(f)} vs {len(s)}"
    for a, b in zip(f, s):
        assert a["preset"] == b["preset"] and a["input"] == b["input"], \
            f"ordering mismatch: {a} vs {b}"
        assert a["alignment_lag_samples"] == b["alignment_lag_samples"], \
            f"{a['preset']}/{a['input']}: lag mismatch"
        assert abs(a["low_band_diff_dbfs"] - b["low_band_diff_dbfs"]) < 1e-9, \
            f"{a['preset']}/{a['input']}: low_band_diff_dbfs diverged"
        assert abs(a["high_band_diff_dbfs"] - b["high_band_diff_dbfs"]) < 1e-9, \
            f"{a['preset']}/{a['input']}: high_band_diff_dbfs diverged"


def test_witness_report_has_50_entries(fresh_witness_run):
    """Schema gate: 50 entries, every entry has the required keys."""
    first, _ = fresh_witness_run
    r = json.loads(first)
    assert len(r) == 50, f"expected 50 entries, got {len(r)}"
    required = {
        "preset", "input", "alignment_lag_samples",
        "low_band_diff_dbfs", "high_band_diff_dbfs",
    }
    for entry in r:
        assert required <= set(entry.keys()), \
            f"entry missing keys: {entry}"


def test_witness_report_records_lv2_commit_pin(fresh_witness_run):
    """Every row carries the lv2 commit pin for traceability (D-05)."""
    first, _ = fresh_witness_run
    r = json.loads(first)
    # Pin must appear in every row. Source of truth: tests/python/_constants.py
    # (WR-07 fix). The witness_diff.py harness also imports from that same
    # module, so this test will fail loudly if the pin drifts between sites.
    for entry in r:
        assert entry.get("lv2_commit") == LV2_COMMIT_PIN, \
            f"{entry['preset']}/{entry['input']}: lv2_commit={entry.get('lv2_commit')!r}"
