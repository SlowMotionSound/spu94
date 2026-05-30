"""Shared pytest fixtures for Phase 6 Plan 3 CLI tests.

The spu94 binary + fixture WAV + JSON configs all live in the CMake build
tree. pytest runs under ctest (`ctest -L cli`) so the binary is guaranteed
to be fresh — build-time `add_dependencies` / DEPENDS wiring handles that.
"""
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="session")
def spu94_cli_path():
    """Absolute path to the built spu94 binary."""
    cli = REPO_ROOT / "build" / "src" / "cli" / "spu94"
    if not cli.exists():
        pytest.fail(
            f"spu94 binary not found: {cli} — "
            f"build via `cmake --build build --target spu94_cli`"
        )
    return str(cli)


@pytest.fixture(scope="session")
def sample_wav_file():
    """The 1-second stereo 44.1 kHz fixture generated at configure time."""
    wav = REPO_ROOT / "build" / "tests" / "fixtures" / "sample_1s_stereo_44k.wav"
    if not wav.exists():
        pytest.fail(
            f"Sample WAV fixture not found: {wav} — "
            f"build via `cmake --build build --target spu94_cli_fixtures`"
        )
    return str(wav)


@pytest.fixture(scope="session")
def sample_override_json():
    return str(REPO_ROOT / "build" / "tests" / "fixtures" / "sample_override_hall.json")


@pytest.fixture(scope="session")
def sample_flat_json():
    return str(REPO_ROOT / "build" / "tests" / "fixtures" / "sample_flat_registermap.json")


@pytest.fixture(scope="function")
def tmp_wav_out(tmp_path):
    """A tmp path for the CLI to write its output WAV to."""
    return str(tmp_path / "out.wav")
