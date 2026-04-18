> **Historical artifact — superseded by `.planning/PROJECT.md` and `.planning/REQUIREMENTS.md`.**
> Kept for traceability as the original project brief. Do not treat as current spec.

# PS1 SPU Reverb — Eurorack Module Concept

## Core Idea

Port the PlayStation 1's SPU reverb algorithm to a Eurorack hardware module. Not an emulation of the sound — a faithful hardware implementation of the actual algorithm, including its fixed-point arithmetic character and ADPCM coloration.

---

## What Makes The PS1 Reverb Distinctive

The SPU reverb is a classical Schroeder/Gardner-style topology — a network of all-pass filters and comb filters operating on a shared reverb work buffer in SPU RAM. What makes it distinctive is not the algorithm's sophistication but its *implementation artifacts*:

- 4-bit Sony ADPCM compression on source material introduces signal-correlated quantization noise
- Fixed-point integer arithmetic with truncation (not rounding) at each stage
- Hard clipping on the summed mix bus with a specific overflow character
- Mediocre DAC reconstruction filtering with phase non-linearity in the upper frequencies
- The reverb register set (dAPF1, dAPF2, vIIR, vCOMB1–4, vWALL, etc.) is fully documented and defines the algorithm precisely

---

## Signal Chain To Recreate

```
Input
  → ADPCM encode/decode (4-bit Sony variant)
  → Fixed-point mixer with integer truncation
  → Hard clip stage (specific PS1 overflow behavior)
  → SPU reverb algorithm (all-pass + comb filter network)
  → Mediocre DAC stage (limited dynamic range, ~80–90dB real-world)
  → 2nd-order analog LPF (~18–20kHz cutoff)
  → Output buffer with slightly elevated output impedance
Output
```

---

## Implementation Path

**FPGA is the correct approach.** The algorithm requires bit-accurate fixed-point arithmetic — you want the exact truncation behavior, not an approximation. An FPGA lets you implement the SPU pipeline at the register level.

Reference implementations already exist in open source:
- Mednafen's PSX SPU core (considered the most accurate behavioral implementation)
- MiSTer FPGA PSX core SPU implementation
- nocash's PSX specs document every reverb register in detail

These are the foundation. The module would essentially be a hardware-wrapped version of this logic with Eurorack I/O.

---

## Eurorack-Specific Considerations

**CV control over reverb registers** — the SPU's register set defines delay times, filter coefficients, and reflection levels. Exposing key registers as CV inputs would allow the reverb character to be modulated in ways the original hardware never permitted. Interesting targets:
- vIIR (input volume to reverb) — controls how much signal enters the reverb
- vWALL (reflection volume) — the apparent size of the space
- dAPF1/dAPF2 (all-pass filter offsets) — timbral character of early reflections

**The ADPCM stage as a separate normalled path** — the encode/decode loop could be bypassable, allowing the reverb to run clean or with the compression character added. A toggle or CV gate would switch between them.

**DAC quality as a design choice** — deliberately use a period-appropriate DAC chip (PCM56P or similar) for the output stage rather than a modern high-fidelity converter. The output reconstruction filtering should be analog, 2nd order, not oversampled.

---

## Reference Material

- problemkaputt.de — nocash's complete PSX hardware documentation including all SPU registers
- Mednafen source — open source, C implementation of the full SPU including reverb
- MiSTer PSX core — FPGA implementation, useful for understanding the hardware mapping
- Sony PlayStation SDK documentation (archived) — original register descriptions

---

## Notes

The reverb's musicality comes from structured, correlated imperfections rather than clean algorithmic design. Any implementation that smooths out the fixed-point truncation, replaces the ADPCM stage with clean audio, or uses a modern DAC will lose the character entirely. Bit-accuracy is not optional — it is the sound.
