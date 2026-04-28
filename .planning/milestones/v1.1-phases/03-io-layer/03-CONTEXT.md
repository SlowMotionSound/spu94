# Phase 3: I/O Layer - Context

**Gathered:** 2026-04-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the ADPCM codec accessible through every existing interface: CLI subcommands for file conversion (WAV↔VAG), a `--adpcm` flag for reverb processing, Python ctypes bindings for the codec and VAG I/O, and a JUCE standalone GUI toggle. No new DSP — this phase wires the Phase 1/2 codec into user-facing surfaces.

</domain>

<decisions>
## Implementation Decisions

### CLI Subcommand Design
- **D-01:** Git-style subcommands: `spu94 reverb`, `spu94 adpcm-encode`, `spu94 adpcm-decode`, `spu94 adpcm-roundtrip`. No subcommand defaults to `reverb` for backward compatibility with existing scripts.
- **D-02:** `spu94 adpcm-roundtrip in.wav out.wav` encodes to ADPCM in memory then decodes back — no intermediate .vag file. Shows ADPCM coloration in isolation.
- **D-03:** `spu94 --help` shows all subcommands with brief one-line descriptions. Each subcommand has its own `--help` for subcommand-specific flags.
- **D-04:** Stereo WAV input is processed as dual-mono (L and R encoded as separate ADPCM streams), matching PS1 convention. VAG output stores two consecutive mono streams.

### JUCE Toggle Placement
- **D-05:** ADPCM toggle lives in the toolbar row, between the preset selector and the Input knob. Always visible, one click to flip.
- **D-06:** Lit toggle button labeled "ADPCM" — lights up (amber/orange glow) when active. State change takes effect on the next process block (click-free, already handled cleanly by the C API).

### VAG Module Scope
- **D-07:** VAG reader/writer is a library module inside libspu94 (`src/spu94/vag.c`, public API in `spu94.h`). Reusable from CLI, Python, JUCE, and future callers — peer module alongside the codec.
- **D-08:** Caller-allocated buffers, same pattern as the codec. Caller queries header for size, allocates, then calls read/write. Zero heap in the VAG module itself.

### Python API Surface
- **D-09:** Python bindings expose the 4 required ADPCM C functions: `adpcm_decode_block()`, `adpcm_encode_block()`, `set_adpcm_enabled()`, `get_adpcm_enabled()`. Matches existing binding style (raw ctypes wrappers).
- **D-10:** Python bindings also expose VAG read/write functions (`vag_read_header()`, `vag_read()`, `vag_write()`) since VAG now lives in libspu94. Enables scripted batch conversions.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### ADPCM Codec (Phase 1/2 foundation)
- `include/spu94/spu94.h` — Public API including ADPCM functions (`spu94_set_adpcm_enabled`, `spu94_get_adpcm_enabled`, block-level encode/decode)
- `src/spu94/adpcm.c` — Codec implementation (decode + encode with brute-force search)
- `docs/DECISIONS.md` — ADR-0001 (Q15 semantics, `>>6` not `/64`), shift 13-15→9 policy, filter 5-7→4 policy, encoder tiebreak

### CLI (existing structure to extend)
- `src/cli/main.c` — Current flat CLI with getopt_long; will be restructured into subcommand dispatch
- `src/cli/wav_io.c` — WAV I/O using vendored dr_wav; pattern for VAG I/O
- `src/cli/CMakeLists.txt` — CLI build configuration

### JUCE Standalone (existing GUI to extend)
- `src/standalone/PluginEditor.cpp` — Current layout: toolbar row (Load, Play, Stop, Preset, Input, Wet/Dry) + RegisterPanel. ADPCM toggle inserts into toolbar.
- `src/standalone/PluginProcessor.cpp` — Audio processing; already calls `spu94_process`
- `src/standalone/PluginEditor.h` — Editor class declaration; new toggle member goes here

### Python Bindings (existing pattern to follow)
- `python/spu94/_binding.py` — ctypes binding layer; new ADPCM + VAG wrappers follow this pattern
- `python/spu94/api.py` — High-level Python API

### Requirements
- `.planning/REQUIREMENTS.md` — ADPCM-IO-01 through ADPCM-IO-06

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `wav_io.c` / `wav_io.h`: WAV read/write using dr_wav — pattern for VAG I/O module
- `ParameterBridge.cpp/h`: Bridges JUCE parameters to SPU94 state — pattern for ADPCM toggle wiring
- `_binding.py`: ctypes wrapper pattern with `cdll.LoadLibrary` — follow for new function bindings
- ADPCM C API (`spu94_set_adpcm_enabled`, `spu94_get_adpcm_enabled`, block-level functions): already implemented and tested

### Established Patterns
- Caller-allocated state, zero heap in library code
- CLI uses getopt_long with long_opts table
- Python bindings are thin ctypes wrappers, no cffi/pybind11
- JUCE standalone uses atomic stores for real-time-safe parameter passing (see `wetDryKnob.onValueChange`)
- No `ntohl` for byte-order conversion (explicit shifts per ADPCM-IO-03)

### Integration Points
- CLI `main.c` dispatch: argv[1] checked for subcommand before falling through to reverb mode
- `PluginEditor::resized()`: toolbar layout at fixed positions — ADPCM button fits between preset selector (ends ~575px) and Input knob (starts 590px)
- `PluginProcessor::processBlock()`: already calls `spu94_process` — ADPCM toggle state read via atomic load
- `_binding.py`: new `ctypes.CFUNCTYPE` declarations for ADPCM + VAG functions

</code_context>

<specifics>
## Specific Ideas

No specific requirements — open to standard approaches following existing patterns.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 3-I/O Layer*
*Context gathered: 2026-04-27*
