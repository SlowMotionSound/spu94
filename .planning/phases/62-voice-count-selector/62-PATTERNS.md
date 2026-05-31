# Phase 62: Voice-Count Selector - Pattern Map

**Mapped:** 2026-05-31
**Files analyzed:** 2 modified (`src/plugin/PluginEditor.h`, `src/plugin/PluginEditor.cpp`); 0 created
**Analogs found:** 2 / 2 (both exact — same role, same data flow)

> This is a GUI-only phase. No new files, no audio behavior. The new control is a
> single `juce::ComboBox` ("Voice Count", items 1–24, default 24) added to the
> **standalone** sampler panel, whose `onChange` calls the already-built
> `processor.setActiveVoiceCount(n)`. The analog (`recordModeBox`) is pinned by
> CONTEXT.md D-01; this map pulls the exact excerpts to clone.

## File Classification

| Modified File | Role | Data Flow | Closest Analog | Match Quality |
|---------------|------|-----------|----------------|---------------|
| `src/plugin/PluginEditor.h` (member decl region, ~88–145) | component (member declarations) | request-response (UI event → setter) | `recordModeBox` declaration, `PluginEditor.h:90` | exact |
| `src/plugin/PluginEditor.cpp` (ctor setup ~58–67; `onChange` ~301; `resized()` ~1670–1745) | component (UI wiring + layout) | request-response (event-driven `onChange`) | `recordModeBox` setup/onChange/layout, `PluginEditor.cpp:59-67, 301-306, 1693-1694` | exact |

Both new code regions clone one analog widget end-to-end (`recordModeBox`), so a single
exact analog covers declaration, setup, wiring, and layout.

## Pattern Assignments

### `src/plugin/PluginEditor.h` — member declaration (add ~near line 90 or near the per-voice group ~136–145)

**Analog:** `recordModeBox` declaration, inside the `// Voice engine panel (standalone-only, Phase 31)` group.

**Declaration pattern to clone** (`PluginEditor.h:88-91`):
```cpp
    // Voice engine panel (standalone-only, Phase 31)
    juce::TextButton recordButton{"Record"};
    juce::ComboBox recordModeBox;
    juce::Label recordModeLabel;
```

**Per-voice neighbor group** (where the new control conceptually belongs — it governs these) (`PluginEditor.h:85-86, 135-145`):
```cpp
    juce::Slider voicePitchKnob;
    juce::Label voicePitchLabel;
    ...
    // Voice pan/level controls (Phase 39: replaces raw Vol L/R)
    juce::Slider voicePanKnob;
    juce::Label voicePanLabel;
    juce::Slider voiceLevelKnob;
    juce::Label voiceLevelLabel;
    juce::ToggleButton voiceInvToggle{"INV"};
    juce::Label voiceInvIndicator;
    // NON/PMON toggles (Phase 40: voice feature toggles)
    juce::ToggleButton voiceNonToggle{"NON"};
    juce::ToggleButton voicePmonToggle{"PMON"};
    juce::Label voiceModeLabel;
```

> **Note on naming convention:** the codebase consistently uses a `juce::ComboBox` +
> a paired `juce::Label` (e.g. `recordModeBox`/`recordModeLabel`,
> `encodeRateBox`/`encodeRateLabel`). The new declaration should follow suit, e.g.
> `juce::ComboBox voiceCountBox;` + `juce::Label voiceCountLabel;` (exact identifier is
> implementer's call per D-07 / Claude's Discretion). Place it either in the
> `// Voice engine panel (standalone-only, Phase 31)` block (line 88, alongside the
> other panel ComboBoxes) or in the per-voice group at ~135 (it governs those controls
> per D-02). Either is consistent; the per-voice group keeps it next to the controls it
> drives.

---

### `src/plugin/PluginEditor.cpp` — constructor setup (add inside the sampler-panel setup block, near lines 58–67)

**Analog:** `recordModeBox` setup. Note the panel handle: per CONTEXT.md D-03 the voice
panel is standalone-only, and the analog confirms it — controls are added to the
`SamplerWindow`'s `panel`, NOT to the editor directly.

**Panel acquisition** (`PluginEditor.cpp:54-56`):
```cpp
        // ---- Sampler window + voice engine controls ----
        samplerWindow = std::make_unique<SamplerWindow>();
        auto& panel = samplerWindow->getPanel();
```

