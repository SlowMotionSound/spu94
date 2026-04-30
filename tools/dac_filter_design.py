#!/usr/bin/env python3
"""tools/dac_filter_design.py -- Phase 5 Plan 01 (DAC-FILT-01)

AK4309 8x digital interpolation filter design, verification, and
coefficient export.  Three cascaded 2x half-band FIR stages designed
with Parks-McClellan (scipy.signal.remez) to meet AK4309B datasheet
specs:

  - Passband ripple:       +/-0.05 dB  (0.10 dB peak-to-peak)
  - Stopband attenuation:  >= 41 dB
  - Response at 20 kHz:    >= -0.2 dB

Usage:
  --verify     Run pass/fail assertions, print results, exit 0/1
  --plot       Generate frequency response PNG (requires matplotlib)
  --export-c   Print Q15 coefficient arrays in C int16_t format

References: AK4309B datasheet (AllDatasheet), Phase 5 CONTEXT.md D-01
through D-13, 05-RESEARCH.md empirical design exploration.
"""

import argparse
import sys

import numpy as np
import scipy.signal as sig

# === Design parameters (D-01, D-03) ===
FS_AUDIO = 44100
F_PASS = 20000  # Audio passband edge

# Stage operating sample rates (rate AFTER 2x upsample)
FS_STAGE1 = 2 * FS_AUDIO   # 88200
FS_STAGE2 = 4 * FS_AUDIO   # 176400
FS_STAGE3 = 8 * FS_AUDIO   # 352800

# Minimum-order cascade meeting all datasheet specs (D-03, 05-RESEARCH.md)
N_STAGE1 = 55  # Narrow transition band dominates
N_STAGE2 = 11  # Moderate transition
N_STAGE3 = 7   # Trivial -- very wide transition


def design_halfband_stage(ntaps, fs_operating, f_passband):
    """Design one 2x half-band interpolation stage via Parks-McClellan.

    Args:
        ntaps: Filter length (odd, Type I linear-phase)
        fs_operating: Sample rate AFTER this stage's 2x upsample
        f_passband: Audio passband edge (Hz)

    Returns:
        h: FIR coefficient array (ntaps,)
    """
    f_pass_norm = f_passband / fs_operating
    f_stop_norm = (fs_operating / 2 - f_passband) / fs_operating
    h = sig.remez(ntaps,
                  [0, f_pass_norm, f_stop_norm, 0.5],
                  [1, 0],
                  weight=[1, 1])
    # Enforce exact half-band zeros (Pitfall 4: numerical noise from remez).
    # For a half-band filter of length 4m+3, all odd-indexed coefficients
    # (except the center tap) should be exactly zero.  remez leaves residuals
    # up to ~1e-5; zero them explicitly.
    center = ntaps // 2
    for i in range(1, ntaps, 2):  # odd indices
        if i != center:
            h[i] = 0.0
    return h


def measure_stage(h, fs_operating, f_passband):
    """Measure individual stage performance.

    Returns dict with ripple, attenuation, and non-zero count.
    """
    w, H = sig.freqz(h, worN=8192, fs=fs_operating)
    H_db = 20 * np.log10(np.abs(H) + 1e-15)
    pb = w <= f_passband
    sb = w >= (fs_operating / 2 - f_passband)
    ripple_pp = float(np.max(H_db[pb]) - np.min(H_db[pb]))
    atten = float(-np.max(H_db[sb]))
    n_nonzero = int(np.count_nonzero(h))
    return {
        'ripple_pp_dB': ripple_pp,
        'stopband_atten_dB': atten,
        'n_nonzero': n_nonzero,
    }


def build_composite(h1, h2, h3):
    """Upsample and convolve three stages into a single composite response.

    h1 at 2x, h2 at 4x, h3 at 8x -> composite at 8x (352800 Hz).
    """
    # Upsample h1 by 4 (insert 3 zeros between each sample)
    h1_up = np.zeros(len(h1) * 4 - 3)
    h1_up[::4] = h1
    # Upsample h2 by 2 (insert 1 zero between each sample)
    h2_up = np.zeros(len(h2) * 2 - 1)
    h2_up[::2] = h2
    # h3 already at final rate
    return np.convolve(np.convolve(h1_up, h2_up), h3)


