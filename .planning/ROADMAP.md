# Roadmap: SPU-94

**Updated:** 2026-05-28
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- v1.10.0 Voice Dynamics & Stereo Effects -- Phases 43-55 (shipped 2026-05-28, tag `v1.10.0`)
- v1.9 Complete Voice -- Phases 33-42 (shipped 2026-05-24, tag `v1.9`)
- v1.8 PSX Voice Engine -- Phases 27-32 (shipped 2026-05-21, tag `v1.8`)
- v1.7 DAW Plugin Port -- Phases 21-26 (shipped 2026-05-16, tag `v1.7`)
- v1.6 User Programmable Waypoints -- Phases 18-20 (shipped 2026-05-10, tag `v1.6`)
- v1.5 Preset Interpolation Engine -- Phases 16-17 (shipped 2026-05-06, tag `v1.5`)
- v1.4 Preset System -- Phases 13-15 (shipped 2026-05-02, tag `v1.4`)
- v1.3 True Oversampled DAC -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- v1.2 DAC Modeling -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- v1.1 ADPCM -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- v1.0 Product -- 8 phases (shipped 2026-04-26, standalone GUI)
- M1 Reverb Core -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Previous Milestone Archives

<details>
<summary>v1.10.0 Voice Dynamics & Stereo Effects (Phases 43-55) -- SHIPPED 2026-05-28</summary>

Five curated VCA ramp effects (Tremolo, Auto-Pan, AM Synthesis, Ring Mod, Sidechain Duck) — all configurations of the same L/R sweep state machine. Three sweep shapes (Triangle/Saw Up/Saw Down). Per-voice internal mod bus (noise-to-pitch/volume/pan). Split-output bus with reverb-only side limiting. Unified effects GUI with dropdown selector and adaptive controls. ADSR calibration rework. Preset format extended. 13 phases, 20 plans, 43/48 requirements (4 dropped, 1 subsumed).

- [x] Phase 43: Retrigger Engine (2/2 plans) -- completed 2026-05-24
- [x] Phase 44: Tremolo (2/2 plans) -- completed 2026-05-24
- [x] Phase 45: Auto-Pan (2/2 plans) -- completed 2026-05-24
- [x] Phase 46: Sidechain Duck (2/2 plans) -- completed 2026-05-24
- [x] ~~Phase 47: Stereo Widener~~ -- DROPPED (no native SPU stereo decorrelation)
- [x] Phase 48: AM Synthesis (2/2 plans) -- completed 2026-05-25
- [x] ~~Phase 49: Phase Modulator~~ -- SUBSUMED by Ring Mod (Phase 52)
- [x] Phase 50: Internal Mod Bus (2/2 plans) -- completed 2026-05-25
- [x] Phase 51: Split-Output Bus (1/1 plans) -- completed 2026-05-25
- [x] Phase 52: Ring Mod (1/1 plans) -- completed 2026-05-25
- [x] Phase 53: Sweep Shapes (1/1 plans) -- completed 2026-05-25
- [x] Phase 54: Unified Effects GUI (1/1 plans) -- completed 2026-05-26
- [x] Phase 55: Effects UAT -- completed 2026-05-27

Full details: `milestones/v1.10.0-ROADMAP.md`, `milestones/v1.10.0-REQUIREMENTS.md`

</details>

<details>
<summary>v1.9 Complete Voice (Phases 33-42) -- SHIPPED 2026-05-24</summary>

Every voice feature-complete to PS1 SPU spec: ADSR correction (ADR-0056), signed volume with phase inversion, PMON voice-to-voice pitch modulation (ADR-0057), global LFSR noise generator (ADR-0058), hardware-driven volume sweep with independent L/R (ADR-0059), plus musician GUI controls (pan/level, NON/PMON toggles, VCA ramp). 10 phases, 16 plans, 37/37 requirements. 98 voice engine tests, 6 rt_safety gates.

- [x] Phase 33: ADSR Correction (1/1 plans) -- completed 2026-05-22
- [x] Phase 34: Signed Volume (2/2 plans) -- completed 2026-05-22
- [x] Phase 35: Pitch Modulation / PMON (2/2 plans) -- completed 2026-05-22
- [x] Phase 36: Noise Generator / NON (2/2 plans) -- completed 2026-05-22
- [x] Phase 37: Volume Sweep (3/3 plans) -- completed 2026-05-23
- [x] Phase 38: Integration & Cross-Feature Verification (2/2 plans) -- completed 2026-05-23
- [x] Phase 39: Pan & Level Controls (1/1 plans) -- completed 2026-05-23
- [x] Phase 40: Voice Feature Toggles (1/1 plans) -- completed 2026-05-24
- [x] Phase 41: VCA Ramp Controls (1/1 plans) -- completed 2026-05-24
- [x] Phase 42: Voice GUI Integration (1/1 plans) -- completed 2026-05-24

Full details: `milestones/v1.9-ROADMAP.md`, `milestones/v1.9-REQUIREMENTS.md`

</details>

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

8 programmable waypoint slots between Sony's 9 factory anchors, turning the morph dial from a 9-position perceptual continuum into a user-customisable 17-position continuum. Per-tick EDIT / EXPORT / LOAD action buttons on MorphPanel; SAVE/REVERT edit flow; preset persistence with byte-identical back-compat for pre-feature files. Engine state mirroring overhaul. 3 phases, 4 plans.

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
*Last updated: 2026-05-28 -- v1.10.0 milestone archived*
