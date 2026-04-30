# Phase 6: DAC Core Implementation - Discussion Log

**Date:** 2026-04-28
**Areas discussed:** 3/3 selected

## Area 1: FIR Folding Strategy

**Question:** How should Phase 6 handle FIR coefficient optimization?
**Options presented:**
1. Skip zeros only [RECOMMENDED] — 41 multiplies, simple loop, easiest to audit
2. Skip zeros + fold symmetry — ~21 multiplies, matches existing spu94_fir.c discipline
3. Literal (no optimization) — 73 multiplies, dead simple, wastes cycles on zero-multiplies

**User clarification:** Asked whether each method affects sound character.
**Claude explanation:** All three produce bit-identical output. Differences are code complexity, cycle count, and verification effort only.

**Selected:** Option 2 — Skip zeros + fold symmetry
**Rationale:** Matches existing codebase discipline, best hardware-port readiness.

## Area 2: Noise Model Calibration

**Question:** How should the noise model amplitude be calibrated?
**Options presented:**
1. Datasheet SNR as placeholder [RECOMMENDED] — 90dB per AK4309B spec, documented as paper target
2. Inaudible-but-present default — ~100dB, conservatively low
3. User-tunable amplitude — expose as runtime parameter

**Selected:** Option 1 — Datasheet SNR as placeholder
**Rationale:** Honest paper target; M5 hardware captures refine later.

## Area 3: Module Boundary

**Question:** How should the filter and noise modules be organized?
**Options presented:**
1. Two separate files [RECOMMENDED] — spu94_dac_fir.c and spu94_dac_noise.c
2. One combined file — spu94_dac.c
3. Three files (coefs separate) — matches exact spu94_fir.c + spu94_fir_coef.c split

**Selected:** Option 1 — Two separate files
**Rationale:** Independent testability, follows ADPCM decoder/encoder precedent.

## Deferred Ideas

None.

## Claude's Discretion Items

- LFSR polynomial choice (any maximal-length 16-bit or 32-bit)
- Specific circular buffer sizing for each stage's delay line
- Unit test organization (one test file per module or combined)
