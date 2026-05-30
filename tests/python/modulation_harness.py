"""tests/python/modulation_harness.py — Phase 7 Plan 04 TEST-05 helpers.

D-17 gate: stability + determinism at every modulation rate from
~0.1 Hz through audio-rate (~11 kHz) for all 35 SPU registers.
D-18 SC-4 reinterpretation: zipper / stepping / polarity-flip at high
modulation rates is *catalogued*, not failed — it's the PS1 hardware
character.
D-19: all 35 registers × {sine, sweep, random_walk}.

Deliberate 105-case shape (vs a naive 385-case grid):
- Sweep mode walks 0.1 Hz → 11 kHz continuously per register via
  scipy.signal.chirp(..., method='logarithmic'); the D-17 rate axis
  is therefore exercised in its entirety INSIDE sweep mode alone.
- Determinism is a per-seed property, not a per-rate property — running
  at N rates proves it N times for no new information.
- Sine mode at one representative mid-rate (500 Hz) spot-checks the
  stability invariants under steady-state (non-swept) modulation.
- Random-walk mode adds non-periodic stressing of the D-08 write-policy
  machinery (IMMEDIATE vs TICK_LATCHED) under audio-rate writes.

Threat T-07-04-E (internal-boundary stability): this module reaches the
raw ctypes state handle through ``SPU94._state`` — Phase 6's test-
surface contract. A rename there fails LOUDLY with AttributeError here,
rather than silently passing the Python wrapper object as a ctypes
argument. Documented in the plan's threat register.
"""
from __future__ import annotations

import hashlib
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple

import numpy as np

# Match fuzz_*.py convention so the module imports cleanly under ctest
# regardless of cwd. SPU94_LIB env var is set by CMake via a generator
# expression; the test_modulation_harness.py wrapper also sets it when
# running outside ctest.
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

import spu94  # noqa: E402 — after sys.path prepend
from spu94 import SPU94, Preset, Register  # noqa: E402

# ---------------------------------------------------------------------------
# Tunables
# ---------------------------------------------------------------------------
FS = 44100
DURATION_SEC = 1.0
N = int(FS * DURATION_SEC)
AMP = 16000
SEED = 0x1094_DADA  # matches Phase 7 Plan 02 golden-corpus white_noise seed
BLOCK = 1024

# D-17 rate axis (sweep mode walks this continuously; sine mode picks index 4).
RATES_HZ = [0.1, 1.0, 10.0, 100.0, 500.0, 1000.0, 2000.0, 5000.0, 11025.0]
SWEEP_RANGE = (0.1, 11025.0)

# Signedness allowlist — the 12 v-prefix registers are i16; the rest are u16.
# Matches spu94_register_io.c's 64-bit packed mask and was re-verified
# against the live binding via lib.spu94_reg_type() in Phase 5's fuzz_process.
I16_REG_NAMES = frozenset([
    "vLOUT", "vROUT", "vIIR", "vWALL",
    "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4",
    "vAPF1", "vAPF2", "vLIN", "vRIN",
])

# D-08 write policy (Phase 2 Plan 03, ADR-0005): 13 IMMEDIATE + 22 TICK_LATCHED.
# IMMEDIATE = all 12 v-prefix + mBASE. TICK_LATCHED = the remaining 22 m* + d*.
IMMEDIATE_REG_NAMES = frozenset([
    "vLOUT", "vROUT", "mBASE", "vIIR", "vWALL",
    "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4",
    "vAPF1", "vAPF2", "vLIN", "vRIN",
])


# ---------------------------------------------------------------------------
# Register signedness / range helpers
# ---------------------------------------------------------------------------
def reg_is_signed(reg: Register) -> bool:
    return reg.name in I16_REG_NAMES


def reg_valid_range(reg: Register) -> Tuple[int, int]:
    if reg_is_signed(reg):
        return (-32768, 32767)
    return (0, 65535)


