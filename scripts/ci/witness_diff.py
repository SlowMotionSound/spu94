#!/usr/bin/env python3
"""scripts/ci/witness_diff.py — Phase 7 TEST-03 witness-diff harness.

Measures SPU-94 vs lv2-psx-reverb divergence across 10 presets × 5 standard
inputs = 50 pairs. Writes a structured `witness_report.json` with per-pair
split-band aligned-RMS divergence in dBFS.

Discipline:
  - D-05 (pin): lv2-psx-reverb is cloned + built fresh at a pinned commit SHA
    by scripts/ci/witness_diff_build.sh BEFORE this harness runs. The pin
    (commit 424e1e8ee7f780106b005011b036386513c61db3) is recorded in every
    witness_report.json row for traceability.
  - D-06 (measurement-only): this harness prints numbers and writes JSON. It
    does NOT gate pass/fail on divergence magnitude. Exit 0 on successful
    render + compute; exit 1 only on infrastructure failure. A tolerance-
    policy ADR is the deferred follow-up that will add a gate on top of
    these numbers.
  - D-07 (split-band): divergence measured separately in low band (≤ 10 kHz,
    where lv2 is a valid witness per ADR-Phase-4-I) and high band (> 10 kHz,
    informational only — lv2 omits the 39-tap half-band FIR by design).
  - D-08 (binary witness only): lv2-psx-reverb source code is never read as
    primary material. Port layout is discovered from `lv2info` output
    (produced by witness_diff_build.sh). The in-process LV2 host below is
    written against the public LV2 C API headers (/usr/include/lv2/) and
    the LV2 spec — not against lv2-psx-reverb's .c file.

LV2 host mechanism:
  The Debian/Ubuntu-shipped `lv2apply` (lilv-utils 0.24.26) does NOT
  provide the `urid:map` host feature that lv2-psx-reverb requires (the
  plugin's instantiate() fails at feature-query and segfaults). Rather
  than add a new apt dep (python3-lilv) OR write a minimal C host, we
  implement the host in-process via ctypes: dlopen the plugin .so, call
  lv2_descriptor(0), provide urid:map + log features, connect float32
  audio buffers + control ports, activate → run(N) → deactivate → cleanup.

  This is still D-08-compliant: the host code is written against the
  public LV2 C API (LV2_Descriptor shape, LV2_URID_Map shape), not
  against lv2-psx-reverb's implementation. The plugin is treated as an
  opaque binary satisfying the LV2 plugin contract.

Port layout (Case A from RESEARCH.md Open Question 1; discovered at build):
  Port 0: wet (control, dB; default 0)
  Port 1: dry (control, dB; default 0)
  Port 2: preset (control, integer 0..9; scale points below)
  Port 3: master (control, dB; default 0)
  Port 4: main_in_0  (audio, L in)
  Port 5: main_in_1  (audio, R in)
  Port 6: main_out_0 (audio, L out)
  Port 7: main_out_1 (audio, R out)

  Scale points for port 2 (preset):
    0 = Room           SPU-94: room
    1 = Studio Small   SPU-94: studio_a
    2 = Studio Medium  SPU-94: studio_b
    3 = Studio Large   SPU-94: studio_c
    4 = Hall           SPU-94: hall
    5 = Half Echo      SPU-94: half_echo
    6 = Space Echo     SPU-94: space_echo
    7 = Chaos Echo     SPU-94: echo        (name divergence)
    8 = Delay          SPU-94: delay
    9 = Off            SPU-94: off

Input set (reused from Plan 07-02; same seed/params):
  44100 Hz int16 stereo, 2.0 s + 2.0 s trailing silence = 4.0 s total
  (Pitfall 6: capture the reverb tail on both sides). Five inputs:
  impulse / silence / white_noise / sine_1khz / sweep.

Output: .artifacts/witness_report.json (50 entries, each with keys:
  preset, input, alignment_lag_samples, low_band_diff_dbfs,
  high_band_diff_dbfs, lv2_commit, n_samples_compared).
"""
import argparse
import ctypes
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, chirp, correlate, sosfiltfilt

# ----------------------------------------------------------------------------
# Constants (locked from Plan 07-02 standard input set)
# ----------------------------------------------------------------------------
FS = 44100
DURATION_SEC = 2.0
TAIL_PAD_SEC = 2.0
N_ACTIVE = int(FS * DURATION_SEC)
N_PAD = int(FS * TAIL_PAD_SEC)
N_TOTAL = N_ACTIVE + N_PAD
AMP = 16000
NOISE_SEED = 0x1094_DADA
SWEEP_F0 = 20.0
SWEEP_F1 = 20000.0
SWEEP_METHOD = "logarithmic"

