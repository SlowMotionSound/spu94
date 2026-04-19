#!/usr/bin/env python3
"""tests/python/fuzz_buffer.py

Phase 2 Plan 05 Task 3 -- ROADMAP Phase 2 SC 3 fuzz harness: 10^6 random
operations against the BufferAddress wrap invariant. Pure-random (no
Hypothesis dependency); seed is printed for reproducibility.

Invariant asserted after every operation:
    buffer_address >= mBASE
    buffer_address <= 0x7FFFE
    (buffer_address & 1) == 0       (halfword alignment)

Narrow exception (T-02-28, ADR-0006): immediately after a set_mBASE call
with an odd value, buffer_address may be odd until the next tick re-aligns
it. The harness recognizes this case and does not flag it as failure.

On failure, prints the last ~10 operations and exits non-zero.

Environment:
    SPU94_LIB -- absolute path to libspu94.so (optional; defaults to
                 build/src/spu94/libspu94.so relative to this script).

Usage:
    python3 tests/python/fuzz_buffer.py [--seed N] [--steps N] [--lib PATH]

Pre-Phase-6 setup: this is a single-file ctypes driver (CONTEXT.md
Specific Ideas: 'a single fuzz_buffer.py is fine, no full wheel needed
yet'). Phase 6 replaces this file's hand-synced enum IDs with ctypes
IntEnum derived from the C header at import time.
"""

import argparse
import ctypes
import os
import random
import sys
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_LIB_PATH = REPO_ROOT / "build" / "src" / "spu94" / "libspu94.so"

# Must match include/spu94/spu94.h macros (hand-synced; Phase 6's real
# binding will assert this matches at import time via spu94_state_size()).
SPU94_STATE_SIZE_MAX = 16384
SPU94_STATE_ALIGN_MAX = 16
WORK_BUF_SIZE = 8192
ADDR_MASK = 0x7FFFE

# Register IDs in enum order. Hand-synced from include/spu94/spu94_registers.h
# (Plan 02 Task 1). Plan 05 Task 3 SUMMARY flags this for Phase 6 auto-sync.
(
    SPU94_REG_vLOUT, SPU94_REG_vROUT, SPU94_REG_mBASE,
    SPU94_REG_dAPF1, SPU94_REG_dAPF2, SPU94_REG_vIIR,
    SPU94_REG_vCOMB1, SPU94_REG_vCOMB2, SPU94_REG_vCOMB3, SPU94_REG_vCOMB4,
    SPU94_REG_vWALL, SPU94_REG_vAPF1, SPU94_REG_vAPF2,
    SPU94_REG_mLSAME, SPU94_REG_mRSAME,
    SPU94_REG_mLCOMB1, SPU94_REG_mRCOMB1,
    SPU94_REG_mLCOMB2, SPU94_REG_mRCOMB2,
    SPU94_REG_dLSAME, SPU94_REG_dRSAME,
    SPU94_REG_mLDIFF, SPU94_REG_mRDIFF,
    SPU94_REG_mLCOMB3, SPU94_REG_mRCOMB3,
    SPU94_REG_mLCOMB4, SPU94_REG_mRCOMB4,
    SPU94_REG_dLDIFF, SPU94_REG_dRDIFF,
    SPU94_REG_mLAPF1, SPU94_REG_mRAPF1,
    SPU94_REG_mLAPF2, SPU94_REG_mRAPF2,
    SPU94_REG_vLIN, SPU94_REG_vRIN,
) = range(35)
SPU94_REG__COUNT = 35

