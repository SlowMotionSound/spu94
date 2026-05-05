# Phase 23: GUI Overlay + Display - Pattern Map

**Mapped:** 2026-05-05
**Files analyzed:** 9 (2 new + 7 modified)
**Analogs found:** 9 / 9

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `src/standalone/MacroPanel.h` | component | request-response | `src/standalone/RegisterPanel.h` | exact |
| `src/standalone/MacroPanel.cpp` | component | request-response | `src/standalone/RegisterPanel.cpp` + `PluginEditor.cpp` | exact |
| `src/standalone/PluginEditor.h` | component | request-response | self (existing) | exact |
| `src/standalone/PluginEditor.cpp` | component | request-response | self (existing) | exact |
| `src/standalone/PluginProcessor.h` | service | request-response | self (existing) | exact |
| `src/standalone/PluginProcessor.cpp` | service | request-response | self (existing, tempo sync section) | exact |
| `src/standalone/RegisterPanel.h` | component | request-response | self (existing) | exact |
| `src/standalone/RegisterPanel.cpp` | component | request-response | self (existing) | exact |
| `src/standalone/ParameterBridge.h` | service | request-response | self (existing) | exact |

## Pattern Assignments

### `src/standalone/MacroPanel.h` (NEW component, request-response)

**Analog:** `src/standalone/RegisterPanel.h`

**Imports pattern** (RegisterPanel.h lines 1-6):
```cpp
#pragma once

#include <JuceHeader.h>
#include "ParameterBridge.h"
#include <array>
#include <functional>
```

MacroPanel.h needs: `<JuceHeader.h>`, forward-declare `SPU94AudioProcessor`, include `<array>`. Does NOT need `ParameterBridge.h` directly -- communicates through processor atomics, not RegisterBridge.

**Custom slider subclass pattern** (RegisterPanel.h lines 8-19):
```cpp
class QuantizedSlider : public juce::Slider
{
public:
    std::function<double(double)> snapFunction;

    double snapValue(double attemptedValue, DragMode) override
    {
        if (snapFunction)
            return snapFunction(attemptedValue);
        return attemptedValue;
    }
};
```
Reuse `QuantizedSlider` for bipolar center detent (Decay, Reflectivity). Import from `RegisterPanel.h` or move to shared header.

**Component class declaration pattern** (RegisterPanel.h lines 25-73):
```cpp
class RegisterPanel : public juce::Component
{
public:
    explicit RegisterPanel(RegisterBridge& bridge);
    ~RegisterPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void updateFromShadows();
    int getPreferredHeight() const;

private:
    RegisterBridge& bridge;

    std::array<QuantizedSlider, SPU94_REG__COUNT> sliders;
    std::array<juce::Label, SPU94_REG__COUNT> labels;

    // Group header labels for visual organization.
    juce::Label headerMasterIO{"", "Master I/O"};
    // ... more headers per section

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RegisterPanel)
};
```
MacroPanel follows identical structure: `public juce::Component`, constructor takes `SPU94AudioProcessor&` reference, override `paint`/`resized`, own all child knobs+labels+toggles+dropdowns as member arrays or named members, section header labels, `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR`.

---

### `src/standalone/MacroPanel.cpp` (NEW component, request-response)

**Analog:** `src/standalone/RegisterPanel.cpp` (slider construction) + `src/standalone/PluginEditor.cpp` (rotary knob construction + atomic write pattern)

**Rotary knob construction pattern** (PluginEditor.cpp lines 144-155):
```cpp
addAndMakeVisible(inputLevelKnob);
inputLevelKnob.setSliderStyle(juce::Slider::Rotary);
inputLevelKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 18);
inputLevelKnob.setRange(0.0, 1.0, 0.01);
inputLevelKnob.setValue(0.25, juce::dontSendNotification);
inputLevelKnob.setColour(juce::Slider::rotarySliderOutlineColourId, psxDarkGray);

inputLevelKnob.onValueChange = [this] {
    processorRef.getInputLevel().store(
        static_cast<float>(inputLevelKnob.getValue()),
        std::memory_order_relaxed);
};
```
Every macro knob copies this pattern exactly. Use `Rotary` style, `TextBoxBelow`, `dontSendNotification` for default value, `onValueChange` stores to processor atomic with `std::memory_order_relaxed`.

