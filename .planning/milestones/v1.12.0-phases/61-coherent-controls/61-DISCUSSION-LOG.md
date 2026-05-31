# Phase 61: Coherent Controls - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-30
**Phase:** 61-coherent-controls
**Areas discussed:** Level vs velocity, Pan (shared stereo position), Live vs trigger-time application

---

## Level vs. velocity (loudness source for MIDI notes)

| Option | Description | Selected |
|--------|-------------|----------|
| Level rides on top of velocity | Velocity still scales loudness (hit harder = louder); Level fader is a master trim over the whole rig. Keeps playing dynamics. | ✓ |
| Level replaces velocity | Every note plays at exactly the Level setting; velocity ignored. Flat, knob-set loudness — matches the Eurorack one-module-one-knob model. | |

**User's choice:** Option 1 — Level rides on top of velocity.
**Notes:** Framed as a mixer channel fader — touch dynamics come through, fader sets the overall level. Neither option is more hardware-faithful (SPU has a per-voice volume register, no velocity concept), so this was a pure feel call. Captured as fanning Level out onto per-voice `base_vol` (pre-reverb-send), not `master_vol`.

---

## Pan — shared stereo position

| Option | Description | Selected |
|--------|-------------|----------|
| Single shared Pan | Every active voice at the same stereo position; a chord lands at one spot, no auto spread. Matches VCTRL-02 and the one-module-one-pan model. | ✓ |
| Per-voice spread | Distribute a chord's voices across the stereo field. | (deferred) |

**User's choice:** Single shared Pan ("yeah that works").
**Notes:** Per-voice pan spread noted as a future feature, not built here.

---

## Live vs. trigger-time application

| Option | Description | Selected |
|--------|-------------|----------|
| Live on sounding voices | Moving Level/Pan or flipping NON/PMON immediately grabs all currently-held notes; envelope shape stays latched at each note's start. | ✓ |
| Newly-played notes only | Knob/toggle changes affect only notes triggered after the change. | |

**User's choice:** Live ("yeah, live").
**Notes:** Live is what the phase goal requires ("changes every *sounding* voice"). Envelope is the deliberate exception — shape is set at key-on and rides out, PS1-faithful.

## Claude's Discretion

- Exact Q15 math combining velocity × Level × Pan × INV into `base_vol_l/r`.
- Where/how to retain each active voice's velocity so `base_vol` can be recomputed live each block.
- Realtime-safe read of `activeVoiceCount` in the apply path (reuse Phase 60 atomic pattern).

## Deferred Ideas

- Per-voice pan spread (auto-distribute a chord across the stereo field) — future phase.
