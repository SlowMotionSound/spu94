# Roadmap: SPU-94

**Updated:** 2026-05-24
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- **v1.10.0 Voice Dynamics & Stereo Effects** -- Phases 43-51 (in progress)
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

## Phases

### v1.10.0 Voice Dynamics & Stereo Effects

- [ ] **Phase 43: Retrigger Engine** - Auto-reversing VCA ramp foundation with independent L/R rates
- [ ] **Phase 44: Tremolo** - Synchronized L/R retriggered volume oscillation with speed/depth/curve
- [ ] **Phase 45: Auto-Pan** - Opposition-phase L/R retrigger creating stereo movement
- [ ] **Phase 46: Sidechain Duck** - Voice-to-voice KON-triggered volume drop with exponential recovery
- [ ] **Phase 47: Stereo Widener** - L/R divergence with mono-safety cap
- [ ] **Phase 48: AM Synthesis** - Audio-rate retrigger producing metallic sidebands
- [ ] **Phase 49: Phase Modulator** - Polarity oscillation cycling through zero crossing
- [ ] **Phase 50: Internal Mod Bus** - Per-voice noise-to-pitch/volume/pan routing
- [ ] **Phase 51: GUI Integration & Verification** - Effect section layout, coexistence verification, RT-safety

## Phase Details

### Phase 43: Retrigger Engine
**Goal**: VCA ramp can automatically reverse direction at its limits, enabling continuous oscillation for all downstream modulation effects
**Depends on**: v1.9 Volume Sweep (Phase 37) -- uses existing sweep shift/step infrastructure
**Requirements**: RTR-01, RTR-02, RTR-03, RTR-04, RTR-05
**Success Criteria** (what must be TRUE):
  1. A voice with retrigger enabled produces continuous oscillating volume (ramp reverses at 0 and max without manual re-arm)
  2. L and R channels can retrigger at independently configured rates, producing polyrhythmic modulation
  3. Retrigger rates cover the full range from sub-Hz (~0.5 Hz) through audio-rate (~7350 Hz)
  4. Disabling retrigger restores one-shot behavior identical to v1.9
  5. A new KON resets retrigger phase to zero (no stale phase from previous note)
**Plans**: TBD

### Phase 44: Tremolo
**Goal**: Users hear periodic volume pulsing on a voice, with musical control over speed, depth, and character
**Depends on**: Phase 43 (retrigger engine)
**Requirements**: TREM-01, TREM-02, TREM-03, TREM-04, TREM-05, TREM-06
**Success Criteria** (what must be TRUE):
  1. A voice with tremolo enabled audibly pulses in volume at the configured speed (both channels move together)
  2. Depth at 0% produces no audible modulation; depth at 100% produces full-range pulsing
  3. Linear curve produces symmetric triangle-wave tremolo; exponential curve produces asymmetric Uni-Vibe character
  4. Setting L/R rate ratio away from 1:1 produces audible stereo drift (polyrhythmic)
  5. Tremolo controls are visible and labeled in the sampler GUI
**Plans**: TBD
**UI hint**: yes

### Phase 45: Auto-Pan
**Goal**: Users hear stereo movement as the sound sweeps between left and right channels
**Depends on**: Phase 43 (retrigger engine)
**Requirements**: PAN-01, PAN-02, PAN-03, PAN-04, PAN-05, PAN-06
**Success Criteria** (what must be TRUE):
  1. A voice with auto-pan enabled audibly moves between left and right speakers in a repeating pattern
  2. At 100% depth the sound reaches full left and full right; at lower depths it stays closer to center
  3. The PS1-faithful linear crossfade creates an audible volume dip at center (no equal-power smoothing)
  4. Asymmetric L/R rates produce evolving non-repeating stereo patterns
  5. Auto-pan controls are visible and labeled in the sampler GUI alongside tremolo
**Plans**: TBD
**UI hint**: yes

### Phase 46: Sidechain Duck
**Goal**: One voice's note-on automatically ducks another voice's volume, creating rhythmic pumping without manual automation
**Depends on**: v1.9 Volume Sweep (Phase 37) -- uses existing one-shot ramp; independent of retrigger engine
**Requirements**: DUCK-01, DUCK-02, DUCK-03, DUCK-04, DUCK-05, DUCK-06
**Success Criteria** (what must be TRUE):
  1. Triggering a note on the source voice causes audible volume drop on the target voice
  2. The duck attack is fast (exponential decrease) and release is controllable (slow to fast recovery)
  3. Partial depth settings produce subtle pumping; full depth produces momentary silence on the target
  4. After the duck completes, the target voice automatically recovers to its original level without manual intervention
  5. Duck source picker is visible per-voice in the sampler GUI
**Plans**: TBD
**UI hint**: yes

