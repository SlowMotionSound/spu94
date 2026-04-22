#!/usr/bin/env python3
"""tests/python/fuzz_process.py -- Phase 5 Plan 05, D-10a.

10^6-step random-walk fuzz harness for the Phase 5 public block-based
audio entry point + mid-stream register writes + preset loads.

Proves API-06 "mid-stream writes first-class" at scale: any register,
any block size, any ordering of process/flush/load_preset calls does
NOT crash, NOT corrupt state, NOT cause an int16 output to escape
its declared range, NOT violate the buffer_address wrap invariant,
NOT drive the FIR delay-line indices out of [0, FIR_DELAY_LEN),
NOT set any of the top 29 reserved bits of pending_mask.

Companion invariants to Phase 2 fuzz_buffer.py (buffer arithmetic),
Phase 3 fuzz_reverb.py (reverb-network), Phase 4 fuzz_fir.py (FIR
chain). Phase 5's fuzz adds the public-API layer above them.

Golden seed: 0x05F05EED (chosen so failures are reproducible). Override
via --seed CLI arg.

Phase 6 Plan 5 D-16 / D-17 migration:
  - D-16: hand-typed SPU94_STATE_SIZE_MAX / SPU94_STATE_ALIGN_MAX /
    SPU94_REG__COUNT / SPU94_PRESET__COUNT / I16_REGS / SPU94_REG_mBASE
    + the per-function ctypes argtype / restype block are replaced
    with imports from the new spu94 binding package. The binding's
    runtime-reflection IntEnum + _lib handle are the single source
    of truth.
  - D-17: struct-internal offsets (PENDING_MASK_OFFSET, FIR_IDX_*_OFFSET)
    stay hand-typed in this script — they reach INTO the private
    spu94_state struct via byte-offset peeks and are not exposed by
    the binding. See the strengthened warning block below.
"""
import argparse
import ctypes
import os
import random
import sys
import time
from pathlib import Path
from ctypes import (
    c_uint8,
    c_uint64,
    c_void_p,
)

import numpy as np  # Phase 6 Plan 5 D-16: binding uses numpy ndpointer
                    # argtypes on spu94_process / spu94_flush, so the
                    # harness switches from raw ctypes arrays to numpy
                    # int16 arrays to satisfy the strict contract (D-09).

GOLDEN_SEED = 0x05F05EED
DEFAULT_STEPS = 1_000_000

# ------------------------------------------------------------------------
# Phase 6 Plan 5 D-16 migration: import constants + _lib from the new
# spu94 binding. The sys.path prepend matches the other three fuzz
# scripts (fuzz_buffer / fuzz_reverb / fuzz_fir) so all four behave
# identically under ctest.
# ------------------------------------------------------------------------
_REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(_REPO_ROOT / "python"))

from spu94 import (  # noqa: E402 — after sys.path prepend
    Register,
    SPU94_REG__COUNT,
    SPU94_PRESET__COUNT,
    SPU94_STATE_SIZE_MAX,
    SPU94_STATE_ALIGN_MAX,
    SPU94_OK,
    SPU94_UNKNOWN_REG,
    SPU94_TYPE_MISMATCH,
)
from spu94._binding import _lib  # noqa: E402

