"""python/spu94/presets.py — Phase 6 Plan 1 Task 3.

Exposes the 10 factory presets as Python data (D-02). Two surfaces:

  1. ``Preset`` — an ``IntEnum`` of 10 members (``OFF..DELAY``) whose
     numeric values match ``include/spu94/spu94.h``'s ``spu94_preset_id_t``.
     ABI-stable; never renumber.

  2. ``presets`` — a dict-like accessor supporting several key forms::

        presets["hall"]                     (lowercase string)
        presets["Hall"]                     (mixed case — normalized)
        presets["Studio A"]                 (space → underscore)
        presets[Preset.HALL]                (enum)
        presets[5]                          (int)
        list(presets)                       (iterates 10 PresetInfo
                                             objects in preset-id order)

Data flows directly from the C ``spu94_presets[]`` .rodata symbol via
``ctypes.in_dll`` — no hand-typed parallel tables in Python. If a
future phase reorders or renames the C-side presets, the drift
assertion at the bottom of this module fires at import time with a
RuntimeError describing which preset differs.

Threat mitigation T-06-04: preset-table drift (reorder / rename) is
caught by cross-checking each C-side ``name`` against a pinned list
of expected human-readable names.
"""
import ctypes
from dataclasses import dataclass
from enum import IntEnum
from typing import Tuple, Union

from ._binding import _lib, SPU94_PRESET__COUNT, SPU94_REG__COUNT


# ----------------------------------------------------------------------
# ctypes mirror of ``spu94_preset_t``
# ----------------------------------------------------------------------

class _CPreset(ctypes.Structure):
    """Mirrors ``struct { const char *name; int16_t regs[35]; }`` from
    ``include/spu94/spu94.h``. The ``spu94_presets[]`` array is read as
    ``(_CPreset * 10).in_dll(_lib, "spu94_presets")``."""

    _fields_ = [
        ("name", ctypes.c_char_p),
        ("regs", ctypes.c_int16 * SPU94_REG__COUNT),
    ]


# Read the ``extern const spu94_preset_t spu94_presets[10]`` symbol.
_presets_arr = (_CPreset * SPU94_PRESET__COUNT).in_dll(_lib, "spu94_presets")


# ----------------------------------------------------------------------
# Preset IntEnum — numeric values are ABI per include/spu94/spu94.h.
#
# We do NOT reflect these from the C-side ``.name`` strings: the C names
# are human-readable display names ("Off", "Room", "Studio A", ...)
# whereas the enum identifiers are uppercase snake case (OFF, ROOM,
# STUDIO_A, ...). The hard-coded IntEnum is the Python identifier
# surface; the drift assertion below defends the display names.
# ----------------------------------------------------------------------

class Preset(IntEnum):
    """Factory preset identifiers. Numeric values are ABI-stable per
    ``spu94_preset_id_t`` in ``include/spu94/spu94.h``. Never renumber."""

    OFF = 0
    ROOM = 1
    STUDIO_A = 2
    STUDIO_B = 3
    STUDIO_C = 4
    HALL = 5
    HALF_ECHO = 6
    SPACE_ECHO = 7
    ECHO = 8
    DELAY = 9


# ----------------------------------------------------------------------
# Drift assertion — the human-readable names in the C ``spu94_presets[]``
# table MUST match the pinned list. If a future phase reorders or renames
# entries, this fires with RuntimeError (T-06-04).
# ----------------------------------------------------------------------

_EXPECTED_NAMES = (
    "Off", "Room", "Studio A", "Studio B", "Studio C",
    "Hall", "Half Echo", "Space Echo", "Echo", "Delay",
)

