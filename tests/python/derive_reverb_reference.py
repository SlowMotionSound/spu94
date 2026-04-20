#!/usr/bin/env python3
"""tests/python/derive_reverb_reference.py — Phase 3 Plan 01 Task 3

Independent reference implementation of the nocash SPU reverb formula.

Pitfall 9 (GPL provenance): NO GPL emulator is in this script's
derivation chain. Every function cites the nocash pseudocode line it
paraphrases. This file is the source of truth the C stage tests cross-
check against (Phase 3 Plan 01 = input_scale, hard_clip, output_scale;
Plans 02/03 extend with same_iir, diff_iir, comb, apf1, apf2).

Primary source (paraphrased per DOCS-03): the SPU Reverb Formula
section of the nocash psx-spx hardware reference at
psx-spx.consoledev.net/soundprocessingunitspu/.
"""

from __future__ import annotations

import argparse

INT16_MIN = -0x8000
INT16_MAX = 0x7FFF


def sat_s16(x: int) -> int:
    """Saturate to int16 range."""
    if x > INT16_MAX:
        return INT16_MAX
    if x < INT16_MIN:
        return INT16_MIN
    return x


def q15_mul_truncate_with_err(a: int, b: int) -> tuple[int, int]:
    """ADR-0001: arithmetic right shift (ASR) truncation toward -infinity.

    Returns (sat_s16(product >> 15), remainder).

    Python's `>>` on a negative int is an arithmetic shift (floor toward
    -inf), matching the ASR semantic the C helpers rely on.
    """
    product = a * b
    shifted = product >> 15
    remainder = product - (shifted << 15)
    return sat_s16(shifted), remainder


def ref_input_scale(left_in: int, right_in: int,
                    vLIN: int, vRIN: int) -> tuple[int, int]:
    """Nocash: Lin = vLIN * LeftInput; Rin = vRIN * RightInput.

    SPU-94 emits the int32 widened product; the hard_clip stage in the
    next step applies sat_s16 (D-09). No shift here.
    """
    return left_in * vLIN, right_in * vRIN


def ref_hard_clip(Lin_wide: int, Rin_wide: int) -> tuple[int, int, int]:
    """D-09 + D-11 extension.

    Returns (sat_s16(L), sat_s16(R), overflow_magnitude) where the
    overflow magnitude is the sum of |x| - INT16_MAX for inputs whose
    magnitude exceeds INT16_MAX, zero otherwise.
    """
    L = sat_s16(Lin_wide)
    R = sat_s16(Rin_wide)
    l_abs = abs(Lin_wide)
    r_abs = abs(Rin_wide)
    lo = max(0, l_abs - INT16_MAX)
    ro = max(0, r_abs - INT16_MAX)
    return L, R, lo + ro


def ref_output_scale(Lout: int, Rout: int,
                     vLOUT: int, vROUT: int) -> tuple[int, int, int]:
    """Nocash: LeftOutput = Lout*vLOUT; RightOutput = Rout*vROUT.

    SPU-94 uses q15_mul_truncate_with_err per D-11 scope (i), so the
    truncation remainder is observable. Returns (L, R, err_sum).
    """
    L, err_l = q15_mul_truncate_with_err(Lout, vLOUT)
    R, err_r = q15_mul_truncate_with_err(Rout, vROUT)
    return L, R, err_l + err_r


# Plans 02/03 will add: ref_same_iir, ref_diff_iir, ref_comb,
# ref_apf1, ref_apf2. Each must cite the nocash line it paraphrases and
# keep derivation independent of any GPL emulator source (Pitfall 9).


# --- Buffer helpers shared by Plan 02 + Plan 03 stage references ------
# The work buffer is modeled as a dict keyed by *byte offset*, holding
# int16 values. Address arithmetic mirrors the Phase 2 Plan 04 wrap:
#     byte_off = (buffer_address + halfword_offset * 2) & 0x7FFFE
# A missing key reads as 0 (buffer is implicitly zero-initialized).
# Halfword offsets are unsigned 16-bit; subtraction wraps mod 2^16, so
# callers mask with 0xFFFF when subtracting (e.g. `(mLSAME - 2) & 0xFFFF`).

