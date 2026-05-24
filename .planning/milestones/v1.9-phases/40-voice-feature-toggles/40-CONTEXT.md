# Phase 40: Voice Feature Toggles - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** Design session conversation

<domain>
## Phase Boundary

Add per-voice NON (noise) and PMON (pitch modulation) toggle controls to the sampler GUI. These C core features were built in Phases 35-36 but have no GUI surface — the user has no way to enable them from the sampler window.

</domain>

<decisions>
## Implementation Decisions

### NON Toggle
- Per-voice toggle button in the sampler GUI
- When enabled, the voice outputs noise from the global LFSR instead of its ADPCM sample
- ADPCM still decodes underneath for flag side effects (loop mechanics, ENDX)
- The noise frequency is controlled by SPUCNT NoiseShift/NoiseStep, not the voice's pitch knob
- Toggle should be clearly labeled — musician needs to understand "this voice becomes a noise source"

### PMON Toggle
- Per-voice toggle button in the sampler GUI
- When enabled, this voice's pitch is modulated by the previous voice's output (outx)
- Voice 0 PMON has no effect (no predecessor)
- The toggle should indicate the modulation chain direction — "voice N-1 modulates this voice"

### Layout
- Place NON and PMON toggles in the sampler window near the voice controls
- The sampler window is currently 400x710 — resize as needed
- Consider grouping NON and PMON together as "Voice Mode" or similar section
- These are simple on/off toggles, not complex controls

### Claude's Discretion
- Exact pixel positioning
- Whether toggles are ToggleButtons or custom buttons
- Label text and grouping
- How to communicate the PMON chain relationship visually
- Color scheme follows existing PS1 palette

</decisions>

<canonical_refs>
## Canonical References

### C Core API
- `include/spu94/spu94_voice.h` — spu94_voice_mixer_set_non, spu94_voice_mixer_set_pmon
- `src/spu94/spu94_voice.c` — non_flags and pmon_flags bitmask handling in mixer_tick

### Current GUI
- `src/plugin/PluginEditor.h` — sampler window member declarations
- `src/plugin/PluginEditor.cpp` — sampler window setup and layout
- `src/plugin/PluginProcessor.h` — processor interface for voice configuration
- `src/plugin/SamplerWindow.h` — window dimensions

### Phase Research
- `.planning/phases/35-pitch-modulation-pmon/` — PMON implementation details
- `.planning/phases/36-noise-generator-non/` — NON implementation details

</canonical_refs>

<specifics>
## Specific Ideas

- NON and PMON are fundamentally different from Pan/Level — they change what the voice IS (noise source vs sample player, FM modulation target vs standalone)
- Consider visual grouping that separates these "voice mode" controls from the "voice level" controls (Pan/Level/INV)

</specifics>

<deferred>
## Deferred Ideas

- Noise frequency controls (NoiseShift/NoiseStep) — global, not per-voice; may come in a future phase
- PMON depth control — the PS1 has no depth parameter; modulation depth is controlled by the modulator voice's ADSR
- Visual FM chain indicator showing which voices are connected

</deferred>

---

*Phase: 40-voice-feature-toggles*
*Context gathered: 2026-05-23 via design session*
