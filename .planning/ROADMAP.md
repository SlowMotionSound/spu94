# Roadmap: SPU-94

**Updated:** 2026-05-16
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- 🔄 **v1.8 PSX Voice Engine** -- Phases 27-32 (in progress)
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

### v1.8 PSX Voice Engine

- [x] **Phase 27: Single Voice Playback** - One voice plays an ADPCM sample with pitch control and Gaussian interpolation, backed by dedicated 512 KB voice RAM
- [x] **Phase 28: ADSR Envelope** - PS1-faithful exponential ADSR wired into the voice tick; silence follows KOFF
- [x] **Phase 29: Loop Mechanics** - SPU loop flags (start/end/repeat) drive auto-latching loop address and one-shot termination
- [x] **Phase 30: 24-Voice Polyphony + Mixer** - All 24 voice slots run simultaneously; their output sums into the dry output and optional reverb send
- [ ] **Phase 31: Standalone Testbed UX** - Load WAV, trigger voice, control pitch, and play notes from MIDI in the standalone GUI
- [ ] **Phase 32: Sampler Anti-Aliasing Toggle** - Per-sampler Gaussian interpolation bypass; OFF gives raw zero-order-hold aliasing artifacts, ON (default) is PS1-faithful 4-tap Gauss

## Phase Details

### Phase 27: Single Voice Playback

**Goal**: One voice can load an ADPCM sample into dedicated voice RAM and play it back with pitch control and 4-tap Gaussian interpolation
**Depends on**: Phase 26 (v1.7 C core baseline); existing ADPCM decoder and Gaussian interpolation modules
**Requirements**: VOICE-01, VOICE-02, VOICE-03, VOICE-04, VOICE-05, VOICE-06, RAM-01, RAM-02, RAM-03, RAM-04
**Success Criteria** (what must be TRUE):

  1. A WAV file loaded into the standalone encodes to ADPCM and stores in the dedicated 512 KB voice RAM buffer without touching the reverb work buffer
  2. Calling voice key-on produces audible audio output at the specified pitch, using the existing 4-tap Gaussian interpolator
  3. Setting the pitch register to 0x3FFF clamps to hardware maximum; values above are rejected, not wrapped
  4. Per-voice L/R volume registers scale the output amplitude across the 0–32767 unsigned range
  5. Loading a sample whose encoded size would exceed the 512 KB voice RAM boundary is rejected with a bounds error

**Plans**: 1 plan

  - [x] 27-01-PLAN.md — Voice struct, tick, sample loader, voice 0 wired into spu94_process

**UI hint**: yes

### Phase 28: ADSR Envelope

**Goal**: Each voice follows the PS1 ADSR envelope shape — counter-accumulate stepping, fake exponential attack above 0x6000, real exponential decay, sustain plateau, and release to silence on KOFF
**Depends on**: Phase 27 (single voice playback; voice tick loop exists)
**Requirements**: ADSR-01, ADSR-02, ADSR-03, ADSR-04, ADSR-05, ADSR-06
**Success Criteria** (what must be TRUE):

  1. A voice key-on triggers Attack phase: amplitude rises using counter-accumulate stepping, with step size halving once the level crosses 0x6000
  2. After Attack, amplitude decays exponentially (each step proportional to current level) toward the sustain target of `(N+1) * 0x800`
  3. The voice holds at the sustain level until KOFF is received
  4. KOFF triggers Release: amplitude decays to zero then the voice goes silent (no audio output, no CPU cost after silence)
  5. With ADSR envelope disabled (registers zeroed), voice output is constant-amplitude — confirming the envelope module is additive and isolated

**Plans**: 1 plan

  - [x] 28-PLAN.md — ADSR state struct, counter-accumulate engine, voice integration, unit tests

### Phase 29: Loop Mechanics

