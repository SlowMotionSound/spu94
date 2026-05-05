# Phase 18: I/O Surfaces - Context

**Gathered:** 2026-05-03
**Status:** Ready for planning

<domain>
## Phase Boundary

The CLI gains a `--tempo` flag to set BPM before processing. The JUCE standalone GUI gains a three-mode tempo sync selector (FREE/INT/EXT), a BPM field, a global subdivision control, and per-register subdivision dropdowns. EXT mode receives MIDI clock input for external tempo sync in the standalone app.

</domain>

<decisions>
## Implementation Decisions

### Sync modes
- **D-01:** Three sync modes: FREE (unclocked — registers are raw sample counts), INT (internal BPM — user-typed), EXT (external BPM — MIDI clock input)
- **D-02:** Entering INT or EXT mode snaps all sync-enabled registers to their assigned subdivisions via the existing auto-resnap machinery (`spu94_set_tempo` → auto-resnap pass). No new C core logic needed for the snap itself
- **D-03:** EXT mode uses MIDI clock (24 PPQN) to derive BPM. Works in standalone — no DAW host required. DAW AudioPlayHead is a future v1.6 plugin-milestone addition feeding the same `spu94_set_tempo` pipe

### CLI surface
- **D-04:** `--tempo <BPM>` flag on the `reverb` subcommand. Sets BPM (INT mode) and triggers resnap. No additional flags needed — subdivision assignments come from the loaded preset's `[tempo]` section or default to Global
- **D-05:** `--tempo` interacts cleanly with `--load-preset` (preset already has tempo/subdivision state) and `--preset` (factory presets have no tempo state, so `--tempo` applies INT mode with default Global subdivision on all registers)

### Per-register subdivision control
- **D-06:** Each of the 10 tempo registers gets a dropdown with three tiers: Free (unclocked, ignores tempo), Global (follows the global subdivision setting), or an individual musical division (1/1 through 1/16, straight/dotted/triplet)
- **D-07:** Global subdivision control — one master setting that all "Global" registers follow. When entering a synced mode (INT/EXT), all registers default to Global
- **D-08:** Individual division override — user can pull any register out of Global and assign a specific subdivision. That register keeps its individual assignment until explicitly returned to Global

### Sync group toggles
- **D-09:** Reflection sync and comb sync group toggles (existing C core API: `spu94_set_reflection_sync`, `spu94_set_comb_sync`) are NOT surfaced in this phase. Per-register dropdowns provide equivalent flexibility. Group toggles may be added later without architectural changes

### Claude's Discretion
- GUI layout and zone placement for tempo controls (toolbar vs dedicated zone vs integrated)
- MIDI device selection UI approach (dropdown, auto-detect, etc.)
- MIDI clock jitter smoothing strategy (moving average window, etc.)
- BPM field input widget style (text box, spinner, etc.)
- How to visually distinguish Free/Global/Individual states in the per-register dropdown
- Whether the global subdivision control needs its own label/section or can be inline with the mode selector

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### CLI surface (being extended)
- `src/cli/cmd_reverb.c` — getopt_long flag parsing, preset loading, ADPCM/DAC flag patterns. Add `--tempo` flag here following the established pattern.
- `src/cli/main.c` — Subcommand dispatcher. No changes needed (reverb subcommand handles tempo).

### JUCE standalone (being extended)
- `src/standalone/PluginEditor.cpp` — GUI construction, 4-zone layout, timer-based state sync, preset management. Tempo controls added here.
- `src/standalone/PluginEditor.h` — Editor class members. New tempo-related widgets declared here.
- `src/standalone/PluginProcessor.cpp` — Audio processing, atomic parameter bridge, preset queue. MIDI clock listener and tempo atomics added here.
- `src/standalone/PluginProcessor.h` — Processor class with atomic parameter getters. New tempo parameter atomics here.
- `src/standalone/RegisterPanel.cpp` — 35-register slider panel. Per-register subdivision dropdowns may integrate here or in a parallel component.
- `src/standalone/ParameterBridge.cpp` — Register shadow values for GUI↔audio thread sync.

