"""Tests for ADPCM + VAG Python ctypes bindings (ADPCM-IO-05, D-09, D-10)."""
import ctypes

import pytest

from spu94._binding import (
    _lib,
    _Spu94AdpcmState,
    _Spu94VagHeader,
    SPU94_ADPCM_BLOCK_SAMPLES,
    SPU94_ADPCM_BLOCK_BYTES,
    SPU94_VAG_HEADER_BYTES,
    SPU94_STATE_SIZE_MAX,
    SPU94_STATE_ALIGN_MAX,
    SPU94_WORK_BUF_MAX_BYTES,
)


class TestAdpcmStateStruct:
    def test_sizeof(self):
        """spu94_adpcm_state is 4 bytes: two int16."""
        assert ctypes.sizeof(_Spu94AdpcmState) == 4

    def test_zero_init(self):
        st = _Spu94AdpcmState(0, 0)
        assert st.old == 0 and st.older == 0


class TestAdpcmDecodeBlock:
    def test_all_zero_block(self):
        """Decoding an all-zero block produces 28 zero samples."""
        state = _Spu94AdpcmState(0, 0)
        block = (ctypes.c_uint8 * SPU94_ADPCM_BLOCK_BYTES)(*([0] * 16))
        out = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)()
        flag = _lib.spu94_adpcm_decode_block(
            ctypes.byref(state), block, out)
        assert flag == 0  # flag byte is block[1] = 0
        for i in range(SPU94_ADPCM_BLOCK_SAMPLES):
            assert out[i] == 0

    def test_returns_flag_byte(self):
        """Decode returns the flag byte from block[1]."""
        state = _Spu94AdpcmState(0, 0)
        block = (ctypes.c_uint8 * SPU94_ADPCM_BLOCK_BYTES)(*([0] * 16))
        block[1] = 0x01  # end flag
        out = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)()
        flag = _lib.spu94_adpcm_decode_block(
            ctypes.byref(state), block, out)
        assert flag == 0x01


class TestAdpcmEncodeBlock:
    def test_encode_silence(self):
        """Encoding 28 zero samples produces a block that decodes to zeros."""
        enc_state = _Spu94AdpcmState(0, 0)
        samples = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)(*([0] * 28))
        block = (ctypes.c_uint8 * SPU94_ADPCM_BLOCK_BYTES)()
        _lib.spu94_adpcm_encode_block(
            ctypes.byref(enc_state), samples, 0, block)
        # Decode and verify round-trip
        dec_state = _Spu94AdpcmState(0, 0)
        decoded = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)()
        _lib.spu94_adpcm_decode_block(
            ctypes.byref(dec_state), block, decoded)
        for i in range(SPU94_ADPCM_BLOCK_SAMPLES):
            assert decoded[i] == 0

    def test_encode_decode_roundtrip_nonzero(self):
        """Encode-decode roundtrip of a non-zero signal is deterministic."""
        # Simple ramp signal
        input_samples = list(range(-14, 14))
        enc_state = _Spu94AdpcmState(0, 0)
        samples_in = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)(*input_samples)
        block = (ctypes.c_uint8 * SPU94_ADPCM_BLOCK_BYTES)()
        _lib.spu94_adpcm_encode_block(
            ctypes.byref(enc_state), samples_in, 0, block)
        # Decode
        dec_state = _Spu94AdpcmState(0, 0)
        decoded = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)()
        _lib.spu94_adpcm_decode_block(
            ctypes.byref(dec_state), block, decoded)
        # Run again -- must produce identical output (deterministic)
        enc_state2 = _Spu94AdpcmState(0, 0)
        block2 = (ctypes.c_uint8 * SPU94_ADPCM_BLOCK_BYTES)()
        _lib.spu94_adpcm_encode_block(
            ctypes.byref(enc_state2), samples_in, 0, block2)
        dec_state2 = _Spu94AdpcmState(0, 0)
        decoded2 = (ctypes.c_int16 * SPU94_ADPCM_BLOCK_SAMPLES)()
        _lib.spu94_adpcm_decode_block(
            ctypes.byref(dec_state2), block2, decoded2)
        for i in range(SPU94_ADPCM_BLOCK_SAMPLES):
            assert decoded[i] == decoded2[i], f"sample {i} mismatch"


class TestAdpcmToggle:
    @pytest.fixture
    def spu_state(self):
        """Create an initialized spu94_state for toggle tests."""
        state_buf = (ctypes.c_ubyte * SPU94_STATE_SIZE_MAX)()
        work_buf = (ctypes.c_ubyte * SPU94_WORK_BUF_MAX_BYTES)()
        state = _lib.spu94_init(
            ctypes.cast(state_buf, ctypes.c_void_p),
            SPU94_STATE_SIZE_MAX,
            ctypes.cast(work_buf, ctypes.c_void_p),
            SPU94_WORK_BUF_MAX_BYTES)
        assert state is not None and state != 0
        yield state
        _lib.spu94_destroy(ctypes.c_void_p(state))

    def test_adpcm_default_off(self, spu_state):
        assert _lib.spu94_get_adpcm_enabled(ctypes.c_void_p(spu_state)) == 0

    def test_adpcm_enable_disable(self, spu_state):
        _lib.spu94_set_adpcm_enabled(ctypes.c_void_p(spu_state), 1)
        assert _lib.spu94_get_adpcm_enabled(ctypes.c_void_p(spu_state)) == 1
        _lib.spu94_set_adpcm_enabled(ctypes.c_void_p(spu_state), 0)
        assert _lib.spu94_get_adpcm_enabled(ctypes.c_void_p(spu_state)) == 0

    def test_total_latency_with_adpcm(self, spu_state):
        assert _lib.spu94_get_total_latency_samples(
            ctypes.c_void_p(spu_state)) == 58  # off
        _lib.spu94_set_adpcm_enabled(ctypes.c_void_p(spu_state), 1)
        assert _lib.spu94_get_total_latency_samples(
            ctypes.c_void_p(spu_state)) == 86  # 58 + 28


class TestVagHeader:
    def test_write_read_roundtrip(self):
        """Write a VAG v2 header, then read it back."""
        buf = (ctypes.c_uint8 * SPU94_VAG_HEADER_BYTES)()
        _lib.spu94_vag_write_header(buf, 448, 44100, b"testname")
        hdr = _Spu94VagHeader()
        rc = _lib.spu94_vag_read_header(buf, ctypes.byref(hdr))
        assert rc == 0
        assert hdr.version == 2
        assert hdr.data_size == 448
        assert hdr.sample_rate == 44100
        assert hdr.name[:8] == b"testname"

    def test_bad_magic_rejected(self):
        """Reader rejects non-VAGp magic."""
        buf = (ctypes.c_uint8 * SPU94_VAG_HEADER_BYTES)(*([0] * 48))
        buf[0] = ord('X')
        hdr = _Spu94VagHeader()
        rc = _lib.spu94_vag_read_header(buf, ctypes.byref(hdr))
        assert rc == -1

    def test_null_name_write(self):
        """Write with NULL name does not crash."""
        buf = (ctypes.c_uint8 * SPU94_VAG_HEADER_BYTES)()
        _lib.spu94_vag_write_header(buf, 160, 22050, None)
        hdr = _Spu94VagHeader()
        rc = _lib.spu94_vag_read_header(buf, ctypes.byref(hdr))
        assert rc == 0
        assert hdr.sample_rate == 22050
