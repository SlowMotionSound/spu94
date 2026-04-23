"""tests/benchmarks/test_benchmark.py -- Phase 7 Plan 05 (BUILD-06).

Report-only timing harness for spu94.process over the 10 factory presets
at two representative block sizes (1024, 4096 samples at 44.1 kHz).

D-20 policy: timing is REPORT-ONLY. The only real-time safety GATE in
Phase 7 is the allocation gate at tests/rt_safety/hotpath_alloc_gate.sh.
pytest-benchmark here simply records numbers for historical comparison;
CI publishes the JSON as an artifact but does NOT fail the build on
timing regressions (ubuntu-latest runner jitter would produce noisy
false positives otherwise).

D-21 policy: tests/benchmarks/benchmark_baselines.json is committed and
human-reviewed. CI NEVER regenerates it. See tests/benchmarks/README.md
for the manual refresh procedure.

Fixture deterministic: seed=42, amplitude=16000 -- matches the style of
the Phase 7 golden-file input set (07-02 used 0x1094_DADA but that's a
different artifact; these fixtures are internal to the benchmark run).

Import path: this file follows the tests/python/fuzz_*.py convention --
sys.path prepend to the repo's python/ dir + SPU94_LIB env var pointing
at the just-built libspu94.so. Works without `pip install -e .` so the
CI job doesn't need a user-site pip cache.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

import numpy as np
import pytest

# Match fuzz_*.py: find the repo root, prepend python/ to sys.path. The
# binding loads libspu94.so via SPU94_LIB env var (set by ctest via a
# generator expression; set manually when running pytest outside ctest).
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

# Surface a friendly error if SPU94_LIB isn't set -- pytest-benchmark
# collection-time failures are less readable than an explicit check.
if "SPU94_LIB" not in os.environ:
    # Try the standard build location as a convenience for interactive runs.
    _candidate = _REPO_ROOT / "build" / "src" / "spu94" / "libspu94.so"
    if _candidate.exists():
        os.environ["SPU94_LIB"] = str(_candidate)

from spu94 import Preset, SPU94  # noqa: E402 -- after sys.path prepend


# Preset.__members__ order: OFF, ROOM, STUDIO_A, STUDIO_B, STUDIO_C,
# HALL, HALF_ECHO, SPACE_ECHO, ECHO, DELAY. Iterate via the enum so any
# renumbering in the C layer flows through without a hand-sync edit.
PRESET_NAMES = [name for name in Preset.__members__
                if name != "__COUNT" and not name.startswith("_")]
assert len(PRESET_NAMES) == 10, (
    f"Expected 10 presets, binding exposes {len(PRESET_NAMES)}: {PRESET_NAMES}"
)

BLOCK_SIZES = (1024, 4096)
SEED = 42
AMPLITUDE = 16000
# 512 KiB reverb work buffer -- matches the hotpath_alloc_gate target
# sizing so long-tail presets never wrap during the measurement window.
WORK_BUF_BYTES = 524288


@pytest.fixture(scope="module")
def input_block_1024():
    rng = np.random.default_rng(SEED)
    return rng.integers(-AMPLITUDE, AMPLITUDE, size=1024, dtype=np.int16)


@pytest.fixture(scope="module")
def input_block_4096():
    rng = np.random.default_rng(SEED)
    return rng.integers(-AMPLITUDE, AMPLITUDE, size=4096, dtype=np.int16)


def _build_spu(preset_name: str) -> SPU94:
    s = SPU94(work_buf_size=WORK_BUF_BYTES)
    s.load_preset(getattr(Preset, preset_name))
    # Unlock the wet output path (ADR-Phase-6-G) so the reverb signal
    # actually reaches the output -- otherwise the measurement would
    # short-circuit to zero-output after the FIR chain, not the full
    # reverb-network cost we want to time.
    s.set_reg("vLOUT", 0x7FFF)
    s.set_reg("vROUT", 0x7FFF)
    s.tick()
    return s


@pytest.mark.benchmark(
    group="process_1024",
    min_rounds=5,
    warmup=True,
    disable_gc=True,
)
@pytest.mark.parametrize("preset_name", PRESET_NAMES)
def test_bench_process_1024(benchmark, preset_name, input_block_1024):
    """Benchmark spu94_process at 1024-sample blocks -- the typical low-
    latency audio block size used in DAWs and plugin hosts."""
    s = _build_spu(preset_name)
    L_in = input_block_1024
    R_in = input_block_1024
    L_out = np.zeros(1024, dtype=np.int16)
    R_out = np.zeros(1024, dtype=np.int16)
    benchmark(s.process, L_in, R_in, L_out, R_out)


@pytest.mark.benchmark(
    group="process_4096",
    min_rounds=5,
    warmup=True,
    disable_gc=True,
)
@pytest.mark.parametrize("preset_name", PRESET_NAMES)
def test_bench_process_4096(benchmark, preset_name, input_block_4096):
    """Benchmark spu94_process at 4096-sample blocks -- the typical
    offline-render / bulk-processing block size."""
    s = _build_spu(preset_name)
    L_in = input_block_4096
    R_in = input_block_4096
    L_out = np.zeros(4096, dtype=np.int16)
    R_out = np.zeros(4096, dtype=np.int16)
    benchmark(s.process, L_in, R_in, L_out, R_out)
