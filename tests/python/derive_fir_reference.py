#!/usr/bin/env python3
"""tests/python/derive_fir_reference.py -- Phase 4 Plan 01 Task 2

Independent pure-Python integer reference implementation of the PS1 SPU
39-tap half-band FIR sample-rate converter. Pitfall 9 (extended from
Phase 3): NO GPL emulator source is in this script's derivation chain.
The 39 coefficient values are transcribed INDEPENDENTLY of the C file
src/spu94/spu94_fir_coef.c so that the Plan 04 bit-identity test
meaningfully cross-checks the C transcription against this one.

Source provenance: BIB-005 (psx-spx coefficient page) + BIB-006
(forums.bannister.org SCPH-5501 hardware readout) + BIB-007 (jsgroth
structural corroboration). See docs/BIBLIOGRAPHY.md.

Uncopyrightable-facts discipline (PROJECT.md + D-12): integer values
only; no prose copied.
"""
import argparse

INT16_MIN = -0x8000
INT16_MAX = 0x7FFF

# 39-tap half-band FIR coefficients (Q15 signed int16). Symmetric about
# index 19. Center tap 0x4000. Half-band Type I: odd-offset-from-center
# positions are zero. Independent transcription -- must match
# src/spu94/spu94_fir_coef.c byte-for-byte (bit-identity audit asserts).
SPU94_FIR_COEF = [
    -0x0001,  0x0000,  0x0002,  0x0000, -0x000A,  0x0000,  0x0023,  0x0000,
    -0x0067,  0x0000,  0x010A,  0x0000, -0x0268,  0x0000,  0x0534,  0x0000,
    -0x0B90,  0x0000,  0x2806,  0x4000,  0x2806,  0x0000, -0x0B90,  0x0000,
     0x0534,  0x0000, -0x0268,  0x0000,  0x010A,  0x0000, -0x0067,  0x0000,
     0x0023,  0x0000, -0x000A,  0x0000,  0x0002,  0x0000, -0x0001,
]
assert len(SPU94_FIR_COEF) == 39

def sat_s16(x: int) -> int:
    if x > INT16_MAX: return INT16_MAX
    if x < INT16_MIN: return INT16_MIN
    return x

def fir_literal(history):
    """Literal 39-multiply form. history[0] = newest, history[38] = oldest.
    Returns (output_int16, acc_int32, err_aggregate_int32).
    D-03 clamp-once: single shift + sat_s16 at the end; no per-multiply
    saturation. D-06 aggregate err-tap interpretation per
    04-RESEARCH Pattern 1 note.
    """
    assert len(history) == 39
    acc = 0
    for k in range(39):
        acc += SPU94_FIR_COEF[k] * history[k]
    # Python integer >> is arithmetic-floor for negatives = ASR (ADR-0001).
    shifted = acc >> 15
    err = acc - (shifted << 15)
    return sat_s16(shifted), acc, err

def fir_folded(history):
    """Folded form exploiting symmetry h[k]==h[38-k]. ~11 multiplies.
    Bit-identical to fir_literal under D-03 clamp-once (04-RESEARCH 8)."""
    assert len(history) == 39
    acc = SPU94_FIR_COEF[19] * history[19]  # center tap
    for k in range(19):
        c = SPU94_FIR_COEF[k]
        if c == 0:
            continue
        acc += c * (history[k] + history[38 - k])
    shifted = acc >> 15
    err = acc - (shifted << 15)
    return sat_s16(shifted), acc, err

def decimate_push(delay, phase, new_sample):
    """Decimator model: shift-register delay line (audit-clear).
    phase=0 -> produce retained 22.05 kHz output; phase=1 -> discard.
    Returns (new_delay, new_phase, output_or_None).
    """
    new_delay = [new_sample] + delay[:38]  # newest-first
    if phase == 0:
        out, _acc, _err = fir_literal(new_delay)
        return new_delay, 1, out
    else:
        return new_delay, 0, None

