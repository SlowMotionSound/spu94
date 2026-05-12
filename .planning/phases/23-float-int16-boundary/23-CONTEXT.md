# Phase 23: Float↔int16 Boundary - Context

**Gathered:** 2026-05-11
**Status:** Ready for planning

<domain>
## Phase Boundary

Extract the float↔int16 translation that currently lives inline inside `SrcChain::processIn` / `SrcChain::processOut` (see `src/plugin/SrcChain.cpp:45-56`) into an explicit module. The translation is the only place audible distortion can be introduced on the input side; making it a named module gives it a home for tests and for the Input-Gain repositioning decided below.

Locked by v1.7 requirements (PLUG-17..21):
- Float→int16 via clamp + truncation (no dither, no noise shaping)
- Clamp/saturation matches the C core's `sat_s16` semantics — no integer wrap on overflow
- int16→float via division by 32768.0f
- Side-channel limiter at `PluginProcessor.cpp:352-405` preserved unchanged
- Default Input Gain initialized to 0.5 (already the atomic default at `PluginProcessor.h:148`)

</domain>

<decisions>
## Implementation Decisions

### Input Gain placement (audible)
- **D-01:** Input Gain is moved **outside the int16 boundary** — applied as a float multiply on the host-rate signal *before* clamp + truncation, rather than as an int16 register inside the SPU engine. This gives the user real headroom: turning the knob down below unity attenuates the signal before it can be clipped by the boundary, rather than scaling an already-clipped result. Rationale: the standalone path's default of 0.5 (-6 dB) was always meant to imply headroom; with the previous engine-register placement that headroom didn't actually exist.

### Input Gain range (audible)
- **D-02:** The Input Gain knob extends **past unity, up to +24 dB of drive** (knob value range 0.0 → ~16.0). At positions ≤ 1.0 the knob is a clean trim; past 1.0 it deliberately drives the signal into the int16 ceiling, turning `sat_s16` into a usable distortion stage. This is the saturator/overdrive character authentic to how PS1 hardware actually clips when overdriven. Recording-engineer framing: this becomes a "drive" knob, not just a "level" knob.

### Coupling between D-01 and D-02
- Both decisions act on the same parameter. D-01 makes the knob a *real* attenuator at sub-unity values; D-02 makes the same knob a *real* drive control at super-unity values. Together they turn Input Gain from a passive trim into a character control whose default of 0.5 is now meaningful (-6 dB headroom below clip, plenty of room above for drive).

### Engine-side Input Gain register
- **D-03:** When Input Gain becomes a pre-clamp float multiply, the existing `spu94_set_input_gain` register inside the C core is no longer the primary gain stage on the plugin path. The planner decides whether to leave it pinned at unity (0x7FFF) on the plugin path, or whether to keep using it as a small post-clamp scaler for backward compatibility with preset round-trips. The C core is bit-faithful and must not be touched in a way that changes its standalone semantics.

### Claude's Discretion
- **Module home / file location** — where the new module lives (sibling file under `src/plugin/`, header-only, or shared into `c_core/spu94/include/` so the standalone path can use the same converter). No sound or feel impact; pure code organization. Planner picks based on testability and existing patterns.
- **Module shape** — class with `prepare()/process()` lifecycle to mirror `SrcChain`, or two named free functions (`f32_to_s16`, `s16_to_f32`). The converter is stateless (gain is decided outside it, see D-01); a class would be ceremony. Planner's call.
- **Whether the standalone testbed path picks up the same Input-Gain repositioning** — standalone is internal dev-only per v1.7, so the planner can decide whether parity matters for testing or whether the plugin path diverges.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Locked requirements
- `.planning/milestones/v1.7-REQUIREMENTS.md` §"Float ↔ int16 Boundary" — PLUG-17..21, plus the locked decisions table at the top (Truncate / sat_s16 / -6 dB default / side-channel limiter preserved)
- `.planning/milestones/v1.7-ROADMAP.md` Phase 23 entry — single-line phase description

