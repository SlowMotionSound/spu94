"""PYBIND-03: spu94.Register IntEnum matches the live C library.

Register identity is reflected from the live library via
``spu94_reg_name(i)`` at import time (D-06). These tests verify the
reflection is wired correctly and that the resulting IntEnum has the
ABI-stable ordering defined by include/spu94/spu94_registers.h's
``spu94_reg_t`` enum.
"""
from enum import IntEnum


# Expected names in enum order — pinned to the Phase 2 Plan 02 decision
# (vLOUT/vROUT/mBASE first, then the reverb block 0x1DC0..0x1DFE in
# ascending hardware-offset order, with vLIN/vRIN at indices 33/34).
# If a future phase reorders the C enum, this list MUST be updated in
# lockstep or the `test_register_names_match_expected_order` test fires.
EXPECTED_NAMES = [
    "vLOUT", "vROUT", "mBASE",
    "dAPF1", "dAPF2", "vIIR",
    "vCOMB1", "vCOMB2", "vCOMB3", "vCOMB4",
    "vWALL", "vAPF1", "vAPF2",
    "mLSAME", "mRSAME",
    "mLCOMB1", "mRCOMB1", "mLCOMB2", "mRCOMB2",
    "dLSAME", "dRSAME",
    "mLDIFF", "mRDIFF",
    "mLCOMB3", "mRCOMB3", "mLCOMB4", "mRCOMB4",
    "dLDIFF", "dRDIFF",
    "mLAPF1", "mRAPF1", "mLAPF2", "mRAPF2",
    "vLIN", "vRIN",
]


def test_register_is_intenum(spu94_module):
    assert issubclass(spu94_module.Register, IntEnum)


def test_register_count_35(spu94_module):
    assert len(spu94_module.Register) == 35


def test_register_names_match_expected_order(spu94_module):
    """The reflected IntEnum yields the 35 register names in ABI order."""
    actual = [r.name for r in spu94_module.Register]
    assert actual == EXPECTED_NAMES


def test_register_values_are_0_to_34(spu94_module):
    for i, r in enumerate(spu94_module.Register):
        assert r.value == i, f"Register {r.name}: value {r.value} != index {i}"


def test_register_reflects_live_library(spu94_module):
    """Cross-check: every Register name also comes straight from
    ``_lib.spu94_reg_name(i)`` for the corresponding index."""
    for i in range(spu94_module.SPU94_REG__COUNT):
        name_bytes = spu94_module._lib.spu94_reg_name(i)
        assert name_bytes is not None, f"spu94_reg_name({i}) returned NULL"
        lib_name = name_bytes.decode("ascii")
        py_name = spu94_module.Register(i).name
        assert lib_name == py_name, (
            f"register {i}: library says {lib_name!r}, Python says {py_name!r}"
        )


def test_register_bare_names_no_prefix(spu94_module):
    """Phase 2 Plan 02 D-17: spu94_reg_name returns the bare name,
    not the SPU94_REG_* identifier."""
    assert spu94_module.Register.vIIR.name == "vIIR"
    assert spu94_module.Register.mBASE.name == "mBASE"
    # Sanity: the SPU94_REG_ prefix never appears in any enum member name.
    for r in spu94_module.Register:
        assert not r.name.startswith("SPU94_REG_"), (
            f"Register {r.name!r} unexpectedly carries the C-side prefix"
        )
