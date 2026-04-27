---
phase: 03-io-layer
verified: 2026-04-27T20:00:00Z
status: human_needed
score: 5/5 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Launch the JUCE standalone and verify the ADPCM toggle is visible, amber when active, and produces audibly grainier output when on"
    expected: "Toggle appears between preset selector and Input knob. Clicking ON shows amber checkbox tick. Audio sounds grainier/more quantized compared to toggle OFF. No clicks or pops when toggling rapidly. No controls clipped at window edges."
    why_human: "Visual layout, colour rendering, and audio character cannot be verified programmatically. User already performed this check (see prompt note) and approved. This item is here for record-keeping per the checkpoint gate in Plan 02 Task 2."
---

# Phase 3: I/O Layer Verification Report

**Phase Goal:** Users can encode/decode ADPCM via CLI, Python, and JUCE standalone — making the codec accessible through every existing interface
**Verified:** 2026-04-27T20:00:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (from ROADMAP Success Criteria)

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| SC-1 | `spu94 adpcm-encode input.wav output.vag` produces valid VAG v2; `spu94 adpcm-decode` round-trips to matching samples | VERIFIED | `cmd_adpcm.c` implements full encode/decode pipeline; `test_cli_adpcm.py::test_adpcm_encode_decode_roundtrip` passes (7/7 CLI tests pass) |
| SC-2 | `spu94 --adpcm --preset hall` produces audibly different (grainier) output than without `--adpcm` | VERIFIED | `cmd_reverb.c` has `--adpcm` flag wired to `spu94_set_adpcm_enabled(state, 1)`; `test_cli_adpcm.py::test_reverb_adpcm_flag` asserts output differs from run without flag |
| SC-3 | Python callers can call `spu94_adpcm_decode_block()`, `spu94_adpcm_encode_block()`, `spu94_set_adpcm_enabled()`, `spu94_get_adpcm_enabled()` via ctypes | VERIFIED | All 4 functions have argtypes/restype in `_binding.py`; 12/12 Python binding tests pass (`test_binding_adpcm`) |
| SC-4 | JUCE standalone shows "ADPCM" toggle enabling/disabling coloration during playback | VERIFIED (code) / HUMAN NEEDED (visual+audio) | `PluginEditor.h` has `juce::ToggleButton adpcmToggle{"ADPCM"}`; `PluginEditor.cpp` sets amber colour `0xFFD4A017`, wires `onClick` to `adpcmEnabled.store`; `PluginProcessor.cpp` reads atomic in `processBlock` and calls `spu94_set_adpcm_enabled`. User approved via checkpoint gate. |
| SC-5 | VAG reader handles big-endian headers with explicit byte-order conversion (no ntohl) and respects terminator blocks | VERIFIED | `vag.c` uses shift-based `read_be32()` exclusively; no `ntohl`/`htonl` present; stop-on-flag logic in `decode_stream` handles `SPU94_VAG_FLAG_END` (0x01) and `SPU94_VAG_FLAG_TERMINATOR` (0x07); `test_vag_read_header_accepts_any_version` passes |

**Score:** 5/5 truths verified (code-level); SC-4 requires human confirmation of visual/audio behavior

### Required Artifacts

