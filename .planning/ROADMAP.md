# Roadmap: SPU-94

**Updated:** 2026-05-22
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- 🚧 **v1.9 Complete Voice** -- Phases 33-38 (in progress)
- ✅ **v1.8 PSX Voice Engine** -- Phases 27-32 (shipped 2026-05-21, tag `v1.8`)
- ✅ **v1.7 DAW Plugin Port** -- Phases 21-26 (shipped 2026-05-16, tag `v1.7`)
- ✅ **v1.6 User Programmable Waypoints** -- Phases 18-20 (shipped 2026-05-10, tag `v1.6`)
- ✅ **v1.5 Preset Interpolation Engine** -- Phases 16-17 (shipped 2026-05-06, tag `v1.5`)
- ✅ **v1.4 Preset System** -- Phases 13-15 (shipped 2026-05-02, tag `v1.4`)
- ✅ **v1.3 True Oversampled DAC** -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- ✅ **v1.2 DAC Modeling** -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- ✅ **v1.1 ADPCM** -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** -- 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Phases

### v1.9 Complete Voice

- [ ] **Phase 33: ADSR Correction** - Fix sustain-decrease and release step formulas to match spec
- [ ] **Phase 34: Signed Volume** - Expose full signed volume range for phase inversion
- [ ] **Phase 35: Pitch Modulation (PMON)** - Voice-to-voice pitch FM synthesis via VxOUTX
- [ ] **Phase 36: Noise Generator (NON)** - Global LFSR noise source replacing ADPCM output per voice
- [ ] **Phase 37: Volume Sweep** - Hardware-driven per-voice volume ramp with independent L/R
- [ ] **Phase 38: Integration & Cross-Feature Verification** - Mixer tick restructuring and cross-feature validation

## Phase Details

### Phase 33: ADSR Correction

