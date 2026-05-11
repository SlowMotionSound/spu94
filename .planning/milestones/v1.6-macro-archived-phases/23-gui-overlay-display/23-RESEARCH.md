# Phase 23: GUI Overlay + Display - Research

**Researched:** 2026-05-05
**Domain:** JUCE GUI overlay, macro control surface, unit conversion display
**Confidence:** HIGH

## Summary

Phase 23 builds a macro control panel as the default GUI view, overlaying the existing register viewport. The macro panel presents all 10 macro groups from Phase 21 as rotary knobs, organized into clearly labeled sections (Walls, Echo Physics, Diffusion, Decay/Reflectivity, Early Reflections, Room Size, Buffer). An Advanced toggle swaps the viewport content between macro and raw register views. Human-readable unit labels appear below each knob, and the raw register panel gains dual hex+human readouts.

The primary architectural challenge is the bridge between GUI knobs and the C core macro/snap API. The existing project uses atomic variables in PluginProcessor as the GUI-to-audio-thread handoff (no AudioProcessorValueTreeState). Every macro knob movement must cross the thread boundary via atomics, then the audio thread calls the appropriate `spu94_macro_apply_*` or `spu94_snap_*` function. The derive path runs in reverse: on preset load, the audio thread calls `spu94_macro_derive_all`, then writes derived positions into atomics that the GUI timer reads.

The codebase already has all the C core functions needed -- `spu94_macro_apply_*`, `spu94_macro_derive_*`, `spu94_snap_*` -- and the existing 30 Hz timer callback pattern for syncing processor state to the GUI. The work is pure JUCE GUI construction + a significant expansion of the PluginProcessor atomic bridge.