def _buf_read(buf: dict, buffer_address: int, halfword_offset: int) -> int:
    byte_off = (buffer_address + (halfword_offset & 0xFFFF) * 2) & 0x7FFFE
    return buf.get(byte_off, 0)


def _buf_write(buf: dict, buffer_address: int, halfword_offset: int,
               value: int) -> None:
    byte_off = (buffer_address + (halfword_offset & 0xFFFF) * 2) & 0x7FFFE
    buf[byte_off] = sat_s16(value)


def ref_same_iir(buf: dict, Lin: int, Rin: int, vIIR: int, vWALL: int,
                 regs: dict, buffer_address: int = 0) -> tuple[dict, int]:
    """Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
        [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]
        [mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]

    Each Q15 multiply uses q15_mul_truncate_with_err (ADR-0001 ASR
    truncation; pre-saturation remainder feeds err accumulator per D-11
    scope (i)).  D-10: if vIIR == -0x8000 (INT16_MIN), the FINAL result
    (the value written to memory) is negated — after saturation, before
    store. The negation itself is protected by sat_s16 to avoid
    INT16_MIN-negation UB (Pitfall 1).

    Returns (new_buf_dict, err_total).  The err total is pre-saturation
    truncation remainder summed over the four Q15 multiplies (2 wall + 2
    iir) for L+R sides.
    """
    new_buf = dict(buf)
    err_total = 0

    for Xin, dXSAME_key, mXSAME_key in (
        (Lin, 'dLSAME', 'mLSAME'),
        (Rin, 'dRSAME', 'mRSAME'),
    ):
        dX = regs[dXSAME_key]
        mX = regs[mXSAME_key]
        tap_d    = _buf_read(new_buf, buffer_address, dX)
        tap_prev = _buf_read(new_buf, buffer_address, (mX - 2) & 0xFFFF)

        wall_prod, err = q15_mul_truncate_with_err(tap_d, vWALL)
        err_total += err

        acc = sat_s16(Xin + wall_prod)
        # Subtract tap_prev with INT16_MIN-negation guard (Pitfall 1):
        acc = sat_s16(acc + sat_s16(-tap_prev))

        iir_prod, err = q15_mul_truncate_with_err(acc, vIIR)
        err_total += err

        result = sat_s16(iir_prod + tap_prev)

        # D-10 anomaly branch: AFTER saturation, BEFORE memory write
        # (Pitfall 5). sat_s16 guards INT16_MIN-negation UB (Pitfall 1).
        if vIIR == INT16_MIN:
            result = sat_s16(-result)

        _buf_write(new_buf, buffer_address, mX, result)

    return new_buf, err_total


def ref_diff_iir(buf: dict, Lin: int, Rin: int, vIIR: int, vWALL: int,
                 regs: dict, buffer_address: int = 0) -> tuple[dict, int]:
    """Nocash E1 (paraphrased):
        [mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]  ;R-to-L
        [mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]  ;L-to-R

    Structurally identical to ref_same_iir but with the cross-side wall
    tap pairing (dRDIFF feeds the L-side write to mLDIFF; dLDIFF feeds
    the R-side write to mRDIFF). D-10 anomaly applies at each write.
    Returns (new_buf_dict, err_total).
    """
    new_buf = dict(buf)
    err_total = 0

    # L side: Lin + [dRDIFF]*vWALL -> mLDIFF (cross-side: R tap feeds L).
    # R side: Rin + [dLDIFF]*vWALL -> mRDIFF (cross-side: L tap feeds R).
    for Xin, dX_cross_key, mX_key in (
        (Lin, 'dRDIFF', 'mLDIFF'),
        (Rin, 'dLDIFF', 'mRDIFF'),
    ):
        dX = regs[dX_cross_key]
        mX = regs[mX_key]
        tap_d    = _buf_read(new_buf, buffer_address, dX)
        tap_prev = _buf_read(new_buf, buffer_address, (mX - 2) & 0xFFFF)

        wall_prod, err = q15_mul_truncate_with_err(tap_d, vWALL)
        err_total += err

        acc = sat_s16(Xin + wall_prod)
        acc = sat_s16(acc + sat_s16(-tap_prev))

        iir_prod, err = q15_mul_truncate_with_err(acc, vIIR)
        err_total += err

        result = sat_s16(iir_prod + tap_prev)

        if vIIR == INT16_MIN:
            result = sat_s16(-result)

        _buf_write(new_buf, buffer_address, mX, result)

    return new_buf, err_total