**ComboBox + label setup pattern to clone** (`PluginEditor.cpp:58-67`):
```cpp
        // Record mode selector — Manual (direct record) vs Threshold (arm-then-trigger)
        panel.addAndMakeVisible(recordModeBox);
        recordModeBox.addItem("Manual", 1);
        recordModeBox.addItem("Threshold", 2);
        recordModeBox.setSelectedId(1, juce::dontSendNotification);
        recordModeBox.setTooltip("Manual: record immediately on press\nThreshold: arm and wait for signal");
        panel.addAndMakeVisible(recordModeLabel);
        recordModeLabel.setText("Rec Mode", juce::dontSendNotification);
        recordModeLabel.setJustificationType(juce::Justification::centred);
        recordModeLabel.setFont(juce::Font(10.0f));
```

**Adaptation for Voice Count (items 1–24, default 24):**
- The analog adds items one-by-one with explicit IDs (`addItem("Manual", 1)`). For 1–24,
  the same idiom works in a loop. JUCE's `addItem(text, itemId)` requires `itemId >= 1`,
  so use the value directly as both label and ID:
  ```cpp
  for (int n = 1; n <= 24; ++n)
      voiceCountBox.addItem(juce::String(n), n);   // itemId == voice count == 1..24
  voiceCountBox.setSelectedId(24, juce::dontSendNotification);  // default 24 (engine default)
  ```
  (`juce::ComboBox::addItemList(StringArray, firstId)` is an alternative, but the
  per-item loop keeps itemId == voice count, which makes the `onChange` read trivial.)
- Label text: `"Voice Count"` (D-04). Match the `centred` justification + `Font(10.0f)`.
- Tooltip text is implementer's discretion (D-07).
- **Default selection = 24** matches `activeVoiceCount{24}` (D-05) — no audible change
  until lowered. Use `juce::dontSendNotification` so construction doesn't fire `onChange`.

---

### `src/plugin/PluginEditor.cpp` — `onChange` wiring (add near the existing `recordModeBox.onChange`, ~line 301)

**Analog:** the `recordModeBox.onChange` lambda — the exact event-wiring precedent.

**onChange pattern to clone** (`PluginEditor.cpp:301-306`):
```cpp
        recordModeBox.onChange = [this]()
        {
            bool isThreshold = (recordModeBox.getSelectedId() == 2);
            thresholdKnob.setVisible(isThreshold);
            thresholdLabel.setVisible(isThreshold);
        };
```

**Adaptation for Voice Count (D-06 — the entire behavioral contract):**
```cpp
        voiceCountBox.onChange = [this]()
        {
            processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId());
        };
```
- The processor handle in this class is `processorRef` (confirmed by sibling calls in the
  same file, e.g. `processorRef.startRecording()` at line 88, `processorRef.stopVoice()`
  at line 139, `processorRef.setOneShotMode(...)` at line 132).
- Because items were added with `itemId == voice count`, `getSelectedId()` returns the
  voice count directly — no mapping needed. (If `addItemList` were used instead, the ID
  would be offset and would need `getText().getIntValue()` or `getSelectedId() - firstId + 1`.)
- No extra logic — Phase 60 clamps/ring-out and Phase 61 fan-out handle the rest (D-06).

**Engine setter to call** (`PluginProcessor.h:277-281`) — the comment literally anticipates this phase:
```cpp
    // Phase 60 (VCOUNT-02 / VALLOC-01..03): set the active sampler-voice count.
    // Message-thread setter; clamps n to [1, 24] and stores realtime-safely for
    // the audio-thread allocator to read. Default count is 24 (see activeVoiceCount).
    // The future Phase 62 GUI selector calls this; Phase 60 tests call it directly.
    void setActiveVoiceCount(int n);
```
Signature: `void setActiveVoiceCount(int n);` — call as `processorRef.setActiveVoiceCount(<1..24>)`.

---

### `src/plugin/PluginEditor.cpp` — `resized()` layout (add inside the `if (samplerWindow)` block, ~lines 1670–1783)

**Analog:** `recordModeBox` bounds + the per-voice Pan/Level/INV layout group.

**ComboBox + label bounds pattern to clone** (`PluginEditor.cpp:1692-1694`):
```cpp
            // Record mode + Encode rate — right column
            recordModeLabel.setBounds(430, 10, 80, 14);
            recordModeBox.setBounds(430, 26, 80, 22);
```
> Pattern: label `setBounds(x, y, w, 14)` then box `setBounds(x, y+16, w, 22)` directly
> below it. A ComboBox is ~22 px tall; its label is ~14 px tall, 16 px above.

