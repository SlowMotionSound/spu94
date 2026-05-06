# Roadmap: SPU-94

**Updated:** 2026-05-05
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- **v1.5 Preset Interpolation Engine** -- Phases 16-17 (in progress)
- ✅ **v1.4 Preset System** -- Phases 13-15 (shipped 2026-05-02, tag `v1.4`)
- ✅ **v1.3 True Oversampled DAC** -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- ✅ **v1.2 DAC Modeling** -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- ✅ **v1.1 ADPCM** -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** -- 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Phases

- [x] **Phase 16: Interpolation Engine** - C core: waypoint table, position mapping, linear interpolation of 30 registers between 9 Sony presets
- [ ] **Phase 17: Morph Knob GUI** - JUCE: single rotary knob with waypoint markers, real-time register updates from morph position

## Phase Details

### Phase 16: Interpolation Engine
**Goal**: A morph position value produces the correct interpolated register set for any point along the 9-preset continuum
**Depends on**: Nothing (v1.4 preset infrastructure already exists)
**Requirements**: INTERP-01, INTERP-02, INTERP-03, INTERP-04, INTERP-05
**Success Criteria** (what must be TRUE):
  1. Setting morph position to 0.0 produces Half Echo registers; setting it to 1.0 produces Delay registers; setting it to 0.5 produces registers halfway between Studio B and Studio C
  2. At each of the 9 waypoint positions (0/8, 1/8, 2/8, ... 8/8), the output registers are bit-identical to the corresponding Sony factory preset
  3. Between waypoints, all 30 active registers change smoothly (linear interpolation) while vLOUT, vROUT, vLIN, vRIN, and mBASE remain fixed
  4. Signed coefficients (v-prefix registers like vIIR, vWALL, vCOMB1-4, vAPF1/2) interpolate through negative values correctly without unsigned wraparound artifacts
**Plans**: 1 plan
Plans:
- [x] 16-01-PLAN.md -- Interpolation engine: waypoint table, position mapping, linear interpolation, TDD tests

### Phase 17: Morph Knob GUI
**Goal**: User controls the interpolation engine via a single large rotary knob with visual preset waypoint indicators
**Depends on**: Phase 16
**Requirements**: GUI-01, GUI-02, GUI-03
**Success Criteria** (what must be TRUE):
  1. A single rotary knob (250-300px) dominates the macro control area and is the sole control for preset morphing
  2. 9 dot markers around the knob arc visually indicate the exact angular positions of the Sony factory presets
  3. Turning the knob produces audible, continuous timbral change in real time -- no clicks, no silence gaps, no waiting
**Plans**: 1 plan
Plans:
- [ ] 16-01-PLAN.md -- Interpolation engine: waypoint table, position mapping, linear interpolation, TDD tests
**UI hint**: yes

## Progress

**Execution Order:** 16 -> 17

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 16. Interpolation Engine | v1.5 | 1/1 | Complete | 2026-05-06 |
| 17. Morph Knob GUI | v1.5 | 0/TBD | Not started | - |
| 1-4 | v1.1 | 10/10 | Complete | 2026-04-27 |
| 5-9 | v1.2 | 12/12 | Complete | 2026-04-30 |
| 10-12 | v1.3 | 8/8 | Complete | 2026-05-01 |
| 13-15 | v1.4 | 5/5 | Complete | 2026-05-02 |

## Previous Milestone Archives

<details>
<summary>v1.4 Preset System (Phases 13-15) -- SHIPPED 2026-05-02</summary>

Save and load custom presets as .spu94 files. C core API, CLI subcommands, JUCE GUI buttons. 3 phases, 5 plans, 10/10 requirements.

- [x] Phase 13: Core Preset API (2/2 plans) -- completed 2026-05-01
- [x] Phase 14: I/O Surfaces (2/2 plans) -- completed 2026-05-02
- [x] Phase 15: Verification (1/1 plan) -- completed 2026-05-02

</details>

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

---
*Last updated: 2026-05-05 -- v1.5 Preset Interpolation Engine roadmap created*
