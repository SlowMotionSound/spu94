# Roadmap: SPU-94

**Updated:** 2026-05-11
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- 🚧 **v1.7 DAW Plugin Port** -- Phases 21-26 + conditional 27 (planning, see `milestones/v1.7-ROADMAP.md`)
- ✅ **v1.6 User Programmable Waypoints** -- Phases 18-20 (shipped 2026-05-10, tag `v1.6`)
- ✅ **v1.5 Preset Interpolation Engine** -- Phases 16-17 (shipped 2026-05-06, tag `v1.5`)
- ✅ **v1.4 Preset System** -- Phases 13-15 (shipped 2026-05-02, tag `v1.4`)
- ✅ **v1.3 True Oversampled DAC** -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- ✅ **v1.2 DAC Modeling** -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- ✅ **v1.1 ADPCM** -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** -- 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 21. Build Skeleton & CI Matrix | v1.7 | 1/1 | Complete | 2026-05-11 |
| 22. SRC & Latency Reporting | v1.7 | 0/? | Not started | -- |
| 23. Float↔int16 Boundary | v1.7 | 2/2 | Complete   | 2026-05-12 |
| 24. State & Automation Surface | v1.7 | 0/? | Not started | -- |
| 25. Buses & Validator Gates | v1.7 | 0/? | Not started | -- |
| 26. Packaging & Beta UAT | v1.7 | 0/? | Not started | -- |
| 27. Code Signing | v1.7 | 0/? | Conditional | -- |
| 18. User Slots Core | v1.6 | 1/1 | Complete | 2026-05-10 |
| 19. Waypoint GUI | v1.6 | 2/2 | Complete | 2026-05-10 |
| 20. User Slot Persistence | v1.6 | 1/1 | Complete | 2026-05-10 |
| 16. Interpolation Engine | v1.5 | 1/1 | Complete | 2026-05-06 |
| 17. Morph Knob GUI | v1.5 | 2/2 | Complete | 2026-05-06 |
| 13-15 | v1.4 | 5/5 | Complete | 2026-05-02 |
| 10-12 | v1.3 | 8/8 | Complete | 2026-05-01 |
| 5-9 | v1.2 | 12/12 | Complete | 2026-04-30 |
| 1-4 | v1.1 | 10/10 | Complete | 2026-04-27 |

## v1.7 DAW Plugin Port (🚧)

Full milestone roadmap: `milestones/v1.7-ROADMAP.md`. Requirements: `milestones/v1.7-REQUIREMENTS.md`.

### Phase 21: Build Skeleton & CI Matrix
Expand JUCE FORMATS, integrate clap-juce-extensions, rename src/standalone/ → src/plugin/ with WavLoader gated by wrapperType, establish 3-OS CI matrix building all 11 user-facing binaries.

### Phase 22: SRC & Latency Reporting
Integrate libsamplerate Sinc Medium bidirectionally, prepareToPlay-allocated, 44.1 kHz fast-path, measured group delay reported via setLatencySamples.

### Phase 23: Float↔int16 Boundary
Truncate conversion (no dither) with sat_s16 saturation semantics; input gain default and existing limiter preserved. Input Gain repositioned outside the int16 boundary as a pre-clamp float multiply with +24 dB drive ceiling (D-01/D-02); engine register pinned at unity on the plugin path (D-03).

**Plans:** 2 plans
- [x] 23-01-PLAN.md — Lift float<->int16 boundary into BoundaryConverter module (refactor, zero audible change; PLUG-17/18/21)
- [x] 23-02-PLAN.md — Reposition Input Gain pre-clamp with +24 dB drive range; pin engine register at unity on plugin path (PLUG-19/20)

### Phase 24: State & Automation Surface
Binary-wrapped .spu94 state round-trip, locale-independent; 9 host-automatable AudioProcessorParameters routed through the existing atomic bridge.

### Phase 25: Buses & Validator Gates
mono/mono + mono/stereo + stereo/stereo declared; pluginval/auval/lv2lint/VST3-validator wired into CI as required gates.

### Phase 26: Packaging & Beta UAT
Per-OS installers (.pkg/.dmg / Inno Setup / tarball+install.sh), beta README with cache-reset + unsigned-binary instructions, per-DAW UAT matrix selected and exercised.

### Phase 27: Code Signing (conditional)
Triggered only if beta install friction is reported. Developer ID + notarytool on macOS, code-signing cert on Windows.

## Previous Milestone Archives

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
*Last updated: 2026-05-10 -- v1.6 milestone archived*