# D-07 split-band divergence
SPLIT_HZ = 10000
BUTTER_ORDER = 8

# D-05 pin recorded per-row
LV2_COMMIT_PIN = "424e1e8ee7f780106b005011b036386513c61db3"

# Block size for lv2 run() loop
LV2_BLOCK = 1024

# T-07-03-C closed allowlist (same as regenerate_goldens.py)
PRESETS = [
    "off", "room", "studio_a", "studio_b", "studio_c",
    "hall", "half_echo", "space_echo", "echo", "delay",
]
INPUTS = ["impulse", "white_noise", "sine_1khz", "silence", "sweep"]

# SPU-94 preset name → lv2-psx-reverb integer preset port value.
SPU94_TO_LV2_PRESET_ID = {
    "off":        9,
    "room":       0,
    "studio_a":   1,
    "studio_b":   2,
    "studio_c":   3,
    "hall":       4,
    "half_echo":  5,
    "space_echo": 6,
    "echo":       7,  # lv2 name: "Chaos Echo"
    "delay":      8,
}

# lv2-psx-reverb port indices (discovered from lv2info; constant for pinned SHA)
LV2_PORT_WET       = 0
LV2_PORT_DRY       = 1
LV2_PORT_PRESET    = 2
LV2_PORT_MASTER    = 3
LV2_PORT_AUDIO_L_IN  = 4
LV2_PORT_AUDIO_R_IN  = 5
LV2_PORT_AUDIO_L_OUT = 6
LV2_PORT_AUDIO_R_OUT = 7
LV2_NUM_PORTS = 8

# SOS filter designs (built once at module scope)
_SOS_LP = butter(BUTTER_ORDER, SPLIT_HZ / (FS / 2), btype="low",  output="sos")
_SOS_HP = butter(BUTTER_ORDER, SPLIT_HZ / (FS / 2), btype="high", output="sos")

# Paths emitted by witness_diff_build.sh
LV2_PATH_FILE = Path(".artifacts/lv2-psx-reverb/.LV2_PATH")
LV2_URI_FILE  = Path(".artifacts/lv2-psx-reverb/.LV2_URI")
REPORT_PATH   = Path(".artifacts/witness_report.json")

# LV2 feature URIs
LV2_URID__map  = b"http://lv2plug.in/ns/ext/urid#map"
LV2_URID__unmap = b"http://lv2plug.in/ns/ext/urid#unmap"
LV2_LOG__log   = b"http://lv2plug.in/ns/ext/log#log"


# ----------------------------------------------------------------------------
# ctypes LV2 host
# ----------------------------------------------------------------------------
class LV2_Feature(ctypes.Structure):
    _fields_ = [
        ("URI",  ctypes.c_char_p),
        ("data", ctypes.c_void_p),
    ]


class LV2_Descriptor(ctypes.Structure):
    pass  # forward; fields set below with self-pointer


# Function-pointer types (defined once; referenced by _fields_ below).
_INSTANTIATE_FN  = ctypes.CFUNCTYPE(ctypes.c_void_p,
                                    ctypes.POINTER(LV2_Descriptor),
                                    ctypes.c_double,
                                    ctypes.c_char_p,
                                    ctypes.POINTER(ctypes.POINTER(LV2_Feature)))
_CONNECT_PORT_FN = ctypes.CFUNCTYPE(None,
                                    ctypes.c_void_p,
                                    ctypes.c_uint32,
                                    ctypes.c_void_p)
_ACTIVATE_FN     = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_RUN_FN          = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint32)
_DEACTIVATE_FN   = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_CLEANUP_FN      = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
_EXTENSION_FN    = ctypes.CFUNCTYPE(ctypes.c_void_p, ctypes.c_char_p)

LV2_Descriptor._fields_ = [
    ("URI",            ctypes.c_char_p),
    ("instantiate",    _INSTANTIATE_FN),
    ("connect_port",   _CONNECT_PORT_FN),
    ("activate",       _ACTIVATE_FN),
    ("run",            _RUN_FN),
    ("deactivate",     _DEACTIVATE_FN),
    ("cleanup",        _CLEANUP_FN),
    ("extension_data", _EXTENSION_FN),
]


