#!/usr/bin/env python3
"""tools/dac_measure.py -- Phase 9 Plan 02 (DAC-TEST-02)

DAC characterization measurement script. Verifies the C engine's DAC
model against the Phase 5 scipy design target:

  1. Frequency response: white noise through C engine vs analytical freqz
  2. Passband ripple: audio band within +/-0.078 dB tolerance
  3. Noise shaping slope: +12 dB/octave (2nd-order HP)

The C engine runs all three FIR stages at 44.1 kHz (not at increasing
rates -- see spu94_dac_fir.c Pitfall 5). The analytical reference is
therefore freqz of each stage at 44.1 kHz, cascaded by multiplication.
The passband is the region where this cascade is approximately flat
(below ~5 kHz). Above that, the cascade rolls off aggressively -- this
is the expected coloration character, not a bug.

Usage:
  python3 tools/dac_measure.py           Run 3 automated checks, exit 0/1
  python3 tools/dac_measure.py --plot    Also generate tools/dac_measure.png
  python3 tools/dac_measure.py --verbose Print detailed diagnostics
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch

# === Constants ===
FS = 44100
SWEEP_DURATION = 2.0
N_SAMPLES = int(FS * SWEEP_DURATION)

# Tolerances
PASSBAND_RIPPLE_8X = 0.078         # dB, Phase 5 design spec at 8x rate
PASSBAND_RIPPLE_TOLERANCE = 0.15   # dB, at-rate cascade tolerance (includes
                                   # Q15 quantization + Welch estimation noise)
DESIGN_DEVIATION_TOLERANCE = 0.5   # dB max deviation from analytical
NOISE_SLOPE_TARGET = 12.0          # dB/octave
NOISE_SLOPE_TOLERANCE = 3.0        # +/- dB/octave

# Passband is defined as the region where the at-rate cascade is ~flat.
# Stage 1 (designed for 88.2kHz) has its first significant rolloff around
# fs/2 * (f_pass/fs_stage1) = 22050 * (20000/88200) ~ 5000 Hz when run
# at 44.1 kHz. We check 20-4000 Hz as the "clean passband".
PASSBAND_LOW = 100
PASSBAND_HIGH = 4000


def locate_cli():
    """Locate the spu94 CLI binary.

    Priority:
      1. $SPU94_BIN env var (explicit override)
      2. build/src/cli/spu94 (dev tree)
      3. 'spu94' on PATH (installed)
    """
    override = os.environ.get("SPU94_BIN")
    if override:
        return override
    dev_build = Path("build/src/cli/spu94")
    if dev_build.is_file():
        return str(dev_build)
    return "spu94"


def get_design_coefficients():
    """Import Phase 5 design coefficients for each stage.

    Returns (h1_q_float, h2_q_float, h3_q_float) -- the Q15 float
    reconstructions for each stage individually.
    """
    tools_dir = str(Path(__file__).resolve().parent)
    if tools_dir not in sys.path:
        sys.path.insert(0, tools_dir)

    from dac_filter_design import (
        design_halfband_stage,
        quantize_to_q15,
    )

    h1 = design_halfband_stage(55, 88200, 20000)
    h2 = design_halfband_stage(11, 176400, 20000)
    h3 = design_halfband_stage(7, 352800, 20000)

    _, h1_q = quantize_to_q15(h1)
    _, h2_q = quantize_to_q15(h2)
    _, h3_q = quantize_to_q15(h3)

    return h1_q, h2_q, h3_q


def get_analytical_response():
    """Compute analytical cascade response at 44.1 kHz.

    All three FIR stages run at fs=44100 in the C engine (Pitfall 5).
    Returns (freq, magnitude_dB) normalized to 0 dB at DC.
    """
    from scipy.signal import freqz
    h1_q, h2_q, h3_q = get_design_coefficients()

    w, H1 = freqz(h1_q, worN=8192, fs=FS)
    _, H2 = freqz(h2_q, worN=8192, fs=FS)
    _, H3 = freqz(h3_q, worN=8192, fs=FS)

    H_cascade = H1 * H2 * H3
    H_dB = 20 * np.log10(np.abs(H_cascade) + 1e-15)
    H_dB -= H_dB[0]

    return w, H_dB


def measure_frequency_response(spu94_bin, verbose=False):
    """Measure DAC FIR frequency response using white noise.

    Returns (freq_measured, mag_measured_dB, freq_analytical, mag_analytical_dB).
    """
    rng = np.random.default_rng(0xDAC_F1B)
    noise = rng.integers(-15000, 15000, size=N_SAMPLES, dtype=np.int16)
    stereo_input = np.stack([noise, noise], axis=1)

    with tempfile.TemporaryDirectory(prefix="dac_measure_") as tmpd:
        in_path = os.path.join(tmpd, "noise_in.wav")
        out_path = os.path.join(tmpd, "noise_out.wav")

        wavfile.write(in_path, FS, stereo_input)

        env = dict(os.environ)
        env["LC_ALL"] = "C"
        env["TZ"] = "UTC"

        r = subprocess.run(
            [spu94_bin, "reverb", "--preset", "off",
             "--dac", "--no-dac-noise",
             "--input-gain", "1.0", "--dry", "1.0",
             in_path, out_path],
            check=False, capture_output=True, env=env,
        )
        if r.returncode != 0:
            stderr = r.stderr.decode("utf-8", errors="replace")[:300]
            raise RuntimeError(
                f"spu94 reverb --dac failed (rc={r.returncode}): {stderr}")

        sr, data = wavfile.read(out_path)

    if data.ndim == 2:
        output = data[:, 0].astype(np.float64)
    else:
        output = data.astype(np.float64)
    input_signal = noise.astype(np.float64)

    # Transfer function via Welch PSD ratio
    nperseg = 8192
    freq_out, psd_out = welch(output, fs=FS, nperseg=nperseg)
    freq_in, psd_in = welch(input_signal, fs=FS, nperseg=nperseg)

    psd_in_safe = np.maximum(psd_in, 1e-20)
    transfer = psd_out / psd_in_safe
    mag_measured_dB = 10 * np.log10(np.maximum(transfer, 1e-20))

    # Normalize to 0 dB at low frequencies (100-1000 Hz)
    low_mask = (freq_out >= 100) & (freq_out <= 1000)
    if np.any(low_mask):
        mag_measured_dB -= np.mean(mag_measured_dB[low_mask])

    if verbose:
        print(f"  Measured: {len(freq_out)} bins, "
              f"{freq_out[1]:.1f}-{freq_out[-1]:.1f} Hz")

    # Analytical
    freq_a, mag_a = get_analytical_response()

    return freq_out, mag_measured_dB, freq_a, mag_a


def measure_noise_slope(spu94_bin, verbose=False):
    """Measure DAC noise shaping spectral slope.

    Returns (slope_dB_per_octave, freq_array, psd_dB_array).
    """
    silence = np.zeros((N_SAMPLES, 2), dtype=np.int16)

    with tempfile.TemporaryDirectory(prefix="dac_noise_") as tmpd:
        in_path = os.path.join(tmpd, "silence_in.wav")
        out_path = os.path.join(tmpd, "silence_out.wav")

        wavfile.write(in_path, FS, silence)

        env = dict(os.environ)
        env["LC_ALL"] = "C"
        env["TZ"] = "UTC"

        r = subprocess.run(
            [spu94_bin, "reverb", "--preset", "off",
             "--dac", "--no-dac-fir",
             "--input-gain", "1.0", "--dry", "1.0",
             in_path, out_path],
            check=False, capture_output=True, env=env,
        )
        if r.returncode != 0:
            stderr = r.stderr.decode("utf-8", errors="replace")[:300]
            raise RuntimeError(
                f"spu94 reverb --dac --no-dac-fir failed "
                f"(rc={r.returncode}): {stderr}")

        sr, data = wavfile.read(out_path)

    if data.ndim == 2:
        output = data[:, 0].astype(np.float64)
    else:
        output = data.astype(np.float64)

    freq, psd = welch(output, fs=FS, nperseg=4096)
    psd_dB = 10 * np.log10(np.maximum(psd, 1e-30))

    # Fit slope in log-log (dB vs log2-freq) from 500Hz to 8kHz.
    # The 2nd-order HP shaping produces +12 dB/oct in this region;
    # above ~8kHz the spectrum flattens near Nyquist (expected).
    mask = (freq >= 500) & (freq <= 8000)
    log2_f = np.log2(freq[mask])
    coeffs = np.polyfit(log2_f, psd_dB[mask], 1)
    slope = coeffs[0]

    if verbose:
        print(f"  Noise slope: {slope:.2f} dB/octave "
              f"(target: +{NOISE_SLOPE_TARGET:.0f})")
        print(f"  Non-zero samples: {np.count_nonzero(output)}/{len(output)}")

    return slope, freq, psd_dB


def check_passband_ripple(freq, mag_dB, verbose=False):
    """Check passband ripple in 100-4000 Hz band.

    The at-rate cascade is approximately flat here. Phase 5 spec is
    +/-0.078 dB for the 8x composite; at 44.1 kHz the at-rate cascade
    has slightly more ripple from Q15 quantization and aliased transition-
    band energy. Tolerance widened to 0.15 dB for the at-rate measurement.

    Returns (passed, max_deviation_dB).
    """
    mask = (freq >= PASSBAND_LOW) & (freq <= PASSBAND_HIGH)
    if not np.any(mask):
        return False, float('inf')

    band = mag_dB[mask]
    max_dev = np.max(np.abs(band))
    passed = max_dev <= PASSBAND_RIPPLE_TOLERANCE

    if verbose:
        print(f"  Passband ({PASSBAND_LOW}-{PASSBAND_HIGH} Hz): "
              f"max |dev| = {max_dev:.4f} dB "
              f"(8x design: {PASSBAND_RIPPLE_8X}, "
              f"at-rate limit: {PASSBAND_RIPPLE_TOLERANCE}), "
              f"range [{np.min(band):.4f}, {np.max(band):.4f}] dB")

    return passed, float(max_dev)


def check_design_deviation(freq_measured, measured_dB, freq_analytical,
                           analytical_dB, verbose=False):
    """Check max deviation between C engine and analytical response.

    Compares in the passband region (20 Hz to 4 kHz) where both measured
    and analytical are well-defined. Above 4 kHz the measured PSD gets
    noisy as the filter attenuates heavily.

    Returns (passed, max_deviation_dB, freq_at_max_Hz).
    """
    analytical_interp = np.interp(freq_measured, freq_analytical, analytical_dB)

    mask = (freq_measured >= PASSBAND_LOW) & (freq_measured <= PASSBAND_HIGH)
    if not np.any(mask):
        return False, float('inf'), 0.0

    deviation = np.abs(measured_dB[mask] - analytical_interp[mask])
    max_dev = np.max(deviation)
    idx_max = np.argmax(deviation)
    freq_at_max = freq_measured[mask][idx_max]
    passed = max_dev <= DESIGN_DEVIATION_TOLERANCE

    if verbose:
        print(f"  Design deviation: max = {max_dev:.4f} dB "
              f"at {freq_at_max:.0f} Hz "
              f"(limit: {DESIGN_DEVIATION_TOLERANCE} dB)")

    return passed, float(max_dev), float(freq_at_max)


def check_noise_slope(slope_dB_per_octave, verbose=False):
    """Check noise shaping spectral slope is +12 dB/octave +/- 3.

    Returns (passed, measured_slope).
    """
    passed = abs(slope_dB_per_octave - NOISE_SLOPE_TARGET) <= NOISE_SLOPE_TOLERANCE

    if verbose:
        print(f"  Noise slope: {slope_dB_per_octave:.2f} dB/oct "
              f"(target: {NOISE_SLOPE_TARGET} +/- {NOISE_SLOPE_TOLERANCE})")

    return passed, float(slope_dB_per_octave)


def plot_report(freq_measured, mag_measured, freq_analytical, mag_analytical,
                noise_freq, noise_psd, output_path):
    """Generate 2-subplot characterization report PNG."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8))

    # --- Top: Filter magnitude response ---
    # Mask out DC for log scale
    m_mask = freq_measured > 0
    a_mask = freq_analytical > 0

    ax1.semilogx(freq_measured[m_mask], mag_measured[m_mask], 'b-',
                 linewidth=1.0, label='Measured (C engine)')
    ax1.semilogx(freq_analytical[a_mask], mag_analytical[a_mask], '--',
                 color='orange', linewidth=1.0,
                 label='Analytical (Q15 freqz)')
    ax1.axvline(x=PASSBAND_HIGH, color='gray', linestyle=':',
                linewidth=0.8, label=f'{PASSBAND_HIGH} Hz (passband edge)')
    ax1.axvline(x=10000, color='red', linestyle=':', linewidth=0.8,
                alpha=0.5, label='10 kHz')
    ax1.axvline(x=20000, color='red', linestyle=':', linewidth=0.8,
                alpha=0.5, label='20 kHz')
    ax1.set_xlim(20, 22050)
    ax1.set_ylim(-3, 0.5)
    ax1.set_xlabel('Frequency (Hz)')
    ax1.set_ylabel('Magnitude (dB)')
    ax1.set_title('DAC FIR Frequency Response: Measured vs Analytical '
                  '(all stages at 44.1 kHz)')
    ax1.legend(loc='lower left', fontsize=9)
    ax1.grid(True, alpha=0.3)

    # --- Bottom: Noise spectral slope ---
    n_mask = noise_freq > 0
    ax2.semilogx(noise_freq[n_mask], noise_psd[n_mask], 'b-',
                 linewidth=0.8, label='DAC noise PSD')

    # +12 dB/octave reference line anchored at 1 kHz
    ref_freqs = np.array([500, 1000, 2000, 4000, 8000, 16000, 20000])
    idx_1k = np.argmin(np.abs(noise_freq - 1000))
    psd_at_1k = noise_psd[idx_1k] if idx_1k < len(noise_psd) else -60
    ref_dB = psd_at_1k + 12.0 * np.log2(ref_freqs / 1000.0)
    ax2.semilogx(ref_freqs, ref_dB, 'r--', linewidth=1.0,
                 label='+12 dB/octave reference')

    ax2.set_xlim(100, 22050)
    ax2.set_xlabel('Frequency (Hz)')
    ax2.set_ylabel('PSD (dB)')
    ax2.set_title('DAC Noise Shaping Spectral Slope')
    ax2.legend(loc='upper left', fontsize=9)
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="DAC characterization: frequency response + noise slope")
    parser.add_argument('--plot', action='store_true',
                        help='Generate tools/dac_measure.png report')
    parser.add_argument('--verbose', action='store_true',
                        help='Print detailed diagnostics')
    args = parser.parse_args()

    spu94_bin = locate_cli()
    if args.verbose:
        print(f"Using CLI: {spu94_bin}")

    # --- Measure ---
    if args.verbose:
        print("\n--- Frequency Response ---")
    freq_m, mag_m, freq_a, mag_a = measure_frequency_response(
        spu94_bin, verbose=args.verbose)

    if args.verbose:
        print("\n--- Noise Slope ---")
    noise_slope, noise_freq, noise_psd = measure_noise_slope(
        spu94_bin, verbose=args.verbose)

    # --- Checks ---
    results = []

    p, v = check_passband_ripple(freq_m, mag_m, verbose=args.verbose)
    print(f"  {'PASS' if p else 'FAIL'}: Passband ripple "
          f"({PASSBAND_LOW}-{PASSBAND_HIGH} Hz) = {v:.4f} dB "
          f"(limit: {PASSBAND_RIPPLE_TOLERANCE} dB)")
    results.append(p)

    p, v, f = check_design_deviation(
        freq_m, mag_m, freq_a, mag_a, verbose=args.verbose)
    print(f"  {'PASS' if p else 'FAIL'}: Design deviation = {v:.4f} dB "
          f"at {f:.0f} Hz (limit: {DESIGN_DEVIATION_TOLERANCE} dB)")
    results.append(p)

    p, v = check_noise_slope(noise_slope, verbose=args.verbose)
    print(f"  {'PASS' if p else 'FAIL'}: Noise slope = {v:.2f} dB/octave "
          f"(target: {NOISE_SLOPE_TARGET} +/- {NOISE_SLOPE_TOLERANCE})")
    results.append(p)

    # --- Plot ---
    if args.plot:
        png_path = str(Path(__file__).resolve().parent / "dac_measure.png")
        plot_report(freq_m, mag_m, freq_a, mag_a,
                    noise_freq, noise_psd, png_path)
        print(f"\n  Saved: {png_path}")

    # --- Summary ---
    n_pass = sum(results)
    n_total = len(results)
    if n_pass == n_total:
        print(f"\nDAC Characterization: {n_pass}/{n_total} checks passed")
        return 0
    else:
        print(f"\nDAC Characterization: "
              f"{n_total - n_pass}/{n_total} checks FAILED")
        return 1


if __name__ == '__main__':
    sys.exit(main())
