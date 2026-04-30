"""tests/conformance/test_goldens_present.py — Phase 7 Plan 02 Task 2,
extended Phase 4 Plan 02 for ADPCM golden corpus, extended Phase 9 Plan 01
for DAC golden corpus.

Structural conformance: the 135-golden corpus (50 reverb + 30 ADPCM + 55 DAC)
exists and sidecars are well-formed. This is the cheap always-runnable
counterpart to the Dockerfile-repro + --check reproducibility gate:

  - 50 reverb .wav files under tests/golden/<preset>/<input>.wav
  - 30 ADPCM .wav files under tests/golden/<preset>/adpcm/<input>.wav
  - 50 DAC full-pipeline .wav files under tests/golden/<preset>/dac/<input>.wav
  - 5 DAC isolated .wav files under tests/golden/dac_isolated/<input>.wav
  - 135 .wav.sha256 sidecars alongside each .wav
  - every sidecar is formatted "<64-hex>  <filename>\\n" (sha256sum-compatible)
  - spot-check reverb + ADPCM + DAC sidecars match the SHA-256 of their .wav

Registered with ctest via tests/conformance/CMakeLists.txt (test name:
`goldens_present`). Parametrized across presets x inputs — ~270 test
cases pass per run + spot-checks.
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

# Phase 9 D-02: DAC inputs — same 5 as the reverb corpus.
DAC_INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]

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
# DAC full-pipeline golden tests (Phase 9 Plan 01 — 50 files)
# ============================================================================

@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", DAC_INPUTS)
def test_dac_wav_exists(preset: str, input_name: str):
    """Each of the 50 DAC full-pipeline .wav files is present as a regular file."""
    p = ROOT / preset / "dac" / f"{input_name}.wav"
    assert p.is_file(), f"missing DAC golden WAV: {p}"


@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", DAC_INPUTS)
def test_dac_sidecar_exists_and_format(preset: str, input_name: str):
    """Each DAC sidecar exists and matches sha256sum's output format."""
    p = ROOT / preset / "dac" / f"{input_name}.wav.sha256"
    assert p.is_file(), f"missing DAC sidecar: {p}"
    text = p.read_text()
    m = re.match(r"^[0-9a-f]{64}\s+\S+\s*$", text)
    assert m is not None, f"DAC sidecar {p} malformed: {text!r}"


@pytest.mark.parametrize(
    "preset,input_name",
    [("hall", "impulse"), ("room", "sine_1khz"), ("studio_a", "sweep")],
)
def test_dac_sidecar_matches_wav(preset: str, input_name: str):
    """Spot-check: re-hash the DAC .wav, compare against sidecar."""
    wav = ROOT / preset / "dac" / f"{input_name}.wav"
    sha_file = ROOT / preset / "dac" / f"{input_name}.wav.sha256"
    digest = hashlib.sha256(wav.read_bytes()).hexdigest()
    sidecar_digest = sha_file.read_text().split()[0]
    assert digest == sidecar_digest, (
        f"{preset}/dac/{input_name}: wav={digest[:16]}... "
        f"sidecar={sidecar_digest[:16]}..."
    )


# ============================================================================
# DAC isolated golden tests (Phase 9 Plan 01 — 5 files)
# ============================================================================

@pytest.mark.parametrize("input_name", DAC_INPUTS)
def test_dac_isolated_wav_exists(input_name: str):
    """Each of the 5 DAC isolated .wav files is present as a regular file."""
    p = ROOT / "dac_isolated" / f"{input_name}.wav"
    assert p.is_file(), f"missing DAC isolated golden WAV: {p}"


@pytest.mark.parametrize("input_name", DAC_INPUTS)
def test_dac_isolated_sidecar_exists_and_format(input_name: str):
    """Each DAC isolated sidecar exists and matches sha256sum's output format."""
    p = ROOT / "dac_isolated" / f"{input_name}.wav.sha256"
    assert p.is_file(), f"missing DAC isolated sidecar: {p}"
    text = p.read_text()
    m = re.match(r"^[0-9a-f]{64}\s+\S+\s*$", text)
    assert m is not None, f"DAC isolated sidecar {p} malformed: {text!r}"


@pytest.mark.parametrize(
    "input_name",
    ["impulse", "sweep"],
)
def test_dac_isolated_sidecar_matches_wav(input_name: str):
    """Spot-check: re-hash the DAC isolated .wav, compare against sidecar."""
    wav = ROOT / "dac_isolated" / f"{input_name}.wav"
    sha_file = ROOT / "dac_isolated" / f"{input_name}.wav.sha256"
    digest = hashlib.sha256(wav.read_bytes()).hexdigest()
    sidecar_digest = sha_file.read_text().split()[0]
    assert digest == sidecar_digest, (
        f"dac_isolated/{input_name}: wav={digest[:16]}... "
        f"sidecar={sidecar_digest[:16]}..."
    )


# ============================================================================
# Combined count gate
# ============================================================================

def test_expected_count():
    """Exactly 135 .wav + 135 .sha256 files — catches stray accumulations."""
    wavs = sorted(ROOT.glob("**/*.wav"))
    shas = sorted(ROOT.glob("**/*.wav.sha256"))
    assert len(wavs) == 135, f"expected 135 .wav, got {len(wavs)}"
    assert len(shas) == 135, f"expected 135 .sha256, got {len(shas)}"
