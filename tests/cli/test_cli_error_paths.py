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


def test_flat_config_typo_reports_missing_register(tmp_path, spu94_cli_path,
                                                   sample_flat_json,
                                                   sample_wav_file, tmp_wav_out):
    """H-05: a flat config with 34 valid + 1 typo (count == 35) used to
    fire 'unknown register \\'TYPO\\'' — hiding the fact that one canonical
    register is also missing. The pre-pass now reports the missing real
    register first, which is more actionable."""
    full = json.loads(open(sample_flat_json).read())
    keys = list(full.keys())
    assert len(keys) == 35
    displaced = keys[7]  # arbitrary canonical register to remove
    bad = dict(full)
    del bad[displaced]
    bad["TYPO_KEY"] = 0  # restores count to 35
    bad_path = tmp_path / "flat_typo.json"
    bad_path.write_text(json.dumps(bad))
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad_path), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "missing register" in result.stderr
    assert displaced in result.stderr


def test_flat_config_duplicate_caught_via_pigeonhole(tmp_path, spu94_cli_path,
                                                    sample_flat_json,
                                                    sample_wav_file, tmp_wav_out):
    """H-05: a flat config with 34 distinct + 1 duplicate (count == 35) is
    caught as 'missing register X' — the duplicate displaces a canonical
    register, so the pre-pass's seen[] mask reports the displaced one."""
    full = json.loads(open(sample_flat_json).read())
    keys = list(full.keys())
    displaced = keys[12]
    survivor = keys[0]
    bad = dict(full)
    del bad[displaced]
    # Python dicts can't carry duplicate keys. Hand-build the JSON string
    # with two `survivor` entries so jsmn sees a duplicate at parse time.
    raw = "{\n"
    items = list(bad.items()) + [(survivor, bad[survivor])]
    for i, (k, v) in enumerate(items):
        comma = "," if i < len(items) - 1 else ""
        raw += f'  "{k}": {json.dumps(v)}{comma}\n'
    raw += "}\n"
    bad_path = tmp_path / "flat_dup.json"
    bad_path.write_text(raw)
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad_path), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "missing register" in result.stderr
    assert displaced in result.stderr


def _override_with_value(tmp_path, name, value):
    """Build a {"base": "hall", "overrides": {<name>: <value>}} config and
    return the path. <value> may be int or string (hex form)."""
    cfg = tmp_path / "ovr.json"
    cfg.write_text(json.dumps({"base": "hall", "overrides": {name: value}}))
    return str(cfg)


def test_parse_hex_rejects_bare_negative_prefix(tmp_path, spu94_cli_path,
                                                sample_wav_file, tmp_wav_out):
    """H-06: '-0x' (no digits) must be rejected — the s[2]=='\\0' guard."""
    cfg = _override_with_value(tmp_path, "vIIR", "-0x")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "invalid value" in result.stderr


def test_parse_hex_rejects_above_int_max(tmp_path, spu94_cli_path,
                                         sample_wav_file, tmp_wav_out):
    """H-06: '0x80000000' (2^31, just past INT_MAX) must be rejected
    by the v > INT_MAX check on 64-bit long systems."""
    cfg = _override_with_value(tmp_path, "vIIR", "0x80000000")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "invalid value" in result.stderr or "out of range" in result.stderr


def test_parse_hex_rejects_below_int_min(tmp_path, spu94_cli_path,
                                         sample_wav_file, tmp_wav_out):
    """H-06: '-0x80000001' (one past INT_MIN) must be rejected."""
    cfg = _override_with_value(tmp_path, "vIIR", "-0x80000001")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "invalid value" in result.stderr or "out of range" in result.stderr


def test_parse_hex_rejects_internal_whitespace(tmp_path, spu94_cli_path,
                                               sample_wav_file, tmp_wav_out):
    """H-06: '0x 10' (space between prefix and digits) — strtol stops at
    space; *endp != '\\0' rejects it."""
    cfg = _override_with_value(tmp_path, "vIIR", "0x 10")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "invalid value" in result.stderr


def test_parse_hex_accepts_plus_sign_prefix(tmp_path, spu94_cli_path,
                                            sample_wav_file, tmp_wav_out):
    """H-06: '+0x10' is a defensible accept case — leading '+' is consumed
    by line 78 and the rest parses normally to 16."""
    cfg = _override_with_value(tmp_path, "vIIR", "+0x10")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        f"expected success on +0x10, got rc={result.returncode}, "
        f"stderr={result.stderr!r}"
    )


def test_parse_hex_accepts_capital_x_prefix(tmp_path, spu94_cli_path,
                                            sample_wav_file, tmp_wav_out):
    """H-06: '0X10' (capital X) is a defensible accept case — line 79
    explicitly handles both 'x' and 'X'."""
    cfg = _override_with_value(tmp_path, "vIIR", "0X10")
    result = subprocess.run(
        [spu94_cli_path, "--config", cfg, sample_wav_file, tmp_wav_out],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, (
        f"expected success on 0X10, got rc={result.returncode}, "
        f"stderr={result.stderr!r}"
    )


def test_overlong_json_key_distinct_error(tmp_path, spu94_cli_path,
                                          sample_wav_file, tmp_wav_out):
    """H-04: a 70-char JSON key must produce a 'key too long' message,
    NOT a misleading 'unknown register' error keyed on the truncated
    first 63 chars of the gibberish key."""
    bad = tmp_path / "long_key.json"
    long_key = "x" * 70
    bad.write_text(json.dumps({"base": "hall", "overrides": {long_key: 0}}))
    result = subprocess.run(
        [spu94_cli_path, "--config", str(bad), sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "too long" in result.stderr or "chars long" in result.stderr
    assert "unknown register" not in result.stderr


def _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, tail):
    return subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--tail-seconds", tail,
         sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )


def test_tail_seconds_rejects_inf(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """C-02: the integer parser must reject 'inf' (no digits → !saw_digit)."""
    result = _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, "inf")
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr


def test_tail_seconds_rejects_nan(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """C-02: 'nan' is also non-digit; must be rejected."""
    result = _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, "nan")
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr


def test_tail_seconds_rejects_scientific_notation(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """C-02: '1e18' (the original review's attack vector) must be rejected
    by the integer parser — `e` is trailing garbage after the leading `1`."""
    result = _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, "1e18")
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr


def test_tail_seconds_rejects_above_cap(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """C-02: 601 seconds is just above the 600 s cap; must be rejected
    BEFORE allocation so no buffer overflow risk reaches malloc."""
    result = _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, "601")
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr


def test_tail_seconds_rejects_trailing_whitespace(spu94_cli_path, sample_wav_file, tmp_wav_out):
    """L-05 / C-02: '1.5 ' (trailing space) must be rejected — locks in the
    no-trailing-garbage contract that the integer parser enforces. Closes
    the L-05 finding about strtod accepting some whitespace inconsistently."""
    result = _run_with_tail(spu94_cli_path, sample_wav_file, tmp_wav_out, "1.5 ")
    assert result.returncode == 2
    assert _stderr_line_count(result.stderr) == 1
    assert "tail-seconds" in result.stderr


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