def verify_cascade(h_composite, fs_final, f_pass, fs_audio):
    """Verify composite cascade against AK4309B datasheet specs.

    Returns dict with measurements and pass/fail booleans.
    """
    w, H = sig.freqz(h_composite, worN=16384, fs=fs_final)
    H_db = 20 * np.log10(np.abs(H) + 1e-15)
    dc_gain = H_db[0]
    H_db_norm = H_db - dc_gain  # Normalize to 0 dB at DC

    passband = w <= f_pass
    # Stopband: Stage 1's stopband edge is fs_stage1/2 - f_pass = 24100 Hz.
    # The transition band (20000-24100 Hz) is NOT stopband -- the datasheet's
    # 41 dB spec applies to the rejection of interpolation images, which are
    # centered at multiples of fs_audio (44100, 88200, ...) and fall in the
    # stopband of each cascaded stage.
    f_stop_stage1 = fs_audio - f_pass  # 44100 - 20000 = 24100 Hz
    stopband = w >= f_stop_stage1

    ripple_pp = float(np.max(H_db_norm[passband]) - np.min(H_db_norm[passband]))
    atten = float(-np.max(H_db_norm[stopband]))
    at_20k = float(H_db_norm[np.argmin(np.abs(w - 20000))])

    return {
        'ripple_pp_dB': ripple_pp,
        'ripple_pm_dB': ripple_pp / 2,
        'stopband_atten_dB': atten,
        'response_at_20kHz_dB': at_20k,
        'dc_gain_dB': float(dc_gain),
        'pass_ripple': ripple_pp <= 0.10,
        'pass_stopband': atten >= 41.0,
        'pass_20kHz': at_20k >= -0.2,
        'w': w,
        'H_db_norm': H_db_norm,
    }


def quantize_to_q15(h):
    """Quantize float coefficients to Q15 int16 values.

    Returns (h_q15_int, h_q15_float) where h_q15_int is the integer
    array and h_q15_float is the float reconstruction for re-verification.
    """
    h_q15 = np.round(h * 32768).astype(np.int64)
    h_q15 = np.clip(h_q15, -32768, 32767)
    h_q15_float = h_q15.astype(np.float64) / 32768.0
    return h_q15, h_q15_float


# === Naive 8x zero-stuff cascade simulation (D-04) ===
# Python equivalents of the C helpers in spu94_dac_fir.c, using integer
# arithmetic to match the Q15 fixed-point C implementation exactly.


def sat_s16(value):
    """Saturate to int16 range [-32768, 32767]. Matches C sat_s16."""
    return max(-32768, min(32767, int(value)))


def dac_fir_push_py(delay, idx, sample, ntaps):
    """Python equivalent of C dac_fir_push.

    delay: numpy int16 array (circular buffer)
    idx: int (current write position)
    sample: int (int16 value to push)
    ntaps: int (buffer length)

    Returns: new idx after push.
    """
    delay[idx] = np.int16(sample)
    return (idx + 1) % ntaps


def dac_fir_read_tap_py(delay, idx, k, ntaps):
    """Python equivalent of C dac_fir_read_tap.

    Read logical tap k (0 = newest, ntaps-1 = oldest) from circular buffer.
    idx points at the next-to-write (oldest) slot.

    Returns: int16 value.
    """
    pos = (idx + ntaps - 1 - k) % ntaps
    return int(delay[pos])


def dac_fir_stage_apply_py(delay, idx, ntaps, coef, pairs):
    """Python equivalent of C dac_fir_stage_apply.

    Folded-form evaluation with symmetric pair pre-addition.
    Uses int32 accumulator matching C's int32_t acc.

    delay: numpy int16 array
    idx: int
    ntaps: int
    coef: numpy int16 array (Q15 coefficients)
    pairs: list of (left_index, right_index) tuples

    Returns: int16 result (sat_s16(acc >> 15)).
    """
    center = ntaps // 2
    acc = int(coef[center]) * dac_fir_read_tap_py(delay, idx, center, ntaps)

    for left, right in pairs:
        c = int(coef[left])
        pair = (dac_fir_read_tap_py(delay, idx, left, ntaps)
                + dac_fir_read_tap_py(delay, idx, right, ntaps))
        acc += c * pair

    return sat_s16(acc >> 15)


