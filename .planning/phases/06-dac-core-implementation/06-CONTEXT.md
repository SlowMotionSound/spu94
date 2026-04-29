# Phase 6: DAC Core Implementation - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Implement the AK4309 interpolation filter and delta-sigma noise model as standalone, tested C modules operating at 44.1kHz in Q15 fixed-point. The filter ports Phase 5's scipy-verified coefficients to C using the folded half-band FIR pattern established in `spu94_fir.c`. The noise model produces shaped noise matching the AK4309's delta-sigma characteristics. Both modules are standalone — Phase 7 wires them into the pipeline.

</domain>

<decisions>
## Implementation Decisions

### FIR Optimization
- **D-01:** Skip zeros AND fold symmetry, matching the existing `spu94_fir.c` folded-form discipline. ~21 multiplies total across all three stages. Proven pattern, best cycle count for future MCU/FPGA port.
- **D-02:** Each stage needs its own accumulator width proof (pre-added pairs change worst-case bounds). Follow the same analytic+empirical validation pattern as `spu94_fir.c`'s D-02 proof.
- **D-03:** Coefficients come directly from Phase 5's `python3 tools/dac_filter_design.py --export-c` output. No manual transcription — copy the Q15 hex values verbatim.

### Noise Model Calibration
- **D-04:** Noise amplitude calibrated to produce ~90dB SNR per the AK4309B datasheet spec. This is a paper target — the datasheet number includes analog stages we don't model. Documented as a placeholder value.
- **D-05:** M5 hardware captures (DAC-HW-01 through DAC-HW-03) will refine the amplitude against real PS1 measurements. The current value is a reasonable starting point, not a final calibration.
- **D-06:** LFSR + 2nd-order highpass shaping producing +12dB/octave spectral slope. The shaping character matters more than the absolute amplitude — get the spectral shape right, treat the level as tunable later.

### Module Boundary
- **D-07:** Two separate files: `spu94_dac_fir.c` (interpolation filter + coefficient tables) and `spu94_dac_noise.c` (LFSR + 2nd-order shaping). Each has its own state struct, header, and unit tests.
- **D-08:** Phase 7 composes the two modules in sequence (filter then noise) at the pipeline integration point. The modules have no compile-time dependency on each other.
- **D-09:** Follows the ADPCM precedent where decoder (`spu94_adpcm.c`) and encoder (`spu94_adpcm_encode.c`) are separate files with independent state.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Phase 5 Outputs (Direct Inputs to Phase 6)
- `tools/dac_filter_design.py` — Source of Q15 coefficients via `--export-c`; also the scipy reference for bit-identity verification
- `.planning/phases/05-interpolation-filter-design/05-CONTEXT.md` — D-01 through D-13 decisions that constrain the filter design
- `.planning/phases/05-interpolation-filter-design/05-01-SUMMARY.md` — Actual achieved specs (0.078dB ripple, 49.3dB stopband, -0.016dB @20kHz) and deviations
- `docs/DECISIONS.md` §ADR-0054 — Passband ripple gray area resolution (datasheet authoritative)

### Existing FIR Pattern (Template for Phase 6)
- `src/spu94/spu94_fir.c` — Folded-form half-band FIR with circular buffer, int32 accumulator, width proof. THE pattern to follow.
- `src/spu94/spu94_fir_internal.h` — FIR stage API, delay-line conventions, accumulator width proof documentation
- `src/spu94/spu94_fir_coef.c` — Coefficient table layout (if separate coef file is needed)

### Existing Module Pattern (Template for Standalone Module)
- `src/spu94/spu94_adpcm.c` — Standalone module pattern: own state struct, no spu94_state dependency, header in include/spu94/
- `include/spu94/spu94_adpcm.h` — Public header pattern for standalone module

### DAC Research
- `.planning/research/DEEP-AK4309-FAMILY.md` — AK4309B datasheet extraction, filter pipeline specs
- `.planning/research/DEEP-DELTA-SIGMA.md` — Delta-sigma topology, noise shaping characteristics
- `.planning/research/PITFALLS-v1.2.md` — Critical pitfalls C1-C6, especially C2 (under-modeling)

### Requirements
- `.planning/REQUIREMENTS.md` — DAC-FILT-02 (filter C port), DAC-NOISE-01 (noise model)

### Project Constraints
- `.planning/PROJECT.md` §Constraints — C99, no heap, no locks, rt_safety gates
- `src/spu94/spu94_q15.h` — Q15 arithmetic primitives (q15_mul_truncate, sat_s16)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_fir.c` folded-form FIR: circular buffer delay line, `fir_read_tap`/`fir_push` helpers, accumulator width proof pattern. Phase 6 adapts this for three new half-band stages.
- `spu94_q15.h` Q15 primitives: `q15_mul_truncate`, `sat_s16`, `INT16_MAX`/`INT16_MIN` constants. All filter arithmetic uses these.
- `spu94_adpcm.c` module pattern: standalone state struct, block-processing API, zero heap, no spu94_state dependency until Phase 7 integration.
- `tests/python/derive_fir_reference.py`: Python FIR reference pattern for bit-identity testing. Phase 6 will need a similar Python reference for the new filter (or reuse Phase 5's scipy script directly).

### Established Patterns
- **Filter design → C port pipeline:** Phase 4 (v1.0) designed the 39-tap FIR in Python, ported to C, verified bit-identity. Phase 5→6 follows the same pipeline with the new coefficients.
- **Q15 fixed-point with int32 accumulator:** Proven sufficient for half-band FIR with int16 coefficients/inputs. Each new stage needs its own width proof.
- **ADR discipline:** Every gray-area resolution gets a numbered ADR in `docs/DECISIONS.md`.
- **rt_safety gates:** rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency must all pass with the new modules.
- **-Werror/-pedantic:** All new C code must compile clean under the existing strict flags.

### Integration Points
- Phase 6 modules are standalone — no integration with `spu94_state` or the pipeline yet. That's Phase 7.
- Coefficient source: `python3 tools/dac_filter_design.py --export-c` (verbatim copy, no manual transcription).
- Bit-identity verification: Phase 6 unit tests must prove C output matches scipy output for identical input sequences.

</code_context>

<specifics>
## Specific Ideas

- The folded+zero-skip optimization should be documented in the same style as `spu94_fir.c`'s D-01/D-02 comments — accumulator width proof as a block comment, with analytic derivation and empirical test reference.
- The noise LFSR should use a well-known polynomial (e.g., maximal-length 16-bit or 32-bit) documented in the source. The specific polynomial choice is Claude's discretion as long as it's maximal-length.
- Unit tests should include: coefficient bit-identity vs scipy, impulse response verification, DC gain verification, noise spectral slope measurement, and state-reset-on-init behavior.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 6-DAC Core Implementation*
*Context gathered: 2026-04-28*
