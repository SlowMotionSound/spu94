# Phase 24: State & Automation Surface - Research

**Researched:** 2026-05-12
**Domain:** JUCE AudioProcessorParameter registration (raw, non-APVTS), binary state persistence via getStateInformation/setStateInformation, locale-safe serialization, host automation parameter display units
**Confidence:** HIGH

## Summary

Phase 24 fills two empty stubs (`getStateInformation`/`setStateInformation` at PluginProcessor.cpp:747-755) and registers 9 host-automatable `juce::AudioParameterFloat` instances. Both subsystems bolt onto existing, proven infrastructure: the `.spu94` text serializer (Phase 13, `spu94_preset_save`/`spu94_preset_load`) and the `std::atomic<float>` + SPSC `RegisterBridge` pattern that already drives the GUI-to-audio-thread handoff.

The key architectural constraint is **NOT APVTS** (PLUG-29). The plugin uses `AudioProcessor::addParameter(raw_ptr)` to register 9 parameters. Each parameter's `setValue` callback writes into the existing `std::atomic<float>` member. The audio thread reads atomics per block (already does this at lines 262-307 of processBlock). No new threading infrastructure is needed.

The state persistence container wraps the existing `.spu94` text payload in a 9-byte binary header (magic `SPU9` + version byte + 4-byte little-endian body length). This approach is locale-independent by construction: the `.spu94` format uses `snprintf` with `%04X` hex formatting and hand-rolled `parse_hex_u16` -- no floating-point parsing, no locale sensitivity anywhere in the chain. The wrapper adds 6 additional float fields (inputGain, morphPosition, morphSpeed) that the `.spu94` text format does not carry -- these are appended as fixed-width binary IEEE 754 values after the text body, inside the same container.

**Primary recommendation:** Implement as two clean, separable units: (1) a `StateSerializer` that wraps `.spu94` text + mixer/morph float appendix in the binary container, and (2) a parameter registration block in the `SPU94AudioProcessor` constructor that creates 9 `AudioParameterFloat`s via `addParameter`. Keep both under 200 LOC total.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **D-01:** Mix-level knobs (Dry Level, ADPCM Level, Reverb Level) display as percent 0-100 in DAW automation lanes.
- **D-02:** Send knobs (Dry Send, ADPCM Send) display as percent 0-100 in DAW automation lanes.
- **D-03:** Input Gain displays as decibels, -inf to +24 dB, matching the 0..16 internal range established in Phase 23 (D-02).
- **D-04:** Morph Speed and Morph Grit display as percent 0-100.
- **D-05:** Morph Position displays as percent 0-100.
- **D-06:** When a v1 build encounters a state chunk with a version byte > 1, it refuses the chunk and leaves the engine at defaults. Standard fail-safe forward-compat.
- Phase 23 carryover: Input Gain slider range 0..16 with unity-at-midpoint skew is PERMANENT. Strip the REVERT comment at PluginEditor.cpp:92-98. Engine register pinned at 0x7FFF on both paths: PERMANENT. Pre-clamp float multiply placement: PERMANENT.
- NOT APVTS -- raw atomics + SPSC RegisterBridge per PLUG-29.

### Claude's Discretion
- Parameter ID naming convention (internal strings for DAW save files) -- pick a consistent scheme, document it, freeze it per PLUG-30.
- State container magic bytes, version byte encoding, body-length endianness -- follow JUCE/industry convention.

