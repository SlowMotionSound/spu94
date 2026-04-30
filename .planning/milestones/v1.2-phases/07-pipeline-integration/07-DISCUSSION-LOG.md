# Phase 7: Pipeline Integration - Discussion Log

**Date:** 2026-04-29
**Duration:** Extended session (signal flow redesign preceded formal discussion)

## Pre-Discussion: Signal Flow Redesign

Before the formal Phase 7 discussion, an extended collaborative session redesigned the entire signal flow architecture. This was driven by the user's vision for flexibility and versatility.

### Signal Flow Evolution
1. **Started with:** Existing single-path (ADPCM → decimator → reverb → interpolator → DAC)
2. **User proposed:** Independent wet/dry mix on ADPCM, reverb, and DAC for parallel processing
3. **Evolved to:** Three cascading wet/dry mix points (rejected — phase alignment complexity)
4. **Final architecture:** Send/return mixer with three buses, two reverb sends, master mixer, DAC section at end

### Key Moments
- User identified that the dry copy should be taken AFTER ADPCM, not before (matches hardware — ADPCM decode IS the voice)
- User identified DAC model should go after the master mixer output, not before crossfade (matches hardware — DAC colors everything)
- User proposed send/return architecture over cascading wet/dry to avoid complexity
- Two independent reverb sends (dry + patina) decided — allows user to mix what feeds the reverb

## Gray Area Selection

**Presented:** Mixer levels, ADPCM latency, JUCE migration, DAC composition
**Selected:** Mixer levels, ADPCM latency, DAC composition
**Dropped:** JUCE migration (user correctly noted this isn't a gray area — just cleanup)

## Area 1: Mixer Levels

### Q: What format should mixer fader/send values use?
**Options:** Q15 int16 (recommended) / Float (0.0-1.0) / Both
**Discussion:** User asked about the relationship between float smoothing and the character of the C core. Extended discussion about:
- Whether float smoothing eliminates digital artifacts on parameter changes (yes — it removes clicks, stepping, crunch)
- PS1 hardware register update rate (~22.05kHz for reverb registers)
- User's core philosophy: exploiting digital character musically
- Claude initially understated the impact of smoothing; user pushed back and Claude corrected
**Decision:** Q15, pure fixed-point, no smoothing in C core. Raw register writes preserve digital character.

### Deferred: Parameter Slew Control
User proposed a dedicated user-facing knob that controls how much parameter changes are smoothed vs raw. All the way down = full digital crunch. All the way up = smooth transitions. Agreed this belongs in M4 (real-time lever layer), captured as deferred idea.

## Area 2: ADPCM Latency

### Q: How to handle 28-sample delay offset between dry and patina buses?
**Options:** Compensate with dry delay (recommended) / Accept offset / You decide
**Decision:** Compensate (default ON), but make it toggleable. User noted comb filtering from the offset can be musically useful — wants both flavors available.

## Area 3: DAC Composition

### Q: One DAC toggle or independent sub-toggles?
**Discussion:** User asked how audibly different each DAC component is independently. Claude provided honest assessment: both are subtle (FIR ±0.078dB ripple, noise at -85dB RMS). Neither dramatic alone, together they contribute cumulative authenticity.
**Decision:** DAC section with master toggle + two independent sub-toggles (FIR on/off, noise on/off). All three ON = faithful PS1. Signal order: FIR first, then noise (matches hardware).

## Deferred Ideas

1. **Parameter slew/smoothing control** — user-facing knob controlling parameter transition character (raw → smooth). Target: M4 real-time lever layer.
2. **Movable ADPCM insert point** — ability to reposition ADPCM in the signal chain. Future milestone.