def build_pair_table(coef_q15):
    """Derive symmetric pair index table from Q15 coefficient array.

    For a half-band filter, non-zero coefficients are at even indices
    (plus the center tap at index N//2). Symmetric pairs: coef[i] == coef[N-1-i]
    where i < N//2 and coef[i] != 0.

    Returns list of (left_index, right_index) tuples matching C pair tables.
    """
    n = len(coef_q15)
    center = n // 2
    pairs = []
    for i in range(center):
        if int(coef_q15[i]) != 0:
            j = n - 1 - i
            pairs.append((i, j))
    return pairs


def verify_8x_cascade(h1_q, h2_q, h3_q, pairs1, pairs2, pairs3, input_signal):
    """Simulate the naive 8x zero-stuff + cascade + decimate in Python.

    For each input sample at 44.1kHz:
    - Stage 1: push real, evaluate; push 0, evaluate -> 2 outputs (88.2kHz)
    - Stage 2: for each s1 output, push real, evaluate; push 0, evaluate -> 4 outputs (176.4kHz)
    - Stage 3: for each s2 output, push real, evaluate; push 0, evaluate -> 8 outputs (352.8kHz)
    - Decimation: keep the last (index 7) output

    Uses Q15 integer arithmetic matching the C implementation exactly.

    Args:
        h1_q, h2_q, h3_q: numpy int16 coefficient arrays (Q15)
        pairs1, pairs2, pairs3: pair index tables
        input_signal: numpy int16 array of input samples

    Returns:
        numpy int16 array of decimated output at 44.1kHz.
    """
    ntaps1 = len(h1_q)
    ntaps2 = len(h2_q)
    ntaps3 = len(h3_q)

    # Initialize delay lines as circular buffers (all zeros, matching C memset)
    s1_delay = np.zeros(ntaps1, dtype=np.int16)
    s2_delay = np.zeros(ntaps2, dtype=np.int16)
    s3_delay = np.zeros(ntaps3, dtype=np.int16)
    s1_idx, s2_idx, s3_idx = 0, 0, 0

    output = []
    for sample in input_signal:
        # Stage 1: push real sample, evaluate; push zero, evaluate
        s1_out = []
        for val in [int(sample), 0]:
            s1_idx = dac_fir_push_py(s1_delay, s1_idx, val, ntaps1)
            s1_out.append(dac_fir_stage_apply_py(
                s1_delay, s1_idx, ntaps1, h1_q, pairs1))

        # Stage 2: for each s1 output, push real, evaluate; push zero, evaluate
        s2_out = []
        for val in s1_out:
            for sub_val in [val, 0]:
                s2_idx = dac_fir_push_py(s2_delay, s2_idx, sub_val, ntaps2)
                s2_out.append(dac_fir_stage_apply_py(
                    s2_delay, s2_idx, ntaps2, h2_q, pairs2))

        # Stage 3: for each s2 output, push real, evaluate; push zero, evaluate
        s3_out = []
        for val in s2_out:
            for sub_val in [val, 0]:
                s3_idx = dac_fir_push_py(s3_delay, s3_idx, sub_val, ntaps3)
                s3_out.append(dac_fir_stage_apply_py(
                    s3_delay, s3_idx, ntaps3, h3_q, pairs3))

        # Decimate: keep the last of 8 outputs
        output.append(s3_out[-1])

    return np.array(output, dtype=np.int16)