# u16 TICK_LATCHED registers (the 22 d-prefix and m-prefix delay/address
# registers, excluding mBASE which is IMMEDIATE). The fuzz harness only
# writes u16 registers (mBASE + tick-latched). Writing i16 registers is
# covered exhaustively by the C unit tests in tests/unit/registers/.
U16_TICK_LATCHED_REGS = [
    SPU94_REG_dAPF1, SPU94_REG_dAPF2,
    SPU94_REG_mLSAME, SPU94_REG_mRSAME,
    SPU94_REG_mLCOMB1, SPU94_REG_mRCOMB1,
    SPU94_REG_mLCOMB2, SPU94_REG_mRCOMB2,
    SPU94_REG_dLSAME, SPU94_REG_dRSAME,
    SPU94_REG_mLDIFF, SPU94_REG_mRDIFF,
    SPU94_REG_mLCOMB3, SPU94_REG_mRCOMB3,
    SPU94_REG_mLCOMB4, SPU94_REG_mRCOMB4,
    SPU94_REG_dLDIFF, SPU94_REG_dRDIFF,
    SPU94_REG_mLAPF1, SPU94_REG_mRAPF1,
    SPU94_REG_mLAPF2, SPU94_REG_mRAPF2,
]
assert len(U16_TICK_LATCHED_REGS) == 22, "U16_TICK_LATCHED_REGS must have 22 entries"


def load_lib(lib_path: Path) -> ctypes.CDLL:
    if not lib_path.exists():
        sys.exit(
            f"FAIL: {lib_path} not found. Build first: cmake --build build"
        )
    lib = ctypes.CDLL(str(lib_path))

    lib.spu94_state_size.restype = ctypes.c_size_t
    lib.spu94_state_size.argtypes = []

    lib.spu94_init.restype = ctypes.c_void_p
    lib.spu94_init.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
    ]

    lib.spu94_set_reg_u16.restype = ctypes.c_int
    lib.spu94_set_reg_u16.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16,
    ]

    lib.spu94_get_reg_u16.restype = ctypes.c_uint16
    lib.spu94_get_reg_u16.argtypes = [ctypes.c_void_p, ctypes.c_int]

    lib.spu94_tick.restype = None
    lib.spu94_tick.argtypes = [ctypes.c_void_p]

    lib.spu94_get_buffer_address.restype = ctypes.c_uint32
    lib.spu94_get_buffer_address.argtypes = [ctypes.c_void_p]

    return lib


def aligned_buffer(size: int, align: int):
    """Allocate a size-byte buffer aligned to `align` bytes.

    Returns (raw_keepalive, void_ptr_to_aligned_start). The caller MUST
    keep `raw` alive for the lifetime of the void_ptr -- otherwise Python
    may garbage-collect the underlying storage.
    """
    raw = (ctypes.c_ubyte * (size + align))()
    addr = ctypes.addressof(raw)
    offset = (align - (addr % align)) % align
    aligned_ptr = ctypes.c_void_p(addr + offset)
    return raw, aligned_ptr