**Label construction pattern** (PluginEditor.cpp lines 157-161):
```cpp
addAndMakeVisible(inputLevelLabel);
inputLevelLabel.setText("Input Gain", juce::dontSendNotification);
inputLevelLabel.setJustificationType(juce::Justification::centred);
inputLevelLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF5A5A5A));
```

**Section header label pattern** (RegisterPanel.cpp lines 46-57):
```cpp
const auto psxLightGray = juce::Colour(0xFFB0B0B0);
const auto psxMauve    = juce::Colour(0xFFD49EBF);
auto boldFont = juce::FontOptions(14.0f, juce::Font::bold);
for (auto* header : {&headerMasterIO, &headerIIRWall, &headerComb,
                     &headerAPF, &headerDelay, &headerBase,
                     &headerSameGeom, &headerDiffGeom, &headerAPFAddr})
{
    header->setFont(boldFont);
    header->setJustificationType(juce::Justification::centredLeft);
    header->setColour(juce::Label::textColourId, psxMauve);
    addAndMakeVisible(header);
}
```
Use same bold font, psxMauve color for section headers (Walls, Echo Physics, Tap Positions, Diffusion, Decay/Reflectivity, Early Reflections, Room Size + Buffer).

**Slider color setup pattern** (RegisterPanel.cpp lines 33-35):
```cpp
slider.setColour(juce::Slider::textBoxTextColourId, sliderTrackGray);
slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF7079CC));
slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xFF6FD8CE));
```
Colors: dark gray text `0xFF2A2A2A`, blue outline `0xFF7079CC`, teal thumb `0xFF6FD8CE`.

**ComboBox dropdown construction pattern** (PluginEditor.cpp lines 373-397):
```cpp
for (int r = 0; r < SPU94_TEMPO_REG__COUNT; ++r) {
    auto& dropdown = perRegDropdowns[r];
    dropdown.addItem("Free", kPerRegFreeId);
    dropdown.addItem("Global", kPerRegGlobalId);
    dropdown.addSeparator();
    for (int s = 0; s < SPU94_SUBDIVISION__COUNT; ++s) {
        dropdown.addItem(
            juce::String(spu94_subdivision_to_string((spu94_subdivision_t)s)),
            kPerRegSubBase + s);
    }
    dropdown.setSelectedId(kPerRegGlobalId, juce::dontSendNotification);
    dropdown.setEnabled(false);  // disabled until synced mode

    // No onChange -- timer polls dropdown values to avoid Linux hover-trigger
    addAndMakeVisible(dropdown);

    // Label: use C core register name
    const char* regName = spu94_tempo_reg_name((spu94_tempo_reg_t)r);
    perRegLabels[r].setText(juce::String(regName), juce::dontSendNotification);
    perRegLabels[r].setJustificationType(juce::Justification::centredRight);
    perRegLabels[r].setColour(juce::Label::textColourId, psxDarkGray);
    perRegLabels[r].setFont(juce::FontOptions(11.0f));
    addAndMakeVisible(perRegLabels[r]);
}
```
CRITICAL: Timer-poll pattern for ComboBox on Linux (no `onChange`). Echo speed snap dropdowns (4x) and diffusion snap dropdowns (2x) must follow this same approach.