### Phase 47: Stereo Widener
**Goal**: Users can widen the stereo image of a voice beyond its natural position while maintaining mono compatibility
**Depends on**: v1.9 Volume Sweep (Phase 37) -- uses existing L/R volume divergence; independent of retrigger
**Requirements**: WIDE-01, WIDE-02, WIDE-03, WIDE-04
**Success Criteria** (what must be TRUE):
  1. Increasing the width control makes the voice sound wider in headphones/speakers
  2. At maximum width, summing to mono loses no more than ~3 dB (mono-safety cap prevents full cancellation)
  3. Width at 0% produces no change from the voice's natural stereo position
  4. Stereo widener control is visible in the sampler GUI with a mono-safety indicator
**Plans**: TBD
**UI hint**: yes

### Phase 48: AM Synthesis
**Goal**: Users can create metallic, bell-like timbres by modulating a voice's amplitude at audio rates
**Depends on**: Phase 43 (retrigger engine -- audio-rate range)
**Requirements**: AM-01, AM-02, AM-03, AM-04, AM-05
**Success Criteria** (what must be TRUE):
  1. Setting AM rate in the audio range (~37-7350 Hz) produces audible sidebands (metallic/bell-like tones above and below the fundamental)
  2. Depth at low values produces subtle ring; depth at 100% produces full metallic character
  3. Linear and exponential curve settings produce audibly different harmonic series
  4. AM synthesis controls (rate/depth/curve) are visible and labeled in the sampler GUI
**Plans**: TBD
**UI hint**: yes

### Phase 49: Phase Modulator
**Goal**: Users can create hollow, phaser-like timbral effects by oscillating a voice's volume through zero into negative polarity
**Depends on**: Phase 43 (retrigger engine + polarity cycling)
**Requirements**: PMOD-01, PMOD-02, PMOD-03, PMOD-04, PMOD-05
**Success Criteria** (what must be TRUE):
  1. At slow rates (~0.5 Hz), the stereo image audibly breathes/widens rhythmically
  2. At medium rates (4-15 Hz), repeated cancellation creates a hollow, phase-like timbral character
  3. Zero-crossing behavior is documented (ADR) with findings on whether it clicks, pops, or transitions smoothly
  4. Depth control limits how far into negative volume the oscillation reaches
  5. Phase modulator controls are visible in the sampler GUI (labeled experimental if zero-crossing is problematic)
**Plans**: TBD
**UI hint**: yes

### Phase 50: Internal Mod Bus
**Goal**: Each voice is a self-contained sound design instrument -- noise can modulate pitch, volume, and pan without dedicating a separate NON voice
**Depends on**: v1.9 NON (Phase 36) -- uses existing global LFSR noise output
**Requirements**: MOD-01, MOD-02, MOD-03, MOD-04, MOD-05, MOD-06
**Success Criteria** (what must be TRUE):
  1. Turning up noise-to-pitch depth produces audible random pitch wobble proportional to the knob position
  2. Turning up noise-to-volume depth produces audible random amplitude variation (noise gate / broken speaker character)
  3. Turning up noise-to-pan depth produces audible random stereo jitter
  4. All three mod destinations can be active simultaneously on the same voice without interference
  5. Internal mod bus controls (three depth knobs) are visible as a dedicated section in the sampler GUI
**Plans**: TBD
**UI hint**: yes

### Phase 51: GUI Integration & Verification
**Goal**: All v1.10.0 effects coexist cleanly with existing voice features, the sampler window is properly sized, and the system passes all safety gates
**Depends on**: Phases 44-50 (all effect phases)
**Requirements**: GUI-01, GUI-02, GUI-03, GUI-04, GUI-05
**Success Criteria** (what must be TRUE):
  1. Sampler window displays all effect sections (tremolo, auto-pan, sidechain, widener, AM, phase mod, mod bus) without cramming or overlap
  2. Existing v1.9 VCA ramp controls (direction/speed/curve/ARM) remain accessible as raw register access
  3. All existing voice features (ADSR, PMON, NON, pan/level) work without regression when new effects are enabled
  4. rt_safety gates pass with all effects simultaneously enabled (no heap, no locks, no syscalls, bounded latency)
**Plans**: TBD
**UI hint**: yes

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 43. Retrigger Engine | 0/TBD | Not started | - |
| 44. Tremolo | 0/TBD | Not started | - |
| 45. Auto-Pan | 0/TBD | Not started | - |
| 46. Sidechain Duck | 0/TBD | Not started | - |
| 47. Stereo Widener | 0/TBD | Not started | - |
| 48. AM Synthesis | 0/TBD | Not started | - |
| 49. Phase Modulator | 0/TBD | Not started | - |
| 50. Internal Mod Bus | 0/TBD | Not started | - |
| 51. GUI Integration | 0/TBD | Not started | - |

## Previous Milestone Archives

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
*Last updated: 2026-05-24 -- v1.10.0 roadmap created*
