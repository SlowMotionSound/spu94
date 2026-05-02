# Roadmap: SPU-94

**Updated:** 2026-05-02
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- 🚧 **v1.4 Preset System** -- Phases 13-15 (in progress)
- ✅ **v1.3 True Oversampled DAC** -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- ✅ **v1.2 DAC Modeling** -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- ✅ **v1.1 ADPCM** -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** -- 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Phases

<details>
<summary>v1.3 True Oversampled DAC (Phases 10-12) -- SHIPPED 2026-05-01</summary>

Genuine 8x oversampling at 352.8kHz replacing v1.2's single-rate approximation. Sum-of-8 proper decimation, unified HP-shaped noise model, A/B mode toggle across all surfaces. 3 phases, 8 plans.

- [x] Phase 10: Core Polyphase FIR Cascade (4/4 plans) -- completed 2026-05-01
- [x] Phase 11: Noise Recalibration + Integration (2/2 plans) -- completed 2026-05-01
- [x] Phase 12: Verification + Characterization (2/2 plans) -- completed 2026-05-01

</details>

<details>
<summary>v1.2 DAC Modeling (Phases 5-9) -- SHIPPED 2026-04-30</summary>

AK4309 DAC interpolation filter + delta-sigma noise model as toggleable coloration stage. Send/return mixer architecture with 3 buses, 6 faders, latency compensation. 14 requirements, 12 plans.

- [x] Phase 5: Interpolation Filter Design (1/1 plans) -- completed 2026-04-28
- [x] Phase 6: DAC Core Implementation (2/2 plans) -- completed 2026-04-29
- [x] Phase 7: Pipeline Integration (3/3 plans) -- completed 2026-04-29
- [x] Phase 8: I/O Surface (3/3 plans) -- completed 2026-04-30
- [x] Phase 9: Verification + Documentation (3/3 plans) -- completed 2026-04-30

</details>

<details>
<summary>v1.1 ADPCM (Phases 1-4) -- SHIPPED 2026-04-27</summary>

Sony 4-bit ADPCM encode/decode: bit-faithful codec as toggleable reverb coloration stage. 23 requirements, 10 plans.

- [x] Phase 1: Core Codec (2/2 plans) -- completed 2026-04-26
- [x] Phase 2: Pipeline Integration (2/2 plans) -- completed 2026-04-27
- [x] Phase 3: I/O Layer (3/3 plans) -- completed 2026-04-27
- [x] Phase 4: Verification + Documentation (3/3 plans) -- completed 2026-04-27

</details>

<details>
<summary>v1.0 Product (8 phases / 37 plans) -- SHIPPED 2026-04-26</summary>

M1 reverb core + standalone JUCE GUI. Archived to `.planning/milestones/v1.0-product-ROADMAP.md`.

</details>

<details>
<summary>M1 Reverb Core (7 phases / 33 plans) -- SHIPPED 2026-04-25</summary>

49 requirements validated. Archived to `.planning/milestones/v1.0-ROADMAP.md`.

</details>

### v1.4 Preset System (In Progress)

**Milestone Goal:** Save and load custom presets as .spu94 files -- human-readable key=value text capturing all register + mixer + DAC state -- with C core API, CLI subcommands, and JUCE GUI buttons.

- [x] **Phase 13: Core Preset API** - C functions to serialize/deserialize full SPU state as versioned key=value text, with round-trip fidelity proof
- [x] **Phase 14: I/O Surfaces** - CLI preset-dump/preset-load subcommands and JUCE Save/Load buttons with file dialogs -- completed 2026-05-02
- [ ] **Phase 15: Verification** - Round-trip golden test proving save/load/process produces bit-identical output

## Phase Details

