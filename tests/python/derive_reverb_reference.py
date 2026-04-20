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