| Artifact | Status | Evidence |
|----------|--------|----------|
| `include/spu94/spu94_vag.h` | VERIFIED | File exists; contains `typedef struct spu94_vag_header`, `spu94_vag_read_header`, `spu94_vag_write_header`, `SPU94_VAG_HEADER_BYTES 48`, `SPU94_VAG_FLAG_END 0x01`, `SPU94_VAG_FLAG_TERMINATOR 0x07` |
| `src/spu94/vag.c` | VERIFIED | File exists; shift-based `read_be32`/`write_be32`; no `ntohl`/`malloc`/`free`; zero-heap; force null-terminate at `name[15]` (T-03-03) |
| `src/cli/cmd_adpcm.c` | VERIFIED | File exists; 384 lines; implements `cmd_adpcm_encode`, `cmd_adpcm_decode`, `cmd_adpcm_roundtrip`; calls `spu94_vag_read_header`, `spu94_vag_write_header`, `spu94_adpcm_encode_block`, `spu94_adpcm_decode_block`, `SPU94_VAG_FLAG_END` |
| `src/cli/cmd_reverb.c` | VERIFIED | File exists; contains `{"adpcm"` in long_opts table; `spu94_set_adpcm_enabled` called when `adpcm_enabled = true` |
| `src/cli/main.c` | VERIFIED | File exists; dispatches `adpcm-encode`, `adpcm-decode`, `adpcm-roundtrip`, `reverb`; legacy fallback `cmd_reverb(argc, argv)` on leading `-` or no subcommand |
| `tests/unit/vag/test_vag.c` | VERIFIED | File exists; contains `test_vag_read_header_accepts_any_version`, `test_vag_write_header_roundtrip`, `test_vag_read_header_bad_magic`, `test_vag_read_header_valid`, `test_vag_write_header_null_name` |
| `tests/cli/test_cli_adpcm.py` | VERIFIED | File exists; 7 test functions covering legacy compat, reverb subcommand, encode/decode roundtrip, roundtrip subcommand, --adpcm flag, global help, unknown subcommand |
| `src/standalone/PluginEditor.h` | VERIFIED | Contains `juce::ToggleButton adpcmToggle{"ADPCM"}` |
| `src/standalone/PluginProcessor.h` | VERIFIED | Contains `std::atomic<bool> adpcmEnabled{false}` and `getAdpcmEnabled()` accessor |
| `src/standalone/PluginEditor.cpp` | VERIFIED | `adpcmToggle.setClickingTogglesState(true)`; amber colour `0xFFD4A017`; `adpcmToggle.setBounds(585, 10, 70, 30)`; `setSize(850, 750)` |
| `src/standalone/PluginProcessor.cpp` | VERIFIED | `spu94_set_adpcm_enabled(spu, adpcmEnabled.load(std::memory_order_relaxed) ? 1 : 0)` in processBlock |
| `python/spu94/_binding.py` | VERIFIED | `_Spu94AdpcmState`, `_Spu94VagHeader`, all 4 ADPCM function prototypes, 2 VAG function prototypes, 3 constants (`SPU94_ADPCM_BLOCK_SAMPLES=28`, `SPU94_ADPCM_BLOCK_BYTES=16`, `SPU94_VAG_HEADER_BYTES=48`) |
| `tests/python/binding/test_binding_adpcm.py` | VERIFIED | File exists; 5 test classes (TestAdpcmStateStruct, TestAdpcmDecodeBlock, TestAdpcmEncodeBlock, TestAdpcmToggle, TestVagHeader); 12 tests |

### Key Link Verification

| From | To | Via | Status | Evidence |
|------|----|-----|--------|----------|
| `src/cli/main.c` | `src/cli/cmd_reverb.c` | `cmd_reverb` extern + dispatch | WIRED | `extern int cmd_reverb(int argc, char **argv)` at line 26; legacy fallback at line 70 |
| `src/cli/main.c` | `src/cli/cmd_adpcm.c` | `cmd_adpcm_encode` extern + dispatch | WIRED | `extern int cmd_adpcm_encode` at line 27; dispatch at line 61 |
| `src/cli/cmd_adpcm.c` | `src/spu94/vag.c` | `spu94_vag_read_header` / `spu94_vag_write_header` | WIRED | Both functions called in `cmd_adpcm_decode` and `encode_channel` |
| `src/cli/cmd_reverb.c` | `include/spu94/spu94.h` | `spu94_set_adpcm_enabled` | WIRED | Called at line 213 when `adpcm_enabled` flag set |
| `src/standalone/PluginEditor.cpp` | `src/standalone/PluginProcessor.h` | `adpcmEnabled.store` in onClick | WIRED | `processorRef.getAdpcmEnabled().store(adpcmToggle.getToggleState(), ...)` at line 101 |
| `src/standalone/PluginProcessor.cpp` | `spu94_set_adpcm_enabled` | atomic load in processBlock | WIRED | Lines 119-120 confirmed |
| `python/spu94/_binding.py` | `libspu94.so` | ctypes `_lib.spu94_adpcm_decode_block` | WIRED | Prototype declared at line 206; 12 tests pass calling into live .so |
| `python/spu94/_binding.py` | `libspu94.so` | ctypes `_lib.spu94_vag_read_header` | WIRED | Prototype declared at line 234; `TestVagHeader` tests pass |
| `src/spu94/CMakeLists.txt` | `vag.c` | OBJECT library source list | WIRED | `vag.c` listed in `spu94_obj` sources |
| `src/cli/CMakeLists.txt` | `cmd_reverb.c`, `cmd_adpcm.c` | `spu94_cli` target sources | WIRED | Both files listed in CLI build sources |
| `tests/unit/CMakeLists.txt` | `vag/test_vag.c` | `add_subdirectory(vag)` + `add_test(NAME vag_read_write)` | WIRED | Registered and passes: `ctest -R vag_read_write` → 1/1 PASSED |
| `tests/python/binding/CMakeLists.txt` | `test_binding_adpcm.py` | CTest registration | WIRED | Listed in `_binding_tests`; `ctest -R test_binding_adpcm` → PASSED |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|--------------|--------|--------------------|--------|
| `cmd_adpcm.c` → encode path | VAG file output | `spu94_adpcm_encode_block` + `spu94_vag_write_header` | Yes — live codec + file write | FLOWING |
| `cmd_adpcm.c` → decode path | WAV sample buffer | `spu94_adpcm_decode_block` from real file bytes | Yes — reads real .vag bytes | FLOWING |
| `PluginProcessor.cpp` | `adpcmEnabled` | `adpcmEnabled.load()` from GUI atomic | Yes — live GUI interaction | FLOWING |
| `_binding.py` | ctypes function calls | `_lib.spu94_adpcm_decode_block` etc. | Yes — calls into live `libspu94.so` | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| `spu94 --help` shows all subcommands | `build/src/cli/spu94 --help` | Output shows adpcm-encode, adpcm-decode, adpcm-roundtrip, reverb | PASS |
| VAG unit tests pass | `ctest -R vag_read_write` | 1/1 PASSED (0.00s) | PASS |
| ADPCM+VAG ctest suite | `ctest -R "vag_read_write|adpcm"` | 5/5 PASSED | PASS |
| CLI ADPCM integration tests | `pytest tests/cli/test_cli_adpcm.py` | 7/7 PASSED | PASS |
| Full CLI test suite (regression) | `pytest tests/cli/` | 60/60 PASSED | PASS |
| Python binding ADPCM tests | `SPU94_LIB=... pytest tests/python/binding/test_binding_adpcm.py` | 12/12 PASSED | PASS |
| Python binding ADPCM via CTest | `ctest -R test_binding_adpcm` | 1/1 PASSED | PASS |