# URID map: assign monotonically increasing LV2_URID ints to URI strings.
class URIDMapState:
    def __init__(self):
        self.uri_to_id: dict[bytes, int] = {}
        self.id_to_uri: dict[int, bytes] = {}
        self.next_id = 1
        # Keep refs to c_char_p so they don't get GC'd.
        self._pins: list = []

    def map(self, uri: bytes) -> int:
        if uri in self.uri_to_id:
            return self.uri_to_id[uri]
        new_id = self.next_id
        self.next_id += 1
        self.uri_to_id[uri] = new_id
        self.id_to_uri[new_id] = uri
        return new_id

    def unmap(self, urid: int) -> bytes:
        return self.id_to_uri.get(urid, b"")


# LV2_URID_Map struct:
#   typedef struct { LV2_URID_Map_Handle handle;
#                    LV2_URID (*map)(LV2_URID_Map_Handle handle, const char* uri); }
_URID_MAP_FN = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_void_p,
                                ctypes.c_char_p)
_URID_UNMAP_FN = ctypes.CFUNCTYPE(ctypes.c_char_p, ctypes.c_void_p,
                                  ctypes.c_uint32)


class LV2_URID_Map(ctypes.Structure):
    _fields_ = [
        ("handle", ctypes.c_void_p),
        ("map",    _URID_MAP_FN),
    ]


class LV2_URID_Unmap(ctypes.Structure):
    _fields_ = [
        ("handle", ctypes.c_void_p),
        ("unmap",  _URID_UNMAP_FN),
    ]


# log->printf(handle, type_urid, fmt, ...)
_LOG_PRINTF = ctypes.CFUNCTYPE(ctypes.c_int,
                               ctypes.c_void_p,
                               ctypes.c_uint32,
                               ctypes.c_char_p)
# log->vprintf(handle, type_urid, fmt, va_list) — va_list is opaque; receive
# as c_void_p and ignore (we just return 0). The plugin uses vprintf for
# error messages which we'll silently drop.
_LOG_VPRINTF = ctypes.CFUNCTYPE(ctypes.c_int,
                                ctypes.c_void_p,
                                ctypes.c_uint32,
                                ctypes.c_char_p,
                                ctypes.c_void_p)


class LV2_Log_Log(ctypes.Structure):
    _fields_ = [
        ("handle",  ctypes.c_void_p),
        ("printf",  _LOG_PRINTF),
        ("vprintf", _LOG_VPRINTF),
    ]


