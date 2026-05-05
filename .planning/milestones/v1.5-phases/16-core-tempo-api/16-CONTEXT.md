# Phase 16: Core Tempo API - Context

**Gathered:** 2026-05-02
**Status:** Ready for planning

<domain>
## Phase Boundary

The C core stores BPM state, knows all musical subdivisions, and can snap any of the 10 delay registers to the nearest subdivision sample count at 22,050 Hz. Two group toggles control which registers participate in tempo sync. Three register binding states allow grid-bound, proportional, and fully-fixed behaviors.

</domain>

<decisions>
## Implementation Decisions

### Which registers to snap
- **D-01:** 10 registers total — 6 d-prefix (dAPF1, dAPF2, dLSAME, dRSAME, dLDIFF, dRDIFF) + 4 virtual comb-delay targets (dCOMB1-4) that internally compute and write the corresponding mCOMB addresses relative to current buffer geometry
- **D-02:** Two independent group toggles in engine state: "reflection sync" (controls the 6 d-prefix registers) and "comb sync" (controls the 4 virtual comb delays). Each on/off independently — four possible combinations
- **D-03:** The 6 d-prefix registers snap echoes/reflections to the beat (obvious rhythmic effect). The 4 comb delays tune the reverb tail's resonant character to tempo-sympathetic frequencies (subtler, makes the room itself rhythmic)

### Snap behavior and binding
- **D-04:** Persistent binding — setting a subdivision on a register creates a live link. Changing BPM auto-resnaps all grid-bound registers
- **D-05:** Three register states: grid-bound (snapped to subdivision, recalculates on BPM change), proportional (manual value that scales with tempo but not quantized to grid), fully-fixed (raw sample count, ignores tempo entirely)
- **D-06:** Manual write to a grid-bound register transitions it to proportional state (default escape hatch). Explicit API call required to transition to fully-fixed
- **D-07:** Re-binding a proportional or fixed register to a subdivision returns it to grid-bound state

### Overflow handling
- **D-08:** Invalid BPM/subdivision combinations (result exceeds uint16 max) are rejected — register unchanged, error code returned
- **D-09:** Query function `spu94_subdivision_valid(bpm, subdivision)` lets callers check which subdivisions are available at a given BPM before attempting to bind
- **D-10:** At any BPM a musician would realistically use (40-300), all useful subdivisions are available. Only extreme edge cases (sub-30 BPM + dotted whole notes) overflow

### Computation method
- **D-11:** Pure integer math throughout — no floats anywhere in the tempo computation path. Formula: `samples = (60 * 22050 * sub_numerator) / (bpm * sub_denominator)` with integer division
- **D-12:** Integer division truncates (C default) — matching PS1 MIPS R3000A integer behavior. This gives a consistent "tight/forward" character where every delay tap is at or slightly shorter than the theoretical subdivision
- **D-13:** Subdivision ratios stored as rational fractions (numerator/denominator pairs) in a compile-time lookup table. 15 entries: 1/1, 1/2, 1/4, 1/8, 1/16 × {straight, dotted, triplet}

### Claude's Discretion
- Exact virtual comb-delay computation (how mCOMB addresses derive from desired delay time relative to buffer geometry)
- Internal struct layout for per-register binding state (enum for state + stored subdivision + reference BPM for proportional scaling)
- Error code choice for overflow rejection (SPU94_INVALID_ARG vs new code)
- Whether proportional state stores the BPM at which the manual value was written, or the ratio directly

</decisions>

<specifics>
## Specific Ideas

- The 4 comb-delay registers (dCOMB1-4) are virtual — they don't exist in PS1 hardware. They're a creative extension that computes mCOMB address writes from a desired delay time. The reverb "rings" in sympathy with the beat when these are engaged.
- Truncation character: every reflection arrives at or slightly before the theoretical grid line. The reverb leans forward, like a drummer on top of the beat. This is the PS1's native integer math character.
- "Groove-feel offset" concept noted for lever layer — a continuous timing bias knob (±1-5ms range) that would give perceptible tight/centered/laid-back character beyond the sub-sample truncation bias.

</specifics>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### C core API surface
- `include/spu94/spu94.h` — Public API umbrella; lifecycle, process, preset, mixer, DAC functions. New tempo API goes here.
- `include/spu94/spu94_registers.h` — Register enum (spu94_reg_t, 35 entries), typed get/set declarations. Shows d-prefix register identifiers.
- `include/spu94/spu94_register_facade.h` — Per-register static inline wrappers. Shows uint16 type for d/m-prefix registers and the set/get/get_pending pattern.

### Internal state structure
- `src/spu94/spu94_state_internal.h` — Private state struct. New tempo/binding fields go here.

### Existing patterns to follow
- `include/spu94/spu94.h` §spu94_preset_save/load — INI-style serialization pattern (Phase 17 extends this with tempo fields)
- `include/spu94/spu94.h` §spu94_set_adpcm_enabled — Boolean toggle pattern (model for reflection_sync/comb_sync toggles)
- `include/spu94/spu94.h` §spu94_result_t — Error code enum (append-only contract)

### Requirements
- `.planning/REQUIREMENTS.md` — TEMPO-01 through TEMPO-04 define Phase 16 scope

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_set_reg_u16` / `spu94_get_reg_u16`: Engine-layer typed register access — virtual comb writes will call these internally
- `spu94_register_facade.h` inline wrapper pattern: Template for new `spu94_set_tempo`, `spu94_get_tempo`, etc.
- `spu94_result_t` enum: Append new error codes for tempo overflow rejection
- TICK_LATCHED write policy: d-prefix registers already stage→commit on next tick; tempo snap writes go through same path

### Established Patterns
- No heap, no floats in stored state, rt_safety clean — enforced by existing CI gates
- Boolean toggle pattern (set/get pairs returning void/int) — model for sync group toggles
- SPU94_STATE_SIZE_MAX contract with _Static_assert — new fields must fit within 16384 bytes

### Integration Points
- `spu94_state` internal struct: Add BPM, per-register binding state, sync group flags
- `spu94_set_reg_u16`: Intercept writes to bound registers to transition state to proportional
- Phase 17 will extend `spu94_preset_save/load` to serialize tempo/subdivision fields

</code_context>

<deferred>
## Deferred Ideas

- **Groove-feel offset** — continuous timing bias knob (±1-5ms) for perceptible tight/centered/laid-back character. Lever layer feature, not Phase 16.
- **DAW host tempo sync** — reading BPM from AudioPlayHead. Plugin milestone (v1.6), requires JUCE DAW plugin architecture.
- **Tempo-modulated delays** — smooth real-time BPM transitions with crossfade/interpolation. Lever layer feature.

</deferred>

---

*Phase: 16-core-tempo-api*
*Context gathered: 2026-05-02*
