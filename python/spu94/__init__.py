"""python/spu94/__init__.py — Phase 6 Plan 1 entry point.

Responsibilities, in order at import time:

  1. Load ``libspu94.so`` by absolute path (via ``_binding._lib``).
  2. Verify the library is self-consistent (drift assertions — D-07,
     threat mitigations T-06-02..T-06-04).
  3. Build ``spu94.Register`` by iterating ``spu94_reg_name(i)`` over
     ``0..SPU94_REG__COUNT-1`` (D-06). The live library is authoritative;
     Python has no parallel typed list that could drift.
  4. Build ``spu94.Preset`` + ``spu94.presets`` — delegated to
     ``presets.py`` so the ``ctypes.in_dll`` concern is isolated.
  5. Publish the module's public surface: ``_lib``, ``Register``,
     ``Preset``, ``presets``, the re-exported C constants, and the
     public ``__all__``.

The raw-panel public API (``init``, ``destroy``, ``process``, ``flush``,
``load_preset``, ``tick``, ``get_reg_*``, ``set_reg_*``,
``snapshot_registers``, ``get_buffer_address``, ``get_latency_samples``)
and the ``SPU94`` class wrapper are DELIVERED BY PLAN 2. Plan 1 exposes
only the reflection scaffolding those depend on.
"""
from enum import IntEnum

from ._binding import (
    _lib,
    SPU94_STATE_SIZE_MAX,
    SPU94_STATE_ALIGN_MAX,
    SPU94_LATENCY_SAMPLES,
    SPU94_REG__COUNT,
    SPU94_PRESET__COUNT,
    SPU94_WORK_BUF_MAX_BYTES,
    SPU94_OK,
    SPU94_CLAMPED,
    SPU94_UNKNOWN_REG,
    SPU94_TYPE_MISMATCH,
    SPU94_INVALID_STATE,
    SPU94_WORK_BUF_TOO_SMALL,
    SPU94_INVALID_ARG,
)

# ----------------------------------------------------------------------
# Drift assertions (D-07, T-06-02..T-06-04)
# ----------------------------------------------------------------------

_state_size = _lib.spu94_state_size()
if _state_size > SPU94_STATE_SIZE_MAX:
    raise RuntimeError(
        f"spu94 library mismatch: spu94_state_size() = {_state_size} exceeds "
        f"SPU94_STATE_SIZE_MAX = {SPU94_STATE_SIZE_MAX}. The library is from a "
        f"future incompatible build — upgrade the Python binding or pin the "
        f"library version."
    )

# Cache for Plan 2's SPU94 class and the eventual self_test() helper.
_STATE_SIZE = _state_size


# ----------------------------------------------------------------------
# Register IntEnum — runtime reflection (D-06)
# ----------------------------------------------------------------------

def _reflect_registers():
    """Walk ``spu94_reg_name(0..SPU94_REG__COUNT-1)`` to build the
    Register IntEnum. Validate that the sentinel call
    ``spu94_reg_name(SPU94_REG__COUNT)`` returns NULL — this catches
    "count grew" drift (T-06-03)."""
    members = {}
    for i in range(SPU94_REG__COUNT):
        name_bytes = _lib.spu94_reg_name(i)
        if not name_bytes:
            raise RuntimeError(
                f"spu94 library mismatch: spu94_reg_name({i}) returned NULL. "
                f"Library reports fewer than SPU94_REG__COUNT = "
                f"{SPU94_REG__COUNT} registers. Recompile the bindings "
                f"against the live library, or pin the library version."
            )
        members[name_bytes.decode("ascii")] = i

    sentinel = _lib.spu94_reg_name(SPU94_REG__COUNT)
    if sentinel is not None:
        raise RuntimeError(
            f"spu94 library mismatch: spu94_reg_name({SPU94_REG__COUNT}) "
            f"returned non-NULL ({sentinel!r}). Library has MORE than "
            f"{SPU94_REG__COUNT} registers. Recompile the bindings."
        )
    return IntEnum("Register", members, module=__name__)


Register = _reflect_registers()


# ----------------------------------------------------------------------
# Preset IntEnum + .presets accessor (delegated to presets.py so the
# ctypes.in_dll concern is isolated; Task 3 lands the full implementation)
# ----------------------------------------------------------------------
from .presets import Preset, presets  # noqa: E402 — after Register is built


# ----------------------------------------------------------------------
# Public re-exports
# ----------------------------------------------------------------------
__all__ = [
    "Register",
    "Preset",
    "presets",
    "SPU94_STATE_SIZE_MAX",
    "SPU94_STATE_ALIGN_MAX",
    "SPU94_LATENCY_SAMPLES",
    "SPU94_REG__COUNT",
    "SPU94_PRESET__COUNT",
    "SPU94_WORK_BUF_MAX_BYTES",
    "SPU94_OK",
    "SPU94_CLAMPED",
    "SPU94_UNKNOWN_REG",
    "SPU94_TYPE_MISMATCH",
    "SPU94_INVALID_STATE",
    "SPU94_WORK_BUF_TOO_SMALL",
    "SPU94_INVALID_ARG",
]

# ----------------------------------------------------------------------
# Plan 2 Task 1 — raw-panel public functions (D-01 primary surface)
# ----------------------------------------------------------------------
from .api import (  # noqa: E402 — after Register / Preset / presets build
    init,
    reset,
    destroy,
    tick,
    process,
    flush,
    load_preset,
    set_reg_i16,
    set_reg_u16,
    get_reg_i16,
    get_reg_u16,
    get_reg_i16_pending,
    get_reg_u16_pending,
    snapshot_registers,
    get_buffer_address,
    get_latency_samples,
    self_test,
)

__all__ += [
    "init",
    "reset",
    "destroy",
    "tick",
    "process",
    "flush",
    "load_preset",
    "set_reg_i16",
    "set_reg_u16",
    "get_reg_i16",
    "get_reg_u16",
    "get_reg_i16_pending",
    "get_reg_u16_pending",
    "snapshot_registers",
    "get_buffer_address",
    "get_latency_samples",
    "self_test",
]
# ----------------------------------------------------------------------
# Plan 2 Task 2 — SPU94 class (D-01 secondary surface) + cli shim
# ----------------------------------------------------------------------
from .reverb import SPU94  # noqa: E402 — after api.py is importable
from . import cli  # noqa: E402,F401 — exposed as spu94.cli.main

__all__ += [
    "SPU94",
    "cli",
]