# ---------------------------------------------------------------------------
# Modulation stream generators
# ---------------------------------------------------------------------------
def modulation_stream(mode: str, rate_hz: float, n: int, seed: int) -> np.ndarray:
    """Return an n-sample modulation signal in [-1, 1] for the requested mode.

    ``sine`` — steady-state sinusoid at ``rate_hz``.
    ``sweep`` — logarithmic chirp from 0.1 Hz → 11 kHz over DURATION_SEC;
      ``rate_hz`` is ignored. This is the D-17 rate-axis walker.
    ``random_walk`` — non-periodic random walk, seeded by ``seed``, normalized
      so the peak excursion lands at ±0.95.
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n) / FS
    if mode == "sine":
        return 0.95 * np.sin(2.0 * np.pi * float(rate_hz) * t)
    if mode == "sweep":
        # Import locally to avoid paying the scipy import cost when only
        # sine / random_walk cases are exercised.
        from scipy.signal import chirp
        return 0.95 * chirp(
            t, SWEEP_RANGE[0], DURATION_SEC, SWEEP_RANGE[1],
            method="logarithmic",
        )
    if mode == "random_walk":
        steps = rng.integers(-500, 500, size=n)
        walk = np.cumsum(steps).astype(np.float64)
        denom = float(np.max(np.abs(walk))) + 1.0
        return 0.95 * walk / denom
    raise ValueError(f"unknown mode: {mode}")


def modulation_to_register_values(norm: np.ndarray, reg: Register) -> np.ndarray:
    """Map a normalized [-1, 1] stream to integer register values clipped
    to the register's signedness-determined valid range."""
    lo, hi = reg_valid_range(reg)
    center = (lo + hi) // 2
    half = (hi - lo) // 2
    vals = (center + norm * half).astype(np.int64)
    return np.clip(vals, lo, hi)


def set_reg_typed(state_handle, reg: Register, value: int) -> None:
    """Dispatch to the signed- or unsigned-variant raw-panel setter.

    ``state_handle`` must be the raw ctypes state pointer (``SPU94._state``),
    NOT the ``SPU94`` instance itself — T-07-04-E documents this contract."""
    v = int(value)
    if reg_is_signed(reg):
        spu94.set_reg_i16(state_handle, reg, v)
    else:
        spu94.set_reg_u16(state_handle, reg, v)


# ---------------------------------------------------------------------------
# Result type
# ---------------------------------------------------------------------------
@dataclass
class ModulationResult:
    reg_name: str
    mode: str
    rate_hz: Optional[float]
    out_sha256: str  # determinism-gate bit-identity hash
    zipper_onset_hz: Optional[float]
    sample_to_sample_rms_max: float
    stability_ok: bool
    # ADR-0023 (M1 close-out Step 6): the snapshot of the OOB tap
    # counter at the END of the run. Hall's working set fits inside
    # the SPU94_WORK_BUF_MAX_BYTES default; a non-zero count here
    # signals that the modulation drove an m-prefix or d-prefix
    # register past the buffer end, which is the class of caller-bug
    # the counter exists to surface. test_modulation_harness asserts
    # this stays at 0 across all 105 cases.
    oob_tap_count: int


