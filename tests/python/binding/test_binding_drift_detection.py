"""PYBIND-05: import-time drift assertions fire with RuntimeError.

Phase 6's ctypes binding runs a handful of self-consistency checks at
import time (D-07). These tests force each failure path by swapping
``ctypes.CDLL`` with a shim that intercepts one specific symbol and
returns a drifted value; a fresh import must then raise
``RuntimeError`` with the expected diagnostic prefix.

Each test also keeps the shim self-contained so the import tests do not
pollute the ``sys.modules`` cache for the surface / register / preset
tests that run in the same pytest session (the shim is removed before
the test ends, and the next session-scoped import happens in a
different test file whose shim is already absent).
"""
import ctypes
import importlib
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[3]
_PYTHON_PKG = REPO_ROOT / "python"


def _reimport_spu94():
    """Pop all cached spu94 submodules and re-import ``spu94``.

    The conftest fixture uses ``monkeypatch_session.syspath_prepend`` to
    put python/ on the path before importing spu94. Once that has run,
    ``sys.path`` already contains our package root for the rest of the
    session; we only need to drop cached module state here.
    """
    # Ensure python/ is on sys.path even if no prior session import primed it.
    if str(_PYTHON_PKG) not in sys.path:
        sys.path.insert(0, str(_PYTHON_PKG))
    for mod in list(sys.modules):
        if mod == "spu94" or mod.startswith("spu94."):
            sys.modules.pop(mod, None)
    return importlib.import_module("spu94")


def _build_patched_cdll(real_cdll, overrides):
    """Return a CDLL-alike class that forwards to a real CDLL handle
    but replaces the named attributes with the override callables.

    Each override is a dict with ``fn`` (the new callable), ``restype``,
    and ``argtypes``. The shim object returned by ``__getattr__`` is
    callable AND exposes .restype/.argtypes just like a ctypes function
    pointer, so the binding's own argtype/restype assignments still work.
    """

    class _ShimFnPtr:
        def __init__(self, fn, restype, argtypes):
            self._fn = fn
            self.restype = restype
            self.argtypes = list(argtypes)

        def __call__(self, *args, **kwargs):
            return self._fn(*args, **kwargs)

    class PatchedCDLL:
        def __init__(self, path):
            # Delegate real symbols to an actual CDLL handle.
            self._inner = real_cdll(path)

        def __getattr__(self, name):
            if name in overrides:
                cfg = overrides[name]
                return _ShimFnPtr(cfg["fn"], cfg["restype"], cfg["argtypes"])
            return getattr(self._inner, name)

    return PatchedCDLL


def test_state_size_overflow_raises(spu94_lib_path, monkeypatch):
    """spu94_state_size() returning > SPU94_STATE_SIZE_MAX must raise."""
    monkeypatch.setenv("SPU94_LIB", spu94_lib_path)
    real_cdll = ctypes.CDLL
    overrides = {
        "spu94_state_size": {
            "fn": lambda: 16385,  # > SPU94_STATE_SIZE_MAX (16384)
            "restype": ctypes.c_size_t,
            "argtypes": [],
        },
    }
    monkeypatch.setattr(ctypes, "CDLL", _build_patched_cdll(real_cdll, overrides))
    with pytest.raises(RuntimeError, match="spu94 library mismatch:"):
        _reimport_spu94()


def test_register_count_shrank_raises(spu94_lib_path, monkeypatch):
    """spu94_reg_name returning NULL before i=35 means the library
    reports fewer than SPU94_REG__COUNT registers — must raise."""
    monkeypatch.setenv("SPU94_LIB", spu94_lib_path)
    real_cdll = ctypes.CDLL

    # We need to close over the real .so's spu94_reg_name to pass
    # through for indices < 30. Capture it from a throwaway real handle.
    _baseline = real_cdll(spu94_lib_path)
    _baseline.spu94_reg_name.restype = ctypes.c_char_p
    _baseline.spu94_reg_name.argtypes = [ctypes.c_int]
    real_reg_name = _baseline.spu94_reg_name

    def patched_reg_name(i):
        if i >= 30:
            return None
        return real_reg_name(i)

    overrides = {
        "spu94_reg_name": {
            "fn": patched_reg_name,
            "restype": ctypes.c_char_p,
            "argtypes": [ctypes.c_int],
        },
    }
    monkeypatch.setattr(ctypes, "CDLL", _build_patched_cdll(real_cdll, overrides))
    with pytest.raises(RuntimeError, match="reports fewer than"):
        _reimport_spu94()


def test_register_count_grew_raises(spu94_lib_path, monkeypatch):
    """spu94_reg_name(35) returning a non-NULL name means the library
    exposes MORE registers than SPU94_REG__COUNT — must raise."""
    monkeypatch.setenv("SPU94_LIB", spu94_lib_path)
    real_cdll = ctypes.CDLL

    _baseline = real_cdll(spu94_lib_path)
    _baseline.spu94_reg_name.restype = ctypes.c_char_p
    _baseline.spu94_reg_name.argtypes = [ctypes.c_int]
    real_reg_name = _baseline.spu94_reg_name

    def patched_reg_name(i):
        if i == 35:
            return b"EXTRA_REG"
        return real_reg_name(i)

    overrides = {
        "spu94_reg_name": {
            "fn": patched_reg_name,
            "restype": ctypes.c_char_p,
            "argtypes": [ctypes.c_int],
        },
    }
    monkeypatch.setattr(ctypes, "CDLL", _build_patched_cdll(real_cdll, overrides))
    with pytest.raises(RuntimeError, match="MORE than"):
        _reimport_spu94()


def test_positive_case_imports_cleanly(spu94_lib_path, monkeypatch):
    """Without any patches, a fresh import must succeed and produce a
    35-member Register IntEnum."""
    monkeypatch.setenv("SPU94_LIB", spu94_lib_path)
    spu94 = _reimport_spu94()
    assert len(spu94.Register) == 35
    assert spu94.Register.vIIR.value == 5
