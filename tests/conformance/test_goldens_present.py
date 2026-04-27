"""tests/conformance/test_goldens_present.py — Phase 7 Plan 02 Task 2,
extended Phase 4 Plan 02 for ADPCM golden corpus.

Structural conformance: the 80-golden corpus (50 reverb + 30 ADPCM) exists
and sidecars are well-formed. This is the cheap always-runnable counterpart
to the Dockerfile-repro + --check reproducibility gate:

  - 50 reverb .wav files under tests/golden/<preset>/<input>.wav
  - 30 ADPCM .wav files under tests/golden/<preset>/adpcm/<input>.wav
  - 80 .wav.sha256 sidecars alongside each .wav
  - every sidecar is formatted "<64-hex>  <filename>\n" (sha256sum-compatible)
  - spot-check reverb + ADPCM sidecars match the SHA-256 of their .wav

Registered with ctest via tests/conformance/CMakeLists.txt (test name:
`goldens_present`). Parametrized across presets x inputs — ~160 test
cases pass per run + 6 spot-checks.
"""
from __future__ import annotations

import hashlib
import re
from pathlib import Path

import pytest

# --------------------------------------------------------------------------
# Closed allowlist — mirrors scripts/regenerate_goldens.py. If these fall
# out of sync with the generator, the golden corpus integrity has already
# broken; the presence test deliberately hard-codes them to catch that.
# --------------------------------------------------------------------------
PRESETS = [
    "off", "room", "studio_a", "studio_b", "studio_c",
    "hall", "half_echo", "space_echo", "echo", "delay",
]
INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]

# Phase 4 D-04: 3 ADPCM inputs per preset.
ADPCM_INPUTS = ["impulse", "sine_1khz", "chirp"]

# Resolved from this test file: tests/conformance/ -> ../.. = repo root
REPO_ROOT = Path(__file__).resolve().parents[2]
ROOT = REPO_ROOT / "tests" / "golden"


# ============================================================================
# Reverb golden tests (original 50)
# ============================================================================

@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", INPUTS)
def test_wav_exists(preset: str, input_name: str):
    """Each of the 50 reverb .wav files is present as a regular file."""
    p = ROOT / preset / f"{input_name}.wav"
    assert p.is_file(), f"missing golden WAV: {p}"


@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", INPUTS)
def test_sidecar_exists_and_format(preset: str, input_name: str):
    """Each reverb sidecar exists and matches sha256sum's output format."""
    p = ROOT / preset / f"{input_name}.wav.sha256"
    assert p.is_file(), f"missing sidecar: {p}"
    text = p.read_text()
    m = re.match(r"^[0-9a-f]{64}\s+\S+\s*$", text)
    assert m is not None, f"sidecar {p} malformed: {text!r}"


@pytest.mark.parametrize(
    "preset,input_name",
    [("hall", "impulse"), ("room", "sine_1khz"), ("studio_a", "sweep")],
)
def test_sidecar_matches_wav(preset: str, input_name: str):
    """Spot-check: re-hash the reverb .wav, compare against sidecar."""
    wav = ROOT / preset / f"{input_name}.wav"
    sha_file = ROOT / preset / f"{input_name}.wav.sha256"
    digest = hashlib.sha256(wav.read_bytes()).hexdigest()
    sidecar_digest = sha_file.read_text().split()[0]
    assert digest == sidecar_digest, (
        f"{preset}/{input_name}: wav={digest[:16]}... "
        f"sidecar={sidecar_digest[:16]}..."
    )


# ============================================================================
# ADPCM golden tests (Phase 4 Plan 02 — 30 files)
# ============================================================================

@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", ADPCM_INPUTS)
def test_adpcm_wav_exists(preset: str, input_name: str):
    """Each of the 30 ADPCM .wav files is present as a regular file."""
    p = ROOT / preset / "adpcm" / f"{input_name}.wav"
    assert p.is_file(), f"missing ADPCM golden WAV: {p}"


@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", ADPCM_INPUTS)
def test_adpcm_sidecar_exists_and_format(preset: str, input_name: str):
    """Each ADPCM sidecar exists and matches sha256sum's output format."""
    p = ROOT / preset / "adpcm" / f"{input_name}.wav.sha256"
    assert p.is_file(), f"missing ADPCM sidecar: {p}"
    text = p.read_text()
    m = re.match(r"^[0-9a-f]{64}\s+\S+\s*$", text)
    assert m is not None, f"ADPCM sidecar {p} malformed: {text!r}"


@pytest.mark.parametrize(
    "preset,input_name",
    [("hall", "impulse"), ("room", "sine_1khz"), ("studio_a", "chirp")],
)
def test_adpcm_sidecar_matches_wav(preset: str, input_name: str):
    """Spot-check: re-hash the ADPCM .wav, compare against sidecar."""
    wav = ROOT / preset / "adpcm" / f"{input_name}.wav"
    sha_file = ROOT / preset / "adpcm" / f"{input_name}.wav.sha256"
    digest = hashlib.sha256(wav.read_bytes()).hexdigest()
    sidecar_digest = sha_file.read_text().split()[0]
    assert digest == sidecar_digest, (
        f"{preset}/adpcm/{input_name}: wav={digest[:16]}... "
        f"sidecar={sidecar_digest[:16]}..."
    )


# ============================================================================
# Combined count gate
# ============================================================================

def test_expected_count():
    """Exactly 80 .wav + 80 .sha256 files — catches stray accumulations."""
    wavs = sorted(ROOT.glob("**/*.wav"))
    shas = sorted(ROOT.glob("**/*.wav.sha256"))
    assert len(wavs) == 80, f"expected 80 .wav, got {len(wavs)}"
    assert len(shas) == 80, f"expected 80 .sha256, got {len(shas)}"
