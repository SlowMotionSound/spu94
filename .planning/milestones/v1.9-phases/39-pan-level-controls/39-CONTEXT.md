# Phase 39: Pan & Level Controls - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** Design session conversation

<domain>
## Phase Boundary

Replace the raw Volume L/R rotary knobs with a musician-intuitive Pan knob + Level fader. The two raw knobs expose -16384..+16383 (the SPU's -0x4000..+0x3FFF signed volume register), which is meaningless to a musician. Pan + Level translates the musical intent ("put this voice left of center at 80% volume") into the same two register values underneath.

The existing Vol L/R knobs are awkwardly placed between the ADSR display and the sample waveform display. This phase should relocate them to a better position and resize the sampler window as needed — don't cram new controls into leftover space.

</domain>

<decisions>
## Implementation Decisions

### Control Surface
- Replace voiceVolLKnob + voiceVolRKnob with a single Pan rotary knob and a single Level fader
- Pan knob: center = equal L/R, hard left = L only, hard right = R only
- Level fader: 0% to 100% overall volume, maps to both vol_l and vol_r scaled by pan position
- Pan + Level must produce identical vol_l/vol_r values as the old raw knobs for equivalent settings
- Phase inversion (negative volume) support: teal INV indicator from Phase 34 must remain functional

### Pan-to-Register Math
- Pan range: -1.0 (hard left) to +1.0 (hard right), center = 0.0
- Level range: 0.0 (silent) to 1.0 (full, maps to 0x3FFF)
- vol_l = level * (1.0 - pan) * 0x3FFF (when pan is 0..1, left decreases)
- vol_r = level * (1.0 + pan) * 0x3FFF (when pan is -1..0, right decreases)
- Clamped to 0x3FFF max per channel
- Phase inversion: a separate INV toggle flips the sign on both channels (or per-channel if needed)

### Layout
- Don't be afraid to resize the sampler window to accommodate the new controls
- Move the volume section to a more logical position (below ADSR, or alongside it — not squeezed between waveform markers and ADSR)
- The Vol L/R knobs currently sit at y=322 in the sampler panel, between marker knobs (y=252) and ADSR display (y=393)

### Claude's Discretion
- Exact pixel positioning and panel dimensions
- Whether Pan is a rotary knob or horizontal slider
- Whether Level is a vertical fader or rotary knob
- Whether INV is a toggle button or integrated into the Level control
- Color scheme follows existing PS1 palette (light gray, dark gray, teal, mauve, coral, blue)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Current Implementation
- `src/plugin/PluginEditor.cpp` lines 344-393 — current Vol L/R knob setup
- `src/plugin/PluginEditor.cpp` lines 986-993 — current Vol L/R layout bounds
- `src/plugin/PluginEditor.h` lines 129-135 — current Vol L/R member declarations
- `src/plugin/PluginProcessor.cpp` — setGuiVoiceVolL/R methods

### Voice Engine
- `src/spu94/spu94_voice.c` lines 284-285 — vol_l/vol_r multiply in signal chain
- `include/spu94/spu94_voice.h` — voice struct with vol_l/vol_r fields

### Design Research
- `.planning/research/MILESTONE-VOICE-DYNAMICS-STEREO.md` — broader VCA ramp UX context

</canonical_refs>

<specifics>
## Specific Ideas

- Pan knob should feel like a standard mixer pan pot — center detent, smooth sweep to sides
- Level fader conceptually similar to a channel fader — full range from silence to max
- The INV indicator (teal "INV" text) currently appears per-channel; with pan+level it may make more sense as a single INV toggle that applies to both channels

</specifics>

<deferred>
## Deferred Ideas

- Raw L/R register access (collapsible power-user panel) — deferred to Voice Dynamics milestone
- Per-channel independent phase inversion — deferred to Voice Dynamics milestone
- VCA ramp controls — Phase 41

</deferred>

---

*Phase: 39-pan-level-controls*
*Context gathered: 2026-05-23 via design session*
