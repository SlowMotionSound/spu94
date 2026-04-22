"""python/spu94/_binding.py

Raw ctypes CDLL loader and prototype declarations for the full
libspu94.so public surface: ``include/spu94/spu94.h`` and
``include/spu94/spu94_registers.h``.

Import this module to obtain the configured CDLL handle as ``_lib``.
Every public C symbol has argtypes + restype set — downstream modules
(``__init__.py``, ``presets.py``, and Plan 2's ``api.py`` / ``reverb.py``,
plus Plan 5's migrated fuzz scripts) rely on those attributes being in
place before they call anything on ``_lib``.

This file is intentionally minimal: it declares prototypes and does
nothing else at import time. The runtime-reflection IntEnum build and
the import-time drift assertions live in ``__init__.py`` — they run
after this module has populated ``_lib``.

Plan 2 note: ``_lib.spu94_process.argtypes`` and
``_lib.spu94_flush.argtypes`` are declared here with raw
``ctypes.POINTER(ctypes.c_int16)``. Plan 2 overrides those four
argtype entries with ``numpy.ctypeslib.ndpointer`` once the numpy
dependency is admitted. Plan 1 must not import numpy — keeping the
package dependency-free for consumers who only need reflection /
presets / register IO.

Threat mitigation (T-06-01, T-06-02):
  * Library is resolved by ABSOLUTE path — never a bare filename —
    so the linker's search path cannot silently substitute a stale
    ``/usr/lib/libspu94.so``.
  * The SPU94_STATE_SIZE_MAX / SPU94_REG__COUNT / SPU94_PRESET__COUNT
    constants mirrored below are validated against the live library
    inside ``__init__.py``'s drift assertions; they are not trusted
    blindly.
"""
import ctypes
import os
import sys
from pathlib import Path

import numpy as np
from numpy.ctypeslib import ndpointer

# ----------------------------------------------------------------------
# Library path resolution (T-06-01 mitigation: absolute path only)
# ----------------------------------------------------------------------
_HERE = Path(__file__).resolve().parent


def _resolve_lib_path() -> str:
    """Return an absolute path to ``libspu94.so``.

    Precedence:
      1. ``SPU94_LIB`` env var — dev convenience; matches the Phases 2–5
         fuzz scripts (Phase 2 Plan 05 ENVIRONMENT generator-expression
         pattern, Pitfall 7 mitigation).
      2. ``Path(__file__).parent / 'libspu94.so'`` — installed wheel
         layout (Plan 4's CMake install rule drops ``libspu94.so`` next
         to ``__init__.py``). This is also where a regular ``pip install
         dist/spu94-*.whl`` wheel puts the library.
      3. scikit-build-core editable-install layout (``pip install -e .``):
         the source ``python/spu94/__init__.py`` gets put on sys.path
         via a .pth file, but the CMake install rules drop the compiled
         artifacts into ``site-packages/spu94/`` instead. Walk
         ``sys.path`` looking for ``spu94/libspu94.so``.

    Never returns a bare filename — doing so would trigger the linker's
    search path and allow a silent load of a stale
    ``/usr/lib/libspu94.so`` (Pitfall 4).

    Raises ``OSError`` with an actionable message if no candidate path
    exists. The message lists every location we checked so the user can
    diagnose whether it's a missing build / broken editable install /
    stale SPU94_LIB env.
    """
    # (1) Explicit env var — dev / fuzz-harness convention.
    env = os.environ.get("SPU94_LIB")
    if env:
        return env

    # (2) Wheel-install layout: next to this __init__.py. Also the layout
    # scikit-build-core uses for non-editable wheel installs and the
    # layout the Plan 4 CMake install rules produce.
    candidates = [_HERE / "libspu94.so"]

    # (3) scikit-build-core editable-install layout: search every
    # site-packages-like entry on sys.path for spu94/libspu94.so. This
    # picks up the library that scikit-build-core's CMake --install step
    # writes into site-packages/spu94/ when you do `pip install -e .`.
    for entry in sys.path:
        if not entry:
            continue
        cand = Path(entry) / "spu94" / "libspu94.so"
        candidates.append(cand)

    for cand in candidates:
        if cand.exists():
            return str(cand.resolve())

    # No path exists — raise with the full search trail for the user.
    checked = "\n  ".join(str(c) for c in candidates)
    raise OSError(
        "spu94: could not locate libspu94.so. Searched:\n  "
        f"{checked}\n"
        "Set SPU94_LIB to an absolute path, or `pip install spu94` / "
        "`pip install -e .` the package so the library is installed next "
        "to the Python sources."
    )


_LIB_PATH = _resolve_lib_path()
_lib = ctypes.CDLL(_LIB_PATH)

# ----------------------------------------------------------------------
# Prototype declarations — every public symbol in the Phase 5 headers.
# ----------------------------------------------------------------------

