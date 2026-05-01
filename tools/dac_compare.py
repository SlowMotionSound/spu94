#!/usr/bin/env python3
"""tools/dac_compare.py -- Phase 12 (CMP-02)

v1.2 vs v1.3 DAC characterization comparison. Produces 4 measurements:
  1. Frequency response: transfer function overlay (v1.2 vs v1.3)
  2. Impulse response: time-domain impulse difference
  3. Noise floor: PSD comparison (silence input, noise-only)
  4. Time-domain: waveform overlay on music-like signal (chirp)

Usage:
  python3 tools/dac_compare.py           Run measurements, print summary
  python3 tools/dac_compare.py --plot    Also generate tools/dac_compare.png
  python3 tools/dac_compare.py --verbose Print detailed diagnostics
"""

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import welch, chirp

# === Constants ===
FS = 44100
DURATION = 2.0
N_SAMPLES = int(FS * DURATION)
NOISE_SEED = 0xDAC_F1B       # Same as dac_measure.py for consistency
AMP = 16000                   # Same as regenerate_goldens.py


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


def _run_cli(spu94_bin, input_path, output_path, *, true_oversample=True,
             no_fir=False, no_noise=False):
    """Run the spu94 CLI with DAC enabled and the specified sub-toggles.

    Parameters
    ----------
    true_oversample : bool
        True = v1.3 (default), False = v1.2 (adds --no-dac-true-oversample).
    no_fir : bool
        If True, adds --no-dac-fir (noise-only measurement).
    no_noise : bool
        If True, adds --no-dac-noise (FIR-only measurement).
    """
    cmd = [
        spu94_bin, "reverb", "--preset", "off",
        "--dac",
        "--input-gain", "1.0", "--dry", "1.0",
    ]
    if not true_oversample:
        cmd.append("--no-dac-true-oversample")
    if no_fir:
        cmd.append("--no-dac-fir")
    if no_noise:
        cmd.append("--no-dac-noise")
    cmd.extend([input_path, output_path])

    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"

    r = subprocess.run(cmd, check=False, capture_output=True, env=env)
    if r.returncode != 0:
        stderr = r.stderr.decode("utf-8", errors="replace")[:300]
        raise RuntimeError(
            f"spu94 CLI failed (rc={r.returncode}): {stderr}")
    return r


def _read_left_channel(wav_path):
    """Read a WAV file and return the left channel as float64."""
    sr, data = wavfile.read(wav_path)
    if data.ndim == 2:
        return data[:, 0].astype(np.float64)
    return data.astype(np.float64)


def render_v12(input_path, output_path, spu94_bin, no_fir=False,
               no_noise=False):
    """Render through v1.2 approximate DAC path."""
    _run_cli(spu94_bin, input_path, output_path,
             true_oversample=False, no_fir=no_fir, no_noise=no_noise)


def render_v13(input_path, output_path, spu94_bin, no_fir=False,
               no_noise=False):
    """Render through v1.3 true oversampled DAC path."""
    _run_cli(spu94_bin, input_path, output_path,
             true_oversample=True, no_fir=no_fir, no_noise=no_noise)


def measure_frequency_response(spu94_bin, verbose=False):
    """Measure DAC FIR frequency response: v1.2 vs v1.3 transfer functions.

    Uses white noise input (same seed as dac_measure.py).
    Both renders use --no-dac-noise to isolate the FIR cascade.

    Returns (freq, v12_dB, v13_dB, max_difference_dB).
    """
    rng = np.random.default_rng(NOISE_SEED)
    noise = rng.integers(-15000, 15000, size=N_SAMPLES, dtype=np.int16)
    stereo_input = np.stack([noise, noise], axis=1)

    with tempfile.TemporaryDirectory(prefix="dac_cmp_freq_") as tmpd:
        in_path = os.path.join(tmpd, "noise_in.wav")
        out_v12 = os.path.join(tmpd, "noise_v12.wav")
        out_v13 = os.path.join(tmpd, "noise_v13.wav")

        wavfile.write(in_path, FS, stereo_input)

        render_v12(in_path, out_v12, spu94_bin, no_noise=True)
        render_v13(in_path, out_v13, spu94_bin, no_noise=True)

        sig_v12 = _read_left_channel(out_v12)
        sig_v13 = _read_left_channel(out_v13)

    input_f64 = noise.astype(np.float64)

    nperseg = 8192
    freq_in, psd_in = welch(input_f64, fs=FS, nperseg=nperseg)
    psd_in_safe = np.maximum(psd_in, 1e-20)

    # v1.2 transfer function
    freq_12, psd_12 = welch(sig_v12, fs=FS, nperseg=nperseg)
    tf_12 = 10 * np.log10(np.maximum(psd_12 / psd_in_safe, 1e-20))

    # v1.3 transfer function
    freq_13, psd_13 = welch(sig_v13, fs=FS, nperseg=nperseg)
    tf_13 = 10 * np.log10(np.maximum(psd_13 / psd_in_safe, 1e-20))

    # Normalize both to 0 dB in low-frequency band (100-1000 Hz)
    low_mask = (freq_12 >= 100) & (freq_12 <= 1000)
    if np.any(low_mask):
        tf_12 -= np.mean(tf_12[low_mask])
        tf_13 -= np.mean(tf_13[low_mask])

    # Max difference in passband (20-20kHz)
    passband = (freq_12 >= 20) & (freq_12 <= 20000)
    max_diff = float(np.max(np.abs(tf_12[passband] - tf_13[passband])))

    if verbose:
        print(f"  Freq bins: {len(freq_12)}, range {freq_12[1]:.1f}-"
              f"{freq_12[-1]:.1f} Hz")
        print(f"  Max passband difference (v1.2 vs v1.3): {max_diff:.4f} dB")

    return freq_12, tf_12, tf_13, max_diff