# ------------------------------------------------------------------------
# Hand-synced struct-internal offsets (per D-17 / 06-CONTEXT.md):
#
# These reach INTO the private spu94_state struct via byte-offset peeks.
# They are NOT exposed via the public C API or the Phase 6 Python
# binding; D-08 / D-17 explicitly keeps them in the fuzz scripts as
# "tests-only knowledge." The binding does not know about these
# offsets because struct-internal layout is not part of the public ABI.
#
# If the struct layout shifts, the import-time spu94_state_size() check
# in the Phase 6 binding catches the shift LOUDLY
# (RuntimeError: spu94 library mismatch: ...) — but only a struct-size
# change fires that gate. Individual FIELD offsets can still drift
# without tripping it. If THAT happens, the symptoms are:
#   - fuzz_process failures with nonsense values in fir_idx_* (> 39 or > 255)
#   - pending_mask invariants failing with bits set in positions we know
#     should be zero
# When triaged, re-compute these offsets via a C probe:
#     echo '#include "spu94_state_internal.h"
#           #include <stdio.h>
#           int main(void){
#             printf("pending_mask=%zu\n",
#                    offsetof(struct spu94_state, pending_mask));
#             printf("fir_idx_l_in=%zu\n",
#                    offsetof(struct spu94_state, fir_idx_l_in));
#             /* etc */
#           }' | gcc -I src/spu94 -x c -o /tmp/probe - && /tmp/probe
#
# Current layout (verified 2026-04-21 via offset probe; also re-verified
# at the start of Phase 6 Plan 5 Task 2 migration):
#   sizeof(struct spu94_state) = 544 bytes.
# ------------------------------------------------------------------------
PENDING_MASK_OFFSET = 160   # uint64_t; bound: (v >> 35) == 0
FIR_IDX_L_IN_OFFSET = 360   # uint8_t;  bound: v < FIR_DELAY_LEN
FIR_IDX_R_IN_OFFSET = 361   # uint8_t;  bound: v < FIR_DELAY_LEN
FIR_IDX_L_OUT_OFFSET = 518  # uint8_t;  bound: v < FIR_DELAY_LEN
FIR_IDX_R_OUT_OFFSET = 519  # uint8_t;  bound: v < FIR_DELAY_LEN

FIR_DELAY_LEN = 39           # ring index wraps in [0, FIR_DELAY_LEN); 39-tap FIR
PENDING_MASK_WIDTH = 35      # SPU94_REG__COUNT; pending_mask tracks at most 35 bits

# ------------------------------------------------------------------------
# CLI
# ------------------------------------------------------------------------
ap = argparse.ArgumentParser()
ap.add_argument("--steps", type=int, default=DEFAULT_STEPS)
ap.add_argument("--seed", type=lambda s: int(s, 0), default=GOLDEN_SEED)
args = ap.parse_args()

# Keep the existing SPU94_LIB gate — the binding has already loaded the
# library by this point, but we still assert the env var is set so a
# missing-env-var misconfiguration fails early with the Phase 5 error
# text rather than a confusing downstream trace.
LIB_PATH = os.environ.get("SPU94_LIB")
if not LIB_PATH:
    print("FAIL: SPU94_LIB env var required", file=sys.stderr)
    sys.exit(1)

lib = _lib  # shorthand; the binding owns the CDLL + argtypes.

# ------------------------------------------------------------------------
# Register partitions — derived from the live library via spu94_reg_type.
# spu94_reg_type returns 0 for I16 signed gain regs and 1 for U16
# unsigned address/delay regs. Replaces the hand-typed set from the
# pre-migration version (D-16).
# ------------------------------------------------------------------------
_SPU94_REG_TYPE_I16 = 0
I16_REGS = {
    int(r) for r in Register
    if lib.spu94_reg_type(int(r)) == _SPU94_REG_TYPE_I16
}
assert len(I16_REGS) == 12, (
    f"expected 12 I16 registers, got {len(I16_REGS)}"
)

# Convenience alias — the halfword-alignment invariant branches on
# mBASE by name rather than by number, so keep it symbolic.
SPU94_REG_mBASE = int(Register.mBASE)

# ------------------------------------------------------------------------
# Allocate caller state + work buffer (aligned; mirrors fuzz_fir.py)
# ------------------------------------------------------------------------
def _aligned_buffer(size: int, align: int):
    raw = (ctypes.c_ubyte * (size + align))()
    addr = ctypes.addressof(raw)
    offset = (align - (addr % align)) % align
    return raw, c_void_p(addr + offset), addr + offset


state_raw, state_ptr, state_addr = _aligned_buffer(
    SPU94_STATE_SIZE_MAX, SPU94_STATE_ALIGN_MAX
)
work_raw, work_ptr, _work_addr = _aligned_buffer(
    256 * 1024, SPU94_STATE_ALIGN_MAX
)
_keepalive = (state_raw, work_raw)