def cmd_verify_8x():
    """CLI handler for --verify-8x: naive 8x zero-stuff cascade simulation.

    Steps:
    a. Design and quantize all three stages.
    b. Build pair tables matching C pair table structure.
    c. Generate test signals: impulse, swept sine, DC.
    d. Run verify_8x_cascade on each.
    e. Check impulse response, frequency response, DC gain.
    f. Print results with PASS/FAIL labels.
    """
    print("=== 8x Zero-Stuff Cascade Verification (D-04) ===\n")

    # a. Design and quantize
    h1 = design_halfband_stage(N_STAGE1, FS_STAGE1, F_PASS)
    h2 = design_halfband_stage(N_STAGE2, FS_STAGE2, F_PASS)
    h3 = design_halfband_stage(N_STAGE3, FS_STAGE3, F_PASS)

    h1_q, h1_qf = quantize_to_q15(h1)
    h2_q, h2_qf = quantize_to_q15(h2)
    h3_q, h3_qf = quantize_to_q15(h3)

    # Cast to int16 for the simulation
    h1_q16 = h1_q.astype(np.int16)
    h2_q16 = h2_q.astype(np.int16)
    h3_q16 = h3_q.astype(np.int16)

    # b. Build pair tables from coefficient arrays
    pairs1 = build_pair_table(h1_q16)
    pairs2 = build_pair_table(h2_q16)
    pairs3 = build_pair_table(h3_q16)

    print(f"  Stage 1: {len(pairs1)} pairs (expect 14)")
    print(f"  Stage 2: {len(pairs2)} pairs (expect 3)")
    print(f"  Stage 3: {len(pairs3)} pairs (expect 2)")

    all_pass = True

    # c. Generate test signals
    # Impulse: INT16_MAX at sample 0, rest zeros
    n_impulse = 256
    impulse_in = np.zeros(n_impulse, dtype=np.int16)
    impulse_in[0] = np.int16(32767)  # INT16_MAX

    # Swept sine: 20Hz-20kHz over 4096 samples at 44.1kHz
    n_sweep = 4096
    t_sweep = np.arange(n_sweep) / FS_AUDIO
    sweep_phase = 2 * np.pi * (20 + (20000 - 20) / (2 * t_sweep[-1]) * t_sweep) * t_sweep
    sweep_float = 16384.0 * np.sin(sweep_phase)
    sweep_in = np.clip(np.round(sweep_float), -32768, 32767).astype(np.int16)

    # DC: constant 16384 for 256 samples
    n_dc = 256
    dc_in = np.full(n_dc, 16384, dtype=np.int16)

    # d. Run cascade on each
    print("\n--- Running impulse test ---")
    impulse_out = verify_8x_cascade(
        h1_q16, h2_q16, h3_q16, pairs1, pairs2, pairs3, impulse_in)

    print("--- Running swept sine test ---")
    sweep_out = verify_8x_cascade(
        h1_q16, h2_q16, h3_q16, pairs1, pairs2, pairs3, sweep_in)

    print("--- Running DC test ---")
    dc_out = verify_8x_cascade(
        h1_q16, h2_q16, h3_q16, pairs1, pairs2, pairs3, dc_in)

    # e. Impulse response checks
    print("\n=== Impulse Response Checks ===\n")

    # Not all zeros
    imp_nonzero = np.any(impulse_out != 0)
    status = "PASS" if imp_nonzero else "FAIL"
    print(f"  {status}: Impulse output is not all zeros")
    if not imp_nonzero:
        all_pass = False

    # Returns to zero after finite samples (check last 32 samples are zero)
    imp_tail_zero = np.all(impulse_out[-32:] == 0)
    status = "PASS" if imp_tail_zero else "FAIL"
    print(f"  {status}: Impulse response returns to zero (last 32 samples)")
    if not imp_tail_zero:
        all_pass = False

    # First non-zero within first 10 samples
    nonzero_indices = np.nonzero(impulse_out)[0]
    if len(nonzero_indices) > 0:
        first_nonzero = nonzero_indices[0]
        imp_early = first_nonzero < 10
    else:
        first_nonzero = -1
        imp_early = False
    status = "PASS" if imp_early else "FAIL"
    print(f"  {status}: First non-zero output at sample {first_nonzero} (expect < 10)")
    if not imp_early:
        all_pass = False

    # f. Frequency response check: compare 8x cascade vs analytical composite
    #
    # The analytical composite (from build_composite) models the cascade
    # filter at FS_STAGE3 = 352.8kHz. The 8x cascade produces output at
    # FS_AUDIO = 44.1kHz after decimation. Both have the same passband
    # SHAPE -- the comparison normalizes to DC gain and checks shape match.
    #
    # NOTE: The Q15 integer arithmetic in the cascade introduces ~0.04 dB
    # of quantization ripple beyond the analytical float composite. This
    # is inherent to the fixed-point implementation (14 evaluate calls per
    # sample, each with acc>>15 truncation). The threshold is set to 0.05 dB
    # to accommodate this while still proving the cascade is correct.
    print("\n=== Frequency Response Check (INT-04: passband shape) ===\n")

    # Reference: analytical composite at audio-band frequencies (at FS_STAGE3)
    h_composite_q = build_composite(h1_qf, h2_qf, h3_qf)
    n_pts = 8192
    freqs_audio = np.linspace(20, F_PASS, n_pts)  # 20 Hz to 20 kHz
    w_ref = 2 * np.pi * freqs_audio / FS_STAGE3
    _, H_ref = sig.freqz(h_composite_q, worN=w_ref)
    H_ref_db = 20 * np.log10(np.abs(H_ref) + 1e-15)
    # Normalize to DC
    _, H_ref_dc = sig.freqz(h_composite_q, worN=[1e-6])
    dc_ref = 20 * np.log10(np.abs(H_ref_dc[0]) + 1e-15)
    H_ref_db_norm = H_ref_db - dc_ref

    # 8x cascade: frequency response from impulse response at FS_AUDIO
    imp_float = impulse_out.astype(np.float64)
    w_8x = 2 * np.pi * freqs_audio / FS_AUDIO
    _, H_8x = sig.freqz(imp_float, worN=w_8x)
    H_8x_db = 20 * np.log10(np.abs(H_8x) + 1e-15)
    # Normalize to DC
    _, H_8x_dc = sig.freqz(imp_float, worN=[1e-6])
    dc_8x = 20 * np.log10(np.abs(H_8x_dc[0]) + 1e-15)
    H_8x_db_norm = H_8x_db - dc_8x

    deviation = np.abs(H_8x_db_norm - H_ref_db_norm)
    max_deviation = float(np.max(deviation))

    # 0.05 dB threshold accounts for Q15 truncation noise across 14
    # evaluations per sample. The analytical composite uses float math;
    # the cascade uses integer truncation (acc >> 15) at each stage.
    freq_pass = max_deviation < 0.05
    status = "PASS" if freq_pass else "FAIL"
    print(f"  {status}: Passband deviation = {max_deviation:.6f} dB "
          f"(limit: 0.05 dB, Q15 truncation budget)")
    if not freq_pass:
        all_pass = False

    # Also report the analytical composite's own passband ripple for context
    composite_ripple = float(np.max(H_ref_db_norm) - np.min(H_ref_db_norm))
    cascade_ripple = float(np.max(H_8x_db_norm) - np.min(H_8x_db_norm))
    print(f"  INFO: Composite passband ripple = {composite_ripple:.4f} dB")
    print(f"  INFO: 8x cascade passband ripple = {cascade_ripple:.4f} dB")

    # g. DC gain check
    print("\n=== DC Gain Check ===\n")

    # The 8x zero-stuff cascade has a DC gain of approximately 1/8 (-18.06 dB)
    # times the filter cascade gain (~0.997, i.e. -0.027 dB).
    # Total expected: 0.997/8 = 0.1246, or about -18.09 dB.
    #
    # The 1/8 factor comes from zero-stuffing: each 2x zero-stuff halves
    # the signal amplitude (half the samples are zero), and 3 stages of
    # 2x = 8x attenuation. This is the correct behavior of the naive
    # zero-stuff cascade (D-01).
    dc_steady = dc_out[-64:].astype(np.float64).mean()
    dc_input = 16384.0
    dc_gain_linear = dc_steady / dc_input
    if dc_gain_linear > 0:
        dc_gain_db = 20 * np.log10(dc_gain_linear)
    else:
        dc_gain_db = -999.0

    # Expected: cascade_gain / 8, where cascade_gain ~ 0.997 (-0.027 dB)
    # = 0.997 / 8 = 0.1246 => -18.09 dB
    cascade_gain = float(np.sum(h1_qf) * np.sum(h2_qf) * np.sum(h3_qf))
    expected_dc_gain = 20 * np.log10(cascade_gain / 8)
    dc_gain_err = abs(dc_gain_db - expected_dc_gain)
    dc_pass = dc_gain_err < 0.05
    status = "PASS" if dc_pass else "FAIL"
    print(f"  {status}: DC gain = {dc_gain_db:.4f} dB "
          f"(expected: {expected_dc_gain:.3f} dB, tolerance: 0.05 dB, "
          f"error: {dc_gain_err:.4f} dB)")
    print(f"  INFO: 1/8 zero-stuff factor = {20 * np.log10(1/8):.3f} dB, "
          f"cascade filter gain = {20 * np.log10(cascade_gain):.3f} dB")
    if not dc_pass:
        all_pass = False

    # Final verdict
    print()
    if all_pass:
        print("ALL 8x CASCADE CHECKS PASS")
        return 0
    else:
        print("8x CASCADE VERIFICATION FAILED -- see details above")
        return 1


