# Phase 5: Interpolation Filter Design - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Design the AK4309's 8x digital interpolation filter as a scipy prototype with automated verification against the AK4309B datasheet specs. Produces filter coefficients, frequency response plots, and an ADR documenting the passband ripple gray area. This is a Python/scipy-only phase — no C code.

</domain>

<decisions>
## Implementation Decisions

### Filter Design Target
- **D-01:** Design the filter to match AK4309B datasheet specs exactly: +/-0.05 dB passband ripple, 41 dB stopband attenuation, -0.2 dB at 20 kHz
- **D-02:** Stereophile/Archimago PS1 measurements are NOT used as design targets or sanity-check overlays. They measure the full analog output chain (digital filter + SCF + CTF + op-amps + cables) and introduce ambiguity about which stage causes which deviation. Drop them entirely from the filter design workflow.
- **D-03:** Design a minimum-order filter that just meets the datasheet spec limits — authentic to cost-optimized mid-90s silicon. Do not over-design with tighter specs.

### Verification System
- **D-04:** The scipy script runs automated pass/fail assertions against three datasheet specs: (1) passband ripple within +/-0.05 dB across 0–22.05 kHz, (2) stopband attenuation ≥41 dB, (3) deviation at 20 kHz within tolerance of -0.2 dB
- **D-05:** Frequency response plot shows the designed filter against datasheet spec limits drawn as horizontal/vertical reference lines. No Stereophile/Archimago overlay.
- **D-06:** Verification is automated and deterministic — pass/fail, no eyeballing.

### Ripple ADR Stance
- **D-07:** The ADR states the AK4309B datasheet is authoritative for the digital interpolation filter's behavior. Confidence: HIGH for digital filter specs.
- **D-08:** Stereophile's "audible ripple" and "underspecified digital filter" observations are noted in the ADR but attributed to the composite analog output chain, not the digital filter alone.
- **D-09:** The ADR is honest about the gap: the DAC model reproduces the digital conversion stage only. It does not claim to reproduce the full PS1 output "sound" (which includes analog stages not modeled in v1.2). Confidence for full-chain reproduction: LOW.

### Cascade Architecture
- **D-10:** Implement as three cascaded 2x half-band FIR stages (2x → 2x → 2x = 8x total). Each half-band filter exploits the zero-coefficient property to halve the multiply count.
- **D-11:** Each stage can be independently designed and tested, then cascaded to verify the composite response meets the overall datasheet spec.
- **D-12:** If the researcher finds AKM-specific documentation confirming or contradicting the cascaded half-band assumption, update the design accordingly. If no documentation is found, proceed with cascaded half-band as a plausible era-typical assumption and document it honestly in the ADR.

### Research Flag
- **D-13:** The phase researcher MUST investigate whether AKM documentation (application notes, other datasheets from the AK43xx family, academic papers) confirms the internal filter architecture of the AK4309. This is a flagged investigation — the answer informs the ADR's confidence level but does not block the design (the frequency response meets datasheet spec regardless of internal structure).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### DAC Chip Specs
- `.planning/research/DEEP-AK4309-FAMILY.md` — AK4309B datasheet extraction: electrical specs, filter pipeline (8x FIR + SCF + CTF), pin configurations, AVM vs B variant differences
- `.planning/research/DEEP-DELTA-SIGMA.md` — Delta-sigma topology context (1-bit, noise shaping, why this is NOT an R2R DAC)

### Milestone Research
- `.planning/research/FEATURES-v1.2.md` — Feature landscape, artifact analysis, what to model vs what to skip
- `.planning/research/PITFALLS-v1.2.md` — Critical pitfalls C1-C6 and significant pitfalls S1-S5, especially C2 (under-modeling) and C5 (historical accuracy trap)
- `.planning/research/ARCHITECTURE-v1.2.md` — Where DAC stage fits in the signal chain

### Existing FIR Implementation (Pattern Reference)
- `src/spu94/spu94_fir.c` — Existing 39-tap half-band FIR (decimator/interpolator for 22.05↔44.1 kHz conversion). Phase 6 will port the new filter to C following this pattern.
- `src/spu94/spu94_fir_internal.h` — FIR stage API, delay-line conventions, accumulator width proof

### Requirements
- `.planning/REQUIREMENTS.md` — DAC-FILT-01, DAC-FILT-03 are this phase's requirements

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `src/spu94/spu94_fir.c` / `spu94_fir_internal.h`: Existing half-band FIR implementation with folded-form optimization, circular buffer delay line, Q15 arithmetic, accumulator width proof. Phase 6 will follow this exact pattern for the new interpolation filter.
- `tests/python/derive_fir_reference.py`: Python FIR reference used to verify bit-identity with the C implementation. The Phase 5 scipy script serves the same role for the new filter.

### Established Patterns
- **Filter design → C port pipeline:** Phase 4 (v1.0) designed the 39-tap half-band FIR with Python reference, then ported to C with bit-identity tests. Phase 5→6 follows the same pipeline.
- **Q15 fixed-point with int32 accumulator:** The existing FIR proves that int32 is sufficient for half-band filters with int16 coefficients and inputs (accumulator width proof in `spu94_fir.c`). Each new half-band stage in Phase 6 will need its own width proof.
- **ADR discipline:** Every gray-area resolution gets a numbered ADR in `docs/DECISIONS.md`.

### Integration Points
- Phase 5 is Python-only — no integration with the C codebase. The scipy script produces coefficients that Phase 6 consumes.
- The coefficients will eventually live in a `spu94_dac_fir_coef.c` file (Phase 6), following the `spu94_fir_coef.c` pattern.

</code_context>

<specifics>
## Specific Ideas

- The datasheet's +/-0.05 dB ripple spec IS the tolerance band — design the minimum-order filter that just meets it, don't aim tighter
- The AK4309AVM datasheet is lost; the AK4309B is used as the closest available proxy for digital filter specs (HIGH confidence for topology-class specs, MEDIUM for exact performance numbers)
- The three cascaded half-band stages should each be independently verifiable before cascade

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 5-Interpolation Filter Design*
*Context gathered: 2026-04-28*
