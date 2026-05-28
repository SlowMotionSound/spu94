---
gsd_state_version: 1.0
milestone: v1.10.0
milestone_name: Voice Dynamics & Stereo Effects
status: executing
stopped_at: Phase 55 effects UAT — ADSR rework done, remaining cleanup items
last_updated: "2026-05-27T21:00:00.000Z"
last_activity: 2026-05-27 -- ADSR calibration confirmed, stale references cleaned
progress:
  total_phases: 13
  completed_phases: 12
  total_plans: 20
  completed_plans: 19
  percent: 92
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-24)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 55 — effects-uat

## Current Position

Phase: 55 (effects-uat) — IN PROGRESS
Plan: UAT pass (no formal plan file)
Status: UAT in progress — 5 of 5 effect modes verified, ADSR calibration rework complete
Last activity: 2026-05-27 -- ADSR calibration rework, attack direct indexing confirmed

Progress: [████████░░] 85%

## Phase Map (v1.10.0 — revised)

| Phase | Name | Status | Notes |
|-------|------|--------|-------|
| 43 | Retrigger Engine | Done | Foundation for all VCA ramp effects |
| 44 | Tremolo | Done (rework pending) | DSP works, GUI being unified |
| 45 | Auto-Pan | Done (rework pending) | DSP works, GUI being unified |
| 46 | Sidechain Duck | Done (addition pending) | Needs attack control exposed |
| 47 | ~~Stereo Widener~~ | Dropped | SPU has no native stereo decorrelation |
| 48 | AM Synthesis | Done (rework pending) | DSP works, GUI being unified |
| 49 | ~~Phase Modulator~~ | Subsumed | Replaced by Ring Mod (Phase 52) |
| 50 | Internal Mod Bus | Done | UAT verified |
| 51 | Split-Output Bus | Done | Reverb-only side limiting |
| 52 | Ring Mod | Done | Bipolar sweep crossing zero in C core |
| 53 | Sweep Shapes | Done | Triangle / Saw Down / Saw Up |
| 54 | Unified Effects GUI | Done | Dropdown + adaptive controls |
| 55 | Effects UAT | In progress | 5/5 modes verified, ADSR rework done, remaining: one-shot trigger, GUI cleanup |

## Milestone History

| Milestone | Phases | Status | Shipped |
|-----------|--------|--------|---------|
| v1.10.0 Voice Dynamics | 43-51 (9 phases) | In progress | -- |
| v1.9 Complete Voice | 33-42 (10 phases, 16 plans) | Archived | 2026-05-24 |
| v1.8 PSX Voice Engine | 27-32 (6 phases, 7 plans) | Archived | 2026-05-21 |
| v1.7 DAW Plugin Port | 21-26 (6 phases, 10 plans) | Archived | 2026-05-16 |

See `.planning/MILESTONES.md` for full history.

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.
v1.10.0 key architectural decisions (from research):

- Retrigger lives in C core (creative extension beyond hardware, but RT-safe and sample-accurate)
- Effects are curated preconfigurations of the L/R VCA ramp state machines (not new DSP)
- Internal mod bus is a creative extension (PS1 requires separate NON voice + PMON chain; we internalize it)
- Phase Modulator needs prototype-first approach (zero-crossing behavior is unknown)

### VCA Ramp Effects — Locked Requirements (2026-05-25)

Five effects, all configurations of the same VCA ramp state machine. Mutually exclusive (one at a time). GUI is a dropdown selector with per-mode controls.

1. **Auto-Pan** — L/R sweeps in opposition
2. **Tremolo** — sub-audio rate, unipolar (0 to +1)
3. **AM** — audio rate, unipolar (0 to +1)
4. **Ring Mod** — audio rate, bipolar (-1 to +1), sweep crosses zero into phase inversion
5. **Ducking** — KON-triggered one-shot volume drop

Phase Mod (Phase 49) subsumed by Ring Mod — same mechanism. Stereo Widener dropped — SPU has no native stereo decorrelation.

**GUI:** One dropdown selects mode. Controls adapt per mode:

| Mode | Rate | Depth | Shape | Lin/Exp | Extra |
|------|------|-------|-------|---------|-------|
| Auto-Pan | yes | yes | yes | yes | L/R Ratio |
| Tremolo | yes | yes | yes | yes | — |
| AM | yes | yes | yes | yes | — |
| Ring Mod | yes | yes | yes | yes | — |
| Ducking | — | yes | — | — | Source, Attack, Release |

Shape = Triangle / Saw Up / Saw Down. All native sweep waveforms.
Four shared controls visible for all modes except Ducking, which swaps in Source, Attack, Release.

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
| Archive cleanup | v1.6 phase file organization | Flagged | 2026-05-10 |
| UI gate | Hide preset Save/Load in plugin formats | Flagged | 2026-05-11 |

## Deferred Ideas

| Category | Item | Deferred At |
|----------|------|-------------|
| UI Enhancement | ADPCM filter pair LED indicators | 2026-04-29 |
| Performance | Memory Flush button -- instant spu94_reset | 2026-04-29 |
| Creative | Codec re-sync effect | 2026-05-01 |
| Visualization | Real-time room geometry visualizer | 2026-05-03 |
| Idea | Unified Morph Control | 2026-05-10 |
| Creative effect | "Bit Corrupt" mode | 2026-05-11 |
| Creative extension | Pitch quantizer on mod bus — quantize LFSR noise to musical intervals before pitch modulation | 2026-05-25 |
| Eurorack | Raw LFSR CV output — expose noise generator as a patchable output alongside audio outs | 2026-05-25 |
| GUI cleanup | ~~Remove old VCA ramp controls~~ — done (commit 09e7ff4) | 2026-05-25 |
| GUI cleanup | Organize Noise section in sampler panel | 2026-05-25 |
| UI | Musical divisions for effects Speed encoder (note values, BPM sync) | 2026-05-25 |
| Bug | Pan inaccessible during effects — sweep overwrites vol_l/vol_r, ignoring pan position. Fix: depth formula should use pan-set volume as ceiling, not hardcoded 0x7FFF | 2026-05-25 |

## Performance Metrics

**Velocity:** (reset for new milestone)

- Total plans completed: 8
- Average duration: 8.5 min
- Total execution time: 1.16 hours

*Updated after each plan completion*

## Session Continuity

Last session: 2026-05-27
Stopped at: Removed FRACT mode, fixed oversized sample freeze, extended preset save/load with full system state
Resume file: .planning/.continue-here.md
Next action: UAT preset save/load, one-shot trigger, GUI cleanup, sidechain duck UAT (blocked on MIDI)

## UAT Bugs — Resolved

- [x] Auto-pan: R channel started at same level as L — fixed (sweep_r.level=0 on enable)
- [x] Auto-pan: parameter changes reset oscillation state — fixed (update shift/step directly)
- [x] Split-Output Bus: side limiter crushing stereo effects — fixed (Phase 51, reverb-only side limiting)
- [x] GUI layout: effects off-screen — fixed (two-column layout)
- [x] Mod Bus: UAT verified (noise-to-pitch working)
