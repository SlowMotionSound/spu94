# Roadmap: SPU-94

**Updated:** 2026-05-30
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- [ ] v1.12.0 Voice Count -- Phases 60-63 (current milestone)
- [x] v1.11.0 Live Input Sampling -- Phases 56-59 (shipped 2026-05-30, tag `v1.11.0`)
- [x] v1.10.0 Voice Dynamics & Stereo Effects -- Phases 43-55 (shipped 2026-05-28, tag `v1.10.0`)
- [x] v1.9 Complete Voice -- Phases 33-42 (shipped 2026-05-24, tag `v1.9`)
- [x] v1.8 PSX Voice Engine -- Phases 27-32 (shipped 2026-05-21, tag `v1.8`)
- [x] v1.7 DAW Plugin Port -- Phases 21-26 (shipped 2026-05-16, tag `v1.7`)
- [x] v1.6 User Programmable Waypoints -- Phases 18-20 (shipped 2026-05-10, tag `v1.6`)
- [x] v1.5 Preset Interpolation Engine -- Phases 16-17 (shipped 2026-05-06, tag `v1.5`)
- [x] v1.4 Preset System -- Phases 13-15 (shipped 2026-05-02, tag `v1.4`)
- [x] v1.3 True Oversampled DAC -- Phases 10-12 (shipped 2026-05-01, tag `v1.3`)
- [x] v1.2 DAC Modeling -- Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- [x] v1.1 ADPCM -- Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- [x] v1.0 Product -- 8 phases (shipped 2026-04-26, standalone GUI)
- [x] M1 Reverb Core -- 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Current Milestone: v1.12.0 Voice Count

**Goal:** Let the player choose how many of the 24 sampler voices are active, and make the whole sampler play correctly at that count.

A voice-count selector (1–24) in the sampler window decides how many voices are live: 1 is the mono single-module voice, 24 is the full rig. The per-voice controls (Level, Pan, ADSR envelope, and the NON / PMON / phase-invert toggles) stop being voice-0-only and apply across every active voice. Note allocation respects the count — playing past the limit steals the oldest-sounding voice, and at count = 1 each new note takes over the single voice (last-note priority). The selector, the controls, and the allocation stay in sync as the count changes, and the count saves and restores with presets.

### Phases

- [ ] **Phase 60: Engine Voice-Count & Allocation** -- Engine knows an active-voice count and allocates notes within it, stealing the oldest-sounding voice past the limit
- [ ] **Phase 61: Coherent Controls** -- Level, Pan, envelope, and per-voice toggles apply to every active voice instead of only voice 0
- [ ] **Phase 62: Voice-Count Selector** -- A 1–24 voice control in the sampler window that takes effect immediately, with controls and allocation following the new count
- [ ] **Phase 63: Voice-Count Persistence** -- The active voice count saves to and restores from presets / system state

## Phase Details

### Phase 60: Engine Voice-Count & Allocation
**Goal**: The sampler engine knows how many voices are active and allocates played notes only among them, stealing the oldest-sounding voice when more notes play than the count allows.
**Depends on**: Nothing new (builds on the v1.8 24-voice mixer `spu94_voice_mixer_t` and the existing round-robin `allocateVoice`)
**Requirements**: VCOUNT-02, VALLOC-01, VALLOC-02, VALLOC-03
**Success Criteria** (what must be TRUE):
  1. With the count set to 1, playing notes only ever sounds one voice at a time — each new note takes over that single voice (last-note priority)
  2. With the count set to N (1 < N < 24), playing up to N notes sounds them all simultaneously; no note is allocated to a voice beyond the active set
  3. Playing an (N+1)th simultaneous note cuts the oldest-sounding note to make room, rather than dropping the new note or sounding a disabled voice
  4. Raising the count adds polyphony (more notes sound together); lowering it reduces the number of simultaneous notes, up to the full 24 at maximum
**Plans**: 1 plan
- [x] 60-01-PLAN.md — engine active-voice-count state + bounded round-robin allocator, proven by a 5-case CTest (mono/poly, only-active, steal-oldest, mono-takeover, default-24 regression)