### C core tempo API (already implemented)
- `include/spu94/spu94.h` §spu94_set_tempo through §spu94_restore_binding_grid — Full tempo API surface: set/get BPM, subdivision enums, binding states, subdivision string conversion, tempo register names
- `src/spu94/spu94_tempo.c` — Subdivision table, auto-resnap, write-interception hook, binding state management
- `src/spu94/spu94_state_internal.h` — Private state struct with tempo fields (tempo_bpm, bind arrays, sync toggles)

### Preset format (already handles tempo)
- `src/spu94/spu94_preset_io.c` — [tempo] section serializer/parser. Presets loaded via `--load-preset` already carry full tempo state.

### Requirements
- `.planning/REQUIREMENTS.md` — TEMPO-07 (CLI --tempo), TEMPO-08 (BPM field in GUI), TEMPO-09 (subdivision selectors)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_set_tempo` / `spu94_get_tempo`: BPM storage with auto-resnap. CLI and GUI both call this.
- `spu94_set_subdivision` / `spu94_get_binding_subdivision`: Per-register subdivision assignment. GUI dropdowns map directly to these.
- `spu94_subdivision_to_string` / `spu94_subdivision_from_string`: String ↔ enum conversion. Useful for dropdown labels.
- `spu94_tempo_reg_name`: Register name strings (e.g. "dAPF1"). Useful for dropdown labels.
- `spu94_subdivision_valid`: Checks whether a BPM/subdivision combination fits in uint16. Useful for graying out invalid subdivisions in dropdowns.
- Existing `getopt_long` flag pattern in `cmd_reverb.c`: `--dac`, `--adpcm`, `--load-preset` patterns to follow for `--tempo`.
- Atomic parameter bridge pattern (`std::atomic<float>` getters in PluginProcessor): Model for BPM and sync mode atomics.
- Timer-based GUI sync (`timerCallback` at 30Hz): Detect tempo changes and update dropdown states.

### Established Patterns
- CLI flags use `getopt_long` with `required_argument` / `no_argument` and numbered cases for long-only options (1001, 1002, etc.)
- GUI↔audio thread communication via `std::atomic` stores in PluginProcessor, polled by 30Hz timer in PluginEditor
- Rotary knobs for continuous values, toggles for booleans, dropdowns for enumerations
- Modified-state tracking (asterisk on preset name) — tempo changes should trigger this too

### Integration Points
- `cmd_reverb.c`: Add `--tempo` to `long_opts[]`, parse and call `spu94_set_tempo` before processing
- `PluginProcessor`: Add MIDI clock listener (juce::MidiInputCallback), tempo atomics, mode state
- `PluginEditor`: Add tempo mode selector, BPM field, global subdivision dropdown, per-register subdivision dropdowns
- Preset baseline snapshot: Add tempo state fields so modified-state tracking includes tempo changes

</code_context>

<specifics>
## Specific Ideas

- FREE/INT/EXT as a three-way selector mirrors how hardware synths handle clock source — immediately familiar to the target audience (musicians with synth/Eurorack experience)
- The three-tier dropdown (Free/Global/Individual) means a user can get tempo-synced reverb with just two actions: set mode to INT, type a BPM. Everything snaps to the global subdivision. Individual overrides are there when you want them, but the default path is minimal.
- MIDI clock in standalone mode means a drum machine or sequencer plugged in via USB-MIDI can drive the reverb timing — no DAW required

</specifics>

<deferred>
## Deferred Ideas

- **Sync group toggles (reflection_sync / comb_sync)** — per-register dropdowns provide equivalent flexibility for now. Group toggles can be surfaced later as a convenience feature.
- **DAW host tempo sync (AudioPlayHead)** — deferred to v1.6 plugin milestone. Same `spu94_set_tempo` pipe, different source.
- **Tempo-modulated delays** — smooth real-time BPM transitions with crossfade/interpolation. Lever layer feature.

</deferred>

---

*Phase: 18-i-o-surfaces*
*Context gathered: 2026-05-03*
