"""Shared fixtures for Phase 6 Plan 1 binding tests.

The binding lives at python/spu94/; the dev loop imports it via sys.path
injection (wheel install is Plan 4). libspu94.so is located via the
SPU94_LIB env var that ctest sets per-test via
``$<TARGET_FILE:spu94_shared>`` — same Pitfall-7 mitigation the fuzz
harnesses have used since Phase 2 Plan 05.
"""

import os
import sys
from pathlib import Path

import pytest


# tests/python/binding/conftest.py → repo root is 4 levels up
# (binding → python → tests → repo).
REPO_ROOT = Path(__file__).resolve().parents[3]


@pytest.fixture(scope="session")
def monkeypatch_session():
    """Session-scoped monkeypatch (pytest's built-in is function-scoped)."""
    from _pytest.monkeypatch import MonkeyPatch

    mp = MonkeyPatch()
    yield mp
    mp.undo()


@pytest.fixture(scope="session")
def spu94_lib_path():
    """Absolute path to libspu94.so.

    Fails fast if ``SPU94_LIB`` is unset or the file does not exist — the
    ctest wiring for this directory always sets
    ``SPU94_LIB=$<TARGET_FILE:spu94_shared>``, so any miss here means the
    test is being run outside of ctest without a path override.
    """
    lib = os.environ.get("SPU94_LIB")
    if not lib:
        pytest.fail(
            "SPU94_LIB env var not set — run these tests via "
            "`ctest --test-dir build -L binding`, or set SPU94_LIB to an "
            "absolute path to libspu94.so."
        )
    if not Path(lib).exists():
        pytest.fail(f"SPU94_LIB points to a missing file: {lib}")
    return lib


@pytest.fixture(scope="session")
def spu94_module(spu94_lib_path, monkeypatch_session):
    """Import the spu94 package with SPU94_LIB pointing at the dev library.

    scope=session keeps the import-time drift assertions a one-shot per
    test session; individual tests that need a fresh import (e.g. the
    drift-detection suite) perform their own ``importlib.reload`` inside
    a function-scoped monkeypatch context.
    """
    monkeypatch_session.setenv("SPU94_LIB", spu94_lib_path)

    # python/spu94/ lives in the repo root's python/ dir — prepend to sys.path
    # so `import spu94` resolves to the working-tree sources.
    py_pkg_root = REPO_ROOT / "python"
    monkeypatch_session.syspath_prepend(str(py_pkg_root))

    import spu94  # noqa: E402 — intentional late import after env setup.
    return spu94
