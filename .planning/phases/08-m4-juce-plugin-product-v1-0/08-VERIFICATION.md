---
phase: 08-m4-juce-plugin-product-v1-0
verified: 2026-04-26T03:06:51Z
status: human_needed
score: 8/8
overrides_applied: 0
human_verification:
  - test: "Launch SPU-94 standalone, load a WAV file, press Play, and confirm audio plays back through the reverb with no crashes, glitches, or wrong-rate playback"
    expected: "Audio plays cleanly at correct pitch through the SPU reverb engine"
    why_human: "Real-time audio playback quality, glitch detection, and pitch correctness require human ears and a running audio device"
  - test: "Twist the 12 free-class sliders (vLOUT, vROUT, vLIN, vRIN, vIIR, vWALL, vCOMB1-4, vAPF1-2) during playback and confirm smooth, zipper-free changes"
    expected: "Gain-class registers change the reverb character smoothly with no stepping artifacts"
    why_human: "Perceptual smoothness vs. zipper noise requires human ears"
  - test: "Twist the 6 sample-quantized sliders (dLSAME, dRSAME, dLDIFF, dRDIFF, dAPF1, dAPF2) during playback and confirm audible stepping is present (character, not bug)"
    expected: "Delay-position registers produce audible discontinuities when moved -- this is expected SPU behavior"
    why_human: "Confirming audible stepping is present and sounds like character (not distortion) requires human ears"
  - test: "Switch presets via the dropdown during playback (try Hall, Room, Echo, Off) and confirm each produces audibly distinct output with no crash"
    expected: "Each preset produces a different reverb character; switching is instant with possible audible discontinuity (acceptable per ADR-0006)"
    why_human: "Verifying preset differences requires human listening; crash resilience during preset switch requires interactive testing"
  - test: "Sweep the Wet/Dry knob from 0% to 100% during playback"
    expected: "At 0% Wet: unprocessed dry input only. At 100% Wet: SPU reverb only. At 50%: constant-power blend. Smooth transition throughout."
    why_human: "Perceived loudness constancy (equal-power crossfade) requires human ears"
  - test: "Load a WAV file at a non-44.1 kHz sample rate (e.g., 48 kHz or 96 kHz) and verify it plays at correct pitch"
    expected: "Audio plays at the correct pitch regardless of source sample rate -- WavLoader resamples to 44.1 kHz at load time"
    why_human: "Pitch correctness after resampling requires human ears. Also verifies that the output device rate does not cause pitch shift."
---

# Phase 8: SPU-94 Standalone GUI (product v1.0) Verification Report

**Phase Goal:** Anthony launches SPU-94 as a single-window standalone application on Linux, loads any WAV file, picks a preset, and hears the audio play back through the bit-faithful reverb in real-time. While playback runs, he twists 18 raw register sliders and a Wet/Dry mix knob to hear how each parameter changes the character. The standalone wraps libspu94 without modifying the C core.