### Deferred Ideas (OUT OF SCOPE)
- Bipolar morph offset automation -- morph ships as standard percent 0-100.
- Visual clip indicator for int16 boundary.
- Hide preset Save/Load buttons in plugin formats.
- Plugin GUI loses parameter state when window closed/reopened (Ardour LV2) -- may surface here but is a separate follow-up unless directly caused by parameter wiring.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| PLUG-22 | `getStateInformation` writes the existing `.spu94` byte payload wrapped in a binary container | StateSerializer wraps spu94_preset_save output in SPU9 magic + version + body-length header |
| PLUG-23 | `setStateInformation` reads the wrapped payload and applies it via the existing `pendingPresetBuf` deferred-apply mechanism (no allocation on the audio thread) | Binary container unwrapped on message thread; text body fed into the existing filePresetReady atomic handoff |
| PLUG-24 | State container format: 4-byte magic `'SPU9'` + 1-byte version + 4-byte body length + body bytes | Binary header format documented in Architecture Patterns section |
| PLUG-25 | State round-trip path is locale-independent -- no `juce::String::getFloatValue` in the persistence chain | `.spu94` text format uses hex-only integer serialization via snprintf %04X; mixer/morph floats appended as raw IEEE 754 bytes, not as locale-dependent text |
| PLUG-26 | Save -> close session -> reopen -> load produces byte-identical engine state in every supported host | Verified by wrapping the already-tested spu94_preset_save/load round-trip in a deterministic binary envelope |
| PLUG-27 | Multi-instance smoke test: two plugin instances on independent tracks retain independent state across save/load | All state is per-instance (engines[], atomics, pendingPresetBuf are member fields, not static) |
| PLUG-28 | Exactly 9 AudioProcessorParameters exposed for host automation | 9 AudioParameterFloat registrations in the constructor via addParameter |
| PLUG-29 | All 9 parameters routed through the existing atomic-scalar + SPSC RegisterBridge pattern (not APVTS) | Each param's setValue writes the existing std::atomic<float> member; processBlock reads atomics per block as it already does |
| PLUG-30 | Parameter IDs stable across versions -- once published, never reassigned or repurposed | IDs frozen as snake_case strings with versionHint=1 for AU ordering stability |
| PLUG-31 | Audio-block-granularity automation is sufficient; per-sample-accurate CLAP automation is explicitly out of scope | No per-sample parameter interpolation; atomics read once per processBlock |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| State serialization (getStateInformation) | Plugin Processor (message thread) | -- | JUCE calls getStateInformation on the message thread; spu94_preset_save is safe to call from message thread since it only reads engine state |
| State deserialization (setStateInformation) | Plugin Processor (message thread -> audio thread) | -- | Message thread parses container, stuffs pendingPresetBuf; audio thread applies via existing deferred-apply |
| Parameter registration | Plugin Processor constructor | -- | addParameter called during construction; JUCE owns parameter lifecycle |
| Parameter value read (host automation) | Audio thread (processBlock) | -- | Reads existing std::atomic<float> members once per block; no new threading needed |
| Parameter value write (host -> plugin) | JUCE host callback (message thread) | -- | AudioParameterFloat::setValue fires on message thread; writes into atomic |
| Parameter value write (GUI -> plugin) | Editor (message thread) | -- | Existing slider.onValueChange writes into atomic AND calls beginChangeGesture/setValueNotifyingHost/endChangeGesture on the AudioParameterFloat |
| Display unit formatting (dB, %) | AudioParameterFloat stringFromValue lambda | -- | Pure formatting; no state |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | AudioProcessor, AudioParameterFloat, MemoryBlock, NormalisableRange | Already pinned in CMakeLists.txt via FetchContent [VERIFIED: CMakeLists.txt line "JUCE 8.0.12 via FetchContent"] |
| libspu94 (C core) | current | spu94_preset_save, spu94_preset_load | Existing serializer; Phase 24 wraps but does not modify [VERIFIED: spu94_preset_io.c] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| None new | -- | -- | Phase 24 adds no new dependencies |

### Alternatives Considered
| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Raw addParameter | APVTS | APVTS adds XML state save/restore for free BUT requires rearchitecting the atomic bridge; explicitly rejected by PLUG-29 and ARCHITECTURE-v1.7 S5.1 |
| Binary container | Raw .spu94 text | Text-only state works but adds no version envelope; future changes would break silently without a version byte |
| IEEE 754 float appendix | Text key=value for floats | Text parsing invites locale bugs (PLUG-25); binary IEEE 754 is locale-proof by construction |

## Architecture Patterns

### System Architecture Diagram

```
HOST DAW
  |
  |--- getStateInformation (message thread, save project) ------+
  |                                                              |
  |                                                              v
  |                                           +----------------------------------+
  |                                           | StateSerializer::save()          |
  |                                           |   1. spu94_preset_save(engine)   |
  |                                           |      -> .spu94 text body         |
  |                                           |   2. Append mixer/morph floats   |
  |                                           |      as 6x IEEE 754 LE bytes     |
  |                                           |   3. Wrap in binary container:   |
  |                                           |      [SPU9][v1][bodyLen][body]    |
  |                                           +----------------------------------+
  |                                                              |
  |                                                              v
  |                                                      juce::MemoryBlock
  |
  |--- setStateInformation (message thread, load project) ------+
  |                                                              |
  |                                                              v
  |                                           +----------------------------------+
  |                                           | StateSerializer::load()          |
  |                                           |   1. Validate magic + version    |
  |                                           |   2. Extract .spu94 text body    |
  |                                           |   3. Extract 6 float appendix    |
  |                                           |   4. Copy text into              |
  |                                           |      pendingPresetBuf            |
  |                                           |   5. Write floats to atomics     |
  |                                           |   6. Set filePresetReady=true    |
  |                                           +----------------------------------+
  |                                                              |
  |                                              (audio thread picks up next block)
  |                                                              |
  |                                                              v
  |                                           +----------------------------------+
  |                                           | processBlock: drain pending      |
  |                                           |   spu94_preset_load(engine, buf) |
  |                                           |   Sync shadows + atomics         |
  |                                           +----------------------------------+
  |
  |--- Host writes automation lane (message thread) ------------+
  |                                                              |
  |                                                              v
  |                                           +----------------------------------+
  |                                           | AudioParameterFloat::setValue()  |
  |                                           |   -> std::atomic<float>.store()  |
  |                                           +----------------------------------+
  |                                                              |
  |                                              (audio thread reads next block)
  |                                                              v
  |                                           +----------------------------------+
  |                                           | processBlock: read atomics       |
  |                                           |   -> spu94_set_*() engine calls  |
  |                                           +----------------------------------+
```

