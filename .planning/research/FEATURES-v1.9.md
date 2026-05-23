# Feature Landscape: v1.9 Complete Voice

**Domain:** PS1 SPU per-voice modulation features
**Researched:** 2026-05-21

## Table Stakes

Features that make the voice engine "complete to spec." Missing = the sampler is a subset of what a PS1 voice actually does.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| PMON (Pitch Modulation) | PS1 games use voice-chain FM for vibrato, sirens, sweeps | High | Voice N modulates voice N+1; requires cross-voice data flow in mixer |
| NON (Noise Generator) | Every PS1 percussion sound uses SPU noise; hi-hats, snares, wind | Medium | Global LFSR shared by all noise voices; independent new component |
| Volume Sweep | PS1 autopanning, fade effects, stereo widening | Medium | Same counter-accumulate math as ADSR; L/R independent sweep envelopes |
| Signed Volume / Phase Inversion | PS1 "Dolby Surround" rear-channel simulation; stereo widening | Low | Already int16_t in code; unlock negative range, update docs + API |

## Differentiators

Features that set SPU-94S apart from "yet another sampler." Not expected by default, but the PS1 hardware behavior enables them.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| Noise-as-modulator via PMON | A noise voice can pitch-modulate the next voice, creating random frequency wobble | Zero (emergent) | Falls out of PMON + NON interaction; no extra code needed |
| Phase-inversion stereo tricks | Negative volume on one channel creates "outside the speakers" effect | Zero (emergent) | Falls out of signed volume; document as a creative technique |
| ADSR-shaped noise | Noise through ADSR creates filtered-noise-like timbres without a filter | Zero (emergent) | Already works in hardware; ADSR attack/decay on noise = pseudo-filter sweep |
| Sweep-driven autopan | Volume sweep on L vs R at different rates creates automatic stereo motion | Zero (emergent) | Document as a creative technique using the sweep API |

## Anti-Features

Features to explicitly NOT build in v1.9.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| Per-voice noise pitch control | PS1 noise is GLOBAL frequency; per-voice control is not hardware-faithful | Use the global SPUCNT shift/step; document that all noise voices share one frequency |
| Smoothed PMON transitions | Hardware applies PMON factor discretely each tick; smoothing would mask the characteristic FM harshness | Apply factor directly, no interpolation |
| Volume sweep auto-trigger on KON | PS1 games set sweep parameters explicitly; KON does not implicitly start sweep | Require explicit sweep parameter write to begin sweep |
| Master volume sweep | May exist in PS1 hardware but is out of scope for v1.9 per-voice focus | Defer to future milestone if needed |
| PMON on voice 0 | Bit 0 of PMON register is unused (no voice -1 to modulate from) | Silently ignore bit 0; document that voice 0 can only BE a modulator, not be modulated |

## Feature Dependencies

```
Signed Volume  -->  Volume Sweep (sweep outputs negative values via phase bit)
Volume Sweep   -->  (none, but benefits from ADSR refactor)
NON            -->  (independent)
PMON           -->  VxOUTX capture in voice tick (requires slight pipeline change)
```

## MVP Recommendation

Build order matches dependency chain:

1. **Signed Volume** -- near-zero cost, unblocks sweep, immediate creative value
2. **NON (Noise Generator)** -- independent, high musical impact, tests well in isolation
3. **Volume Sweep** -- medium complexity, requires signed volume, shared math with ADSR
4. **PMON (Pitch Modulation)** -- highest complexity, voice-chain coupling, build last

Defer: Master Volume sweep, PMON GUI visualization (show modulation chain). These are polish, not core.

## Sources

- nocash psx-spx register documentation
- ARCHITECTURE-v1.9.md (this milestone's architecture research)
