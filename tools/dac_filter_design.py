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
    args = parser.parse_args()

    if not (args.verify or args.plot or args.export_c):
        parser.print_help()
        sys.exit(0)

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