**Goal**: The SPU loop-flag bits in each ADPCM block header drive loop-start address auto-latching, filter state snapshots and restoration at loop boundaries, one-shot termination, and the ENDX status bit
**Depends on**: Phase 27 (voice struct, ADPCM block reader, playback cursor)
**Requirements**: LOOP-01, LOOP-02, LOOP-03, LOOP-04, LOOP-05
**Success Criteria** (what must be TRUE):

  1. Playing an ADPCM stream with the loop-start flag set on a block auto-latches that block's address; playback jumps back to that address when loop-end is reached
  2. After a loop jump, the ADPCM filter state (predictor history) matches the snapshot taken when the loop-start block was first passed — no filter-seam click on repeated loops
  3. A sample flagged as loop-end without the repeat bit plays to the end of that block then stops; the ENDX bit for that voice reads set
  4. Removing the loop-start flag from a sample causes playback to run off the end of voice RAM and stop, confirming the latch is flag-driven not address-hardwired

**Plans**: 1 plan

  - [x] 29-PLAN.md — Loop fields in spu94_voice_t, flag-byte dispatch in spu94_voice_tick, ENDX API, four unit tests

### Phase 30: 24-Voice Polyphony + Mixer

**Goal**: All 24 voice slots run in parallel per sample tick; their outputs accumulate into a 32-bit sum, saturate to int16 at the master output, scale by Master Volume L/R, and split into a dry path (to DAC) and a per-voice-gated reverb send (to the existing reverb engine input)
**Depends on**: Phase 27 (single voice tick), Phase 28 (ADSR), Phase 29 (loop)
**Requirements**: MIX-01, MIX-02, MIX-03, MIX-04, MIX-05, MIX-06
**Success Criteria** (what must be TRUE):

  1. Key-on on multiple voices simultaneously produces polyphonic output; the mix saturates cleanly at int16 rather than wrapping
  2. Setting a voice's EON flag routes its contribution to the reverb send; clearing EON keeps it dry-only — the reverb engine receives only the flagged voices
  3. Master Volume L/R attenuate the final mixed output proportionally
  4. KON applies at the start of the next sample tick, not the current one; two voices keyed on in the same block start at the same sample offset
  5. The voice engine output and the ADPCM coloration bus (existing patina path) are independently active — both can be heard simultaneously without mutual cancellation

**Plans**: 1 plan
Plans:

- [x] 30-PLAN.md — spu94_voice_mixer_t, 24-voice mixer loop, pending KON/KOFF, EON routing, Master Volume, MIX-06 coexistence

### Phase 31: Standalone Testbed UX

**Goal**: The standalone application can load a WAV into voice RAM, trigger a voice with pitch control from a GUI button, receive MIDI note-on/off from the host to trigger voices, and remains the sole v1.8 development surface (no plugin UX changes)
**Depends on**: Phase 27 (voice RAM + load), Phase 28 (ADSR), Phase 29 (loop), Phase 30 (mixer)
**Requirements**: TEST-01, TEST-02, TEST-03, TEST-04
**Success Criteria** (what must be TRUE):

  1. Dragging or loading a WAV file in the standalone encodes it to ADPCM and confirms the load with a file name and byte count displayed in the GUI
  2. A GUI trigger button (with a pitch knob or field) keys on a voice and produces audible output through the mixer; the same button or a stop control silences it
  3. Playing a MIDI note into the standalone (from any MIDI device the OS sees) keys on a voice at the correct pitch and releases it on note-off
  4. The DAW plugin GUI and behavior are unchanged from v1.7 — all v1.8 work lives in the standalone path only

**Plans**: 2 plans
Plans:
**Wave 1**

- [x] 31-01-PLAN.md — Voice engine processor API: sample load, trigger/stop, MIDI dispatch

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 31-02-PLAN.md — Voice panel GUI: Load Sample, pitch knob, Trigger/Stop, status label

**UI hint**: yes

### Phase 32: Sampler Anti-Aliasing Toggle

