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

# Work-buf sizing (ADR-0022) --- scans the preset's u16 address registers
# and returns the minimum work_buf_size in bytes required to load the preset
# safely. Returns 0 for an out-of-range id. ---------------------------
_lib.spu94_preset_min_work_buf_size.restype = ctypes.c_size_t
_lib.spu94_preset_min_work_buf_size.argtypes = [ctypes.c_int]


# Observable error counters (ADR-0023) --- snapshot of the C-side
# spu94_error_counters_t struct. Currently a single uint64 field
# (oob_tap_count); append-only if future counters land. Declared as a
# Structure so ctypes packs it to match the C layout.
class _Spu94ErrorCounters(ctypes.Structure):
    _fields_ = [("oob_tap_count", ctypes.c_uint64)]


_lib.spu94_get_error_counters.restype = _Spu94ErrorCounters
_lib.spu94_get_error_counters.argtypes = [ctypes.c_void_p]

# ADPCM codec state (M2 Phase 3, D-09). Two int16 fields (old, older) = 4 bytes.
# ctypes.sizeof(_Spu94AdpcmState) MUST equal 4; validated in test suite.
class _Spu94AdpcmState(ctypes.Structure):
    _fields_ = [("old", ctypes.c_int16), ("older", ctypes.c_int16)]

# VAG header struct (M2 Phase 3, D-10). Matches spu94_vag_header in spu94_vag.h.
class _Spu94VagHeader(ctypes.Structure):
    _fields_ = [
        ("version", ctypes.c_uint32),
        ("data_size", ctypes.c_uint32),
        ("sample_rate", ctypes.c_uint32),
        ("name", ctypes.c_char * 16),
    ]

# ADPCM block-level codec (M2 Phase 3, D-09) --------------------------------

_lib.spu94_adpcm_decode_block.restype = ctypes.c_uint8  # flag byte
_lib.spu94_adpcm_decode_block.argtypes = [
    ctypes.POINTER(_Spu94AdpcmState),     # state
    ctypes.POINTER(ctypes.c_uint8),       # block[16]
    ctypes.POINTER(ctypes.c_int16),       # out[28]
]

_lib.spu94_adpcm_encode_block.restype = None
_lib.spu94_adpcm_encode_block.argtypes = [
    ctypes.POINTER(_Spu94AdpcmState),     # state
    ctypes.POINTER(ctypes.c_int16),       # in[28]
    ctypes.c_uint8,                       # flags
    ctypes.POINTER(ctypes.c_uint8),       # block[16]
]

# ADPCM pipeline toggle (M2 Phase 3, D-09) ----------------------------------

_lib.spu94_set_adpcm_enabled.restype = None
_lib.spu94_set_adpcm_enabled.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_adpcm_enabled.restype = ctypes.c_int
_lib.spu94_get_adpcm_enabled.argtypes = [ctypes.c_void_p]

_lib.spu94_get_total_latency_samples.restype = ctypes.c_uint32
_lib.spu94_get_total_latency_samples.argtypes = [ctypes.c_void_p]

# Phase 8: Full mixer/DAC control surface (DAC-IO-02) -------------------------

# Fader setters (4 declared in Phase 7, getters + 2 missing pairs added here)
_lib.spu94_set_input_gain.restype = None
_lib.spu94_set_input_gain.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_input_gain.restype = ctypes.c_int16
_lib.spu94_get_input_gain.argtypes = [ctypes.c_void_p]

_lib.spu94_set_dry_fader.restype = None
_lib.spu94_set_dry_fader.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_dry_fader.restype = ctypes.c_int16
_lib.spu94_get_dry_fader.argtypes = [ctypes.c_void_p]

_lib.spu94_set_reverb_fader.restype = None
_lib.spu94_set_reverb_fader.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_reverb_fader.restype = ctypes.c_int16
_lib.spu94_get_reverb_fader.argtypes = [ctypes.c_void_p]