state = lib.spu94_init(state_ptr, SPU94_STATE_SIZE_MAX, work_ptr, 256 * 1024)
if not state:
    sys.exit("FAIL: spu94_init returned NULL")

# ------------------------------------------------------------------------
# Struct-offset guard (mirror fuzz_fir.py WR-02). If spu94_state grew or
# shrank, the highest documented offset must still lie inside the live
# struct footprint. Fail loudly with a diagnostic pointing at the fix.
# ------------------------------------------------------------------------
_actual_state_size = lib.spu94_state_size()
_max_peek_offset = max(PENDING_MASK_OFFSET + 8, FIR_IDX_R_OUT_OFFSET + 1)
if _max_peek_offset > _actual_state_size:
    sys.exit(
        f"FAIL: struct-offset guard: peek past offset {_max_peek_offset} "
        f"exceeds live sizeof(spu94_state)={_actual_state_size}. "
        f"Struct shrank or a documented field was removed. "
        f"Re-verify offsets in fuzz_process.py against "
        f"src/spu94/spu94_state_internal.h and update BOTH."
    )

# Live ctypes views into the struct for per-step invariant asserts.
_pending_mask_view = c_uint64.from_address(state_addr + PENDING_MASK_OFFSET)
_fir_idx_l_in_view = c_uint8.from_address(state_addr + FIR_IDX_L_IN_OFFSET)
_fir_idx_r_in_view = c_uint8.from_address(state_addr + FIR_IDX_R_IN_OFFSET)
_fir_idx_l_out_view = c_uint8.from_address(state_addr + FIR_IDX_L_OUT_OFFSET)
_fir_idx_r_out_view = c_uint8.from_address(state_addr + FIR_IDX_R_OUT_OFFSET)

# ------------------------------------------------------------------------
# Per-op implementations — Phase 6 Plan 5 D-16: numpy int16 arrays to
# satisfy the binding's ndpointer contract (D-09). Pre-migration used
# raw ctypes c_int16 arrays; swap is mechanical, same semantics.
# ------------------------------------------------------------------------
MAX_BLOCK = 4096
Lin = np.zeros(MAX_BLOCK, dtype=np.int16)
Rin = np.zeros(MAX_BLOCK, dtype=np.int16)
Lout = np.zeros(MAX_BLOCK, dtype=np.int16)
Rout = np.zeros(MAX_BLOCK, dtype=np.int16)


def fill_random_input(rng, n):
    # Bulk-fill via rng.randbytes() + numpy frombuffer copy. Roughly
    # equivalent to the pre-migration ctypes.memmove path in wall time —
    # numpy's write into the existing allocated slice still goes through
    # a C-level memcpy. Using .view + np.frombuffer keeps the whole
    # operation within a single allocation (no per-call np.zeros).
    raw = rng.randbytes(4 * n)
    # First n*2 bytes -> L; next n*2 bytes -> R.
    Lin[:n] = np.frombuffer(raw, dtype=np.int16, count=n, offset=0)
    Rin[:n] = np.frombuffer(raw, dtype=np.int16, count=n, offset=2 * n)


def op_write_i16_reg(rng):
    r = rng.choice(sorted(I16_REGS))
    v = rng.randint(-32768, 32767)
    lib.spu94_set_reg_i16(state, r, v)
    return ("w_i16", r, v)


def op_write_u16_reg(rng):
    u16_regs = [i for i in range(SPU94_REG__COUNT) if i not in I16_REGS]
    r = rng.choice(u16_regs)
    v = rng.randint(0, 65535)
    lib.spu94_set_reg_u16(state, r, v)
    return ("w_u16", r, v)