**Toggle button pattern** (PluginEditor.cpp lines 251-258):
```cpp
addAndMakeVisible(latencyCompToggle);
latencyCompToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xFF5A5A5A));
latencyCompToggle.setClickingTogglesState(true);
latencyCompToggle.setToggleState(true, juce::dontSendNotification);  // ON by default
latencyCompToggle.onClick = [this] {
    processorRef.getLatencyCompEnabled().store(
        latencyCompToggle.getToggleState(),
        std::memory_order_relaxed);
};
```
Use for Sync/Free toggles (echo speed, diffusion texture), wall link toggles, same/cross link, constrained toggles.

**Manual `resized()` layout pattern** (RegisterPanel.cpp lines 125-176):
```cpp
void RegisterPanel::resized()
{
    const int labelW = 80;
    const int rowH = 24;
    const int headerH = 22;
    const int gap = 2;
    const int margin = 4;

    auto area = getLocalBounds().reduced(margin, 0);
    int y = 0;

    auto layoutGroup = [&](juce::Label& header, size_t startIdx, size_t count) {
        header.setBounds(area.getX(), y, area.getWidth(), headerH);
        y += headerH + gap;

        for (size_t i = startIdx; i < startIdx + count; ++i)
        {
            labels[i].setBounds(area.getX(), y, labelW, rowH);
            sliders[i].setBounds(area.getX() + labelW + gap, y,
                                  area.getWidth() - labelW - gap, rowH);
            y += rowH + gap;
        }
    };
    // ... call layoutGroup per section
}
```
MacroPanel uses same manual `setBounds()` approach but with a multi-column grid for rotary knobs rather than vertical rows. Constants for knob diameter (~65px), label height (~16px), section padding, column spacing.

**Paint background pattern** (RegisterPanel.cpp lines 76-79):
```cpp
void RegisterPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF5A5A5A));
}
```

---

### `src/standalone/PluginEditor.h` (MODIFY, add MacroPanel + Advanced toggle)

**Analog:** self (existing member declarations)

**New member pattern** -- follow existing style (PluginEditor.h lines 42-81):
```cpp
// Existing pattern for component members:
RegisterPanel registerPanel;
juce::Viewport registerViewport;

// NEW members to add:
MacroPanel macroPanel;                           // owns all macro knobs
juce::TextButton advancedToggle{"Advanced"};     // view swap button
```
Also add `#include "MacroPanel.h"` to the include block.

---

### `src/standalone/PluginEditor.cpp` (MODIFY, integrate MacroPanel, view swap, window sizing)

**Analog:** self (existing constructor, timer, resized patterns)

**Component initialization pattern in constructor** (PluginEditor.cpp lines 399-406):
```cpp
// Register panel -- all 35 SPU registers in a scrollable viewport.
registerViewport.setViewedComponent(&registerPanel, false);
registerViewport.setScrollBarsShown(true, false);
addAndMakeVisible(registerViewport);

// Sync slider positions to the initial preset (Hall).
registerPanel.updateFromShadows();
```
MacroPanel: `addAndMakeVisible(macroPanel)` (default visible). RegisterViewport: change to `addChildComponent(registerViewport)` (hidden by default, per D-01).

**View swap toggle pattern** -- modeled after sync mode toggle (PluginEditor.cpp lines 312-342):
```cpp
addAndMakeVisible(advancedToggle);
advancedToggle.onClick = [this] {
    bool showAdvanced = !registerViewport.isVisible();
    macroPanel.setVisible(!showAdvanced);
    registerViewport.setVisible(showAdvanced);
    advancedToggle.setButtonText(showAdvanced ? "Macro" : "Advanced");

    if (!showAdvanced) {
        // Switching from Advanced to Macro: request re-derive (MACRO-04)
        processorRef.requestMacroDerive();
    } else {
        // Switching to Advanced: sync raw sliders from current state
        registerPanel.updateFromShadows();
    }
};
```

**Timer callback extension pattern** (PluginEditor.cpp lines 483-596):
The timer already has: factory preset detection, file preset detection, tempo sync polling, modified-state tracking. Add a new section after tempo sync for: reading derived macro positions from processor atomics, updating MacroPanel knob positions + unit labels, updating snap dropdown selections.

