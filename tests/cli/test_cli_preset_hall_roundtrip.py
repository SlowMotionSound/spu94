"""CLI-01: spu94 --preset <name> produces a valid output WAV.

These tests run the `spu94` binary as a subprocess against the generated
1-second stereo fixture, then validate the output WAV's headers. Bit-exact
output comparison is Phase 7's job (golden files); CLI-01 only asserts the
command runs to completion with a correctly-shaped output.
"""
import subprocess
import wave
from pathlib import Path

import pytest


def test_preset_hall_produces_valid_wav(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Smoke: hall preset over 1 second of audio produces a readable WAV."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"CLI failed: stderr={result.stderr!r}"
    assert Path(tmp_wav_out).exists()
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getsampwidth() == 2
        assert w.getframerate() == 44100


def test_preset_hall_frame_count_matches_input(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Without --tail-seconds the output must be exactly input_frames long."""
    with wave.open(sample_wav_file, "rb") as w:
        in_frames = w.getnframes()
    subprocess.run(
        [spu94_cli_path, "--preset", "hall", sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
        check=True,
    )
    with wave.open(tmp_wav_out, "rb") as w:
        out_frames = w.getnframes()
    assert out_frames == in_frames


def test_preset_hall_with_tail_seconds(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--tail-seconds N appends exactly N * 44100 frames of flush output."""
    with wave.open(sample_wav_file, "rb") as w:
        in_frames = w.getnframes()
    subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--tail-seconds", "1.0",
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
        check=True,
    )
    with wave.open(tmp_wav_out, "rb") as w:
        out_frames = w.getnframes()
    assert out_frames == in_frames + 44100


def test_preset_off_succeeds(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """The Off preset is silent (all gains zero) — should still round-trip."""
    subprocess.run(
        [spu94_cli_path, "--preset", "off", sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
        check=True,
    )
    assert Path(tmp_wav_out).exists()


@pytest.mark.parametrize("preset_name", [
    "off", "room", "studio_a", "studio_b", "studio_c",
    "hall", "half_echo", "space_echo", "echo", "delay",
])
def test_every_preset_roundtrips(spu94_cli_path, sample_wav_file, tmp_wav_out, preset_name):
    """All 10 factory presets should produce valid output WAVs."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", preset_name, sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"preset '{preset_name}' failed: {result.stderr!r}"
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getframerate() == 44100


def test_preset_case_insensitive(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Uppercase and mixed-case preset names should resolve identically."""
    for variant in ("HALL", "Hall", "hAlL"):
        result = subprocess.run(
            [spu94_cli_path, "--preset", variant, sample_wav_file, tmp_wav_out],
            capture_output=True,
            text=True,
        )
        assert result.returncode == 0, f"variant {variant!r} failed: {result.stderr!r}"


def test_preset_spaces_equivalent_to_underscores(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """'Studio A' (as the C-side display name does) must resolve like 'studio_a'."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", "Studio A", sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"Studio A failed: {result.stderr!r}"
