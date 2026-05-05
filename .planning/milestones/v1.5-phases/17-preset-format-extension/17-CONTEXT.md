# Phase 17: Preset Format Extension - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

<domain>
## Phase Boundary

The existing .spu94 preset format gains a `[tempo]` section that serializes and restores full tempo state: BPM, sync group toggles, per-register binding states, and subdivision assignments. v1.4 presets (no tempo fields) continue to load without error. v1.5 presets load in v1.4 builds without crashing (unknown section/keys silently skipped by existing parser).

</domain>

<decisions>
## Implementation Decisions

### What gets saved
- **D-01:** Full fidelity — preset captures the complete tempo setup: BPM, reflection_sync, comb_sync, all 10 binding states (GRID/PROPORTIONAL/FIXED), subdivision assignments for grid-bound registers, and proportional ref BPMs
- **D-02:** Registers with FIXED binding state get no subdivision or ref_bpm fields (their raw hex values in [registers] are sufficient)

### Subdivision string format
- **D-03:** Compact musical notation — `1/4`, `1/8d`, `1/16t` where `d` suffix = dotted, `t` suffix = triplet. Straight subdivisions have no suffix
- **D-04:** The 15 subdivision strings: `1/1`, `1/1d`, `1/1t`, `1/2`, `1/2d`, `1/2t`, `1/4`, `1/4d`, `1/4t`, `1/8`, `1/8d`, `1/8t`, `1/16`, `1/16d`, `1/16t`

### Section layout
- **D-05:** New `[tempo]` section after `[dac]` in the preset file
- **D-06:** Section contains: `tempo=` (BPM as decimal integer), `reflection_sync=` and `comb_sync=` (0/1 booleans), then per-register fields using the tempo register name prefix (e.g. `dAPF1_bind=`, `dAPF1_sub=`, `dAPF1_ref_bpm=`)

### Load-time behavior
- **D-07:** Auto-snap on load — after restoring BPM, sync toggles, and binding metadata, the loader recomputes and writes sample counts for all grid-bound registers via `spu94_set_subdivision`. Guarantees BPM-to-register consistency even for hand-edited preset files
- **D-08:** v1.4 presets (no `[tempo]` section) load with tempo features inactive — BPM stays 0, all bindings remain FIXED. No backward compatibility break.

### Claude's Discretion
- Binding state string format for the file (e.g. `grid`, `proportional`, `fixed` vs abbreviations)
- Field ordering within the `[tempo]` section
- Whether to add a `# Tempo-synced delays` comment header in the section (consistent with existing section comments)
- Parser implementation: extend the existing section state machine with a `SECTION_TEMPO` enum value

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Preset serialization (the code being extended)
- `src/spu94/spu94_preset_io.c` — INI-style serializer/parser with section state machine. Save uses EMIT macro, load uses line-by-line parsing with key=value dispatch. This is the primary file being modified.
- `include/spu94/spu94.h` — Public API declarations for `spu94_preset_save` and `spu94_preset_load`. New tempo getter/setter declarations also here.

### Tempo subsystem (the state being serialized)
- `src/spu94/spu94_tempo.c` — Subdivision table, `spu94_set_tempo`, `spu94_set_subdivision`, binding state management, auto-resnap, write-interception hook. Must understand this to serialize/restore correctly.
- `src/spu94/spu94_state_internal.h` — Private state struct with `tempo_bpm`, `tempo_bind_state[]`, `tempo_bind_sub[]`, `tempo_bind_ref_bpm[]`, `reflection_sync`, `comb_sync` fields.
- `include/spu94/spu94_registers.h` — Register enum and type declarations. Tempo register enum (`spu94_tempo_reg_t`) and subdivision enum (`spu94_subdivision_t`).

### Requirements
- `.planning/REQUIREMENTS.md` — TEMPO-05 (optional `tempo=` field) and TEMPO-06 (subdivision names alongside hex values)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_preset_save` EMIT macro pattern: overflow-safe snprintf accumulation into caller-provided buffer. Extend with new EMIT calls for tempo fields.
- `spu94_preset_load` section state machine: `preset_section_t` enum with `SECTION_NONE`, `SECTION_REGISTERS`, `SECTION_MIXER`, `SECTION_DAC`. Add `SECTION_TEMPO`.
- `parse_hex_u16` and `parse_bool` static helpers in preset_io.c: reuse for tempo field parsing. New `parse_subdivision` helper needed for the compact musical strings.
- `spu94_set_tempo`, `spu94_set_subdivision`, `spu94_get_binding_state` API: use during load to restore and auto-snap state.

### Established Patterns
- Unknown keys silently ignored (D-09 from Phase 13): backward and forward compat mechanism already in place
- All 16-bit values formatted as 4-digit hex (`0xNNNN`) in [registers]: tempo section uses different formats (decimal BPM, musical strings, boolean toggles)
- Zero-heap contract: preset_save writes to caller-provided buffer, preset_load parses in-place. No allocations.
- Re-entrancy guard (`state->tempo_writing`): must be set during auto-snap-on-load to prevent binding state transitions

### Integration Points
- `spu94_preset_save`: append `[tempo]` section after `[dac]` section
- `spu94_preset_load`: add `SECTION_TEMPO` to section dispatch, parse tempo-specific keys
- Existing test infrastructure: extend preset round-trip tests to cover tempo fields
- CLI `cmd_preset_dump.c`: already calls `spu94_preset_save`, gets tempo section for free

</code_context>

<specifics>
## Specific Ideas

- The compact musical notation (`1/8d`, `1/16t`) matches how musicians talk about subdivisions — a preset file should read like a human wrote it
- Auto-snap on load means a hand-edited preset with `tempo=140` (changed from 120) will produce correct register values even if the user forgot to update the hex values in [registers]

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 17-preset-format-extension*
*Context gathered: 2026-05-03*
