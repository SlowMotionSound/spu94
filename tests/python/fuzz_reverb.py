#!/usr/bin/env python3
"""tests/python/fuzz_reverb.py -- Phase 3 Plan 04 Task 2

10^6-step random fuzz harness for the full reverb network. The C reverb
body computation (Plans 01-03) is exercised through spu94_tick with a
stream of random register writes interleaved with ticks. The harness
asserts a set of structural invariants after every step; divergence from
any invariant is a regression against Plans 01-04.

Invariants:
  -- buffer_address <= 0x7FFFE (Phase 2 wrap rule; ADR-0006)
  -- buffer_address >= mBASE   (Phase 2 floor; the wrap formula MAX term)
  -- (buffer_address & 1) == 0 OR buffer_address == mBASE (halfword
     alignment, with the T-02-28 snap-on-write exception for odd mBASE)
  -- No crashes / no OOB memory access (a ctypes call into a crashing
     .so aborts the Python process with SIGSEGV, failing the test).
  -- err_* and overflow_magnitude accumulators remain int32 (tautology
     via the C type; verified indirectly by the no-crash assertion).

Per-sample bit-exactness between reverb_body and the stage-by-stage
composition is covered by the C test_reverb_body Unity TU; this Python
fuzz harness covers structural invariants over a much larger random
state-space.

Environment:
    SPU94_LIB    -- absolute path to libspu94.so (required by the CMake
                    test wrapper; defaults to build/src/spu94/libspu94.so
                    if unset for local invocation).
    SPU94_FUZZ_SEED  -- integer seed, hex or decimal (default 0xDEADBEEF).
    SPU94_FUZZ_STEPS -- number of iterations (default 1_000_000).

Pitfall 9 (GPL provenance): this harness loads only the SPU-94 shared
library. It does not import or link against any GPL emulator (Mednafen,
DuckStation, lv2-psx-reverb, UPSE, PCSX-Redux, MiSTer).

Pattern source: tests/python/fuzz_buffer.py (Phase 2 Plan 05 Task 3).
Shares the aligned-buffer helper + generator-expression env-var wiring.
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

# Default fuzz step count. 10^6 satisfies ROADMAP Phase 3's SC-4 bound;
# callers can override via --steps or SPU94_FUZZ_STEPS.
N_STEPS = 1000000

# Hand-synced from include/spu94/spu94.h. Phase 6 will derive these via
# ctypes introspection of the header instead of duplicating them.
SPU94_STATE_SIZE_MAX = 16384
SPU94_STATE_ALIGN_MAX = 16
WORK_BUF_SIZE = 8192
ADDR_MASK = 0x7FFFE

# Hand-synced from include/spu94/spu94_registers.h enum order (same as
# fuzz_buffer.py). If the enum ordering changes, the C tests catch the
# divergence via spu94_reg_hw_offset; this constant table is a
# defensible second line of defense.
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

# I16 (signed gain) registers -- the 12 v-prefix entries.
I16_REGS = [
    SPU94_REG_vLOUT, SPU94_REG_vROUT, SPU94_REG_vIIR,
    SPU94_REG_vCOMB1, SPU94_REG_vCOMB2, SPU94_REG_vCOMB3, SPU94_REG_vCOMB4,
    SPU94_REG_vWALL, SPU94_REG_vAPF1, SPU94_REG_vAPF2,
    SPU94_REG_vLIN, SPU94_REG_vRIN,
]
assert len(I16_REGS) == 12

# U16 registers -- mBASE (IMMEDIATE + snap-on-write) and the 22
# TICK_LATCHED d*/m* delay/address registers.
U16_REGS = [
    SPU94_REG_mBASE,
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
assert len(U16_REGS) == 23


def load_lib(lib_path: Path) -> ctypes.CDLL:
    if not lib_path.exists():
        sys.exit(f"FAIL: {lib_path} not found. Build first: cmake --build build")
    lib = ctypes.CDLL(str(lib_path))

    lib.spu94_state_size.restype = ctypes.c_size_t
    lib.spu94_state_size.argtypes = []

    lib.spu94_init.restype = ctypes.c_void_p
    lib.spu94_init.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
    ]

    lib.spu94_set_reg_i16.restype = ctypes.c_int
    lib.spu94_set_reg_i16.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int16]
    lib.spu94_set_reg_u16.restype = ctypes.c_int
    lib.spu94_set_reg_u16.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16]
    lib.spu94_get_reg_i16.restype = ctypes.c_int16
    lib.spu94_get_reg_i16.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.spu94_get_reg_u16.restype = ctypes.c_uint16
    lib.spu94_get_reg_u16.argtypes = [ctypes.c_void_p, ctypes.c_int]

    lib.spu94_tick.restype = None
    lib.spu94_tick.argtypes = [ctypes.c_void_p]

    lib.spu94_get_buffer_address.restype = ctypes.c_uint32
    lib.spu94_get_buffer_address.argtypes = [ctypes.c_void_p]

    return lib


def aligned_buffer(size: int, align: int):
    """Allocate size bytes aligned to `align`. Returns (raw, void_ptr)."""
    raw = (ctypes.c_ubyte * (size + align))()
    addr = ctypes.addressof(raw)
    offset = (align - (addr % align)) % align
    return raw, ctypes.c_void_p(addr + offset)


def run_fuzz(lib: ctypes.CDLL, seed: int, steps: int) -> int:
    rng = random.Random(seed)
    state_raw, state_ptr = aligned_buffer(SPU94_STATE_SIZE_MAX, SPU94_STATE_ALIGN_MAX)
    work_raw, work_ptr = aligned_buffer(WORK_BUF_SIZE, SPU94_STATE_ALIGN_MAX)
    # Keep raw buffers alive for the duration of the run -- ctypes would
    # otherwise GC them out from under the .so.
    _keepalive = (state_raw, work_raw)

    state = lib.spu94_init(state_ptr, SPU94_STATE_SIZE_MAX,
                           work_ptr, WORK_BUF_SIZE)
    if not state:
        sys.exit("FAIL: spu94_init returned NULL")

    op_log: list[str] = []  # ring buffer of the last 10 ops for diagnostics

    for i in range(steps):
        # Four operation classes, biased toward ticks so the reverb
        # network actually runs frequently: 1/4 set_i16, 1/4 set_u16,
        # 1/2 tick.
        op = rng.randrange(4)

        if op == 0:
            reg = rng.choice(I16_REGS)
            value = rng.randrange(-0x8000, 0x8000)
            lib.spu94_set_reg_i16(state, reg, value)
            op_desc = f"set_i16(reg={reg}, {value})"
        elif op == 1:
            reg = rng.choice(U16_REGS)
            value = rng.randrange(0, 0x10000)
            lib.spu94_set_reg_u16(state, reg, value)
            op_desc = f"set_u16(reg={reg}, {value:#06x})"
        else:
            lib.spu94_tick(state)
            op_desc = "tick"

        op_log.append(op_desc)
        if len(op_log) > 10:
            op_log.pop(0)

        # Invariant checks after every op.
        ba = lib.spu94_get_buffer_address(state)
        mb = lib.spu94_get_reg_u16(state, SPU94_REG_mBASE)
        failures = []

        if ba > ADDR_MASK:
            failures.append(f"buffer_address ({ba:#x}) > 0x7FFFE")
        if ba < mb:
            failures.append(f"buffer_address ({ba:#x}) < mBASE ({mb:#x})")
        if (ba & 1) != 0 and ba != mb:
            # Odd buffer_address is only permitted via the odd-mBASE
            # snap-on-write exception (T-02-28 / ADR-0006).
            failures.append(
                f"buffer_address ({ba:#x}) is odd but != mBASE ({mb:#x})"
            )

        if failures:
            print(f"FAIL at step {i}: {failures}", file=sys.stderr)
            print("Last ops:", file=sys.stderr)
            for op_str in op_log:
                print(f"  {op_str}", file=sys.stderr)
            print(f"Seed: {seed:#x}", file=sys.stderr)
            return 1

    print(f"OK: {steps} steps passed (seed={seed:#x})")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 3 Plan 04 reverb fuzz harness (10^6 default)."
    )
    parser.add_argument(
        "--seed", type=lambda s: int(s, 0),
        default=int(os.environ.get("SPU94_FUZZ_SEED", "0xDEADBEEF"), 0),
        help="Random seed (default: 0xDEADBEEF or $SPU94_FUZZ_SEED).",
    )
    parser.add_argument(
        "--steps", type=int,
        default=int(os.environ.get("SPU94_FUZZ_STEPS", "1000000")),
        help="Number of fuzz iterations (default: 1_000_000 or $SPU94_FUZZ_STEPS).",
    )
    parser.add_argument(
        "--lib", type=Path, default=None,
        help="Path to libspu94.so (default: $SPU94_LIB or the repo-root build dir).",
    )
    args = parser.parse_args()

    lib_path = args.lib
    if lib_path is None:
        env_lib = os.environ.get("SPU94_LIB")
        lib_path = Path(env_lib) if env_lib else DEFAULT_LIB_PATH

    lib = load_lib(lib_path)

    # Seed announcement (required for reproducibility on failures).
    print(f"fuzz_reverb seed={args.seed:#010x} steps={args.steps}")

    t0 = time.monotonic()
    rc = run_fuzz(lib, args.seed, args.steps)
    elapsed = time.monotonic() - t0
    if rc == 0:
        rate = args.steps / elapsed if elapsed > 0 else float("inf")
        print(f"   elapsed={elapsed:.2f}s  rate={rate:,.0f} ops/s")
    return rc


if __name__ == "__main__":
    sys.exit(main())