_lib.spu94_set_dry_send.restype = None
_lib.spu94_set_dry_send.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_dry_send.restype = ctypes.c_int16
_lib.spu94_get_dry_send.argtypes = [ctypes.c_void_p]

_lib.spu94_set_patina_fader.restype = None
_lib.spu94_set_patina_fader.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_patina_fader.restype = ctypes.c_int16
_lib.spu94_get_patina_fader.argtypes = [ctypes.c_void_p]

_lib.spu94_set_patina_send.restype = None
_lib.spu94_set_patina_send.argtypes = [ctypes.c_void_p, ctypes.c_int16]

_lib.spu94_get_patina_send.restype = ctypes.c_int16
_lib.spu94_get_patina_send.argtypes = [ctypes.c_void_p]

# Latency compensation (Phase 7, D-07/D-08)
_lib.spu94_set_latency_comp.restype = None
_lib.spu94_set_latency_comp.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_latency_comp.restype = ctypes.c_int
_lib.spu94_get_latency_comp.argtypes = [ctypes.c_void_p]

# DAC coloration section (Phase 7, D-09 through D-12)
_lib.spu94_set_dac_enabled.restype = None
_lib.spu94_set_dac_enabled.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_dac_enabled.restype = ctypes.c_int
_lib.spu94_get_dac_enabled.argtypes = [ctypes.c_void_p]

_lib.spu94_set_dac_fir_enabled.restype = None
_lib.spu94_set_dac_fir_enabled.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_dac_fir_enabled.restype = ctypes.c_int
_lib.spu94_get_dac_fir_enabled.argtypes = [ctypes.c_void_p]

_lib.spu94_set_dac_noise_enabled.restype = None
_lib.spu94_set_dac_noise_enabled.argtypes = [ctypes.c_void_p, ctypes.c_int]

_lib.spu94_get_dac_noise_enabled.restype = ctypes.c_int
_lib.spu94_get_dac_noise_enabled.argtypes = [ctypes.c_void_p]

# VAG file format I/O (M2 Phase 3, D-10) ------------------------------------

_lib.spu94_vag_read_header.restype = ctypes.c_int  # 0=ok, -1=bad magic
_lib.spu94_vag_read_header.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),       # buf[48]
    ctypes.POINTER(_Spu94VagHeader),      # out
]

_lib.spu94_vag_write_header.restype = None
_lib.spu94_vag_write_header.argtypes = [
    ctypes.POINTER(ctypes.c_uint8),       # buf[48]
    ctypes.c_uint32,                      # data_size
    ctypes.c_uint32,                      # sample_rate
    ctypes.c_char_p,                      # name (may be NULL)
]

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

# Work-buf sizing contract (ADR-0022). SPU94_WORK_BUF_MAX_BYTES is the PS1
# SPU's full 512 KiB RAM — guaranteed to fit every factory preset. Callers
# that don't know which preset(s) they'll load can size against this and
# forget about it. Embedded callers can instead query
# spu94_preset_min_work_buf_size(id) for an exact bound per preset.
SPU94_WORK_BUF_MAX_BYTES = 0x80000

# Result codes — re-exported as module-level ints. Plan 2 may promote
# to a proper IntEnum once the public API layer lands.
SPU94_OK = 0
SPU94_CLAMPED = 1
SPU94_UNKNOWN_REG = 2
SPU94_TYPE_MISMATCH = 3
# Appended 2026-04-24 (ADR-0022): argument validation at mutation sites.
# Existing codes 0..3 keep their names and numeric values (append-only D-07).
SPU94_INVALID_STATE = 4
SPU94_WORK_BUF_TOO_SMALL = 5
SPU94_INVALID_ARG = 6

# ADPCM block constants (M2 Phase 3)
SPU94_ADPCM_BLOCK_SAMPLES = 28
SPU94_ADPCM_BLOCK_BYTES = 16
SPU94_VAG_HEADER_BYTES = 48
