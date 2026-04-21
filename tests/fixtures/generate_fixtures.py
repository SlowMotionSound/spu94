#!/usr/bin/env python3
"""tests/fixtures/generate_fixtures.py — Phase 6 Plan 3 Task 1.

Generate a deterministic 1-second stereo 44.1 kHz 16-bit WAV file for the
CLI round-trip tests. "Deterministic" means byte-identical across hosts,
across Python versions, across numpy versions — this fixture is a contract
input, not a sampled signal. Uses only Python stdlib (wave + struct).

Invoked at configure time by tests/fixtures/CMakeLists.txt so the fixture
sits next to the built binary in build/tests/fixtures/sample_1s_stereo_44k.wav.
"""
import argparse
import struct
import wave
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    sr = 44100
    n_frames = sr  # exactly 1 second

    # Deterministic content: linear ramps on both channels, opposite phase,
    # spanning [-8000, +8000] — avoids int16 clipping and produces a
    # non-trivial input to the reverb (DC-free, time-varying).
    samples = bytearray()
    for i in range(n_frames):
        l = int(-8000 + (16000 * i) // n_frames)
        r = int( 8000 - (16000 * i) // n_frames)
        samples.extend(struct.pack("<h", l))
        samples.extend(struct.pack("<h", r))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(args.out), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(bytes(samples))
    print(f"Wrote {args.out} "
          f"({n_frames} frames x 2 channels x 2 bytes = {len(samples)} bytes)")


if __name__ == "__main__":
    main()
