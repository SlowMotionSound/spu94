"""PYBIND-01: full C API surface reachable via ctypes.

Every public C symbol from include/spu94/spu94.h and
include/spu94/spu94_registers.h must be attached to ``spu94._lib`` with
argtypes + restype set correctly. The binding is the type contract;
these tests guard the contract against drift between Phase 5's public
C API and Plan 1's ctypes mirror.

Note: ``spu94_process`` and ``spu94_flush`` are declared here with
``ctypes.POINTER(ctypes.c_int16)`` for their audio-buffer arguments.
Plan 2 replaces those four argtype entries with ``numpy.ctypeslib.ndpointer``
to enforce the strict int16 / C-contiguous contract (D-09). Everything
else in _binding.py is stable across both plans.
"""
import ctypes

REQUIRED_FUNCTIONS = {
    # name: (restype, argtypes) — one tuple per public symbol.
    "spu94_state_size":           (ctypes.c_size_t, []),
    "spu94_init":                 (ctypes.c_void_p,
                                   [ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.c_void_p, ctypes.c_size_t]),
    "spu94_reset":                (None, [ctypes.c_void_p]),
    "spu94_destroy":              (None, [ctypes.c_void_p]),
    "spu94_tick":                 (None, [ctypes.c_void_p]),
    "spu94_get_buffer_address":   (ctypes.c_uint32, [ctypes.c_void_p]),
    "spu94_get_latency_samples":  (ctypes.c_uint32, []),
    "spu94_load_preset":          (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
    "spu94_reg_name":             (ctypes.c_char_p, [ctypes.c_int]),
    "spu94_reg_hw_offset":        (ctypes.c_uint16, [ctypes.c_int]),
    "spu94_reg_type":             (ctypes.c_int, [ctypes.c_int]),
    "spu94_snapshot_registers":   (None, [ctypes.c_void_p,
                                          ctypes.POINTER(ctypes.c_int16)]),
    "spu94_set_reg_i16":          (ctypes.c_int,
                                   [ctypes.c_void_p, ctypes.c_int, ctypes.c_int16]),
    "spu94_get_reg_i16":          (ctypes.c_int16, [ctypes.c_void_p, ctypes.c_int]),
    "spu94_get_reg_i16_pending":  (ctypes.c_int16, [ctypes.c_void_p, ctypes.c_int]),
    "spu94_set_reg_u16":          (ctypes.c_int,
                                   [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16]),
    "spu94_get_reg_u16":          (ctypes.c_uint16, [ctypes.c_void_p, ctypes.c_int]),
    "spu94_get_reg_u16_pending":  (ctypes.c_uint16, [ctypes.c_void_p, ctypes.c_int]),
    "spu94_process":              (None, [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.POINTER(ctypes.c_int16),
        ctypes.POINTER(ctypes.c_int16),
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_uint32,
    ]),
    "spu94_flush":                (None, [
        ctypes.c_void_p,
        ctypes.POINTER(ctypes.c_int16),
        ctypes.POINTER(ctypes.c_int16),
        ctypes.c_uint32,
    ]),
}


def test_all_functions_present(spu94_module):
    """Every public C function is attached to spu94._lib with the correct
    argtypes/restype declared. 20 entries total."""
    for fn_name, (restype, argtypes) in REQUIRED_FUNCTIONS.items():
        fn = getattr(spu94_module._lib, fn_name)
        assert fn.restype == restype, (
            f"{fn_name}: restype {fn.restype!r} != expected {restype!r}"
        )
        assert list(fn.argtypes) == argtypes, (
            f"{fn_name}: argtypes {list(fn.argtypes)!r} != expected {argtypes!r}"
        )


def test_lib_handle_is_cdll(spu94_module):
    assert isinstance(spu94_module._lib, ctypes.CDLL)


def test_state_size_callable(spu94_module):
    """The library reports a positive state size within the public bound."""
    size = spu94_module._lib.spu94_state_size()
    assert 0 < size <= spu94_module.SPU94_STATE_SIZE_MAX


def test_latency_samples_callable(spu94_module):
    """spu94_get_latency_samples() matches the public SPU94_LATENCY_SAMPLES constant."""
    assert spu94_module._lib.spu94_get_latency_samples() == spu94_module.SPU94_LATENCY_SAMPLES


def test_required_constants_exported(spu94_module):
    """The module re-exports every public C constant the binding mirrors."""
    assert spu94_module.SPU94_STATE_SIZE_MAX == 16384
    assert spu94_module.SPU94_STATE_ALIGN_MAX == 16
    assert spu94_module.SPU94_LATENCY_SAMPLES == 58
    assert spu94_module.SPU94_REG__COUNT == 35
    assert spu94_module.SPU94_PRESET__COUNT == 10
    assert spu94_module.SPU94_OK == 0
    assert spu94_module.SPU94_CLAMPED == 1
    assert spu94_module.SPU94_UNKNOWN_REG == 2
    assert spu94_module.SPU94_TYPE_MISMATCH == 3
