"""python/spu94/api.py

Phase 6 Plan 2: raw-panel public functions (D-01 primary surface).

Each function takes a ``state`` handle (opaque ctypes.c_void_p returned
by ``init()``) and wraps exactly one C function. State-handle-explicit —
matches the C side 1:1. The SPU94 class in reverb.py wraps these.

Zero-copy contract on process/flush:
  - numpy int16 C-contiguous arrays are required (D-09). numpy's
    ``ndpointer`` raises TypeError on dtype / flag violations.
  - When the contract holds, the C function receives the numpy buffer's
    raw pointer — no intermediate allocation (D-10).

NULL L_in/R_in convention:
  - ``spu94.process(state, None, None, L_out, R_out)`` is legal; the
    Python wrapper substitutes a silent int16 zero buffer for any
    input channel passed as None. The behavior is identical to the C
    side's "NULL substitutes silence" convention.
  - ``L_out`` and ``R_out`` MUST be valid int16 arrays of matching
    length. NULL outputs are a C-side micro-optimization; from Python
    they would mean "computed audio discarded," which is never useful.
"""
import ctypes
from typing import Optional, Tuple, Union

import numpy as np

from ._binding import (
    _lib,
    SPU94_STATE_SIZE_MAX,
    SPU94_STATE_ALIGN_MAX,
    SPU94_LATENCY_SAMPLES,
    SPU94_REG__COUNT,
    SPU94_PRESET__COUNT,
    SPU94_OK,
    SPU94_CLAMPED,
    SPU94_UNKNOWN_REG,
    SPU94_TYPE_MISMATCH,
)

# Forward-referenced at runtime — Register / Preset / presets are built
# by spu94/__init__.py and presets.py after _binding.py loads. We import
# lazily inside each function that needs them to avoid a circular import
# at module-load time (api.py is imported FROM __init__.py).


# ----------------------------------------------------------------------
# Lifecycle (D-01 / API-01 / API-02 / API-09)
# ----------------------------------------------------------------------

def init(work_buf_size: int = 8192) -> ctypes.c_void_p:
    """Allocate state + work buffer and return an opaque state handle.

    Parameters
    ----------
    work_buf_size : int
        Bytes of reverb work buffer to allocate. The PS1 SPU uses 512 KB
        of RAM for the full reverb buffer; 8192 bytes is a small-footprint
        default that covers most short delays. Pass a larger size for
        long-tail presets (Space Echo, Delay).

    Returns
    -------
    ctypes.c_void_p
        Non-NULL state handle. The underlying Python allocations for
        the state buffer and the work buffer are anchored on the handle
        via attribute assignment so the caller doesn't need to manage
        them — they are freed when the handle is GC'd.

    Raises
    ------
    RuntimeError
        If ``spu94_init`` returns NULL (misaligned buffer, zero size,
        etc.). The C-side validator logs a diagnostic; we surface it as
        a Python exception here.
    """
    size = _lib.spu94_state_size()
    # ctypes arrays give us heap-backed storage. Align the state pointer
    # to SPU94_STATE_ALIGN_MAX by overallocating and taking the first
    # aligned address inside the raw buffer.
    raw = (ctypes.c_uint8 * (SPU94_STATE_SIZE_MAX + SPU94_STATE_ALIGN_MAX))()
    raw_addr = ctypes.addressof(raw)
    aligned_addr = (raw_addr + SPU94_STATE_ALIGN_MAX - 1) & ~(SPU94_STATE_ALIGN_MAX - 1)
    state_buf_ptr = ctypes.c_void_p(aligned_addr)
    work_buf = (ctypes.c_uint8 * work_buf_size)()
    state = _lib.spu94_init(
        state_buf_ptr,
        SPU94_STATE_SIZE_MAX,
        ctypes.cast(work_buf, ctypes.c_void_p),
        work_buf_size,
    )
    if not state:
        raise RuntimeError(
            f"spu94_init returned NULL (state_size={size}, "
            f"work_buf_size={work_buf_size}). Check alignment and sizing."
        )
    # Anchor raw + work_buf on the state pointer object so GC doesn't
    # free them while the state is live. ctypes lets us attach arbitrary
    # attributes on c_void_p instances.
    handle = ctypes.c_void_p(state)
    handle._spu94_state_buf = raw      # keep-alive anchor
    handle._spu94_work_buf = work_buf  # keep-alive anchor
    return handle


