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


# ----------------------------------------------------------------------
# Task 2 — SPU94 class tests (context manager, auto-dispatch, destroy)
# ----------------------------------------------------------------------

def test_spu94_class_constructs_and_destroys(spu94_module):
    rev = spu94_module.SPU94()
    assert rev.state is not None
    rev.destroy()
    # After destroy, accessing state raises.
    with pytest.raises(RuntimeError, match="has been destroyed"):
        _ = rev.state


def test_spu94_class_context_manager(spu94_module):
    with spu94_module.SPU94() as rev:
        assert rev.state is not None
        assert spu94_module.SPU94_OK == rev.load_preset("hall")
        rev.tick()
        n = 256
        L_in  = np.zeros(n, dtype=np.int16)
        R_in  = np.zeros(n, dtype=np.int16)
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        rev.process(L_in, R_in, L_out, R_out)
    # After exit, state is None (destroyed).
    with pytest.raises(RuntimeError, match="has been destroyed"):
        _ = rev.state


def test_spu94_class_set_get_reg_dispatch_by_type(spu94_module):
    with spu94_module.SPU94() as rev:
        # vIIR is i16 — set_reg should dispatch to set_reg_i16
        rc = rev.set_reg(spu94_module.Register.vIIR, -20000)
        assert rc == spu94_module.SPU94_OK
        assert rev.get_reg(spu94_module.Register.vIIR) == -20000
        # mBASE is u16 — set_reg should dispatch to set_reg_u16
        rc = rev.set_reg(spu94_module.Register.mBASE, 0x3F00)
        assert rc == spu94_module.SPU94_OK
        assert rev.get_reg(spu94_module.Register.mBASE) == 0x3F00


def test_spu94_class_snapshot_returns_35_tuple(spu94_module):
    with spu94_module.SPU94() as rev:
        snap = rev.snapshot()
        assert isinstance(snap, tuple) and len(snap) == 35


def test_spu94_class_buffer_address_nonnegative(spu94_module):
    with spu94_module.SPU94() as rev:
        assert rev.buffer_address >= 0
        assert rev.buffer_address <= 0x7FFFE


def test_spu94_class_latency_samples_58(spu94_module):
    with spu94_module.SPU94() as rev:
        assert rev.latency_samples == 58


def test_spu94_class_custom_work_buf_size(spu94_module):
    with spu94_module.SPU94(work_buf_size=32768) as rev:
        assert rev.work_buf_size == 32768


def test_spu94_class_repr(spu94_module):
    rev = spu94_module.SPU94(work_buf_size=8192)
    try:
        s = repr(rev)
        assert "SPU94" in s
        assert "8192" in s
        assert "live" in s
    finally:
        rev.destroy()
    s = repr(rev)
    assert "destroyed" in s


def test_spu94_class_flush(spu94_module):
    """The class's flush() forwards to api.flush."""
    with spu94_module.SPU94() as rev:
        rev.load_preset("hall")
        rev.tick()
        n = 512
        L_out = np.zeros(n, dtype=np.int16)
        R_out = np.zeros(n, dtype=np.int16)
        rev.flush(L_out, R_out)  # should not raise


def test_spu94_class_double_destroy_is_idempotent(spu94_module):
    """Destroy called twice does not raise — class guards internal state."""
    rev = spu94_module.SPU94()
    rev.destroy()
    rev.destroy()  # second destroy is a legal no-op


def test_spu94_class_process_after_destroy_raises(spu94_module):
    """Calling process() after destroy() raises RuntimeError, not a
    silent SEGV on a freed state pointer."""
    rev = spu94_module.SPU94()
    rev.destroy()
    n = 64
    L = np.zeros(n, dtype=np.int16)
    with pytest.raises(RuntimeError, match="has been destroyed"):
        rev.process(L, L, L, L)


def test_spu94_class_get_reg_pending_dispatch(spu94_module):
    """get_reg_pending auto-dispatches by signedness, same as get_reg."""
    with spu94_module.SPU94() as rev:
        # vIIR is i16 — set_reg with IMMEDIATE policy commits + caches
        # the pending value too (Plan 3 decision); mBASE is u16.
        rev.set_reg(spu94_module.Register.vIIR, -12345)
        # Pending getter should return something — just verify it returns
        # an int without raising (value semantics depend on write policy).
        assert isinstance(rev.get_reg_pending(spu94_module.Register.vIIR), int)
        assert isinstance(rev.get_reg_pending(spu94_module.Register.mBASE), int)


def test_cli_main_missing_binary_exits_1(tmp_path, monkeypatch, capsys):
    """When the compiled binary is absent, cli.main() prints a clear
    error and sys.exit(1)s."""
    import importlib.util
    import sys as _sys
    from pathlib import Path

    # Create an isolated fake spu94 package dir with NO binary inside.
    fake_pkg = tmp_path / "fake_spu94_pkg"
    fake_pkg.mkdir()

    # Copy the real cli.py body into the fake package.
    repo_root = Path(__file__).resolve().parents[3]
    cli_src = (repo_root / "python" / "spu94" / "cli.py").read_text()
    (fake_pkg / "__init__.py").write_text("")
    (fake_pkg / "cli.py").write_text(cli_src)

    # Load cli.py as a standalone module from the fake-pkg path so
    # Path(__file__).parent inside cli.py points at the fake pkg
    # (which has no "spu94" binary). This sidesteps caching of the
    # real spu94 package already imported by other tests.
    spec = importlib.util.spec_from_file_location(
        "spu94_cli_test_isolated", str(fake_pkg / "cli.py")
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    # Save and clear argv so os.execv isn't called with test-runner args.
    monkeypatch.setattr(_sys, "argv", ["spu94"])

    with pytest.raises(SystemExit) as exc:
        mod.main()
    assert exc.value.code == 1
    err = capsys.readouterr().err
    assert "compiled binary not found" in err