# Lifecycle + meta -----------------------------------------------------
_lib.spu94_state_size.restype = ctypes.c_size_t
_lib.spu94_state_size.argtypes = []

_lib.spu94_init.restype = ctypes.c_void_p
_lib.spu94_init.argtypes = [
    ctypes.c_void_p, ctypes.c_size_t,
    ctypes.c_void_p, ctypes.c_size_t,
]

_lib.spu94_reset.restype = None
_lib.spu94_reset.argtypes = [ctypes.c_void_p]

_lib.spu94_destroy.restype = None
_lib.spu94_destroy.argtypes = [ctypes.c_void_p]

_lib.spu94_tick.restype = None
_lib.spu94_tick.argtypes = [ctypes.c_void_p]

_lib.spu94_get_buffer_address.restype = ctypes.c_uint32
_lib.spu94_get_buffer_address.argtypes = [ctypes.c_void_p]

_lib.spu94_get_latency_samples.restype = ctypes.c_uint32
_lib.spu94_get_latency_samples.argtypes = []

# Audio processing — Plan 2 declares argtypes using numpy.ctypeslib
# .ndpointer so the strict int16 / C-contiguous / 1-D contract (D-09)
# is enforced at the binding boundary. ndpointer raises TypeError on
# dtype / flags violations BEFORE the C function is called, which means
# no raw pointer crosses the boundary for a malformed input (T-06-07,
# T-06-08). Zero-copy (D-10) holds when the contract is satisfied.
_ARR_I16_1D = ndpointer(dtype=np.int16, ndim=1, flags="C_CONTIGUOUS")

_lib.spu94_process.restype = None
_lib.spu94_process.argtypes = [
    ctypes.c_void_p,    # state
    _ARR_I16_1D,        # L_in
    _ARR_I16_1D,        # R_in
    _ARR_I16_1D,        # L_out
    _ARR_I16_1D,        # R_out
    ctypes.c_uint32,    # num_samples
]

_lib.spu94_flush.restype = None
_lib.spu94_flush.argtypes = [
    ctypes.c_void_p,    # state
    _ARR_I16_1D,        # L_out
    _ARR_I16_1D,        # R_out
    ctypes.c_uint32,    # num_samples
]

# Preset loader --------------------------------------------------------
_lib.spu94_load_preset.restype = ctypes.c_int    # spu94_result_t enum
_lib.spu94_load_preset.argtypes = [ctypes.c_void_p, ctypes.c_int]

# Register identity + reflection (used at IMPORT TIME by __init__.py) --
_lib.spu94_reg_name.restype = ctypes.c_char_p
_lib.spu94_reg_name.argtypes = [ctypes.c_int]

_lib.spu94_reg_hw_offset.restype = ctypes.c_uint16
_lib.spu94_reg_hw_offset.argtypes = [ctypes.c_int]

_lib.spu94_reg_type.restype = ctypes.c_int       # spu94_reg_type_t (0=I16, 1=U16)
_lib.spu94_reg_type.argtypes = [ctypes.c_int]

_lib.spu94_snapshot_registers.restype = None
_lib.spu94_snapshot_registers.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_int16),
]

# Engine-layer register I/O (signed) -----------------------------------
_lib.spu94_set_reg_i16.restype = ctypes.c_int    # spu94_result_t
_lib.spu94_set_reg_i16.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int16]

_lib.spu94_get_reg_i16.restype = ctypes.c_int16
_lib.spu94_get_reg_i16.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_reg_i16_pending.restype = ctypes.c_int16
_lib.spu94_get_reg_i16_pending.argtypes = [ctypes.c_void_p, ctypes.c_int]

# Engine-layer register I/O (unsigned) ---------------------------------
_lib.spu94_set_reg_u16.restype = ctypes.c_int    # spu94_result_t
_lib.spu94_set_reg_u16.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16]

_lib.spu94_get_reg_u16.restype = ctypes.c_uint16
_lib.spu94_get_reg_u16.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_reg_u16_pending.restype = ctypes.c_uint16
_lib.spu94_get_reg_u16_pending.argtypes = [ctypes.c_void_p, ctypes.c_int]

# ----------------------------------------------------------------------
# Constants mirrored from include/spu94/spu94.h
#
# These are CROSS-CHECKED against the live library by __init__.py's
# drift assertions — they are not trusted blindly. See D-07 and
# threat IDs T-06-02..T-06-04 in the plan's threat register.
# ----------------------------------------------------------------------
SPU94_STATE_SIZE_MAX = 16384
SPU94_STATE_ALIGN_MAX = 16
SPU94_LATENCY_SAMPLES = 58
SPU94_REG__COUNT = 35
SPU94_PRESET__COUNT = 10

# Result codes — re-exported as module-level ints. Plan 2 may promote
# to a proper IntEnum once the public API layer lands.
SPU94_OK = 0
SPU94_CLAMPED = 1
SPU94_UNKNOWN_REG = 2
SPU94_TYPE_MISMATCH = 3