for _i, _expected in enumerate(_EXPECTED_NAMES):
    _actual_bytes = _presets_arr[_i].name
    if _actual_bytes is None:
        raise RuntimeError(
            f"spu94 library mismatch: spu94_presets[{_i}].name is NULL; "
            f"expected {_expected!r}."
        )
    _actual = _actual_bytes.decode("ascii")
    if _actual != _expected:
        raise RuntimeError(
            f"spu94 library mismatch: spu94_presets[{_i}].name = "
            f"{_actual!r}, expected {_expected!r}. Preset table has "
            f"been reordered or renamed — update python/spu94/presets.py "
            f"_EXPECTED_NAMES and coordinate the change with "
            f"docs/DECISIONS.md."
        )


# ----------------------------------------------------------------------
# Public PresetInfo value type
# ----------------------------------------------------------------------

@dataclass(frozen=True)
class PresetInfo:
    """Snapshot of one factory preset.

    ``.regs`` is a 35-tuple of int16 indexed by ``Register`` (e.g.
    ``info.regs[Register.mBASE]`` reads this preset's mBASE value).
    Frozen / immutable — represents .rodata content.
    """

    id: int
    name: str                    # human-readable (e.g. "Hall")
    regs: Tuple[int, ...]        # length == 35


# ----------------------------------------------------------------------
# Build the accessor lookup tables at module import time.
# ----------------------------------------------------------------------

_preset_infos_by_id: "dict[int, PresetInfo]" = {}
_preset_infos_by_name: "dict[str, PresetInfo]" = {}


def _normalize_key(key: str) -> str:
    """Canonical preset-name key: lowercase, spaces → underscores.

    Matches the CLI ``--preset`` contract that Plan 3 will adopt.
    ``"Hall"`` → ``"hall"``; ``"Studio A"`` → ``"studio_a"``;
    ``"Half Echo"`` → ``"half_echo"``.
    """
    return key.lower().replace(" ", "_")


for _i in range(SPU94_PRESET__COUNT):
    _name_str = _presets_arr[_i].name.decode("ascii")
    _regs_tuple = tuple(_presets_arr[_i].regs[_j] for _j in range(SPU94_REG__COUNT))
    _info = PresetInfo(id=_i, name=_name_str, regs=_regs_tuple)
    _preset_infos_by_id[_i] = _info
    _preset_infos_by_name[_normalize_key(_name_str)] = _info


# ----------------------------------------------------------------------
# The dict-like accessor
# ----------------------------------------------------------------------

class _PresetTable:
    """Read-only dict-like accessor for the factory presets (D-02).

    Supports integer, Preset enum, and case-insensitive string keys.
    Iteration yields PresetInfo objects in preset-id order.
    """

    def __getitem__(self, key: Union[int, Preset, str]) -> PresetInfo:
        if isinstance(key, Preset):
            return _preset_infos_by_id[int(key)]
        if isinstance(key, bool):
            # bool is a subclass of int in Python; guard against it
            # silently resolving to True → 1 / False → 0.
            raise TypeError("Preset key must be int, Preset, or str; got bool")
        if isinstance(key, int):
            if key not in _preset_infos_by_id:
                raise KeyError(f"Preset id {key} out of range (0..9)")
            return _preset_infos_by_id[key]
        if isinstance(key, str):
            normalized = _normalize_key(key)
            if normalized not in _preset_infos_by_name:
                valid = ", ".join(self.keys())
                raise KeyError(
                    f"Unknown preset {key!r}; valid: {valid}"
                )
            return _preset_infos_by_name[normalized]
        raise TypeError(
            f"Preset key must be int, Preset, or str; got {type(key).__name__}"
        )

    def __iter__(self):
        return (_preset_infos_by_id[i] for i in range(SPU94_PRESET__COUNT))

    def __len__(self):
        return SPU94_PRESET__COUNT

    def __contains__(self, key) -> bool:
        try:
            self[key]
            return True
        except (KeyError, TypeError):
            return False

    def keys(self):
        """Canonical lowercase-underscore preset names in preset-id order
        (useful for CLI ``--list-presets`` and error messages)."""
        return [
            _normalize_key(_preset_infos_by_id[i].name)
            for i in range(SPU94_PRESET__COUNT)
        ]


presets = _PresetTable()
