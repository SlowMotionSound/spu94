---
gsd_state_version: 1.0
milestone: v1.7
milestone_name: DAW Plugin Port
status: planning
stopped_at: Phase 25 context gathered
last_updated: "2026-05-13T01:10:31.616Z"
last_activity: 2026-05-12
progress:
  total_phases: 7
  completed_phases: 4
  total_plans: 6
  completed_plans: 6
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-10)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 24 — state-automation-surface

## Current Position

Phase: 25
Plan: Not started
Status: Ready to plan
Last activity: 2026-05-12

Progress: [██████████] 100%

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.7 locked decisions (as of requirements draft, 2026-05-11):

- 4 plugin formats (VST3 + AU + LV2 + CLAP) across 3 OSes (Linux + macOS + Windows); AU is macOS-only and LV2 is dropped on Windows -- 11 unique binaries total
- Bit-faithful C core (libspu94) stays untouched -- SR and bit-depth compatibility is wrapper-only
- SRC library: libsamplerate (BSD-2), Sinc Medium quality preset
- Dither at float->int16 boundary: truncate (no dither) -- period-faithful per North Star
- Source folder split: src/standalone/ -> src/plugin/; new small src/standalone/ holds testbed-only files (WavLoader)
- Channel buses: mono->mono, mono->stereo, stereo->stereo; mono duplicated into both reverb inputs (engine always stereo internally)
- 9 host-automatable parameters: Input Gain, ADPCM Send, Dry Send, Morph Position, Morph Speed, Morph Grit, Dry Level, ADPCM Level, Reverb Level
- Standalone reframed: internal dev/test tool, no longer a user deliverable from v1.7 onward
- Code signing deferred for beta -- testers click through Gatekeeper / SmartScreen; reactive re-evaluation only
- Custom plugin UI redesign out of scope -- current standalone GUI reused inside plugin window
- Beta-tester preset return-loop mechanism out of scope -- handled out-of-band
- UAT (User Acceptance Testing) DAW matrix deferred to packaging-and-testing phase

### Blockers/Concerns

None.

### Pending Todos

None.

## Deferred Items

| Category | Item | Status | Deferred At |
|----------|------|--------|-------------|
| Cleanup | REVIEW-cli-python.md M/L/N findings | Carried from v1.0 | 2026-04-26 |
| Paperwork | Phase 6/7 Nyquist validation | Carried from v1.0 | 2026-04-26 |
| License | MIT vs Apache-2.0 pick | Carried from M1 | 2026-04-25 |
| Archive cleanup | `.planning/milestones/v1.6-phases/` currently contains the abandoned master-branch macro work, not the live v1.6 user-waypoint phases; live phases 10-20 (v1.3 through v1.6) sit in `.planning/phases/` rather than archived. Separate cleanup pass. | Flagged | 2026-05-10 |
| UI gate | Hide preset Save/Load buttons in plugin formats — wrap `addAndMakeVisible(savePresetButton/loadPresetButton)` in PluginEditor.cpp in the existing `wrapperType == wrapperType_Standalone` check (same pattern as PLUG-49 WAV-loader gate). | Flagged during phase 22 UAT | 2026-05-11 |
| Bug | Plugin GUI loses parameter state when window is closed and reopened (LV2 in Ardour 8.12). Params live in the processor; editor should reflect existing state on construction, not reset. Likely missing APVTS attachments or editor reads defaults instead of current values. Repro: open plugin → change Dry/Reverb/DAC → close plugin window → reopen → values reverted to defaults. Audio thread state may or may not match GUI; needs investigation. | Flagged during phase 22 UAT | 2026-05-11 |

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Performance | Smooth Memory Drain -- gradual buffer decay | 2026-04-29 |
| Performance | Parameter Slew Control | 2026-04-29 |
| UI Enhancement | Stereo Link toggle -- lock/unlock L/R values | 2026-04-29 |
| ADPCM Creative | ADPCM filter pair manual override | 2026-04-29 |
| Distribution | Visual signal flow diagram as GUI | 2026-04-29 |
| Feature | Continuous oversampling sweep (luxury) | 2026-04-29 |
| Performance | Buffer Base step-lock | 2026-05-01 |
| Performance | Resistance to Feedback meta-control | 2026-05-01 |
| Creative | Codec re-sync effect | 2026-05-01 |
| Experiment | Independent clamping (ratio drift) | 2026-05-03 |
| Visualization | Real-time room geometry visualizer | 2026-05-03 |
| Visualization | Polytope room shapes | 2026-05-03 |
| Documentation | Register reference manual | 2026-05-03 |
| Idea | Unified Morph Control -- merge Morph Speed + Morph Grit into one encoder | 2026-05-10 |
| UI Enhancement | Move Save/Load preset actions into a top-left panel dropdown menu instead of standalone buttons (user-perspective preference; cleaner header, mirrors how hosts already group state actions) | 2026-05-11 |
| Creative effect | "Bit Corrupt" mode -- bring back the v1.5 register-shadow overflow sound as an opt-in effect on the wall/echo register chain. Original bug (commit `e2e8f57`, 2026-05-05) read uint16 shadows as int16, so values past 32767 wrapped negative -- Anthony recalls the audio image "evaporating into smoke, bits sprinkling back in as you eased off." Authentic recreation = re-enable the signed read path as a toggle. Pair with a low-headroom helper so the wrap is reachable at musical signal levels. Fits Digital Patina Engine vision. | 2026-05-11 |
| Product direction | Companion PSX sampler instrument -- 24-voice ADPCM-based sampler matching PSX musical specs (4-bit ADPCM intake, 4-tap Gaussian interpolation on playback, exponential ADSR, loop start/end markers, 512 KB sample memory budget, SPU-94 reverb baked in). Signature features unique to SPU: voice-to-voice pitch modulation and noise-generator voice slot. Open design axes for future spitball: spec-strict (512 KB / 24 voices) vs spec-inspired (loose); MIDI instrument vs tracker workflow vs both. Headline framing: "the only sampler with a PSX motherboard inside." Companions SPU-94 reverb -- same DSP engine, same patina philosophy. | 2026-05-11 |

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 18 | 01 | retroactive | 4 | 4 |
| 19 | 01 | retroactive | 4 | 4 |
| 19 | 02 | retroactive | 8 | 9 |
| 20 | 01 | retroactive | 4 | 3 |
| 17 | 02 | (v1.5)   | -   | -   |
| 16 | 01 | 50m 19s | 2 | 6 |

## Session Continuity

Last session: 2026-05-13T01:10:31.567Z
Stopped at: Phase 25 context gathered
Resume file: .planning/phases/25-buses-validator-gates/25-CONTEXT.md
