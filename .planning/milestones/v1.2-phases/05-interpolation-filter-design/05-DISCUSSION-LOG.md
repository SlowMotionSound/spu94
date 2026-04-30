# Phase 5: Interpolation Filter Design - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-28
**Phase:** 05-interpolation-filter-design
**Areas discussed:** Filter design target, Measurement overlay, Ripple ADR stance, Cascade architecture

---

## Filter Design Target

| Option | Description | Selected |
|--------|-------------|----------|
| Datasheet spec | Design to +/-0.05dB ripple, 41dB stopband, -0.2dB at 20kHz. Use measurements as sanity check only. | ✓ |
| Measurement curves | Fit to Stereophile/Archimago measured response. More "authentic" but bakes analog chain into digital model. | |
| Datasheet + measurement envelope | Design to datasheet, widen tolerance if outside measurement envelope. | |

**User's choice:** Datasheet spec
**Notes:** None — clean pick.

---

## Measurement Overlay

| Option | Description | Selected |
|--------|-------------|----------|
| Eyeball overlay | Visual comparison against Stereophile screenshot | |
| Digitized curves | Extract numerical points from Stereophile images via plot digitizer | |
| Tolerance band | Define envelope informed by measurements, verify filter stays inside | |

**User's choice:** None of the above — user pushed back on the premise entirely.
**Notes:** Anthony questioned why we'd measure against something that incorporates the analog signal path when we're only focused on the AKM digital filter specs. This led to dropping the Stereophile/Archimago overlay entirely. The verification system now tests purely against the three datasheet specs with automated pass/fail assertions. The plot shows designed filter vs datasheet spec limits as reference lines — no external measurement data.

---

## Ripple ADR Stance

| Option | Description | Selected |
|--------|-------------|----------|
| Datasheet is authoritative | Trust +/-0.05dB for the digital filter. Stereophile's "audible ripple" attributed to analog chain. HIGH confidence for digital, LOW for full PS1 sound. | ✓ |
| Datasheet is a floor | Real chip probably exceeded spec due to 1995 silicon tolerances. MEDIUM confidence. | |
| Punt to hardware | Acknowledge ambiguity, flag for M5 hardware measurement. LOW confidence until measured. | |

**User's choice:** Datasheet is authoritative for the digital filter
**Notes:** Clean stance aligned with the design target decision — if we're designing to the datasheet, the ADR should trust the datasheet for the digital stage and be honest about the gap for the full analog chain.

---

## Cascade Architecture

| Option | Description | Selected |
|--------|-------------|----------|
| Three cascaded half-band stages | 2x→2x→2x = 8x. Era-typical, maps to Phase 6 fixed-point, independently testable. | ✓ |
| Single 8x FIR | One polyphase FIR. Simpler to prototype, harder to port, doesn't reflect likely hardware. | |
| Cascaded + single-FIR option | Design as cascaded, also compute equivalent single convolution. Ship both. | |

**User's choice:** Cascaded half-band stages, with a research flag
**Notes:** Anthony pushed back on the "almost certainly" qualifier for cascaded half-band being the AK4309's approach. Asked whether we can verify how AKM actually handled interpolation. Answer: the datasheet doesn't specify internal architecture, and the AK4309AVM datasheet is lost. Agreed to have the researcher agent investigate AKM-specific documentation. If nothing is found, proceed with cascaded half-band as a plausible but honestly-documented assumption. The frequency response meets datasheet spec regardless of internal structure.

---

## Claude's Discretion

None — all decisions made by the user.

## Deferred Ideas

None — discussion stayed within phase scope.
