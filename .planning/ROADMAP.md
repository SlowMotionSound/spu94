# Roadmap: SPU-94 — Product v1.0

**Updated:** 2026-04-25 (post-M1 ship; M4 pulled forward per v1.0 redefinition)
**Milestone:** v1.0 (product) = M4 JUCE plugin shippable to a DAW
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Product Goal

A Linux DAW user (Reaper / Ardour) installs the SPU-94 plugin, drops it on a stereo bus, picks a PS1 factory preset, and gets the recognizable PS1 reverb sound. Named musical levers (Room Size, Pre Delay, Damping, etc.) are real-time controllable and DAW-automatable. The plugin wraps `libspu94` (shipped at M1) without modifying the C core.

## Phases

- [x] **Phase 1: Foundation — Fixed-Point Math + Build Infrastructure** *(shipped 2026-04-18)*
- [x] **Phase 2: Buffer + Register Infrastructure** *(shipped)*
- [x] **Phase 3: Core Reverb Algorithm + Hard Clip** *(shipped)*
- [x] **Phase 4: Sample Rate Conversion (39-tap half-band FIR)** *(shipped)*
- [x] **Phase 5: Public API + Presets Integration** *(shipped)*
- [x] **Phase 6: Python Binding + CLI** *(shipped 2026-04-23)*
- [x] **Phase 7: Verification — Golden Files, Witness Diff, Modulation** *(shipped 2026-04-25, tagged `m1-reverb-core`)*
- [ ] **Phase 8: M4 — JUCE Plugin (product v1.0)** — wrap libspu94 as a DAW plugin; named musical lever UI; preset bank
- [ ] **Phase 9: MCU Cross-Compile Smoke Test** *(parked per 2026-04-24; may move past M4 or be removed entirely — design-discipline + `rt_safety` ctests already prove portability)*

**Post-v1.0 (deferred — layer on top of M4 without changing user-facing surface):**
- M2 ADPCM (was originally Milestone 2)
- M3 DAC reconstruction modeling (was originally Milestone 3)
- M5 Hardware validation via PSX homebrew + digital capture
- Eurorack module / FPGA / MCU firmware (long-term hardware future)

Detailed phase 1-7 archive: `.planning/milestones/v1.0-ROADMAP.md` (filename uses GSD's internal milestone numbering — see PROJECT.md "Important framing distinction").

---

## Phase Details

### Phases 1-7: M1 Reverb Core (SHIPPED 2026-04-25, tag `m1-reverb-core`)

7 phases / 33 plans / 58 tasks / 82/82 ctest green. See `.planning/milestones/v1.0-ROADMAP.md` for the original phase breakdown and `.planning/MILESTONES.md` for the shipped-accomplishments list. Validated requirements moved to PROJECT.md "Validated" section.

### Phase 8: M4 — JUCE Plugin (product v1.0)

**Goal**: A Linux DAW user installs the SPU-94 plugin, loads it on a stereo bus in Reaper / Ardour, picks any of the 10 PS1 factory presets, and controls named musical levers (Room Size, Pre Delay, Damping, etc.) in real-time with full DAW automation. The plugin wraps `libspu94` without modifying the C core.

**Depends on**: Phases 1-7 (M1 Reverb Core shipped, tag `m1-reverb-core`)

**Requirements**: PLUGIN-01..09 (see REQUIREMENTS.md)

**Success Criteria** (what must be TRUE):
1. The SPU-94 plugin installs and loads successfully in at least one Linux DAW (Reaper as the reference target). The plugin appears in the plugin list, instantiates without errors, and shows a UI when opened.
2. With the plugin on a stereo bus, audio plays through cleanly with the Hall preset hardcoded as a smoke-test default — no crashes, no buffer underruns, no obvious distortion or wrong-rate playback.
3. All 10 PS1 factory presets (Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay, Off) are selectable from the plugin's preset switcher and each produces audibly correct output (matches the M1 CLI's output for the same input).
4. Named musical levers (a curated set sourced from `docs/LEVERS-CATALOG.md` HAND columns — Room Size, Pre Delay, Damping, Width, Mix at minimum) are exposed as DAW-automatable parameters. Moving a lever during playback produces glitch-free audio (no zipper noise on gain levers; tolerated audible click on delay levers per LEVERS-CATALOG cost classification).
5. The plugin builds from source via the same CMake build system as `libspu94` (extended with JUCE), reproducibly on Linux. The C core stays unmodified — `libspu94.so` is linked, not forked.
6. The plugin UI uses polished tone consistent with the shipped product (named levers, no raw register exposure in the primary UI; advanced register-level access optional and gated).

**Open shaping questions** (to be resolved during planning):
- Plugin formats to ship: VST3 (Reaper-native on Linux) is the obvious first; AU / LV2 / Standalone — pick one or all?
- Lever set: which subset of LEVERS-CATALOG.md HAND-column candidates make the v1.0 UI? "Living instrument" framing implies expressive, not exhaustive.
- Preset bank shape: all 10 PS1 factory presets in one menu, or a curated default set with the rest behind an "advanced" disclosure?
- Registers exposed beyond named levers: full 35-register raw panel as a debug/expert mode, or v1.0 ships levers-only and raw access lands later?
- Plugin name + branding: "SPU-94" matches the library name; UI panel design tone and visual identity TBD.

**Plans:** TBD (filled by `/gsd-plan-phase 8`)

### Phase 9: MCU Cross-Compile Smoke Test (PARKED)

Per Anthony's 2026-04-24 decision, this phase is parked indefinitely. The MCU portability claim is upheld at v1.0 by the M1 design discipline (no heap, no locks, no syscalls, no STL, freestanding C subset) and the existing `arm-none-eabi-gcc` build infrastructure. Four `rt_safety` ctest targets prove the runtime claim. The smoke test is paper formality, not discovery — may move past M4 or be removed entirely.

---

## Coverage Audit

- v1.0 (product) requirements: 49 (M1) + 9 (PLUGIN) = 58 total
- Mapped to phases: 58
- Unmapped: 0
- Duplicates: 0

---
*Roadmap restored 2026-04-25 after the premature `/gsd-complete-milestone v1.0` deletion. M4 added as Phase 8 to scope the JUCE plugin work that completes product v1.0 by Anthony's redefinition.*