**Verified:** 2026-04-26T03:06:51Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth (ROADMAP SC) | Status | Evidence |
|---|---------------------|--------|----------|
| 1 | Standalone application launches on Linux from a fresh CMake build. Single-window UI appears. No plugin host or DAW required. | VERIFIED | Binary at `build/src/standalone/spu94_standalone_artefacts/Release/Standalone/SPU-94` is a 64-bit ELF executable. `ldd` confirms dynamic linkage to `libspu94.so`. `FORMATS Standalone` in CMakeLists.txt -- no VST3/LV2/CLAP/AU. Root CMakeLists.txt has `add_subdirectory(src/standalone)`. JUCE 8.0.12 via FetchContent with pinned SHA. |
| 2 | User loads any WAV via file picker -- I/O wrapper accepts any SR, any bit depth, mono or stereo -- converts to 44.1 kHz int16 stereo. SPU core stays unmodified. | VERIFIED | `WavLoader.cpp` (101 lines): `registerBasicFormats()` handles all JUCE-supported formats. `WindowedSincInterpolator` resamples to 44.1 kHz. Mono duplicated to stereo. Float-to-int16 with 32767 scaling, `std::lround`, `std::clamp`. File picker via `juce::FileChooser::launchAsync`. No changes to `src/spu94/` or `include/spu94/` in git log. |
| 3 | Real-time playback runs cleanly: no crashes, no glitches, no distortion, no wrong-rate playback. | NEEDS HUMAN | Code path verified: `processBlock` feeds stack-allocated int16 buffers to `spu94_process`, converts output to float, loops continuously. Double-buffered WAV swap (CR-01 fixed). Divide-by-zero guard (CR-03 fixed). Oversized block bail (WR-02 fixed). Actual audio quality requires human ears. |
| 4 | All 10 PS1 factory presets selectable from flat dropdown, each producing correct output. Switching during playback works without crash. | VERIFIED (code) / NEEDS HUMAN (audio) | `PluginEditor.cpp:47-48` iterates `spu94_presets[0..9]` via `SPU94_PRESET__COUNT`. ComboBox onChange routes through `PresetCommandQueue` (lock-free SPSC). `processBlock` drains queue and calls `spu94_load_preset`. Timer at 30Hz syncs slider positions after switch. Audible correctness requires human. |
| 5 | 18 raw labeled register sliders with raw names (not musical aliases). 12 free-class smooth, 6 sample-quantized step audibly. | VERIFIED (code) / NEEDS HUMAN (audio) | `kSliderRegisters` constexpr array: 12 v-prefix + 6 d-prefix = 18, enforced by `static_assert`. Labels from `spu94_reg_name()`. I16 sliders: [-32768, 32767]. U16 sliders: [0, 65535]. Numeric TextBoxRight display. Grouped with bold headers (Master I/O, IIR+Wall, Comb, All-Pass, Delay Offsets). Perceptual smoothness/stepping requires human. |
| 6 | Wet/Dry knob blends dry input and SPU wet output. 0% Wet = dry only. 100% Wet = reverb only. Smooth transition. | VERIFIED (code) / NEEDS HUMAN (audio) | `PluginProcessor.cpp:158-173`: `wetGain = sqrt(wet)`, `dryGain = sqrt(1-wet)`. Output = `dry * dryGain + spu * wetGain`. Rotary knob range [0,1], step 0.01, default 0.5. Atomic float for lock-free GUI-to-audio. Perceptual loudness constancy requires human. |
| 7 | Builds reproducibly via same root CMakeLists.txt. libspu94 linked unmodified. | VERIFIED | Root `CMakeLists.txt` extended with `LANGUAGES C CXX`, C++17, JUCE FetchContent, `add_subdirectory(src/standalone)`. `src/standalone/CMakeLists.txt` links `spu94_shared PRIVATE`. `ldd` confirms `libspu94.so` dynamic link. Git log shows zero commits touching `src/spu94/` or `include/spu94/`. |
| 8 | JUCE plugin metadata uses "SPU-94" not "PSX Reverb". | VERIFIED | `CMakeLists.txt`: `PRODUCT_NAME "SPU-94"`, `PLUGIN_NAME "SPU-94"`, `COMPANY_NAME "SPU-94 Project"`. `PluginProcessor.cpp:42`: `getName()` returns `"SPU-94"`. Zero occurrences of "PSX Reverb" in any standalone source file. |