# ---------------------------------------------------------------------------
# Core harness
# ---------------------------------------------------------------------------
def run_one_case(
    reg: Register,
    mode: str,
    rate_hz: Optional[float],
    seed: int = SEED,
) -> Tuple[np.ndarray, ModulationResult]:
    """Drive SPU-94 with a 440 Hz sine input and the modulation stream for
    ``reg``; return (stereo_output_int16, ModulationResult)."""
    # SPU94() default work_buf_size = SPU94_WORK_BUF_MAX_BYTES (M1 close-out
    # Step 5) covers Hall and every other preset.
    s = SPU94()
    s.load_preset(Preset.HALL)
    # ADR-Phase-6-H: non-Off factory preset tables ship vLOUT=vROUT=0x7FFF,
    # so no post-load master-send unlock is required for HALL. The master
    # gains ARE registers the harness may subsequently modulate (vLOUT /
    # vROUT appear in the 35-register sweep); those cases drive the value
    # themselves from the modulation stream.

    # Input: 440 Hz sine, int16 stereo.
    t = np.arange(N) / FS
    input_ch = (AMP * np.sin(2.0 * np.pi * 440.0 * t)).astype(np.int16)
    Lin = input_ch.copy()
    Rin = input_ch.copy()
    Lout = np.zeros(N, dtype=np.int16)
    Rout = np.zeros(N, dtype=np.int16)

    # Modulation stream in [-1, 1]; map to the register's valid range.
    norm = modulation_stream(mode, rate_hz if rate_hz is not None else 0.0,
                             N, seed)
    reg_vals = modulation_to_register_values(norm, reg)

    # LOAD-BEARING BINDING CONTRACT (T-07-04-E): reach the raw ctypes
    # state pointer directly via SPU94._state. No hasattr fallback — a
    # rename in the Phase 6 binding fails LOUDLY with AttributeError
    # rather than silently treating the SPU94 instance as a valid
    # ctypes argument. See the plan's threat register.
    state_handle = s._state

    # Drive in BLOCK-sample blocks; write the register once per block at
    # block-start. This gives the modulation stream a per-block sampling
    # granularity (~23 ms at 1024 samples / 44.1 kHz) — slower than the
    # rates in sweep mode but matching how a DAW plugin host would call
    # process() with a bounded block size in a real-world use case.
    try:
        for bs in range(0, N, BLOCK):
            be = min(bs + BLOCK, N)
            set_reg_typed(state_handle, reg, int(reg_vals[bs]))
            s.process(Lin[bs:be], Rin[bs:be], Lout[bs:be], Rout[bs:be])
        # Snapshot the OOB counter BEFORE destroy zeros the state.
        oob_tap_count = int(spu94.get_error_counters(state_handle)["oob_tap_count"])
    finally:
        # Destroy even on exception so parametrized runs don't leak state.
        s.destroy()

    stereo_out = np.stack([Lout, Rout], axis=1)
    stability_ok = bool(
        np.all(np.isfinite(stereo_out.astype(np.float64)))
        and int(stereo_out.min()) >= -32768
        and int(stereo_out.max()) <= 32767
    )

    # Sample-to-sample RMS delta — a numeric proxy for zipper audibility.
    # High s2s_rms at audio-rate sine modulation is the D-18 "character,
    # not failure" signal we want to catalogue.
    diff = np.diff(stereo_out.astype(np.float64), axis=0)
    s2s_rms_max = float(np.sqrt(np.mean(diff ** 2)))

    # Zipper onset proxy: sine mode only (sweep / random_walk have no
    # single rate to report). Threshold is 1% of full-scale AMP — a
    # deliberately low floor so the catalog captures onsets earlier
    # rather than missing marginal cases; the LEVERS-CATALOG writer
    # summarizes this as "~500 Hz" style.
    if mode == "sine" and s2s_rms_max > 0.01 * AMP:
        zipper_onset = float(rate_hz) if rate_hz is not None else None
    else:
        zipper_onset = None

    out_sha = hashlib.sha256(stereo_out.tobytes()).hexdigest()

    return stereo_out, ModulationResult(
        reg_name=reg.name,
        mode=mode,
        rate_hz=rate_hz,
        out_sha256=out_sha,
        zipper_onset_hz=zipper_onset,
        sample_to_sample_rms_max=s2s_rms_max,
        stability_ok=stability_ok,
        oob_tap_count=oob_tap_count,
    )


# ---------------------------------------------------------------------------
# Pitfall-7 classifier
# ---------------------------------------------------------------------------
def classify_modulation_cost(reg: Register) -> str:
    """Catalog label for a register's modulation cost class (07-RESEARCH
    Pitfall 7). HAND-override-able in LEVERS-CATALOG.md.

    - "free" — IMMEDIATE policy + v-prefix gain register (12 total).
    - "sample-quantized" — TICK_LATCHED + d-prefix delay pointer (6 total:
      dAPF1/2, dLSAME/dRSAME, dLDIFF/dRDIFF).
    - "catastrophic" — m-prefix address register; writes relocate a memory
      region the reverb is still reading from; audible as hard clicks at
      every write boundary. Includes mBASE even though mBASE's policy is
      IMMEDIATE — its snap-on-write ADR-0006 side effect is the definition
      of catastrophic for modulation purposes. (17 total: mBASE + 16 m*.)
    """
    n = reg.name
    if n in IMMEDIATE_REG_NAMES and n.startswith("v"):
        return "free"
    if n.startswith("d"):
        return "sample-quantized"
    return "catastrophic"
