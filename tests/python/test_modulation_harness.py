"""tests/python/test_modulation_harness.py — Phase 7 Plan 04 TEST-05.

Parametrized stability + determinism gates for the 35-register × 3-mode
modulation harness (D-17). Sweep mode walks 0.1 Hz → 11 kHz continuously
per register, so the per-register rate axis is exercised inside sweep
mode without a parametrize-rate explosion. See modulation_harness.py's
module docstring for the deliberate 105-case-count rationale.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

import numpy as np
import pytest

# Match fuzz_*.py / test_benchmark.py convention: prepend python/ to sys.path
# and point SPU94_LIB at the built .so when running outside ctest.
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

if "SPU94_LIB" not in os.environ:
    _candidate = _REPO_ROOT / "build" / "src" / "spu94" / "libspu94.so"
    if _candidate.exists():
        os.environ["SPU94_LIB"] = str(_candidate)

# Make modulation_harness.py importable regardless of invocation cwd.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from spu94 import Register  # noqa: E402 — after sys.path prepend
from modulation_harness import (  # noqa: E402
    RATES_HZ,
    ModulationResult,
    classify_modulation_cost,
    run_one_case,
)


REPORT = Path("tests/python/modulation_report.json")


# Session-scoped result collector so the final report-writer test can
# consolidate what every parametrized case produced.
@pytest.fixture(scope="session")
def results():
    return {}


@pytest.mark.parametrize("mode", ["sine", "sweep", "random_walk"])
@pytest.mark.parametrize(
    "reg", list(Register), ids=[r.name for r in Register]
)
def test_stability(reg, mode, results):
    """Every (reg, mode) case produces bounded int16 output with no NaN."""
    rate = RATES_HZ[4] if mode == "sine" else None  # 500 Hz mid-rate
    _out, res = run_one_case(reg, mode, rate)
    assert res.stability_ok, (
        f"{reg.name}/{mode}: stability gate failed; "
        f"s2s_rms_max={res.sample_to_sample_rms_max:.3f}"
    )
    # ADR-0023 (M1 close-out Step 6): Hall fits inside the
    # SPU94_WORK_BUF_MAX_BYTES default by construction (ADR-0022). A
    # non-zero OOB counter here means the modulation drove an address
    # register past the buffer end — exactly the silent-corruption
    # class the counter was added to surface.
    assert res.oob_tap_count == 0, (
        f"{reg.name}/{mode}: oob_tap_count={res.oob_tap_count} "
        f"(must be 0 — modulation drove a tap past the work buffer)"
    )
    results.setdefault(reg.name, {})[mode] = res


@pytest.mark.parametrize("mode", ["sine", "sweep", "random_walk"])
@pytest.mark.parametrize(
    "reg", list(Register), ids=[r.name for r in Register]
)
def test_determinism(reg, mode):
    """Running the same case twice with the same seed produces byte-
    identical output audio — this is the Phase 2 D-08 write-policy
    verification at audio-rate modulation."""
    rate = RATES_HZ[4] if mode == "sine" else None
    _out_a, res_a = run_one_case(reg, mode, rate, seed=0x1094_DADA)
    _out_b, res_b = run_one_case(reg, mode, rate, seed=0x1094_DADA)
    assert res_a.out_sha256 == res_b.out_sha256, (
        f"{reg.name}/{mode}: non-deterministic; "
        f"sha_a={res_a.out_sha256[:16]} sha_b={res_b.out_sha256[:16]}"
    )


def test_report_written(results):
    """Runs last in session (alphabetically after test_stability /
    test_determinism), consolidating the stability-test results into
    ``tests/python/modulation_report.json``."""
    assert len(results) > 0, (
        "expected parametrized stability tests to populate results"
    )
    report: dict = {}
    for reg_name, modes in results.items():
        reg = next((r for r in Register if r.name == reg_name), None)
        if reg is None:
            continue
        report[reg_name] = {
            "modulation_cost": classify_modulation_cost(reg),
            "modes": {
                m: {
                    "stability_ok": res.stability_ok,
                    "sample_to_sample_rms_max": res.sample_to_sample_rms_max,
                    "zipper_onset_hz": res.zipper_onset_hz,
                    "out_sha256": res.out_sha256,
                    "oob_tap_count": res.oob_tap_count,
                }
                for m, res in modes.items()
            },
        }
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(report, indent=2, sort_keys=True))
    assert len(report) == 35, f"expected 35 registers, got {len(report)}"
