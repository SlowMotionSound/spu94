---
phase: 24-state-automation-surface
reviewed: 2026-05-12T15:33:11Z
depth: standard
files_reviewed: 7
files_reviewed_list:
  - src/plugin/MorphPanel.cpp
  - src/plugin/PluginEditor.cpp
  - src/plugin/PluginProcessor.cpp
  - src/plugin/PluginProcessor.h
  - src/plugin/StateSerializer.h
  - tests/plugin/CMakeLists.txt
  - tests/plugin/test_state_serializer.cpp
findings:
  critical: 2
  warning: 4
  info: 2
  total: 8
status: issues_found
---

# Phase 24: Code Review Report

**Reviewed:** 2026-05-12T15:33:11Z
**Depth:** standard
**Files Reviewed:** 7
**Status:** issues_found

## Summary

Phase 24 wires 9 host-automatable AudioParameterFloat instances into the existing GUI knobs, morph engine, and mixer/DAC controls, adds a binary state container (StateSerializer) for DAW project persistence, and includes unit tests for the serializer. The parameter registration, gesture API wiring, and timer-based GUI sync are generally well-structured. However, the review found two critical issues (missing float sanitization on deserialized state, and data race on concurrent engine writes from message + audio threads) and four warnings (LookAndFeel lifetime, endianness assumption, stale atomics creating a dual-source-of-truth risk, and missing test coverage for NaN/Inf inputs).

## Critical Issues

### CR-01: No float sanitization on deserialized state -- NaN/Inf propagation into DSP

**File:** `src/plugin/StateSerializer.h:156-159` and `src/plugin/PluginProcessor.cpp:914-927`
**Issue:** `StateSerializer::load` extracts four IEEE 754 floats from the binary appendix via raw `memcpy` and returns them without any validation. A corrupted or adversarially crafted DAW project file can inject `NaN`, `Inf`, `-Inf`, or out-of-range values (e.g., `inputGain = -1.0f` or `morphPosition = 500.0f`) for `inputGain`, `morphPosition`, `morphSpeed`, and `morphGrit`. These values flow directly into `processBlock` via `paramInputGain->setValueNotifyingHost(...)` and the corresponding atomics. NaN propagation through the SPU core would corrupt the reverb buffer and produce persistent silence or noise until the plugin is reloaded. Out-of-range `morphPosition` would index past the 9 Sony waypoint anchors in the interpolation engine.
**Fix:** Add float sanitization in `StateSerializer::load` before setting `r.ok = true`. Clamp each value to its valid range and reject NaN/Inf:
```cpp
#include <cmath>

// After extracting appendix values (line 159), before r.ok = true:
auto sanitize = [](float v, float lo, float hi, float fallback) -> float {
    if (std::isnan(v) || std::isinf(v)) return fallback;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
};
r.inputGain     = sanitize(appendix[0], 0.0f, 16.0f, 0.5f);
r.morphPosition = sanitize(appendix[1], 0.0f, 1.0f,  0.625f);
r.morphSpeed    = sanitize(appendix[2], 0.0f, 1.0f,  0.5f);
r.morphGrit     = sanitize(appendix[3], 0.0f, 1.0f,  0.0f);
```

### CR-02: Data race -- getStateInformation and savePresetToString write engines[0] from message thread concurrent with processBlock on audio thread

