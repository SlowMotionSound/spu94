# Phase 14: I/O Surfaces - Context

**Gathered:** 2026-05-02
**Status:** Ready for planning

<domain>
## Phase Boundary

CLI subcommands and JUCE GUI buttons that let users save engine state to .spu94 preset files and load .spu94 files back into the engine. The C core API (`spu94_preset_save`/`spu94_preset_load`) is complete from Phase 13 -- this phase wraps it in user-facing surfaces.

</domain>

<decisions>
## Implementation Decisions

### CLI Command Design (CLI)
- **D-01:** `preset-dump` is a standalone subcommand that exports factory presets as .spu94 text. Accepts `--preset <name>` as the source (required). Outputs to stdout by default, `-o <file>` writes to disk.
- **D-02:** `preset-dump` accepts an optional `--name <text>` flag to set the name metadata field. No `--desc` flag -- description stays empty (hand-edit if wanted). Factory preset name is NOT auto-filled; name is empty unless `--name` is provided.
- **D-03:** No `preset-load` subcommand. Instead, `reverb` gains a `--load-preset <file.spu94>` flag. One-shot workflow: `spu94 reverb --load-preset my.spu94 in.wav out.wav`. Consistent with existing `--preset`/`--config` pattern.
- **D-04:** `--load-preset` is mutually exclusive with `--preset` and `--config` (same way `--preset` and `--config` are already mutually exclusive).
- **D-05:** Mixer/DAC flags (`--dac`, `--patina`, etc.) override values after `--load-preset` applies the file. Same layering behavior as `--preset hall --dac` today -- preset sets the base, flags tweak.

### Save Dialog Behavior (SAVE)
- **D-06:** Save button opens a name prompt first (JUCE AlertWindow or similar), then the native file dialog. Two-step: name/description capture, then file path selection.
- **D-07:** Name prompt has two fields: name (required) and description (optional). Name pre-fills from the current factory preset name if on a factory preset, from the loaded .spu94 file's name field if on a custom preset, or blank for Init/no-name presets.
- **D-08:** The typed name becomes the default filename in the file dialog (e.g. name "Dark Hall" suggests "Dark Hall.spu94").

### Preset Selector After Load (SEL)
- **D-09:** After loading a custom .spu94 file, a dynamic entry appears at the top of the preset dropdown showing the loaded preset's name (from the file's name field, or the filename if no name). Visually distinguished from factory presets (e.g. prefix marker).
- **D-10:** Selecting a factory preset from the dropdown clears the custom entry and reverts to normal factory preset behavior.
- **D-11:** Modified state indicator: when any register/mixer/DAC value differs from the last loaded state (factory or custom), an asterisk appends to the preset name in the dropdown (e.g. "Hall *" or "Dark Hall *").
- **D-12:** Switching to a factory preset or loading a .spu94 file both reset the modified-state baseline. The asterisk tracks divergence from whatever was last loaded.

### Claude's Discretion
- JUCE AlertWindow styling and field layout for the name/description prompt
- Exact modified-state comparison mechanism (snapshot + timer-based diff vs. flag-on-change)
- `preset-dump` help text and error message wording
- Whether to add `--list-presets` to `preset-dump` help or share with `reverb --list-presets`
- CMake integration for new CLI source file (cmd_preset_dump.c)

</decisions>

<specifics>
## Specific Ideas

- CLI workflow: dump a factory preset as starting point, hand-edit the .spu94 file, process with `reverb --load-preset` -- the edit-process loop should feel natural
- Save prompt pre-fill from factory preset name removes friction for the common "tweak Hall and save as variant" workflow
- The asterisk modified indicator gives visual feedback that you have unsaved changes -- important for a tool used during creative sessions where you might forget what you changed

</specifics>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 13 Preset API (direct dependency)
- `include/spu94/spu94.h` lines 486-517 -- `spu94_preset_save` / `spu94_preset_load` API signatures, `SPU94_PRESET_BUF_SIZE` constant
- `.planning/phases/13-core-preset-api/13-CONTEXT.md` -- Format decisions (D-01 through D-09): INI-style sections, hex values, version tolerance, state coverage

### CLI Subcommand Pattern
- `src/cli/main.c` -- Git-style strcmp subcommand dispatcher (add `preset-dump` entry here)
- `src/cli/cmd_reverb.c` -- Full subcommand implementation with getopt_long, `--preset`/`--config` mutual exclusion pattern, mixer/DAC flag layering. `--load-preset` flag goes here.
- `src/cli/preset_names.c` -- Factory preset name lookup (reuse for `preset-dump --preset` validation)

### JUCE Standalone GUI
- `src/standalone/PluginEditor.h` -- Current GUI layout: toolbar with Load WAV / Play / Stop / preset dropdown, register panel, mixer/DAC zones
- `src/standalone/PluginEditor.cpp` -- FileChooser async pattern (WAV loading), preset ComboBox, timer-based sync for preset switching. Save/Load buttons + name prompt + modified indicator go here.
- `src/standalone/PluginProcessor.h` / `PluginProcessor.cpp` -- PresetQueue mechanism for thread-safe factory preset switching; will need analogous mechanism for .spu94 file load

### Requirements
- `.planning/REQUIREMENTS.md` -- PRE-06, PRE-07, PRE-08, PRE-09 (Phase 14 scope)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_preset_save()` / `spu94_preset_load()`: Complete C core API -- CLI and JUCE both call these directly
- `spu94_presets[]` table + `preset_names.c`: Factory preset lookup for `preset-dump --preset` validation
- `juce::FileChooser`: Already used for WAV loading in PluginEditor.cpp -- same async pattern for .spu94 Save/Load
- `PresetQueue`: Thread-safe preset switching mechanism in PluginProcessor -- model for .spu94 load path
- `getopt_long` pattern in `cmd_reverb.c`: Established flag parsing with mutual exclusion checks

### Established Patterns
- CLI: subcommand in separate `cmd_*.c` file, `extern int cmd_*()` declaration in `main.c`, strcmp dispatch
- CLI: `SPU94_ERROR(...)` macro for consistent error output
- CLI: `--preset` and `--config` mutual exclusion with clear error message
- JUCE: Timer-based polling (30 Hz) to sync GUI state after audio-thread changes
- JUCE: Async FileChooser with lambda callback (fileChooser must outlive callback)

### Integration Points
- `src/cli/main.c`: Add `preset-dump` strcmp entry + extern declaration
- `src/cli/cmd_reverb.c`: Add `--load-preset` to long_opts array and getopt_long parsing, add mutual exclusion with `--preset`/`--config`
- New file `src/cli/cmd_preset_dump.c`: Standalone subcommand implementation
- `src/standalone/PluginEditor.h`: Add Save/Load buttons, name prompt state, modified-state tracking members
- `src/standalone/PluginEditor.cpp`: Save/Load button handlers, name prompt AlertWindow, modified indicator logic, custom preset dropdown entry
- `CMakeLists.txt`: Add `cmd_preset_dump.c` to CLI target sources

</code_context>

<deferred>
## Deferred Ideas

None -- discussion stayed within phase scope

</deferred>

---

*Phase: 14-i-o-surfaces*
*Context gathered: 2026-05-02*
