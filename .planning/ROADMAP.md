# Roadmap: SPU-94 — Product v1.0

**Updated:** 2026-04-26 (Phase 8 re-scoped to SPU-94 Standalone GUI per `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md`)
**Milestone:** v1.0 (product) = SPU-94 Standalone GUI (single-window JUCE-built standalone audio tool wrapping the M1 reverb core)
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Product Goal

Anthony (or any user) launches SPU-94 as a single-window standalone application on Linux, drags any WAV file onto it (any sample rate, any bit depth, mono or stereo), picks one of the 10 PS1 factory presets, and hears the audio play back through the bit-faithful reverb in real-time. While playback runs, they twist 18 raw register sliders and a Wet/Dry mix knob to hear how each parameter changes the character. The standalone wraps `libspu94` (shipped at M1, tag `m1-reverb-core`) without modifying the C core. Plugin formats (VST3 / LV2 / CLAP / AU) and DAW integration are explicitly out of scope for v1.0 — the standalone closes Anthony's "I can't easily hear what's being built" gap, which is the primary v1.0 need.

## Phases

- [x] **Phase 1: Foundation — Fixed-Point Math + Build Infrastructure** *(shipped 2026-04-18)*
- [x] **Phase 2: Buffer + Register Infrastructure** *(shipped)*
- [x] **Phase 3: Core Reverb Algorithm + Hard Clip** *(shipped)*
- [x] **Phase 4: Sample Rate Conversion (39-tap half-band FIR)** *(shipped)*
- [x] **Phase 5: Public API + Presets Integration** *(shipped)*
- [x] **Phase 6: Python Binding + CLI** *(shipped 2026-04-23)*
- [x] **Phase 7: Verification — Golden Files, Witness Diff, Modulation** *(shipped 2026-04-25, tagged `m1-reverb-core`)*
- [ ] **Phase 8: SPU-94 Standalone GUI (product v1.0)** — JUCE-built standalone audio tool that loads any-SR / any-bit-depth WAV, plays it through `libspu94` in real-time, exposes 18 raw register sliders + 10-preset dropdown + Wet/Dry knob. JUCE stock look-and-feel. Linux primary. No plugin formats, no DAW integration.
- [ ] **Phase 9: MCU Cross-Compile Smoke Test** *(parked per 2026-04-24; may move past v1.0 or be removed entirely — design-discipline + `rt_safety` ctests already prove portability)*

**Post-v1.0 (deferred — layer on top of the standalone GUI without changing the v1.0 user-facing surface):**
- Plugin formats (VST3 / LV2 / CLAP / AU) — same JUCE codebase, just adds `FORMATS` arguments to `juce_add_plugin`
- Named-lever curation (Room Size, Pre Delay, Damping, Width, Mix at minimum) — informed by listening evidence Anthony gathers from the v1.0 standalone
- Plugin-layer DSP extensions (true Pre-Delay buffer, Input HPF, Freeze, Tail-modulation LFO)
- WAV file save / export
- Live audio input (mic / line-in via JACK / PipeWire / ALSA)
- Custom UI / visual identity
- macOS / Windows builds
- M2 ADPCM (was originally Milestone 2)
- M3 DAC reconstruction modeling (was originally Milestone 3)
- M5 Hardware validation via PSX homebrew + digital capture
- Eurorack module / FPGA / MCU firmware (long-term hardware future)