class LV2Plugin:
    """Minimal in-process LV2 host. Loads one plugin; provides urid:map + log."""

    def __init__(self, bundle_path: Path, sample_rate: float = FS):
        self._bundle_path = bundle_path
        self._sample_rate = sample_rate
        so_path = bundle_path / "psx-reverb.so"
        if not so_path.exists():
            raise RuntimeError(f"lv2 bundle .so not found at {so_path}")
        self._lib = ctypes.CDLL(str(so_path))
        self._lib.lv2_descriptor.restype  = ctypes.POINTER(LV2_Descriptor)
        self._lib.lv2_descriptor.argtypes = [ctypes.c_uint32]
        desc_ptr = self._lib.lv2_descriptor(0)
        if not desc_ptr:
            raise RuntimeError("lv2_descriptor(0) returned NULL")
        self._desc = desc_ptr.contents

        # Provide urid:map + urid:unmap + log:log features.
        self._urid_state = URIDMapState()

        # Callbacks must be stored on self to survive GC.
        self._map_cb         = _URID_MAP_FN(self._urid_map_cb)
        self._unmap_cb       = _URID_UNMAP_FN(self._urid_unmap_cb)
        self._log_printf_cb  = _LOG_PRINTF(self._log_printf_cb_impl)
        self._log_vprintf_cb = _LOG_VPRINTF(self._log_vprintf_cb_impl)

        self._urid_map_struct = LV2_URID_Map(handle=0, map=self._map_cb)
        self._urid_unmap_struct = LV2_URID_Unmap(handle=0, unmap=self._unmap_cb)
        self._log_struct = LV2_Log_Log(handle=0,
                                       printf=self._log_printf_cb,
                                       vprintf=self._log_vprintf_cb)

        # Feature array (NULL-terminated).
        self._features = (ctypes.POINTER(LV2_Feature) * 4)()
        self._f_map = LV2_Feature(LV2_URID__map,
                                  ctypes.cast(ctypes.pointer(self._urid_map_struct),
                                              ctypes.c_void_p))
        self._f_unmap = LV2_Feature(LV2_URID__unmap,
                                    ctypes.cast(ctypes.pointer(self._urid_unmap_struct),
                                                ctypes.c_void_p))
        self._f_log = LV2_Feature(LV2_LOG__log,
                                  ctypes.cast(ctypes.pointer(self._log_struct),
                                              ctypes.c_void_p))
        self._features[0] = ctypes.pointer(self._f_map)
        self._features[1] = ctypes.pointer(self._f_unmap)
        self._features[2] = ctypes.pointer(self._f_log)
        self._features[3] = ctypes.POINTER(LV2_Feature)()

        bundle_str = str(bundle_path).encode() + b"/"
        instantiate = self._desc.instantiate
        self._instance = instantiate(desc_ptr,
                                     ctypes.c_double(sample_rate),
                                     bundle_str,
                                     self._features)
        if not self._instance:
            raise RuntimeError("lv2 instantiate() returned NULL")

    def _urid_map_cb(self, _handle, uri):  # uri is bytes
        return self._urid_state.map(bytes(uri))

    def _urid_unmap_cb(self, _handle, urid):
        return self._urid_state.unmap(int(urid))

    def _log_printf_cb_impl(self, _handle, _type, _fmt):
        return 0  # silently drop plugin log messages

    def _log_vprintf_cb_impl(self, _handle, _type, _fmt, _args):
        return 0  # silently drop

    def connect_port(self, port_idx: int, ptr_or_addr):
        # ptr_or_addr may be a numpy .ctypes.data (int) or a ctypes pointer.
        if hasattr(ptr_or_addr, "ctypes"):
            addr = ptr_or_addr.ctypes.data
        else:
            addr = int(ptr_or_addr) if not isinstance(ptr_or_addr, int) else ptr_or_addr
        self._desc.connect_port(self._instance, port_idx, ctypes.c_void_p(addr))

    def activate(self):
        if self._desc.activate:
            self._desc.activate(self._instance)

    def run(self, n_samples: int):
        self._desc.run(self._instance, n_samples)

    def deactivate(self):
        if self._desc.deactivate:
            self._desc.deactivate(self._instance)

    def cleanup(self):
        if self._desc.cleanup:
            self._desc.cleanup(self._instance)
        self._instance = None


def lv2_render(bundle_path: Path, preset_name: str,
               in_wav: Path, out_wav: Path) -> None:
    """Render in_wav through lv2-psx-reverb at preset_name, write out_wav."""
    preset_id = SPU94_TO_LV2_PRESET_ID[preset_name]

    fs_in, x = wavfile.read(in_wav)
    if fs_in != FS:
        raise RuntimeError(f"{in_wav}: expected {FS} Hz, got {fs_in}")
    if x.ndim != 2 or x.shape[1] != 2:
        raise RuntimeError(f"{in_wav}: expected stereo int16, got shape {x.shape}")
    if x.dtype != np.int16:
        raise RuntimeError(f"{in_wav}: expected int16, got {x.dtype!r}")
    n = x.shape[0]

    # Convert to float32 for lv2 (audio ports are float32 per spec).
    xl = np.ascontiguousarray(x[:, 0].astype(np.float32) / 32768.0)
    xr = np.ascontiguousarray(x[:, 1].astype(np.float32) / 32768.0)
    yl = np.zeros(n, dtype=np.float32)
    yr = np.zeros(n, dtype=np.float32)

    # Control port storage (float32 scalars, referenced by pointer).
    c_wet    = np.array([0.0], dtype=np.float32)
    c_dry    = np.array([0.0], dtype=np.float32)
    c_preset = np.array([float(preset_id)], dtype=np.float32)
    c_master = np.array([0.0], dtype=np.float32)

    plugin = LV2Plugin(bundle_path, sample_rate=float(FS))
    try:
        # Connect static control ports + output audio buffers before activate.
        plugin.connect_port(LV2_PORT_WET,    c_wet)
        plugin.connect_port(LV2_PORT_DRY,    c_dry)
        plugin.connect_port(LV2_PORT_PRESET, c_preset)
        plugin.connect_port(LV2_PORT_MASTER, c_master)
        plugin.activate()

        # Run in LV2_BLOCK chunks; re-connect audio buffers per chunk.
        for start in range(0, n, LV2_BLOCK):
            end = min(start + LV2_BLOCK, n)
            block = end - start
            # Take ctypes pointers into the numpy arrays offset by `start`.
            xl_ptr = xl[start:end].ctypes.data
            xr_ptr = xr[start:end].ctypes.data
            yl_ptr = yl[start:end].ctypes.data
            yr_ptr = yr[start:end].ctypes.data
            plugin.connect_port(LV2_PORT_AUDIO_L_IN,  xl_ptr)
            plugin.connect_port(LV2_PORT_AUDIO_R_IN,  xr_ptr)
            plugin.connect_port(LV2_PORT_AUDIO_L_OUT, yl_ptr)
            plugin.connect_port(LV2_PORT_AUDIO_R_OUT, yr_ptr)
            plugin.run(block)

        plugin.deactivate()
    finally:
        plugin.cleanup()

    # Write stereo float32 interleaved to out_wav.
    out = np.stack([yl, yr], axis=1).astype(np.float32)
    wavfile.write(out_wav, FS, out)