Follow the timer-poll pattern (lines 562-591) -- read atomics, update GUI controls with `dontSendNotification`:
```cpp
// In timerCallback, new macro panel sync section:
// Only update if macro panel is visible
if (macroPanel.isVisible()) {
    macroPanel.updateKnobPositions();  // reads processor atomics, updates knobs
    macroPanel.updateUnitLabels();     // converts register shadows to human units
}
```

**Window sizing** (PluginEditor.cpp lines 474-476):
```cpp
setResizeLimits(900, 880, 1600, 1500);
setSize(900, 1180);
```
Increase to accommodate macro panel: ~1100x1000 default, ~1100x900 minimum, ~1600x1500 max.

**Zone layout in resized()** (PluginEditor.cpp lines 648-719):
```cpp
// Existing zone structure:
// ZONE 1: Toolbar (y=0, h=75)
// TEMPO ZONE: (y=75, h=80)
// ZONE 2: Register viewport (fills middle)
// ZONE 3+4: Mixer/DAC (bottom 80px)

// Modified: macro panel and register viewport occupy the same ZONE 2 area.
// Both get the same setBounds() in resized(). Only one is visible at a time.
const int mainContentTop = 75 + tempoZoneHeight;
const int mainContentBottom = getHeight() - bottomZoneHeight - 5;
macroPanel.setBounds(10, mainContentTop, viewportW, mainContentBottom - mainContentTop);
registerViewport.setBounds(10, mainContentTop, viewportW, mainContentBottom - mainContentTop);
```

---

### `src/standalone/PluginProcessor.h` (MODIFY, add macro atomic fields)

**Analog:** self (existing atomic field declarations, lines 59-84)

**Atomic accessor pattern** (PluginProcessor.h lines 59-78):
```cpp
// --- Input level (pre-SPU gain staging) ---
std::atomic<float>& getInputLevel() { return inputLevel; }

// --- Mixer faders (0.0-1.0 float) ---
std::atomic<float>& getDryLevel() { return dryLevel; }
std::atomic<float>& getPatinaLevel() { return patinaLevel; }
// ... etc
```
Add ~17 float accessors for macro knob positions, ~6 bool accessors for toggles, array accessors for snap subdivisions. Follow identical naming convention: `getMacroRoomSize()`, `getMacroDecay()`, etc.

**Atomic field declaration pattern** (PluginProcessor.h lines 113-140):
```cpp
std::atomic<float> inputLevel{0.25f};
std::atomic<bool> adpcmEnabled{false};
std::atomic<float> dryLevel{0.0f};
// ...
std::atomic<uint16_t> tempoBpm{0};
std::atomic<uint8_t>  syncMode{0};
std::array<std::atomic<uint8_t>, 10> perRegSub;
```
New macro fields follow same pattern: `std::atomic<float>` for knob positions, `std::atomic<bool>` for toggles, `std::array<std::atomic<uint8_t>, N>` for snap dropdown arrays. Default values from RESEARCH.md Pattern 1.

**Last-pushed tracking pattern** (PluginProcessor.h lines 143-146):
```cpp
// Audio-thread-only tracking (not atomic)
uint16_t lastPushedBpm = 0;
uint8_t  lastPushedMode = 0;
std::array<uint8_t, 10> lastPushedSub{};
```
Add `lastPushed*` fields for each macro knob to avoid redundant C core calls in processBlock (only call `spu94_macro_apply_*` when the atomic value differs from last pushed).

**Derive request flag:**
```cpp
std::atomic<bool> requestDeriveAll{false};
```

---

### `src/standalone/PluginProcessor.cpp` (MODIFY, processBlock macro apply)

**Analog:** self (existing tempo sync push section, lines 269-392)