**Goal**: A single toggle on the sampler switches all 24 voices between PS1-faithful 4-tap Gaussian interpolation (default ON) and raw zero-order-hold playback (OFF), exposing aliasing artifacts as a creative texture
**Depends on**: Phase 27 (voice tick with Gaussian interpolation), Phase 31 (sampler GUI surface)
**Requirements**: AA-01, AA-02, AA-03
**Success Criteria** (what must be TRUE):

  1. With Anti-Aliasing ON (default), voice playback sounds identical to the current Gauss-interpolated output — no regression
  2. With Anti-Aliasing OFF, pitched playback produces audible aliasing artifacts from zero-order hold (sample-skipping on pitch-up, sample-repeating on pitch-down) — the raw, unfiltered sound
  3. The toggle is a single global control on the sampler (not per-voice), visible in the Sampler window, and defaults to ON on fresh state

**Plans**: 1 plan

Plans:

- [ ] 32-01-PLAN.md — gauss_bypass in C core + Anti-Alias toggle in Sampler window

**UI hint**: yes

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 27. Single Voice Playback | v1.8 | 1/1 | Complete | 2026-05-16 |
| 28. ADSR Envelope | v1.8 | 1/1 | Complete | 2026-05-16 |
| 29. Loop Mechanics | v1.8 | 1/1 | Complete | 2026-05-16 |
| 30. 24-Voice Polyphony + Mixer | v1.8 | 1/1 | Complete | 2026-05-16 |
| 31. Standalone Testbed UX | v1.8 | 2/2 | Complete   | 2026-05-17 |
| 32. Sampler Anti-Aliasing Toggle | v1.8 | 0/0 | Not started | — |
| 21. Build Skeleton & CI Matrix | v1.7 | 1/1 | Complete | 2026-05-11 |
| 22. SRC & Latency Reporting | v1.7 | 1/1 | Complete | 2026-05-11 |
| 23. Float↔int16 Boundary | v1.7 | 2/2 | Complete   | 2026-05-12 |
| 24. State & Automation Surface | v1.7 | 2/2 | Complete    | 2026-05-12 |
| 25. Buses & Validator Gates | v1.7 | 2/2 | Complete | 2026-05-13 |
| 26. Packaging & Beta UAT | v1.7 | 2/2 | Complete    | 2026-05-13 |
| 18. User Slots Core | v1.6 | 1/1 | Complete | 2026-05-10 |
| 19. Waypoint GUI | v1.6 | 2/2 | Complete | 2026-05-10 |
| 20. User Slot Persistence | v1.6 | 1/1 | Complete | 2026-05-10 |
| 16. Interpolation Engine | v1.5 | 1/1 | Complete | 2026-05-06 |
| 17. Morph Knob GUI | v1.5 | 2/2 | Complete | 2026-05-06 |
| 13-15 | v1.4 | 5/5 | Complete | 2026-05-02 |
| 10-12 | v1.3 | 8/8 | Complete | 2026-05-01 |
| 5-9 | v1.2 | 12/12 | Complete | 2026-04-30 |
| 1-4 | v1.1 | 10/10 | Complete | 2026-04-27 |

## Previous Milestone Archives

<details>
<summary>v1.7 DAW Plugin Port (Phases 21-26) -- SHIPPED 2026-05-16</summary>

Multi-format DAW plugin (VST3 + AU + LV2 + CLAP) on Linux + macOS + Windows. Bidirectional SRC, float↔int16 boundary, binary state persistence, 9 host-automatable parameters, bus layout whitelist, pluginval strictness-7 CI gates, per-OS packaging, tag-triggered GitHub Release. 6 phases, 10 plans, 45/51 requirements.

- [x] Phase 21: Build Skeleton & CI Matrix (1/1 plans) -- completed 2026-05-11
- [x] Phase 22: SRC & Latency Reporting (1/1 plans) -- completed 2026-05-11
- [x] Phase 23: Float↔int16 Boundary (2/2 plans) -- completed 2026-05-12
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
*Last updated: 2026-05-16 -- Phase 28 ADSR Envelope complete*
