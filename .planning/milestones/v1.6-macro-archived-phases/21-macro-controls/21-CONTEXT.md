# Phase 21: Macro Controls - Context

**Gathered:** 2026-05-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Define the full macro control surface in the C core: Room Designer (walls, echo speeds, tap positions, diffusion, buffer), Decay/Reflectivity coupling, Early Reflections, and all Spread+Sweep macro pairs. Wire everything through the Phase 20 macro engine with bipolar knob support, link/constrain toggles, vIIR-specific safety constraints (SAFE-03/SAFE-04), and preset-to-macro derivation. No GUI, no CLI flags — pure C core addition building on the macro engine and safety layer from Phase 20.

The control surface has expanded significantly from the original 8 macro knobs to ~30 controls and toggles. This reflects a coherent Room Designer model where every SPU register has a musically meaningful control.

</domain>

<decisions>
## Implementation Decisions

### Macro knob model — universal rules
- **D-01:** All multi-register macros use **Spread + Sweep**. Spread controls the proportional spacing between registers (collapse ↔ preset reference ↔ exaggerated). Sweep moves the whole set across its range while maintaining proportional relationships.
- **D-02:** Preset's original register values are the reference anchor for all macros. User can override individual registers in advanced mode to set a new reference. Macros operate from whatever that reference is.
- **D-03:** Signed registers can sweep into negative territory. Unsigned registers sweep within their positive range. Same concept, range determined by register type.
- **D-04:** No hardcoded floor values. Proportional relationships come from the preset (or user overrides).

### Room Designer — Walls
- **D-05:** 4 wall controls, each with distance + echo speed paired together:
  - Left Wall: distance (mLSAME) + echo speed (dLSAME)
  - Right Wall: distance (mRSAME) + echo speed (dRSAME)
  - Left Cross: distance (mLDIFF) + echo speed (dLDIFF)
  - Right Cross: distance (mRDIFF) + echo speed (dRDIFF)
- **D-06:** Per-wall link toggle: tethers echo speed to distance. When linked, moving distance also moves echo speed proportionally. When unlinked, they're independent (SPU physics-breaking — echo speed decoupled from wall distance).
- **D-07:** Same/Cross link toggle: tethers same-side and cross-side pairs for physical realism. Independent by default.

### Room Designer — Echo Speed macro
- **D-08:** Spread + Sweep macro over all 4 echo speeds (dLSAME, dRSAME, dLDIFF, dRDIFF). Preset proportions as default reference, user can override.

### Room Designer — Tap Positions
- **D-09:** 8 individual controls (mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4). Each tap independently positioned — not locked to L/R pairs.
- **D-10:** Spread + Sweep macro on top of the 8 individual controls.
- **D-11:** Wall constrained/unconstrained toggle. Constrained: tap positions clamped within respective wall boundaries. Unconstrained: taps can go anywhere in the buffer.

### Room Designer — Diffusion
- **D-12:** Diffusion Amount — Spread + Sweep (vAPF1, vAPF2). How hard each all-pass diffuser works.
- **D-13:** Diffusion Texture — Spread + Sweep (dAPF1, dAPF2). Grain size of the smear.
- **D-14:** Diffusion Position — 4 individual controls (mLAPF1, mRAPF1, mLAPF2, mRAPF2) + Spread + Sweep macro. Where each diffusion stage sits in the room. Preset proportions as default, user can override. Constrained/unconstrained toggle.

### Room Designer — Room Size master
- **D-15:** Single knob. Scales all m-prefix registers proportionally. Also scales echo speeds when per-wall link toggles are active. This is the top-level "make the whole room bigger or smaller" control.

### Room Designer — Buffer
- **D-16:** mBASE exposed as "Buffer" control. Fully up = maximum buffer space (mBASE at minimum value, biggest rooms possible). Pull down = crush available space. Safety layer clamps all m-prefix registers that exceed the shrinking boundary. Default is fully up.
- **D-17:** Inverted mapping — knob up = mBASE value down. User thinks in "more space" not "lower address."
- **D-18:** Snap-on-write semantics — changes are immediate, not smooth. Inherently dramatic/destructive-creative.

### Decay + Reflectivity
- **D-19:** Decay — bipolar center-detent (vIIR). Left half = negative range (-0x1000 to 0), right half = positive range (0 to 0x7FFF), linear within each half. Center = zero detent.
- **D-20:** SAFE-03: vIIR floor at -0x1000 (~-12.5%), enforced by the Decay macro definition.
- **D-21:** SAFE-04: -0x8000 (INT16_MIN) unreachable — Decay macro floor is -0x1000, no path bypasses the safety layer.
- **D-22:** Reflectivity — bipolar center-detent (vWALL). Sweeps through full signed range, negative through positive.
- **D-23:** Auto-rederive coupling: when Decay changes vIIR, the system immediately re-derives Reflectivity's knob position from the now-clamped vWALL value. Knob always shows truth.
- **D-24:** Stability coupling: Reflectivity's range dynamically constrained by current vIIR value (abs(vIIR) × abs(vWALL) <= SPU94_STABILITY_LIMIT).

### Early Reflections
- **D-25:** Spread + Sweep (vCOMB1, vCOMB2, vCOMB3, vCOMB4). Spread: left = collapse all to zero (no early reflections), center = preset/user-defined proportional weights, right = exaggerated weights maintaining same ratios. Sweep: slides all 4 through the signed range maintaining spacing. Both halves proportional — ratios preserved the whole way.
- **D-26:** Individual vCOMB sliders set the proportional relationship (the reference). Preset weights are default, user can override.