def reset(state: ctypes.c_void_p) -> None:
    """Restore state to post-init invariants. NULL-safe."""
    _lib.spu94_reset(state)


def destroy(state: ctypes.c_void_p) -> None:
    """Zero state bytes (security hygiene). NULL-safe. Memory release
    happens when the handle falls out of Python scope."""
    _lib.spu94_destroy(state)


def tick(state: ctypes.c_void_p) -> None:
    """Advance one 22.05 kHz tick. Usually driven by ``process``
    internally; exposed here for tests and preset-load + snapshot
    sequences that need an explicit tick to latch pending writes."""
    _lib.spu94_tick(state)


# ----------------------------------------------------------------------
# Audio processing (D-09..D-11 / PYBIND-02 / API-03, API-06)
# ----------------------------------------------------------------------

# Cached silent input buffer — used when process() is called with
# L_in=None or R_in=None. The C side accepts NULL for these but our
# ndpointer argtypes reject None, so we substitute a zero-filled numpy
# array. Cached to avoid allocating a new buffer per block; resized on
# demand when a caller passes a block larger than any prior call.
_silent_in: Optional[np.ndarray] = None


def _silent_input(n: int) -> np.ndarray:
    """Return a cached zero-filled int16 C-contiguous array of length n.

    The cache grows monotonically — if a caller has passed a 44100-sample
    block we keep a 44100-sample silent buffer around for reuse. Smaller
    subsequent blocks use a slice of the cached buffer (which is still
    C-contiguous because a leading slice of a 1-D contiguous array is
    contiguous)."""
    global _silent_in
    if _silent_in is None or len(_silent_in) < n:
        _silent_in = np.zeros(n, dtype=np.int16)
        return _silent_in
    if len(_silent_in) == n:
        return _silent_in
    return _silent_in[:n]  # leading slice is contiguous


def _validate_process_lengths(L_in, R_in, L_out, R_out) -> int:
    """Return num_samples. Raises ValueError on mismatch (D-09, T-06-09).

    L_in / R_in may be None (substituted with silence downstream); L_out /
    R_out must be non-None and equal-length (PYBIND-02 contract)."""
    n = len(L_out)
    if len(R_out) != n:
        raise ValueError(
            f"spu94.process requires L_out and R_out of equal length; "
            f"L_out={n}, R_out={len(R_out)}"
        )
    if L_in is not None and len(L_in) != n:
        raise ValueError(
            f"spu94.process requires input and output arrays of equal length; "
            f"L_in={len(L_in)}, L_out={n}, R_out={len(R_out)}"
        )
    if R_in is not None and len(R_in) != n:
        raise ValueError(
            f"spu94.process requires input and output arrays of equal length; "
            f"R_in={len(R_in)}, L_out={n}, R_out={len(R_out)}"
        )
    return n


def process(state: ctypes.c_void_p,
            L_in: Optional[np.ndarray],
            R_in: Optional[np.ndarray],
            L_out: np.ndarray,
            R_out: np.ndarray) -> None:
    """Block-process stereo int16 @ 44.1 kHz (D-01..D-04, D-09..D-11).

    L_in / R_in may be None to substitute silence on that channel
    (matches the C-side NULL convention). L_out and R_out must be valid
    numpy int16 arrays of matching length; the C side writes processed
    audio into them.

    numpy contract (D-09):
      - dtype must be exactly int16
      - arrays must be C-contiguous
      - all non-None arrays must have the same length

    Zero-copy (D-10): when the contract holds, ndpointer passes the
    buffer pointer directly to C. No intermediate conversion.

    Raises
    ------
    TypeError
        dtype or flags violation (message includes 'int16' or
        'contiguous' / 'C_CONTIGUOUS' depending on which rule was
        broken). The wrapper upgrades the raw ndpointer error with
        actionable conversion guidance (Pitfall 5).
    ValueError
        Mismatched lengths between provided arrays.
    """
    n = _validate_process_lengths(L_in, R_in, L_out, R_out)
    # Substitute silent buffers for None inputs — the C side accepts
    # NULL here but ndpointer argtypes reject None, so we hand the C
    # function a pointer to a cached zero buffer instead. Semantics
    # match: both paths deliver silence to the input.
    L_eff = L_in if L_in is not None else _silent_input(n)
    R_eff = R_in if R_in is not None else _silent_input(n)
    try:
        _lib.spu94_process(state, L_eff, R_eff, L_out, R_out, n)
    except ctypes.ArgumentError as e:
        _raise_upgraded(e, where="spu94.process")


