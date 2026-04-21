"""python/spu94/presets.py — Task 2 stub.

Exposes the ABI-stable ``Preset`` IntEnum and an empty ``presets``
accessor so ``__init__.py``'s ``from .presets import Preset, presets``
resolves while Task 2 focuses on the surface / register-intenum /
drift-detection tests.

Task 3 REPLACES this module with the full data-backed implementation:
``ctypes.in_dll(_lib, "spu94_presets")`` pulls the .rodata table,
preset-name drift assertions fire if the C-side names shift, and the
``presets`` accessor supports string, enum, and integer keys and
iteration in preset-id order.
"""
from enum import IntEnum


class Preset(IntEnum):
    """Factory preset identifiers — numeric values are ABI per
    ``include/spu94/spu94.h`` ``spu94_preset_id_t``. Never renumber."""

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


# Task 2 placeholder — Task 3 replaces with a fully-backed
# ``_PresetTable()`` instance that reads ``spu94_presets[]`` via
# ``ctypes.in_dll`` and supports string / enum / integer keys.
presets = None  # type: ignore[assignment]