def run_fuzz(lib: ctypes.CDLL, seed: int, steps: int) -> int:
    rng = random.Random(seed)
    state_raw, state_ptr = aligned_buffer(SPU94_STATE_SIZE_MAX, SPU94_STATE_ALIGN_MAX)
    work_raw, work_ptr = aligned_buffer(WORK_BUF_SIZE, SPU94_STATE_ALIGN_MAX)
    # Keep raw buffers alive for the duration of the fuzz run.
    _keepalive = (state_raw, work_raw)

    state = lib.spu94_init(state_ptr, SPU94_STATE_SIZE_MAX, work_ptr, WORK_BUF_SIZE)
    if not state:
        sys.exit("FAIL: spu94_init returned NULL")

    # Independent Python model of buffer_address + mBASE. Each step we
    # compute the predicted post-op state in Python from the formula
    # MAX(mBASE, (ba+2) & 0x7FFFE) for tick, or ba := mBASE for set_mBASE,
    # then compare against the C library's reported state. This is a
    # stronger check than the wrap invariant alone -- it catches divergence
    # in either direction. The wrap invariant
    #   (ba <= 0x7FFFE) and (ba >= mBASE) and ((ba & 1) == 0 OR ba == mBASE_odd)
    # is also asserted.
    py_ba = 0           # mirrors state->buffer_address
    py_mb = 0           # mirrors mBASE register value
    op_log = []  # ring buffer of last 10 ops for failure diagnostics

    for i in range(steps):
        op = rng.randint(0, 2)

        if op == 0:
            # Write mBASE: IMMEDIATE policy; snap-on-write per ADR-0006.
            value = rng.randint(0, 0xFFFF)
            lib.spu94_set_reg_u16(state, SPU94_REG_mBASE, value)
            py_mb = value
            py_ba = value          # snap-on-write (verbatim, odd-pass-through)
            op_desc = f"set_mBASE({value:#06x})"
        elif op == 1:
            # Write a non-mBASE u16 register (TICK_LATCHED). Does NOT
            # mutate buffer_address or mBASE.
            reg = rng.choice(U16_TICK_LATCHED_REGS)
            value = rng.randint(0, 0xFFFF)
            lib.spu94_set_reg_u16(state, reg, value)
            op_desc = f"set_reg_u16(reg={reg}, {value:#06x})"
        else:
            lib.spu94_tick(state)
            # Apply the formula: ba := MAX(mBASE, (ba+2) & 0x7FFFE).
            advanced = (py_ba + 2) & ADDR_MASK
            py_ba = py_mb if py_mb > advanced else advanced
            op_desc = "tick"

        op_log.append(op_desc)
        if len(op_log) > 10:
            op_log.pop(0)

        # Read C state and compare with Python model.
        c_ba = lib.spu94_get_buffer_address(state)
        c_mb = lib.spu94_get_reg_u16(state, SPU94_REG_mBASE)
        failures = []

        if c_mb != py_mb:
            failures.append(
                f"mBASE diverged: C={c_mb:#x} vs Python model={py_mb:#x}"
            )
        if c_ba != py_ba:
            failures.append(
                f"buffer_address diverged: C={c_ba:#x} vs Python model={py_ba:#x}"
            )
        # Wrap invariants (still asserted independently for clarity).
        if c_ba < c_mb:
            failures.append(f"buffer_address ({c_ba:#x}) < mBASE ({c_mb:#x})")
        if c_ba > 0x7FFFE:
            failures.append(f"buffer_address ({c_ba:#x}) > 0x7FFFE")
        if (c_ba & 1) != 0:
            # Permitted only when c_ba == c_mb (odd snap or odd-MAX result);
            # never as a result of a tick whose wrap-mask path was taken.
            if c_ba != c_mb:
                failures.append(
                    f"buffer_address ({c_ba:#x}) is odd but != mBASE "
                    f"({c_mb:#x}) -- bit 0 should only persist via mBASE"
                )

        if failures:
            print(f"FAIL at step {i}: {failures}", file=sys.stderr)
            print("Last ops:", file=sys.stderr)
            for op_str in op_log:
                print(f"  {op_str}", file=sys.stderr)
            print(f"Seed: {seed}", file=sys.stderr)
            return 1

    print(f"OK: {steps} steps passed (seed={hex(seed)})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 2 SC 3 BufferAddress fuzz harness (10^6 default)."
    )
    parser.add_argument(
        "--seed", type=lambda s: int(s, 0), default=0xC0FFEE,
        help="Random seed (default: 0xC0FFEE; printed on every run).",
    )
    parser.add_argument(
        "--steps", type=int, default=1_000_000,
        help="Number of fuzz iterations (default: 10^6).",
    )
    parser.add_argument(
        "--lib", type=Path, default=None,
        help=f"Path to libspu94.so (default: $SPU94_LIB or {DEFAULT_LIB_PATH})",
    )
    args = parser.parse_args()

    lib_path = args.lib
    if lib_path is None:
        env_lib = os.environ.get("SPU94_LIB")
        lib_path = Path(env_lib) if env_lib else DEFAULT_LIB_PATH

    lib = load_lib(lib_path)

    t0 = time.monotonic()
    rc = run_fuzz(lib, args.seed, args.steps)
    elapsed = time.monotonic() - t0
    if rc == 0:
        rate = args.steps / elapsed if elapsed > 0 else float("inf")
        print(f"   elapsed={elapsed:.2f}s  rate={rate:,.0f} ops/s")
    return rc


if __name__ == "__main__":
    sys.exit(main())
