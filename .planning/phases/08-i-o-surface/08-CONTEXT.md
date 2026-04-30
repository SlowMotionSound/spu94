# Phase 8: I/O Surface - Context

**Gathered:** 2026-04-29
**Status:** Ready for planning

<domain>
## Phase Boundary

Expose the Phase 7 mixer controls through all three I/O layers — CLI flags, Python ctypes bindings, and JUCE standalone GUI. All 10 new controls (6 faders + latency comp + 3 DAC toggles) are exposed in every layer. The C API already exists (22 setter/getter functions from Phase 7); this phase wires the hosts to it.

</domain>

<decisions>
## Implementation Decisions

### Control Exposure
- **D-01:** All 10 new controls exposed in every layer (CLI, Python, JUCE). No hidden controls, no subsets. ADPCM toggle already exposed from v1.1 — that's done.
- **D-02:** The 10 new controls: input gain, dry fader, patina fader, dry reverb send, patina reverb send, reverb fader, latency comp toggle, DAC master toggle, DAC FIR sub-toggle, DAC noise sub-toggle.

### CLI Flag Design
- **D-03:** Fader flags accept 0.0–1.0 float values. The CLI converts to Q15 int16 at the boundary before calling the C API. Example: `--input-gain 0.5`, `--dry 1.0`, `--reverb 0.8`.
- **D-04:** `--dac` enables the full DAC section — master toggle ON, FIR ON, noise ON all at once. To disable a sub-component, use `--no-dac-fir` or `--no-dac-noise` alongside `--dac`.
- **D-05:** Toggle flags follow the existing `--adpcm` pattern: flag present = enabled, absent = default (off). Add `--latency-comp` / `--no-latency-comp` (default is ON per D-07 from Phase 7).

### Python Bindings
- **D-06:** All 10 new controls exposed via ctypes with the same calling convention as the existing ADPCM toggle. Fader setters accept Python int (Q15 value) — the Python layer does NOT do float conversion (keeps it thin, matches existing pattern).

### JUCE GUI Layout
- **D-07:** Four distinct zones, top to bottom:
  1. **Toolbar** (top row): Load | Play | Stop | Preset ▼ | Input Gain knob | Reverb Sends outlined section [ADPCM send knob, Dry Input send knob]
  2. **Register panel** (middle): Existing reverb core controls — completely untouched
  3. **Mixer strip** (below registers): Three level knobs — Dry | ADPCM | Reverb | Latency Comp on/off toggle
  4. **DAC section** (bottom row): DAC on/off | FIR on/off | Noise on/off
- **D-08:** Remove the existing ADPCM toggle from the toolbar — it is no longer an always-on bus path toggle. ADPCM is controlled via the Reverb Sends section (ADPCM send knob) and the Mixer strip (ADPCM level knob).
- **D-09:** Remove the existing Wet/Dry knob — replaced by the three-fader mixer strip.
- **D-10:** The Reverb Sends section should appear as a visually outlined/grouped area so it reads as a coherent unit — the user understands these two knobs control what feeds the reverb.
- **D-11:** Latency Comp toggle lives in the Mixer strip as a "general summing mixer setting" — it controls how the master mixer handles signal summation.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 7 Outputs (C API — Already Built)
- `include/spu94/spu94.h` — All 22 setter/getter function declarations (the API this phase wires to)
- `src/spu94/spu94_io_chain.c` — All setter/getter implementations (reference for behavior, state reset patterns)
- `src/spu94/spu94_process.c` — The 7-stage mixer implementation (source of truth for signal flow)

### ADPCM I/O Pattern (Direct Template for All Three Layers)
- `src/cli/cmd_reverb.c` — CLI `--adpcm` flag handling, fader defaults for non-Off presets
- `python/spu94/_binding.py` or equivalent — Python ctypes ADPCM toggle binding
- `src/standalone/PluginProcessor.cpp` — JUCE ADPCM toggle wiring, prepareToPlay defaults, passthrough
- `src/standalone/PluginEditor.cpp` — JUCE GUI layout, ADPCM toggle placement (to be removed/relocated)
- `src/standalone/PluginEditor.h` — JUCE GUI component declarations

### Requirements
- `.planning/REQUIREMENTS.md` — DAC-IO-01 (CLI --dac), DAC-IO-02 (Python ctypes), DAC-IO-03 (JUCE GUI)

### Phase 7 Context (Mixer Architecture Decisions)
- `.planning/phases/07-pipeline-integration/07-CONTEXT.md` — D-01 through D-12 (mixer architecture, Q15 controls, DAC section design)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `cmd_reverb.c` `--adpcm` flag: exact pattern for `--dac`, `--no-dac-fir`, `--no-dac-noise`, `--latency-comp`, and all fader flags. Float-to-Q15 conversion is new (multiply by 0x7FFF, clamp, cast).
- Python ADPCM binding: exact pattern for all 10 new setter/getter pairs. Keep thin — pass Q15 int, no float conversion in Python.
- `PluginEditor.cpp` layout: existing `resized()` method with absolute bounds. New sections (mixer strip, DAC row) extend the window height. The Reverb Sends outlined section uses JUCE's `GroupComponent` or a simple `drawRect` in `paint()`.
- `PluginProcessor.cpp` prepareToPlay: already sets unity fader defaults and maps `inputLevel` to `spu94_set_input_gain`. Pattern for wiring all remaining controls.

### Established Patterns
- CLI flags: `--flag` enables, absence = default. Add `--no-flag` variant for toggles whose default is ON (latency comp).
- Python ctypes: `lib.spu94_set_X(state, value)` / `lib.spu94_get_X(state)` with `argtypes`/`restype` declarations.
- JUCE knobs: `juce::Slider::Rotary` with `TextBoxBelow`, `onValueChange` lambda stores to atomic or calls C API directly. Toggles use `juce::ToggleButton` with `setClickingTogglesState(true)`.
- All controls wire to the C API via the `spu94_state*` pointer held by the processor.

### Integration Points
- CLI: `cmd_reverb.c` argument parsing + `spu94_set_*` calls before process loop
- Python: binding module adds `argtypes`/`restype` declarations + thin wrapper functions
- JUCE: PluginEditor adds widgets in constructor + positions in `resized()`, PluginProcessor wires callbacks

</code_context>

<specifics>
## Specific Ideas

- The Reverb Sends section should feel like a distinct panel — outlined border, maybe a subtle header label "Reverb Sends" — so the user immediately understands these two knobs control what goes into the reverb.
- The Mixer strip and DAC section at the bottom should also feel like distinct zones, visually separated from the register panel above.
- Input Gain knob replaces the old Input knob position in the toolbar — same visual style, just renamed/repositioned.
- The ADPCM send knob in the Reverb Sends section and the ADPCM level knob in the Mixer strip are different controls — one feeds the reverb, the other controls the ADPCM bus level in the final mix. Labels should make this clear.

</specifics>

<deferred>
## Deferred Ideas

- **Visual signal flow diagram as the GUI** — user considered a panel-mounted block diagram (Ensoniq/ASM style) where controls attach to their signal flow blocks. Tabled for now — revisit as a future UI overhaul.
- **Parameter slew/smoothing control** — deferred to M4 real-time lever layer (from Phase 7 discussion).

</deferred>

---

*Phase: 8-I/O Surface*
*Context gathered: 2026-04-29*