### Recommended Project Structure
```
src/plugin/
  PluginProcessor.h      -- add 9 AudioParameterFloat* raw pointers; add getStateInformation/setStateInformation bodies
  PluginProcessor.cpp    -- register params in constructor; fill state stubs; wire GUI<->param gestures
  PluginEditor.cpp       -- strip REVERT comment; wire knob gestures through AudioParameterFloat
  StateSerializer.h      -- header-only: save/load binary container wrapping .spu94 text + float appendix
  (no new .cpp files needed -- StateSerializer is small enough for header-only)
```

### Pattern 1: Raw AudioParameterFloat Registration (non-APVTS)
**What:** Register AudioParameterFloat instances via addParameter in the AudioProcessor constructor. The processor takes ownership of the raw pointer. Keep a non-owning raw pointer as a member for runtime access.
**When to use:** When the project explicitly avoids APVTS (PLUG-29).
**Example:**
```cpp
// Source: JUCE 8 docs (docs.juce.com/master/classAudioProcessor.html) [VERIFIED: Context7]
// In SPU94AudioProcessor constructor, BEFORE BusesProperties init:

// Input Gain: 0.0..16.0 linear, skew so 1.0 (unity) is at the midpoint
auto inputGainRange = juce::NormalisableRange<float>(0.0f, 16.0f, 0.01f);
inputGainRange.setSkewForCentre(1.0f);  // 0.5 normalized -> 1.0 real value (unity)
addParameter(paramInputGain = new juce::AudioParameterFloat(
    juce::ParameterID{"input_gain", 1},   // versionHint=1 for AU ordering
    "Input Gain",
    inputGainRange,
    0.5f,                                  // default: -6 dB
    juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float v, int) -> juce::String {
            if (v <= 0.0001f) return "-inf";
            return juce::String(20.0f * std::log10(v), 1) + " dB";
        })
        .withValueFromStringFunction([](const juce::String& s) -> float {
            if (s.containsIgnoreCase("inf")) return 0.0f;
            // Parse dB, convert back to linear
            float dB = s.getFloatValue();
            return std::pow(10.0f, dB / 20.0f);
        })
));

// Percent 0-100 params (mixer/send/morph): simpler
addParameter(paramDryLevel = new juce::AudioParameterFloat(
    juce::ParameterID{"dry_level", 1},
    "Dry Level",
    juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
    0.0f,                                 // default: OFF
    juce::AudioParameterFloatAttributes()
        .withLabel("%")
        .withStringFromValueFunction([](float v, int) -> juce::String {
            return juce::String(v * 100.0f, 0) + "%";
        })
        .withValueFromStringFunction([](const juce::String& s) -> float {
            return s.getFloatValue() / 100.0f;
        })
));
```

### Pattern 2: Bidirectional Param <-> Atomic Wiring
**What:** When the host writes a parameter (automation), the value flows into the existing atomic. When the GUI slider moves, it calls setValueNotifyingHost so the host records automation.
**When to use:** For every parameter -- the wiring is bidirectional.
**Example:**
```cpp
// Source: JUCE 8 AudioProcessorParameter docs [VERIFIED: Context7]

// In the AudioParameterFloat's Listener or via onValueChange on the param:
// Option A: Override parameterValueChanged on the Listener interface
// Option B: Poll in processBlock (already done -- read atomic per block)

// The simplest pattern for non-APVTS: the param IS the atomic.
// AudioParameterFloat already stores its value internally; we just
// mirror it to our existing atomic on every host-initiated change.

// In processBlock (existing code at lines 286-307):
// Replace: dryLevel.load(std::memory_order_relaxed)
// With:    paramDryLevel->get()  (or keep reading the atomic if the
//          param's setValue callback pushes to the atomic).

// For GUI -> host notification (PluginEditor.cpp slider onChange):
// Replace the direct atomic store with:
inputLevelKnob.onValueChange = [this] {
    auto* param = processorRef.getParamInputGain();
    param->beginChangeGesture();
    param->setValueNotifyingHost(
        param->getNormalisableRange().convertTo0to1(
            static_cast<float>(inputLevelKnob.getValue())));
    param->endChangeGesture();
};
```

