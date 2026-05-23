# JUCE Standalone GUI Audit

**Reviewed:** 2026-05-07
**Scope:** Dead code, simplification, stale ideas, redundancy, bugs
**Files:** 12 source files (PluginProcessor, PluginEditor, MorphPanel, RegisterPanel, ParameterBridge, WavLoader -- headers and implementations)

---

## BUG Findings

### BUG-01: savePresetToString data-races engines[0] across threads

**File:** `src/standalone/PluginProcessor.cpp:340-378`
**Severity:** BUG

`savePresetToString` is called on the message thread (triggered by the GUI Save button). It calls `spu94_set_input_gain`, `spu94_set_dry_fader`, etc., directly on `engines[0]` -- the same engine that `processBlock` mutates concurrently on the audio thread. The `spu94.h` header explicitly states: *"A spu94_state is NOT thread-safe. Concurrent access from multiple threads requires external synchronization."* No synchronization exists here.

Every `spu94_set_*` call in lines 347-368 is a data race. The save could also read a partially-written register state via `spu94_preset_save` at line 371.

**Fix:** Route the save operation through a pending-request mechanism (like the existing `filePresetReady` flag for loads), so `engines[0]` is only touched from the audio thread. Or use a lock that the audio thread trylocks and the message thread blocks on.

---

### BUG-02: isWaypointCustom reads engines[0] from the GUI thread

**File:** `src/standalone/PluginProcessor.cpp:412-415`
**Severity:** BUG

`isWaypointCustom` calls `spu94_interp_is_waypoint_custom(engines[0], slot)` directly on the engine from whatever thread calls it. It is called from:
- `MorphPanel::updateLabelText` (line 79 of MorphPanel.cpp) -- message thread
- `MorphPanel::paint` (line 125 of MorphPanel.cpp) -- message thread

This races with `processBlock` on the audio thread which also mutates `engines[0]`.

**Fix:** Mirror the waypoint-custom state into a `std::array<std::atomic<bool>, 9>` in the processor, updated from the audio thread after any waypoint capture/reset.

---

### BUG-03: Play/Stop buttons are non-functional -- `playing` flag never checked in processBlock

**File:** `src/standalone/PluginProcessor.cpp:105-336`
**Severity:** BUG

The `wavSource.playing` atomic is written by `startPlayback()` (line 436) and `stopPlayback()` (line 441), but `processBlock` never reads it. Once a WAV is loaded (`numFrames > 0` at line 304), audio is always fed to the engine regardless of the playing state. The Play and Stop buttons in the UI have no effect on audio output.

`stopPlayback()` resets `playPos` to 0 (line 442), but the next `processBlock` call immediately reads from position 0 and advances the position (lines 332-335), so the WAV resumes playing from the start.

**Fix:** Either gate WAV reading on `wavSource.playing` in processBlock (silence when not playing), or remove the `playing` flag, `startPlayback`, `stopPlayback`, `isPlaying`, and the Play/Stop buttons entirely if "always-on" is the intended design.

---

### BUG-04: U16 register values above 32767 display incorrectly in RegisterPanel

**File:** `src/standalone/ParameterBridge.cpp:40`, `src/standalone/RegisterPanel.cpp:61`
**Severity:** BUG (latent)

`syncShadowsFromSPU` stores U16 register values into `int16_t` shadows via `static_cast<int16_t>(spu94_get_reg_u16_pending(...))`. Values above 32767 wrap to negative int16 values. When `RegisterPanel::updateFromShadows` reads these via `bridge.getShadowValue(i)`, the implicit `int16_t -> double` conversion produces a negative number, but the slider range is `[0.0, 65535.0]`, so JUCE clamps it to 0.

This doesn't trigger with factory presets (all U16 values < 0x2000), but would corrupt the display for any user-edited address register above 32767 or a loaded preset file containing large U16 values.

**Fix:** Change the shadow type to `int32_t` or `uint16_t`, or change `getShadowValue` to return the value through the correct signedness based on the register type.

---

### BUG-05: Preset dropdown rebuild on factory switch loses selection atomicity

**File:** `src/standalone/PluginEditor.cpp:104-120`
**Severity:** BUG (minor, cosmetic)

When a factory preset is selected and a custom entry exists, the handler clears the entire ComboBox and rebuilds it (lines 107-110), then attempts to use `id` (captured before the clear) to apply the preset (line 117). But `id` was read from the old ComboBox state at line 100. After `clear()`, the call to `processorRef.getPresetQueue().requestPreset()` uses the stale `id` value -- which is correct numerically, but the ComboBox selection state is now `0` (nothing selected) because clear() resets it. The `presetSelector.setSelectedId` in the timer callback (line 549) eventually restores the correct selection, but there is a brief visual glitch where no preset appears selected.