**Score:** 8/8 truths verified (code-level). 6 items require human audio verification.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/standalone/CMakeLists.txt` | JUCE standalone target linking spu94_shared | VERIFIED | 48 lines. `juce_add_plugin` with `FORMATS Standalone`. Links `spu94_shared PRIVATE`. `PRIVATE` compile defs (WR-04 fixed). |
| `src/standalone/PluginProcessor.h` | AudioProcessor with SPU state, WAV source, parameter bridge | VERIFIED | 99 lines. Double-buffered PendingWav (CR-01). Atomic wetDry + inputLevel. RegisterBridge + PresetCommandQueue members. |
| `src/standalone/PluginProcessor.cpp` | processBlock with spu94_process, crossfade, input level | VERIFIED | 261 lines. Constructor pre-seeds shadows (CR-02). prepareToPlay destroys old state (WR-03). processBlock: double-buffer swap, preset drain, register push, spu94_process, equal-power crossfade. Divide-by-zero guard (CR-03). Oversized block bail (WR-02). |
| `src/standalone/PluginEditor.h` | Editor with buttons, sliders, knobs, timer | VERIFIED | 43 lines. Load/Play/Stop buttons, RegisterPanel, ComboBox, wetDryKnob, inputLevelKnob, Timer inheritance. |
| `src/standalone/PluginEditor.cpp` | UI layout, preset wiring, knob wiring | VERIFIED | 150 lines. Async file picker. 10-preset ComboBox from spu94_presets. Wet/Dry rotary 0-1. Input Level rotary 0-1 default 0.25. 30Hz timer for preset sync. Window locked 800x750. |
| `src/standalone/WavLoader.h` | LoadedWav struct + load declaration | VERIFIED | 27 lines. Struct with L/R int16 vectors, numFrames, original metadata. |
| `src/standalone/WavLoader.cpp` | Resample + format convert implementation | VERIFIED | 101 lines. registerBasicFormats, createReaderFor, WindowedSincInterpolator (per-channel), float-to-int16 with 32767/lround/clamp, mono-to-stereo duplication. |
| `src/standalone/ParameterBridge.h` | Lock-free bridge + SPSC preset queue | VERIFIED | 86 lines. kSliderRegisters constexpr array (18 entries, static_assert). RegisterBridge with atomic int16 shadows. PresetCommandQueue with atomic request/drain. |
| `src/standalone/ParameterBridge.cpp` | Bridge implementation with acquire/release ordering | VERIFIED | 87 lines. pushPendingRegisterWrites dispatches I16/U16 via spu94_reg_type. syncShadowsFromSPU reads back from SPU. PresetCommandQueue drains with spu94_load_preset + appliedCount. |
| `src/standalone/RegisterPanel.h` | 18-slider panel component | VERIFIED | 37 lines. Arrays of 18 sliders + 18 labels. Group headers. updateFromShadows(). |
| `src/standalone/RegisterPanel.cpp` | Dynamic slider construction + layout | VERIFIED | 95 lines. Iterates kSliderRegisters, labels from spu94_reg_name, type-aware ranges, grouped layout (4+2+4+2+6). |
| `CMakeLists.txt` (root) | Extended with JUCE + standalone subdirectory | VERIFIED | LANGUAGES C CXX. C++17. JUCE 8.0.12 FetchContent with pinned SHA. add_subdirectory(src/standalone). |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| PluginProcessor.cpp | libspu94 C API | `extern "C"` include + spu94_init/process/destroy/load_preset calls | WIRED | 9 C API call sites in processBlock + prepareToPlay + constructor + destructor |
| PluginEditor.cpp | PluginProcessor | loadWavFile, startPlayback, stopPlayback, getRegisterBridge, getPresetQueue, getWetDry, getInputLevel | WIRED | All 7 accessor paths exercised in button/knob/combo handlers |
| WavLoader.cpp | JUCE AudioFormatManager | registerBasicFormats + createReaderFor | WIRED | File read + resample at load time, result returned as LoadedWav |
| RegisterPanel.cpp | ParameterBridge.h | kSliderRegisters iteration + bridge.setRegisterShadow + spu94_reg_name | WIRED | Slider construction iterates the 18-entry array, onChange stores to bridge |
| ParameterBridge.cpp | libspu94 register API | spu94_set_reg_i16/u16, spu94_get_reg_i16/u16, spu94_load_preset | WIRED | pushPendingRegisterWrites + syncShadowsFromSPU + PresetCommandQueue::drain |
| PluginEditor.cpp (wetDryKnob) | PluginProcessor.h (wetDry atomic) | onChange stores to getWetDry() | WIRED | Knob value stored via atomic float, read in processBlock crossfade |
| PluginEditor.cpp (inputLevelKnob) | PluginProcessor.h (inputLevel atomic) | onChange stores to getInputLevel() | WIRED | Knob value stored via atomic float, read in processBlock input scaling |
| CMakeLists.txt (root) | src/standalone/CMakeLists.txt | add_subdirectory(src/standalone) | WIRED | Standalone target registered in build system |
| src/standalone/CMakeLists.txt | spu94_shared | target_link_libraries PRIVATE | WIRED | ldd confirms libspu94.so linked at runtime |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| PluginProcessor.cpp | wavSource.L/R | WavLoader::load -> loadWavFile -> double-buffer swap | Yes -- reads WAV file, resamples, converts to int16 | FLOWING |
| PluginProcessor.cpp | spu94_process output (tmpL_out/tmpR_out) | spu94_process(spu, ...) | Yes -- calls real C API with loaded WAV data | FLOWING |
| RegisterPanel.cpp | slider values | bridge.getShadowValue(i) | Yes -- shadows populated from spu94_get_reg_i16/u16 via syncShadowsFromSPU | FLOWING |
| PluginEditor.cpp | presetSelector items | spu94_presets[i].name | Yes -- iterates real preset array from C library | FLOWING |
| PluginProcessor.cpp | wetDry / inputLevel | atomic floats from editor knobs | Yes -- user-driven values consumed in processBlock | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Binary exists and is valid ELF | `file build/.../SPU-94` | ELF 64-bit LSB pie executable, x86-64, dynamically linked | PASS |
| libspu94.so dynamically linked | `ldd build/.../SPU-94 \| grep spu94` | `libspu94.so => .../build/src/spu94/libspu94.so` | PASS |
| No "PSX Reverb" in standalone | `grep -r "PSX Reverb" src/standalone/` | No matches | PASS |
| "SPU-94" in metadata | `grep "SPU-94" src/standalone/CMakeLists.txt` | PRODUCT_NAME, PLUGIN_NAME both "SPU-94" | PASS |
| 18 registers compile-time enforced | `grep static_assert src/standalone/ParameterBridge.h` | `static_assert(kSliderRegisters.size() == 18)` | PASS |
| C core untouched | `git log --name-only -- src/spu94/ include/spu94/` | No commits touching C core files | PASS |
| All 7 review fixes committed | `git log --oneline HEAD~10..HEAD` | 7 fix commits (5f6eb43, 5cd749e, 6c6c7d3, 14466ca, a1dafe4, 0cb4d07, b61406a) | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| STANDALONE-01 | 08-01 | Standalone JUCE app on Linux, no DAW required | SATISFIED | ELF binary, `FORMATS Standalone`, no plugin formats |
| STANDALONE-02 | 08-02 | Load WAV (any SR, any BD, mono/stereo) via file picker with I/O conversion | SATISFIED | WavLoader with WindowedSincInterpolator, float-to-int16, mono-to-stereo |
| STANDALONE-03 | 08-02 | Real-time playback through libspu94, no crashes/underruns | SATISFIED (code) / NEEDS HUMAN (audio) | processBlock with spu94_process, stack I/O buffers, all critical bugs fixed |
| STANDALONE-04 | 08-03 | 10 PS1 factory presets in flat dropdown | SATISFIED | ComboBox iterates spu94_presets[0..9], SPSC preset queue, lock-free drain |
| STANDALONE-05 | 08-03 | 18 raw labeled register sliders (12 free + 6 quantized) | SATISFIED | kSliderRegisters array, static_assert == 18, labels from spu94_reg_name |
| STANDALONE-06 | 08-04 | Wet/Dry mix knob with equal-power crossfade | SATISFIED | Rotary [0,1], sqrt pan law, atomic float bridge |
| STANDALONE-07 | 08-03 | JUCE stock look-and-feel, no custom skin | SATISFIED | No custom LookAndFeel class. Stock Slider, ComboBox, TextButton, Label throughout |
| STANDALONE-08 | 08-01 | Builds reproducibly via root CMakeLists.txt, libspu94 linked unmodified | SATISFIED | FetchContent JUCE 8.0.12 pinned SHA. spu94_shared linked PRIVATE. ldd confirms. |
| STANDALONE-09 | 08-01 | Metadata says "SPU-94" not "PSX Reverb" | SATISFIED | PRODUCT_NAME, PLUGIN_NAME, getName() all return "SPU-94" |

No orphaned requirements found. All 9 STANDALONE requirements mapped to plans and satisfied in code.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| PluginProcessor.cpp | 250 | Stale comment: "Plan 03 fills this with register state serialization" | Info | Comment references Plan 03 but state serialization was never in scope for v1.0. The empty `getStateInformation`/`setStateInformation` bodies are correct -- JUCE requires the overrides but standalone does not need state persistence. Not a functional issue. |
| PluginProcessor.cpp | 254 | Stale comment: "Plan 03 fills this with register state deserialization" | Info | Same as above. |

No blocker or warning-level anti-patterns found. No TODOs, FIXMEs, placeholders, or stub implementations. No debug output (console.log, printf, DBG). The `return {}` in `getProgramName` is the standard JUCE pattern for single-program plugins.

### Human Verification Required

All 8 ROADMAP success criteria are verified at the code level. However, 6 items require human verification because they involve perceptual audio quality that cannot be assessed programmatically:

### 1. Real-Time Audio Playback Quality

**Test:** Launch SPU-94, load a WAV file, press Play, and confirm audio plays back through the reverb with no crashes, glitches, or wrong-rate playback.
**Expected:** Audio plays cleanly at correct pitch through the SPU reverb engine.
**Why human:** Real-time audio playback quality, glitch detection, and pitch correctness require human ears and a running audio device.

### 2. Free-Class Slider Smoothness

**Test:** Twist the 12 free-class sliders (vLOUT, vROUT, vLIN, vRIN, vIIR, vWALL, vCOMB1-4, vAPF1-2) during playback and confirm smooth, zipper-free changes.
**Expected:** Gain-class registers change the reverb character smoothly with no stepping artifacts.
**Why human:** Perceptual smoothness vs. zipper noise requires human ears.

### 3. Sample-Quantized Slider Stepping

**Test:** Twist the 6 sample-quantized sliders (dLSAME, dRSAME, dLDIFF, dRDIFF, dAPF1, dAPF2) during playback and confirm audible stepping is present (character, not bug).
**Expected:** Delay-position registers produce audible discontinuities when moved -- this is expected SPU behavior.
**Why human:** Confirming audible stepping is present and sounds like character (not distortion) requires human ears.

### 4. Preset Switching During Playback

**Test:** Switch presets via the dropdown during playback (try Hall, Room, Echo, Off) and confirm each produces audibly distinct output with no crash.
**Expected:** Each preset produces a different reverb character; switching is instant with possible audible discontinuity (acceptable per ADR-0006).
**Why human:** Verifying preset differences requires human listening; crash resilience during preset switch requires interactive testing.

### 5. Wet/Dry Equal-Power Crossfade

**Test:** Sweep the Wet/Dry knob from 0% to 100% during playback.
**Expected:** At 0% Wet: unprocessed dry input only. At 100% Wet: SPU reverb only. At 50%: constant-power blend. Smooth transition throughout.
**Why human:** Perceived loudness constancy (equal-power crossfade) requires human ears.

### 6. Non-Native Sample Rate Playback

**Test:** Load a WAV file at a non-44.1 kHz sample rate (e.g., 48 kHz or 96 kHz) and verify it plays at correct pitch.
**Expected:** Audio plays at the correct pitch regardless of source sample rate -- WavLoader resamples to 44.1 kHz at load time.
**Why human:** Pitch correctness after resampling requires human ears. Also verifies that the output device rate does not cause pitch shift.

### Gaps Summary

No code-level gaps found. All 11 source files exist, are substantive (1034 total lines), and are fully wired. All 9 key links verified as WIRED. All 5 data-flow traces show real data flowing from WAV file through spu94_process to audio output. All 9 STANDALONE requirements are satisfied in code. All 7 code review findings (3 critical + 4 warning) have been fixed in atomic commits. The C core (libspu94) was not modified.

Two info-level notes:

1. **Stale comments** in `getStateInformation`/`setStateInformation` reference "Plan 03" but state serialization was never in v1.0 scope. The empty bodies are correct (JUCE requires the override, standalone does not need persistence). Cosmetic only.

2. **No output-side resampling**: The WavLoader resamples input to 44.1 kHz, but the JUCE audio output runs at the system device rate. If Anthony's audio device is at 48 kHz, playback would be pitched up slightly (~9%). The ROADMAP architecture sketch shows "JUCE audio output -> speakers" without explicit output resampling. JUCE's standalone wrapper does handle device rate matching in most configurations, but this should be confirmed during human verification (test item 6).

---

_Verified: 2026-04-26T03:06:51Z_
_Verifier: Claude (gsd-verifier)_