def cmd_verify():
    """Design, verify, and report pass/fail for all datasheet specs."""
    # Design all three stages
    h1 = design_halfband_stage(N_STAGE1, FS_STAGE1, F_PASS)
    h2 = design_halfband_stage(N_STAGE2, FS_STAGE2, F_PASS)
    h3 = design_halfband_stage(N_STAGE3, FS_STAGE3, F_PASS)

    stages = [
        ("Stage 1", h1, FS_STAGE1, N_STAGE1),
        ("Stage 2", h2, FS_STAGE2, N_STAGE2),
        ("Stage 3", h3, FS_STAGE3, N_STAGE3),
    ]

    # === Individual stage results ===
    print("=== Individual Stage Results ===\n")
    for name, h, fs, n in stages:
        m = measure_stage(h, fs, F_PASS)
        print(f"  {name} (N={n}): ripple={m['ripple_pp_dB']:.4f} dB p-p, "
              f"atten={m['stopband_atten_dB']:.1f} dB, "
              f"non-zero={m['n_nonzero']}")

    # === Float composite verification ===
    print("\n=== Composite Cascade (float coefficients) ===\n")
    h_composite = build_composite(h1, h2, h3)
    results_float = verify_cascade(h_composite, FS_STAGE3, F_PASS, FS_AUDIO)

    all_pass = True
    checks_float = [
        ("Passband ripple", results_float['ripple_pp_dB'], "<= 0.10 dB",
         results_float['pass_ripple']),
        ("Stopband attenuation", results_float['stopband_atten_dB'], ">= 41.0 dB",
         results_float['pass_stopband']),
        ("Response @20kHz", results_float['response_at_20kHz_dB'], ">= -0.2 dB",
         results_float['pass_20kHz']),
    ]
    for label, value, spec, passed in checks_float:
        status = "PASS" if passed else "FAIL"
        print(f"  {status}: {label} = {value:.4f} dB (spec: {spec})")
        if not passed:
            all_pass = False

    print(f"\n  DC gain: {results_float['dc_gain_dB']:.4f} dB (not compensated)")

    # === Q15 quantized verification ===
    print("\n=== Composite Cascade (Q15 quantized coefficients) ===\n")
    _, h1_q = quantize_to_q15(h1)
    _, h2_q = quantize_to_q15(h2)
    _, h3_q = quantize_to_q15(h3)

    h_composite_q = build_composite(h1_q, h2_q, h3_q)
    results_q15 = verify_cascade(h_composite_q, FS_STAGE3, F_PASS, FS_AUDIO)

    checks_q15 = [
        ("Passband ripple (Q15)", results_q15['ripple_pp_dB'], "<= 0.10 dB",
         results_q15['pass_ripple']),
        ("Stopband attenuation (Q15)", results_q15['stopband_atten_dB'], ">= 41.0 dB",
         results_q15['pass_stopband']),
        ("Response @20kHz (Q15)", results_q15['response_at_20kHz_dB'], ">= -0.2 dB",
         results_q15['pass_20kHz']),
    ]
    for label, value, spec, passed in checks_q15:
        status = "PASS" if passed else "FAIL"
        print(f"  {status}: {label} = {value:.4f} dB (spec: {spec})")
        if not passed:
            all_pass = False

    print(f"\n  DC gain (Q15): {results_q15['dc_gain_dB']:.4f} dB")

    # === Final verdict ===
    print()
    if all_pass:
        print("ALL PASS")
        return 0
    else:
        print("FAIL -- see details above")
        return 1