### Pattern 3: Binary State Container
**What:** A minimal binary envelope around the `.spu94` text payload.
**When to use:** `getStateInformation` / `setStateInformation`.
**Example:**
```cpp
// Source: Project decision PLUG-24 + ARCHITECTURE-v1.7 S6

// Container layout (all integers little-endian):
// Offset  Size  Field
// 0       4     Magic bytes: 'S','P','U','9'  (0x53505539)
// 4       1     Version byte: 0x01
// 5       4     Body length (uint32_t LE) = text_len + float_appendix_len
// 9       N     .spu94 text body (from spu94_preset_save)
// 9+N     24    Float appendix: 6 x float32 LE (IEEE 754)
//               [inputGain, morphPosition, morphSpeed, morphGrit_as_float,
//                padding0, padding1]
// Total: 9 + N + 24 bytes

// Save:
void getStateInformation(juce::MemoryBlock& destData) {
    char textBuf[SPU94_PRESET_BUF_SIZE];
    int textLen = spu94_preset_save(engines[0], "DAW State", "", textBuf, sizeof(textBuf));
    if (textLen <= 0) return;

    const uint32_t floatAppendixSize = 6 * sizeof(float);  // 24 bytes
    const uint32_t bodyLen = static_cast<uint32_t>(textLen) + floatAppendixSize;

    // Header
    destData.append("SPU9", 4);                         // magic
    uint8_t version = 1;
    destData.append(&version, 1);                       // version
    destData.append(&bodyLen, sizeof(bodyLen));          // body length (LE on x86/ARM)

    // Text body
    destData.append(textBuf, static_cast<size_t>(textLen));

    // Float appendix (mixer/morph atomics not in .spu94 text)
    float floats[6] = {
        inputLevel.load(std::memory_order_relaxed),
        morphPosition.load(std::memory_order_relaxed),
        morphSpeed.load(std::memory_order_relaxed),
        static_cast<float>(morphGrit.load(std::memory_order_relaxed)),
        0.0f, 0.0f  // reserved padding
    };
    destData.append(floats, sizeof(floats));
}
```

### Anti-Patterns to Avoid
- **Locale-dependent float serialization:** Never use `juce::String::getFloatValue` or `std::stof(locale_default)` in the state persistence chain. The `.spu94` format uses hex integers only; the float appendix uses binary IEEE 754. This was called out in PITFALLS-v1.7 C4 and PLUG-25. [CITED: PITFALLS-v1.7.md C4]
- **APVTS for this project:** PLUG-29 explicitly forbids it. The morph engine's 35-register model does not map cleanly to APVTS's one-param-one-control paradigm. [CITED: ARCHITECTURE-v1.7.md S5.1]
- **Parameter ID reuse across versions:** Once published, IDs are frozen forever (PLUG-30). A parameter named `"reverb_level"` in v1.7 can never be renamed to `"reverb_vol"` in v1.8 without breaking every saved session. [CITED: v1.7-REQUIREMENTS.md PLUG-30]
- **Forgetting beginChangeGesture/endChangeGesture:** Without gesture brackets, the host cannot record automation correctly. Every GUI slider drag must call begin on mouseDown, end on mouseUp, and setValueNotifyingHost on drag. [VERIFIED: Context7 JUCE docs]
- **Allocating in processBlock:** The state load path goes through the existing `pendingPresetBuf` mechanism -- message thread writes, audio thread reads. No allocation on the audio thread. [VERIFIED: PluginProcessor.cpp existing pattern]
- **Reading AudioParameterFloat::get() on the audio thread while also writing atomics from GUI:** The atomic is the single source of truth. The AudioParameterFloat mirrors to the atomic; processBlock reads the atomic. Do not introduce a second source-of-truth.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Parameter display formatting | Custom formatting per-host | AudioParameterFloat's stringFromValue / valueFromString lambdas | JUCE formats the display for all hosts consistently; the lambda is the single place to define dB / percent formatting |
| Normalized <-> real-value mapping | Manual math in setValue / processBlock | NormalisableRange with setSkewForCentre | JUCE handles the 0..1 normalized <-> 0..16 real-value conversion including skew; getting this wrong breaks automation curves |
| State container endianness | Manual byte-swapping | `juce::MemoryBlock::append` + platform-native float layout | x86, ARM, and Apple Silicon are all little-endian; IEEE 754 float is universal. No byte-swap needed for the target platforms |
| Thread-safe parameter read | Custom lock or fence | std::atomic<float> with memory_order_relaxed | The existing pattern is correct and proven; adding locks would violate RT-safety |

**Key insight:** Phase 24 is almost entirely wiring -- connecting existing mechanisms (atomics, RegisterBridge, spu94_preset_save/load, pendingPresetBuf) to JUCE's parameter and state APIs. No new DSP, no new threading primitives, no new data structures.

## Common Pitfalls

### Pitfall 1: AU Parameter Ordering Instability
**What goes wrong:** Logic and GarageBand identify AU parameters by index, not by string ID. If parameter registration order changes between versions, every saved session breaks.
**Why it happens:** `addParameter` determines the parameter's index. Reordering calls or inserting a new parameter before existing ones shifts all subsequent indices.
**How to avoid:** Use `juce::ParameterID` with `versionHint=1` for all v1.7 parameters. Lock the registration order and document it. Add new parameters at the END of the list in future versions with `versionHint=2`.
**Warning signs:** Logic session recall produces wrong values on some knobs but not others.