# ----------------------------------------------------------------------------
# Input generation (mirrors scripts/regenerate_goldens.py but with trailing pad)
# ----------------------------------------------------------------------------
def generate_input(name: str) -> np.ndarray:
    x = np.zeros((N_TOTAL, 2), dtype=np.int16)
    if name == "impulse":
        x[0, 0] = AMP
        x[0, 1] = AMP
        return x
    if name == "silence":
        return x
    if name == "white_noise":
        rng = np.random.default_rng(NOISE_SEED)
        x[:N_ACTIVE] = rng.integers(-AMP, AMP, size=(N_ACTIVE, 2), dtype=np.int16)
        return x
    if name == "sine_1khz":
        t = np.arange(N_ACTIVE, dtype=np.float64) / FS
        y = (AMP * np.sin(2.0 * np.pi * 1000.0 * t)).astype(np.int16)
        x[:N_ACTIVE, 0] = y
        x[:N_ACTIVE, 1] = y
        return x
    if name == "sweep":
        t = np.arange(N_ACTIVE, dtype=np.float64) / FS
        y = (AMP * chirp(t, SWEEP_F0, DURATION_SEC, SWEEP_F1,
                         method=SWEEP_METHOD)).astype(np.int16)
        x[:N_ACTIVE, 0] = y
        x[:N_ACTIVE, 1] = y
        return x
    raise ValueError(f"unknown input: {name!r}")


# ----------------------------------------------------------------------------
# Metric
# ----------------------------------------------------------------------------
def load_mono_float64(path: Path) -> np.ndarray:
    fs, x = wavfile.read(path)
    if fs != FS:
        raise RuntimeError(f"{path}: expected {FS} Hz, got {fs}")
    if x.dtype == np.int16:
        xf = x.astype(np.float64) / 32768.0
    elif x.dtype == np.int32:
        xf = x.astype(np.float64) / 2147483648.0
    elif x.dtype == np.float32:
        xf = x.astype(np.float64)
    elif x.dtype == np.float64:
        xf = x
    else:
        raise RuntimeError(f"{path}: unsupported dtype {x.dtype!r}")
    if xf.ndim == 2:
        xf = xf.mean(axis=1)
    return xf


def aligned_split_band_divergence(spu: np.ndarray, lv2: np.ndarray) -> dict:
    xc = correlate(spu, lv2, mode="full", method="fft")
    lag = int(np.argmax(xc) - (len(lv2) - 1))
    if lag > 0:
        spu_a = spu[lag:]
        lv2_a = lv2[: len(spu) - lag]
    elif lag < 0:
        spu_a = spu[: len(lv2) + lag]
        lv2_a = lv2[-lag:]
    else:
        spu_a, lv2_a = spu, lv2
    n = min(len(spu_a), len(lv2_a))
    spu_a = spu_a[:n]
    lv2_a = lv2_a[:n]

    padlen = min(1024, max(0, n - 1))
    spu_lp = sosfiltfilt(_SOS_LP, spu_a, padlen=padlen)
    lv2_lp = sosfiltfilt(_SOS_LP, lv2_a, padlen=padlen)
    spu_hp = sosfiltfilt(_SOS_HP, spu_a, padlen=padlen)
    lv2_hp = sosfiltfilt(_SOS_HP, lv2_a, padlen=padlen)

    def _ratio_dbfs(a: np.ndarray, b: np.ndarray) -> float:
        diff_rms = float(np.sqrt(np.mean((a - b) ** 2)))
        ref_rms  = float(np.sqrt(np.mean(b ** 2))) + 1e-12
        return 20.0 * float(np.log10(diff_rms / ref_rms + 1e-18))

    return {
        "alignment_lag_samples": lag,
        "low_band_diff_dbfs":  _ratio_dbfs(spu_lp, lv2_lp),
        "high_band_diff_dbfs": _ratio_dbfs(spu_hp, lv2_hp),
        "n_samples_compared":  int(n),
    }