def measure_impulse_response(spu94_bin, verbose=False):
    """Measure impulse response: v1.2 vs v1.3 (FIR only, no noise).

    Returns (v12_impulse, v13_impulse, peak_diff, rms_diff).
    """
    # Single-sample impulse at L[0]=R[0]=AMP, then silence
    impulse = np.zeros((N_SAMPLES, 2), dtype=np.int16)
    impulse[0, 0] = AMP
    impulse[0, 1] = AMP

    with tempfile.TemporaryDirectory(prefix="dac_cmp_ir_") as tmpd:
        in_path = os.path.join(tmpd, "impulse_in.wav")
        out_v12 = os.path.join(tmpd, "impulse_v12.wav")
        out_v13 = os.path.join(tmpd, "impulse_v13.wav")

        wavfile.write(in_path, FS, impulse)

        render_v12(in_path, out_v12, spu94_bin, no_noise=True)
        render_v13(in_path, out_v13, spu94_bin, no_noise=True)

        ir_v12 = _read_left_channel(out_v12)
        ir_v13 = _read_left_channel(out_v13)

    # Trim to meaningful portion (first 512 samples covers any FIR ringing)
    trim = min(512, len(ir_v12), len(ir_v13))
    ir_v12_t = ir_v12[:trim]
    ir_v13_t = ir_v13[:trim]

    diff = ir_v12_t - ir_v13_t
    peak_diff = float(np.max(np.abs(diff)))
    rms_diff = float(np.sqrt(np.mean(diff ** 2)))

    if verbose:
        print(f"  IR length trimmed to: {trim} samples")
        print(f"  v1.2 peak: {np.max(np.abs(ir_v12_t)):.1f}, "
              f"v1.3 peak: {np.max(np.abs(ir_v13_t)):.1f}")
        print(f"  Peak sample difference: {peak_diff:.1f}")
        print(f"  RMS difference: {rms_diff:.2f}")

    return ir_v12_t, ir_v13_t, peak_diff, rms_diff


def measure_noise_floor(spu94_bin, verbose=False):
    """Measure noise floor: v1.2 vs v1.3 (noise only, no FIR).

    Both modes use the same HP-shaped noise model (spu94_dac_noise_step
    at 44.1kHz), so differences should be negligible.

    Returns (freq, v12_psd_dB, v13_psd_dB, rms_v12_dBFS, rms_v13_dBFS).
    """
    silence = np.zeros((N_SAMPLES, 2), dtype=np.int16)

    with tempfile.TemporaryDirectory(prefix="dac_cmp_noise_") as tmpd:
        in_path = os.path.join(tmpd, "silence_in.wav")
        out_v12 = os.path.join(tmpd, "silence_v12.wav")
        out_v13 = os.path.join(tmpd, "silence_v13.wav")

        wavfile.write(in_path, FS, silence)

        render_v12(in_path, out_v12, spu94_bin, no_fir=True)
        render_v13(in_path, out_v13, spu94_bin, no_fir=True)

        noise_v12 = _read_left_channel(out_v12)
        noise_v13 = _read_left_channel(out_v13)

    nperseg = 4096
    freq_12, psd_12 = welch(noise_v12, fs=FS, nperseg=nperseg)
    freq_13, psd_13 = welch(noise_v13, fs=FS, nperseg=nperseg)

    psd_12_dB = 10 * np.log10(np.maximum(psd_12, 1e-30))
    psd_13_dB = 10 * np.log10(np.maximum(psd_13, 1e-30))

    # RMS in dBFS (ref: 32767)
    rms_v12 = float(np.sqrt(np.mean(noise_v12 ** 2)))
    rms_v13 = float(np.sqrt(np.mean(noise_v13 ** 2)))
    rms_v12_dBFS = 20 * np.log10(max(rms_v12, 1e-15) / 32767.0)
    rms_v13_dBFS = 20 * np.log10(max(rms_v13, 1e-15) / 32767.0)

    if verbose:
        print(f"  v1.2 noise RMS: {rms_v12:.2f} ({rms_v12_dBFS:.1f} dBFS)")
        print(f"  v1.3 noise RMS: {rms_v13:.2f} ({rms_v13_dBFS:.1f} dBFS)")
        nz_12 = np.count_nonzero(noise_v12)
        nz_13 = np.count_nonzero(noise_v13)
        print(f"  Non-zero samples: v1.2={nz_12}, v1.3={nz_13} "
              f"(of {len(noise_v12)})")

    return freq_12, psd_12_dB, psd_13_dB, float(rms_v12_dBFS), \
        float(rms_v13_dBFS)