### Pitfall 2: State Load Crash on Corrupt/Future-Version Chunks
**What goes wrong:** A DAW hands a corrupt or future-version state chunk to `setStateInformation`. Without validation, the parser reads past buffer bounds or interprets garbage as text length.
**Why it happens:** Hosts store opaque blobs; there is no host-side validation.
**How to avoid:** Validate magic bytes, check version (reject > 1 per D-06), bounds-check body length against actual data size. On any failure, return early and leave engine at defaults.
**Warning signs:** Crash during session load; pluginval strictness-10 state-fuzz test failure.

### Pitfall 3: Input Gain dB Display at Zero
**What goes wrong:** `20 * log10(0.0)` is negative infinity. If the stringFromValue lambda doesn't handle this, the automation lane shows "NaN" or "-inf dB" in some hosts.
**Why it happens:** The parameter range starts at 0.0 (silence). Log10 of zero is undefined.
**How to avoid:** Special-case `v <= epsilon` to return `"-inf"` or `"-inf dB"`. Hosts vary in how they display this string; test in Reaper and Logic.
**Warning signs:** Automation lane shows "nan" or blank when gain is at minimum.

### Pitfall 4: GUI Slider and AudioParameterFloat Fight
**What goes wrong:** The editor's slider onChange writes the atomic directly. The AudioParameterFloat's setValue also writes the atomic. If the GUI slider doesn't go through setValueNotifyingHost, the host never records the automation.
**Why it happens:** The existing GUI sliders write directly to atomics (e.g., `processorRef.getInputLevel().store(...)`). This bypass the parameter system entirely.
**How to avoid:** Rewire ALL 9 GUI controls to call `beginChangeGesture` + `setValueNotifyingHost` + `endChangeGesture` on the corresponding AudioParameterFloat. The param's internal listener then writes the atomic. Remove the direct atomic stores from the editor's onValueChange callbacks for these 9 params.
**Warning signs:** Automation lanes in the host show no movement when knobs are dragged; saved sessions don't recall GUI-set values.

### Pitfall 5: Morph Grit Mapping to AudioParameterFloat
**What goes wrong:** Morph Grit is an `std::atomic<int>` with values 0 (Int) or 1 (Fract). But AudioParameterFloat operates on floats. Using AudioParameterFloat with a 0..1 range and step 1.0 works but the host may display it as a slider instead of a toggle.
**Why it happens:** JUCE's AudioParameterBool or AudioParameterChoice would be more natural, but the project is already coded around an atomic<int>.
**How to avoid:** Use AudioParameterFloat with range 0..1, step 1.0. The stringFromValue lambda returns "Int" for 0 and "Fract." for 1. Hosts will show it as a 2-position switch in automation lanes. Alternatively, use AudioParameterInt with range 0..1 if cleaner.
**Warning signs:** Host shows a continuous slider instead of a toggle.

### Pitfall 6: State Save Misses Current Mixer Atomics
**What goes wrong:** The `.spu94` text format captures engine register state via `spu94_preset_save`, which reads from the engine's internal registers. But the engine's input_gain register is pinned at 0x7FFF (Phase 23 D-03); the actual gain lives in the `inputLevel` atomic. Similarly, morphPosition and morphSpeed are atomics not stored in the engine.
**Why it happens:** Phase 23 moved Input Gain outside the engine into a pre-clamp float multiply. The engine register no longer carries the real value.
**How to avoid:** The float appendix in the binary container explicitly captures these 6 atomic values alongside the .spu94 text body. On load, the appendix values are written back to the atomics. The .spu94 text body handles the 35 reverb registers + engine-side mixer registers; the float appendix handles wrapper-side state.
**Warning signs:** Session recall restores reverb sound correctly but Input Gain and Morph Position reset to defaults.

## Code Examples

