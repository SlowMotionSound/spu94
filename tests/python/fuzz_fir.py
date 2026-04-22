#!/usr/bin/env python3
"""tests/python/fuzz_fir.py -- Phase 4 Plan 04 Task 2 (D-16)

10^6-step ctypes fuzz harness for the full 44.1 kHz FIR chain via
spu94_fir_chain_step_reverb_bypass. The harness asserts a set of
structural invariants after every step; divergence from any invariant
is a regression against Plans 01-04.

Invariants:
  -- output L/R are in [INT16_MIN, INT16_MAX] (sat_s16 contract;
     ADR-0001 + D-05 clamp-once).
  -- spu94_get_latency_samples() is stable at SPU94_LATENCY_SAMPLES
     (58u as of Plan 03; D-09 latency contract).
  -- An explicit 0x5A5A5A5A canary stamped into the state buffer past
     the struct's in-use footprint stays intact (catches any stage
     function writing past its delay-line bounds; complements Phase 2's
     aligned_buffer + Plan 01's struct-size _Static_assert).
  -- Periodic spu94_reset exercises phase-tracker reset hygiene
     (Pitfall 5); canary is re-verified across the reset.
  -- No crashes / no OOB (a ctypes call into a crashing .so aborts the
     Python process with SIGSEGV and fails the test).

Per 04-RESEARCH 9.9: this harness extends the Phase 2+3 fuzz discipline
to the Phase 4 FIR boundary. Deeper invariants (err/overflow tap
monotonicity, delay-line-index bounds) require a ctypes.Structure mirror
of spu94_state with precise hand-synced offsets; Phase 6 formalizes this
and this fuzz defers those invariants to Phase 6 per the research
recommendation.

Pitfall 9 (GPL provenance): this harness loads only the SPU-94 shared
library. It does not import or link against any GPL emulator (Mednafen,
DuckStation, lv2-psx-reverb, UPSE, PCSX-Redux, MiSTer).

Environment:
    SPU94_LIB         -- absolute path to libspu94.so (required by the
                         CMake test wrapper; defaults to build-dir .so).
    SPU94_FUZZ_SEED   -- integer seed, hex or decimal (default 0xDEADBEEF).
    SPU94_FUZZ_STEPS  -- number of iterations (default 1_000_000).

Pattern source: tests/python/fuzz_reverb.py (Phase 3 Plan 04 Task 2).
Shares the aligned-buffer helper + generator-expression env-var wiring.

Phase 6 Plan 5 D-16 migration: hand-typed SPU94_STATE_SIZE_MAX /
SPU94_STATE_ALIGN_MAX / SPU94_LATENCY_SAMPLES constants are replaced
by imports from the new spu94 binding package. The fuzz script's
extra C symbol (spu94_fir_chain_step_reverb_bypass) is not on the
binding's public prototype list (it's an internal-but-exported test
hook per Plan 4 SUMMARY); its argtypes are declared locally.
"""

import argparse
import ctypes
import os
import random
import sys
import time
from pathlib import Path

# Phase 6 Plan 5 D-16: prepend python/ to sys.path so `import spu94`
# resolves to the repository's binding source tree during ctest runs.
# Matches the python/spu94/__init__.py layout landed in Plan 1. The
# SPU94_LIB env var (set by ctest via $<TARGET_FILE:spu94_shared>)
# continues to control which libspu94.so is loaded.
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

from spu94 import (  # noqa: E402 — after sys.path prepend
    SPU94_STATE_SIZE_MAX,
    SPU94_STATE_ALIGN_MAX,
    SPU94_LATENCY_SAMPLES,
)
from spu94._binding import _lib  # noqa: E402

REPO_ROOT = _REPO_ROOT
DEFAULT_LIB_PATH = REPO_ROOT / "build" / "src" / "spu94" / "libspu94.so"

WORK_BUF_SIZE = 8192
SPU94_LATENCY_SAMPLES_EXPECTED = SPU94_LATENCY_SAMPLES

# Canary stamped past the in-use struct footprint. struct spu94_state is
# ~544 bytes at end of Plan 03 (<< SPU94_STATE_SIZE_MAX); offset 0x1000
# (4096) is deep inside the slack region and will never be touched by
# any stage function.
CANARY_OFFSET = 0x1000
CANARY_VALUE = 0x5A5A5A5A

# Default step count. 10^6 satisfies 04-RESEARCH 9.9 scope.
N_STEPS_DEFAULT = 1_000_000

# Periodic spu94_reset interval (Pitfall 5 phase-tracker reset hygiene).
RESET_INTERVAL = 10_000


