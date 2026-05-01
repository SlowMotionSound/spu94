# Phase 11: Noise Recalibration + Integration - Context

**Gathered:** 2026-05-01
**Status:** Ready for planning

<domain>
## Phase Boundary

Complete the DAC pipeline end-to-end: run the LFSR noise model at the true 352.8kHz rate with correct amplitude, add an A/B mode toggle for v1.2/v1.3 comparison, update latency reporting, and verify all existing surfaces (CLI/Python/JUCE) work identically.

</domain>

<decisions>
## Implementation Decisions

### A/B Mode Toggle (CMP-01)
- **D-01:** Use existing toggle pattern — simple set/get pair matching `dac_fir_enabled`, `dac_noise_enabled`, etc. No new enum, no elaborate API. Keep flush with everything else.
- **D-02:** The toggle selects between v1.2 (`spu94_dac_fir_step`) and v1.3 (`spu94_dac_fir_step_8x`) processing paths. Both functions already coexist per Phase 10 D-03.

### Noise Injection Strategy (DSP-05)
- **D-03:** Inject noise at 352.8kHz before decimation, NOT at 44.1kHz post-decimation. Run the LFSR 8 ticks per output sample at the true oversampled rate. The interpolation filter shapes the noise spectrum on the way down — this is what gives oversampling DACs their characteristic noise floor.
- **D-04:** This is standard practice for oversampling DAC modeling. The noise originates at the converter's clock rate and gets filtered by the reconstruction path, producing spectrally shaped noise rather than flat white noise.

### Gain Compensation
- **D-05:** Phase 10's `<<3` gain compensation stays in place. Final sign-off is a human listen gate — Anthony will listen to the output and approve or request adjustment. No code-level revisit unless the listen gate raises issues.

### Latency (DSP-07)
- **D-06:** Claude's discretion on the calculation. The true 8x path has different group delay than v1.2's single-rate approximation. Update `spu94_get_total_latency_samples` to report the correct value for whichever mode is active.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### DAC Implementation (from Phase 10)
- `.planning/phases/10-core-polyphase-fir-cascade/10-CONTEXT.md` — Phase 10 locked decisions (D-01 through D-04): naive zero-stuff, no polyphase, coexist with v1.2, scipy prototype first
- `.planning/phases/10-core-polyphase-fir-cascade/10-RESEARCH.md` — Technical details on FIR cascade, operating rates, coefficient reuse

### Noise Model
- `src/spu94/spu94_dac_noise.c` — Current LFSR + HP noise implementation, DAC_NOISE_SHIFT=14
- `include/spu94/spu94_dac_noise.h` — Noise state struct and step function API

### DAC FIR (modified in Phase 10)
- `src/spu94/spu94_dac_fir.c` — Both `spu94_dac_fir_step` (v1.2) and `spu94_dac_fir_step_8x` (v1.3) implementations
- `include/spu94/spu94_dac_fir.h` — Public declarations for both step functions

### Pipeline
- `src/spu94/spu94_process.c` — Main processing function, DAC section at lines 114-124
- `src/spu94/spu94_io_chain.c` — `spu94_get_total_latency_samples` implementation

### Public API
- `include/spu94/spu94.h` — All public toggles (dac_enabled, dac_fir_enabled, dac_noise_enabled, latency)

### Requirements
- `.planning/REQUIREMENTS.md` — DSP-05, DSP-07, CMP-01, INT-01

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_dac_noise_step`: Current noise function — needs 8x call loop or internal modification for 352.8kHz
- `spu94_dac_fir_step` / `spu94_dac_fir_step_8x`: Both paths exist, toggle just selects which one runs
- Toggle pattern: `spu94_set_*/spu94_get_*` pairs with `int enabled` — follow exactly for A/B mode

### Established Patterns
- All DSP in C core, surfaces (CLI/Python/JUCE) are thin wrappers with no DSP logic
- Q15 fixed-point throughout, sat_s16 for overflow protection
- Unity test framework with ctest labels (dac_fir, dac_noise, etc.)

### Integration Points
- `spu94_process.c` DAC section: noise injection point needs to move inside the 8x cascade loop (or a new 8x-aware noise injection path)
- `spu94_io_chain.c`: latency calculation needs mode-aware group delay
- CLI `--dac` flag, Python `set_dac_enabled()`, JUCE toggle: must work identically (INT-01)

</code_context>

<specifics>
## Specific Ideas

- The noise should interact with the interpolation filter cascade — this is what gives oversampling DACs their characteristic shaped noise floor (not flat white noise)
- A/B toggle should be as simple as the existing toggles — no ceremony
- Gain compensation listen gate: Anthony will evaluate the output by ear before final sign-off

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 11-Noise Recalibration + Integration*
*Context gathered: 2026-05-01*
