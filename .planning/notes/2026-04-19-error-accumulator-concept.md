# The Error Accumulator — Algorithm & Hardware Concept

## Core Idea

A performance-oriented audio effect built around progressive quantization error accumulation across time. Not static bit crushing — a stateful system with memory, where errors compound across frames and the control interface is a physical inertial rotor rather than a knob.

---

## The Algorithm

### Basic Operation (Per Frame)

```
accumulated_error += carried_error * k
output = truncate(input + accumulated_error, target_bits)
carried_error = (input + accumulated_error) - output
```

The discarded remainder from each truncation is not thrown away. It is fed forward into the next frame's calculation, weighted by `k`. This transforms quantization from a stateless per-sample operation into a stateful process with long memory.

### The k Parameter

`k` is the core control variable — the accumulation factor:

```
k = 0.0   → errors discarded each frame, clean signal
k = 0.5   → errors partially carried forward, mild texture
k = 1.0   → full error carry, audible degradation
k > 1.0   → errors amplified before carry, escalating instability
```

The threshold at `k = 1.0` is where the system tips from decaying errors to escalating errors. This region is musically the most interesting and requires the most physical resolution on the control surface.

### Error Pool Memory

Rather than carrying 100% of the raw error forward, the error pool is maintained as an exponential moving average:

```
carried_error = (carried_error * memory) + (new_error * (1 - memory))
```

Where `memory` is very close to 1.0 (e.g. 0.998). This means the error pool fills and drains slowly across potentially thousands of frames — like a reservoir rather than a pipe. Even after the control is released, the system continues evolving on its own momentum.

### Frame Rate As A Parameter

The buffer size at which errors accumulate determines the temporal character of the effect:

| Buffer Size | Ripple Rate | Character |
|---|---|---|
| 64 samples | ~689 Hz | Harsh, near sample-level |
| 512 samples | ~86 Hz | Fast flutter |
| 2048 samples | ~21 Hz | Slow wave-like degradation |
| 8192 samples | ~5 Hz | Long tidal swell |

---

## Three Nested Time Scales

The system operates simultaneously across three layers:

| Layer | Rate | Character |
|---|---|---|
| Sample-level truncation | 44.1kHz | Instantaneous, inaudible as rhythm |
| Error pool accumulation | Buffer rate | Slow texture evolution |
| k slew movement | Seconds–minutes | Glacial tidal shift |

Each layer is too slow to hear discretely but fast enough to shape the layer below it. This nesting produces the laminar, smooth feel — no single layer moves fast enough to create turbulence.

---

## The Control Philosophy

### Smooth, Not Slow

The defining property is **continuity** — no discontinuities in the parameter space. The effect can change at varying rates but must never jump. The performer touches momentum, not values.

### Slew Limiting

The physical control position and the actual `k` value are decoupled through a slew limiter:

```
k_current += (k_target - k_current) * slew_rate
```

Changes in `k` are smoothed so fast movements produce gradual arcs, not sudden jumps. The control leads; the parameter follows.

### Instability Threshold As A Performance Zone

The region near `k = 1.0` — where the system tips from decaying to escalating — should feel like leaning into something, not falling off a cliff. A soft knee around this threshold mathematically smooths the transition, creating a plateau the performer can inhabit. The system tips into escalation the way water tips over a weir.

### Emergent Feel Over Time

At low accumulation levels the system feels loose and responsive. As the error pool fills and approaches instability, it develops its own gravity — fast gestures get absorbed, the accumulated state resists change. The feel evolves organically over the course of a performance without any mode switching.

---

## The Hardware Interface — Inertial Rotor

### Concept

A heavy multidirectional rotor with real physical mass. The inertia of the hardware and the memory of the DSP become one unified system rather than a knob approximating inertia in software.

### Mechanical Specification

- **Brushless motor with ball bearings** — low friction so momentum carries genuinely
- **Hall effect sensor** (e.g. AH3503 linear hall sensor) — reads velocity and direction continuously, no detents, no dead zones, no mechanical wear
- **Heavy rotor** — brass or steel, mass concentrated toward the rim to maximize moment of inertia
- **Bidirectional** — spinning backward actively subtracts from the error pool

The encoder reports **velocity**, not position. Velocity feeds directly into the slew rate. The performer never touches a value — they touch momentum.

### Gesture Range

The same physical rotor should support two distinct gestural characters without mode switching:

**Sustained pull** — slow, building resistance, the rotor reluctantly giving way. Sustained effort loading the error pool gradually. Feels like standing up and pulling tape off a spooled reel, trying to get the hub to spin.

**Fast and twitchy** — quick flicks, short reversals, staccato spins. Sharp injections into the error pool. The rotor's inertia becomes a springboard rather than a weight.

These are not separate modes. They are the same physical system at different time scales. A heavy rotor naturally produces both — gesture duration determines which character emerges.

### Motor Resistance As Feedback

The motor can actively resist or assist based on the current error pool state. When errors are heavily accumulated and the system is near instability, the rotor feels heavier to turn. When the system is clean, it spins freely. The hardware communicates DSP state through tactile resistance — the performer knows from their hand alone how much energy is in the system.

### Reference Hardware

- **Teenage Engineering TP-7** — motorized tape reel with brushless motor, ball bearings, and hall sensor. Good reference for the interaction paradigm. The Error Accumulator would want more rotor mass and sensitivity at slow velocities.
- **Brushless gimbal motors** — designed for smooth, slow, controlled rotation. Closer use case than drone/RC motors. Search: *hollow shaft brushless gimbal motor*
- **Maxon Motor** — Swiss precision motors used in medical and aerospace applications. Expensive but benchmark quality
- **T-Motor** — boutique, very smooth, used in cinematography gimbals
- Digikey / Mouser for hall sensors individually

---

## What Happens At The Edge

At sustained high `k` values the accumulated error eventually exceeds the signal amplitude. Because the error is derived from the signal's own history, the output does not become random noise — it becomes a smeared, ghostly reflection of the original signal drowning in its own quantized memory. This is the intended destination of the effect when pushed to its limit.

---

## Notes

The effect's musicality depends on errors being **correlated to the signal** rather than random. Structured, signal-derived imperfections sound musical. Uncorrelated noise does not. The accumulation mechanism preserves this correlation across time — the system degrades in a way that retains the shape of the original material even as it consumes it.
