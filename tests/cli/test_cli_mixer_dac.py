"""DAC-IO-01: CLI integration tests for mixer faders and DAC flags.

Tests the 10 new CLI flags added in Phase 8 Plan 01:
  - 6 fader flags (--input-gain, --dry, --patina, --dry-send, --patina-send, --reverb)
  - 3 DAC toggles (--dac, --no-dac-fir, --no-dac-noise)
  - Latency comp (--latency-comp, --no-latency-comp)
"""
import subprocess
from pathlib import Path


# ---------- DAC toggle tests ----------


def test_dac_flag_produces_output(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--dac enables DAC coloration and produces a valid output WAV."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--dac",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


def test_dac_no_fir_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--dac --no-dac-fir disables DAC FIR but keeps noise."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--dac", "--no-dac-fir",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


def test_dac_no_noise_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--dac --no-dac-noise disables DAC noise but keeps FIR."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--dac", "--no-dac-noise",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


def test_dac_flag_changes_output(spu94_cli_path, sample_wav_file, tmp_path):
    """Output with --dac differs from output without --dac."""
    out_no_dac = str(tmp_path / "no_dac.wav")
    out_dac = str(tmp_path / "dac.wav")
    subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall",
         sample_wav_file, out_no_dac],
        check=True, capture_output=True, text=True,
    )
    subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--dac",
         sample_wav_file, out_dac],
        check=True, capture_output=True, text=True,
    )
    assert Path(out_no_dac).read_bytes() != Path(out_dac).read_bytes()


# ---------- Fader tests ----------


def test_input_gain_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--input-gain 0.5 exits 0 and produces output."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--input-gain", "0.5",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


def test_dry_fader_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--dry 0.5 exits 0 and produces output."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--dry", "0.5",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


def test_reverb_fader_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--reverb 0.0 exits 0 (dry-only output)."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--reverb", "0.0",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


# ---------- Latency comp test ----------


def test_no_latency_comp_flag(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--adpcm --no-latency-comp exits 0."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--adpcm",
         "--no-latency-comp", sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()


# ---------- Error path tests ----------


def test_fader_out_of_range_rejected(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--input-gain 1.5 exits 2 with an error message."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--input-gain", "1.5",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert "invalid value" in result.stderr


def test_fader_invalid_string_rejected(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """--input-gain abc exits 2 with an error message."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall", "--input-gain", "abc",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert "invalid value" in result.stderr


# ---------- Help text test ----------


def test_help_shows_new_flags(spu94_cli_path):
    """--help output includes all new flag names."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--help"],
        capture_output=True, text=True,
    )
    assert result.returncode == 0
    for flag in ("--dac", "--input-gain", "--no-dac-fir", "--no-latency-comp"):
        assert flag in result.stdout, f"Missing {flag} in help text"


# ---------- Regression test ----------


def test_existing_presets_unchanged(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Baseline: --preset hall (no new flags) still works."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--preset", "hall",
         sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, f"stderr: {result.stderr}"
    assert Path(tmp_wav_out).exists()
    assert Path(tmp_wav_out).stat().st_size > 44  # more than just a WAV header