def op_process(rng):
    n = rng.randint(1, MAX_BLOCK)
    fill_random_input(rng, n)
    # ndpointer argtypes require sized slices that are themselves
    # C-contiguous. Numpy's basic slicing produces contiguous views for
    # the MAX_BLOCK-rooted arrays, so no copy.
    lib.spu94_process(state, Lin[:n], Rin[:n], Lout[:n], Rout[:n], n)
    # Invariant: every output sample within int16. numpy int16 already
    # enforces the domain at storage time; bulk min/max via numpy is
    # a C-level scan.
    lo_min = int(Lout[:n].min())
    lo_max = int(Lout[:n].max())
    ro_min = int(Rout[:n].min())
    ro_max = int(Rout[:n].max())
    if lo_min < -32768 or lo_max > 32767 or ro_min < -32768 or ro_max > 32767:
        raise AssertionError(
            f"output escape in {n}-sample block: "
            f"L=[{lo_min},{lo_max}] R=[{ro_min},{ro_max}]"
        )
    return ("process", n)


def op_flush(rng):
    n = rng.randint(1, MAX_BLOCK)
    lib.spu94_flush(state, Lout[:n], Rout[:n], n)
    lo_min = int(Lout[:n].min())
    lo_max = int(Lout[:n].max())
    ro_min = int(Rout[:n].min())
    ro_max = int(Rout[:n].max())
    if lo_min < -32768 or lo_max > 32767 or ro_min < -32768 or ro_max > 32767:
        raise AssertionError(
            f"flush output escape in {n}-sample drain: "
            f"L=[{lo_min},{lo_max}] R=[{ro_min},{ro_max}]"
        )
    return ("flush", n)


# Track the last-loaded preset so the "non-zero output after preset load"
# invariant can be amortized across the next few process calls. The
# integer counter is bumped on every process op and reset on every load;
# INFO-1: count how many contiguous post-load process calls produced
# all-zero output for non-Off presets so a degenerate "preset loads but
# never emits" regression surfaces.
_last_preset = [None, 0, 0]  # (preset_id, post_load_process_calls, contiguous_silent_calls)


_VLOUT_REG_ID = int(Register.vLOUT)
_VROUT_REG_ID = int(Register.vROUT)


def op_load_preset(rng):
    id_ = rng.randint(0, SPU94_PRESET__COUNT - 1)
    rv = lib.spu94_load_preset(state, id_)
    if rv != SPU94_OK:
        raise AssertionError(f"load_preset({id_}) returned {rv}")
    # HI-04: ADR-Phase-6-G gates the wet output on vLOUT/vROUT, and every
    # factory preset table leaves these at 0. Without an explicit write
    # the "non-Off preset -> non-zero output within 256 calls" invariant
    # below depends on a random op_write_i16_reg eventually picking
    # vLOUT/vROUT before the patience window elapses (~1-in-12 per I16-
    # write, ~1-in-5 op classes). Pin the contract deterministically:
    # after any non-Off load, set vLOUT/vROUT to full-scale so the
    # invariant tests the product claim, not a random-choice race.
    if id_ != 0:
        lib.spu94_set_reg_i16(state, _VLOUT_REG_ID, 0x7FFF)
        lib.spu94_set_reg_i16(state, _VROUT_REG_ID, 0x7FFF)
    _last_preset[0] = id_
    _last_preset[1] = 0
    _last_preset[2] = 0
    return ("load", id_)


# ------------------------------------------------------------------------
# Invariants post-op
# ------------------------------------------------------------------------
# Max contiguous all-zero-output process calls permitted after a non-Off
# preset load before we flag "preset loads but never emits". The reverb
# group delay + FIR group delay sum to roughly 58 samples; a block size
# as small as 1 could take 58 process calls to first non-zero. Scale up
# conservatively so small-block sequences don't flake. If the fuzz picks
# many small blocks in a row the counter still resets on the first
# non-zero sample.
_NONZERO_OUTPUT_PATIENCE_CALLS = 256