Detailed phase 1-7 archive: `.planning/milestones/v1.0-ROADMAP.md` (filename uses GSD's internal milestone numbering — see PROJECT.md "Important framing distinction").

---

## Phase Details

### Phases 1-7: M1 Reverb Core (SHIPPED 2026-04-25, tag `m1-reverb-core`)

7 phases / 33 plans / 58 tasks / 82/82 ctest green. See `.planning/milestones/v1.0-ROADMAP.md` for the original phase breakdown and `.planning/MILESTONES.md` for the shipped-accomplishments list. Validated requirements moved to PROJECT.md "Validated" section.

### Phase 8: SPU-94 Standalone GUI (product v1.0)

**Goal**: Anthony launches SPU-94 as a single-window standalone application on Linux, drags any WAV file onto it (any sample rate, any bit depth, mono or stereo), picks one of the 10 PS1 factory presets, and hears the audio play back through the bit-faithful reverb in real-time. While playback runs, he twists 18 raw register sliders (the 12 free-class + 6 sample-quantized; `m*` family stays preset-fixed) and a Wet/Dry mix knob to hear how each parameter changes the character. The standalone wraps `libspu94` (shipped at M1, tag `m1-reverb-core`) without modifying the C core. JUCE stock look-and-feel. Linux primary. No plugin formats. The standalone closes Anthony's "I can't easily hear what's being built" gap, which is the primary v1.0 need.

**Depends on**: Phases 1-7 (M1 Reverb Core shipped, tag `m1-reverb-core`)

**Requirements**: STANDALONE-01..09 (see REQUIREMENTS.md)

**Authoritative scope source**: `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md` (locked decisions D-01..D-06, D-01-A; gathered 2026-04-25)

**Success Criteria** (what must be TRUE):
1. The SPU-94 standalone application launches on Linux from a fresh build of the existing root CMake project (extended with JUCE). Single-window UI appears. No plugin host or DAW required.
2. User loads any WAV file via a file picker — the I/O wrapper accepts any sample rate (8 / 11.025 / 22.05 / 44.1 / 48 / 88.2 / 96 kHz at minimum), any bit depth (8 / 16 / 24 / 32-int / 32-float), and either mono or stereo input — and converts internally to 44.1 kHz int16 stereo before the reverb sees the buffer. SPU core stays bit-faithful and unmodified.
3. Real-time playback runs cleanly: no crashes, no audio glitches or buffer underruns at typical desktop audio buffer sizes (256-1024 samples), no obvious distortion, no wrong-rate playback.
4. All 10 PS1 factory presets (Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay, Off) are selectable from a flat dropdown and each produces audibly correct output matching the M1 CLI's output for the same WAV input. Switching presets during playback works without crashing (audible discontinuity is acceptable per ADR-0006 mBASE snap-on-write).
5. 18 raw labeled register sliders are present in the panel with raw register names as labels (`vIIR`, `dCOMB1`, etc. — NOT musical aliases like "Decay" / "Damping"). The 12 free-class sliders move smoothly during playback (no zipper noise — gain-class registers are smooth at any modulation rate per LEVERS-CATALOG). The 6 sample-quantized sliders step audibly during playback (the audible stepping is character, not bug). Optional numeric value display next to each slider for debug clarity (planner's call).
6. Wet/Dry knob blends the dry input alongside the SPU's wet output. At 0% Wet/100% Dry the user hears the input WAV unchanged. At 100% Wet/0% Dry the user hears only the reverb wet path. Smooth transition between.
7. The standalone builds reproducibly via the same root `CMakeLists.txt` that produces `libspu94` and the existing CLI / Python binding — extended with a JUCE subproject and a standalone executable target. `libspu94` (the existing `spu94_shared` CMake target) is linked unmodified.
8. JUCE plugin metadata (name, version, vendor) reads "SPU-94" — not "PSX Reverb" — per the project's trademark posture.

**Planner discretion** (within the above scope):
- Slider layout / grouping on the panel (by register class, by signal-flow position, or flat — planner's call within JUCE stock components)
- Knob/slider widget choice (rotary vs vertical-strip — planner's call within JUCE stock)
- Specific JUCE interpolator for resampling (LagrangeInterpolator vs CatmullRomInterpolator vs WindowedSincInterpolator — planner's call)
- WAV reader: JUCE's built-in `AudioFormatReader` vs the existing vendored `dr_wav` — planner's call (JUCE built-in is the standard JUCE pattern)
- JUCE version pin (7.x or 8.x both viable)
- Specific JUCE module set imported
- Whether playback auto-starts on file load or requires a button press
- Whether to show numeric value next to each slider (recommended yes for debug)

**Plans:** 4 plans

Plans:
- [x] 08-01-PLAN.md — JUCE 8.0.12 build scaffolding + empty AudioProcessor shell (install deps, FetchContent, standalone target)
- [x] 08-02-PLAN.md — WAV I/O wrapper (any-SR/BD/mono-stereo to 44.1 kHz int16 stereo) + real-time audio playback pipeline
- [x] 08-03-PLAN.md — 18 register sliders + 10-preset dropdown + lock-free parameter bridge
- [x] 08-04-PLAN.md — Wet/Dry equal-power crossfade knob + full v1.0 UAT checkpoint

### Phase 9: MCU Cross-Compile Smoke Test (PARKED)

Per Anthony's 2026-04-24 decision, this phase is parked indefinitely. The MCU portability claim is upheld at v1.0 by the M1 design discipline (no heap, no locks, no syscalls, no STL, freestanding C subset) and the existing `arm-none-eabi-gcc` build infrastructure. Four `rt_safety` ctest targets prove the runtime claim. The smoke test is paper formality, not discovery — may move past v1.0 or be removed entirely.

---

## Coverage Audit

- v1.0 (product) requirements: 49 (M1, validated) + 9 (STANDALONE, active) = 58 total
- Mapped to phases: 58
- Unmapped: 0
- Duplicates: 0

---
*Roadmap restored 2026-04-25 after the premature `/gsd-complete-milestone v1.0` deletion. Phase 8 originally added as "M4 — JUCE Plugin"; re-scoped 2026-04-26 to "SPU-94 Standalone GUI" per `.planning/phases/08-m4-juce-plugin-product-v1-0/08-CONTEXT.md`. Plugin-format work deferred to a separate post-v1.0 phase. Phase 8 directory name retains the original `08-m4-juce-plugin-product-v1-0` slug for git-history continuity; phase scope is authoritatively defined in CONTEXT.md.*