**Fix:** Store the selected preset ID before clearing, rebuild the ComboBox, then restore selection before returning. Or skip the rebuild if the custom entry doesn't exist.

---

## DEAD_CODE Findings

### DC-01: `adpcmEnabled` atomic and `getAdpcmEnabled()` are unused

**File:** `src/standalone/PluginProcessor.h:61,126`
**Severity:** DEAD_CODE

The `adpcmEnabled` member and its accessor `getAdpcmEnabled()` are declared and initialized but never read anywhere in the codebase. ADPCM is now implicitly enabled based on the patina fader and ADPCM send levels (PluginProcessor.cpp lines 186-191). The comment at line 183 even explains: *"The old dedicated toggle was removed (D-08); the mixer controls now implicitly drive ADPCM on/off."*

**Fix:** Remove `adpcmEnabled` member variable and `getAdpcmEnabled()` accessor from PluginProcessor.h.

---

### DC-02: `wavSource.playing` flag and playback control methods are dead

**File:** `src/standalone/PluginProcessor.h:48-50,191`, `src/standalone/PluginProcessor.cpp:434-448`
**Severity:** DEAD_CODE

As identified in BUG-03, `wavSource.playing` is never checked in processBlock. The `startPlayback()`, `stopPlayback()`, and `isPlaying()` methods, along with the `playing` and `loaded` atomic members in WavSource, have no effect on audio processing.

The Play and Stop buttons in the editor (PluginEditor.cpp lines 31-43) call these methods, but the calls produce no observable behavior change.

**Fix:** Either integrate the `playing` flag into processBlock to gate WAV reading, or remove the dead flag and buttons.

---

### DC-03: `psxDarkGray` declared but never used in MorphPanel.cpp

**File:** `src/standalone/MorphPanel.cpp:6`
**Severity:** DEAD_CODE

`static const auto psxDarkGray = juce::Colour(0xFF5A5A5A)` is declared at file scope but never referenced in MorphPanel.cpp. The only usage of dark gray in MorphPanel is via `juce::Colours::darkgrey` in paint() (line 104).

**Fix:** Remove the unused variable.

---

### DC-04: `getStateInformation` / `setStateInformation` are empty stubs with stale comments

**File:** `src/standalone/PluginProcessor.cpp:484-492`
**Severity:** DEAD_CODE

Both methods are empty with comments referencing "Plan 03 fills this with register state serialization/deserialization." Phase 14's file-preset system (`.spu94` files) is already fully implemented via `savePresetToString` and `loadPresetFromString`. These JUCE host-state methods are vestigial -- the standalone app has no host to call them.

**Fix:** Remove the Plan 03 comments. Keep the empty method bodies (JUCE requires them), but add a comment explaining they are intentionally empty for the standalone build.

---

### DC-05: `PresetSnapshot` member initializers never take effect

**File:** `src/standalone/PluginEditor.h:112-116`
**Severity:** DEAD_CODE

The default member initializers in `PresetSnapshot` (e.g., `dry = 1.0f`, `drySend = 1.0f`, `dac = false`) don't match the processor's actual defaults (`dryLevel{0.0f}`, `drySend{0.0f}`, `dacEnabled{true}`). However, `captureBaseline()` is always called before `checkModified()`, so these initializers are immediately overwritten. They are misleading dead values.

**Fix:** Either remove the initializers and rely on `captureBaseline()`, or correct them to match the processor defaults for clarity.

---

## STALE Findings

### ST-01: "MacroPanel" reference in MorphPanel.cpp comment

**File:** `src/standalone/MorphPanel.cpp:5`
**Severity:** STALE

Comment reads: *"PS1 color palette (same values as archived MacroPanel)"*. MacroPanel was part of the abandoned macro control system. This comment references an artifact that no longer exists in the codebase.

**Fix:** Change to *"PS1 color palette"* or *"PS1 color palette (PS1 controller face buttons)"*.

---

### ST-02: "Plan 03" references in getStateInformation / setStateInformation

**File:** `src/standalone/PluginProcessor.cpp:486,491`
**Severity:** STALE

Comments say *"Plan 03 fills this with register state serialization."* Plan 03 has long since been completed and the serialization was implemented via a different mechanism (file presets in Phase 14). These comments point to a future that already arrived and went a different direction.

**Fix:** Replace with *"Intentionally empty -- standalone build uses file-based preset save/load."*

---

### ST-03: "Plan 03: lock-free GUI <-> audio handoff" comment on getRegisterBridge

**File:** `src/standalone/PluginProcessor.h:53`
**Severity:** STALE

Comment references *"Plan 03"* which is long completed. The register bridge works; the comment's forward-looking language is misleading.

**Fix:** Remove the plan reference. Keep the description: *"Lock-free GUI <-> audio register bridge."*