def flush(state: ctypes.c_void_p,
          L_out: np.ndarray,
          R_out: np.ndarray) -> None:
    """Drain trailing reverb tail by feeding silence (D-02). Same numpy
    contract as ``process`` — int16, C-contig, equal length required."""
    if len(L_out) != len(R_out):
        raise ValueError(
            f"spu94.flush requires L_out and R_out of equal length; "
            f"L_out={len(L_out)}, R_out={len(R_out)}"
        )
    n = len(L_out)
    try:
        _lib.spu94_flush(state, L_out, R_out, n)
    except ctypes.ArgumentError as e:
        _raise_upgraded(e, where="spu94.flush")


def _raise_upgraded(e: ctypes.ArgumentError, *, where: str) -> None:
    """Upgrade ndpointer's stock ArgumentError with actionable guidance
    and re-raise as TypeError (T-06-07, T-06-08, Pitfall 5).

    This centralizes the float32 / non-contiguous messaging so process()
    and flush() share one wording — keeps the public surface quiet about
    which function tripped, and gives float32-in-[-1,1] callers the
    exact conversion recipe they need.
    """
    msg = str(e)
    if "int16" in msg:
        raise TypeError(
            f"{where} requires int16 numpy arrays (one sample per int16, "
            f"range [-32768, 32767]). If your audio is float32 in "
            f"[-1.0, 1.0], convert with: "
            f"(arr * 32767).clip(-32768, 32767).astype(np.int16). "
            f"Original error: {msg}"
        ) from None
    if "C_CONTIGUOUS" in msg or "contiguous" in msg.lower():
        raise TypeError(
            f"{where} requires C-contiguous numpy arrays. If your array "
            f"is a non-contiguous slice (e.g. stereo[:, 0] on a 2-D "
            f"interleaved array), copy it with np.ascontiguousarray(arr). "
            f"Original error: {msg}"
        ) from None
    # Unknown ArgumentError shape — surface the original for debugging.
    raise


# ----------------------------------------------------------------------
# Preset loading (API-05, PYBIND-04)
# ----------------------------------------------------------------------

def load_preset(state: ctypes.c_void_p,
                preset: Union[int, str, "Preset"]) -> int:
    """Atomically load one of the 10 factory presets.

    Accepts:
      - Preset enum (``spu94.Preset.HALL``)
      - int preset id (``5``)
      - case-insensitive string (``"hall"``, ``"Hall"``, ``"HALL"``,
        ``"Studio A"``, ``"studio_a"`` — all normalized by the
        presets lookup table)

    Returns the ``spu94_result_t`` code: ``SPU94_OK`` on success,
    ``SPU94_UNKNOWN_REG`` if the preset id is out of range. String
    lookup failures raise ``ValueError`` directly (a typoed preset
    name is a caller bug, not a runtime condition)."""
    from . import Preset, presets  # late import — avoids circular
    if isinstance(preset, Preset):
        preset_id = int(preset)
    elif isinstance(preset, str):
        try:
            info = presets[preset]
        except KeyError as e:
            raise ValueError(str(e)) from None
        preset_id = info.id
    elif isinstance(preset, int):
        preset_id = preset
    else:
        raise TypeError(
            f"load_preset expects Preset, int, or str; got {type(preset).__name__}"
        )
    return _lib.spu94_load_preset(state, preset_id)


# ----------------------------------------------------------------------
# Register I/O (API-04, PYBIND-03)
# ----------------------------------------------------------------------