# ----------------------------------------------------------------------------
# Drivers
# ----------------------------------------------------------------------------
def locate_spu94_cli() -> str:
    override = os.environ.get("SPU94_BIN")
    if override:
        return override
    dev_build = Path("build/src/cli/spu94")
    if dev_build.is_file():
        return str(dev_build)
    return "spu94"


def read_lv2_context() -> tuple[Path, str]:
    if not LV2_PATH_FILE.exists() or not LV2_URI_FILE.exists():
        print(f"FAIL: LV2 context files missing under {LV2_PATH_FILE.parent} — "
              f"run scripts/ci/witness_diff_build.sh first", file=sys.stderr)
        sys.exit(1)
    lv2_path = Path(LV2_PATH_FILE.read_text().strip())
    # Find the .lv2 bundle directory.
    bundles = list(lv2_path.glob("*.lv2"))
    if not bundles:
        print(f"FAIL: no .lv2 bundle under {lv2_path}", file=sys.stderr)
        sys.exit(1)
    return bundles[0], LV2_URI_FILE.read_text().strip()


def invoke_spu94(spu94_bin: str, preset: str,
                 in_wav: Path, out_wav: Path) -> None:
    r = subprocess.run(
        [spu94_bin, "--preset", preset, str(in_wav), str(out_wav)],
        check=False, capture_output=True,
    )
    if r.returncode != 0:
        stderr_snip = r.stderr.decode("utf-8", errors="replace")[:300]
        raise RuntimeError(
            f"spu94 CLI failed on preset={preset!r} (rc={r.returncode}): "
            f"{stderr_snip}"
        )


def render_and_measure_one(preset: str, input_name: str,
                            spu94_bin: str, bundle_path: Path,
                            tmp: Path) -> dict:
    in_wav  = tmp / f"{preset}_{input_name}_in.wav"
    spu_wav = tmp / f"{preset}_{input_name}_spu94.wav"
    lv2_wav = tmp / f"{preset}_{input_name}_lv2.wav"

    x = generate_input(input_name)
    wavfile.write(str(in_wav), FS, x)

    invoke_spu94(spu94_bin, preset, in_wav, spu_wav)
    lv2_render(bundle_path, preset, in_wav, lv2_wav)

    spu_mono = load_mono_float64(spu_wav)
    lv2_mono = load_mono_float64(lv2_wav)

    d = aligned_split_band_divergence(spu_mono, lv2_mono)
    return {
        "preset": preset,
        "input":  input_name,
        "alignment_lag_samples": d["alignment_lag_samples"],
        "low_band_diff_dbfs":  round(d["low_band_diff_dbfs"],  6),
        "high_band_diff_dbfs": round(d["high_band_diff_dbfs"], 6),
        "n_samples_compared":  d["n_samples_compared"],
        "lv2_commit": LV2_COMMIT_PIN,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--report", default=str(REPORT_PATH),
                    help="output path for witness_report.json "
                         "(default: %(default)s)")
    args = ap.parse_args()

    spu94_bin = locate_spu94_cli()
    bundle_path, _lv2_uri = read_lv2_context()

    results: list[dict] = []
    out_path = Path(args.report)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="spu94_witness_") as tmpd:
        tmp = Path(tmpd)
        for preset in sorted(PRESETS):
            for input_name in sorted(INPUTS):
                print(f"  {preset:<11s} / {input_name:<12s} ... ",
                      end="", flush=True)
                try:
                    entry = render_and_measure_one(
                        preset, input_name, spu94_bin, bundle_path, tmp,
                    )
                except Exception as e:
                    print(f"ERROR: {e}", file=sys.stderr)
                    return 1
                results.append(entry)
                print(
                    f"lag={entry['alignment_lag_samples']:>6d} "
                    f"low={entry['low_band_diff_dbfs']:+8.3f} dBFS  "
                    f"high={entry['high_band_diff_dbfs']:+8.3f} dBFS"
                )

    if len(results) != 50:
        print(f"FAIL: expected 50 rows, got {len(results)}", file=sys.stderr)
        return 1

    out_path.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
    print()
    print(f"PASS: wrote {len(results)} rows to {out_path}")
    print("      (D-06 measurement-only; no pass/fail gate on magnitude)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
