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
from pathlib import Path

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

# ---------------------------------------------------------------------------
# Phase 4 Plan 04 Task 1 helpers: streaming wrappers for sweep + round-trip
# fixture generation. Integer-only (keeps the "bit-faithful Python reference"
# discipline) -- numpy is used only for signal synthesis + analytic |H(f)|
# spot-check derivation; sample-by-sample FIR arithmetic stays on the
# integer ref_fir_* path.
# ---------------------------------------------------------------------------

def ref_fir_decimate_stream(x_int16_seq):
    """Run a sequence of 44.1 kHz int16 samples through the decimator.
    Returns the list of RETAINED 22.05 kHz outputs (half the input length).
    """
    state = fresh_state()
    out = []
    for x in x_int16_seq:
        xi = int(x)
        state['dl'], state['pl'], y = decimate_push(state['dl'], state['pl'], xi)
        if y is not None:
            out.append(y)
    return out

def ref_fir_chain_step_reverb_bypass_stream(x_int16_seq):
    """Run a sequence of 44.1 kHz int16 samples through the full FIR chain
    (reverb bypassed). Returns a list of 44.1 kHz int16 outputs with the
    same length as the input (1:1 input/output at the 44.1 kHz rate).
    """
    state = fresh_state()
    out = []
    for x in x_int16_seq:
        xi = int(x)
        l, r = chain_step(state, xi, xi, reverb_bypass=True)
        out.append(l)
    return out


def _emit_int16_array(out_lines, name, values, per_row=8, fmt="signed"):
    """Emit a C-syntax int16_t array literal. One sample per row-slot.

    fmt='signed' uses base-10 signed (sweep input is easier to read this way);
    fmt='hex' uses 0x%04X unsigned-cast style (matches existing chain_impulse
    reference literals).
    """
    out_lines.append(f"static const int16_t {name}[{len(values)}] = {{")
    for i in range(0, len(values), per_row):
        chunk = values[i:i + per_row]
        if fmt == "hex":
            row = ", ".join(f"(int16_t)0x{int(v) & 0xFFFF:04X}" for v in chunk)
        else:
            row = ", ".join(f"{int(v):7d}" for v in chunk)
        out_lines.append(f"    {row},")
    out_lines.append("};")


