# Requirements: SPU-94 — Product v1.0

**Updated:** 2026-04-26 (Phase 8 re-scoped to SPU-94 Standalone GUI per `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md`; PLUGIN-01..09 archived, replaced with STANDALONE-01..09)
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

Product v1.0 = SPU-94 Standalone GUI — a single-window JUCE-built standalone audio tool that loads any-SR / any-bit-depth WAV input and plays it through the M1 reverb core at 44.1 kHz int16 internally, with 18 raw register sliders, the 10 PS1 factory presets, and a Wet/Dry mix knob. M1 (reverb core library, tag `m1-reverb-core`) shipped 2026-04-25; the standalone GUI completes product v1.0.

## Active — Phase 8 SPU-94 Standalone GUI Requirements

### STANDALONE — JUCE Standalone Audio Tool

- [ ] **STANDALONE-01**: User can launch SPU-94 as a single-window standalone JUCE application on Linux. No DAW required, no plugin host required. Plugin formats (VST3 / LV2 / CLAP / AU) are explicitly out of scope for v1.0.
- [ ] **STANDALONE-02**: User can load a WAV file (any sample rate, any bit depth, mono or stereo) via a file picker. The light I/O wrapper handles bit-depth conversion (any → int16), sample-rate conversion (any → 44.1 kHz), and channel adaptation (mono → duplicate to stereo) before the reverb core sees the buffer.
- [ ] **STANDALONE-03**: Loaded audio plays back in real-time through `libspu94` at the SPU's native 44.1 kHz int16 stereo internally. No crashes, no buffer underruns, no obvious distortion. The SPU core is bit-faithful and unmodified — all input adaptation happens in the I/O wrapper.
- [ ] **STANDALONE-04**: All 10 PS1 factory presets (Room, Studio A, Studio B, Studio C, Hall, Half Echo, Space Echo, Echo, Delay, Off) are selectable from a flat dropdown and each produces audibly correct output matching the M1 CLI's output for the same input. No preset categories or advanced disclosure splits.
- [ ] **STANDALONE-05**: User can manipulate 18 raw labeled SPU register sliders during playback — the 12 free-class registers (`vLOUT`, `vROUT`, `vLIN`, `vRIN`, `vIIR`, `vWALL`, `vCOMB1..4`, `vAPF1..2`) and the 6 sample-quantized registers (`dLSAME`, `dRSAME`, `dLDIFF`, `dRDIFF`, `dAPF1`, `dAPF2`). Slider labels are the raw register names (no musical-role aliases). Free-class movement is smooth; sample-quantized movement steps audibly per the LEVERS-CATALOG modulation cost classification (audible stepping is character, not bug). The 17 catastrophic / preset-fixed `m*` registers are NOT exposed as sliders.
- [ ] **STANDALONE-06**: User can adjust a Wet/Dry mix knob to A/B between the dry input signal and the SPU-processed wet output. Wet/Dry is the only DSP added outside `libspu94` — no Pre-Delay, no Input HPF, no Freeze, no LFO.
- [ ] **STANDALONE-07**: GUI uses JUCE stock look-and-feel — no custom skin, no painted backgrounds, no bespoke widgets. Functional appearance matching the debug-tool framing of v1.0. Custom UI / visual identity belongs in a future polish phase.
- [ ] **STANDALONE-08**: Standalone application builds reproducibly on Linux via the existing root `CMakeLists.txt` extended with JUCE. The C core stays unmodified — `libspu94` (the existing `spu94_shared` CMake target) is linked, not forked. The standalone executable is a separate CMake target alongside the library and Python binding.
- [ ] **STANDALONE-09**: Application name, version metadata, and vendor string in the JUCE plugin manifest / Standalone wrapper use "SPU-94" (not "PSX Reverb" — see Constraints in PROJECT.md re: trademark).

## Validated — M1 Reverb Core (Shipped 2026-04-25, tag `m1-reverb-core`)

All 49 M1 requirements validated through phases 1-7. See PROJECT.md "Validated" section for the per-requirement traceability with phase references and ADR citations. The full archived REQUIREMENTS.md is at `.planning/milestones/v1.0-REQUIREMENTS.md`.

Categories shipped:
- **CORE-01..10**: 10 / 10 validated (DSP algorithm)
- **API-01..09**: 9 / 9 validated (C library public surface)
- **PYBIND-01..06**: 6 / 6 validated (Python bindings)
- **CLI-01..04**: 4 / 4 validated (CLI tool)
- **TEST-01..08**: 8 / 8 validated (verification)
- **BUILD-01..08**: 8 / 8 validated (build + portability — BUILD-03 MCU smoke test parked, portability claim upheld by design discipline + `rt_safety` ctests)
- **DOCS-01..05**: 5 / 5 validated (first-class documentation artifacts)