def interpolate_push(delay, new_sample):
    """Interpolator model: one 22.05 kHz sample in, two 44.1 kHz out.
    The PS1 reuses the same table for both phases (jsgroth's 'rather
    strange' observation, 04-RESEARCH Fact 2). Phase 0 = even-index-
    only subfilter; phase 1 = center-tap passthrough (= 0x4000 * x >> 15).
    """
    new_delay = [new_sample] + delay[:38]
    # Phase 0: even-offset subfilter (non-zero coefficients at even k).
    # For the half-band Type I table, those are k = 0, 2, 4, ..., 38.
    acc0 = 0
    for k in range(0, 39, 2):
        acc0 += SPU94_FIR_COEF[k] * new_delay[k]
    phase0 = sat_s16(acc0 >> 15)
    # Phase 1: odd-offset subfilter -- only k=19 is non-zero (center tap).
    acc1 = SPU94_FIR_COEF[19] * new_delay[19]
    phase1 = sat_s16(acc1 >> 15)
    return new_delay, phase0, phase1

def chain_step(state, l_in_44k1, r_in_44k1, reverb_bypass=True):
    """Internal 44.1 kHz chain. state is a dict with delay lines + phases.
    reverb_bypass=True passes the 22.05 kHz pair through unchanged;
    reverb_bypass=False calls a caller-supplied reverb hook (not
    modeled in this pure-Python reference -- Plan 04 imports and tests
    FIR chain directly, reverb witness comes from spu94_tick in C).
    Returns (l_out_44k1, r_out_44k1).
    """
    # Decimate L and R independently (D-08 separate state).
    state['dl'], state['pl'], dec_l = decimate_push(
        state['dl'], state['pl'], l_in_44k1)
    state['dr'], state['pr'], dec_r = decimate_push(
        state['dr'], state['pr'], r_in_44k1)
    # When the retained phase fires, dec_* is a 22.05 kHz sample pair.
    # When discarded, the interpolator emits the pre-existing tick's
    # phase-1 sample; otherwise it absorbs the new tick and emits phase-0.
    if dec_l is not None:
        if not reverb_bypass:
            raise NotImplementedError(
                "reverb hook belongs to C spu94_tick; Python reference "
                "is bypass-only.")
        state['il'], ph0_l, ph1_l = interpolate_push(state['il'], dec_l)
        state['ir'], ph0_r, ph1_r = interpolate_push(state['ir'], dec_r)
        state['pending_ph1_l'] = ph1_l
        state['pending_ph1_r'] = ph1_r
        return ph0_l, ph0_r
    else:
        # Emit the previously-stored phase-1 sample (Pitfall 7 ordering).
        return state.get('pending_ph1_l', 0), state.get('pending_ph1_r', 0)

def fresh_state():
    return {
        'dl': [0] * 39, 'dr': [0] * 39, 'pl': 0, 'pr': 0,
        'il': [0] * 39, 'ir': [0] * 39,
        'pending_ph1_l': 0, 'pending_ph1_r': 0,
    }

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--dump", action="store_true")
    args = p.parse_args()
    if args.dump:
        print("# Sum of coefficients (DC gain, Q15):", sum(SPU94_FIR_COEF),
              f"= 0x{sum(SPU94_FIR_COEF) & 0xFFFF:04X}")
        print("# Sum of |coefficients|:", sum(abs(c) for c in SPU94_FIR_COEF),
              f"= 0x{sum(abs(c) for c in SPU94_FIR_COEF):04X}")
        print("# Center tap:", f"0x{SPU94_FIR_COEF[19]:04X}")
        # Impulse response sample
        hist = [0] * 39
        hist[0] = INT16_MAX
        out, acc, err = fir_literal(hist)
        print(f"# Impulse at t=0 (literal): out={out} acc={acc} err={err}")
        out_f, acc_f, err_f = fir_folded(hist)
        print(f"# Impulse at t=0 (folded) : out={out_f} acc={acc_f} err={err_f}")
        assert out == out_f and acc == acc_f, "folded != literal on impulse"
        # Adversarial overflow-proof input (from 04-RESEARCH Test-Vector
        # Library test 3): x[k] = +32767 if coef[38-k] >= 0 else -32768.
        # The delay-line convention here is history[0]=newest; the filter
        # index k reads history[k]. Align x[k] to maximize |acc|.
        adv = [(INT16_MAX if SPU94_FIR_COEF[k] >= 0 else INT16_MIN)
               for k in range(39)]
        _out, acc_adv, _err = fir_literal(adv)
        print(f"# Adversarial input acc: {acc_adv} = 0x{acc_adv & 0xFFFFFFFF:08X}")
        print(f"# Expected bound 0x5CD2632E = 1557291822 per 04-RESEARCH 7")
