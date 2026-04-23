#!/usr/bin/env python3
"""scripts/regenerate_goldens.py — Phase 7 TEST-04 + TEST-08 golden generator.

D-09 .wav + .sha256 sidecar pair; D-10 tests/golden/<preset>/<input>.{wav,sha256};
D-11 five inputs (impulse/white_noise/sine_1khz/silence/sweep); D-12 regeneration
requires ADR.

Modes:
  default (no args) — regenerate all 50 goldens + sidecars, overwriting.
  --check          — for each committed sidecar, re-render and compare SHA-256;
                     exit 1 on any mismatch (identifies preset/input pair on stderr).

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


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Regenerate or verify the 50 golden WAV + SHA-256 sidecar pairs."
    )
    ap.add_argument(
        "--check", action="store_true",
        help="verify each committed sidecar by re-rendering into a scratch "
             "directory and comparing SHA-256; exit 1 on mismatch.",
    )
    args = ap.parse_args()

    spu94_bin = locate_cli()
    failures: list[str] = []
    total = len(PRESETS) * len(INPUTS)

    with tempfile.TemporaryDirectory(prefix="spu94_goldens_") as tmpd:
        tmp_in = Path(tmpd) / "input.wav"

        for preset in PRESETS:
            preset_dir = GOLDEN_ROOT / preset
            preset_dir.mkdir(parents=True, exist_ok=True)
            for input_name in INPUTS:
                out_wav = preset_dir / f"{input_name}.wav"
                out_sha = preset_dir / f"{input_name}.wav.sha256"

                if args.check:
                    # Read committed sidecar digest.
                    if out_sha.exists():
                        committed_line = out_sha.read_text().strip()
                        committed = committed_line.split()[0] if committed_line else ""
                    else:
                        committed = ""

                    # (a) Committed .wav bytes must match the sidecar — catches
                    # tampering with either the .wav or the sidecar alone.
                    if out_wav.exists():
                        wav_digest = sha256_of(out_wav)
                    else:
                        wav_digest = ""
                    if wav_digest != committed:
                        failures.append(
                            f"{preset}/{input_name}: committed-wav mismatches sidecar — "
                            f"sidecar={committed[:16] if committed else '<missing>'}... "
                            f"wav={wav_digest[:16] if wav_digest else '<missing>'}..."
                        )
                        # Still re-render below so a later sidecar forge plus
                        # matching .wav forge is still caught by the fresh check.

                    # (b) Fresh re-render must match the sidecar — this is the
                    # reproducibility gate (host vs container). If the wav
                    # already mismatched in (a), this is additional signal.
                    scratch = Path(tmpd) / f"{preset}_{input_name}.wav"
                    render_golden(preset, input_name, scratch, tmp_in, spu94_bin)
                    fresh_digest = sha256_of(scratch)
                    if fresh_digest != committed:
                        failures.append(
                            f"{preset}/{input_name}: fresh render mismatches sidecar — "
                            f"committed={committed[:16] if committed else '<missing>'}... "
                            f"fresh={fresh_digest[:16]}..."
                        )
                else:
                    render_golden(preset, input_name, out_wav, tmp_in, spu94_bin)
                    digest = sha256_of(out_wav)
                    # Sidecar body: <64 hex>  <basename>\n (sha256sum-compatible).
                    out_sha.write_text(f"{digest}  {input_name}.wav\n")

    if failures:
        for f in failures:
            print(f"FAIL: {f}", file=sys.stderr)
        return 1

    if args.check:
        print(f"PASS: {total}/{total} goldens match")
    else:
        print(f"Regenerated {total} goldens under {GOLDEN_ROOT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