def measure_time_domain(spu94_bin, verbose=False):
    """Measure time-domain waveform: v1.2 vs v1.3 (full DAC: FIR + noise).

    Uses a log chirp (20-20kHz) as music-like test signal.

    Returns (v12_signal, v13_signal, max_sample_diff, rms_diff_dBFS).
    """
    t = np.arange(N_SAMPLES) / FS
    sweep = (AMP * chirp(t, 20, DURATION, 20000,
                         method='logarithmic')).astype(np.int16)
    stereo_input = np.stack([sweep, sweep], axis=1)

    with tempfile.TemporaryDirectory(prefix="dac_cmp_td_") as tmpd:
        in_path = os.path.join(tmpd, "chirp_in.wav")
        out_v12 = os.path.join(tmpd, "chirp_v12.wav")
        out_v13 = os.path.join(tmpd, "chirp_v13.wav")

        wavfile.write(in_path, FS, stereo_input)

        render_v12(in_path, out_v12, spu94_bin)
        render_v13(in_path, out_v13, spu94_bin)

        sig_v12 = _read_left_channel(out_v12)
        sig_v13 = _read_left_channel(out_v13)

    # Ensure same length
    min_len = min(len(sig_v12), len(sig_v13))
    sig_v12 = sig_v12[:min_len]
    sig_v13 = sig_v13[:min_len]

    diff = sig_v12 - sig_v13
    max_sample_diff = float(np.max(np.abs(diff)))
    rms_diff = float(np.sqrt(np.mean(diff ** 2)))
    rms_diff_dBFS = 20 * np.log10(max(rms_diff, 1e-15) / 32767.0)

    if verbose:
        print(f"  Signal length: {min_len} samples")
        print(f"  v1.2 RMS: {np.sqrt(np.mean(sig_v12**2)):.1f}, "
              f"v1.3 RMS: {np.sqrt(np.mean(sig_v13**2)):.1f}")
        print(f"  Max sample difference: {max_sample_diff:.1f}")
        print(f"  RMS difference: {rms_diff:.2f} ({rms_diff_dBFS:.1f} dBFS)")

    return sig_v12, sig_v13, max_sample_diff, rms_diff_dBFS