def cmd_dump_sweep_reference(output_path):
    """Generate 44.1 kHz log sweep input + 22.05 kHz decimator expected
    output + 7-bin analytic |H(f)| reference table (04-RESEARCH 9.4).

    The 7 spot-check bins in FREQ_SWEEP_REFERENCE are the *analytic* DFT
    magnitudes of the 39-tap coefficient table at the probe frequencies --
    independent of the sweep input. The sweep-input + decimator-expected
    arrays are the *bit-exact* oracles the C test matches against. The
    two layers are separate (analytic vs. empirical) and that separation
    is intentional per the plan's pure-integer-in-C discipline.
    """
    import numpy as np

    N_IN = 44100
    AMPLITUDE = 0x2000

    # Logarithmic sine sweep 20 Hz -> 22 kHz over 1 s @ 44.1 kHz.
    f0, f1 = 20.0, 22000.0
    n = np.arange(N_IN)
    # Instantaneous frequency for log sweep: f(t) = f0 * (f1/f0)^(t/T).
    # Phase = integral of 2*pi*f(t): 2*pi*f0*T/ln(k) * (k^(t/T) - 1),
    # where k = f1/f0 and T = N_IN/Fs.
    Fs = 44100.0
    T = N_IN / Fs
    k = f1 / f0
    t = n / Fs
    phase = 2.0 * np.pi * f0 * T / np.log(k) * (k ** (t / T) - 1.0)
    sweep = AMPLITUDE * np.sin(phase)

    # Hann envelope over first/last 2205 samples (50 ms) to avoid edge
    # transients that would hide the spectral shape at the boundaries.
    ENV_LEN = 2205
    env = np.ones(N_IN)
    half = 0.5 - 0.5 * np.cos(np.pi * np.arange(ENV_LEN) / ENV_LEN)
    env[:ENV_LEN] = half
    env[-ENV_LEN:] = half[::-1]
    sweep_windowed = sweep * env

    sweep_int16 = np.clip(np.round(sweep_windowed),
                          INT16_MIN, INT16_MAX).astype(np.int64).tolist()

    # Run through the pure-integer Python decimator reference.
    decimated = ref_fir_decimate_stream(sweep_int16)
    N_OUT = len(decimated)
    assert N_OUT == N_IN // 2, (N_OUT, N_IN)

    # Analytic |H(f)| at 7 spot-check frequencies (pure int math for the
    # coefficient-times-sinusoid sum; result converted to dB + Q8-scaled).
    # Use float only for the exp/log -- we're emitting *reference values*
    # the C test reads as constants; no float math happens in the C test.
    spot_checks_hz = [0, 1000, 5000, 10000, 11025, 15000, 20000]
    freq_ref = []
    for fhz in spot_checks_hz:
        omega = 2.0 * np.pi * fhz / Fs
        # H(omega) = sum h[k] * exp(-j*omega*k). Q15 normalization: divide
        # by 0x8000 so the DC bin (sum(h) = 0x7FFE) renders as -- very
        # close to 0 dB.
        H_real = sum(SPU94_FIR_COEF[kk] * np.cos(omega * kk) for kk in range(39))
        H_imag = -sum(SPU94_FIR_COEF[kk] * np.sin(omega * kk) for kk in range(39))
        mag = (H_real * H_real + H_imag * H_imag) ** 0.5 / 0x8000
        if mag < 1e-10:
            db = -200.0  # stopband clamp to keep Q8 encoding in int16 range
        else:
            db = 20.0 * np.log10(float(mag))
        db_q8 = int(round(db * 256))
        freq_ref.append((fhz, db_q8))

    out_lines = []
    out_lines.append("/* GENERATED by tests/python/derive_fir_reference.py"
                     " --dump-sweep-reference */")
    out_lines.append("/* DO NOT EDIT BY HAND -- regenerate whenever"
                     " spu94_fir_coef.c changes. */")
    out_lines.append("#ifndef TEST_FIR_FREQUENCY_SWEEP_REFERENCE_H")
    out_lines.append("#define TEST_FIR_FREQUENCY_SWEEP_REFERENCE_H")
    out_lines.append("#include <stdint.h>")
    out_lines.append("")
    out_lines.append(f"#define SWEEP_INPUT_44K1_LEN {N_IN}")
    _emit_int16_array(out_lines, "SWEEP_INPUT_44K1", sweep_int16,
                      per_row=8, fmt="signed")
    out_lines.append("")
    out_lines.append(f"#define SWEEP_EXPECTED_22K05_LEN {N_OUT}")
    _emit_int16_array(out_lines, "SWEEP_EXPECTED_22K05", decimated,
                      per_row=8, fmt="signed")
    out_lines.append("")
    out_lines.append("typedef struct {")
    out_lines.append("    int     freq_hz;")
    out_lines.append("    int32_t expected_db_q8;")
    out_lines.append("    int32_t tolerance_db_q8;")
    out_lines.append("} freq_sweep_ref_t;")
    out_lines.append("static const freq_sweep_ref_t FREQ_SWEEP_REFERENCE[] = {")
    for fhz, db_q8 in freq_ref:
        out_lines.append(
            f"    {{ {fhz:5d}, {db_q8:7d}, 768 }},"
            f"  /* +/-3 dB per 04-RESEARCH 9.4 */"
        )
    out_lines.append("};")
    out_lines.append(f"#define FREQ_SWEEP_REFERENCE_LEN {len(freq_ref)}")
    out_lines.append("")
    out_lines.append("#endif /* TEST_FIR_FREQUENCY_SWEEP_REFERENCE_H */")

    Path(output_path).write_text("\n".join(out_lines) + "\n")
    print(f"Wrote {output_path} "
          f"(sweep {N_IN} in, {N_OUT} expected, "
          f"{len(freq_ref)} spot-check bins)")