### Requirements Coverage

| REQ-ID | Plan | Description | Status | Evidence |
|--------|------|-------------|--------|----------|
| ADPCM-IO-01 | 03-01 | CLI gains adpcm-encode, adpcm-decode, adpcm-roundtrip subcommands | SATISFIED | All 3 subcommands in `main.c` dispatch + `cmd_adpcm.c`; 7 CLI tests pass |
| ADPCM-IO-02 | 03-01 | CLI gains `--adpcm` flag for reverb mode | SATISFIED | `{"adpcm", no_argument, NULL, 'a'}` in `cmd_reverb.c`; `spu94_set_adpcm_enabled` called |
| ADPCM-IO-03 | 03-01 | VAG reader uses explicit byte-order (no ntohl); accepts any version; handles terminator blocks | SATISFIED | `read_be32` shift-based; no `ntohl` in source; `decode_stream` stops on flag 0x01/0x07; `test_vag_read_header_accepts_any_version` passes |
| ADPCM-IO-04 | 03-01 | VAG writer produces valid VAG v2; zero-pads final block; sets end flag | SATISFIED | `spu94_vag_write_header` writes version=2; `encode_channel` sets `SPU94_VAG_FLAG_END` on last block; zero-pads via `memset(padded, 0, ...)` |
| ADPCM-IO-05 | 03-03 | Python ctypes bindings expose decode_block, encode_block, set/get_adpcm_enabled | SATISFIED | All 4 functions have prototypes in `_binding.py`; 12 binding tests pass |
| ADPCM-IO-06 | 03-02 | JUCE standalone gains ADPCM toggle | SATISFIED (code) / HUMAN (visual+audio) | Toggle wired end-to-end in code; user checkpoint approved per plan gate |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `tests/cli/CMakeLists.txt` | — | `test_cli_adpcm.py` not registered in CTest `_cli_tests` list | Warning | `ctest` does not automatically run CLI ADPCM integration tests; must use `pytest tests/cli/` directly. All 7 tests pass when run this way. This is a test wiring omission, not a functionality gap. |

**Stub classification:** No stubs found. All code paths lead to live data (real file I/O, real codec calls, real atomic reads).

### Human Verification Required

#### 1. ADPCM Toggle Visual and Audio Verification

**Test:** Launch `./build/src/standalone/SPU94Standalone_artefacts/Debug/SPU94Standalone` (or equivalent build path). Verify the toolbar shows [Load WAV] [Play] [Stop] [Preset] [ADPCM] [Input] [Wet/Dry]. Click the ADPCM toggle on and off.
**Expected:** Toggle appears between preset selector and Input knob. Active state shows amber checkbox tick (0xFFD4A017 colour). With Hall preset and WAV playing, ADPCM on produces grainier/more quantized character compared to off. Rapid toggling produces no clicks or pops. No controls overlap or clip at window edges (850px wide).
**Why human:** Visual layout, colour rendering, and audio character cannot be verified programmatically. This is the Plan 02 Task 2 checkpoint gate.
**Note:** User already approved this during phase execution per the task prompt. This entry is present because the checkpoint gate design makes it a formal human-verify item. No action needed if user has already confirmed approval.

### Gaps Summary

No functional gaps found. All 6 ADPCM-IO requirements have complete implementations that are wired and tested.

One test wiring observation: `tests/cli/test_cli_adpcm.py` is not registered in `tests/cli/CMakeLists.txt` and therefore does not run via `ctest`. The 7 CLI ADPCM tests pass when invoked directly via `pytest tests/cli/`. This does not block the phase goal — the test file exists and all tests pass — but the CTest registration gap means CI would not automatically run these tests via `ctest`. This is a WARNING-level observation, not a blocker.

---

_Verified: 2026-04-27T20:00:00Z_
_Verifier: Claude (gsd-verifier)_