**Goal**: ADSR envelope produces correct step magnitudes matching the PS1 spec
**Depends on**: Nothing (first phase -- standalone bug fix)
**Requirements**: ADSR-FIX-01, ADSR-FIX-02, ADSR-FIX-03, ADSR-FIX-04
**Success Criteria** (what must be TRUE):

  1. Sustain-decrease produces steps of -8, -7, -6, -5 (not -7, -6, -5, -4) for step values 0..3
  2. Release step formula audited and corrected if off-by-one (matching decay's proven `-(8-step)` pattern)
  3. ADSR golden files reflect the corrected behavior and regression suite passes
  4. ADR documents the correction with spec source citation

**Plans**: 1 plan
Plans:

- [ ] 33-01-PLAN.md -- Fix sustain-decrease and release step formulas, write ADR-0056

### Phase 34: Signed Volume

**Goal**: Voices can produce phase-inverted output through negative volume values
**Depends on**: Phase 33
**Requirements**: SVOL-01, SVOL-02, SVOL-03, SVOL-04, SVOL-05
**Success Criteria** (what must be TRUE):

  1. A voice with vol_l = -0x4000 produces sample-by-sample exact negation compared to vol_l = +0x4000
  2. All call sites that previously clamped volume to positive-only accept the full -0x4000..+0x3FFF range
  3. VxOUTX (post-ADSR, pre-volume) is unchanged by volume sign -- PMON reads are unaffected
  4. Sampler GUI exposes the signed volume range with a visible phase-flip indicator

**Plans**: 2 plans
Plans:
**Wave 1**

- [x] 34-01-PLAN.md -- C core signed volume API, outx field, TDD regression tests

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 34-02-PLAN.md -- GUI Volume L/R knobs with phase-flip indicator

### Phase 35: Pitch Modulation (PMON)

**Goal**: Voice N-1 output modulates voice N pitch, enabling FM synthesis and vibrato
**Depends on**: Phase 34
**Requirements**: PMON-01, PMON-02, PMON-03, PMON-04, PMON-05, PMON-06, PMON-07
**Success Criteria** (what must be TRUE):

  1. A modulator voice playing a slow sine sweeps the carrier voice's pitch audibly, with depth controlled by the modulator's ADSR
  2. Silent modulator (output = 0) halves the carrier pitch (Factor = 0x8000) without special-casing
  3. PMON chain stacking works: voice 0 modulates voice 1, voice 1 modulates voice 2, producing cascading pitch modulation
  4. PMON bit 0 is accepted but ignored (voice 0 has no predecessor)
  5. ADR documents VxOUTX capture point (post-ADSR, pre-volume) with DuckStation as behavioral witness

**Plans**: 2 plans
Plans:
**Wave 1**

- [x] 35-01-PLAN.md -- TDD: PMON bitmask, formula, tests (PMON-01, PMON-03..06)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 35-02-PLAN.md -- ADR-0057 VxOUTX capture point, ADSR-shaped FM integration test (PMON-02, PMON-07)

### Phase 36: Noise Generator (NON)

**Goal**: Voices can output LFSR pseudo-random noise instead of ADPCM, enabling percussion and texture
**Depends on**: Phase 35
**Requirements**: NON-01, NON-02, NON-03, NON-04, NON-05, NON-06, NON-07, NON-08, NON-09
**Success Criteria** (what must be TRUE):

  1. NON-enabled voice produces noise from the global LFSR at the frequency set by SPUCNT NoiseShift/NoiseStep -- per-voice pitch has no effect
  2. Two NON-enabled voices output identical noise samples on every tick (one global generator, not per-voice)
  3. ADSR envelope still shapes noise output (noise * adsr_level), producing percussive noise when ADSR has a fast decay
  4. ADPCM decode still runs for NON voices (loop flags fire, ENDX status updates)
  5. ADR documents noise initial seed, LFSR polynomial, and ADPCM-fetch-during-NON decision

**Plans**: 2 plans
Plans:
**Wave 1**

- [x] 36-01-PLAN.md -- TDD: noise generator LFSR module + NON voice pipeline integration (NON-01..08)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 36-02-PLAN.md -- ADR-0058 noise generator decisions documentation (NON-09)

### Phase 37: Volume Sweep

**Goal**: Per-voice volume ramps automatically via hardware-driven sweep, independent of ADSR
**Depends on**: Phase 36
**Requirements**: SWEEP-01, SWEEP-02, SWEEP-03, SWEEP-04, SWEEP-05, SWEEP-06, SWEEP-07, SWEEP-08, SWEEP-09, SWEEP-10
**Success Criteria** (what must be TRUE):

  1. Left volume can sweep up while right sweeps down simultaneously on the same voice, creating automatic stereo panning
  2. Sweep and ADSR run concurrently as independent envelopes -- a voice with sweep-decrease and ADSR-sustain produces a volume fade without affecting the envelope shape
  3. Sweep modifies vol_l/vol_r directly (the volume register IS the sweep's working state, not a separate multiplier)
  4. KON resets sweep state to the initial value; KOFF does not affect sweep
  5. Exponential decrease near zero does not stall (anti-stall guard: if scaled_step == 0 and level > 0, step = -1)

**Plans**: TBD

### Phase 38: Integration & Cross-Feature Verification

**Goal**: All four new features work together correctly in the restructured voice mixer tick
**Depends on**: Phase 37
**Requirements**: INT-01, INT-02, INT-03, INT-04
**Success Criteria** (what must be TRUE):

  1. Voice mixer tick processes in the correct order: noise tick globally, then per-voice sweep, PMON pitch modify, ADPCM decode, noise/Gauss branch, ADSR, store VxOUTX, volume multiply, accumulate
  2. A noise voice's output feeds PMON factor for the next voice, producing random pitch jitter (spec-orthogonal behavior verified, not blocked)
  3. All existing voice features unbroken: ADSR, loop mechanics, EON reverb send, Gaussian interpolation, anti-aliasing toggle, MIDI dispatch
  4. rt_safety gates pass with all new features enabled (no heap, no locks, no syscalls, bounded latency)

**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 33, 34, 35, 36, 37, 38

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 33. ADSR Correction | 1/1 | Complete | 2026-05-22 |
| 34. Signed Volume | 2/2 | Complete    | 2026-05-22 |
| 35. Pitch Modulation (PMON) | 2/2 | Complete    | 2026-05-22 |
| 36. Noise Generator (NON) | 2/2 | Complete    | 2026-05-22 |
| 37. Volume Sweep | 0/TBD | Not started | - |
| 38. Integration & Cross-Feature Verification | 0/TBD | Not started | - |

## Previous Milestone Archives

<details>
<summary>v1.8 PSX Voice Engine (Phases 27-32) -- SHIPPED 2026-05-21</summary>

24-voice ADPCM sampler engine with PS1-faithful single-counter Gaussian interpolation, counter-accumulate ADSR envelope, SPU loop mechanics with filter state snapshot/restore, polyphonic mixer with EON-gated reverb send. Full sampler GUI in standalone with waveform zoom/scroll, draggable markers, ADSR controls, latch/lock modes, drive stage, MIDI dispatch. Anti-aliasing toggle for creative zero-order-hold aliasing. 6 phases, 7 plans, 34/34 requirements.

- [x] Phase 27: Single Voice Playback (1/1 plans) -- completed 2026-05-16
- [x] Phase 28: ADSR Envelope (1/1 plans) -- completed 2026-05-16
- [x] Phase 29: Loop Mechanics (1/1 plans) -- completed 2026-05-16
- [x] Phase 30: 24-Voice Polyphony + Mixer (1/1 plans) -- completed 2026-05-16
- [x] Phase 31: Standalone Testbed UX (2/2 plans) -- completed 2026-05-17
- [x] Phase 32: Sampler Anti-Aliasing Toggle (1/1 plans) -- completed 2026-05-19

Full details: `milestones/v1.8-ROADMAP.md`, `milestones/v1.8-REQUIREMENTS.md`

</details>

<details>
<summary>v1.7 DAW Plugin Port (Phases 21-26) -- SHIPPED 2026-05-16</summary>

Multi-format DAW plugin (VST3 + AU + LV2 + CLAP) on Linux + macOS + Windows. Bidirectional SRC, float-to-int16 boundary, binary state persistence, 9 host-automatable parameters, bus layout whitelist, pluginval strictness-7 CI gates, per-OS packaging, tag-triggered GitHub Release. 6 phases, 10 plans, 45/51 requirements.

- [x] Phase 21: Build Skeleton & CI Matrix (1/1 plans) -- completed 2026-05-11
- [x] Phase 22: SRC & Latency Reporting (1/1 plans) -- completed 2026-05-11
- [x] Phase 23: Float-to-int16 Boundary (2/2 plans) -- completed 2026-05-12
- [x] Phase 24: State & Automation Surface (2/2 plans) -- completed 2026-05-12
- [x] Phase 25: Buses & Validator Gates (2/2 plans) -- completed 2026-05-13
- [x] Phase 26: Packaging & Beta UAT (2/2 plans) -- completed 2026-05-13

Full details: `milestones/v1.7-ROADMAP.md`, `milestones/v1.7-REQUIREMENTS.md`, `milestones/v1.7-MILESTONE-AUDIT.md`

</details>

<details>
<summary>v1.6 User Programmable Waypoints (Phases 18-20) -- SHIPPED 2026-05-10</summary>

8 programmable waypoint slots between Sony's 9 factory anchors, turning the morph dial from a 9-position perceptual continuum into a user-customisable 17-position continuum. Per-tick EDIT / EXPORT / LOAD action buttons on MorphPanel; SAVE/REVERT edit flow; preset persistence with byte-identical back-compat for pre-feature files. Engine state mirroring overhaul so sliders always reflect engine state regardless of WAV/playback. 3 phases, 4 plans.

- [x] Phase 18: User Slots Core (1/1 plans) -- completed 2026-05-10
- [x] Phase 19: Waypoint GUI (2/2 plans) -- completed 2026-05-10
- [x] Phase 20: User Slot Persistence (1/1 plans) -- completed 2026-05-10

</details>

<details>
<summary>v1.5 Preset Interpolation Engine (Phases 16-17) -- SHIPPED 2026-05-06</summary>

Single morph knob between Sony's 9 factory presets. C interpolation engine with bit-identical waypoint output, 280px JUCE rotary knob with PS1-colored waypoint dots, detent snap, Macro/Advanced toggle. 2 phases, 3 plans, 8/8 requirements.

- [x] Phase 16: Interpolation Engine (1/1 plans) -- completed 2026-05-06
- [x] Phase 17: Morph Knob GUI (2/2 plans) -- completed 2026-05-06

</details>

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
*Last updated: 2026-05-22 -- Phase 36 planned (2 plans)*