def cmd_plot():
    """Generate frequency response plot with datasheet spec reference lines."""
    try:
        import matplotlib
        matplotlib.use('Agg')  # Non-interactive backend
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not found. Install it in a venv:")
        print("  python3 -m venv .venv && .venv/bin/pip install matplotlib")
        print("Then run: .venv/bin/python3 tools/dac_filter_design.py --plot")
        sys.exit(1)

    import os

    h1 = design_halfband_stage(N_STAGE1, FS_STAGE1, F_PASS)
    h2 = design_halfband_stage(N_STAGE2, FS_STAGE2, F_PASS)
    h3 = design_halfband_stage(N_STAGE3, FS_STAGE3, F_PASS)

    h_composite = build_composite(h1, h2, h3)
    results = verify_cascade(h_composite, FS_STAGE3, F_PASS, FS_AUDIO)

    w = results['w']
    H_db = results['H_db_norm']

    fig, ax = plt.subplots(figsize=(12, 7))

    # Composite response (blue)
    ax.plot(w, H_db, 'b-', linewidth=1.0, label='Composite cascade response')

    # Passband ripple limits (red dashed)
    ax.axhline(y=0.05, color='r', linestyle='--', linewidth=0.8,
               label='Passband limit (+0.05 dB)')
    ax.axhline(y=-0.05, color='r', linestyle='--', linewidth=0.8,
               label='Passband limit (-0.05 dB)')

    # Stopband attenuation limit (green dashed)
    ax.axhline(y=-41.0, color='g', linestyle='--', linewidth=0.8,
               label='Stopband limit (-41 dB)')

    # Original Nyquist boundary (vertical dashed)
    ax.axvline(x=22050, color='gray', linestyle='--', linewidth=0.8,
               label='Original Nyquist (22050 Hz)')

    # 20 kHz marker (orange)
    at_20k = results['response_at_20kHz_dB']
    ax.axvline(x=20000, color='orange', linestyle='-', linewidth=1.0, alpha=0.7)
    ax.plot(20000, at_20k, 'o', color='orange', markersize=8,
            label=f'@20kHz = {at_20k:.3f} dB')

    ax.set_title('AK4309 8x Interpolation Filter -- Composite Response (55+11+7 taps)')
    ax.set_xlabel('Frequency (Hz)')
    ax.set_ylabel('Magnitude (dB)')
    ax.set_xlim(0, 40000)
    ax.set_ylim(-80, 2)
    ax.legend(loc='lower left', fontsize=9)
    ax.grid(True, alpha=0.3)

    # Ensure plots/ directory exists
    os.makedirs('plots', exist_ok=True)
    fig.savefig('plots/dac_interpolation_response.png', dpi=150, bbox_inches='tight')
    plt.close(fig)
    print("Saved: plots/dac_interpolation_response.png")