def load_lib(lib_path: Path) -> ctypes.CDLL:
    """Return the binding's shared-library handle.

    Phase 6 Plan 5 D-16 migration: thin wrapper over the already-
    configured `_lib` from `python/spu94/_binding.py`. Public entries
    (spu94_state_size, spu94_init, spu94_reset, spu94_get_latency_samples)
    have their argtypes set by the binding at import time.

    Internal-but-exported entry `spu94_fir_chain_step_reverb_bypass` is
    NOT on the binding's public prototype list — Plan 4 SUMMARY flagged
    it as a tests-only hook and kept it out of `_binding.py`. Its
    argtypes are declared on the shared handle here; doing so is a no-op
    for the binding because ctypes setattr stores the argtypes on the
    C function's `_FuncPtr` attribute, not on the module.
    """
    if not lib_path.exists():
        sys.exit(f"FAIL: {lib_path} not found. "
                 f"Build first: cmake --build build")

    # Extend the binding handle with the tests-only FIR-chain step
    # entry point. Safe: the binding doesn't declare this symbol, so
    # there's no argtypes-conflict to worry about.
    _lib.spu94_fir_chain_step_reverb_bypass.restype = None
    _lib.spu94_fir_chain_step_reverb_bypass.argtypes = [
        ctypes.c_void_p, ctypes.c_int16, ctypes.c_int16,
        ctypes.POINTER(ctypes.c_int16), ctypes.POINTER(ctypes.c_int16),
    ]
    return _lib


def aligned_buffer(size: int, align: int):
    """Allocate size bytes aligned to `align`. Returns (raw, void_ptr).
    Matches the fuzz_buffer.py / fuzz_reverb.py pattern."""
    raw = (ctypes.c_ubyte * (size + align))()
    addr = ctypes.addressof(raw)
    offset = (align - (addr % align)) % align
    return raw, ctypes.c_void_p(addr + offset), addr + offset


def stamp_canary(aligned_addr: int) -> ctypes.c_uint32:
    """Write the 0x5A5A5A5A canary at state + CANARY_OFFSET. Returns a
    live ctypes.c_uint32 view so the caller can read it on every step."""
    slot = ctypes.c_uint32.from_address(aligned_addr + CANARY_OFFSET)
    slot.value = CANARY_VALUE
    return slot