### Width — REMOVED
- **D-27:** Separate Width macro removed. Stereo width is already fully controllable via the wall distance controls (mLSAME/mRSAME vs mLDIFF/mRDIFF) in the Room Designer. Same/Cross link toggle covers the linked vs independent use case.

### Preset derivation
- **D-28:** Loading a .spu94 preset sets register values, then all macros derive their positions from register state without altering any register value.
- **D-29:** All macros store preset reference values alongside the engine's existing base values. The engine needs a "reference values" concept for the interpolation/Spread model.

### Bipolar engine extension
- **D-30:** The Phase 20 engine's unipolar [0,1] apply model needs a bipolar path. Bipolar controls: Decay, Reflectivity, and all Sweep macros on signed register groups (Early Reflections Sweep, Diffusion Amount Sweep). Unipolar: all Spread macros, Room Size master, Buffer, all distance/position/texture controls.

### Claude's Discretion
- Concrete floor/ceiling values for each register in each group definition
- How the bipolar apply path integrates with the existing spu94_macro_apply (separate function vs mode flag)
- Internal storage layout for reference values
- Whether the Reflectivity re-derive trigger lives in the Decay apply path or in a post-write hook
- How link toggles are represented in the C core (bitfield, bool array, etc.)
- How constrained/unconstrained mode interacts with the safety layer
- Test file organization and specific test scenarios
- Whether SPU94_MACRO_MAX_GROUPS (currently 8) needs increasing for the expanded control count

</decisions>

<specifics>
## Specific Ideas

- The interpolation/reference model means macros need a reference snapshot from the preset and an interpolation path, not just the base-value snapshot the engine currently stores.
- The auto-rederive coupling between Decay and Reflectivity is the only cross-macro dependency. All other macros operate independently.
- mBASE as "Buffer" control is a creative destructive effect — room crush via buffer shrinkage. Noted as future sequenceable effect.
- Wall constrained/unconstrained mode and same/cross link toggle give the user a spectrum from physically realistic rooms to impossible SPU spaces. Default is unconstrained + unlinked (full exploration), toggles available for physical realism.
- Every factory preset has mBASE = 0x0000 because on the PS1, mBASE is a memory management parameter (voice samples vs reverb buffer allocation), not a reverb design parameter. In SPU-94 it becomes a creative control.

</specifics>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Macro engine (Phase 20 output)
- `include/spu94/spu94_macro.h` — macro group types, engine API (register_group, derive, apply, get_range), SPU94_MACRO_MAX_GROUPS/MAX_MEMBERS
- `src/spu94/spu94_macro.c` — proportional scaling model, gang clamping, re-derivation logic, compute_max_scale
- `src/spu94/spu94_safety.c` — vIIR×vWALL stability ceiling (SAFE-01), m-prefix address bounds (SAFE-02)

### Register definitions
- `include/spu94/spu94_registers.h` — register enum (35 registers), signedness, typed setters/getters
- `include/spu94/spu94_register_facade.h` — per-register named wrappers
- `src/spu94/spu94_register_io.c` — typed setter/getter implementation

### Internal state
- `src/spu94/spu94_state_internal.h` — struct spu94_state layout (macro_group_defs[], macro_base_values[], macro_knob_pos[], macro_writing flag)

### Preset system
- `src/spu94/spu94_presets.c` — factory preset table with 35-register vectors, mBASE = 0x0000 for all presets (ADR-Phase-6-G)
- `src/spu94/spu94_preset_io.c` — user preset save/load (derive trigger point)

### Current GUI (for understanding existing register layout)
- `src/standalone/RegisterPanel.cpp` — current register slider groups and layout (lines 151-169)

### Planning
- `.planning/ROADMAP.md` — Phase 21 goal and success criteria (will need updating for expanded scope)
- `.planning/REQUIREMENTS.md` — CTRL-01..08, SAFE-03, SAFE-04, PRESET-01 definitions (will need updating)
- `.planning/phases/20-macro-engine-safety-core/20-CONTEXT.md` — Phase 20 decisions (engine architecture, register group ownership)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_macro_register_group` / `spu94_macro_apply` / `spu94_macro_derive`: unipolar engine from Phase 20, needs bipolar extension + Spread/Sweep model
- `spu94_safe_set_reg_i16` / `spu94_safe_set_reg_u16`: safety enforcement already routes through these — macro writes use them automatically
- `spu94_macro_group_t` / `spu94_macro_member_t`: group definition structs — may need extension for link toggles, constrained/unconstrained, reference values
- `SPU94_STABILITY_LIMIT` (0x40000000): stability ceiling constant used by safety layer

### Established Patterns
- Tempo system (`spu94_tempo.c`): per-register state arrays indexed by dedicated enum — closest analog to macro group coordination
- Re-entrancy guard (`state->macro_writing`): prevents hooks from interfering during batch writes
- `spu94_result_t` append-only enum: new result codes for bipolar or coupling operations should follow existing pattern

### Integration Points
- `spu94_macro_apply` → `spu94_safe_set_reg_*`: all macro writes go through safety enforcement
- Preset load path (`spu94_preset_io.c`): derive trigger for all macros after register values are set
- `macro_base_values[][]` in state struct: will need companion `macro_ref_values[][]` for the reference/interpolation model
- `SPU94_MACRO_MAX_GROUPS` (currently 8): likely needs increasing for the expanded control count

</code_context>

<deferred>
## Deferred Ideas

- mBASE "Buffer" as sequenceable effect — future automation/modulation target
- Naming refinement for Wall Constrained/Unconstrained toggle — placeholder name, refine in GUI phase

</deferred>

---

*Phase: 21-macro-controls*
*Context gathered: 2026-05-04*