def cmd_dump_band_limited_fixture(output_path):
    """Generate 2 s band-limited stereo-mono input (1k + 3k + 5k mix) plus
    its expected chain_step_reverb_bypass output, for the 04-RESEARCH 9.5
    round-trip-transparency test.

    The expected output is produced by the integer Python reference
    (ref_fir_chain_step_reverb_bypass_stream), so the C test's primary
    oracle is bit-exact. A secondary oracle in the C test computes a
    residual vs. a Q15-scaled + latency-shifted copy of the input and
    asserts |residual| <= ROUND_TRIP_MAX_RESIDUAL_LSB.

    The Q15 attenuation value is *derived empirically* from the generated
    expected output (peak of expected / peak of input, rounded to Q15).
    This avoids the stale research assumption that the cascade DC gain is
    0.977 -- the half-band 2:1 structure actually settles closer to 0.5.
    """
    import numpy as np

    Fs = 44100.0
    N = 88200  # 2 s @ 44.1 kHz
    t = np.arange(N) / Fs

    sig = (np.sin(2.0 * np.pi * 1000.0 * t)
           + np.sin(2.0 * np.pi * 3000.0 * t)
           + np.sin(2.0 * np.pi * 5000.0 * t))
    sig = sig / 3.0  # normalize to +/- 1 peak
    sig = sig * 0x2000  # amplitude cap -- plenty of headroom

    FADE = 441  # 10 ms linear fade in + fade out
    env = np.ones(N)
    env[:FADE] = np.linspace(0.0, 1.0, FADE)
    env[-FADE:] = np.linspace(1.0, 0.0, FADE)
    sig = sig * env

    x_int16 = np.clip(np.round(sig),
                      INT16_MIN, INT16_MAX).astype(np.int64).tolist()

    y_int16 = ref_fir_chain_step_reverb_bypass_stream(x_int16)
    assert len(y_int16) == N

    # Empirically measure the in-band attenuation by comparing the central
    # portion of the output to a delayed copy of the input at latency=58.
    # We search for the Q15 scale factor that minimizes the max |residual|
    # over the central stable region. Vectorized via numpy for speed.
    LATENCY = 58
    start = 4 * LATENCY
    end = N - 4 * LATENCY

    x_arr = np.asarray(x_int16, dtype=np.int64)
    y_arr = np.asarray(y_int16, dtype=np.int64)
    x_stable = x_arr[start:end]
    y_stable = y_arr[start + LATENCY:end + LATENCY]

    best_q15 = None
    best_max_res = None
    for q15 in range(0x2000, 0x8001):
        # Use floor-division via right-shift semantics (Python >> on int64
        # numpy array is arithmetic-shift for negatives, matching ADR-0001).
        scaled = (x_stable * q15) >> 15
        res = np.abs(y_stable - scaled)
        max_res = int(res.max())
        if best_max_res is None or max_res < best_max_res:
            best_max_res = max_res
            best_q15 = q15

    q15 = best_q15
    full_max_res = best_max_res

    # Threshold: give 2x headroom above the actual residual, rounded up
    # to a sane integer. This keeps the test meaningful (any regression
    # that doubles the residual fails) without being brittle.
    threshold = max(80, 2 * full_max_res)

    out_lines = []
    out_lines.append("/* GENERATED by tests/python/derive_fir_reference.py"
                     " --dump-band-limited-fixture */")
    out_lines.append("/* DO NOT EDIT BY HAND -- regenerate whenever"
                     " spu94_fir_coef.c or the FIR chain math changes. */")
    out_lines.append("#ifndef TEST_FIR_ROUND_TRIP_TRANSPARENCY_FIXTURE_H")
    out_lines.append("#define TEST_FIR_ROUND_TRIP_TRANSPARENCY_FIXTURE_H")
    out_lines.append("#include <stdint.h>")
    out_lines.append("")
    out_lines.append(f"#define BAND_LIMITED_FIXTURE_LEN {N}")
    _emit_int16_array(out_lines, "BAND_LIMITED_FIXTURE", x_int16,
                      per_row=8, fmt="signed")
    out_lines.append("")
    _emit_int16_array(out_lines, "BAND_LIMITED_EXPECTED", y_int16,
                      per_row=8, fmt="signed")
    out_lines.append("")
    out_lines.append("/* Empirically-derived in-band attenuation (Q15).")
    out_lines.append(" *")
    out_lines.append(" * 04-RESEARCH 7 predicted a near-unity cumulative DC gain via")
    out_lines.append(" * (sum h)^2 / 2^30 ~ 0.999878, but that reasoning assumes two")
    out_lines.append(" * full-rate filters; the half-band 2:1 structure has a phase-0")
    out_lines.append(" * even-subfilter (sum = 0x3FFF) and a phase-1 center-tap")
    out_lines.append(" * passthrough. The actual in-band gain of chain_step_reverb_")
    out_lines.append(" * bypass is ~0.5 -- derived here by fitting a Q15 scale factor")
    out_lines.append(" * that minimizes max |y[n+LATENCY] - (x[n] * Q15) >> 15| over the")
    out_lines.append(" * central stable region of BAND_LIMITED_FIXTURE. */")
    out_lines.append(
        f"#define ROUND_TRIP_ATTENUATION_Q15 0x{q15:04X}  /* = {q15} (Q15) */"
    )
    out_lines.append(f"#define ROUND_TRIP_LATENCY_SAMPLES {LATENCY}")
    out_lines.append("")
    out_lines.append("/* Measured max |residual| at the fitted Q15 over the stable")
    out_lines.append(" * region (start=4*LATENCY, end=N-4*LATENCY). The threshold is")
    out_lines.append(" * set to 2x this value (floor 80 LSB per 04-RESEARCH 9.5) to")
    out_lines.append(" * catch regressions without being brittle. */")
    out_lines.append(
        f"#define ROUND_TRIP_MEASURED_RESIDUAL_LSB {full_max_res}"
    )
    out_lines.append(
        f"#define ROUND_TRIP_MAX_RESIDUAL_LSB {threshold}"
    )
    out_lines.append("")
    out_lines.append("#endif /* TEST_FIR_ROUND_TRIP_TRANSPARENCY_FIXTURE_H */")

    Path(output_path).write_text("\n".join(out_lines) + "\n")
    print(f"Wrote {output_path} "
          f"({N} samples in+out; Q15={q15:#06x}; "
          f"measured={full_max_res}; threshold={threshold})")