def export_c_arrays(h1, h2, h3):
    """Print Q15 coefficient arrays in C int16_t format to stdout."""
    stages = [
        ("dac_interp_stage1", h1),
        ("dac_interp_stage2", h2),
        ("dac_interp_stage3", h3),
    ]
    for name, h in stages:
        h_q15, _ = quantize_to_q15(h)
        n_nonzero = int(np.count_nonzero(h_q15))
        print(f"/* {len(h)}-tap half-band FIR, Q15 ({n_nonzero} non-zero) */")
        print(f"static const int16_t {name}[{len(h)}] = {{")
        for i in range(0, len(h_q15), 8):
            chunk = h_q15[i:i + 8]
            row = ", ".join(f"0x{int(v) & 0xFFFF:04X}" for v in chunk)
            print(f"    {row},")
        print("};")
        print()


def cmd_export_c():
    """Design and export C-array coefficients."""
    h1 = design_halfband_stage(N_STAGE1, FS_STAGE1, F_PASS)
    h2 = design_halfband_stage(N_STAGE2, FS_STAGE2, F_PASS)
    h3 = design_halfband_stage(N_STAGE3, FS_STAGE3, F_PASS)
    export_c_arrays(h1, h2, h3)


def main():
    parser = argparse.ArgumentParser(
        description="AK4309 8x interpolation filter design and verification")
    parser.add_argument('--verify', action='store_true',
                        help='Run pass/fail assertions against datasheet specs')
    parser.add_argument('--plot', action='store_true',
                        help='Generate frequency response PNG (requires matplotlib)')
    parser.add_argument('--export-c', action='store_true',
                        help='Print Q15 coefficient arrays in C int16_t format')
    parser.add_argument('--verify-8x', action='store_true',
                        help='Run naive 8x zero-stuff cascade simulation (D-04)')
    args = parser.parse_args()

    if not (args.verify or args.plot or args.export_c or args.verify_8x):
        parser.print_help()
        sys.exit(0)

    if args.verify_8x:
        rc = cmd_verify_8x()
        sys.exit(rc)

    if args.verify:
        rc = cmd_verify()
        if args.plot:
            cmd_plot()
        if args.export_c:
            cmd_export_c()
        sys.exit(rc)

    if args.plot:
        cmd_plot()

    if args.export_c:
        cmd_export_c()


if __name__ == '__main__':
    main()
