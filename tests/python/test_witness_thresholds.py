"""tests/python/test_witness_thresholds.py — Step 12 / ADR-0024 gate.

Asserts that the per-pair `low_band_diff_dbfs` values in
`.artifacts/witness_report.json` stay at-or-below the per-preset
thresholds in `config/witness_diff_thresholds.json`.

This is a regression gate, not a correctness gate. It catches the
"future commit silently doubles the SPU-94 vs lv2 divergence" class of
regression while leaving the absolute numbers as a measurement-only
output for human / stakeholder review (`scripts/ci/witness_diff.py`
prints them, this test only fails on regression past the threshold).

High-band divergence is intentionally NOT gated — lv2-psx-reverb omits
the half-band FIR by design (ADR-Phase-4-I), so high-band numbers are
informational and reflect a known algorithmic difference, not a bug.

The "off" preset produces silence on both sides (low-band is -inf /
-360 dBFS); the threshold table's `skip_presets` list excludes it
explicitly to avoid asserting on a silent baseline.

Preconditions: the same as test_witness_determinism.py — the witness
harness must have produced a fresh report. We do NOT re-run the
harness here (that would double CI time); we read the already-produced
.artifacts/witness_report.json. ctest job ordering ensures the
witness_determinism test (which runs the harness) lands a fresh report
before this gate fires.
"""
import json
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
REPORT = REPO_ROOT / ".artifacts" / "witness_report.json"
THRESHOLDS = REPO_ROOT / "config" / "witness_diff_thresholds.json"


def _load_thresholds() -> dict:
    """Parse config/witness_diff_thresholds.json. Required to exist —
    if the file is missing, the test fails (the policy is a committed
    artifact, not optional)."""
    assert THRESHOLDS.exists(), (
        f"missing threshold table: {THRESHOLDS} — "
        "this is a committed policy artifact (Step 12 / ADR-0024)"
    )
    return json.loads(THRESHOLDS.read_text())


def _skip_if_no_report() -> None:
    """Self-skip when the witness report hasn't been generated yet
    (matches the witness_determinism precondition pattern)."""
    if not REPORT.exists():
        pytest.skip(
            f"{REPORT} not present — "
            "run scripts/ci/witness_diff.py (or ctest -L witness) first"
        )


def test_witness_thresholds_table_well_formed():
    """The threshold table itself is required to be well-formed: nine
    presets with numeric thresholds + a skip list. Catches typos in
    the JSON before they ship as silently-disabled gates."""
    cfg = _load_thresholds()
    assert "low_band_max_dbfs" in cfg, (
        "threshold table missing 'low_band_max_dbfs' key"
    )
    assert "skip_presets" in cfg, (
        "threshold table missing 'skip_presets' key"
    )
    expected_presets = {
        "delay", "echo", "half_echo", "hall", "room",
        "space_echo", "studio_a", "studio_b", "studio_c",
    }
    actual_presets = set(cfg["low_band_max_dbfs"].keys())
    assert actual_presets == expected_presets, (
        f"threshold preset set mismatch: "
        f"missing={expected_presets - actual_presets!r}, "
        f"extra={actual_presets - expected_presets!r}"
    )
    for preset, threshold in cfg["low_band_max_dbfs"].items():
        assert isinstance(threshold, (int, float)), (
            f"threshold for {preset!r} is not a number: {threshold!r}"
        )
    assert "off" in cfg["skip_presets"], (
        "'off' preset must be in skip_presets (silence on both sides)"
    )


def test_witness_low_band_diff_within_threshold():
    """Each (preset, input) pair's low_band_diff_dbfs must be at-or-
    below its preset's threshold. Failures are reported with the full
    triple (preset, input, value, threshold) so a regression is
    actionable."""
    _skip_if_no_report()
    cfg = _load_thresholds()
    thresholds = cfg["low_band_max_dbfs"]
    skip = set(cfg["skip_presets"])

    report = json.loads(REPORT.read_text())
    failures = []
    asserted = 0
    for entry in report:
        preset = entry["preset"]
        if preset in skip:
            continue
        if preset not in thresholds:
            failures.append(
                f"  {preset!r}/{entry['input']!r}: "
                f"no threshold defined for preset (table out of sync with report?)"
            )
            continue
        observed = entry["low_band_diff_dbfs"]
        ceiling = thresholds[preset]
        if observed > ceiling:
            failures.append(
                f"  {preset!r}/{entry['input']!r}: "
                f"low_band_diff_dbfs={observed:.3f} exceeds threshold {ceiling:.3f}"
            )
        asserted += 1

    assert asserted > 0, (
        "no entries asserted — witness report is empty or threshold table "
        "skips every preset; check ctest ordering / harness output"
    )
    assert not failures, (
        f"{len(failures)} witness-diff threshold violation(s):\n"
        + "\n".join(failures)
    )
