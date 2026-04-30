#!/usr/bin/env python3
"""scripts/regenerate_goldens.py — Phase 7 TEST-04 + TEST-08 golden generator,
extended Phase 4 Plan 02 for ADPCM golden corpus.

D-09 .wav + .sha256 sidecar pair; D-10 tests/golden/<preset>/<input>.{wav,sha256};
D-11 five inputs (impulse/white_noise/sine_1khz/silence/sweep); D-12 regeneration
requires ADR.

Phase 4 D-03/D-04/D-05: ADPCM goldens in tests/golden/<preset>/adpcm/<input>.wav
with 3 inputs (impulse, sine_1khz, chirp) x 10 presets = 30 pairs.

Modes:
  default (no args) — regenerate all 50 reverb-only goldens + sidecars, overwriting.
  --check           — verify each committed reverb sidecar by re-rendering; exit 1 on mismatch.
  --adpcm           — regenerate all 30 ADPCM goldens + sidecars, overwriting.
  --check-adpcm     — verify each committed ADPCM sidecar by re-rendering; exit 1 on mismatch.
  --dac             — regenerate all 50 full-pipeline DAC goldens + sidecars.
  --check-dac       — verify committed DAC sidecars by re-rendering; exit 1 on mismatch.
  --dac-isolated    — regenerate 5 isolated DAC-only goldens + sidecars.
  --check-dac-isolated — verify committed isolated DAC sidecars; exit 1 on mismatch.

Phase 9 D-01..D-05: DAC goldens — 55 total:
  50 full-pipeline in tests/golden/<preset>/dac/<input>.wav (10 presets x 5 inputs)
  5 isolated in tests/golden/dac_isolated/<input>.wav (Off preset, DAC-only)

Flags can be combined: `--adpcm` with no other flags generates only ADPCM goldens.
Running with no flags generates only the original 50 reverb goldens.

Determinism discipline (mirrors the reproducibility Docker container):
  - LC_ALL=C, TZ=UTC, SOURCE_DATE_EPOCH=1704067200 forced in the env that
    shells out to the spu94 CLI, so dr_wav + locale-sensitive code in the
    toolchain can't vary output bytes between host and container.
  - Preset names + input names are module-level literals (closed allowlist).
    No CLI arg, env var, or external config reaches filename construction —
    path-traversal is structurally impossible (T-07-02-D mitigation).
  - WAV generation uses scipy.io.wavfile (in-process) for the fixed input
    set; the reverb-rendered golden is produced by shelling out to the
    native `spu94` CLI which writes via vendored dr_wav (proven
    deterministic in Phase 6 CI).

Standard input set (D-11; parameters locked here per plan's discretion):
  All inputs: 44100 Hz, int16 PCM, stereo (2 channels), 2.0 seconds.
  impulse     : L[0]=R[0]=16000; zeros after.
  silence     : all zeros.
  white_noise : np.random.default_rng(0x1094_DADA).integers(-16000,16000).
  sine_1khz   : 16000*sin(2*pi*1000*t/Fs), applied identically L=R.
  sweep       : 16000*scipy.signal.chirp(t, 20, 2.0, 20000, 'logarithmic'),
                applied identically L=R.
  chirp       : identical to sweep (20Hz-20kHz log chirp); named separately
                for ADPCM corpus per D-05.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import chirp

# ----------------------------------------------------------------------------
# Determinism env — set BEFORE any subprocess invocation.
# ----------------------------------------------------------------------------
os.environ.setdefault("LC_ALL", "C")
os.environ.setdefault("TZ", "UTC")
os.environ.setdefault("SOURCE_DATE_EPOCH", "1704067200")  # 2024-01-01 UTC

# ----------------------------------------------------------------------------
# Closed allowlists — module-level literals, never from external input.
# T-07-02-D mitigation: no user-controlled name reaches Path() construction.
# ----------------------------------------------------------------------------
PRESETS = [
    "off", "room", "studio_a", "studio_b", "studio_c",
    "hall", "half_echo", "space_echo", "echo", "delay",
]
INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]

# Phase 4 D-04: 3 ADPCM inputs (subset + chirp). Closed allowlist.
ADPCM_INPUTS = ["impulse", "sine_1khz", "chirp"]

# Phase 9 D-02: DAC inputs — same 5 as the reverb corpus. Closed allowlist.
DAC_INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]

# ----------------------------------------------------------------------------
# Standard input set parameters (D-11 lock).
# ----------------------------------------------------------------------------
FS = 44100            # sample rate (Hz)
DURATION_SEC = 2.0    # seconds
N = int(FS * DURATION_SEC)  # 88200 samples per channel
AMP = 16000           # peak int16 amplitude for all generated inputs
NOISE_SEED = 0x1094_DADA  # fixed PRNG seed (SPU94 + DADA — not derived from anything external)
SWEEP_F0 = 20.0       # sweep start frequency (Hz)
SWEEP_F1 = 20000.0    # sweep end frequency (Hz)
SWEEP_METHOD = "logarithmic"

GOLDEN_ROOT = Path("tests/golden")


def generate_input(name: str) -> np.ndarray:
    """Return the D-11 standard input `name` as an (N, 2) int16 array."""
    if name == "impulse":
        x = np.zeros((N, 2), dtype=np.int16)
        x[0, 0] = AMP
        x[0, 1] = AMP
        return x
    if name == "silence":
        return np.zeros((N, 2), dtype=np.int16)
    if name == "white_noise":
        rng = np.random.default_rng(NOISE_SEED)
        return rng.integers(-AMP, AMP, size=(N, 2), dtype=np.int16)
    if name == "sine_1khz":
        t = np.arange(N, dtype=np.float64) / FS
        y = (AMP * np.sin(2.0 * np.pi * 1000.0 * t)).astype(np.int16)
        return np.stack([y, y], axis=1)
    if name == "sweep":
        t = np.arange(N, dtype=np.float64) / FS
        y = (AMP * chirp(t, SWEEP_F0, DURATION_SEC, SWEEP_F1,
                         method=SWEEP_METHOD)).astype(np.int16)
        return np.stack([y, y], axis=1)
    if name == "chirp":
        # Phase 4 D-05: chirp is a 20Hz-20kHz log sweep — identical generation
        # to the existing "sweep" input. Named separately for the ADPCM corpus
        # so ADPCM-on + chirp vs reverb-only + sweep provides a useful
        # before/after comparison on the same underlying waveform.
        t = np.arange(N, dtype=np.float64) / FS
        y = (AMP * chirp(t, SWEEP_F0, DURATION_SEC, SWEEP_F1,
                         method=SWEEP_METHOD)).astype(np.int16)
        return np.stack([y, y], axis=1)
    # Unreachable: closed allowlist gate above.
    raise ValueError(f"unknown input: {name!r}")


def locate_cli() -> str:
    """Locate the spu94 CLI binary.

    Priority:
      1. $SPU94_BIN env var (explicit override)
      2. build/src/cli/spu94 (dev tree)
      3. 'spu94' on PATH (installed container)
    """
    override = os.environ.get("SPU94_BIN")
    if override:
        return override
    dev_build = Path("build/src/cli/spu94")
    if dev_build.is_file():
        return str(dev_build)
    return "spu94"


def render_golden(preset: str, input_name: str, out_path: Path,
                  tmp_in_path: Path, spu94_bin: str) -> None:
    """Write the input WAV to tmp_in_path, run spu94, capturing to out_path."""
    x = generate_input(input_name)
    wavfile.write(str(tmp_in_path), FS, x)

    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    env["SOURCE_DATE_EPOCH"] = "1704067200"

    r = subprocess.run(
        [spu94_bin, "--preset", preset, str(tmp_in_path), str(out_path)],
        check=False,
        capture_output=True,
        env=env,
    )
    if r.returncode != 0:
        stderr_snip = r.stderr.decode("utf-8", errors="replace")[:200].strip()
        raise RuntimeError(
            f"{spu94_bin} failed on {preset}/{input_name} (rc={r.returncode}): "
            f"{stderr_snip}"
        )


def render_adpcm_golden(preset: str, input_name: str, out_path: Path,
                        tmp_in_path: Path, spu94_bin: str) -> None:
    """Write the input WAV, run spu94 reverb with --adpcm, capture to out_path.

    Phase 4 D-03: ADPCM golden = reverb + ADPCM coloration enabled.
    """
    x = generate_input(input_name)
    wavfile.write(str(tmp_in_path), FS, x)

    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    env["SOURCE_DATE_EPOCH"] = "1704067200"

    r = subprocess.run(
        [spu94_bin, "reverb", "--adpcm", "--preset", preset,
         str(tmp_in_path), str(out_path)],
        check=False,
        capture_output=True,
        env=env,
    )
    if r.returncode != 0:
        stderr_snip = r.stderr.decode("utf-8", errors="replace")[:200].strip()
        raise RuntimeError(
            f"{spu94_bin} (adpcm) failed on {preset}/{input_name} "
            f"(rc={r.returncode}): {stderr_snip}"
        )


def render_dac_golden(preset: str, input_name: str, out_path: Path,
                      tmp_in_path: Path, spu94_bin: str) -> None:
    """Write the input WAV, run spu94 reverb with --dac, capture to out_path.

    Phase 9 D-03: DAC golden = reverb + DAC coloration enabled (full pipeline).
    """
    x = generate_input(input_name)
    wavfile.write(str(tmp_in_path), FS, x)

    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    env["SOURCE_DATE_EPOCH"] = "1704067200"

    r = subprocess.run(
        [spu94_bin, "reverb", "--dac", "--preset", preset,
         str(tmp_in_path), str(out_path)],
        check=False,
        capture_output=True,
        env=env,
    )
    if r.returncode != 0:
        stderr_snip = r.stderr.decode("utf-8", errors="replace")[:200].strip()
        raise RuntimeError(
            f"{spu94_bin} (dac) failed on {preset}/{input_name} "
            f"(rc={r.returncode}): {stderr_snip}"
        )


def render_dac_isolated(input_name: str, out_path: Path,
                        tmp_in_path: Path, spu94_bin: str) -> None:
    """Write the input WAV, run spu94 reverb with --dac --preset off, capture.

    Phase 9 D-04: Isolated DAC-only golden — Off preset (no reverb) with DAC
    enabled, capturing the pure DAC model fingerprint. Uses --preset off
    explicitly (CLI requires a preset flag).
    """
    x = generate_input(input_name)
    wavfile.write(str(tmp_in_path), FS, x)

    env = dict(os.environ)
    env["LC_ALL"] = "C"
    env["TZ"] = "UTC"
    env["SOURCE_DATE_EPOCH"] = "1704067200"

    r = subprocess.run(
        [spu94_bin, "reverb", "--dac", "--preset", "off",
         str(tmp_in_path), str(out_path)],
        check=False,
        capture_output=True,
        env=env,
    )
    if r.returncode != 0:
        stderr_snip = r.stderr.decode("utf-8", errors="replace")[:200].strip()
        raise RuntimeError(
            f"{spu94_bin} (dac-isolated) failed on {input_name} "
            f"(rc={r.returncode}): {stderr_snip}"
        )


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def _check_loop(presets, inputs, golden_root, render_fn, tmpd, tmp_in,
                spu94_bin, failures, label=""):
    """Shared check logic for reverb and ADPCM golden verification."""
    for preset in presets:
        for input_name in inputs:
            if label:
                preset_dir = golden_root / preset / label
                tag = f"{preset}/{label}/{input_name}"
            else:
                preset_dir = golden_root / preset
                tag = f"{preset}/{input_name}"

            out_wav = preset_dir / f"{input_name}.wav"
            out_sha = preset_dir / f"{input_name}.wav.sha256"

            # Read committed sidecar digest.
            if out_sha.exists():
                committed_line = out_sha.read_text().strip()
                committed = committed_line.split()[0] if committed_line else ""
            else:
                committed = ""

            # (a) Committed .wav bytes must match the sidecar.
            if out_wav.exists():
                wav_digest = sha256_of(out_wav)
            else:
                wav_digest = ""
            if wav_digest != committed:
                failures.append(
                    f"{tag}: committed-wav mismatches sidecar — "
                    f"sidecar={committed[:16] if committed else '<missing>'}... "
                    f"wav={wav_digest[:16] if wav_digest else '<missing>'}..."
                )

            # (b) Fresh re-render must match the sidecar.
            scratch = Path(tmpd) / f"{preset}_{label}_{input_name}.wav"
            render_fn(preset, input_name, scratch, tmp_in, spu94_bin)
            fresh_digest = sha256_of(scratch)
            if fresh_digest != committed:
                failures.append(
                    f"{tag}: fresh render mismatches sidecar — "
                    f"committed={committed[:16] if committed else '<missing>'}... "
                    f"fresh={fresh_digest[:16]}..."
                )


def _generate_loop(presets, inputs, golden_root, render_fn, tmp_in,
                   spu94_bin, label=""):
    """Shared generation logic for reverb and ADPCM goldens."""
    for preset in presets:
        if label:
            preset_dir = golden_root / preset / label
        else:
            preset_dir = golden_root / preset
        preset_dir.mkdir(parents=True, exist_ok=True)
        for input_name in inputs:
            out_wav = preset_dir / f"{input_name}.wav"
            out_sha = preset_dir / f"{input_name}.wav.sha256"
            render_fn(preset, input_name, out_wav, tmp_in, spu94_bin)
            digest = sha256_of(out_wav)
            out_sha.write_text(f"{digest}  {input_name}.wav\n")


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Regenerate or verify golden WAV + SHA-256 sidecar pairs."
    )
    ap.add_argument(
        "--check", action="store_true",
        help="verify each committed reverb sidecar by re-rendering into a "
             "scratch directory and comparing SHA-256; exit 1 on mismatch.",
    )
    ap.add_argument(
        "--adpcm", action="store_true",
        help="regenerate all 30 ADPCM goldens + sidecars (10 presets x 3 inputs).",
    )
    ap.add_argument(
        "--check-adpcm", action="store_true",
        help="verify each committed ADPCM sidecar by re-rendering with "
             "--adpcm and comparing SHA-256; exit 1 on mismatch.",
    )
    ap.add_argument(
        "--dac", action="store_true",
        help="regenerate all 50 full-pipeline DAC goldens + sidecars "
             "(10 presets x 5 inputs).",
    )
    ap.add_argument(
        "--check-dac", action="store_true",
        help="verify each committed DAC sidecar by re-rendering with "
             "--dac and comparing SHA-256; exit 1 on mismatch.",
    )
    ap.add_argument(
        "--dac-isolated", action="store_true",
        help="regenerate 5 isolated DAC-only goldens + sidecars "
             "(Off preset, no reverb, DAC only).",
    )
    ap.add_argument(
        "--check-dac-isolated", action="store_true",
        help="verify each committed isolated DAC sidecar by re-rendering "
             "and comparing SHA-256; exit 1 on mismatch.",
    )
    args = ap.parse_args()

    spu94_bin = locate_cli()
    failures: list[str] = []

    # Determine what to run. With no flags at all: reverb generation (original behavior).
    # --adpcm alone: ADPCM generation only.
    # --check alone: reverb check only.
    # --check-adpcm alone: ADPCM check only.
    # --dac alone: DAC full-pipeline generation only.
    # --check-dac alone: DAC full-pipeline check only.
    # --dac-isolated alone: DAC isolated generation only.
    # --check-dac-isolated alone: DAC isolated check only.
    any_explicit = (args.check or args.adpcm or args.check_adpcm
                    or args.dac or args.check_dac
                    or args.dac_isolated or args.check_dac_isolated)
    do_reverb_gen = not any_explicit
    do_reverb_check = args.check
    do_adpcm_gen = args.adpcm and not args.check_adpcm
    do_adpcm_check = args.check_adpcm
    do_dac_gen = args.dac and not args.check_dac
    do_dac_check = args.check_dac
    do_dac_isolated_gen = args.dac_isolated and not args.check_dac_isolated
    do_dac_isolated_check = args.check_dac_isolated

    reverb_total = len(PRESETS) * len(INPUTS)
    adpcm_total = len(PRESETS) * len(ADPCM_INPUTS)
    dac_total = len(PRESETS) * len(DAC_INPUTS)
    dac_isolated_total = len(DAC_INPUTS)

    with tempfile.TemporaryDirectory(prefix="spu94_goldens_") as tmpd:
        tmp_in = Path(tmpd) / "input.wav"

        # --- Reverb goldens (original 50) ---
        if do_reverb_check:
            _check_loop(PRESETS, INPUTS, GOLDEN_ROOT, render_golden,
                        tmpd, tmp_in, spu94_bin, failures)
        elif do_reverb_gen:
            _generate_loop(PRESETS, INPUTS, GOLDEN_ROOT, render_golden,
                           tmp_in, spu94_bin)

        # --- ADPCM goldens (30) ---
        if do_adpcm_check:
            _check_loop(PRESETS, ADPCM_INPUTS, GOLDEN_ROOT,
                        render_adpcm_golden, tmpd, tmp_in, spu94_bin,
                        failures, label="adpcm")
        elif do_adpcm_gen:
            _generate_loop(PRESETS, ADPCM_INPUTS, GOLDEN_ROOT,
                           render_adpcm_golden, tmp_in, spu94_bin,
                           label="adpcm")

        # --- DAC full-pipeline goldens (50) ---
        if do_dac_check:
            _check_loop(PRESETS, DAC_INPUTS, GOLDEN_ROOT,
                        render_dac_golden, tmpd, tmp_in, spu94_bin,
                        failures, label="dac")
        elif do_dac_gen:
            _generate_loop(PRESETS, DAC_INPUTS, GOLDEN_ROOT,
                           render_dac_golden, tmp_in, spu94_bin,
                           label="dac")

        # --- DAC isolated goldens (5) ---
        if do_dac_isolated_check:
            iso_dir = GOLDEN_ROOT / "dac_isolated"
            for input_name in DAC_INPUTS:
                tag = f"dac_isolated/{input_name}"
                out_wav = iso_dir / f"{input_name}.wav"
                out_sha = iso_dir / f"{input_name}.wav.sha256"

                # Read committed sidecar digest.
                if out_sha.exists():
                    committed_line = out_sha.read_text().strip()
                    committed = committed_line.split()[0] if committed_line else ""
                else:
                    committed = ""

                # (a) Committed .wav bytes must match the sidecar.
                if out_wav.exists():
                    wav_digest = sha256_of(out_wav)
                else:
                    wav_digest = ""
                if wav_digest != committed:
                    failures.append(
                        f"{tag}: committed-wav mismatches sidecar -- "
                        f"sidecar={committed[:16] if committed else '<missing>'}... "
                        f"wav={wav_digest[:16] if wav_digest else '<missing>'}..."
                    )

                # (b) Fresh re-render must match the sidecar.
                scratch = Path(tmpd) / f"dac_isolated_{input_name}.wav"
                render_dac_isolated(input_name, scratch, tmp_in, spu94_bin)
                fresh_digest = sha256_of(scratch)
                if fresh_digest != committed:
                    failures.append(
                        f"{tag}: fresh render mismatches sidecar -- "
                        f"committed={committed[:16] if committed else '<missing>'}... "
                        f"fresh={fresh_digest[:16]}..."
                    )

        elif do_dac_isolated_gen:
            iso_dir = GOLDEN_ROOT / "dac_isolated"
            iso_dir.mkdir(parents=True, exist_ok=True)
            for input_name in DAC_INPUTS:
                out_wav = iso_dir / f"{input_name}.wav"
                out_sha = iso_dir / f"{input_name}.wav.sha256"
                render_dac_isolated(input_name, out_wav, tmp_in, spu94_bin)
                digest = sha256_of(out_wav)
                out_sha.write_text(f"{digest}  {input_name}.wav\n")

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1

    # Summary output.
    parts = []
    if do_reverb_check:
        parts.append(f"PASS: {reverb_total}/{reverb_total} reverb goldens match")
    elif do_reverb_gen:
        parts.append(f"Regenerated {reverb_total} reverb goldens under {GOLDEN_ROOT}")
    if do_adpcm_check:
        parts.append(f"PASS: {adpcm_total}/{adpcm_total} ADPCM goldens match")
    elif do_adpcm_gen:
        parts.append(f"Regenerated {adpcm_total} ADPCM goldens under {GOLDEN_ROOT}")
    if do_dac_check:
        parts.append(f"PASS: {dac_total}/{dac_total} DAC goldens match")
    elif do_dac_gen:
        parts.append(f"Regenerated {dac_total} DAC goldens under {GOLDEN_ROOT}")
    if do_dac_isolated_check:
        parts.append(f"PASS: {dac_isolated_total}/{dac_isolated_total} DAC isolated goldens match")
    elif do_dac_isolated_gen:
        parts.append(f"Regenerated {dac_isolated_total} DAC isolated goldens under {GOLDEN_ROOT}")
    for p in parts:
        print(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