**File:** `src/plugin/PluginProcessor.cpp:848-890` (getStateInformation) and `src/plugin/PluginProcessor.cpp:638-687` (savePresetToString)
**Issue:** Both `getStateInformation` and `savePresetToString` run on the message thread and call `spu94_set_dry_fader(engines[0], ...)`, `spu94_set_patina_fader(engines[0], ...)`, etc., directly mutating `engines[0]`'s internal state. Simultaneously, `processBlock` on the audio thread also calls the same `spu94_set_*` functions on `engines[0]` (lines 389-410) and then reads from `engines[0]` via `spu94_process`. There is no mutex, suspend flag, or lock-free handoff protecting `engines[0]` from concurrent access. This is undefined behavior per the C++ memory model -- the `spu94_set_*` functions write non-atomic fields inside the engine struct. In practice, a DAW that calls `getStateInformation` while audio is running (e.g., auto-save) could produce torn reads in the reverb buffer, leading to audio glitches, corrupted state, or silence. This is a pre-existing pattern but Phase 24 extends it (the new `paramDryLevel->get() * 0x7FFF` calls add more write sites).
**Fix:** Read all wrapper-side values (params and atomics) into local variables, serialize those locals directly into the preset buffer without touching `engines[0]`. Alternatively, flag-gate the engine writes: set an atomic `stateSaveInProgress` flag, and have `processBlock` skip its `spu94_set_*` calls while the flag is set.
```cpp
// Alternative: serialize from the AudioParameterFloat values directly,
// constructing the text body without touching engines[0]. This requires
// a variant of spu94_preset_save that takes explicit register values
// instead of reading from an engine instance.
```

## Warnings

### WR-01: MorphPanel buttons hold raw pointer to file-scope static LookAndFeel without clearing it in destructor

**File:** `src/plugin/MorphPanel.cpp:67,167` and `src/plugin/MorphPanel.h:10`
**Issue:** `gritIntButton` and `gritFractButton` have `setLookAndFeel(&gritLookAndFeel)` called, where `gritLookAndFeel` is a file-scope static. The MorphPanel destructor is `= default` and does not call `setLookAndFeel(nullptr)` on either button. JUCE fires a `jassert` in debug builds when a component is destroyed while referencing a custom LookAndFeel, and if the static's destruction order is unfortunate relative to any lingering Component instances (e.g., during plugin unload race conditions), this becomes a use-after-free.
**Fix:** Add a destructor to MorphPanel that clears the LookAndFeel:
```cpp
// In MorphPanel.h, replace ~MorphPanel() override = default; with:
~MorphPanel() override;

// In MorphPanel.cpp:
MorphPanel::~MorphPanel()
{
    gritIntButton.setLookAndFeel(nullptr);
    gritFractButton.setLookAndFeel(nullptr);
}
```

### WR-02: StateSerializer assumes little-endian byte order without runtime check or explicit serialization

**File:** `src/plugin/StateSerializer.h:74`
**Issue:** The `bodyLen` field (uint32_t) and float appendix are written via `dest.append(&bodyLen, 4)` and `dest.append(appendix, kFloatAppendixSize)` using native byte order. The comment on line 74 says "LE on all target platforms (x86, ARM64)" which is true today, but the format has no endianness marker. If the project ever targets a big-endian platform (or if a state file is transferred between platforms), the binary container will be silently misinterpreted. IEEE 754 float byte order also varies with platform endianness.
**Fix:** Either (a) add a static_assert or runtime check enforcing LE, or (b) serialize integers and floats with explicit byte-order writes:
```cpp
// Option (a): compile-time guard
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
              "StateSerializer assumes little-endian byte order");
```

### WR-03: Dual source of truth -- morphPosition/morphSpeed/morphGrit atomics shadow AudioParameterFloat values

**File:** `src/plugin/PluginProcessor.h:221-241` and `src/plugin/PluginProcessor.cpp:914-918`
**Issue:** The atomics `morphPosition`, `morphSpeed`, and `morphGrit` are still declared and written to (e.g., in `setStateInformation` lines 914-918, and in `processBlock` line 335 reads `morphPosition` to set the param). Phase 24 moved the source of truth to the AudioParameterFloat instances, but these atomics remain as a parallel store that is written in some paths and read in others. This creates a risk of divergence: if a future code change writes to the atomic but forgets to update the param (or vice versa), the audio thread and GUI will see different values. The `morphPosition` atomic is still read at line 335 (file-preset load path) and then pushed to the param, but the param is ALSO set directly at line 925 from `setStateInformation`.
**Fix:** Remove the redundant atomic stores in `setStateInformation` (lines 914-918) since the AudioParameterFloat values are set immediately after (lines 923-927), and the audio thread reads from the params. Mark the atomics with a comment indicating they are legacy and should not be the write path for new code. Longer term, remove `morphPosition`/`morphSpeed`/`morphGrit` atomics entirely and use the AudioParameterFloat as the single source.

