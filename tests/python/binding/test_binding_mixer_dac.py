"""Tests for mixer fader and DAC toggle Python ctypes bindings (DAC-IO-02)."""
import ctypes
import sys
from pathlib import Path

import numpy as np
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "python"))

from spu94._binding import (  # noqa: E402
    _lib,
    SPU94_STATE_SIZE_MAX,
    SPU94_WORK_BUF_MAX_BYTES,
)


@pytest.fixture
def state():
    """Create an initialized + reset spu94_state for mixer/DAC tests."""
    state_buf = (ctypes.c_ubyte * SPU94_STATE_SIZE_MAX)()
    work_buf = (ctypes.c_ubyte * SPU94_WORK_BUF_MAX_BYTES)()
    s = _lib.spu94_init(
        ctypes.cast(state_buf, ctypes.c_void_p),
        SPU94_STATE_SIZE_MAX,
        ctypes.cast(work_buf, ctypes.c_void_p),
        SPU94_WORK_BUF_MAX_BYTES,
    )
    assert s is not None and s != 0
    _lib.spu94_reset(ctypes.c_void_p(s))
    yield ctypes.c_void_p(s)
    _lib.spu94_destroy(ctypes.c_void_p(s))


class TestMixerFaders:
    def test_input_gain_default_zero(self, state):
        assert _lib.spu94_get_input_gain(state) == 0

    def test_input_gain_set_get_roundtrip(self, state):
        _lib.spu94_set_input_gain(state, 0x4000)
        assert _lib.spu94_get_input_gain(state) == 0x4000

    def test_dry_fader_set_get(self, state):
        _lib.spu94_set_dry_fader(state, 0x7FFF)
        assert _lib.spu94_get_dry_fader(state) == 0x7FFF

    def test_adpcm_fader_set_get(self, state):
        _lib.spu94_set_adpcm_fader(state, 0x3FFF)
        assert _lib.spu94_get_adpcm_fader(state) == 0x3FFF

    def test_dry_send_set_get(self, state):
        _lib.spu94_set_dry_send(state, 0x7FFF)
        assert _lib.spu94_get_dry_send(state) == 0x7FFF

    def test_adpcm_send_set_get(self, state):
        _lib.spu94_set_adpcm_send(state, 0x1000)
        assert _lib.spu94_get_adpcm_send(state) == 0x1000

    def test_reverb_fader_set_get(self, state):
        _lib.spu94_set_reverb_fader(state, 0x5000)
        assert _lib.spu94_get_reverb_fader(state) == 0x5000


class TestLatencyComp:
    def test_latency_comp_default_on(self, state):
        """After init+reset, latency comp defaults ON (per D-07)."""
        assert _lib.spu94_get_latency_comp(state) == 1

    def test_latency_comp_disable(self, state):
        _lib.spu94_set_latency_comp(state, 0)
        assert _lib.spu94_get_latency_comp(state) == 0

    def test_latency_comp_enable(self, state):
        _lib.spu94_set_latency_comp(state, 0)
        _lib.spu94_set_latency_comp(state, 1)
        assert _lib.spu94_get_latency_comp(state) == 1


class TestDacToggles:
    def test_dac_default_off(self, state):
        assert _lib.spu94_get_dac_enabled(state) == 0

    def test_dac_enable_disable(self, state):
        _lib.spu94_set_dac_enabled(state, 1)
        assert _lib.spu94_get_dac_enabled(state) == 1
        _lib.spu94_set_dac_enabled(state, 0)
        assert _lib.spu94_get_dac_enabled(state) == 0

    def test_dac_fir_default_off(self, state):
        assert _lib.spu94_get_dac_fir_enabled(state) == 0

    def test_dac_fir_toggle(self, state):
        _lib.spu94_set_dac_fir_enabled(state, 1)
        assert _lib.spu94_get_dac_fir_enabled(state) == 1

    def test_dac_noise_default_off(self, state):
        assert _lib.spu94_get_dac_noise_enabled(state) == 0

    def test_dac_noise_toggle(self, state):
        _lib.spu94_set_dac_noise_enabled(state, 1)
        assert _lib.spu94_get_dac_noise_enabled(state) == 1

    def test_dac_master_gate(self, state):
        """DAC master toggle gates the DAC section processing.

        Enable DAC master + FIR + noise, process audio. Disable DAC master.
        Sub-toggles should still read enabled, but output differs because
        DAC section is bypassed when master is off.
        """
        num_samples = 128
        # Load a preset so reverb produces non-trivial output
        _lib.spu94_load_preset(state, 0)

        # Set up mixer faders for audible output
        _lib.spu94_set_input_gain(state, 0x7FFF)
        _lib.spu94_set_dry_fader(state, 0x7FFF)
        _lib.spu94_set_dry_send(state, 0x7FFF)
        _lib.spu94_set_reverb_fader(state, 0x7FFF)

        # Enable full DAC chain
        _lib.spu94_set_dac_enabled(state, 1)
        _lib.spu94_set_dac_fir_enabled(state, 1)
        _lib.spu94_set_dac_noise_enabled(state, 1)

        # Feed a simple impulse
        lin = np.zeros(num_samples, dtype=np.int16)
        rin = np.zeros(num_samples, dtype=np.int16)
        lin[0] = 10000
        rin[0] = 10000
        lout_dac_on = np.zeros(num_samples, dtype=np.int16)
        rout_dac_on = np.zeros(num_samples, dtype=np.int16)
        _lib.spu94_process(state, lin, rin, lout_dac_on, rout_dac_on,
                           num_samples)

        # Reset and run again with DAC master off
        _lib.spu94_reset(state)
        _lib.spu94_load_preset(state, 0)
        _lib.spu94_set_input_gain(state, 0x7FFF)
        _lib.spu94_set_dry_fader(state, 0x7FFF)
        _lib.spu94_set_dry_send(state, 0x7FFF)
        _lib.spu94_set_reverb_fader(state, 0x7FFF)

        # DAC master OFF, sub-toggles still ON
        _lib.spu94_set_dac_enabled(state, 0)
        _lib.spu94_set_dac_fir_enabled(state, 1)
        _lib.spu94_set_dac_noise_enabled(state, 1)

        lout_dac_off = np.zeros(num_samples, dtype=np.int16)
        rout_dac_off = np.zeros(num_samples, dtype=np.int16)
        _lib.spu94_process(state, lin, rin, lout_dac_off, rout_dac_off,
                           num_samples)

        # Sub-toggles should still read as enabled
        assert _lib.spu94_get_dac_fir_enabled(state) == 1
        assert _lib.spu94_get_dac_noise_enabled(state) == 1

        # Outputs should differ (DAC on adds FIR filtering + noise)
        assert not np.array_equal(lout_dac_on, lout_dac_off), \
            "DAC master gate: outputs should differ when DAC is on vs off"