### Current implementation to lift / modify
- `src/plugin/SrcChain.cpp:45-56` — current `f32_to_s16` and `s16_to_f32` inline lambdas, the code that becomes the explicit BoundaryConverter
- `src/plugin/SrcChain.cpp:254-307` (`processIn`) and `:309-367` (`processOut`) — call sites for the converters
- `src/plugin/PluginProcessor.cpp:80-90` (engine construction, default register writes), `:262-265` (atomic→engine sync per block), `:484-524` (preset save with atomic→engine sync) — Input Gain plumbing that must be reworked for the pre-clamp placement
- `src/plugin/PluginProcessor.h:148` — `inputLevel{0.50f}` atomic default
- `src/plugin/PluginProcessor.cpp:352-405` — existing side-channel limiter; **unchanged per PLUG-20**

### Hardware-fidelity anchor
- `.planning/research/PITFALLS-v1.7.md` — RT-safety + saturation hazards (referenced from Phase 22, still relevant)
- ADR-0001 (referenced by PLUG-18 for `sat_s16` semantics) — verify path exists or note absent during research

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `f32_to_s16` / `s16_to_f32` inline functions in `SrcChain.cpp:45-56` — already implement the locked clamp+truncate / divide-by-32768 semantics. Phase 23 lifts these into a named module, preserving the math byte-identically.
- `inputLevel` `std::atomic<float>` (`PluginProcessor.h:148`) — the existing UI→audio gain bridge. Pre-clamp placement reads from the same atomic; only the *point of application* changes.

### Established Patterns
- **Stateless RT-safe converters** — the existing inline functions are pure functions with no allocations, no locks, no syscalls. The new module must preserve that contract (caught by `pluginval --strictness-level 7` in CI per Phase 21).
- **Atomic→engine sync per block** — `processBlock` already pushes UI atomics into engine registers each block (see `PluginProcessor.cpp:262-265`). The pre-clamp Input-Gain path follows the same atomic-read pattern but applies the value in float-land before the boundary, not as a register write.
- **Plugin-path / standalone-path split in `processBlock`** (`PluginProcessor.cpp:357-477`) — the standalone testbed branch and the SRC-sandwich plugin branch are already separated; Input-Gain rework targets the plugin branch first.

### Integration Points
- Float pre-clamp Input-Gain multiply sits *before* `SrcChain::processIn` (or inside `processIn`'s host-float read loop, before `f32_to_s16` is called). Order: host buffer → input-gain multiply → SRC down to 44.1k float → clamp+truncate (BoundaryConverter) → `spu94_process`.
- Output side is unchanged in audible behavior: SPU int16 out → BoundaryConverter (int16→float) → SRC up to host rate → side-channel limiter → host buffer.

</code_context>

<specifics>
## Specific Ideas

- D-02 framing came from the user (recording engineer) — he picked +24 dB drive ("fuzz-pedal territory") over +6 dB ("gentle") and +12 dB ("preamp-style"). The plugin's Input Gain is intentionally aggressive: at the top of the knob, even quiet program material can be slammed into the int16 ceiling, making the boundary itself a creative effect. This matches the project's North Star — fixed-point quirks ARE the product.
- "Outside the door, knob can drive into clip" — user's own words for D-01 + D-02 combined. Treat the boundary clip as character, not as a defect to engineer around (no dithering, no soft-knee, no auto-makeup-gain).

</specifics>

<deferred>
## Deferred Ideas

- **Visual clip indicator** — Phase 23 makes the int16 boundary the dominant distortion source on the input side, so a meter / LED that lights when the converter is clamping would be informative. Out of scope for Phase 23 (UI surface change); revisit during the next UI-touching phase.
- **Move Save/Load preset actions into a top-left panel dropdown** — captured in `STATE.md` deferred ideas (2026-05-11), unrelated to Phase 23 but came up while looking at the editor.
- **Standalone-path parity for pre-clamp Input Gain** — listed under D-03/Claude's Discretion above; if planner punts on parity, capture as a follow-up so the testbed doesn't drift permanently from the plugin path.

</deferred>

---

*Phase: 23-float-int16-boundary*
*Context gathered: 2026-05-11*
