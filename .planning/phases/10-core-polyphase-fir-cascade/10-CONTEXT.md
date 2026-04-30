# Phase 10: Core Polyphase FIR Cascade - Context

**Gathered:** 2026-04-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace `spu94_dac_fir_step` (44.1kHz single-rate FIR approximation) with true 8x oversampled interpolation: zero-stuff to 352.8kHz, run the three-stage AK4309 cascade at real operating rates (88.2/176.4/352.8kHz), decimate back to 44.1kHz. Prove zero blast radius on non-DAC paths via DAC-off golden identity assertion.

</domain>

<decisions>
## Implementation Decisions

### Implementation Approach
- **D-01:** Naive 8x zero-stuff implementation — literally insert 7 zeros between each input sample and run the existing `dac_fir_stage_apply` function at 8x rate through the cascade. This is what the AK4309 hardware does. ~176 multiplies per output sample vs 22 in v1.2, but well under the ~22,000ns per-sample budget on desktop. If MCU cost matters later, polyphase decomposition is a pure optimization that can be added without changing output.
- **D-02:** Do NOT pursue polyphase decomposition in this phase. The naive approach is simpler, faithful to hardware behavior, and fast enough. Polyphase is deferred as a future optimization if profiling shows it's needed.

### v1.2 Path Preservation
- **D-03:** Keep the existing `spu94_dac_fir_step` function intact. Add `spu94_dac_fir_step_8x` as the new true-oversampled path. `spu94_process.c` switches to calling the 8x version by default. Both functions coexist for Phase 11's A/B mode toggle (CMP-01).

### Prototype Strategy
- **D-04:** Prototype the 8x cascade in Python/scipy first (extend `tools/dac_filter_design.py`). Verify frequency response and impulse response match expectations at the elevated rate. Then port to C with a known-good reference to diff against. This catches inter-stage buffer ordering and decimation phase bugs before they hit C.

### Claude's Discretion
- Accumulator overflow proof re-derivation for the 8x path (same coefficients, zero-stuffed inputs reduce worst-case)
- Delay line dimensioning for 8x state (trivial: 8 samples × 2 channels × 2 bytes = 32 bytes intermediate buffer)
- Decimation sample selection (which of 8 outputs to keep — resolve with impulse test during prototype)
- Whether to add the 8x state to the existing `spu94_dac_fir_state` struct or create a new struct

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### DAC FIR Implementation (v1.2 — the code being extended)
- `src/spu94/spu94_dac_fir.c` — Current single-rate cascade; `dac_fir_stage_apply` is the function to reuse at 8x rate
- `src/spu94/spu94_dac_fir_internal.h` — Stage dimensions (55/11/7 taps), pair tables, static_assert guards
- `src/spu94/spu94_dac_fir_coef.c` — Coefficient tables (reuse verbatim, zero modification)
- `include/spu94/spu94_dac_fir.h` — Public API and state struct (extend for 8x)

### Polyphase Pattern Precedent (reverb FIR — same codebase)
- `src/spu94/spu94_fir.c` — Proven polyphase half-band interpolator; delay line convention, push/read helpers, folded-form apply. Same conventions apply to DAC FIR.

### Integration Point
- `src/spu94/spu94_process.c` — Lines 115-119: DAC section calls `spu94_dac_fir_step` per channel. This is where the 8x function gets wired in.

### Filter Design Tool
- `tools/dac_filter_design.py` — scipy prototype for coefficient design; extend for true 8x cascade verification

### v1.3 Research
- `.planning/research/STACK.md` — No new dependencies; int32 accumulators sufficient
- `.planning/research/FEATURES.md` — Table stakes vs differentiators; expected audible differences
- `.planning/research/ARCHITECTURE-v1.3.md` — Integration architecture; polyphase detail (deferred per D-02)
- `.planning/research/PITFALLS-v1.3.md` — Accumulator headroom, golden file transition, noise interaction

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `dac_fir_stage_apply` (static in spu94_dac_fir.c): Generic folded-form half-band apply — takes delay line, coefficients, pair table. Can be called at any rate. This is the core function the naive 8x approach reuses verbatim.
- `dac_fir_push` / `dac_fir_read_tap`: Circular buffer helpers parameterized by ntaps. Same convention as spu94_fir.c.
- `spu94_dac_fir_test_stage_apply`: Test-visible per-stage wrapper — useful for verifying individual stages at 8x rate.

### Established Patterns
- Mono API per channel (ADPCM precedent) — each channel gets its own state struct instance
- Folded-form + zero-skip optimization with accumulator overflow proofs in comment blocks
- `_Static_assert` guards on delay line dimensions vs coefficient table dimensions
- Test-visible functions for per-stage isolation testing

### Integration Points
- `spu94_process.c` DAC section (step 7): replace `spu94_dac_fir_step` call with `spu94_dac_fir_step_8x`
- `spu94_state_internal.h`: state struct contains `spu94_dac_fir_state dac_fir_l, dac_fir_r` — may need extension for 8x intermediate buffers
- Golden files in `tests/golden/` — DAC-on files will change; DAC-off files must be bit-identical

</code_context>

<specifics>
## Specific Ideas

- Anthony wants to explore the "magically smooth sound" of oversampling as a design principle — this is the foundation for that exploration
- The honest question "does it actually sound different?" is Phase 12's ADR — Phase 10 just builds the correct implementation

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope

</deferred>

---

*Phase: 10-Core Polyphase FIR Cascade*
*Context gathered: 2026-04-30*