**Atomic read-and-push-if-changed pattern** (PluginProcessor.cpp lines 309-371):
```cpp
// Tempo steady-state: check for individual changes
// BPM change
if (bpm > 0 && bpm != lastPushedBpm) {
    spu94_set_tempo(spu, bpm);
    lastPushedBpm = bpm;
}

// Per-register subdivision changes
for (int r = 0; r < SPU94_TEMPO_REG__COUNT; r++) {
    uint8_t regSub = perRegSub[r].load(std::memory_order_relaxed);
    if (regSub != lastPushedSub[r]) {
        // ... apply change ...
        lastPushedSub[r] = regSub;
    }
}
```
Macro knob push follows IDENTICAL pattern:
```cpp
float roomPos = macroRoomSize.load(std::memory_order_relaxed);
if (roomPos != lastPushedRoomSize) {
    spu94_macro_apply_room_size(spu, roomPos);
    lastPushedRoomSize = roomPos;
    registerBridge.syncShadowsFromSPU(spu);
}
```

**Derive-all trigger** -- model after factory preset drain (PluginProcessor.cpp lines 122-143):
```cpp
if (presetQueue.drain(spu))
{
    registerBridge.syncShadowsFromSPU(spu);
    // ... sync GUI state ...
}
```
Derive-all section:
```cpp
if (requestDeriveAll.load(std::memory_order_relaxed)) {
    spu94_macro_derive_all(spu);
    // Write derived positions back to atomics for GUI timer to read
    // ... read from state->macro_knob_pos and write to derivedDecay, etc.
    requestDeriveAll.store(false, std::memory_order_relaxed);
}
```

**Decay-Reflectivity coupling writeback** -- after Decay apply, read back derived Reflectivity:
```cpp
float decayPos = macroDecay.load(std::memory_order_relaxed);
if (decayPos != lastPushedDecay) {
    spu94_macro_apply_decay(spu, decayPos);
    lastPushedDecay = decayPos;
    // Coupling: Reflectivity may have been re-derived
    float derivedRefl = spu94_macro_derive_bipolar(spu, SPU94_MACRO_REFLECTIVITY);
    derivedReflectivity.store(derivedRefl, std::memory_order_relaxed);
    registerBridge.syncShadowsFromSPU(spu);
}
```

---

### `src/standalone/RegisterPanel.h` (MODIFY, minimal)

**Analog:** self

No structural changes needed. The `QuantizedSlider` class defined here may need to be shared with MacroPanel (either via include or extraction to a shared header).

---

### `src/standalone/RegisterPanel.cpp` (MODIFY, hide vLIN/vRIN/vLOUT/vROUT + dual readout)

**Analog:** self (existing `resized()` layout)

**Hide first 4 sliders pattern** -- use `setVisible(false)` per Pitfall 5 (do NOT remove from arrays):
```cpp
// In constructor or resized(), after building all sliders:
// Indices 0-3 are vLOUT, vROUT, vLIN, vRIN -- hide per SAFE-05
for (size_t i = 0; i < 4; ++i) {
    sliders[i].setVisible(false);
    labels[i].setVisible(false);
}
```

**Dual readout pattern** -- override slider text display:
```cpp
// In constructor, per slider:
slider.textFromValueFunction = [regType](double val) -> juce::String {
    int16_t v = static_cast<int16_t>(val);
    juce::String hex = "0x" + juce::String::toHexString(
        static_cast<uint16_t>(v)).toUpperCase().paddedLeft('0', 4);
    juce::String human;
    // Convert based on register type (d-prefix = ms, m-prefix = meters, v-prefix = %)
    // ...
    return hex + " / " + human;
};
```

**Skip hidden sliders in resized()** (RegisterPanel.cpp lines 161-169):
```cpp
// Current:
layoutGroup(headerMasterIO,  0, 4);

// Modified: skip the Master I/O group entirely, or lay it out with zero height
// since sliders[0..3] are setVisible(false), they won't render.
// Simplest: remove the headerMasterIO layoutGroup call and start from index 4.
```

---

### `src/standalone/ParameterBridge.h` (MODIFY, possibly extend)

