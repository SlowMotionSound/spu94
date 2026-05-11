# Phase 23: GUI Overlay + Display - Context

**Gathered:** 2026-05-05
**Status:** Ready for planning

<domain>
## Phase Boundary

Overlay the existing raw register panel with a musical macro control surface as the default view. Room Designer with walls, tap positions, diffusion (including snap), decay/reflectivity, early reflections, buffer, and echo physics (including Sync/Free snap). Advanced toggle swaps back to the raw register sliders. Human-readable units on all macro controls. Safety constraints enforced on both surfaces. vLIN/vRIN/vLOUT/vROUT hidden from all GUI surfaces.

</domain>

<decisions>
## Implementation Decisions

### View architecture
- **D-01:** Overlay swap model. Macro panel replaces the register viewport entirely. Advanced toggle swaps the viewport content between macro view and raw register view. Only one visible at a time.
- **D-02:** Single-screen layout. All macro controls visible at once, no scrolling. Window sized to fit everything with generous spacing. Coherent and intuitive grouping.
- **D-03:** Clearly designated sections with visual dividers. Easy on the eyes — not too dense. Expand the window rather than cramming controls.

### Control appearance
- **D-04:** Color scheme is prototype-quality for this phase. Use existing PS1 palette (light gray #B0B0B0, dark gray #5A5A5A, teal #6FD8CE, mauve #D49EBF, coral #E8736E, blue #7079CC) loosely applied. Specific color-to-control mapping will be refined later with user input.
- **D-05:** Rotary knobs for all macro controls. More compact than linear sliders, fits the dense single-screen layout.

### Value display
- **D-06:** Human unit values displayed as small text labels below each knob (e.g., "12.4 ms", "67%", "3.2 m").
- **D-07:** Bipolar knobs (Decay, Reflectivity) show signed percentage: "-12%" through "0%" through "+100%". Center detent displays "0%".
- **D-08:** Raw register sliders in Advanced mode show dual readout: raw hex value plus human unit (per UNIT-02).

### Echo Physics + Snap
- **D-09:** Sweep, Spread, and Rotate knobs are always usable in both Free and Sync modes. In Free mode they control continuous Phase 21 Spread+Sweep. In Sync mode they control discrete subdivision transforms. The knobs are never grayed out.
- **D-10:** Per-register subdivision dropdowns are always visible but grayed out (disabled) in Free mode. Active in Sync mode. This keeps the layout stable and shows the user what subdivisions are "loaded" even when not active.

### Diffusion snap
- **D-11:** Diffusion snap controls (dAPF1/dAPF2 Sync/Free toggle + 2 subdivision dropdowns) live inside the diffusion section alongside Amount, Texture, and Position controls. Not a separate section.

### Hidden controls
- **D-12:** vLIN/vRIN/vLOUT/vROUT are hidden from both macro panel and raw register panel (per SAFE-05). They are set by presets but never shown because the mixer already controls input/output levels.

### Claude's Discretion
- Exact section ordering and grouping within the single-screen layout
- JUCE component hierarchy (whether macro panel is a new Component subclass or restructured PluginEditor)
- How the Advanced toggle is visually presented (button, tab, etc.)
- Precise unit conversion formulas (register value → meters, ms, etc.)
- How link/constrain toggles are visually presented alongside their parent controls
- Test strategy (if any GUI-level testing is feasible)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing GUI
- `src/standalone/PluginEditor.h` — current editor with register viewport, mixer, DAC, tempo sync
- `src/standalone/PluginEditor.cpp` — 911-line editor implementation, PS1 color setup, all current controls
- `src/standalone/RegisterPanel.h` — raw register sliders with QuantizedSlider, group headers
- `src/standalone/RegisterPanel.cpp` — 176-line register panel layout
- `src/standalone/ParameterBridge.h` — register bridge between JUCE and C core

### Macro engine (Phase 20-22 C core)
- `include/spu94/spu94_macro.h` — macro group types, Spread+Sweep apply, derive API
- `include/spu94/spu94_snap.h` — Sync/Free toggle, subdivision assignment, Sweep/Spread/Rotate transforms
- `src/spu94/spu94_macro_controls.c` — all 10 group definitions with register lists
- `src/spu94/spu94_state_internal.h` — state struct with macro fields, snap fields, safety fields

### Safety layer (Phase 20)
- `include/spu94/spu94_safety.h` — stability ceiling, address bounds API

### Color scheme
- `tools/color_swatch.html` — PS1 controller color swatch preview

### Planning
- `.planning/phases/21-macro-controls/21-CONTEXT.md` — Room Designer control surface decisions
- `.planning/phases/22-echo-speed-diffusion-snap/22-CONTEXT.md` — Sync/Free snap decisions
- `.planning/REQUIREMENTS.md` — GUI-01..05, SAFE-05, UNIT-01..02 definitions

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `RegisterPanel` component: existing raw register sliders with group headers. Becomes the Advanced view.
- `RegisterBridge` / `ParameterBridge`: existing bridge between JUCE GUI and C core register state. Macro panel can use the same bridge for reading register values.
- PS1 color palette already applied in PluginEditor constructor via LookAndFeel.
- `QuantizedSlider`: custom slider with snap function — may be useful for macro knobs with detents.
- Tempo sync controls (BPM field, sync mode selector, per-register dropdowns) already exist — may need restructuring to fit macro panel layout.
- Preset selector and modified-state tracking already implemented.

### Integration Points
- `spu94_macro_apply_*` functions: macro panel knobs call these to write register values through the macro engine.
- `spu94_macro_derive_*` functions: on preset load or Advanced→Macro switch, derive knob positions from current register state.
- `spu94_snap_*` functions: Sync/Free toggle and snap dropdown changes call these.
- `spu94_safety_*` functions: both surfaces must enforce safety constraints.
- Timer callback (existing 30Hz poll): already syncs register values from processor to GUI.

### Patterns
- Existing GUI uses `addAndMakeVisible` + manual `resized()` layout (no JUCE layout managers).
- All processor communication is via atomic variables in ParameterBridge (no JUCE AudioProcessorValueTreeState).
- Preset changes trigger `updateFromShadows()` to sync slider positions.

</code_context>
