# Requirements: SPU-94 — Product v1.0

**Updated:** 2026-04-25 (M4 PLUGIN reqs added; M1 reqs all validated)
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

Product v1.0 = M4 JUCE plugin shippable to a DAW. M1 (reverb core library) shipped 2026-04-25 as the foundation; M4 adds the plugin shell that makes it usable in a DAW.

## Active — M4 JUCE Plugin Requirements

### PLUGIN — DAW Plugin Shell

- [ ] **PLUGIN-01**: User can install SPU-94 as a plugin in a Linux DAW. Reaper is the reference target; additional formats (VST3 / AU / LV2 / Standalone) and platforms (macOS / Windows) to be scoped during planning.
- [ ] **PLUGIN-02**: User can route stereo audio through the SPU-94 plugin without crashes, glitches, buffer corruption, or DAW freezes. Plugin is real-time safe end-to-end (consistent with the M1 `rt_safety` guarantees on `libspu94`).
- [ ] **PLUGIN-03**: All 10 PS1 factory presets (Room, Studio A, Studio B, Studio C, Hall, Half Echo, Space Echo, Echo, Delay, Off) are selectable from the plugin's preset switcher and each produces audibly correct output matching the M1 CLI's output for the same input.
- [ ] **PLUGIN-04**: User can control named musical levers (curated from `docs/LEVERS-CATALOG.md` HAND-column candidates — Room Size, Pre Delay, Damping, Width, Mix at minimum) in real-time during playback. Lever changes are glitch-free per the M1 mid-stream-write policy and the LEVERS-CATALOG cost classification.
- [ ] **PLUGIN-05**: All lever parameters are exposed as DAW-automatable parameters with appropriate value ranges, units (where meaningful), and parameter smoothing where required by the lever's classification (free / sample-quantized / catastrophic).
- [ ] **PLUGIN-06**: Plugin builds reproducibly from source on Linux via the same CMake build system that produces `libspu94`, extended with JUCE. The C core stays unmodified — `libspu94.so` is linked, not forked.
- [ ] **PLUGIN-07**: Plugin UI uses polished tone consistent with the shipped product (named musical levers, no raw register identifiers in the primary UI panel). Advanced register-level access, if shipped, is gated behind a deliberate disclosure surface.
- [ ] **PLUGIN-08**: Plugin loads and runs in Reaper on Linux as the reference smoke test. Additional DAWs (Ardour, Bitwig, Reason) are nice-to-have validation targets, not blockers.
- [ ] **PLUGIN-09**: Plugin name, version metadata, vendor string, and licensing posture in the plugin manifest match the SPU-94 project (not "PSX Reverb" — see Constraints in PROJECT.md re: trademark).

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

## Future Requirements (Deferred Past M4)

### M2 — ADPCM (deferred past M4 per 2026-04-25 redefinition; DAW provides PCM natively, so ADPCM not needed for plugin usability)

- **M2-ADPCM-01**: 4-bit Sony ADPCM encode
- **M2-ADPCM-02**: 4-bit Sony ADPCM decode with filter coefficient tables, loop flags, block structure
- **M2-ADPCM-03**: Integrated pipeline: PCM → ADPCM encode → ADPCM decode → M1 reverb → PCM
- **M2-ADPCM-04**: ADPCM-specific DECISIONS.md entries for its own gray areas

### M3 — DAC Reconstruction Modeling (deferred past M4; layers in as switchable flag without changing plugin user-facing surface)

- **M3-DAC-01**: Period-appropriate DAC reconstruction colors as a switchable plugin parameter

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
| **PLUGIN-01..09** | **Phase 8 (M4)** | **Active** |
| M2-ADPCM-01..04 | Future (post-M4) | Deferred |
| M3-DAC-01 | Future (post-M4) | Deferred |
| M5-HW-01..02 | Future (post-M4) | Deferred |

---
*Requirements file restored 2026-04-25 after the premature `/gsd-complete-milestone v1.0` deletion. PLUGIN-01..09 added to scope the M4 JUCE plugin work that completes product v1.0 by Anthony's redefinition.*