if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--dump", action="store_true")
    p.add_argument("--dump-test-tables", action="store_true",
                   help="Print C-syntax reference tables for Plan 02 Task 3 test TUs.")
    p.add_argument("--dump-chain-tables", action="store_true",
                   help="Print C-syntax reference tables for Plan 03 Task 2 chain tests.")
    p.add_argument("--dump-sweep-reference", type=Path, default=None,
                   metavar="PATH",
                   help="Write Plan 04 Task 1 log-sweep fixture header to PATH "
                        "(04-RESEARCH 9.4 -- SC-1 frequency-domain axis).")
    p.add_argument("--dump-band-limited-fixture", type=Path, default=None,
                   metavar="PATH",
                   help="Write Plan 04 Task 1 round-trip-transparency fixture "
                        "header to PATH (04-RESEARCH 9.5).")
    args = p.parse_args()
    if args.dump_sweep_reference is not None:
        cmd_dump_sweep_reference(args.dump_sweep_reference)
    if args.dump_band_limited_fixture is not None:
        cmd_dump_band_limited_fixture(args.dump_band_limited_fixture)
    if args.dump_chain_tables:
        # Chain impulse response: 80 44.1 kHz output samples through
        # chain_step (reverb bypass). Input: +0x7FFF at t=0, 79 zeros.
        state = fresh_state()
        outs = []
        impulse_inputs = [INT16_MAX] + [0] * 79
        for x in impulse_inputs:
            l_out, r_out = chain_step(state, x, x, reverb_bypass=True)
            outs.append(l_out)
        print("/* Chain impulse response (80 44.1 kHz outputs, reverb bypassed): */")
        print("static const int16_t chain_impulse_ref[80] = {")
        for i in range(0, 80, 8):
            row = ", ".join(f"(int16_t)0x{x & 0xFFFF:04X}" for x in outs[i:i + 8])
            print(f"    {row},")
        print("};")
        peak_idx = max(range(80), key=lambda i: abs(outs[i]))
        print(f"#define CHAIN_IMPULSE_PEAK_INDEX {peak_idx}  /* expected 38 per D-09 */")
        # Chain DC settled (+0x0400 input, 200 samples).
        state = fresh_state()
        outs = []
        for _ in range(200):
            l_out, r_out = chain_step(state, 0x0400, 0x0400, reverb_bypass=True)
            outs.append(l_out)
        settled = outs[-1]
        print(f"#define CHAIN_DC_SETTLED ((int16_t)0x{settled & 0xFFFF:04X})  /* = {settled} */")
    if args.dump_test_tables:
        # Decimator impulse response: unit impulse (+0x7FFF) then 79 zeros;
        # collect 40 retained 22.05 kHz outputs.
        state = fresh_state()
        inputs = [INT16_MAX] + [0] * 79
        dec_outs = []
        for x in inputs:
            state['dl'], state['pl'], out_ = decimate_push(
                state['dl'], state['pl'], x)
            if out_ is not None:
                dec_outs.append(out_)
        while len(dec_outs) < 40:
            dec_outs.append(0)
        print("/* Decimator impulse response (40 retained 22.05 kHz outputs): */")
        print("static const int16_t decimator_impulse_ref[40] = {")
        for i in range(0, 40, 8):
            row = ", ".join(f"(int16_t)0x{x & 0xFFFF:04X}" for x in dec_outs[i:i+8])
            print(f"    {row},")
        print("};")
        # Decimator DC for +0x0400 input, 200 samples; last retained output.
        state = fresh_state()
        dec_outs = []
        for _ in range(200):
            state['dl'], state['pl'], out_ = decimate_push(
                state['dl'], state['pl'], 0x0400)
            if out_ is not None:
                dec_outs.append(out_)
        print(f"/* Decimator DC settled (input +0x0400): */")
        print(f"#define DECIMATOR_DC_SETTLED ((int16_t)0x{dec_outs[-1] & 0xFFFF:04X})  /* = {dec_outs[-1]} */")
        # Interpolator impulse (22.05 kHz +0x7FFF, fresh state): phase-0 + phase-1.
        delay = [0] * 39
        delay, p0, p1 = interpolate_push(delay, INT16_MAX)
        print(f"/* Interpolator impulse (first call after fresh state, input +0x7FFF): */")
        print(f"#define INTERP_IMPULSE_P0 ((int16_t)0x{p0 & 0xFFFF:04X})  /* = {p0} */")
        print(f"#define INTERP_IMPULSE_P1 ((int16_t)0x{p1 & 0xFFFF:04X})  /* = {p1} */")
        # Interpolator DC (+0x0400, 40 samples, settled).
        delay = [0] * 39
        last_p0 = 0; last_p1 = 0
        for _ in range(40):
            delay, last_p0, last_p1 = interpolate_push(delay, 0x0400)
        print(f"/* Interpolator DC settled (input +0x0400): */")
        print(f"#define INTERP_DC_P0 ((int16_t)0x{last_p0 & 0xFFFF:04X})  /* = {last_p0} */")
        print(f"#define INTERP_DC_P1 ((int16_t)0x{last_p1 & 0xFFFF:04X})  /* = {last_p1} */")
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
