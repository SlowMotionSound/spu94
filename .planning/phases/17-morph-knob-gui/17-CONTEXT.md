# Phase 17: Morph Knob GUI - Context

**Gathered:** 2026-05-06
**Status:** Ready for planning

<domain>
## Phase Boundary

JUCE GUI for the preset interpolation engine: a single large rotary knob that drives `spu94_interp_set_morph` in real time, with waypoint tick marks at each of the 9 Sony factory preset positions and a dynamic label showing preset names or numerical position.

</domain>

<decisions>
## Implementation Decisions

### Placement
- **D-01:** Macro/Advanced toggle pattern -- reuse the archived `MacroPanel` overlay approach from `archive/v1.5-v1.6-macro-approach`. A toggle button in the toolbar switches between the morph knob panel (macro, default) and the raw register panel (advanced). Both share the same bounds in Zone 2; one is visible at a time via `setVisible`.
- **D-02:** Macro view is the default on startup. Register panel is hidden until the user clicks "Advanced".
- **D-03:** The existing toolbar (Zone 1) and mixer/DAC bar (Zone 3) are unchanged. Only Zone 2 content switches.

### Waypoint Indicators
- **D-04:** 9 tick marks around the knob arc at equal angular spacing, using the PS1 color palette (teal/mauve/coral/blue).
- **D-05:** Dynamic label zone below the encoder. On a waypoint detent: shows the preset name (e.g., "Hall", "Echo"). Between detents: shows a numerical value 0.0-100.0 derived from the morph position (0.0-1.0 scaled to 0.0-100.0).
- **D-06:** No preset name labels permanently visible around the arc -- dots/ticks only. The dynamic label is the sole text indicator.

### Knob Feel
- **D-07:** Detent behavior at the 9 waypoint positions. The knob thumb settles into exact waypoint values (0/8, 1/8, ... 8/8) when close. No magnetic physics or spring-back for v1.5 -- simple detent snap.
- **D-08:** Between detents, the knob moves freely (continuous, not stepped).

### Thread Bridge
- **D-09:** Single `std::atomic<float>` for morph position. GUI thread stores on knob change. Audio thread reads in `processBlock` and calls `spu94_interp_set_morph(state, position)`. Same pattern as the archived `getMacroRoomSize()` atomics.
- **D-10:** No command queue needed -- morph position is latest-value semantics. Overwritten values between audio callbacks are inaudible.

### Claude's Discretion
- Knob diameter within the 250-300px range -- choose based on available Zone 2 space
- Tick mark size and exact PS1 palette color assignments for tick marks vs arc vs thumb
- Detent snap threshold (how close the knob must be to a waypoint before snapping)
- Numerical display precision (0.0 vs 0 vs 0.00) in the label zone between detents

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Archived Macro Panel (reference implementation)
- `archive/v1.5-v1.6-macro-approach:src/standalone/MacroPanel.cpp` -- archived macro overlay panel with PS1 palette, setupRotaryKnob helper, timer-based knob sync, unit labels. Strip away all controls; reuse toggle pattern, color constants, and layout structure.
- `archive/v1.5-v1.6-macro-approach:src/standalone/MacroPanel.h` -- header with class definition
- `archive/v1.5-v1.6-macro-approach:src/standalone/PluginEditor.cpp` -- advanced toggle wiring, setVisible swap, startup derive, timer callback pattern

### Current GUI
- `src/standalone/PluginEditor.cpp` -- current editor layout (900x1100, 3 zones)
- `src/standalone/PluginEditor.h` -- editor class definition
- `src/standalone/ParameterBridge.cpp` -- atomic shadow pattern for register bridge
- `src/standalone/ParameterBridge.h` -- RegisterBridge and PresetCommandQueue classes
- `src/standalone/PluginProcessor.cpp` -- audio thread processBlock
- `src/standalone/PluginProcessor.h` -- processor class with atomic getters

### Interpolation Engine (Phase 16 output)
- `src/spu94/spu94_interp.c` -- the C function this knob drives
- `include/spu94/spu94.h` -- public API: `spu94_interp_set_morph`, `SPU94_INTERP_WAYPOINT_COUNT`

### Requirements
- `.planning/REQUIREMENTS.md` -- GUI-01 (single rotary knob), GUI-02 (9 dot markers), GUI-03 (real-time update)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `MacroPanel::setupRotaryKnob()` (archived) -- configures Slider::Rotary with PS1 palette colors, text box, double-click-return-to-default
- `MacroPanel::setupLabel()` (archived) -- centered label with psxLightGray
- PS1 color constants (archived): psxDarkGray(0xFF5A5A5A), psxLightGray(0xFFB0B0B0), psxTeal(0xFF6FD8CE), psxMauve(0xFFD49EBF), psxCoral(0xFFE8736E), psxBlue(0xFF7079CC)
- `advancedToggle` button pattern (archived) -- toggle text, setVisible swap, state sync on switch

### Established Patterns
- Manual `setBounds` layout (no FlexBox/Grid) -- all existing panels use explicit pixel coordinates
- `isUpdatingFromTimer` guard flag -- prevents feedback loops when timer updates knob positions
- `juce::dontSendNotification` on programmatic knob updates -- avoids re-triggering onChange
- `std::atomic` + `std::memory_order_relaxed` for GUI-to-audio float transport

### Integration Points
- `PluginProcessor::processBlock` -- reads atomic morph float, calls `spu94_interp_set_morph(state, position)` each buffer
- `PluginEditor` timer callback -- updates macro panel knob position and label from current state
- `PluginEditor` constructor -- adds macro panel, advanced toggle, wires visibility swap
- `PluginEditor::resized()` -- places macro panel and register viewport at same bounds

</code_context>

<specifics>
## Specific Ideas

- The morph knob should feel like a single big encoder dominating the macro panel -- minimal surrounding UI
- Label zone transitions: turning through the in-between spaces shows the numerical position value, then when the knob snaps to a preset detent tick, the label switches to show the preset name
- Reference the archived MacroPanel for the overall structure, but strip away all 20+ knobs and section headers -- this panel has exactly one control

</specifics>

<deferred>
## Deferred Ideas

- Magnetic physics / spring-back at waypoints (smooth pull toward detents) -- possible future enhancement
- Preset name labels permanently visible around the arc -- deferred, may revisit if users request

</deferred>

---

*Phase: 17-morph-knob-gui*
*Context gathered: 2026-05-06*