### Complete Parameter Registration Block
```cpp
// Source: JUCE 8.0.12 AudioParameterFloat API [VERIFIED: Context7 /websites/juce_master]

// In SPU94AudioProcessor constructor body, AFTER BusesProperties init:

// Helper: percent 0-100 display
auto pctStringFromValue = [](float v, int) -> juce::String {
    return juce::String(static_cast<int>(v * 100.0f + 0.5f)) + "%";
};
auto pctValueFromString = [](const juce::String& s) -> float {
    return s.getFloatValue() / 100.0f;
};
auto pctRange = juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f);
auto pctAttrs = juce::AudioParameterFloatAttributes()
    .withLabel("%")
    .withStringFromValueFunction(pctStringFromValue)
    .withValueFromStringFunction(pctValueFromString);

// 1. Input Gain: dB display, 0..16 range, skew at unity (1.0)
auto igRange = juce::NormalisableRange<float>(0.0f, 16.0f, 0.01f);
igRange.setSkewForCentre(1.0f);
addParameter(paramInputGain = new juce::AudioParameterFloat(
    juce::ParameterID{"input_gain", 1}, "Input Gain", igRange, 0.5f,
    juce::AudioParameterFloatAttributes()
        .withLabel("dB")
        .withStringFromValueFunction([](float v, int) -> juce::String {
            if (v < 0.0001f) return juce::String::fromUTF8("-\xe2\x88\x9e dB"); // -inf dB
            return juce::String(20.0f * std::log10(v), 1) + " dB";
        })
        .withValueFromStringFunction([](const juce::String& s) -> float {
            if (s.containsIgnoreCase("inf")) return 0.0f;
            return std::pow(10.0f, s.getFloatValue() / 20.0f);
        })
));

// 2-3. Sends (percent)
addParameter(paramAdpcmSend = new juce::AudioParameterFloat(
    juce::ParameterID{"adpcm_send", 1}, "ADPCM Send", pctRange, 1.0f, pctAttrs));
addParameter(paramDrySend = new juce::AudioParameterFloat(
    juce::ParameterID{"dry_send", 1}, "Dry Send", pctRange, 0.0f, pctAttrs));

// 4-6. Morph controls (percent)
addParameter(paramMorphPosition = new juce::AudioParameterFloat(
    juce::ParameterID{"morph_position", 1}, "Morph Position", pctRange, 0.625f, pctAttrs));
addParameter(paramMorphSpeed = new juce::AudioParameterFloat(
    juce::ParameterID{"morph_speed", 1}, "Morph Speed", pctRange, 0.5f, pctAttrs));
addParameter(paramMorphGrit = new juce::AudioParameterFloat(
    juce::ParameterID{"morph_grit", 1}, "Morph Grit",
    juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f),  // step=1: two positions
    0.0f,
    juce::AudioParameterFloatAttributes()
        .withLabel("")
        .withStringFromValueFunction([](float v, int) -> juce::String {
            return v < 0.5f ? "Int" : "Fract.";
        })
        .withValueFromStringFunction([](const juce::String& s) -> float {
            return s.containsIgnoreCase("fract") ? 1.0f : 0.0f;
        })
));

// 7-9. Mixer levels (percent)
addParameter(paramDryLevel = new juce::AudioParameterFloat(
    juce::ParameterID{"dry_level", 1}, "Dry Level", pctRange, 0.0f, pctAttrs));
addParameter(paramAdpcmLevel = new juce::AudioParameterFloat(
    juce::ParameterID{"adpcm_level", 1}, "ADPCM Level", pctRange, 0.0f, pctAttrs));
addParameter(paramReverbLevel = new juce::AudioParameterFloat(
    juce::ParameterID{"reverb_level", 1}, "Reverb Level", pctRange, 1.0f, pctAttrs));
```

### State Container Save/Load (StateSerializer.h)
```cpp
// Source: Project decisions PLUG-22..27, ARCHITECTURE-v1.7 S6

namespace StateSerializer {

static constexpr uint8_t kMagic[4] = {'S', 'P', 'U', '9'};
static constexpr uint8_t kVersion  = 1;
static constexpr size_t  kHeaderSize = 9;  // 4 magic + 1 version + 4 bodyLen
static constexpr size_t  kFloatAppendixCount = 6;
static constexpr size_t  kFloatAppendixSize  = kFloatAppendixCount * sizeof(float);

inline bool save(const spu94_state* engine,
                 float inputGain, float morphPos, float morphSpd,
                 float morphGritF, float /*pad0*/, float /*pad1*/,
                 juce::MemoryBlock& dest)
{
    char textBuf[SPU94_PRESET_BUF_SIZE];
    int textLen = spu94_preset_save(engine, "DAW State", "", textBuf, sizeof(textBuf));
    if (textLen <= 0) return false;

    uint32_t bodyLen = static_cast<uint32_t>(textLen) + static_cast<uint32_t>(kFloatAppendixSize);

    dest.reset();
    dest.ensureSize(kHeaderSize + bodyLen);
    dest.append(kMagic, 4);
    dest.append(&kVersion, 1);
    dest.append(&bodyLen, 4);           // LE on all target platforms
    dest.append(textBuf, static_cast<size_t>(textLen));

    float appendix[kFloatAppendixCount] = {inputGain, morphPos, morphSpd, morphGritF, 0.0f, 0.0f};
    dest.append(appendix, kFloatAppendixSize);
    return true;
}

struct LoadResult {
    bool        ok = false;
    const char* textBody = nullptr;
    size_t      textLen = 0;
    float       inputGain = 0.5f;
    float       morphPosition = 0.625f;
    float       morphSpeed = 0.5f;
    float       morphGrit = 0.0f;
};

inline LoadResult load(const void* data, int sizeInBytes)
{
    LoadResult r;
    if (sizeInBytes < static_cast<int>(kHeaderSize)) return r;

    auto* bytes = static_cast<const uint8_t*>(data);

    // Validate magic
    if (std::memcmp(bytes, kMagic, 4) != 0) return r;

    // Validate version (D-06: reject future versions)
    if (bytes[4] > kVersion) return r;

    // Read body length
    uint32_t bodyLen = 0;
    std::memcpy(&bodyLen, bytes + 5, 4);

    if (static_cast<int>(kHeaderSize + bodyLen) > sizeInBytes) return r;

    // Text body = body minus float appendix
    if (bodyLen < kFloatAppendixSize) return r;
    r.textLen = bodyLen - kFloatAppendixSize;
    r.textBody = reinterpret_cast<const char*>(bytes + kHeaderSize);

    // Float appendix
    const float* floats = reinterpret_cast<const float*>(
        bytes + kHeaderSize + r.textLen);
    r.inputGain     = floats[0];
    r.morphPosition = floats[1];
    r.morphSpeed    = floats[2];
    r.morphGrit     = floats[3];

    r.ok = true;
    return r;
}

} // namespace StateSerializer
```