**Analog:** self

Likely no changes needed. MacroPanel writes to processor atomics (not through RegisterBridge). The `syncShadowsFromSPU()` call happens on the audio thread after macro apply, which is already the established pattern. If derived macro knob positions need to flow back to the GUI, those use dedicated atomics in PluginProcessor, not the register bridge.

---

## Shared Patterns

### Atomic Bridge (GUI -> Audio Thread)
**Source:** `src/standalone/PluginProcessor.h` lines 59-78, `src/standalone/PluginEditor.cpp` lines 151-155
**Apply to:** All macro knob writes, all toggle writes, all snap dropdown selections

```cpp
// GUI side (MacroPanel or PluginEditor onValueChange):
processorRef.getMacroXxx().store(
    static_cast<float>(knob.getValue()),
    std::memory_order_relaxed);

// Audio side (processBlock read-if-changed):
float val = macroXxx.load(std::memory_order_relaxed);
if (val != lastPushedXxx) {
    spu94_macro_apply_xxx(spu, val);
    lastPushedXxx = val;
    registerBridge.syncShadowsFromSPU(spu);
}
```

### Timer-Based GUI Sync (Audio Thread -> GUI)
**Source:** `src/standalone/PluginEditor.cpp` lines 483-596
**Apply to:** MacroPanel knob position updates, unit label updates, snap dropdown state updates

Always use `setValue(val, juce::dontSendNotification)` to prevent feedback loops (Pitfall 3). Guard with `isUpdatingFromTimer` flag if needed.

### PS1 Color Palette
**Source:** `src/standalone/PluginEditor.cpp` lines 8-25 (LookAndFeel setup)
**Apply to:** All new UI components

```cpp
const auto psxDarkGray  = juce::Colour(0xFF5A5A5A);
const auto psxLightGray = juce::Colour(0xFFB0B0B0);
const auto psxTeal      = juce::Colour(0xFF6FD8CE);
const auto psxMauve     = juce::Colour(0xFFD49EBF);
const auto psxCoral     = juce::Colour(0xFFE8736E);
const auto psxBlue      = juce::Colour(0xFF7079CC);
```

### Timer Poll for ComboBox (Linux Hover-Trigger Workaround)
**Source:** `src/standalone/PluginEditor.cpp` lines 362-397 (construction) and lines 562-591 (timer poll)
**Apply to:** All new ComboBox dropdowns (6x echo speed + diffusion snap dropdowns)

```cpp
// Construction: NO onChange callback
dropdown.setSelectedId(defaultId, juce::dontSendNotification);
addAndMakeVisible(dropdown);

// Timer: poll only when popup is NOT active
if (!dropdown.isPopupActive()) {
    int id = dropdown.getSelectedId();
    processorRef.getSnapXxx().store(
        static_cast<uint8_t>(id - base), std::memory_order_relaxed);
}
```

### updateFromShadows on View Switch
**Source:** `src/standalone/RegisterPanel.cpp` lines 81-90
**Apply to:** Advanced-to-Macro transition (derive), Macro-to-Advanced transition (sync sliders)

```cpp
// Already exists:
void RegisterPanel::updateFromShadows()
{
    for (size_t i = 0; i < kSliderRegisters.size(); ++i)
    {
        double val = bridge.getShadowValue(i);
        baseline[i] = val;
        sliders[i].setValue(val, juce::dontSendNotification);
    }
    scaleSlider.setValue(1.0, juce::dontSendNotification);
}
```

## No Analog Found

No files lack analogs. Every file in this phase maps directly to an existing pattern in the codebase.

| File | Role | Data Flow | Reason |
|------|------|-----------|--------|
| (none) | -- | -- | All files have exact analogs |

## Metadata

**Analog search scope:** `src/standalone/` (5 files read in full)
**Files scanned:** 5 source files + 2 C core headers + 1 C core internal header (grep only)
**Pattern extraction date:** 2026-05-05
