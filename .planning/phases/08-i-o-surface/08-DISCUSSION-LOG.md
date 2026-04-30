# Phase 8: I/O Surface - Discussion Log

**Date:** 2026-04-29

## Gray Area Selection

**Presented:** Control exposure per layer, CLI flag design, JUCE GUI layout
**Selected:** All three

## Area 1: Control Exposure Per Layer

### Q: Does every layer need all 10 new controls?
**Decision:** All 10 exposed in every layer. No hidden controls, no subsets.

## Area 2: CLI Flag Design

### Q: Fader value format on command line?
**Options:** Q15 hex / Float 0.0-1.0 / Percent 0-100
**Decision:** Float 0.0–1.0, CLI converts to Q15 internally.

### Q: DAC sub-toggle defaults when --dac is passed?
**Decision:** `--dac` enables master + FIR + noise all at once. Use `--no-dac-fir` or `--no-dac-noise` to disable individually.

## Area 3: JUCE GUI Layout

### Extended Discussion

User launched SPU-94 to look at current layout. Collaborative design session followed.

**Signal flow diagram concept:** User referenced Ensoniq ESQ-1 and ASM Hydrasynth front-panel signal flow diagrams where controls are placed next to their signal flow blocks. Explored building the GUI around a visual signal flow diagram. User tabled this idea — wants to think about it more. Captured as deferred idea.

**Final layout (4 zones top to bottom):**

1. **Toolbar:** Load | Play | Stop | Preset ▼ | Input Gain knob | Reverb Sends [ADPCM send / Dry Input send]
2. **Register panel:** untouched
3. **Mixer strip:** Dry | ADPCM | Reverb level knobs + Latency Comp toggle
4. **DAC section:** DAC on/off | FIR on/off | Noise on/off

**Removals:** ADPCM toggle from toolbar (no longer an always-on path), Wet/Dry knob (replaced by mixer strip).

**Latency Comp placement:** User chose mixer strip — "it decides how the master mixer handles signal summation, so it should live there."

**Reverb Sends section:** Must appear as visually outlined/grouped area so user understands these two knobs control what feeds the reverb.

## Deferred Ideas

1. Visual signal flow diagram as GUI — Ensoniq/ASM style panel-mounted block diagram. Revisit as future UI overhaul.
2. Parameter slew/smoothing control — M4 (carried from Phase 7).