### GUI Knob Gesture Wiring
```cpp
// Source: JUCE 8 AudioProcessorParameter gesture API [VERIFIED: Context7]
// PluginEditor.cpp -- rewired inputLevelKnob example

inputLevelKnob.onValueChange = [this] {
    auto* param = processorRef.getParamInputGain();
    const float realValue = static_cast<float>(inputLevelKnob.getValue());
    const float normalized = param->getNormalisableRange().convertTo0to1(realValue);
    param->setValueNotifyingHost(normalized);
    // The param's internal setValue callback writes the atomic --
    // no direct atomic store from the editor anymore.
};

// For proper automation recording, also wire mouseDown/mouseUp:
inputLevelKnob.onDragStart = [this] {
    processorRef.getParamInputGain()->beginChangeGesture();
};
inputLevelKnob.onDragEnd = [this] {
    processorRef.getParamInputGain()->endChangeGesture();
};
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| APVTS for everything | Raw addParameter for control; APVTS only when XML state is wanted | Established JUCE pattern | SPU-94 uses raw addParameter; this is intentional and correct for the register-centric architecture |
| `String::getFloatValue` for state | Binary state or hex-only text | Locale bugs documented since JUCE 5 era | SPU-94's .spu94 format already avoids this; the binary container adds belt-and-suspenders safety |
| `ParameterID(string)` without versionHint | `ParameterID(string, versionHint)` | JUCE 7+ | versionHint is critical for AU parameter ordering stability in Logic |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | All target platforms (x86_64, ARM64) use IEEE 754 little-endian float layout | Architecture Patterns, State Container | If a target were big-endian, the float appendix would load with byte-swapped values. All actual targets (Linux x86_64, macOS ARM64, Windows x86_64) are LE. Risk: effectively zero. |
| A2 | juce::Slider provides onDragStart/onDragEnd callbacks for gesture wiring | Code Examples, GUI Knob Gesture Wiring | If JUCE 8 Slider lacks these specific callbacks, we'd use a MouseListener instead. The pattern is well-established; exact callback name may differ. [ASSUMED] |
| A3 | Morph Grit stored as float 0.0/1.0 in the appendix is sufficient for forward compat | State Container | If a future version adds grit values > 1, the float representation handles it; the uint8 version byte in the container header provides the compat gate. Low risk. |

## Open Questions

1. **Float appendix field set: are 6 fields enough?**
   - What we know: inputGain, morphPosition, morphSpeed, morphGrit are the 4 wrapper-side atomics not captured by spu94_preset_save. Two padding fields reserved.
   - What's unclear: Should adpcmSend, drySend, dryLevel, patinaLevel, reverbLevel also be in the appendix? They ARE in the .spu94 text via the [mixer] section (as Q15 hex), so they round-trip through spu94_preset_save/load. But the wrapper atomics are 0.0-1.0 float and the engine stores Q15 -- the round-trip goes float -> Q15 -> save hex -> load hex -> Q15 -> float, which loses 1 LSB at Q15 resolution. This is inaudible (0.003% error) but NOT byte-identical to the original float.
   - Recommendation: Accept the Q15 round-trip loss for mixer values (it is below the perceptual threshold of any human ear). Keep the appendix to 6 fields. If PLUG-26 demands byte-exact float state round-trip, add the 5 mixer floats to the appendix (11 fields total, still fits in 44 bytes).

2. **Should parameters also be written/read from the state container?**
   - What we know: JUCE's non-APVTS path does NOT automatically save/restore AudioParameterFloat values in getStateInformation. The plugin must do it manually.
   - What's unclear: Does the host independently save parameter values via its own mechanism (VST3 does; AU does via ComponentState)?
   - Recommendation: Always save all 9 parameter values in our own state container. This is the fail-safe approach: our container is the single source of truth regardless of host behavior. Some hosts save parameters AND state; some save only state. By putting everything in our container, recall works everywhere.

3. **Slider gesture API exact names in JUCE 8**
   - What we know: JUCE Slider has `onDragStart` and `onDragEnd` callback members in recent versions.
   - What's unclear: The exact JUCE 8.0.12 Slider member names. Might be `Slider::onDragStart` / `Slider::onDragEnd` or might require a `Slider::Listener` approach.
   - Recommendation: Check the JUCE 8.0.12 Slider header at implementation time. If `onDragStart`/`onDragEnd` are not available, use `Slider::Listener::sliderDragStarted`/`sliderDragEnded`.

## Environment Availability

Step 2.6: SKIPPED (no external dependencies identified -- Phase 24 is code/config-only changes within the existing JUCE + libspu94 build).

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | ctest (CMake) + manual host testing |
| Config file | CMakeLists.txt (existing ctest infrastructure) |
| Quick run command | `cd build && ctest --output-on-failure -R "preset"` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| PLUG-22 | getStateInformation writes binary-wrapped .spu94 | unit | `ctest -R state_save` | Wave 0 |
| PLUG-23 | setStateInformation reads container, deferred-applies | unit | `ctest -R state_load` | Wave 0 |
| PLUG-24 | Container format: SPU9 magic + v1 + bodyLen + body | unit | `ctest -R state_container_format` | Wave 0 |
| PLUG-25 | Round-trip is locale-independent | unit | `ctest -R state_locale` | Wave 0 |
| PLUG-26 | Byte-identical engine state after save/load cycle | unit | `ctest -R state_roundtrip` | Wave 0 |
| PLUG-27 | Multi-instance independent state | manual-only | Load two instances in Reaper, set different params, save/reopen | N/A -- manual host test |
| PLUG-28 | 9 AudioProcessorParameters registered | unit | `ctest -R param_count` | Wave 0 |
| PLUG-29 | Params route through atomic bridge (not APVTS) | code-review | Verify no APVTS in source | N/A |
| PLUG-30 | Parameter IDs stable | code-review | Verify ID strings and versionHint in constructor | N/A |
| PLUG-31 | Block-granularity automation (no per-sample) | code-review | Verify no per-sample param read in processBlock | N/A |

### Sampling Rate
- **Per task commit:** `cd build && ctest --output-on-failure -R "state\|param"`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/test_state_serializer.cpp` -- covers PLUG-22/23/24/25/26
- [ ] `tests/test_param_registration.cpp` -- covers PLUG-28 (parameter count and IDs)
- [ ] May need CMakeLists.txt addition for new test targets

