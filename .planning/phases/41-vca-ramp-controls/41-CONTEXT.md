# Phase 41: VCA Ramp Controls - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** Design session conversation

<domain>
## Phase Boundary

Add basic per-voice VCA ramp controls to the sampler GUI. The C core VCA ramp (Sony calls it "volume sweep") was built in Phase 37 but has no GUI surface. This phase exposes the minimum viable controls: direction (ramp up / ramp down), speed, and curve shape (linear / natural).

This is NOT the full Voice Dynamics & Stereo Effects milestone — that comes later with tremolo, auto-pan, sidechain duck, etc. This phase exposes enough for the user to trigger a simple fade in or fade out on a voice.

</domain>

<decisions>
## Implementation Decisions

### Controls
- Direction: two buttons or a toggle — Up (fade in) vs Down (fade out)
- Speed: a rotary knob or slider labeled in seconds (~0.1s to ~7s), mapping to the raw shift/step values underneath
- Curve: a toggle — "Linear" vs "Natural" (exponential). Not labeled "exponential" — that word means nothing to most musicians.
- An Arm/Activate button to trigger the ramp — the ramp should start when the user presses it, not just when configuring parameters
- Ramp state resets on KON (new note starts fresh)

### Speed Mapping
- The raw shift parameter (0-31) produces exponentially spaced durations
- The GUI maps a seconds value to the nearest shift/step combination
- Musically useful range: ~0.1s (shift ~9) to ~7s (shift ~17)
- The step parameter (0-3) provides fine tuning within each shift

### Layout
- Place in the sampler window below the Pan/Level/INV section
- Resize the window as needed — don't cram
- Group as "VCA Ramp" or "Volume Ramp" section with a clear label

### Processor Wiring
- Add atomics to PluginProcessor for ramp direction, speed, curve, and active state
- In processBlock, when ramp is activated, call spu94_voice_mixer_set_sweep_l and spu94_voice_mixer_set_sweep_r with matched parameters for both channels
- The ramp modifies vol_l/vol_r directly — after a ramp runs, the Pan/Level controls may be out of sync with the actual register values. This is acceptable for now; the Pan/Level GUI reflects the user's intent, the ramp overrides the registers.

### Claude's Discretion
- Exact pixel positioning and control sizes
- Whether direction is two buttons, a toggle, or a dropdown
- Whether speed is rotary or horizontal slider
- Color scheme follows PS1 palette

</decisions>

<canonical_refs>
## Canonical References

### C Core API
- `include/spu94/spu94_sweep.h` — spu94_sweep_t struct, spu94_sweep_configure, spu94_sweep_tick
- `include/spu94/spu94_voice.h` — spu94_voice_mixer_set_sweep_l/r
- `src/spu94/spu94_sweep.c` — sweep implementation

### Current GUI
- `src/plugin/PluginEditor.h` — sampler window member declarations
- `src/plugin/PluginEditor.cpp` — sampler window setup and layout (Pan/Level/INV at bottom)
- `src/plugin/PluginProcessor.h` — processor atomics pattern
- `src/plugin/SamplerWindow.h` — window dimensions (400x710)

### Research
- `.planning/research/SWEEP-MUSICAL-GESTURES.md` — full gesture catalog with timing tables
- `.planning/research/MILESTONE-VOICE-DYNAMICS-STEREO.md` — VCA ramp research summary

</canonical_refs>

<specifics>
## Specific Ideas

- The speed knob should display seconds, not raw shift values
- "Natural" curve label instead of "exponential" — matches musician mental model
- Consider a visual indicator showing the ramp is active (e.g., a small animated bar or color change)

</specifics>

<deferred>
## Deferred Ideas

- Retrigger mode (for tremolo/auto-pan) — Voice Dynamics milestone
- Independent L/R ramp configuration — Voice Dynamics milestone
- BPM sync — Voice Dynamics milestone
- AM synthesis at audio rates — Voice Dynamics milestone
- Phase Modulator — Voice Dynamics milestone

</deferred>

---

*Phase: 41-vca-ramp-controls*
*Context gathered: 2026-05-23 via design session*