---

## REDUNDANT Findings

### RD-01: PS1 color palette defined in two files

**File:** `src/standalone/MorphPanel.cpp:6-11`, `src/standalone/PluginEditor.cpp:361-362`
**Severity:** REDUNDANT

MorphPanel.cpp defines all 6 PS1 palette colors at file scope. PluginEditor.cpp re-defines `psxMauve` and `psxCoral` as `static` locals inside the constructor. Same hex values in both places.

**Fix:** Extract the palette into a shared header (e.g., `PSXColours.h`) or a single file-scope definition imported by both.

---

### RD-02: Rotary knob setup boilerplate repeated across three patterns

**File:** `src/standalone/PluginEditor.cpp:126-169, 344-352, 402-419`
**Severity:** REDUNDANT

Three distinct rotary knob setup patterns exist:

1. **Toolbar knobs** (inputLevelKnob, adpcmSendKnob, drySendKnob, mixer knobs at lines 126-216): each has 6-7 lines of identical style/textbox/range setup.
2. **Morph zone knobs** (morphSpeedKnob, memorySlipKnob, clockDriftKnob at lines 344-392): use a `setupRotary` lambda.
3. **Envelope tuning knobs** (slipAttack through slipDepth at lines 402-431): use a `setupTuningKnob` lambda.

The toolbar knobs (pattern 1) don't use either lambda and repeat the same `setSliderStyle(Rotary)` / `setTextBoxStyle` / `setRange` / `onValueChange` boilerplate 6 times.

**Fix:** Extend `setupRotary` or create a shared helper that covers all three patterns, parameterized by range, text box style, and color.

---

### RD-03: Mixer-fader push code duplicated between processBlock and savePresetToString

**File:** `src/standalone/PluginProcessor.cpp:206-227, 347-368`
**Severity:** REDUNDANT

Lines 206-227 (in processBlock) push all mixer/DAC atomics to `engines[0]` via `spu94_set_*` calls. Lines 347-368 (in savePresetToString) do the exact same sequence. 11 identical `spu94_set_*` + atomic-load pairs are copy-pasted.

**Fix:** Extract into a private `pushMixerState(spu94_state*)` method called from both locations. (This also partially resolves BUG-01, since the extracted method could be called from the audio thread context.)

---

### RD-04: Engine teardown loop duplicated three times

**File:** `src/standalone/PluginProcessor.cpp:31-39, 50-57, 95-102`
**Severity:** REDUNDANT

The destructor (lines 31-39), `prepareToPlay` (lines 50-57), and `releaseResources` (lines 95-102) all contain an identical `for (int e = 0; e < 2; ++e) { if (engines[e]) { spu94_destroy(engines[e]); engines[e] = nullptr; } }` loop.

**Fix:** Extract into a `destroyEngines()` helper.

---

## SIMPLIFY Findings

### SM-01: Error Accumulator knob callbacks could use a binding pattern

**File:** `src/standalone/PluginEditor.cpp:433-447`
**Severity:** SIMPLIFY

Five identical lambda patterns for the envelope follower knobs:
```cpp
slipAttackKnob.onValueChange = [this]() {
    processorRef.getSlipAttack().store((float)slipAttackKnob.getValue(), ...);
};
```
Each differs only in which knob/accessor pair is used. A binding helper or loop over a knob-accessor pair array would reduce this from 15 lines to 5.

**Fix:** Create a `bindKnobToAtomic(Slider&, std::atomic<float>&)` helper that sets `onValueChange` in one line per knob.

---

### SM-02: shDivision values array has misleading index-0 padding

**File:** `src/standalone/PluginEditor.cpp:495-501`
**Severity:** SIMPLIFY

The `divValues[]` array starts with a dummy `0` at index 0 so that `divValues[id]` works with 1-based ComboBox IDs. This is fragile -- an off-by-one in the ComboBox population or ID assignment would silently select the wrong division. The array has 15 elements for 14 divisions.

**Fix:** Use `divValues[id - 1]` with a 0-based array, eliminating the dummy element.

---

### SM-03: checkModified re-reads all atomics every 30Hz timer tick

**File:** `src/standalone/PluginEditor.cpp:805-829`
**Severity:** SIMPLIFY

`updatePresetDisplayName()` calls `checkModified()` on every timer tick (30 Hz). `checkModified` iterates all 35 registers plus 11 mixer/DAC values, performing 46 atomic loads and comparisons per tick. The display only needs updating when a knob actually changes.

**Fix:** Set a `dirty` flag in each `onValueChange` callback instead of polling. Only call `checkModified()` when the dirty flag is set.

---

---

_Reviewed: 2026-05-07_
_Reviewer: Claude (JUCE standalone audit)_
_Depth: deep_