def ref_comb(buf: dict, regs: dict,
             vCOMB1: int, vCOMB2: int, vCOMB3: int, vCOMB4: int,
             buffer_address: int = 0) -> tuple[int, int, int]:
    """Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
        Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2]
             + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
        Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2]
             + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]

    D-07 LOCKED: cascading sat_s16 after each add (NOT int32 accumulate).
    The 4-tap sum clamps after every intermediate add; each q15_add_sat
    is one clamp point, producing three cascading clamps per side. This
    diverges from the DuckStation / Mednafen-PSX witnesses (which use
    int32 accumulate) per the user's taste-driven decision in CONTEXT.md.

    Each Q15 multiply uses q15_mul_truncate_with_err (ADR-0001 ASR
    truncation; pre-saturation remainder feeds err accumulator per D-11
    scope (i)).

    Returns (Lout, Rout, err_total).  err_total sums pre-saturation
    truncation remainders across all 8 multiplies (L+R x 4 taps each).
    """
    err_total = 0

    def _side(tap_keys: tuple[str, ...]) -> int:
        """Run one side's 4-tap cascading sum. Closes over err_total."""
        nonlocal err_total
        taps = [_buf_read(buf, buffer_address, regs[k]) for k in tap_keys]
        vs = (vCOMB1, vCOMB2, vCOMB3, vCOMB4)
        prods = []
        for v, t in zip(vs, taps):
            p, err = q15_mul_truncate_with_err(v, t)
            err_total += err
            prods.append(p)
        # D-07 cascading sat: acc = p1; acc = sat(acc + p2); acc = sat(acc + p3); acc = sat(acc + p4).
        acc = prods[0]
        acc = sat_s16(acc + prods[1])  # sat #1
        acc = sat_s16(acc + prods[2])  # sat #2
        acc = sat_s16(acc + prods[3])  # sat #3
        return acc

    Lout = _side(('mLCOMB1', 'mLCOMB2', 'mLCOMB3', 'mLCOMB4'))
    Rout = _side(('mRCOMB1', 'mRCOMB2', 'mRCOMB3', 'mRCOMB4'))
    return Lout, Rout, err_total


def _ref_apf_side(buf: dict, buffer_address: int,
                  Xin: int, vAPF: int, mX: int, dAPF: int) -> tuple[int, int]:
    """One side of an APF stage (L or R). Returns (Xout, err_total).

    Nocash APF recurrence (paraphrased):
        step1 = Xin - vAPF*[mX-dAPF]           ;Pitfall 1 guard on subtract
        [mX] = step1                            ;store intermediate
        step3 = step1*vAPF + [mX-dAPF]         ;feedback output
    The delayed tap [mX-dAPF] is read once and reused.
    """
    err_total = 0
    tap_offset = (mX - dAPF) & 0xFFFF
    tap_delayed = _buf_read(buf, buffer_address, tap_offset)

    prod1, err = q15_mul_truncate_with_err(vAPF, tap_delayed)
    err_total += err

    # Step 1: Xin - prod1. Pitfall-1 guard via sat_s16 on the negation.
    step1 = sat_s16(Xin + sat_s16(-prod1))

    # Step 2: [mX] = step1.
    _buf_write(buf, buffer_address, mX, step1)

    # Step 3: step1 * vAPF + tap_delayed.
    prod2, err = q15_mul_truncate_with_err(step1, vAPF)
    err_total += err
    step3 = sat_s16(prod2 + tap_delayed)
    return step3, err_total