### WR-04: Test coverage gap -- no test for NaN/Inf/out-of-range floats in StateSerializer::load

**File:** `tests/plugin/test_state_serializer.cpp`
**Issue:** The unit tests validate magic, version, truncation, and round-trip, but do not test what happens when the float appendix contains `NaN`, `Inf`, `-Inf`, negative values, or out-of-range values (e.g., `inputGain = -5.0f`). Without such tests, a fix for CR-01 could regress silently.
**Fix:** Add a test that crafts a valid container with pathological float values and verifies they are clamped or rejected:
```cpp
static void test_load_sanitizes_nan_inf()
{
    EngineFixture f;
    CHECK(f.init(), "engine init");

    juce::MemoryBlock block;
    bool ok = StateSerializer::save(
        f.engine, 0.5f, 0.625f, 0.5f, 0.0f, 0.0f, 0.0f, block);
    CHECK(ok, "save ok");

    // Patch float appendix with NaN
    auto* bytes = static_cast<uint8_t*>(block.getData());
    uint32_t bodyLen = 0;
    std::memcpy(&bodyLen, bytes + 5, 4);
    float nan_val = std::numeric_limits<float>::quiet_NaN();
    std::memcpy(bytes + 9 + bodyLen - 24, &nan_val, 4); // inputGain = NaN

    auto result = StateSerializer::load(
        block.getData(), static_cast<int>(block.getSize()));
    // After CR-01 fix: should either reject or clamp
    CHECK(result.ok, "load ok (NaN clamped to fallback)");
    CHECK(!std::isnan(result.inputGain), "inputGain is not NaN");
}
```

## Info

### IN-01: Duplicated engine-sync boilerplate across three call sites

**File:** `src/plugin/PluginProcessor.cpp:386-410` (processBlock), `src/plugin/PluginProcessor.cpp:654-677` (savePresetToString), `src/plugin/PluginProcessor.cpp:857-879` (getStateInformation)
**Issue:** The block of `spu94_set_dry_fader`, `spu94_set_patina_fader`, etc. is copy-pasted three times with identical logic. This makes it easy for a future change to update one copy and miss the others, which is how CR-02-type bugs compound.
**Fix:** Extract a private helper `syncParamsToEngine(spu94_state* engine)` that performs all the `spu94_set_*` calls, and call it from all three sites.

### IN-02: PresetSnapshot baseline defaults in PluginEditor.h do not match actual processor defaults

**File:** `src/plugin/PluginEditor.h:90-95`
**Issue:** The `PresetSnapshot` struct has default field values (`inputGain = 0.25f`, `dry = 1.0f`, `adpcmSend = 0.0f`, `drySend = 1.0f`, `dac = false`) that do not match the processor's actual defaults (`inputLevel = 0.5f`, `dryLevel = 0.0f`, `adpcmSend = 1.0f`, `drySend = 0.0f`, `dacEnabled = true`). These defaults are overwritten by `captureBaseline()` before they are ever compared, so this does not cause a bug. But if `captureBaseline()` were ever skipped, `checkModified()` would always report true.
**Fix:** Align the struct default values with the processor's actual defaults to prevent latent bugs:
```cpp
float inputGain = 0.5f;
float dry = 0.0f, patina = 0.0f, reverb = 1.0f;
float adpcmSend = 1.0f, drySend = 0.0f;
bool dac = true;
```

---

_Reviewed: 2026-05-12T15:33:11Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
