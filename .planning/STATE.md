---
gsd_state_version: 1.0
milestone: v1.8
milestone_name: PSX Voice Engine
status: executing
stopped_at: Phase 30 Mixer complete; Phase 31 Standalone Testbed UX ready to plan
last_updated: "2026-05-17T00:09:59.713Z"
last_activity: 2026-05-17 -- Phase 31 execution started
progress:
  total_phases: 6
  completed_phases: 4
  total_plans: 6
  completed_plans: 4
  percent: 67
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-16)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 31 — Standalone Testbed UX

## Current Position

Phase: 31 (Standalone Testbed UX) — EXECUTING
Plan: 1 of 2
Status: Executing Phase 31
Last activity: 2026-05-17 -- Phase 31 execution started

Progress: [####################] 100%

## v1.8 Phase Map

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 27 | Single Voice Playback | VOICE-01..06, RAM-01..04 | Complete |
| 28 | ADSR Envelope | ADSR-01..06 | Complete |
| 29 | Loop Mechanics | LOOP-01..05 | Complete |
| 30 | 24-Voice Polyphony + Mixer | MIX-01..06 | Complete |
| 31 | Standalone Testbed UX | TEST-01..04 | Not started |
| 32 | Sampler Anti-Aliasing Toggle | AA-01..03 | Not started |

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.8 locked decisions (from REQUIREMENTS.md):

- Voice count: 24 (PS1 spec)
- Sample format: 4-bit Sony ADPCM (existing encoder/decoder reused)
- Interpolation: 4-tap Gaussian (existing, per-voice instance)
- Pitch architecture: single-counter (bits 12+ = sample index, bits 4-11 = Gauss index)
- RAM model: two separate 512 KB buffers (voice RAM + reverb RAM) — deliberate deviation from shared PS1 address space
- Envelope: PS1 ADSR (counter-accumulate, fake exponential attack, real exponential decay)
- Mixer topology: voices -> int32 sum -> sat_s16 -> master volume -> dry out + reverb send
- Reverb integration: per-voice EON flag gates reverb send; reverb engine unchanged
- Development target: standalone testbed only (no plugin UX changes in v1.8)
- Volume model: unsigned per-voice L/R (no phase inversion -- deferred)
- spu94_voice_mixer_t lives separately from spu94_state (avoids SPU94_STATE_SIZE_MAX constraint)
- Deferred: PMON, NON, volume sweep, signed volume, CD/external input, SPUIRQ, DMA

### Key Architectural Note

The existing single-counter voice path in spu94_process.c (lines 54-151) is the seed for
Phase 27. The ADPCM decoder and Gaussian interpolation already exist and are reused per-voice.
Voice mixer output feeds the same seam the coloration bus currently uses.

### Open Items (from REQUIREMENTS.md)

1. ~~Loop seam filter state: snapshot after or before decode of loop-start block?~~ (Resolved in Phase 29: snapshot AFTER decode, per C5 pitfall prevention)
2. 24-voice mixer saturation: int32 accumulate + single final sat_s16 vs per-voice clip (Phase 30, witness test)
3. ADSR exponential attack boundary: strictly > 0x6000 (resolved in Phase 28, matches nocash spec)
4. KON timing: first output sample in same tick as KON or following tick? (Phase 30)
5. Voice engine + coloration bus coexistence: stackable or mutually exclusive? (Phase 30, MIX-06)

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
| UI gate | Hide preset Save/Load buttons in plugin formats -- wrap `addAndMakeVisible(savePresetButton/loadPresetButton)` in PluginEditor.cpp in the existing `wrapperType == wrapperType_Standalone` check (same pattern as PLUG-49 WAV-loader gate). | Flagged during phase 22 UAT | 2026-05-11 |

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
| UI Enhancement | Move Save/Load preset actions into a top-left panel dropdown menu instead of standalone buttons | 2026-05-11 |
| Creative effect | "Bit Corrupt" mode -- bring back the v1.5 register-shadow overflow sound as an opt-in effect | 2026-05-11 |
| Product direction | Companion PSX sampler instrument -- 24-voice ADPCM-based sampler (now being built as v1.8) | 2026-05-11 |
| v1.9+ | Pitch modulation (PMON) -- voice-to-voice pitch FM | 2026-05-16 |
| v1.9+ | Noise generator (NON) -- LFSR replacing ADPCM for one voice slot | 2026-05-16 |
| v1.9+ | Volume sweep mode -- automatic volume ramp per voice | 2026-05-16 |
| v1.9+ | Per-voice signed volume / phase inversion | 2026-05-16 |

## Performance Metrics

| Phase | Plan | Duration | Tasks | Files |
|-------|------|----------|-------|-------|
| 18 | 01 | retroactive | 4 | 4 |
| 19 | 01 | retroactive | 4 | 4 |
| 19 | 02 | retroactive | 8 | 9 |
| 20 | 01 | retroactive | 4 | 3 |
| 17 | 02 | (v1.5)   | -   | -   |
| 16 | 01 | 50m 19s | 2 | 6 |
| 27 | 01 | 55m 13s | 3 | 8 |
| 28 | 01 | 47m 54s | 4 | 8 |
| 29 | 01 | 33m 36s | 2 | 3 |
| 30 | 01 | ~45m | 3 | 4 |

## Session Continuity

Last session: 2026-05-16
Stopped at: Phase 30 Mixer complete; Phase 31 Standalone Testbed UX ready to plan
Resume file: .planning/phases/30-24-voice-polyphony-mixer/30-PLAN-SUMMARY.md