def check_global_invariants(op):
    # Buffer-address wrap invariant: addr & 1 == 0, OR addr equals mBASE
    # (halfword exception from Phase 2 Plan 05 mBASE-snap-on-write).
    addr = lib.spu94_get_buffer_address(state)
    mbase = lib.spu94_get_reg_u16(state, SPU94_REG_mBASE)
    if (addr & 1) != 0 and addr != mbase:
        raise AssertionError(
            f"buffer_address odd without mBASE alignment: "
            f"addr=0x{addr:x} mBASE=0x{mbase:x} after op={op}"
        )

    # FIR delay-line index bounds (D-10a item 4, 05-RESEARCH § Fuzz
    # Harness Integration Notes item 4). Each index is the next-write
    # position in the 39-slot ring; valid range is [0, FIR_DELAY_LEN).
    # Hand-synced offset peek mirrors the fuzz_fir.py CANARY_OFFSET
    # pattern.
    li_in = _fir_idx_l_in_view.value
    ri_in = _fir_idx_r_in_view.value
    li_out = _fir_idx_l_out_view.value
    ri_out = _fir_idx_r_out_view.value
    for name, v in (
        ("fir_idx_l_in", li_in),
        ("fir_idx_r_in", ri_in),
        ("fir_idx_l_out", li_out),
        ("fir_idx_r_out", ri_out),
    ):
        if v >= FIR_DELAY_LEN:
            raise AssertionError(
                f"{name}={v} escaped [0,{FIR_DELAY_LEN}) after op={op}"
            )

    # pending_mask width invariant (D-10a item 4). The mask tracks
    # register slots awaiting the next tick's active-commit; at most
    # SPU94_REG__COUNT=35 bits may be set. The top 29 bits of the
    # uint64 storage must be zero.
    pmask = _pending_mask_view.value
    if (pmask >> PENDING_MASK_WIDTH) != 0:
        raise AssertionError(
            f"pending_mask=0x{pmask:016x} has bit(s) set beyond bit "
            f"{PENDING_MASK_WIDTH-1} after op={op}"
        )

    # Preset-load non-zero-output invariant. After a non-Off load, at
    # least one process output sample must be non-zero within
    # _NONZERO_OUTPUT_PATIENCE_CALLS contiguous process calls. Reset the
    # counter on the first non-zero sample observed.
    if _last_preset[0] is not None and _last_preset[0] != 0 and op[0] == "process":
        _last_preset[1] += 1
        n = op[1]
        # numpy.ndarray.any() is a C-level bulk scan; short-circuits on
        # the first non-zero element it encounters.
        any_nonzero = bool(Lout[:n].any()) or bool(Rout[:n].any())
        if any_nonzero:
            _last_preset[2] = 0
        else:
            _last_preset[2] += 1
            if _last_preset[2] > _NONZERO_OUTPUT_PATIENCE_CALLS:
                raise AssertionError(
                    f"non-Off preset {_last_preset[0]} produced "
                    f"{_last_preset[2]} contiguous all-zero process calls "
                    f"post-load (patience={_NONZERO_OUTPUT_PATIENCE_CALLS})"
                )


# ------------------------------------------------------------------------
# Main loop
# ------------------------------------------------------------------------
rng = random.Random(args.seed)
ops = [op_write_i16_reg, op_write_u16_reg, op_process, op_flush, op_load_preset]
counts = {"w_i16": 0, "w_u16": 0, "process": 0, "flush": 0, "load": 0}

print(f"fuzz_process seed={args.seed:#010x} steps={args.steps}")
t0 = time.time()
step = -1
try:
    for step in range(args.steps):
        op_fn = rng.choice(ops)
        result = op_fn(rng)
        counts[result[0]] += 1
        check_global_invariants(result)
        if (step + 1) % 100_000 == 0:
            elapsed = time.time() - t0
            rate = (step + 1) / elapsed if elapsed > 0 else float("inf")
            print(
                f"step={step+1}/{args.steps} rate={rate:.0f}/s ops={counts}"
            )
except AssertionError as e:
    print(f"FAIL at step {step}: {e}", file=sys.stderr)
    lib.spu94_destroy(state)
    sys.exit(1)

elapsed = time.time() - t0
print(f"PASS: {args.steps} steps, {elapsed:.1f} s, ops={counts}")
lib.spu94_destroy(state)
sys.exit(0)