def ref_apf1(buf: dict, Lin: int, Rin: int, vAPF1: int, dAPF1: int,
             regs: dict, buffer_address: int = 0) -> tuple[int, int, dict, int]:
    """Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
        Lout = Lout - vAPF1*[mLAPF1-dAPF1], [mLAPF1] = Lout, Lout = Lout*vAPF1 + [mLAPF1-dAPF1]
        Rout = Rout - vAPF1*[mRAPF1-dAPF1], [mRAPF1] = Rout, Rout = Rout*vAPF1 + [mRAPF1-dAPF1]

    Pitfall 1: the subtract widens to int32 and sat_s16's before the add.
    Pitfall 7: tested by the INT16_MIN-triple edge case in test_reverb_apf1.c.
    D-11 scope (i): 2 multiplies per side; 4 per stage call.

    Returns (Lout, Rout, new_buf_dict, err_total).
    """
    new_buf = dict(buf)
    Lout, eL = _ref_apf_side(new_buf, buffer_address,
                             Lin, vAPF1, regs['mLAPF1'], dAPF1)
    Rout, eR = _ref_apf_side(new_buf, buffer_address,
                             Rin, vAPF1, regs['mRAPF1'], dAPF1)
    return Lout, Rout, new_buf, eL + eR


def ref_apf2(buf: dict, Lin: int, Rin: int, vAPF2: int, dAPF2: int,
             regs: dict, buffer_address: int = 0) -> tuple[int, int, dict, int]:
    """Structurally identical to ref_apf1 with vAPF2 / dAPF2 / mLAPF2 / mRAPF2.

    Returns (Lout, Rout, new_buf_dict, err_total).
    """
    new_buf = dict(buf)
    Lout, eL = _ref_apf_side(new_buf, buffer_address,
                             Lin, vAPF2, regs['mLAPF2'], dAPF2)
    Rout, eR = _ref_apf_side(new_buf, buffer_address,
                             Rin, vAPF2, regs['mRAPF2'], dAPF2)
    return Lout, Rout, new_buf, eL + eR


def _fmt_signed_hex(x: int) -> str:
    return f"-{abs(x):#x}" if x < 0 else f"{x:#x}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Dump reference test tables for the Phase 3 Plan 01 "
                    "reverb stages (input_scale, hard_clip, output_scale)."
    )
    parser.add_argument("--dump", action="store_true",
                        help="Print hand-derived test tables.")
    args = parser.parse_args()

    if not args.dump:
        parser.print_help()
        return 0

    print("# hard_clip test table:")
    hard_clip_inputs = [
        (0, 0),
        (INT16_MAX, INT16_MAX),
        (INT16_MIN, INT16_MIN),
        (0x10000, 0),
        (-0x10000, 0),
        (0x10000, -0x10000),
        (0x7FFFFFFF, -0x80000000),
    ]
    for l, r in hard_clip_inputs:
        out = ref_hard_clip(l, r)
        print(f"  hard_clip({_fmt_signed_hex(l)}, {_fmt_signed_hex(r)}) = "
              f"(L={_fmt_signed_hex(out[0])}, R={_fmt_signed_hex(out[1])}, "
              f"overflow={out[2]:#x})")

    print()
    print("# input_scale test table:")
    input_scale_inputs = [
        (0, 0, 0x1234, -0x5678),
        (INT16_MAX, INT16_MAX, 1, 1),
        (INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX),
        (INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN),
        (INT16_MIN, 0, INT16_MAX, 0),
    ]
    for l, r, vl, vr in input_scale_inputs:
        out = ref_input_scale(l, r, vl, vr)
        print(f"  input_scale(left={_fmt_signed_hex(l)}, right={_fmt_signed_hex(r)}, "
              f"vLIN={_fmt_signed_hex(vl)}, vRIN={_fmt_signed_hex(vr)}) = "
              f"(Lin={_fmt_signed_hex(out[0])}, Rin={_fmt_signed_hex(out[1])})")

    print()
    print("# output_scale test table:")
    output_scale_inputs = [
        (0, 0, 0x1234, -0x5678),
        (0x1234, -0x5678, 0, 0),
        (INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX),
        (INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN),
        (INT16_MAX, INT16_MIN, 0, 0),
    ]
    for lo, ro, vl, vr in output_scale_inputs:
        out = ref_output_scale(lo, ro, vl, vr)
        print(f"  output_scale(Lout={_fmt_signed_hex(lo)}, Rout={_fmt_signed_hex(ro)}, "
              f"vLOUT={_fmt_signed_hex(vl)}, vROUT={_fmt_signed_hex(vr)}) = "
              f"(L={_fmt_signed_hex(out[0])}, R={_fmt_signed_hex(out[1])}, "
              f"err_sum={_fmt_signed_hex(out[2])})")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
