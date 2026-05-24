# ADR-0060: Phase Modulator Zero-Crossing Behavior

**Status:** Accepted
**Date:** 2026-05-24
**Context:** Phase 49 (Phase Modulator DSP)

## Context

When the phase modulator depth formula drives volume through zero, the signal instantaneously inverts polarity. This document captures the expected audible behavior and the decision to use linear mode only.

The phase modulator configures the SPU volume sweep with `phase=1` and `retrigger_enable=1`, causing the sweep level to oscillate between 0 and -0x7FFF. A depth formula then maps this oscillation into the full +0x7FFF to -0x7FFF effective volume range:

```
vol = 0x7FFF + (sweep_level * 2 * depth)
```

At `depth > 0.5`, the effective volume crosses zero into negative territory, inverting the signal polarity.

## Analysis

The zero crossing is examined under two sweep modes:

### Linear Mode (mode=0)

With linear mode, the sweep level changes by a fixed step magnitude each tick:
- `step_magnitude = (7 - step_index) << max(0, 11 - shift)`

At typical phase modulator rates (shift 9-13), the step magnitude is small relative to the full 0x7FFF range. For example at shift=9, step=0: magnitude = 7 << 2 = 28. This means the sweep crosses through zero over approximately 0x7FFF / 28 = ~1170 ticks (26ms at 44100 Hz).

The zero-crossing transition is therefore **smooth and continuous** -- the volume ramp passes through zero gradually with many intermediate values. There is no single-sample discontinuity.

### Exponential Mode (mode=1)

Per ADR-0059, the phase bit is **ignored** when direction=decrease AND mode=exponential. This creates an asymmetry: exponential mode cannot properly oscillate in negative territory because the return half-cycle (decrease toward 0) loses its negative-phase behavior.

Specifically:
- Exponential increase with phase=1: works correctly (moves toward -0x7FFF)
- Exponential decrease with phase=1: phase bit ignored, behaves as phase=0 decrease (clamps at 0 instead of continuing in negative territory)

This makes exponential mode **unsuitable** for the phase modulator's polarity-cycling behavior.

## Finding

Linear zero-crossing produces **no click or pop** because:
1. The volume ramp increments smoothly through zero (each tick changes level by step_magnitude)
2. At musical rates (1-20 Hz), the crossing takes hundreds of ticks -- not a single discontinuity
3. The audio signal experiences a gradual amplitude reduction to silence, then gradual amplitude increase with inverted polarity

The audible character at the zero-crossing moment is:
- **Slow rates (1-4 Hz):** Breathing/widening effect; the silence at zero is brief and sounds like a natural dip
- **Medium rates (4-12 Hz):** Hollow, phaser-like character; repeated zero-crossings create cancellation/reinforcement
- **Fast rates (12-43 Hz):** Ring-modulator-adjacent; approaches sideband territory

## Decision

Phase modulator uses **mode=0 (linear) only**. The "exponential ignores phase bit" exception (ADR-0059) makes exponential mode unsuitable for polarity cycling.

The control is labeled as ready (not experimental) given the smooth linear crossing. If users report artifacts at very fast rates with very low depth (near the 0.5 zero-touch boundary), a 1-sample crossfade at zero could be added as a future refinement -- but this is not expected to be necessary given the continuous ramp behavior.

## References

- ADR-0059: Phase bit ignored in exponential decrease
- PMOD-03: Zero-crossing depth formula specification
- PMOD-04: Zero-crossing prototype and documentation requirement