### Phase 13: Core Preset API
**Goal**: The C core can serialize all SPU state (35 registers + mixer faders + DAC toggles) to a versioned key=value text buffer and restore it with bit-identical fidelity
**Depends on**: Phase 12 (v1.3 complete)
**Requirements**: PRE-01, PRE-02, PRE-03, PRE-04, PRE-05
**Success Criteria** (what must be TRUE):
  1. `spu94_preset_save` writes all register, mixer, and DAC state to a caller-provided buffer in human-readable key=value format
  2. `spu94_preset_load` parses a key=value buffer and restores all state -- a save/load round-trip produces bit-identical register values
  3. The preset text includes a version header line (e.g. `version=1`) so future format additions won't break existing files
  4. A .spu94 file saved to disk is plain text, human-readable, and hand-editable with a text editor
**Plans**: 2 plans
Plans:
- [x] 13-01-PLAN.md -- Public API declaration + spu94_preset_save implementation + save format tests
- [x] 13-02-PLAN.md -- spu94_preset_load parser implementation + round-trip fidelity + edge-case tests

### Phase 14: I/O Surfaces
**Goal**: Users can save and load .spu94 presets through both CLI subcommands and JUCE GUI buttons
**Depends on**: Phase 13
**Requirements**: PRE-06, PRE-07, PRE-08, PRE-09
**Success Criteria** (what must be TRUE):
  1. `spu94 preset-dump` writes the current engine state to stdout (default) or a named .spu94 file
  2. `spu94 preset-load <file.spu94>` reads a preset file and applies it before WAV processing begins
  3. JUCE Save button opens a native file dialog filtered to .spu94, writes the current state to the chosen path
  4. JUCE Load button opens a native file dialog, reads the chosen .spu94 file, and updates all GUI controls (registers, mixer faders, DAC toggles) to reflect the loaded state
**Plans**: 2 plans
Plans:
- [x] 14-01-PLAN.md -- CLI preset-dump subcommand + reverb --load-preset flag
- [x] 14-02-PLAN.md -- JUCE Save/Load buttons, name prompt, custom dropdown entry, modified indicator
**UI hint**: yes

### Phase 15: Verification
**Goal**: Round-trip preset fidelity is proven at the integration level -- save state, load into a fresh engine, process audio, get bit-identical output
**Depends on**: Phase 14
**Requirements**: PRE-10
**Success Criteria** (what must be TRUE):
  1. A ctest target saves a preset from a configured engine, loads it into a fresh engine, processes audio through both, and asserts bit-identical WAV output
  2. The test covers at least two preset configurations (one factory preset, one with non-default mixer/DAC state) to exercise all serialized fields
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 13 -> 14 -> 15

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 10. Core Polyphase FIR Cascade | v1.3 | 4/4 | Complete | 2026-05-01 |
| 11. Noise Recalibration + Integration | v1.3 | 2/2 | Complete | 2026-05-01 |
| 12. Verification + Characterization | v1.3 | 2/2 | Complete | 2026-05-01 |
| 13. Core Preset API | v1.4 | 2/2 | Complete | 2026-05-01 |
| 14. I/O Surfaces | v1.4 | 2/2 | Complete | 2026-05-02 |
| 15. Verification | v1.4 | 0/? | Not started | - |

---

## Previous Milestone Archives

- `.planning/milestones/v1.2-ROADMAP.md` -- Full v1.2 DAC Modeling roadmap
- `.planning/milestones/v1.2-REQUIREMENTS.md` -- 14 requirements (all complete)
- `.planning/milestones/v1.1-ROADMAP.md` -- Full M2 ADPCM roadmap
- `.planning/milestones/v1.1-REQUIREMENTS.md` -- 23 requirements (all complete)
- `.planning/milestones/v1.0-product-ROADMAP.md` -- v1.0 product roadmap
- `.planning/milestones/v1.0-ROADMAP.md` -- M1 reverb core roadmap
- `.planning/milestones/v1.0-REQUIREMENTS.md` -- M1 requirements

---
*Last updated: 2026-05-02 -- Phase 14 planned (2 plans)*
