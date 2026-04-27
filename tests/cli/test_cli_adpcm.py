"""Phase 3 Plan 01: CLI integration tests for ADPCM subcommands.

Tests:
  1. Legacy reverb backward compatibility (no subcommand)
  2. Reverb subcommand
  3. adpcm-encode + adpcm-decode roundtrip
  4. adpcm-roundtrip subcommand
  5. --adpcm flag for reverb mode
  6. Global help shows all subcommands
  7. Unknown subcommand returns exit 2
"""
import subprocess
import wave
from pathlib import Path

import pytest


def test_legacy_reverb_backward_compat(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """No subcommand: `spu94 --preset hall in.wav out.wav` still works."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"Legacy mode failed: {result.stderr!r}"
    assert Path(tmp_wav_out).exists()
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getsampwidth() == 2
        assert w.getframerate() == 44100


def test_reverb_subcommand(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Explicit subcommand: `spu94 reverb --preset hall in.wav out.wav`."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"reverb subcommand failed: {result.stderr!r}"
    assert Path(tmp_wav_out).exists()


def test_adpcm_encode_decode_roundtrip(spu94_cli_path, sample_wav_file, tmp_path):
    """Encode WAV to VAG, then decode VAG back to WAV."""
    vag_path = str(tmp_path / "test.vag")
    decoded_path = str(tmp_path / "decoded.wav")

    # Encode
    result = subprocess.run(
        [spu94_cli_path, "adpcm-encode", sample_wav_file, vag_path],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"adpcm-encode failed: {result.stderr!r}"
    assert Path(vag_path).exists()

    # Check VAG magic
    with open(vag_path, "rb") as f:
        magic = f.read(4)
    assert magic == b"VAGp", f"Expected VAGp magic, got {magic!r}"

    # Decode
    result = subprocess.run(
        [spu94_cli_path, "adpcm-decode", vag_path, decoded_path],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"adpcm-decode failed: {result.stderr!r}"
    assert Path(decoded_path).exists()

    # Verify decoded WAV has same sample count as input
    with wave.open(sample_wav_file, "rb") as w:
        in_frames = w.getnframes()
    with wave.open(decoded_path, "rb") as w:
        out_frames = w.getnframes()

    # ADPCM encodes in 28-sample blocks, so output may be rounded up
    # to the next block boundary. Allow up to 27 extra samples.
    assert out_frames >= in_frames, (
        f"Decoded has fewer frames ({out_frames}) than input ({in_frames})"
    )
    assert out_frames - in_frames < 28, (
        f"Decoded has too many extra frames ({out_frames - in_frames})"
    )


def test_adpcm_roundtrip_subcommand(spu94_cli_path, sample_wav_file, tmp_path):
    """In-memory roundtrip: `spu94 adpcm-roundtrip in.wav out.wav`."""
    rt_path = str(tmp_path / "rt.wav")

    result = subprocess.run(
        [spu94_cli_path, "adpcm-roundtrip", sample_wav_file, rt_path],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"adpcm-roundtrip failed: {result.stderr!r}"
    assert Path(rt_path).exists()

    with wave.open(sample_wav_file, "rb") as w:
        in_frames = w.getnframes()
    with wave.open(rt_path, "rb") as w:
        out_frames = w.getnframes()
    assert out_frames == in_frames


def test_reverb_adpcm_flag(spu94_cli_path, sample_wav_file, tmp_path):
    """--adpcm flag changes the reverb output (ADPCM introduces quantization)."""
    out_normal = str(tmp_path / "normal.wav")
    out_adpcm = str(tmp_path / "adpcm.wav")

    # Without --adpcm
    subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", sample_wav_file, out_normal],
        capture_output=True, text=True, check=True,
    )
    # With --adpcm
    subprocess.run(
        [spu94_cli_path, "reverb", "--adpcm", "--preset", "hall",
         sample_wav_file, out_adpcm],
        capture_output=True, text=True, check=True,
    )

    # Read raw bytes and compare -- they should differ
    with wave.open(out_normal, "rb") as w:
        normal_data = w.readframes(w.getnframes())
    with wave.open(out_adpcm, "rb") as w:
        adpcm_data = w.readframes(w.getnframes())

    assert normal_data != adpcm_data, (
        "ADPCM flag should produce different output than normal mode"
    )


def test_global_help(spu94_cli_path):
    """Global --help shows all subcommands (D-03)."""
    result = subprocess.run(
        [spu94_cli_path, "--help"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    assert "adpcm-encode" in result.stdout
    assert "adpcm-decode" in result.stdout
    assert "adpcm-roundtrip" in result.stdout


def test_unknown_subcommand(spu94_cli_path):
    """Unknown subcommand returns exit 2."""
    result = subprocess.run(
        [spu94_cli_path, "bogus"],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