def plot_comparison(freq, tf_12, tf_13,
                    ir_v12, ir_v13,
                    noise_freq, noise_12_dB, noise_13_dB,
                    td_v12, td_v13,
                    output_path):
    """Generate 4-panel comparison plot (2x2).

    Panels:
      Top-left:     Frequency response overlay (v1.2 vs v1.3)
      Top-right:    Impulse response overlay
      Bottom-left:  Noise floor PSD overlay
      Bottom-right: Time-domain waveform difference
    """
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt

    fig, ((ax1, ax2), (ax3, ax4)) = plt.subplots(2, 2, figsize=(14, 10))

    # --- Top-left: Frequency response ---
    f_mask = freq > 0
    ax1.semilogx(freq[f_mask], tf_12[f_mask], 'b-', linewidth=0.8,
                 label='v1.2 (approximate)', alpha=0.9)
    ax1.semilogx(freq[f_mask], tf_13[f_mask], 'r-', linewidth=0.8,
                 label='v1.3 (true 8x)', alpha=0.9)
    ax1.set_xlim(20, 22050)
    ax1.set_ylim(-3, 0.5)
    ax1.set_xlabel('Frequency (Hz)')
    ax1.set_ylabel('Magnitude (dB)')
    ax1.set_title('Frequency Response: v1.2 vs v1.3 (FIR only)')
    ax1.legend(loc='lower left', fontsize=8)
    ax1.grid(True, alpha=0.3)

    # --- Top-right: Impulse response ---
    n_ir = len(ir_v12)
    t_ir = np.arange(n_ir) / FS * 1000  # ms
    ax2.plot(t_ir, ir_v12, 'b-', linewidth=0.8,
             label='v1.2 (approximate)', alpha=0.7)
    ax2.plot(t_ir, ir_v13, 'r-', linewidth=0.8,
             label='v1.3 (true 8x)', alpha=0.7)
    ax2.set_xlabel('Time (ms)')
    ax2.set_ylabel('Amplitude')
    ax2.set_title('Impulse Response: v1.2 vs v1.3 (FIR only)')
    ax2.legend(loc='upper right', fontsize=8)
    ax2.grid(True, alpha=0.3)
    # Zoom to first 2ms where the impulse lives
    ax2.set_xlim(0, min(2.0, t_ir[-1]))

    # --- Bottom-left: Noise floor PSD ---
    n_mask = noise_freq > 0
    ax3.semilogx(noise_freq[n_mask], noise_12_dB[n_mask], 'b-',
                 linewidth=0.8, label='v1.2 noise', alpha=0.9)
    ax3.semilogx(noise_freq[n_mask], noise_13_dB[n_mask], 'r-',
                 linewidth=0.8, label='v1.3 noise', alpha=0.9)
    ax3.set_xlim(100, 22050)
    ax3.set_xlabel('Frequency (Hz)')
    ax3.set_ylabel('PSD (dB)')
    ax3.set_title('Noise Floor PSD: v1.2 vs v1.3 (noise only)')
    ax3.legend(loc='upper left', fontsize=8)
    ax3.grid(True, alpha=0.3)

    # --- Bottom-right: Time-domain difference ---
    min_len = min(len(td_v12), len(td_v13))
    diff = td_v12[:min_len] - td_v13[:min_len]
    t_td = np.arange(min_len) / FS
    ax4.plot(t_td, diff, 'k-', linewidth=0.3, alpha=0.6)
    ax4.set_xlabel('Time (s)')
    ax4.set_ylabel('Sample Difference')
    ax4.set_title('Time-Domain Difference: v1.2 - v1.3 (full DAC)')
    ax4.grid(True, alpha=0.3)

    fig.suptitle('SPU-94 DAC Comparison: v1.2 (approximate) vs v1.3 '
                 '(true 8x oversampled)',
                 fontsize=13, fontweight='bold', y=0.98)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    fig.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="DAC comparison: v1.2 (approximate) vs v1.3 "
                    "(true oversampled)")
    parser.add_argument('--plot', action='store_true',
                        help='Generate tools/dac_compare.png report')
    parser.add_argument('--verbose', action='store_true',
                        help='Print detailed diagnostics')
    args = parser.parse_args()

    spu94_bin = locate_cli()
    if args.verbose:
        print(f"Using CLI: {spu94_bin}")

    # --- 1. Frequency Response ---
    if args.verbose:
        print("\n--- Frequency Response (FIR only, no noise) ---")
    freq, tf_12, tf_13, freq_max_diff = measure_frequency_response(
        spu94_bin, verbose=args.verbose)

    # --- 2. Impulse Response ---
    if args.verbose:
        print("\n--- Impulse Response (FIR only, no noise) ---")
    ir_v12, ir_v13, ir_peak_diff, ir_rms_diff = measure_impulse_response(
        spu94_bin, verbose=args.verbose)

    # --- 3. Noise Floor ---
    if args.verbose:
        print("\n--- Noise Floor (noise only, no FIR) ---")
    noise_freq, noise_12_dB, noise_13_dB, noise_rms_v12, noise_rms_v13 = \
        measure_noise_floor(spu94_bin, verbose=args.verbose)

    # --- 4. Time-Domain ---
    if args.verbose:
        print("\n--- Time-Domain (full DAC: FIR + noise) ---")
    td_v12, td_v13, td_max_diff, td_rms_dBFS = measure_time_domain(
        spu94_bin, verbose=args.verbose)

    # --- Summary Table ---
    print("\n=== v1.2 vs v1.3 DAC Comparison ===\n")
    print(f"  Frequency response: {freq_max_diff:.4f} dB max deviation "
          f"(20-20kHz passband)")
    print(f"  Impulse response:   peak diff = {ir_peak_diff:.1f} samples, "
          f"RMS diff = {ir_rms_diff:.2f}")
    print(f"  Noise floor:        v1.2 = {noise_rms_v12:.1f} dBFS, "
          f"v1.3 = {noise_rms_v13:.1f} dBFS")
    print(f"  Time-domain:        max diff = {td_max_diff:.1f} samples, "
          f"RMS diff = {td_rms_dBFS:.1f} dBFS")

    # --- Plot ---
    if args.plot:
        png_path = str(Path(__file__).resolve().parent / "dac_compare.png")
        plot_comparison(freq, tf_12, tf_13,
                        ir_v12, ir_v13,
                        noise_freq, noise_12_dB, noise_13_dB,
                        td_v12, td_v13,
                        png_path)
        print(f"\n  Saved: {png_path}")

    print("\nDone. (This is a measurement script -- always exits 0.)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