### Phase 61: Coherent Controls
**Goal**: The sampler's per-voice controls govern every active voice, so the whole rig sounds the way the controls are set — not just the first voice.
**Depends on**: Phase 60 (allocation defines which voices are active)
**Requirements**: VCTRL-01, VCTRL-02, VCTRL-03
**Success Criteria** (what must be TRUE):
  1. Adjusting the Level control changes the loudness of every sounding voice, not just one
  2. Adjusting the Pan control places every sounding voice at the same stereo position
  3. The ADSR envelope shape (attack/decay/sustain/release) applies to every voice that is triggered, so all notes share the same envelope
  4. Toggling noise (NON), pitch-mod (PMON), or phase-invert changes the character of every active voice consistently
**Plans**: 2 plans
- [ ] 61-01-PLAN.md — Wave-0 test scaffold: friend-seam headless test (8 cases pinning VCTRL-01/02/03, D-01, D-06), noteVelocity[24] seam, no-op apply stub (RED)
- [ ] 61-02-PLAN.md — GREEN: applyContinuousVoiceControls() fan-out across [0,activeVoiceCount), per-note velocity capture, range reconciliation, sweep/duck ordering
**UI hint**: yes

### Phase 62: Voice-Count Selector
**Goal**: The player sets the active voice count from a control in the sampler window, and the sampler immediately plays — and is controlled — at that count.
**Depends on**: Phase 60 (engine count + allocation), Phase 61 (controls apply across active voices)
**Requirements**: VCOUNT-01, VCOUNT-03
**Success Criteria** (what must be TRUE):
  1. The sampler window shows a control for the active voice count that can be set anywhere from 1 to 24
  2. Moving the control to 1 makes the sampler monophonic and moving it up adds polyphony, audibly and immediately, without reloading or restarting playback
  3. After changing the count, the per-voice controls and note allocation both follow the new count straight away (the controls reach exactly the newly-active voices, and allocation uses the new limit)
  4. The selector and the engine stay in sync — the displayed count always matches how many voices the sampler is actually using
**Plans**: TBD
**UI hint**: yes

### Phase 63: Voice-Count Persistence
**Goal**: A saved preset remembers how many voices were active, so reopening or reloading it restores the same voice count.
**Depends on**: Phase 62 (the count is a real, settable value to persist)
**Requirements**: VCOUNT-04
**Success Criteria** (what must be TRUE):
  1. Saving a preset (or the plugin's system state) records the current active voice count
  2. Loading that preset restores the saved voice count, and the selector shows the restored value
  3. Presets saved before this feature still load cleanly, defaulting to the full 24 voices (back-compatible)
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 60. Engine Voice-Count & Allocation | 1/1 | Complete    | 2026-05-31 |
| 61. Coherent Controls | 0/2 | Planned     | - |
| 62. Voice-Count Selector | 0/? | Not started | - |
| 63. Voice-Count Persistence | 0/? | Not started | - |

## Previous Milestone Archives

<details>
<summary>v1.11.0 Live Input Sampling (Phases 56-59) -- SHIPPED 2026-05-30</summary>

Record live audio input directly into the sampler's 512KB voice RAM with PS1 ADPCM encoding on intake. Buffer-then-encode pipeline (accumulate raw PCM during capture, batch-encode on stop via `spu94_sample_encode_to_ram`). Manual + threshold-triggered auto-record, four PS1 preset sample rates plus a continuous variable-rate knob, input peak meter, RAM usage display, and WAV export of the trimmed region at the recording rate. 4 phases, 5 plans, all requirements verified.

- [x] Phase 56: Core Recording Pipeline (2/2 plans) -- completed 2026-05-28
- [x] Phase 57: Sample Rate Selection (1/1 plans) -- completed 2026-05-29
- [x] Phase 58: Threshold Trigger (1/1 plans) -- completed 2026-05-29
- [x] Phase 59: Sample Export (1/1 plans) -- completed 2026-05-29

Full details: `milestones/v1.11.0-ROADMAP.md`, `milestones/v1.11.0-REQUIREMENTS.md`

</details>

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
*Last updated: 2026-05-30 -- v1.12.0 Voice Count roadmap created (Phases 60-63)*
