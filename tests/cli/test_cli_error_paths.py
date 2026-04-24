"""CLI-04: every error path exits non-zero with exactly ONE line of stderr.

The D-05 contract says:
    - exit code is non-zero (typically 2 for user error)
    - stderr has exactly one line, starting with `spu94: error:`
    - no tracebacks, no multi-line error blocks

Additionally, the unknown-preset path has an EXACT text contract that
downstream README docs can quote verbatim.
"""
import json
import subprocess
import wave


def _stderr_line_count(stderr: str) -> int:
    """Count non-empty stderr lines."""
    return sum(1 for line in stderr.splitlines() if line.strip())


def test_no_args_fails(spu94_cli_path):
    result = subprocess.run([spu94_cli_path], capture_output=True, text=True)
    assert result.returncode != 0
    assert _stderr_line_count(result.stderr) == 1
    assert result.stderr.startswith("spu94: error:")


def test_unknown_preset_exact_shape(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Exact D-05 message for unknown preset — README quotes this verbatim."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hll", sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    expected = ("spu94: error: unknown preset 'hll' — valid: "
                "off, room, studio_a, studio_b, studio_c, hall, "
                "half_echo, space_echo, echo, delay")
    assert result.stderr.strip() == expected, (
        f"exact message mismatch:\n  expected: {expected!r}\n  got:      {result.stderr.strip()!r}"
    )


def test_missing_input_file(spu94_cli_path, tmp_wav_out):
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall",
         "/tmp/__nonexistent_spu94_test.wav", tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "__nonexistent_spu94_test.wav" in result.stderr
    assert "not found" in result.stderr


def test_missing_config_file(spu94_cli_path, sample_wav_file, tmp_wav_out):
    result = subprocess.run(
        [spu94_cli_path, "--config", "/tmp/__nonexistent_spu94_test.json",
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "__nonexistent_spu94_test.json" in result.stderr
    assert "not found" in result.stderr


def test_malformed_json(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    bad = tmp_path / "bad.json"
    bad.write_text("{ not valid json }")
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "invalid JSON" in result.stderr or "parse" in result.stderr.lower()


def test_unknown_register_flat_config(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    bad = tmp_path / "bad.json"
    bad.write_text(json.dumps({"vNONESENSE": 0}))
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    # Could trigger either "must specify all 35 registers" OR "unknown register"
    # depending on which check fires first.
    assert ("vNONESENSE" in result.stderr) or ("35 registers" in result.stderr)


def test_unknown_register_override(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    bad = tmp_path / "bad.json"
    bad.write_text(json.dumps({"base": "hall", "overrides": {"vNONESENSE": 0}}))
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "unknown register" in result.stderr
    assert "vNONESENSE" in result.stderr


def test_out_of_range_value(tmp_path, spu94_cli_path, sample_wav_file, tmp_wav_out):
    bad = tmp_path / "bad.json"
    bad.write_text(json.dumps({"base": "hall", "overrides": {"vIIR": 999999}}))
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "vIIR" in result.stderr
    assert "range" in result.stderr.lower()


def test_mutually_exclusive(spu94_cli_path, sample_wav_file, tmp_wav_out):
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--config", "x.json",
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "mutually exclusive" in result.stderr


def test_wrong_positional_count(spu94_cli_path, sample_wav_file):
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", sample_wav_file],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "INPUT.wav" in result.stderr or "positional" in result.stderr.lower()


def test_invalid_tail_seconds(spu94_cli_path, sample_wav_file, tmp_wav_out):
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--tail-seconds", "abc",
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr or "abc" in result.stderr


def test_negative_tail_seconds_rejected(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """Negative durations make no sense and should be rejected up front."""
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--tail-seconds", "-1",
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1


def _write_wav_fixture(path, sampwidth_bytes, num_frames=441):
    """Write a minimal stereo 44.1 kHz WAV with the given sample width.
    sampwidth_bytes: 1 (8-bit), 2 (16-bit), or 3 (24-bit). Frame data is zeros."""
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(sampwidth_bytes)
        w.setframerate(44100)
        w.writeframes(b"\x00" * (num_frames * 2 * sampwidth_bytes))


def test_24bit_wav_rejected(tmp_path, spu94_cli_path, tmp_wav_out):
    """C-01: 24-bit PCM is the most likely shape Anthony will drop on the
    CLI; drwav silently down-converts. Gate must fire with a clear message
    pointing at ffmpeg as the conversion path."""
    bad = tmp_path / "input_24bit.wav"
    _write_wav_fixture(bad, sampwidth_bytes=3)
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", str(bad), tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert result.stderr.startswith("spu94: error:")
    assert "24-bit" in result.stderr
    assert "16-bit PCM required" in result.stderr
    assert "ffmpeg" in result.stderr


def test_8bit_wav_rejected(tmp_path, spu94_cli_path, tmp_wav_out):
    """C-01 cross-check: 8-bit PCM also fails the bit-depth gate. Proves
    the check is `!= 16` rather than a happenstance match against 24."""
    bad = tmp_path / "input_8bit.wav"
    _write_wav_fixture(bad, sampwidth_bytes=1)
    result = subprocess.run(
        [spu94_cli_path, "--preset", "hall", str(bad), tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert result.stderr.startswith("spu94: error:")
    assert "8-bit" in result.stderr
    assert "16-bit PCM required" in result.stderr
