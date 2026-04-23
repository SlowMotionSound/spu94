"""tests/conformance/test_goldens_present.py — Phase 7 Plan 02 Task 2.

Structural conformance: the 50-golden corpus exists and sidecars are
well-formed. This is the cheap always-runnable counterpart to the
Dockerfile-repro + --check reproducibility gate:

  - 50 .wav files exist under tests/golden/<preset>/<input>.wav
  - 50 .wav.sha256 sidecars exist alongside
  - every sidecar is formatted "<64-hex>  <filename>\n" (sha256sum-compatible)
  - spot-check three sidecars match the SHA-256 of their .wav

Registered with ctest via tests/conformance/CMakeLists.txt (test name:
`goldens_present`). Parametrized across all 10 presets x 5 inputs —
~100 test cases pass per run + 3 spot-checks.
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

# Resolved from this test file: tests/conformance/ -> ../.. = repo root
REPO_ROOT = Path(__file__).resolve().parents[2]
ROOT = REPO_ROOT / "tests" / "golden"


@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", INPUTS)
def test_wav_exists(preset: str, input_name: str):
    """Each of the 50 .wav files is present as a regular file."""
    p = ROOT / preset / f"{input_name}.wav"
    assert p.is_file(), f"missing golden WAV: {p}"


@pytest.mark.parametrize("preset", PRESETS)
@pytest.mark.parametrize("input_name", INPUTS)
def test_sidecar_exists_and_format(preset: str, input_name: str):
    """Each sidecar exists and matches sha256sum's output format."""
    p = ROOT / preset / f"{input_name}.wav.sha256"
    assert p.is_file(), f"missing sidecar: {p}"
    text = p.read_text()
    # sha256sum format: "<64-hex>  <basename>\n"
    # (two spaces between hex and basename when the hash is of binary content)
    m = re.match(r"^[0-9a-f]{64}\s+\S+\s*$", text)
    assert m is not None, f"sidecar {p} malformed: {text!r}"


@pytest.mark.parametrize(
    "preset,input_name",
    [("hall", "impulse"), ("room", "sine_1khz"), ("studio_a", "sweep")],
)
def test_sidecar_matches_wav(preset: str, input_name: str):
    """Spot-check: re-hash the .wav, compare against sidecar's first field."""
    wav = ROOT / preset / f"{input_name}.wav"
    sha_file = ROOT / preset / f"{input_name}.wav.sha256"
    digest = hashlib.sha256(wav.read_bytes()).hexdigest()
    sidecar_digest = sha_file.read_text().split()[0]
    assert digest == sidecar_digest, (
        f"{preset}/{input_name}: wav={digest[:16]}... "
        f"sidecar={sidecar_digest[:16]}..."
    )


def test_expected_count():
    """Exactly 50 .wav + 50 .sha256 files — catches stray accumulations."""
    wavs = sorted(ROOT.glob("*/*.wav"))
    shas = sorted(ROOT.glob("*/*.wav.sha256"))
    assert len(wavs) == 50, f"expected 50 .wav, got {len(wavs)}"
    assert len(shas) == 50, f"expected 50 .sha256, got {len(shas)}"
