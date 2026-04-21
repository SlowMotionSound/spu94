"""PYBIND-04: spu94.presets exposes the 10 factory presets as Python data.

``spu94.presets`` is a dict-like accessor backed by the C ``spu94_presets[]``
.rodata table imported via ``ctypes.in_dll``. The Preset IntEnum is
ABI-stable (numeric values pinned by ``spu94_preset_id_t`` in the public
header). The behavioral-witness test at the bottom proves the accessor's
.regs tuple matches what ``spu94_load_preset`` actually writes to state.
"""
import ctypes
from enum import IntEnum


def test_preset_is_intenum_with_10_members(spu94_module):
    assert issubclass(spu94_module.Preset, IntEnum)
    assert len(spu94_module.Preset) == 10


def test_preset_numeric_values_abi_stable(spu94_module):
    P = spu94_module.Preset
    assert P.OFF.value == 0
    assert P.ROOM.value == 1
    assert P.STUDIO_A.value == 2
    assert P.STUDIO_B.value == 3
    assert P.STUDIO_C.value == 4
    assert P.HALL.value == 5
    assert P.HALF_ECHO.value == 6
    assert P.SPACE_ECHO.value == 7
    assert P.ECHO.value == 8
    assert P.DELAY.value == 9


def test_presets_len_10(spu94_module):
    assert len(spu94_module.presets) == 10


def test_presets_string_and_enum_agree(spu94_module):
    """Every access form resolves to the same underlying PresetInfo."""
    by_str = spu94_module.presets["hall"]
    by_enum = spu94_module.presets[spu94_module.Preset.HALL]
    by_int = spu94_module.presets[5]
    by_upper = spu94_module.presets["Hall"]
    assert by_str == by_enum == by_int == by_upper
    assert by_str.id == 5
    assert by_str.name == "Hall"


def test_preset_has_35_reg_values(spu94_module):
    """Every preset ships a 35-element int16 tuple."""
    hall = spu94_module.presets["hall"]
    assert len(hall.regs) == 35
    for v in hall.regs:
        assert -32768 <= v <= 32767


def test_iteration_yields_10_presetinfo_in_order(spu94_module):
    infos = list(spu94_module.presets)
    assert len(infos) == 10
    for i, info in enumerate(infos):
        assert info.id == i


def test_off_preset_has_all_zero_gains(spu94_module):
    """Off preset is silent — Phase 5 Plan 03's
    test_preset_nonzero_tail proved this behaviorally. Sanity here: its
    vIIR, vCOMB1..4, vWALL, vAPF1/2, vLIN, vRIN must all be 0 (zero gain)."""
    off = spu94_module.presets["off"]
    v_gain_regs = [
        spu94_module.Register.vIIR,
        spu94_module.Register.vCOMB1,
        spu94_module.Register.vCOMB2,
        spu94_module.Register.vCOMB3,
        spu94_module.Register.vCOMB4,
        spu94_module.Register.vWALL,
        spu94_module.Register.vAPF1,
        spu94_module.Register.vAPF2,
        spu94_module.Register.vLIN,
        spu94_module.Register.vRIN,
    ]
    for reg in v_gain_regs:
        assert off.regs[int(reg)] == 0, (
            f"Off preset {reg.name} should be 0, got {off.regs[int(reg)]}"
        )


def test_normalized_name_aliases(spu94_module):
    """Presets with spaces in their C-side names ('Studio A', 'Half Echo',
    'Space Echo') are accessible via normalized lowercase-underscore keys."""
    p = spu94_module.presets
    assert p["studio_a"].id == 2
    assert p["studio_b"].id == 3
    assert p["studio_c"].id == 4
    assert p["half_echo"].id == 6
    assert p["space_echo"].id == 7


def test_unknown_name_raises_keyerror(spu94_module):
    import pytest

    with pytest.raises(KeyError):
        _ = spu94_module.presets["not_a_preset"]


def test_preset_matches_snapshot_after_load(spu94_module):
    """Behavioral witness: load a preset into a fresh state, tick once so
    the TICK_LATCHED writes commit, snapshot all 35 registers, and assert
    the snapshot equals ``presets[Preset.HALL].regs`` element-wise. This
    is the hard link between the .rodata table the binding imports and
    what ``spu94_load_preset`` actually writes."""
    lib = spu94_module._lib
    state_size = lib.spu94_state_size()

    # Generous buffers so any future struct growth (guarded by
    # SPU94_STATE_SIZE_MAX) still fits.
    state_buf = (ctypes.c_uint8 * 16384)()
    work_buf = (ctypes.c_uint8 * 8192)()
    state = lib.spu94_init(state_buf, 16384, work_buf, 8192)
    assert state, "spu94_init returned NULL"
    try:
        rc = lib.spu94_load_preset(state, int(spu94_module.Preset.HALL))
        assert rc == spu94_module.SPU94_OK
        # Phase 5 D-08: spu94_load_preset stages TICK_LATCHED writes into
        # the pending slot; one tick commits them. Gain registers (IMMEDIATE)
        # are already visible.
        lib.spu94_tick(state)

        out = (ctypes.c_int16 * 35)()
        lib.spu94_snapshot_registers(state, out)
        snapshot = tuple(out[i] for i in range(35))

        expected = spu94_module.presets[spu94_module.Preset.HALL].regs
        assert snapshot == expected, (
            f"Hall snapshot != presets[HALL].regs; "
            f"diffs at indices "
            f"{[i for i in range(35) if snapshot[i] != expected[i]]}"
        )
    finally:
        lib.spu94_destroy(state)
