# Phase 13: Core Preset API - Context

**Gathered:** 2026-05-01
**Status:** Ready for planning

<domain>
## Phase Boundary

C functions to serialize all SPU state (35 registers + mixer faders + DAC controls) to a versioned, human-readable key=value text buffer and restore it with bit-identical fidelity. The format is INI-style with sections, hex values, and section-level comments. File I/O and GUI surfaces are Phase 14.

</domain>

<decisions>
## Implementation Decisions

### File Format (FMT)
- **D-01:** INI-style sectioned format with `[registers]`, `[mixer]`, `[dac]` section headers. Version line and metadata at top, outside any section.
- **D-02:** All 16-bit values written as 4-digit hex (`0x0000`–`0xFFFF`). Boolean toggles written as `0`/`1`. Consistent representation everywhere.
- **D-03:** Section-level `#` comment lines at the top of each section for orientation (e.g. `# SPU reverb registers (35 values, hex)`). No per-key inline comments.
- **D-04:** Name + description metadata fields at the top of the file, before any section. Self-documenting for sharing and browsing.

### State Coverage (STA)
- **D-05:** Preset captures exactly: 35 SPU registers, 6 mixer faders (input_gain, dry_fader, patina_fader, dry_send, patina_send, reverb_fader), latency_comp toggle, and 4 DAC controls (dac_enabled, dac_fir_enabled, dac_noise_enabled, dac_true_oversample).
- **D-06:** ADPCM toggle is NOT saved. ADPCM is always-on infrastructure with its own bus path — the patina_fader and patina_send mixer controls determine how much ADPCM coloration is heard. There is no user-facing ADPCM on/off toggle.
- **D-07:** Latency compensation (on/off) IS saved — it's a real user-facing toggle that affects dry/reverb phase alignment.

### Version Tolerance (VER)
- **D-08:** Missing keys get the engine's default value. Old presets always load into newer software — new features start at their defaults. No version-mismatch rejection.
- **D-09:** Unknown keys are silently ignored. Newer presets load in older software (losing unrecognized fields). Tolerant of hand-edit typos — the typo'd key is skipped, not a fatal error.

### Claude's Discretion
- API signature details (return type, error codes, buffer sizing strategy) — follow the zero-heap, caller-provides-buffer pattern established throughout the codebase
- Parser implementation approach — the codebase already vendors jsmn for JSON parsing in the CLI; a simple line-by-line key=value parser is straightforward
- Register write ordering during load — follow existing `spu94_load_preset` patterns for atomic state restoration
- Whether to expose factory presets through the new save format (serialize the 10 built-in presets as .spu94 text)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Public API
- `include/spu94/spu94.h` — All public state accessors: `spu94_set_*/spu94_get_*` pairs for mixer, DAC, ADPCM; `spu94_load_preset`; `spu94_snapshot_registers`; `spu94_preset_min_work_buf_size`
- `include/spu94/spu94_registers.h` — `spu94_reg_t` enum (35 entries), `spu94_reg_name()` for human-readable key names, `spu94_snapshot_registers()` for bulk register dump

### Engine State
- `src/spu94/spu94_state_internal.h` — Full layout of `struct spu94_state`: reg_values[35], mixer faders, DAC toggles, latency_comp, adpcm_enabled — defines what exists to serialize

### Existing Preset Mechanism
- `src/spu94/spu94_presets.c` — Factory preset table (10 presets) and `spu94_load_preset` implementation — pattern for atomic state restoration

### CLI Subcommand Pattern
- `src/cli/main.c` — Git-style subcommand dispatcher with strcmp routing (Phase 14 will add `preset-dump`/`preset-load` here)
- `src/cli/cmd_reverb.c` — Example of a full subcommand with getopt_long, help text, error handling

### Requirements
- `.planning/REQUIREMENTS.md` — PRE-01 through PRE-05 (Phase 13 scope)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_snapshot_registers()`: Dumps all 35 register values into a caller-provided int16_t array — direct feed for the [registers] section
- `spu94_reg_name()`: Returns bare register name strings ("vIIR", "mBASE") — direct key names for the preset file
- `spu94_get_*` accessors: Individual getters for every mixer fader and DAC toggle — iterate these for the [mixer] and [dac] sections
- `spu94_load_preset()`: Existing pattern for atomic register restoration — model for the load path

### Established Patterns
- Zero-heap hot path: all buffers caller-provided, no malloc in the library
- Opaque state struct: public API uses accessors, internal layout in spu94_state_internal.h
- Q15 fixed-point for all signal levels: mixer faders are int16_t in [0x0000, 0x7FFF]
- Unity test framework with ctest labels

### Integration Points
- New `spu94_preset_save` / `spu94_preset_load` functions join the public API in `include/spu94/spu94.h`
- New source file(s) in `src/spu94/` for serialization logic
- CMakeLists.txt update to add new source files to the libspu94 target
- Test infrastructure: new ctest target(s) for round-trip fidelity proof

</code_context>

<specifics>
## Specific Ideas

- The preset file should look clean and professional when opened in a text editor — like something you'd see in a well-documented audio tool
- Factory preset names ("Hall", "Studio Large", etc.) are the reference for what a good preset name looks like
- The format should be something a musician could plausibly hand-edit with a hex reference card

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 13-Core Preset API*
*Context gathered: 2026-05-01*