def run_fuzz(lib: ctypes.CDLL, seed: int, steps: int) -> int:
    rng = random.Random(seed)
    state_raw, state_ptr, state_addr = aligned_buffer(
        SPU94_STATE_SIZE_MAX, SPU94_STATE_ALIGN_MAX,
    )
    work_raw, work_ptr, _work_addr = aligned_buffer(
        WORK_BUF_SIZE, SPU94_STATE_ALIGN_MAX,
    )
    # Keep raw buffers alive for the duration of the run.
    _keepalive = (state_raw, work_raw)

    # WR-02: validate CANARY_OFFSET against the actual struct footprint
    # before entering the fuzz loop. If a future plan grows spu94_state
    # past CANARY_OFFSET, the canary would sit inside the in-use footprint
    # and spu94_reset's byte-loop would zero it every reset, producing a
    # persistent "canary drift" failure that looks like an OOB bug.
    # Catch that regression here with a clear diagnostic.
    actual_size = lib.spu94_state_size()
    if CANARY_OFFSET < actual_size:
        sys.exit(f"FAIL: CANARY_OFFSET ({CANARY_OFFSET:#x}) is inside "
                 f"the struct footprint ({actual_size} bytes). "
                 f"Struct grew; bump CANARY_OFFSET or rework canary "
                 f"placement.")
    if CANARY_OFFSET + 4 > SPU94_STATE_SIZE_MAX:
        sys.exit(f"FAIL: CANARY_OFFSET ({CANARY_OFFSET:#x}) + "
                 f"sizeof(uint32) overflows state buffer "
                 f"(SPU94_STATE_SIZE_MAX={SPU94_STATE_SIZE_MAX}).")

    state = lib.spu94_init(state_ptr, SPU94_STATE_SIZE_MAX,
                           work_ptr, WORK_BUF_SIZE)
    if not state:
        sys.exit("FAIL: spu94_init returned NULL")

    # Pin latency at init; must stay stable for the whole run (D-09).
    latency0 = lib.spu94_get_latency_samples()
    if latency0 != SPU94_LATENCY_SAMPLES_EXPECTED:
        sys.exit(f"FAIL: spu94_get_latency_samples() == {latency0} "
                 f"at startup, expected {SPU94_LATENCY_SAMPLES_EXPECTED}")

    canary_slot = stamp_canary(state_addr)

    l_out = ctypes.c_int16(0)
    r_out = ctypes.c_int16(0)

    # Ring buffer of the last 10 ops for diagnostics on failure.
    op_log: list[str] = []

    def log_failure(step: int, reason: str) -> None:
        print(f"FAIL at step {step}: {reason}", file=sys.stderr)
        print("Last ops:", file=sys.stderr)
        for op_str in op_log:
            print(f"  {op_str}", file=sys.stderr)
        print(f"Seed: {seed:#x}", file=sys.stderr)

    for i in range(steps):
        L = rng.randrange(-0x8000, 0x8000)
        R = rng.randrange(-0x8000, 0x8000)
        lib.spu94_fir_chain_step_reverb_bypass(
            state, L, R, ctypes.byref(l_out), ctypes.byref(r_out),
        )

        op_log.append(f"step={i} L={L} R={R} "
                      f"lo={l_out.value} ro={r_out.value}")
        if len(op_log) > 10:
            op_log.pop(0)

        # Periodic reset exercise.
        if i > 0 and i % RESET_INTERVAL == 0:
            lib.spu94_reset(state)
            # spu94_reset zeros only the in-use struct footprint; the
            # canary at 0x1000 must survive.
            if canary_slot.value != CANARY_VALUE:
                log_failure(i, f"canary drift after reset: "
                               f"{canary_slot.value:#x} "
                               f"!= {CANARY_VALUE:#x}")
                return 1
            # Re-pin latency; the FIR chain's macro shouldn't change
            # across resets, but the accessor may re-read internal
            # storage. Belt+suspenders.
            if lib.spu94_get_latency_samples() != SPU94_LATENCY_SAMPLES_EXPECTED:
                log_failure(i, f"latency drifted after reset: "
                               f"{lib.spu94_get_latency_samples()}")
                return 1

        # Output range (sat_s16 contract / D-05 clamp-once).
        if not (-0x8000 <= l_out.value <= 0x7FFF):
            log_failure(i, f"L out of int16 range: {l_out.value}")
            return 1
        if not (-0x8000 <= r_out.value <= 0x7FFF):
            log_failure(i, f"R out of int16 range: {r_out.value}")
            return 1

        # Latency pin (D-09).
        if lib.spu94_get_latency_samples() != SPU94_LATENCY_SAMPLES_EXPECTED:
            log_failure(i, f"latency drifted: "
                           f"{lib.spu94_get_latency_samples()}")
            return 1

        # Canary invariant (buffer-overrun detector).
        if canary_slot.value != CANARY_VALUE:
            log_failure(i, f"canary drift: {canary_slot.value:#x} "
                           f"!= {CANARY_VALUE:#x}")
            return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 4 Plan 04 FIR fuzz (10^6 default).",
    )
    parser.add_argument(
        "--seed", type=lambda s: int(s, 0),
        default=int(os.environ.get("SPU94_FUZZ_SEED", "0xDEADBEEF"), 0),
        help="Random seed (default: 0xDEADBEEF or $SPU94_FUZZ_SEED).",
    )
    parser.add_argument(
        "--steps", type=int,
        default=int(os.environ.get("SPU94_FUZZ_STEPS", str(N_STEPS_DEFAULT))),
        help=f"Number of fuzz iterations "
             f"(default: {N_STEPS_DEFAULT} or $SPU94_FUZZ_STEPS).",
    )
    parser.add_argument(
        "--lib", type=Path, default=None,
        help="Path to libspu94.so (default: $SPU94_LIB or build dir).",
    )
    args = parser.parse_args()

    lib_path = args.lib
    if lib_path is None:
        env_lib = os.environ.get("SPU94_LIB")
        lib_path = Path(env_lib) if env_lib else DEFAULT_LIB_PATH

    lib = load_lib(lib_path)

    print(f"fuzz_fir seed={args.seed:#010x} steps={args.steps}")

    t0 = time.monotonic()
    rc = run_fuzz(lib, args.seed, args.steps)
    elapsed = time.monotonic() - t0
    if rc == 0:
        rate = args.steps / elapsed if elapsed > 0 else float("inf")
        print(f"OK: {args.steps} steps passed "
              f"(seed={args.seed:#x}) runtime={elapsed:.2f}s "
              f"rate={rate:,.0f} ops/s")
    return rc


if __name__ == "__main__":
    sys.exit(main())