def _coerce_reg(reg) -> int:
    """Accept Register enum, int, or name string."""
    from . import Register
    if isinstance(reg, Register):
        return int(reg)
    if isinstance(reg, bool):
        # bool subclasses int in Python; reject explicitly to avoid
        # silently resolving True → 1 / False → 0.
        raise TypeError("register key must be Register, int, or str; got bool")
    if isinstance(reg, int):
        return reg
    if isinstance(reg, str):
        # Case-sensitive match against the enum member name.
        return int(Register[reg])
    raise TypeError(
        f"register key must be Register, int, or str; got {type(reg).__name__}"
    )


def set_reg_i16(state, reg, value: int) -> int:
    """Set a signed (int16-family) register. Returns SPU94_TYPE_MISMATCH
    if the register is actually u16-family (e.g. mBASE, delay addresses)."""
    return _lib.spu94_set_reg_i16(state, _coerce_reg(reg), int(value))


def set_reg_u16(state, reg, value: int) -> int:
    """Set an unsigned (uint16-family) register. Returns SPU94_TYPE_MISMATCH
    if the register is actually i16-family (e.g. gain coefficients)."""
    return _lib.spu94_set_reg_u16(state, _coerce_reg(reg), int(value))


def get_reg_i16(state, reg) -> int:
    """Read the active (committed) signed register value."""
    return _lib.spu94_get_reg_i16(state, _coerce_reg(reg))


def get_reg_u16(state, reg) -> int:
    """Read the active (committed) unsigned register value."""
    return _lib.spu94_get_reg_u16(state, _coerce_reg(reg))


def get_reg_i16_pending(state, reg) -> int:
    """Read a staged signed-register write that has not been latched
    by a tick yet (D-06)."""
    return _lib.spu94_get_reg_i16_pending(state, _coerce_reg(reg))


def get_reg_u16_pending(state, reg) -> int:
    """Read a staged unsigned-register write that has not been latched
    by a tick yet (D-06)."""
    return _lib.spu94_get_reg_u16_pending(state, _coerce_reg(reg))


def snapshot_registers(state) -> Tuple[int, ...]:
    """Return a 35-tuple of int16 values — the active register state.

    Indexed by ``Register`` (e.g. ``snap[spu94.Register.vIIR]`` reads
    the current vIIR value). Values are returned as signed Python
    ints; callers inspecting u16 registers should mask to 0xFFFF if
    they need the raw bit pattern (most u16 registers are delay/
    address values, where the signed representation rounds-trips
    cleanly for values < 0x8000)."""
    out = (ctypes.c_int16 * SPU94_REG__COUNT)()
    _lib.spu94_snapshot_registers(state, out)
    return tuple(out[i] for i in range(SPU94_REG__COUNT))


def get_buffer_address(state) -> int:
    """Return the current work-buffer write cursor (0..0x7FFFE, halfword-
    aligned except immediately after a mBASE snap-on-write)."""
    return _lib.spu94_get_buffer_address(state)


def get_latency_samples() -> int:
    """Return the fixed FIR-chain round-trip group delay in samples
    (58 at 44.1 kHz per Phase 4 research)."""
    return _lib.spu94_get_latency_samples()


# ----------------------------------------------------------------------
# self_test — used by cibuildwheel's test-command (D-25) and as a
# troubleshooting aid callable by end users (README Plan 5 references it).
# ----------------------------------------------------------------------

def self_test() -> None:
    """Minimal end-to-end smoke test.

    1. Allocate state + work buffer.
    2. Load Hall preset.
    3. Tick once to commit pending (address / delay) register writes.
    4. Process 1 second of silent stereo audio (44100 samples).
    5. Flush the tail (1 second).
    6. Assert dtype is int16 (guards against future regressions where
       the wrapper might accidentally reassign an output).
    7. Destroy state.

    Raises
    ------
    Any Python exception propagates — ``cibuildwheel`` catches it as a
    non-zero exit and the wheel is rejected.
    """
    state = init(work_buf_size=8192)
    try:
        rc = load_preset(state, "hall")
        if rc != SPU94_OK:
            raise RuntimeError(
                f"load_preset(hall) returned {rc}, expected SPU94_OK"
            )
        tick(state)
        n = 44100
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        process(state, None, None, L_out, R_out)
        # Invariant: int16 arrays stay int16 across the call.
        assert L_out.dtype == np.int16
        assert R_out.dtype == np.int16
        flush(state, L_out, R_out)
    finally:
        destroy(state)