**Per-voice group it governs (where it conceptually belongs, D-02)** (`PluginEditor.cpp:1730-1744`):
```cpp
            // Pan + Level + INV section — below ADSR
            constexpr int voly = tgy + 28;
            // Pan rotary on top
            voicePanLabel.setBounds(20, voly, 100, 16);
            voicePanKnob.setBounds(20, voly + 16, 100, 70);
            // Level vertical fader below pan
            voiceLevelLabel.setBounds(20, voly + 90, 100, 16);
            voiceLevelKnob.setBounds(35, voly + 106, 70, 100);
            // INV toggle + indicator to the right of pan
            voiceInvToggle.setBounds(160, voly + 20, 70, 30);
            voiceInvIndicator.setBounds(160, voly + 52, 70, 16);

            // Noise Color — right column below INV
            noiseColorLabel.setBounds(160, voly + 76, 100, 16);
            noiseColorKnob.setBounds(170, voly + 92, 80, 70);
```

**Layout space confirmation (concrete):**
- The standalone panel (`SamplerWindow.h:18`) is **640 × 1000 px** — ample room; the whole
  voice-panel layout currently ends at `mody + 106` (`fx_start_y = voly + 220`,
  `row1y = fx_start_y + 30`, `mody = row1y + 100`), well under 1000.
- **Spacing is computed by `constexpr int` y-anchors that cascade**: `aky=374` → `tgy=aky+12+akh+2`
  → `voly=tgy+28` → `fx_start_y=voly+220` → `row1y=fx_start_y+30` → `mody=row1y+100`. Inserting a
  small control does NOT require recomputing these as long as it fits in existing gaps; if a new
  row is added, prefer extending the cascade with another `constexpr` anchor rather than hard
  numbers.
- **Two clean placement options, both with confirmed open space:**
  1. **Beside the per-voice group (recommended for D-02 "governs those controls"):** the INV
     column sits at **x=160**. The per-voice band uses roughly x=20..250 from `voly` to
     `voly+162`. There is open horizontal room to the right (x≈270+) at the `voly` band, or a
     compact slot can go above the Pan label. A label+box pair (`100×14` + `100×22`) fits in
     the gap, e.g. label at `(270, voly, 100, 14)` and box at `(270, voly + 16, 100, 22)`
     (exact coords are Claude's Discretion, D-07).
  2. **Right column with the other ComboBoxes:** below `encodeRateBox` (ends y≈172, x=430).
     The right column at x=430 is free between y≈172 and the waveform display at y=186 — tight,
     so option 1 is roomier and keeps the control with the voices it drives.

## Shared Patterns

### ComboBox idiom (declaration → setup → onChange → bounds)
**Source:** `recordModeBox` across `PluginEditor.h:90` and `PluginEditor.cpp:59-67, 301-306, 1693-1694`
**Apply to:** the single new Voice Count dropdown.
The four-part idiom: (1) `juce::ComboBox` + paired `juce::Label` member; (2)
`panel.addAndMakeVisible(...)` + `addItem`/`setSelectedId(..., dontSendNotification)` +
`setTooltip` + label `setText`/`centred`/`Font(10.0f)`; (3) `box.onChange = [this](){ ... }`;
(4) label `setBounds(x,y,w,14)` then box `setBounds(x,y+16,w,22)` inside `if (samplerWindow)`.

A second in-file example of the same idiom is `encodeRateBox`/`encodeRateLabel`
(`PluginEditor.h:101-102`, `PluginEditor.cpp:1695-1697`), confirming this is the
established convention, not a one-off.

### Standalone-only gating
**Source:** all voice-panel controls are added to `samplerWindow->getPanel()`
(`PluginEditor.cpp:55-56`), and laid out only inside `if (samplerWindow)` (`resized()` line 1671).
**Apply to:** the new dropdown — add it to `panel` and bound it inside the `if (samplerWindow)`
block. This is exactly how D-03 (standalone-only, plugin deferred) is satisfied: by joining the
existing standalone panel, no plugin-surface code is touched.

### Processor handle for wiring
**Source:** `processorRef.<method>()` used throughout the editor (e.g. `PluginEditor.cpp:88, 132, 139`).
**Apply to:** the `onChange` body → `processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId())`.

## No Analog Found

None. Every region of this phase has an exact in-file analog (`recordModeBox`, with
`encodeRateBox` as a second confirming example).

## Metadata

**Analog search scope:** `src/plugin/PluginEditor.h`, `src/plugin/PluginEditor.cpp`,
`src/plugin/PluginProcessor.h`, `src/plugin/SamplerWindow.h`
**Files scanned:** 4
**Skills checked:** `.claude/skills/` and `.agents/skills/` — neither exists; no project skills to load
**Pattern extraction date:** 2026-05-31
