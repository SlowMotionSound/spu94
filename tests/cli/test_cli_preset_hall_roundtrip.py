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
    """All 10 factory presets should produce valid output WAVs.

    HI-02 audibility smoke: beyond shape checks, this also inspects the
    output WAV's sample content. For non-Off presets we require at least
    one non-zero sample (the CLI sets vLOUT/vROUT=0x7FFF for non-Off
    presets per ADR-Phase-6-G, so real audio should land in the output).
    For Off, every sample must be zero (all gains zero, no audio path).
    Closes the test-vacuity gap that let the reverb-not-wired bug ship.
    """
    result = subprocess.run(
        [spu94_cli_path, "--preset", preset_name, sample_wav_file, tmp_wav_out],
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, f"preset '{preset_name}' failed: {result.stderr!r}"
    with wave.open(tmp_wav_out, "rb") as w:
        assert w.getnchannels() == 2
        assert w.getframerate() == 44100
        frames = w.readframes(w.getnframes())

    # int16 little-endian interleaved LR samples. Scan for any non-zero
    # byte pair to detect audible output without a numpy dependency.
    any_nonzero = any(frames[i] != 0 or frames[i + 1] != 0
                      for i in range(0, len(frames), 2))
    if preset_name == "off":
        assert not any_nonzero, (
            f"Off preset must produce silence; found non-zero sample"
        )
    else:
        assert any_nonzero, (
            f"preset '{preset_name}' produced all-zero output — "
            f"reverb likely not in audio path (HI-02 regression)"
        )


def test_silent_input_through_hall_is_silent(spu94_cli_path, tmp_path):
    """Silent input through any preset must produce silent output.

    Regression for the CLI's uninitialized work_buf bug: `spu94_init`
    intentionally does NOT zero the caller-owned work buffer (per its
    documented contract — only `spu94_reset` does). The CLI was calling
    `spu94_init` without a follow-up `spu94_reset`, so the reverb's
    delay-line memory started with `malloc` heap garbage. Today's wiring
    fix (ADR-Phase-6-G) surfaced that garbage as an audible "noise blast"
    at the start of every rendered file.

    This test feeds 2 seconds of pure silence through Hall. If the CLI
    properly zeroes its work_buf, output is silent end-to-end. If heap
    residue is bleeding through the delay lines, output will be non-zero
    at the start as the garbage decays through the reverb's feedback loop.
    """
    n_frames = 88200  # 2 seconds
    silence = bytearray(b"\x00" * n_frames * 4)  # stereo int16 zeros
    input_wav = tmp_path / "silent_in.wav"
    with wave.open(str(input_wav), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(44100)
        w.writeframes(bytes(silence))

    output_wav = tmp_path / "silent_out.wav"
    subprocess.run(
        [spu94_cli_path, "--preset", "hall", "--tail-seconds", "0",
         str(input_wav), str(output_wav)],
        capture_output=True, text=True, check=True,
    )

    with wave.open(str(output_wav), "rb") as w:
        raw = w.readframes(w.getnframes())

    # Every int16 sample must be exactly zero.
    peak = 0
    for i in range(0, len(raw), 2):
        v = int.from_bytes(raw[i:i + 2], "little", signed=True)
        if abs(v) > peak:
            peak = abs(v)
    assert peak == 0, (
        f"silent input produced non-silent output (peak={peak}) — "
        f"work_buf heap residue is leaking through the reverb delay "
        f"lines. The CLI must call spu94_reset after spu94_init."
    )


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
