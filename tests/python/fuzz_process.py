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
"""
import argparse
import ctypes
import os
import random
import sys
import time
from ctypes import (
    c_int16,
    c_int,
    c_uint8,
    c_uint16,
    c_uint32,
    c_uint64,
    c_size_t,
    c_void_p,
    POINTER,
)

GOLDEN_SEED = 0x05F05EED
DEFAULT_STEPS = 1_000_000

# ------------------------------------------------------------------------
# Hand-synced struct offsets (mirror fuzz_fir.py CANARY_OFFSET pattern +
# fuzz_buffer.py hand-synced register ID pattern). Offsets computed from
# src/spu94/spu94_state_internal.h end-of-Phase-4 layout via a small C
# probe. A runtime guard below (lib.spu94_state_size() bound check) fails
# loudly if the struct shifts before Phase 6 auto-derives these via
# ctypes.Structure.
#
# If these assertions fire, re-read spu94_state_internal.h, recompute the
# offsets via offsetof() in a small C probe, and update BOTH this file
# AND the <interfaces> block of 05-05-PLAN.md. Never silently adjust.
#
# Current layout (verified 2026-04-21 via offset probe):
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

LIB_PATH = os.environ.get("SPU94_LIB")
if not LIB_PATH:
    print("FAIL: SPU94_LIB env var required", file=sys.stderr)
    sys.exit(1)

lib = ctypes.CDLL(LIB_PATH)

# ------------------------------------------------------------------------
# C signatures (mirror include/spu94/spu94.h + spu94_registers.h)
# ------------------------------------------------------------------------

SPU94_OK = 0
SPU94_UNKNOWN_REG = 2
SPU94_TYPE_MISMATCH = 3

SPU94_STATE_SIZE_MAX = 16384
SPU94_STATE_ALIGN_MAX = 16
SPU94_REG__COUNT = 35
SPU94_PRESET__COUNT = 10

# Register type classifier: mirrors Phase 2's signedness table.
# I16 family: 12 v-prefix regs (enum indices 0, 1, 5, 6, 7, 8, 9, 10, 11,
# 12, 33, 34). U16 family: 23 d-prefix / m-prefix + mBASE (enum indices
# 2, 3, 4, 13..32). Hand-synced from include/spu94/spu94_registers.h.
I16_REGS = {0, 1, 5, 6, 7, 8, 9, 10, 11, 12, 33, 34}

# mBASE enum index for the "buffer_address even OR equals mBASE" check.
SPU94_REG_mBASE = 2

lib.spu94_state_size.restype = c_size_t
lib.spu94_state_size.argtypes = []

lib.spu94_init.restype = c_void_p
lib.spu94_init.argtypes = [c_void_p, c_size_t, c_void_p, c_size_t]

lib.spu94_reset.restype = None
lib.spu94_reset.argtypes = [c_void_p]

lib.spu94_destroy.restype = None
lib.spu94_destroy.argtypes = [c_void_p]

lib.spu94_tick.restype = None
lib.spu94_tick.argtypes = [c_void_p]

lib.spu94_process.restype = None
lib.spu94_process.argtypes = [
    c_void_p,
    POINTER(c_int16), POINTER(c_int16),
    POINTER(c_int16), POINTER(c_int16),
    c_uint32,
]

lib.spu94_flush.restype = None
lib.spu94_flush.argtypes = [
    c_void_p,
    POINTER(c_int16), POINTER(c_int16),
    c_uint32,
]

lib.spu94_load_preset.restype = c_int
lib.spu94_load_preset.argtypes = [c_void_p, c_int]

lib.spu94_set_reg_i16.restype = c_int
lib.spu94_set_reg_i16.argtypes = [c_void_p, c_int, c_int16]

lib.spu94_set_reg_u16.restype = c_int
lib.spu94_set_reg_u16.argtypes = [c_void_p, c_int, c_uint16]

lib.spu94_get_reg_i16.restype = c_int16
lib.spu94_get_reg_i16.argtypes = [c_void_p, c_int]

lib.spu94_get_reg_u16.restype = c_uint16
lib.spu94_get_reg_u16.argtypes = [c_void_p, c_int]

lib.spu94_get_buffer_address.restype = c_uint32
lib.spu94_get_buffer_address.argtypes = [c_void_p]

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
# Per-op implementations
# ------------------------------------------------------------------------
MAX_BLOCK = 4096
Lin = (c_int16 * MAX_BLOCK)()
Rin = (c_int16 * MAX_BLOCK)()
Lout = (c_int16 * MAX_BLOCK)()
Rout = (c_int16 * MAX_BLOCK)()


def fill_random_input(rng, n):
    # Bulk-fill via rng.randbytes() + ctypes.memmove for a single C-level
    # copy. Roughly 10x faster than a Python-level per-sample assignment
    # loop on 1k+ samples -- avoids both the interpreter overhead AND the
    # per-index bounds check on the c_int16 array setitem path.
    #
    # randbytes returns bytes in native order; on little-endian hosts the
    # bytes map 1:1 to int16 LE. All M1 targets (x86-64, Cortex-M7 LE,
    # Cortex-A ARM little) are little-endian so the direct memmove is
    # correct without byte-swap.
    #
    # 2 bytes per int16 sample per channel = 4*n bytes total (L then R).
    raw = rng.randbytes(4 * n)
    ctypes.memmove(Lin, raw, 2 * n)
    ctypes.memmove(Rin, raw[2 * n:], 2 * n)


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
    lib.spu94_process(state, Lin, Rin, Lout, Rout, n)
    # Invariant: every output sample within int16. ctypes.c_int16 enforces
    # the C-type bound already (wrap-on-assign); the check confirms no
    # out-of-domain value escapes via the readback. Values read as Python
    # ints already fall in [-32768, 32767] by definition of c_int16, but
    # we still assert defensively to catch ctypes-driver misbinding.
    # Use slice min/max (C-level bulk ops) instead of per-sample compare.
    L_slice = Lout[:n]
    R_slice = Rout[:n]
    lo_min, lo_max = min(L_slice), max(L_slice)
    ro_min, ro_max = min(R_slice), max(R_slice)
    if lo_min < -32768 or lo_max > 32767 or ro_min < -32768 or ro_max > 32767:
        raise AssertionError(
            f"output escape in {n}-sample block: "
            f"L=[{lo_min},{lo_max}] R=[{ro_min},{ro_max}]"
        )
    return ("process", n)


def op_flush(rng):
    n = rng.randint(1, MAX_BLOCK)
    lib.spu94_flush(state, Lout, Rout, n)
    L_slice = Lout[:n]
    R_slice = Rout[:n]
    lo_min, lo_max = min(L_slice), max(L_slice)
    ro_min, ro_max = min(R_slice), max(R_slice)
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


def op_load_preset(rng):
    id_ = rng.randint(0, SPU94_PRESET__COUNT - 1)
    rv = lib.spu94_load_preset(state, id_)
    if rv != SPU94_OK:
        raise AssertionError(f"load_preset({id_}) returned {rv}")
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
        # any() over a slice is C-level bulk; short-circuits on first nonzero.
        any_nonzero = any(Lout[:n]) or any(Rout[:n])
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