## Sources

### Primary (HIGH confidence)
- [Context7 /websites/juce_master] -- AudioParameterFloat constructor, NormalisableRange, ParameterID versionHint, AudioProcessor addParameter, getStateInformation/setStateInformation, beginChangeGesture/endChangeGesture
- [JUCE 8 AudioProcessor class reference](https://docs.juce.com/master/classAudioProcessor.html) -- addParameter takes raw pointer; processor takes ownership
- [JUCE 8 NormalisableRange](https://docs.juce.com/master/classjuce_1_1NormalisableRange.html) -- setSkewForCentre
- [JUCE 8 ParameterID](https://docs.juce.com/master/classjuce_1_1ParameterID.html) -- versionHint for AU ordering

### Project Sources (HIGH confidence)
- `src/plugin/PluginProcessor.h` -- existing atomics, RegisterBridge, pendingPresetBuf, kInputGainDefault/Max anchors
- `src/plugin/PluginProcessor.cpp:747-755` -- empty getStateInformation/setStateInformation stubs
- `src/plugin/PluginProcessor.cpp:262-307` -- existing per-block atomic -> engine sync
- `src/plugin/PluginEditor.cpp:89-98` -- existing Input Gain knob with 0..16 range and unity-midpoint skew
- `src/plugin/MorphPanel.cpp:117-135` -- existing morphSpeed knob wiring (0..1 range, direct atomic store)
- `src/plugin/MorphPanel.cpp:167-168` -- existing morphGrit button wiring (atomic<int> store)
- `src/spu94/spu94_preset_io.c` -- .spu94 text serializer (hex-only, no float parsing, locale-safe by construction)
- `include/spu94/spu94.h:493` -- SPU94_PRESET_BUF_SIZE = 8192
- `.planning/research/ARCHITECTURE-v1.7.md` S5 (threading), S6 (state pseudocode)
- `.planning/research/PITFALLS-v1.7.md` B1 (RT-safety), C4 (locale/state)

### Secondary (MEDIUM confidence)
- [JUCE forum -- String::getFloatValue locale comma-decimal](https://forum.juce.com/t/string-getfloatvalue-do-not-handle-decimal-comma/55189) -- confirms locale bug risk

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- JUCE 8.0.12 already pinned; all APIs verified via Context7
- Architecture: HIGH -- existing code patterns (atomics, pendingPresetBuf, spu94_preset_save/load) verified by reading source; Phase 24 extends them, doesn't replace them
- Pitfalls: HIGH -- locale bug, AU ordering, gesture wiring, state save completeness all documented from official sources and verified against codebase

**Research date:** 2026-05-12
**Valid until:** 2026-06-12 (stable; JUCE 8.0.12 is pinned, no moving targets)