**Primary recommendation:** Create a new `MacroPanel` component class that owns all macro knobs and labels, swap it with `RegisterPanel` via `setVisible()` on the same viewport area, and expand PluginProcessor with ~25 new atomic fields for macro knob positions and toggle states.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: Overlay swap model -- macro panel replaces register viewport entirely. Only one visible at a time.
- D-02: Single-screen layout. All macro controls visible at once, no scrolling. Window sized to fit everything.
- D-03: Clearly designated sections with visual dividers. Expand window rather than cramming.
- D-04: PS1 color palette (light gray #B0B0B0, dark gray #5A5A5A, teal #6FD8CE, mauve #D49EBF, coral #E8736E, blue #7079CC) applied loosely. Prototype-quality color assignment.
- D-05: Rotary knobs for all macro controls.
- D-06: Human unit values as small text labels below each knob.
- D-07: Bipolar knobs show signed percentage with center detent at 0%.
- D-08: Raw register sliders in Advanced show dual readout (hex + human unit).
- D-09: Sweep/Spread/Rotate knobs always usable in both Free and Sync modes.
- D-10: Per-register subdivision dropdowns always visible but grayed out in Free mode.
- D-11: Diffusion snap controls live inside diffusion section.
- D-12: vLIN/vRIN/vLOUT/vROUT hidden from both surfaces.

### Claude's Discretion
- Exact section ordering and grouping within the single-screen layout
- JUCE component hierarchy (new Component subclass vs restructured PluginEditor)
- How the Advanced toggle is visually presented
- Precise unit conversion formulas (register value to meters, ms, etc.)
- How link/constrain toggles are visually presented
- Test strategy

### Deferred Ideas (OUT OF SCOPE)
- Real-time room geometry visualizer (VIZ-01, VIZ-02)
- Register reference manual (DOC-01)
- Independent clamping mode (EXP-01)
- Color-to-control mapping refinement (future with user input)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| GUI-01 | Macro panel is the default view | MacroPanel component added as default, RegisterPanel hidden initially via setVisible(false) |
| GUI-02 | Advanced toggle reveals raw register sliders | TextButton or ToggleButton swaps visibility between MacroPanel and RegisterPanel |
| GUI-03 | Safety constraints apply in both macro and raw mode | Macro knobs call spu94_macro_apply_* (safety built in); raw sliders already go through RegisterBridge which calls spu94_safe_set_reg_* |
| GUI-04 | Echo Physics section: macro knob + 8 snap dropdowns | 3 knobs (Spread/Sweep/Rotate) + 4 echo speed + 4 comb snap dropdowns + Sync/Free toggle |
| GUI-05 | Diffusion section: Amount + Texture knobs + 2 snap dropdowns | Amount S+S (2 knobs) + Texture S+S (2 knobs) + Sync/Free toggle + 2 dAPF dropdowns |
| SAFE-05 | vLIN/vRIN/vLOUT/vROUT hidden from all GUI surfaces | RegisterPanel must skip indices 0-3 (vLOUT/vROUT/vLIN/vRIN); MacroPanel never exposes them |
| UNIT-01 | Macro knobs display human units | Label below each knob shows converted value (ms, m, %, dB) |
| UNIT-02 | Raw register sliders show dual readout (hex + human) | RegisterPanel text box or label shows both "0x1234 / 12.3 ms" |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Macro knob display + interaction | Frontend (JUCE GUI) | -- | Pure UI component construction |
| Macro engine computation | C core (spu94_macro_*) | -- | DSP logic lives in C core exclusively |
| GUI-to-audio thread handoff | PluginProcessor atomics | -- | Established pattern: atomics bridge GUI and audio thread |
| Unit conversion (register -> human) | Frontend (JUCE GUI) | -- | Display-only calculation, no DSP impact |
| Safety constraint enforcement | C core (spu94_safe_*) | -- | Safety lives in C core; GUI just calls the API |
| View swapping (macro/advanced) | Frontend (JUCE GUI) | -- | setVisible() toggling, pure UI |
| Preset-to-knob derivation | C core (spu94_macro_derive_all) | PluginProcessor (atomic writeback) | Derive runs on audio thread, results written to atomics for GUI |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| JUCE | 8.0.12 | GUI framework | Already in use, fetched via CMake FetchContent [VERIFIED: CMakeLists.txt line 23] |
| libspu94 (C core) | in-tree | Macro/snap/safety API | All DSP logic, already complete from Phases 20-22 [VERIFIED: codebase] |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Unity (vendored) | in-tree | C unit test framework | Existing test infrastructure [VERIFIED: tests/unit/CMakeLists.txt] |

No new external dependencies needed. This phase is pure GUI construction on existing infrastructure.

## Architecture Patterns

### System Architecture Diagram

```
User Interaction (knob drag / toggle click)
    |
    v
MacroPanel (JUCE Component)
    |-- knob.onValueChange -> store to PluginProcessor atomic
    |-- toggle.onClick -> store to PluginProcessor atomic
    |
    v
PluginProcessor atomics (lock-free bridge)
    |
    v  (audio thread, every processBlock)
PluginProcessor::processBlock reads atomics
    |-- macro knob changed? -> spu94_macro_apply_*(spu, position)
    |-- snap toggle changed? -> spu94_snap_set_*_sync(spu, mode)
    |-- snap dropdown changed? -> spu94_snap_set_*_subdivision(spu, ...)
    |
    v
C core macro/snap/safety layer
    |-- writes registers through spu94_safe_set_reg_*
    |-- stability ceiling enforced, address bounds checked
    |
    v  (30 Hz timer callback)
PluginEditor::timerCallback
    |-- reads PluginProcessor atomics (derived knob positions)
    |-- reads RegisterBridge shadows (register values)
    |-- updates MacroPanel knob positions + unit labels
    |-- updates modified-state tracking
```

### Recommended Component Hierarchy

```
SPU94AudioProcessorEditor (PluginEditor)
|-- Toolbar (load/play/stop, preset, input/send knobs) -- EXISTING
|-- Tempo Zone (sync mode, BPM, grid) -- EXISTING, moves into macro panel
|-- advancedToggle (TextButton) -- NEW
|-- MacroPanel (new Component subclass) -- NEW, default visible
|   |-- Walls Section
|   |   |-- 4x wall groups (distance knob + echo speed knob + link toggle)
|   |   |-- Same/Cross link toggle
|   |-- Echo Physics Section
|   |   |-- Sync/Free toggle
|   |   |-- Spread knob, Sweep knob, Rotate knob
|   |   |-- 4x echo speed subdivision dropdowns
|   |-- Tap Positions Section
|   |   |-- Spread knob, Sweep knob
|   |   |-- Constrained toggle
|   |-- Diffusion Section
|   |   |-- Amount: Spread + Sweep knobs
|   |   |-- Texture: Spread + Sweep knobs
|   |   |-- Sync/Free toggle + 2x dAPF subdivision dropdowns
|   |   |-- Position: Spread + Sweep knobs + Constrained toggle
|   |-- Decay/Reflectivity Section
|   |   |-- Decay knob (bipolar)
|   |   |-- Reflectivity knob (bipolar)
|   |-- Early Reflections Section
|   |   |-- Spread + Sweep knobs
|   |-- Room Size + Buffer Section
|   |   |-- Room Size knob
|   |   |-- Buffer knob
|-- RegisterPanel (existing, hidden by default) -- MODIFIED: hide vLIN/vRIN/vLOUT/vROUT
|-- Mixer/DAC bottom bar -- EXISTING
```

### Pattern 1: Atomic Bridge for Macro Knobs

**What:** Each macro knob position is a `std::atomic<float>` in PluginProcessor. GUI writes on drag, audio thread reads in processBlock.

**When to use:** Every macro knob (Room Size, Buffer, Decay, Reflectivity, all Spread/Sweep/Rotate controls).

**Example:**
```cpp
// PluginProcessor.h -- new atomic fields
// Source: follows existing pattern from inputLevel, dryLevel, etc. [VERIFIED: PluginProcessor.h lines 59-78]
std::atomic<float>& getMacroRoomSize() { return macroRoomSize; }
std::atomic<float>& getMacroDecay() { return macroDecay; }
// ... one per macro knob

// PluginProcessor.cpp -- processBlock reads and applies
float roomPos = macroRoomSize.load(std::memory_order_relaxed);
if (roomPos != lastPushedRoomSize) {
    spu94_macro_apply_room_size(spu, roomPos);
    lastPushedRoomSize = roomPos;
    registerBridge.syncShadowsFromSPU(spu);  // update raw slider shadows
}

// MacroPanel -- knob writes atomic
roomSizeKnob.onValueChange = [this] {
    processorRef.getMacroRoomSize().store(
        static_cast<float>(roomSizeKnob.getValue()),
        std::memory_order_relaxed);
};
```

### Pattern 2: View Swapping

**What:** MacroPanel and RegisterPanel (in viewport) occupy the same screen area. Only one is visible at a time. Advanced toggle swaps them.

**Example:**
```cpp
// Source: JUCE Component::setVisible pattern [VERIFIED: Context7 /juce-framework/juce]
// In PluginEditor constructor:
addAndMakeVisible(macroPanel);     // default visible
addChildComponent(registerViewport); // hidden initially (addChildComponent = add but don't show)

advancedToggle.onClick = [this] {
    bool showAdvanced = !registerViewport.isVisible();
    macroPanel.setVisible(!showAdvanced);
    registerViewport.setVisible(showAdvanced);
    advancedToggle.setButtonText(showAdvanced ? "Macro" : "Advanced");
    
    if (!showAdvanced) {
        // Switching from Advanced to Macro: re-derive all macro positions
        // from current register state (MACRO-04)
        processorRef.requestMacroDerive();  // sets atomic flag
    }
};
```

### Pattern 3: Bipolar Knob with Center Detent

**What:** Decay and Reflectivity knobs use [-1.0, 1.0] range with center detent at 0.

**Example:**
```cpp
// Source: existing QuantizedSlider snap pattern [VERIFIED: RegisterPanel.h]
decayKnob.setSliderStyle(juce::Slider::Rotary);
decayKnob.setRange(-1.0, 1.0, 0.001);
decayKnob.setValue(0.0, juce::dontSendNotification);
decayKnob.setDoubleClickReturnValue(true, 0.0);  // double-click resets to center

// Snap to zero detent within a small zone
decayKnob.snapFunction = [](double v) -> double {
    if (std::abs(v) < 0.02) return 0.0;  // detent zone
    return v;
};
```

### Pattern 4: Unit Label Update in Timer

**What:** Human-readable unit labels below knobs are updated from the 30 Hz timer callback using register shadow values.

**Example:**
```cpp
// In timerCallback:
// Read current register value from shadow
int16_t vIIR = processorRef.getRegisterBridge().getShadowValue(4);  // index 4 = vIIR
// Convert to human unit
float percent = (vIIR >= 0)
    ? (static_cast<float>(vIIR) / 0x7FFF * 100.0f)
    : (static_cast<float>(vIIR) / 0x1000 * -12.5f);  // negative floor at -12.5%
decayLabel.setText(juce::String(percent, 1) + "%", juce::dontSendNotification);
```

### Anti-Patterns to Avoid

- **Direct C core calls from GUI thread:** Never call `spu94_macro_apply_*` from the GUI thread. Always go through atomics and let processBlock call the C core on the audio thread. The spu94_state is not thread-safe.
- **AudioProcessorValueTreeState:** This project deliberately avoids APVTS (no JUCE parameters). Do not introduce it. Continue using raw atomics.
- **JUCE layout managers (FlexBox/Grid):** The existing codebase uses manual `setBounds()` in `resized()`. Mixing layout managers with manual layout creates maintenance confusion. Stick with manual layout.
- **Scroll containers for macro panel:** D-02 explicitly requires no scrolling. Size the window to fit all controls.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Rotary knob rendering | Custom paint for knob graphics | `juce::Slider::Rotary` with LookAndFeel colors | Already used throughout the project; consistent appearance |
| Center detent behavior | Custom drag handler | `QuantizedSlider::snapFunction` returning 0.0 near center | Already exists in RegisterPanel.h for tempo snap |
| Thread-safe value passing | Mutex or lock | `std::atomic<float>` with relaxed ordering | Established project pattern; lock-free audio thread |
| Subdivision dropdown | Custom popup | `juce::ComboBox` with `spu94_subdivision_to_string` | Already done for 10 tempo dropdowns in PluginEditor |
| Section dividers | Custom drawing | `juce::GroupComponent` or manual `g.drawLine()` in paint | Simple, standard JUCE approach |

## Common Pitfalls

### Pitfall 1: Macro Derive Race on View Switch

**What goes wrong:** User switches from Advanced to Macro view. Raw register values have changed. Macro knob positions are stale (still showing pre-edit positions). User moves a macro knob, and the first click snaps registers to the stale knob position, discarding Advanced edits.

**Why it happens:** Macro derive must run on the audio thread (needs spu94_state access). GUI thread toggles the view. Without synchronization, there's a window where stale knob positions are displayed.

**How to avoid:** On Advanced-to-Macro transition, set an atomic flag (`requestDeriveAll`). Audio thread sees the flag in processBlock, calls `spu94_macro_derive_all`, writes derived positions back to atomics, clears the flag. GUI timer reads the new positions and updates knobs. During the transition frame (~33ms), knobs are non-interactive or display "syncing."

**Warning signs:** Macro knobs jump to wrong positions after editing in Advanced mode.

### Pitfall 2: Register Value Stomping Between Surfaces

**What goes wrong:** When the macro panel is visible, raw register sliders in the hidden RegisterPanel still have stale values. If the user switches to Advanced and then touches a slider, it writes its stale value, stomping the macro-applied value.

**Why it happens:** RegisterPanel sliders don't track macro writes. Their `onValueChange` fires on any touch, writing the displayed value.

**How to avoid:** On view switch to Advanced, call `registerPanel.updateFromShadows()` to sync all slider positions from current register state (shadows). This already exists and is called on preset load.

**Warning signs:** Register values jump when switching to Advanced view.

### Pitfall 3: Bidirectional Knob Update Loops

**What goes wrong:** Timer updates knob position from atomic. Knob fires `onValueChange`. `onValueChange` writes back to atomic. This creates a feedback loop that fights the derive values.

**Why it happens:** JUCE Slider fires `onValueChange` even when `setValue(..., dontSendNotification)` is NOT used.

**How to avoid:** Always use `setValue(val, juce::dontSendNotification)` when updating from timer. The `onValueChange` callback should only fire on user drag. Add a guard flag `isUpdatingFromTimer` if needed.

**Warning signs:** Knobs jitter, values oscillate, CPU spikes from tight callback loops.

### Pitfall 4: Decay-Reflectivity Coupling in GUI

**What goes wrong:** User moves Decay knob. C core re-derives Reflectivity position (stability coupling). But the GUI Reflectivity knob doesn't update.

**Why it happens:** The coupling happens inside `spu94_macro_apply_decay` on the audio thread. The derived Reflectivity position is written to `state->macro_knob_pos[SPU94_MACRO_REFLECTIVITY]` but never gets back to the GUI atomic.

**How to avoid:** After every Decay apply in processBlock, read back the derived Reflectivity position from the state and write it to the Reflectivity atomic. The GUI timer picks it up next frame.

**Warning signs:** Reflectivity knob shows wrong position after Decay change; visual mismatch with actual sound.

### Pitfall 5: vLIN/vRIN/vLOUT/vROUT Index Mapping

**What goes wrong:** Hiding the first 4 registers from RegisterPanel shifts all slider indices, breaking the bridge index mapping.

**Why it happens:** `kSliderRegisters` array has 35 entries; `RegisterBridge` uses 0-based indices. If you skip entries 0-3 in the panel but not the bridge, indices are misaligned.

**How to avoid:** Do NOT remove entries from `kSliderRegisters` or `RegisterBridge`. Instead, simply skip rendering the first 4 sliders in RegisterPanel's `resized()` and hide them via `setVisible(false)`. The bridge indices stay correct.

**Warning signs:** Wrong registers move when sliders are dragged in Advanced view.

### Pitfall 6: Window Size Explosion

**What goes wrong:** Adding all macro controls requires a much larger window. If the minimum size is too large, it won't fit on smaller monitors.

**Why it happens:** Current window is 900x1180 with min 900x880. Adding ~30 macro controls with rotary knobs needs careful layout.

**How to avoid:** Use compact rotary knobs (60-70px diameter including label). Organize into a 2-column or multi-column grid. Target window width 1000-1200px, height 900-1100px. Keep setResizeLimits reasonable.

**Warning signs:** Controls overlap, text truncated, window can't fit on screen.

## Code Examples

### Unit Conversion Functions

The SPU reverb runs at 22,050 Hz. All d-prefix (delay offset) and m-prefix (memory address) registers are in units of halfwords (2 bytes each). The conversion from register value to time in milliseconds:

```cpp
// Source: spu94.h line 526 [VERIFIED: codebase]
// SPU reverb sample rate is 22050 Hz
// Register values are in halfword units (2 bytes = 1 sample at 22050 Hz)
// time_ms = register_value / 22050.0 * 1000.0

static juce::String regToMs(int16_t val) {
    // d-prefix registers: delay in samples at 22.05 kHz
    float ms = static_cast<float>(val) / 22050.0f * 1000.0f;
    if (ms < 10.0f)
        return juce::String(ms, 2) + " ms";
    return juce::String(ms, 1) + " ms";
}

static juce::String regToMeters(uint16_t val) {
    // m-prefix registers: distance in samples at 22.05 kHz
    // speed of sound ~343 m/s -> one sample = 343/22050 = 0.01556 m
    float meters = static_cast<float>(val) * 343.0f / 22050.0f;
    if (meters < 1.0f)
        return juce::String(meters * 100.0f, 1) + " cm";
    return juce::String(meters, 2) + " m";
}

static juce::String regToPercent(int16_t val) {
    // v-prefix signed registers: percentage of full scale
    float pct = static_cast<float>(val) / 32767.0f * 100.0f;
    return juce::String(pct, 1) + "%";
}

static juce::String regToHex(int16_t val) {
    return "0x" + juce::String::toHexString(static_cast<uint16_t>(val)).toUpperCase().paddedLeft('0', 4);
}
```

### Dual Readout for Advanced Panel

```cpp
// In RegisterPanel, modify slider text display for UNIT-02
// Override getTextFromValue to show "0x1234 / 12.3 ms"
slider.textFromValueFunction = [regType](double val) -> juce::String {
    int16_t v = static_cast<int16_t>(val);
    juce::String hex = regToHex(v);
    juce::String human;
    switch (regType) {
        case REG_DELAY:   human = regToMs(v); break;
        case REG_ADDRESS: human = regToMeters(static_cast<uint16_t>(v)); break;
        case REG_COEFF:   human = regToPercent(v); break;
        default:          human = juce::String(v); break;
    }
    return hex + " / " + human;
};
```

### Macro Knob Count Inventory

From the 10 macro groups and snap controls, the complete knob+control inventory:

```
WALLS SECTION:
  4 wall distance knobs (mLSAME, mRSAME, mLDIFF, mRDIFF)  -- read-only indicators from Room Size
  4 wall echo speed knobs (dLSAME, dRSAME, dLDIFF, dRDIFF) -- individual, NOT macro
  4 wall link toggles (per D-06)
  1 same/cross link toggle (per D-07)

  NOTE: Individual wall controls are NOT macro groups. They're directly
  mapped to individual registers. The Echo Speed GROUP macro (Spread+Sweep)
  is in the Echo Physics section. The Walls section shows the per-wall
  physical layout.

ECHO PHYSICS SECTION:
  3 knobs: Spread, Sweep, Rotate (D-09 Sync mode transforms)
  1 Sync/Free toggle
  4 echo speed subdivision dropdowns (dLSAME/dRSAME/dLDIFF/dRDIFF)

TAP POSITIONS SECTION:
  2 knobs: Spread, Sweep
  1 constrained toggle
  (8 individual taps are advanced-only -- too many for macro panel)

DIFFUSION SECTION:
  2 knobs: Amount Spread, Amount Sweep
  2 knobs: Texture Spread, Texture Sweep
  2 knobs: Position Spread, Position Sweep
  1 constrained toggle (position)
  1 Sync/Free toggle (texture)
  2 subdivision dropdowns (dAPF1, dAPF2)

DECAY / REFLECTIVITY SECTION:
  1 Decay knob (bipolar)
  1 Reflectivity knob (bipolar)

EARLY REFLECTIONS SECTION:
  2 knobs: Spread, Sweep

ROOM SIZE + BUFFER SECTION:
  1 Room Size knob
  1 Buffer knob

TOTAL: ~22 rotary knobs + ~8 toggles + ~6 dropdowns = ~36 interactive controls
```

### Existing PluginProcessor Atomic Expansion Needed

```cpp
// New atomic fields needed in PluginProcessor.h:
// Source: follows existing pattern [VERIFIED: PluginProcessor.h lines 113-150]

// Macro knob positions (GUI -> audio thread)
std::atomic<float> macroRoomSize{0.5f};
std::atomic<float> macroBuffer{1.0f};    // default fully up
std::atomic<float> macroDecay{0.0f};     // bipolar center
std::atomic<float> macroReflectivity{0.0f};
std::atomic<float> macroEchoSpread{0.5f};
std::atomic<float> macroEchoSweep{0.5f};
std::atomic<float> macroEchoRotate{0.0f};
std::atomic<float> macroTapSpread{0.5f};
std::atomic<float> macroTapSweep{0.5f};
std::atomic<float> macroDiffAmountSpread{0.5f};
std::atomic<float> macroDiffAmountSweep{0.0f};
std::atomic<float> macroDiffTextureSpread{0.5f};
std::atomic<float> macroDiffTextureSweep{0.5f};
std::atomic<float> macroDiffPositionSpread{0.5f};
std::atomic<float> macroDiffPositionSweep{0.5f};
std::atomic<float> macroEarlyReflSpread{0.5f};
std::atomic<float> macroEarlyReflSweep{0.0f};

// Toggle states (GUI -> audio thread)
std::atomic<bool> echoSpeedSync{false};   // Sync/Free for echo speed
std::atomic<bool> diffTextureSync{false}; // Sync/Free for diffusion texture
std::atomic<bool> tapConstrained{false};
std::atomic<bool> diffPosConstrained{false};
std::atomic<uint8_t> wallLink{0};         // 4 bits packed or array
std::atomic<bool> sameCrossLink{false};

// Snap subdivision selections (echo speed: 4 regs, diff texture: 2 regs)
std::array<std::atomic<uint8_t>, 4> snapEchoSub;   // subdivision or 0xFF
std::array<std::atomic<uint8_t>, 2> snapDiffSub;   // subdivision or 0xFF

// Derived positions (audio thread -> GUI, read-only for GUI)
std::atomic<float> derivedDecay{0.0f};
std::atomic<float> derivedReflectivity{0.0f};
// ... one per macro knob for timer-based GUI sync

// Control flag
std::atomic<bool> requestDeriveAll{false}; // set on view switch / preset load
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Raw register sliders only | Macro knobs overlaying registers | Phase 23 (this phase) | Musical control surface replaces engineering interface |
| No unit display | Human-readable units on all controls | Phase 23 (this phase) | Usability for non-engineers |
| vLIN/vRIN/vLOUT/vROUT visible | Hidden from all surfaces | Phase 23 (this phase) | Removes confusing redundant controls |
| Per-register tempo dropdowns in toolbar | Snap dropdowns in macro sections | Phase 23 (this phase) | Tempo controls live with their parent controls |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Speed of sound = 343 m/s for register-to-meters conversion | Code Examples (unit conversions) | Minor cosmetic -- meters display would be slightly off. 343 m/s is standard at 20C. |
| A2 | m-prefix registers represent distance in halfword sample units | Code Examples | Unit labels would show wrong values. SPU documentation consistently treats these as sample offsets at 22050 Hz. |
| A3 | Window dimensions of 1000-1200 x 900-1100 will fit all macro controls without scrolling | Pitfalls (Pitfall 6) | May need layout adjustment during implementation. Exact sizing depends on knob dimensions chosen. |
| A4 | ~36 interactive controls is the correct count for the macro panel | Code Examples (inventory) | Plan task breakdown depends on this count. If the count is wrong, tasks may be under/over-scoped. |

## Open Questions

1. **Wall Section: Individual vs Macro Knobs**
   - What we know: Phase 21 defines individual wall distance+echo speed as paired controls (D-05). The Echo Speed Spread+Sweep macro (D-08) operates on all 4 echo speeds as a group.
   - What's unclear: Should the macro panel expose individual wall knobs (4 distance + 4 echo speed = 8 knobs) or just the macro groups? Individual wall knobs would duplicate the Advanced view; macro-only would lose per-wall control.
   - Recommendation: Include individual wall controls in the macro panel. They're the primary spatial controls users interact with. The Echo Physics macro (Spread+Sweep+Rotate) is a secondary layer on top. This matches the "Room Designer" concept where each wall is independently tunable.

2. **Tempo Controls Migration**
   - What we know: Tempo controls (BPM, sync mode, global subdivision) currently live in the PluginEditor toolbar area. D-10 says per-register subdivision dropdowns should be in the macro sections. D-09 says Sweep/Spread/Rotate work in both modes.
   - What's unclear: Do the master tempo controls (BPM field, sync mode selector, global subdivision) stay in the toolbar, or do they move to the macro panel?
   - Recommendation: Keep BPM/SyncMode/GlobalSub in the toolbar (they're global, not section-specific). Move per-register snap dropdowns into their respective macro sections.

3. **Individual Tap Controls in Macro View**
   - What we know: 8 individual tap position controls (mLCOMB1-4, mRCOMB1-4) exist per TAP-01. Spread+Sweep macro sits on top per TAP-02.
   - What's unclear: Should all 8 individual taps be visible in the macro view? That's a lot of knobs for one section.
   - Recommendation: Show only the Spread+Sweep macro knobs in the macro panel. Individual tap tuning is an Advanced-mode activity. Same for individual diffusion positions.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C test framework, vendored) |
| Config file | tests/unit/CMakeLists.txt |
| Quick run command | `cd build_test && ctest -R test_macro -j$(nproc)` |
| Full suite command | `cd build_test && ctest -j$(nproc)` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| GUI-01 | Macro panel default visible | manual | Build and launch, verify macro panel shows | N/A (visual) |
| GUI-02 | Advanced toggle swaps view | manual | Click toggle, verify register panel shows | N/A (visual) |
| GUI-03 | Safety in both surfaces | unit | `ctest -R test_safety -j$(nproc)` | Existing tests cover C core safety |
| GUI-04 | Echo Physics layout | manual | Visual verification | N/A (visual) |
| GUI-05 | Diffusion layout | manual | Visual verification | N/A (visual) |
| SAFE-05 | vLIN/vRIN/vLOUT/vROUT hidden | manual | Build and launch, verify 4 registers not visible | N/A (visual) |
| UNIT-01 | Human unit display | manual | Verify labels show ms/m/% | N/A (visual) |
| UNIT-02 | Dual readout in Advanced | manual | Verify hex+human in register panel | N/A (visual) |

### Sampling Rate
- **Per task commit:** `cd build_test && cmake --build . -j$(nproc) && ctest -R "test_macro|test_snap|test_safety" -j$(nproc)`
- **Per wave merge:** `cd build_test && ctest -j$(nproc)`
- **Phase gate:** Full suite green + manual visual verification of all 8 requirements

### Wave 0 Gaps
- GUI testing is inherently manual for JUCE components (no automated GUI test framework in project)
- C core safety/macro/snap tests already exist and cover the underlying behavior
- No new test files needed -- this phase is pure GUI; correctness is verified through existing C core tests + visual inspection

*Note: The nature of this phase (GUI construction) means most requirements are verified by visual inspection, not automated tests. The C core guarantees (safety enforcement, macro math, snap behavior) are already thoroughly tested by existing test suites.*

## Sources

### Primary (HIGH confidence)
- JUCE 8.0.12 source (via CMake FetchContent) -- Slider, Component, Timer APIs
- Context7 /juce-framework/juce -- component patterns, Slider Rotary style
- In-tree source files:
  - `src/standalone/PluginEditor.cpp` (911 lines) -- current GUI structure
  - `src/standalone/PluginProcessor.h` -- atomic bridge pattern
  - `src/standalone/RegisterPanel.cpp` -- slider layout pattern
  - `include/spu94/spu94_macro.h` -- macro engine API
  - `include/spu94/spu94_snap.h` -- snap API
  - `src/spu94/spu94_macro_controls.c` -- 10 group definitions

### Secondary (MEDIUM confidence)
- SPU reverb sample rate 22050 Hz -- confirmed via spu94.h line 526

### Tertiary (LOW confidence)
- Speed of sound 343 m/s for distance conversion -- standard physics value, not verified against SPU documentation

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new dependencies, all tools already in use
- Architecture: HIGH -- follows established project patterns exactly
- Pitfalls: HIGH -- identified from thorough codebase analysis
- Unit conversions: MEDIUM -- formulas are straightforward but A1/A2 assumptions apply

**Research date:** 2026-05-05
**Valid until:** 2026-06-05 (stable -- no external dependency changes expected)
