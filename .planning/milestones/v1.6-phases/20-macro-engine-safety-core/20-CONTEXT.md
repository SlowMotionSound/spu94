# Phase 20: Macro Engine + Safety Core - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning
**Source:** Derived from STATE.md decisions, ROADMAP.md, REQUIREMENTS.md, and codebase analysis

<domain>
## Phase Boundary

Build a C core macro engine that coordinates register groups via proportional scaling from current state. The engine enforces safety constraints (vIIR x vWALL stability ceiling, m-prefix address bounds) and supports gang clamping, dynamic knob range, and position re-derivation from raw register state. No GUI, no CLI flags, no JUCE changes -- pure C core library addition.

Phase 21 wires up the 8 specific macro control definitions through this engine. This phase builds the engine itself and the safety enforcement layer.

</domain>

<decisions>
## Implementation Decisions

### Macro Engine Architecture
- Macro engine lives in C core (`src/spu94/`) as new files -- not in JUCE, CLI, or Python
- Engine operates on relative scaling from current register state, not absolute lookup tables
- A macro group is a struct defining: which registers belong to the group, their signedness, and their floor/ceiling constraints
- Knob position is a normalized float or Q15 value that maps to the proportional range between floor and ceiling of the most-constrained register in the group
- Gang clamping: when any register in a group hits its ceiling or floor during a sweep, all registers stop together and ratios are preserved
- Dynamic knob range: the engine recalculates min/max from the most-constrained register so full knob travel always maps to the full available range
- Re-derivation: switching from raw to macro mode computes knob position from current register values (not stored last-position)
- Ratio preservation: if a user hand-sculpts register ratios in raw mode, macro sweep preserves those ratios via proportional scaling

### Safety Constraints
- vIIR x vWALL stability ceiling enforced at the engine level -- both macro and raw register writes are checked
- The stability ceiling is a product constraint: `abs(vIIR) * abs(vWALL) <= STABILITY_LIMIT` where the limit is derived from the PS1's observed clamping behavior
- Address bounds checking: all m-prefix (u16) address registers are clamped against `work_buf_size` -- no write can produce a byte offset beyond the buffer boundary
- Safety enforcement applies uniformly in both macro mode and direct register writes (raw mode)
- Safety enforcement hooks into the existing `spu94_set_reg_*` path or wraps it -- the engine never bypasses the typed register setters

### Register Group Ownership (Phase 20 defines engine, Phase 21 wires groups)
- Room Size: 14 m-prefix address registers (mLSAME, mRSAME, mLCOMB1-4, mRCOMB1-4, mLDIFF, mRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2)
- Echo Physics: 8 d-prefix registers (dCOMB1-4, dLSAME, dRSAME, dLDIFF, dRDIFF)
- Decay: vIIR (single register, special floor behavior)
- Reflectivity: vWALL (single register, stability-coupled to vIIR)
- Width: mSAME vs mDIFF address pairs (diverge/converge from ratio)
- Early Reflections: vCOMB1-4 (4 gain registers)
- Diffusion Amount: vAPF1, vAPF2 (2 gain registers)
- Diffusion Texture: dAPF1, dAPF2 (2 delay registers)

### Claude's Discretion
- Internal struct layout for macro group definitions
- Whether stability limit is a compile-time constant or runtime-configurable
- Whether safety clamping returns a result code or silently clamps (lean toward result code for consistency with existing `spu94_result_t`)
- Exact file naming for new source files (e.g., `spu94_macro.c` / `spu94_macro.h` vs alternatives)
- Whether to add a public header `spu94_macro.h` or extend `spu94.h`
- Test file organization

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Public API
- `include/spu94/spu94.h` -- lifecycle, result codes, preset API, tempo API
- `include/spu94/spu94_registers.h` -- register enum (35 registers), signedness classification, typed setters/getters, write-timing policy
- `include/spu94/spu94_register_facade.h` -- per-register named wrappers

### Internal State
- `src/spu94/spu94_state_internal.h` -- struct spu94_state layout (reg_values[], pending_values[], work_buf_size, tempo state)

### Register I/O
- `src/spu94/spu94_register_io.c` -- typed setter/getter implementation (safety hooks attach here or wrap this)

### Existing Patterns
- `src/spu94/spu94_tempo.c` -- tempo system: per-register binding state, sync groups, resnap logic (closest analog to macro group coordination)
- `src/spu94/spu94_presets.c` -- factory preset table with 35-register vectors
- `src/spu94/spu94_preset_io.c` -- user preset save/load (macro re-derivation triggers on preset load)

### Planning
- `.planning/ROADMAP.md` -- Phase 20 goal and success criteria
- `.planning/REQUIREMENTS.md` -- MACRO-01..05, SAFE-01..02 definitions

</canonical_refs>

<specifics>
## Specific Ideas

- The tempo system (`spu94_tempo.c`) is the closest existing pattern: it tracks per-register state (binding type, subdivision, ref BPM) in arrays indexed by a dedicated enum. The macro engine can follow a similar pattern with per-group state arrays.
- The existing `spu94_result_t` enum is append-only. New result codes for safety clamping (e.g., `SPU94_STABILITY_CLAMPED`, `SPU94_ADDRESS_CLAMPED`) should be appended.
- The `_Static_assert` on `sizeof(struct spu94_state)` caps at `SPU94_STATE_SIZE_MAX = 16384`. New macro engine fields in the state struct must fit within this budget. Current struct is well under -- plenty of room.
- The write-timing split (IMMEDIATE vs TICK_LATCHED) means safety enforcement on m-prefix registers needs to validate the pending value, not just the active value.
- mBASE has a snap-on-write side effect -- the macro engine must not interfere with this when scaling m-prefix registers.

</specifics>

<deferred>
## Deferred Ideas

- Independent clamping mode (ratio drift as individual registers hit ceilings) -- tracked in STATE.md deferred ideas
- Parameter slew / smoothing -- separate feature, not part of macro engine core

</deferred>

---

*Phase: 20-macro-engine-safety-core*
*Context gathered: 2026-05-03 via codebase analysis + accumulated STATE.md decisions*
