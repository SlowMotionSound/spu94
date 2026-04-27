"""CLI-02: --config (flat + override shapes), --list-presets, --help.

The config shapes are:
    - Override: {"base": "<preset>", "overrides": {"<reg>": <value>, ...}}
    - Flat:     {"<reg>": <value>, ...}  (all 35 registers required)

Values may be JSON integers (decimal) or hex strings ("0x1000", "-0x40").
"""
import subprocess
from pathlib import Path
import wave


def test_config_override_shape(spu94_cli_path, sample_wav_file, sample_override_json, tmp_wav_out):
    """{base: hall, overrides: {vIIR: -8000, mLCOMB1: '0x1000'}} succeeds."""
    result = subprocess.run(
        [spu94_cli_path, "--config", sample_override_json, sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"override config: {result.stderr!r}"
    assert Path(tmp_wav_out).exists()
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getframerate() == 44100


def test_config_flat_registermap(spu94_cli_path, sample_wav_file, sample_flat_json, tmp_wav_out):
    """All 35 registers specified via the flat register-map shape."""
    result = subprocess.run(
        [spu94_cli_path, "--config", sample_flat_json, sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"flat config: {result.stderr!r}"
    assert Path(tmp_wav_out).exists()


def test_list_presets_10_lines(spu94_cli_path):
    """--list-presets prints exactly the 10 canonical names in enum order."""
    result = subprocess.run(
        [spu94_cli_path, "--list-presets"],
        capture_output=True,
        text=True,
        check=True,
    )
    names = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    assert names == [
        "off", "room", "studio_a", "studio_b", "studio_c",
        "hall", "half_echo", "space_echo", "echo", "delay",
    ], f"got: {names}"


def test_help_exits_0(spu94_cli_path):
    """--help prints the global subcommand list (D-03) and exits 0."""
    result = subprocess.run(
        [spu94_cli_path, "--help"],
        capture_output=True,
        text=True,
        check=True,
    )
    for expected in ("reverb", "adpcm-encode", "adpcm-decode", "adpcm-roundtrip"):
        assert expected in result.stdout, f"missing from global --help: {expected!r}"


def test_reverb_help_shows_options(spu94_cli_path):
    """spu94 reverb --help shows reverb-specific flags."""
    result = subprocess.run(
        [spu94_cli_path, "reverb", "--help"],
        capture_output=True,
        text=True,
        check=True,
    )
    for expected in ("Render a WAV", "--preset", "--config", "--tail-seconds",
                     "--list-presets", "--adpcm"):
        assert expected in result.stdout, f"missing from reverb --help: {expected!r}"


def test_short_h_flag(spu94_cli_path):
    """-h is an alias for --help (shows global help)."""
    result = subprocess.run(
        [spu94_cli_path, "-h"],
        capture_output=True,
        text=True,
        check=True,
    )
    assert "reverb" in result.stdout
    assert "adpcm-encode" in result.stdout


def test_override_with_hex_string(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Hex-string override values are parsed as integers in the right base."""
    cfg = tmp_path / "hex.json"
    cfg.write_text('{"base": "hall", "overrides": {"mLCOMB1": "0x2000"}}')
    result = subprocess.run(
        [spu94_cli_path, "--config", str(cfg), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"hex override: {result.stderr!r}"


def test_override_with_negative_integer(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Negative integer overrides for i16 registers succeed."""
    cfg = tmp_path / "neg.json"
    cfg.write_text('{"base": "hall", "overrides": {"vIIR": -16000}}')
    result = subprocess.run(
        [spu94_cli_path, "--config", str(cfg), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"negative override: {result.stderr!r}"
