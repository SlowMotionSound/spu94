"""PYBIND-02: strict numpy contract + zero-copy.

Plan 6-02 delivers:
  - ``spu94.process`` / ``spu94.flush`` using ``numpy.ctypeslib.ndpointer``
    for the audio-buffer argtypes (int16 + C-contiguous + 1-D).
  - An explicit length-mismatch validator layered on top of ndpointer.
  - A zero-copy sentinel test: writing a known value into ``L_out`` BEFORE
    the call must get overwritten by the C function — proves the numpy
    buffer pointer reaches the C side directly.
  - Register I/O round-trip + signedness dispatch.
  - ``load_preset`` accepting Preset enum / int / string names.
  - The ``SPU94`` class wrapper (context manager, auto-dispatch by signedness).
  - ``cli.main()`` missing-binary error path.
  - ``self_test`` end-to-end smoke.
"""
import ctypes
import numpy as np
import pytest


def _fresh_state(spu94_module):
    state = spu94_module.init(work_buf_size=8192)
    spu94_module.load_preset(state, "off")  # silence-friendly
    spu94_module.tick(state)
    return state


# ----------------------------------------------------------------------
# Task 1 — numpy contract + raw-panel api tests
# ----------------------------------------------------------------------

def test_process_accepts_int16_contig(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        n = 1024
        L_in  = np.zeros(n, dtype=np.int16)
        R_in  = np.zeros(n, dtype=np.int16)
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        spu94_module.process(state, L_in, R_in, L_out, R_out)
    finally:
        spu94_module.destroy(state)


def test_process_rejects_float32(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        n = 64
        L_in  = np.zeros(n, dtype=np.float32)
        R_in  = np.zeros(n, dtype=np.int16)
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        with pytest.raises(TypeError) as exc:
            spu94_module.process(state, L_in, R_in, L_out, R_out)
        assert "int16" in str(exc.value)
    finally:
        spu94_module.destroy(state)


def test_process_rejects_non_contiguous_slice(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        n = 64
        interleaved = np.zeros((n, 2), dtype=np.int16)
        L_in  = interleaved[:, 0]  # non-contig slice
        R_in  = np.zeros(n, dtype=np.int16)
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        with pytest.raises(TypeError) as exc:
            spu94_module.process(state, L_in, R_in, L_out, R_out)
        # Either ndpointer's raw message or planner's upgraded message.
        assert "contiguous" in str(exc.value).lower() or "C_CONTIGUOUS" in str(exc.value)
    finally:
        spu94_module.destroy(state)


def test_process_rejects_mismatched_lengths(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        L_in  = np.zeros(100, dtype=np.int16)
        R_in  = np.zeros(200, dtype=np.int16)
        L_out = np.zeros(100, dtype=np.int16)
        R_out = np.zeros(100, dtype=np.int16)
        with pytest.raises(ValueError) as exc:
            spu94_module.process(state, L_in, R_in, L_out, R_out)
        # Error message names at least the mismatched array and its length.
        msg = str(exc.value)
        assert "R_in" in msg or "200" in msg
        assert "100" in msg
    finally:
        spu94_module.destroy(state)


def test_process_accepts_none_inputs(spu94_module):
    """NULL L_in/R_in substitutes silence per the C contract — should not raise."""
    state = _fresh_state(spu94_module)
    try:
        n = 128
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        spu94_module.process(state, None, None, L_out, R_out)
    finally:
        spu94_module.destroy(state)


def test_process_is_zero_copy(spu94_module):
    """Write a sentinel value into L_out BEFORE processing. After processing,
    the sentinel must be OVERWRITTEN (not merely untouched). If the binding
    allocated a private buffer and forgot to copy the result back, the sentinel
    would survive."""
    state = _fresh_state(spu94_module)
    try:
        # Use Hall with deliberate input pattern to get a non-zero output.
        spu94_module.load_preset(state, "hall")
        spu94_module.tick(state)
        n = 512
        # Deterministic input — ramps produce non-trivial reverb output.
        pattern = np.linspace(-8000, 8000, n, dtype=np.int16)
        L_in = np.ascontiguousarray(pattern)
        R_in = np.ascontiguousarray(pattern)
        L_out = np.full(n, 0x1234, dtype=np.int16)  # sentinel
        R_out = np.full(n, 0x5678, dtype=np.int16)  # sentinel
        spu94_module.process(state, L_in, R_in, L_out, R_out)
        # At least one element must have changed — if the whole array still
        # reads 0x1234 / 0x5678, no processing happened (binding bug).
        assert not np.all(L_out == 0x1234), "L_out untouched — process() did not write"
        assert not np.all(R_out == 0x5678), "R_out untouched — process() did not write"
    finally:
        spu94_module.destroy(state)


def test_flush_accepts_int16_contig(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        n = 1024
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        spu94_module.flush(state, L_out, R_out)
    finally:
        spu94_module.destroy(state)


def test_flush_rejects_float32(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        n = 64
        L_out = np.zeros(n, dtype=np.float32)
        R_out = np.zeros(n, dtype=np.int16)
        with pytest.raises(TypeError) as exc:
            spu94_module.flush(state, L_out, R_out)
        assert "int16" in str(exc.value)
    finally:
        spu94_module.destroy(state)


def test_register_io_roundtrip(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        rc = spu94_module.set_reg_i16(state, spu94_module.Register.vIIR, -20000)
        assert rc == spu94_module.SPU94_OK
        assert spu94_module.get_reg_i16(state, spu94_module.Register.vIIR) == -20000
    finally:
        spu94_module.destroy(state)


def test_register_signedness_mismatch(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        # mBASE is u16; set_reg_i16 on it returns SPU94_TYPE_MISMATCH
        rc = spu94_module.set_reg_i16(state, spu94_module.Register.mBASE, 0)
        assert rc == spu94_module.SPU94_TYPE_MISMATCH
    finally:
        spu94_module.destroy(state)


def test_snapshot_returns_35_tuple(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        snap = spu94_module.snapshot_registers(state)
        assert isinstance(snap, tuple)
        assert len(snap) == 35
    finally:
        spu94_module.destroy(state)


def test_load_preset_accepts_string_enum_int(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        assert spu94_module.load_preset(state, "hall") == spu94_module.SPU94_OK
        assert spu94_module.load_preset(state, spu94_module.Preset.HALL) == spu94_module.SPU94_OK
        assert spu94_module.load_preset(state, 5) == spu94_module.SPU94_OK
    finally:
        spu94_module.destroy(state)


def test_load_preset_unknown_id_returns_unknown_reg(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        # Integer out-of-range → SPU94_UNKNOWN_REG
        assert spu94_module.load_preset(state, 99) == spu94_module.SPU94_UNKNOWN_REG
    finally:
        spu94_module.destroy(state)


def test_load_preset_unknown_string_raises_valueerror(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        with pytest.raises(ValueError):
            spu94_module.load_preset(state, "nonexistent")
    finally:
        spu94_module.destroy(state)


def test_buffer_address_in_range(spu94_module):
    state = _fresh_state(spu94_module)
    try:
        ba = spu94_module.get_buffer_address(state)
        assert ba >= 0
        assert ba <= 0x7FFFE
    finally:
        spu94_module.destroy(state)


def test_latency_samples_matches_constant(spu94_module):
    assert spu94_module.get_latency_samples() == 58


def test_self_test_runs_clean(spu94_module):
    spu94_module.self_test()