## Future Requirements (Deferred Past v1.0 Standalone)

### Plugin Formats (deferred to a separate post-v1.0 phase)

- **PLUGIN-FORMAT-01**: VST3 / LV2 / CLAP / AU plugin builds wrapping the same JUCE codebase as the v1.0 standalone. Same `juce_add_plugin` target with additional `FORMATS` arguments. Lands when Anthony installs a Linux DAW or a DAW user requests it.

### Named-Lever Curation (deferred to a follow-up phase informed by v1.0 listening evidence)

- **LEVER-CURATION-01**: Named musical levers (Room Size, Pre Delay, Damping, Width, Mix at minimum) curated from `docs/LEVERS-CATALOG.md` HAND columns, derived from listening evidence Anthony gathers using the v1.0 standalone tool. Replaces or supplements the raw register sliders.

### Plugin-Layer DSP Extensions (deferred until v1.0 use surfaces a real need)

- **DSP-EXT-01**: True Pre-Delay buffer before SPU input
- **DSP-EXT-02**: Input HPF before SPU input
- **DSP-EXT-03**: Freeze (max `vIIR` + lock — UI trick or dedicated toggle)
- **DSP-EXT-04**: Tail-modulation LFO module targeting a chosen register

### M2 — ADPCM (deferred past v1.0 per 2026-04-25 sequencing change)

- **M2-ADPCM-01**: 4-bit Sony ADPCM encode
- **M2-ADPCM-02**: 4-bit Sony ADPCM decode with filter coefficient tables, loop flags, block structure
- **M2-ADPCM-03**: Integrated pipeline: PCM → ADPCM encode → ADPCM decode → M1 reverb → PCM
- **M2-ADPCM-04**: ADPCM-specific DECISIONS.md entries for its own gray areas

### M3 — DAC Reconstruction Modeling (deferred past v1.0; layers in as switchable flag without changing the standalone's user-facing surface)

- **M3-DAC-01**: Period-appropriate DAC reconstruction colors as a switchable parameter

### M5 — Hardware Validation (deferred; Anthony has original PS1 hardware)

- **M5-HW-01**: Capture reverb output from original PS1 hardware via PSX homebrew + digital capture
- **M5-HW-02**: Diff captured hardware output against SPU-94 output; resolve any divergences via DECISIONS.md

## Out of Scope (for product v1.0)

See PROJECT.md "Out of Scope" section. Notable v1.0 exclusions:
- Hardware (Eurorack, FPGA, MCU firmware)
- macOS / Windows builds (Linux primary; cross-platform straightforward but not in scope)
- SPU voice engine, envelope generation, pitch modulation, noise, ADSR (project-wide exclusion — reverb-only reimplementation)
- Reading Mednafen / lv2-psx-reverb / DuckStation / MiSTer source as a primary development activity (excluded by licensing posture)

## Traceability

| REQ-ID | Phase | Status |
|--------|-------|--------|
| CORE-01..10 | Phases 1-5 | ✓ Validated |
| API-01..09 | Phases 2-5 | ✓ Validated |
| PYBIND-01..06 | Phase 6 | ✓ Validated |
| CLI-01..04 | Phase 6 | ✓ Validated |
| TEST-01..08 | Phase 7 | ✓ Validated |
| BUILD-01..08 | Phases 1, 6, 7 (BUILD-03 parked) | ✓ Validated |
| DOCS-01..05 | Phases 1, 6, 7 | ✓ Validated |
| **STANDALONE-01..09** | **Phase 8 (Standalone GUI = product v1.0)** | **Active** |
| PLUGIN-FORMAT-01 | Future (post-v1.0) | Deferred |
| LEVER-CURATION-01 | Future (post-v1.0, informed by v1.0 listening) | Deferred |
| DSP-EXT-01..04 | Future (post-v1.0, on real-need surfacing) | Deferred |
| M2-ADPCM-01..04 | Future (post-v1.0) | Deferred |
| M3-DAC-01 | Future (post-v1.0) | Deferred |
| M5-HW-01..02 | Future (post-v1.0) | Deferred |

---
*Requirements file restored 2026-04-25 after the premature `/gsd-complete-milestone v1.0` deletion. PLUGIN-01..09 added 2026-04-25 to scope the M4 plugin work, then archived 2026-04-26 when Phase 8 was re-scoped to the SPU-94 Standalone GUI per `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md`. Active surface is now STANDALONE-01..09; plugin formats deferred to a separate post-v1.0 phase as PLUGIN-FORMAT-01.*
