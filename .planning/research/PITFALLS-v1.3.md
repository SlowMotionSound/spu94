# Domain Pitfalls: True 8x DAC Oversampling for SPU-94 v1.3

**Domain:** Adding true 8x oversampling to an existing Q15 fixed-point DSP pipeline
**Researched:** 2026-04-30
**Confidence:** HIGH (derived from codebase analysis + DSP fundamentals; no external sources needed -- these pitfalls are structural consequences of the existing code)

---

## Orientation

This document covers pitfalls specific to **replacing the v1.2 44.1kHz FIR approximation with genuine 8x oversampling** in SPU-94. The v1.2 DAC FIR runs all three cascade stages at 44.1kHz on every sample -- a deliberate approximation that reproduces the AK4309's passband ripple character without actually upsampling. v1.3 changes this to: zero-stuff to 352.8kHz, run the interpolation cascade at the real elevated rates, and decimate back to 44.1kHz.

The v1.2 pitfalls document (PITFALLS.md) covers the broader DAC modeling domain. This document focuses narrowly on the *oversampling refactor* and its interactions with the existing pipeline.

---

## Critical Pitfalls

Mistakes that cause rewrites, incorrect audio, or broken regression suites.

### C1: Accumulator Overflow in Polyphase Sub-Filters

**What goes wrong:** The v1.2 accumulator width proofs in `spu94_dac_fir.c` were computed for the folded-form direct convolution. Stage 1 has only 1.04 dB of headroom (worst-case accumulator 0x71868EB2 vs INT32_MAX 0x7FFFFFFF). Polyphase decomposition restructures the computation: instead of one convolution with the full 55-tap filter, there are two sub-filters (one for even-phase samples, one for odd-phase). Each sub-filter sees a different subset of coefficients and delay-line values. The worst-case accumulator sum for a polyphase branch may differ from the full filter's proof.

**Why it happens:** For half-band filters, the polyphase decomposition is clean: one branch contains all the non-zero coefficients plus the center tap, and the other branch is trivial (outputs the center tap times the input, or just passes through a delayed sample). The non-trivial branch's worst-case is actually *smaller* than the full filter's, because the center tap contribution is separated out. But this must be verified, not assumed -- especially given the 1.04 dB margin.

**Consequences:** Silent int32 wraparound producing crackling, DC offsets, or corrupt audio. Because `sat_s16` clamps only the final `acc >> 15` result, an intermediate wraparound in `acc` itself produces wrong output with no observable error signal.

**Prevention:**
1. Re-derive the accumulator width proof for each polyphase sub-filter branch. For the half-band case, the non-trivial branch excludes the center tap from its accumulator, so its worst case = (full worst case - center_tap_contribution). For Stage 1: center tap contribution = 0x4000 * 0x8000 = 0x20000000. Full worst case = 0x71868EB2. Non-trivial branch worst case = 0x51868EB2, which has ~5.4 dB headroom. This is *safer* than the full filter.
2. Write the proof as a comment block in the new polyphase code, matching the existing pattern in `spu94_dac_fir.c`.
3. Extend `test_dac_fir_overflow_proof.c` to exercise each polyphase branch independently with adversarial (all-positive / all-negative / alternating-sign) delay line contents.

**Detection:** Compile-time `_Static_assert` for worst-case bounds; runtime test with adversarial inputs.

**Phase to address:** Polyphase FIR implementation phase (first code phase).

---

### C2: Coefficient Reuse Is Correct -- But the Implementation Changes Entirely

**What goes wrong:** The v1.2 coefficients in `spu94_dac_fir_coef.c` were designed by `dac_filter_design.py` at the correct stage operating rates (88.2kHz, 176.4kHz, 352.8kHz). The `build_composite()` function in the design script already computes the true polyphase cascade response by upsampling and convolving. The coefficients themselves are correct for true oversampling -- the error in v1.2 is in the C *implementation*, which applies all three stages at 44.1kHz instead of at their designed rates.

The trap is twofold:
1. **Thinking the coefficients need redesign.** They do not. The design script already does the right thing.
2. **Thinking the coefficients can be dropped into the new implementation without rethinking the data flow.** They can, but the `spu94_dac_fir_step()` function needs a complete rewrite: from "push one sample, cascade three stages, return one sample" to "accept one 44.1kHz sample, produce 8 oversampled outputs (or manage the internal rate expansion), and return one decimated output."

**Why it happens:** The v1.2 code is clean and modular. It looks like a small change to "just run it faster." But the entire control flow -- when to clock each stage, how to manage intermediate samples between stages, how to decimate back -- is new logic.

**Consequences:** If treated as a minor refactor, subtle bugs in phase tracking, intermediate sample routing, or decimation timing. These produce audio artifacts that may only appear on specific input patterns (e.g., only when a signal transition aligns with a particular sub-sample phase).

**Prevention:**
1. Keep `spu94_dac_fir_coef.c` completely unchanged. Zero modifications to coefficient tables.
2. Write a new implementation function (e.g., `spu94_dac_fir_step_8x()`) alongside the existing `spu94_dac_fir_step()`. Do not modify the v1.2 function until the v1.3 function is verified.
3. The new function's contract: one int16 input at 44.1kHz in, one int16 output at 44.1kHz out. All 8x processing is internal.
4. Verify by comparing the new function's frequency response against `dac_filter_design.py --verify` composite response.

**Phase to address:** Polyphase FIR implementation phase.

---

### C3: Golden File Mass Invalidation (55 DAC + Unknown Pipeline Goldens)

**What goes wrong:** 135 golden files exist. 55 are DAC-specific (in `tests/golden/dac_isolated/` and `tests/golden/space_echo/dac/`). An unknown number of the remaining 80 were generated with DAC enabled. Switching to true 8x oversampling changes the DAC FIR transfer function. Every golden file that includes DAC processing will produce different output, and every SHA-256 sidecar will fail.

**Why it happens:** The v1.2 approximation and the v1.3 true polyphase produce mathematically different outputs. Both are "correct" for their respective approaches, but the golden files encode v1.2's specific output.

**Consequences:**
1. Naively regenerating goldens loses the ability to detect bugs introduced during the refactor. You cannot distinguish "intentional improvement" from "introduced bug" if the reference has been updated.
2. Not regenerating goldens blocks the entire test suite.
3. The witness-diff thresholds in `config/witness_diff_thresholds.json` may shift if DAC changes affect the reverb's frequency content.

**Prevention:**
1. **Before writing any C code:** Git-tag or archive the v1.2 golden set. These are the "v1.2 reference."
2. **Write a delta characterization test first.** This test runs both v1.2 (`spu94_dac_fir_step`) and v1.3 (`spu94_dac_fir_step_8x`) on the same inputs and measures:
   - RMS difference (should be small -- same design, different implementation)
   - Peak difference (should be bounded)
   - Frequency response difference (v1.3 should show better image rejection)
3. **Assert DAC-off goldens are bit-identical.** Any golden generated with `--dac off` or with `dac_enabled = 0` must produce the exact same SHA-256 before and after the refactor. This proves the change is isolated to the DAC path. If even one DAC-off golden changes, the refactor has leaked into the wrong code path.
4. **Regenerate DAC-on goldens only after the delta test passes.** The delta test IS the regression gate during the transition.
5. **Document the golden file transition in an ADR.** Note which goldens changed, why, and what the measured delta was.

**Detection:** CI fails on SHA-256 mismatch. The risk is not detection -- it is managing the transition.

**Phase to address:** Must be addressed in the first implementation phase, before any DAC code changes. The delta test and DAC-off-identity assertion are prerequisites.

---

### C4: Zero-Stuffing Image Rejection -- Getting the Cascade Order and Rates Right

**What goes wrong:** True 8x oversampling works by cascading three 2x upsample stages:

```
44.1kHz -> [zero-stuff 2x] -> Stage 1 at 88.2kHz -> [zero-stuff 2x] -> Stage 2 at 176.4kHz -> [zero-stuff 2x] -> Stage 3 at 352.8kHz
```

Each zero-stuffing creates a spectral image that the subsequent filter must suppress. Stage 1 has the hardest job: rejecting the image at 44.1kHz +/- 20kHz (narrow transition band, hence 55 taps). Stage 2 and Stage 3 have progressively easier jobs (wider transition bands, hence 11 and 7 taps).

The trap is implementing stages in the wrong order, at the wrong rate, or confusing which stage handles which image.

**Why it happens:**
- The v1.2 code runs all stages at the same rate in series, which obscures the rate-change structure.
- The `dac_filter_design.py` script designs each stage at its correct rate, but the C code does not enforce this.
- The cascade order and rate assignment are implicit in the design, not explicit in the code.

**Consequences:** If Stage 1 runs at the wrong rate, its transition band misses the image frequency, and a spectral image leaks through. This produces audible aliasing -- metallic, digital-sounding artifacts that defeat the purpose of faithful DAC modeling.

**Prevention:**
1. Clearly label each stage's operating rate in the code and in comments.
2. The processing order is: input at 44.1kHz -> zero-stuff -> Stage 1 (88.2kHz) -> zero-stuff -> Stage 2 (176.4kHz) -> zero-stuff -> Stage 3 (352.8kHz) -> decimate (take every 8th) -> output at 44.1kHz.
3. For polyphase efficiency: the zero-stuffing + filtering can be merged. When zero-stuffing by 2, every other input to the filter is zero. The polyphase decomposition exploits this: one sub-filter processes real samples, the other processes zeros (trivially). This means Stage 1 at 88.2kHz requires only ~15 MACs per 44.1kHz input sample, not 30 (half the taps multiply zero).
4. Write a sweep test: drive a sine sweep from 100Hz to 20kHz through the 8x cascade, FFT the 352.8kHz output, and verify no spectral content above 22.05kHz exceeds -41dB relative to the passband.

**Detection:** Frequency-domain sweep test; visual inspection of spectrogram.

**Phase to address:** Polyphase FIR implementation phase.

---

### C5: Decimation Path -- Aliasing From DAC Noise Model

**What goes wrong:** After processing at 352.8kHz internally, the signal must be decimated back to 44.1kHz. Decimation aliases any energy above 22.05kHz into the audio band. Two sources of ultrasonic energy exist:

1. **FIR stopband leakage:** The cascade provides 41dB of stopband rejection, not infinite. Residual image energy above 22.05kHz will alias back, but at -41dB it is inaudible.
2. **Delta-sigma noise model:** In v1.2, the noise model (`spu94_dac_noise.c`) runs at 44.1kHz and adds shaped noise after the FIR. If v1.3 moves the noise model to 352.8kHz (before decimation) for more faithful delta-sigma behavior, the noise energy above 22.05kHz will alias into the audio band during decimation.

The v1.2 noise model uses an LFSR + 2nd-order high-pass shaping, producing a +12dB/octave noise slope that concentrates energy at high frequencies. At 352.8kHz, most of this energy would be ultrasonic -- and would alias back during decimation.

**Why it happens:** On real hardware, the AK4309's delta-sigma noise is shaped ultrasonically and removed by the analog reconstruction filter. There is no decimation step in hardware. SPU-94's decimation is an artifact of digital simulation.

**Consequences:** If noise is applied at 352.8kHz and then decimated without additional anti-alias filtering, the audio-band noise floor rises. The shaped noise that was safely ultrasonic folds back into the audible band.

**Prevention:**
1. **Recommended approach for v1.3: keep the noise model at 44.1kHz, applied after decimation.** This matches the v1.2 architecture, avoids the aliasing problem entirely, and is documented as a known approximation.
2. If the noise model is moved to 352.8kHz in a future milestone, add a separate anti-alias low-pass before decimation, or use the FIR cascade itself as the anti-alias filter (run it in reverse for decimation).
3. Make this an ADR: "Noise model remains at 44.1kHz for v1.3. Moving to 352.8kHz requires anti-alias filtering during decimation and is deferred."

**Detection:** Enable noise model, measure the output noise floor with and without 8x oversampling. If the noise floor rises with oversampling enabled, decimation aliasing is occurring.

**Phase to address:** Integration phase (when wiring the new FIR into `spu94_process.c`).

---

## Moderate Pitfalls

### M1: CPU Budget -- Polyphase vs Naive 8x Cost

**What goes wrong:** There are three implementation strategies with very different CPU costs:

| Strategy | MACs/channel/sample | Relative to v1.2 |
|----------|-------------------|-------------------|
| v1.2 (all at 44.1kHz) | 22 | 1.0x |
| Naive 8x (no polyphase) | ~176 | 8.0x |
| Polyphase with zero-skip | ~35 | 1.6x |
| Polyphase with zero-skip + symmetry folding | ~22 | 1.0x |

The best polyphase implementation is computationally *equivalent* to v1.2 because half-band filters have the property that every other coefficient is zero, and every other input to each stage is zero (from zero-stuffing). When you combine zero-coefficient skip (already in v1.2) with zero-input skip (from polyphase decomposition), the total MAC count stays the same.

The trap is implementing naive 8x (running the full filter at the elevated rate on every sample, including the zero-stuffed zeros) and getting an 8x cost increase.

**Why it happens:** The naive approach is the obvious first implementation: insert zeros, run the filter, take every 8th output. It works correctly but wastes 7/8 of the computation multiplying by zeros.

**Consequences:** 8x CPU cost may violate real-time budgets, especially on future MCU targets. Even on desktop, it is wasteful and will show up in benchmarks.

**Prevention:**
1. Implement polyphase decomposition from the start. Do not implement naive 8x as an intermediate step.
2. For half-band filters, the polyphase is simple: on "real sample" phases, run the non-trivial sub-filter; on "zero" phases, the output is just the center tap times the previous real sample. This is a conditional branch, not a full filter evaluation.
3. The existing `pairs` tables in `spu94_dac_fir_coef.c` can be reused for the non-trivial branch's folded-form computation. The folded symmetry optimization is orthogonal to polyphase decomposition.
4. Benchmark the polyphase implementation against v1.2. It should be within 10-20% of v1.2's cost, not 8x.

**Detection:** `rt_bench_latency` test, `test_benchmark.py`.

**Phase to address:** Polyphase FIR implementation phase. This is a design choice, not an optimization afterthought.

---

### M2: State Struct Growth and SPU94_STATE_SIZE_MAX

**What goes wrong:** The `spu94_dac_fir_state` struct currently holds three delay lines (55 + 11 + 7 = 73 int16 entries) plus three uint8 indices. With polyphase, the delay line sizes per sub-filter are *smaller* (each polyphase branch has ceil(N/2) taps), but there are now two sub-filters per stage, plus phase counters to track which sub-sample phase the cascade is on.

The `_Static_assert` in `spu94_state_internal.h` enforces `sizeof(spu94_state) <= SPU94_STATE_SIZE_MAX`. If the struct grows past this limit, the build fails.

**Why it happens:** Polyphase decomposition trades computation for state: instead of one delay line clocked at the elevated rate, there are two half-length delay lines clocked at the base rate. The total memory is roughly the same, but additional phase counters and possibly intermediate sample buffers add to the struct.

**Consequences:** Build failure if `SPU94_STATE_SIZE_MAX` is exceeded. If the macro is bumped, it is a minor ABI change that should be documented.

**Prevention:**
1. Check current `SPU94_STATE_SIZE_MAX` value before designing the new state layout.
2. Polyphase sub-filter delay lines for half-band: Stage 1 has 55 taps, so each polyphase branch has 28 taps. Stage 2: 11 -> 6 taps each. Stage 3: 7 -> 4 taps each. Total: 2*(28+6+4) = 76 int16 entries vs current 73. Negligible growth.
3. Phase tracking: add 3 uint8 phase counters (one per stage) and 2 intermediate int16 buffers (output of Stage 1 and Stage 2 at their respective elevated rates). Total additional state: ~10 bytes.
4. If `SPU94_STATE_SIZE_MAX` needs bumping, do it deliberately with an ADR.

**Detection:** `_Static_assert` fires at compile time.

**Phase to address:** Polyphase FIR implementation phase (struct redesign).

---

### M3: Polyphase Phase Tracking -- The Hidden Complexity

**What goes wrong:** In a three-stage cascaded polyphase system, each stage has its own phase counter (which sub-sample phase of the 2x upsample we are on). When processing one 44.1kHz input sample, the cascade must:
- Stage 1: process 2 sub-phases (produces 2 outputs at 88.2kHz)
- Stage 2: process 2 sub-phases per Stage 1 output = 4 total (produces 4 outputs at 176.4kHz)
- Stage 3: process 2 sub-phases per Stage 2 output = 8 total (produces 8 outputs at 352.8kHz)

But with decimation back to 44.1kHz, only 1 of the 8 Stage 3 outputs is kept. The efficient implementation recognizes this and does NOT compute all 8 outputs -- it only computes the one that will be kept. This requires tracking which sub-phases align with the decimation point across all three stages.

The trap is getting the phase alignment wrong, which produces the correct output at the wrong time offset, or the wrong output at the correct time.

**Why it happens:** Three nested 2-phase counters create 8 possible states (2^3). Only one state produces the decimated output. The other 7 states either produce intermediate results needed for future filter evaluations or can be skipped entirely. Determining which is which requires careful analysis.

**Consequences:** Phase-offset audio (sounds slightly delayed or advanced compared to v1.2, breaking golden files even when the frequency response is correct). Or, worse, computing the wrong polyphase branch and producing a subtly different transfer function.

**Prevention:**
1. For decimation, recognize that the three-stage cascade with 8x overall decimation is equivalent to: process Stage 1 polyphase at 44.1kHz (one real-sample sub-phase and one zero sub-phase per input), then Stage 2 at 88.2kHz (ditto), then Stage 3 at 176.4kHz (ditto). Each stage produces one output per input sample at the *input* rate, not at the elevated rate. This eliminates the nested phase tracking entirely.

   Wait -- this is wrong. For interpolation + decimation, the approach is:
   - Upsample 8x: one real sample + 7 zeros per period
   - Filter through the cascade at 352.8kHz
   - Keep every 8th output

   The efficient polyphase approach for *interpolation followed by decimation* is to compute only the one output sample you need. This is equivalent to running a single "composite" polyphase filter at 44.1kHz that produces one output per input. The composite filter is the convolution of all three stages.

2. **Simpler alternative:** Compute the composite filter once (as `dac_filter_design.py`'s `build_composite()` does), quantize it to Q15, and run it as a single FIR at 44.1kHz. This avoids all phase-tracking complexity. The composite filter is long (55*4-3 + 11*2-1 + 7 = 217 + 21 + 7 = ~245 taps after upsample-and-convolve), but many coefficients are zero (half-band property propagates through convolution).

3. **Recommended approach:** Use the three-stage cascade, but implement it as a polyphase filter bank at 44.1kHz. Each stage processes one input sample and produces one output sample. The zero-stuffed zeros are implicit (skip the multiplies with zero input). The sub-phase is always the same for interpolate-then-decimate: the "real sample" phase. The "zero" phases never need to be computed because they contribute to outputs we are decimating away.

4. Write a unit test that verifies the v1.3 polyphase output matches a known-good float implementation (Python `build_composite()` applied to the same input) within +/-1 LSB.

**Detection:** Impulse response test: feed a single impulse through the system, compare output against the Python composite filter's Q15 response.

**Phase to address:** Polyphase FIR implementation phase. This is THE central design decision of v1.3.

---

### M4: Latency Change From True Polyphase

**What goes wrong:** The group delay of the true polyphase cascade differs from the v1.2 approximation. In v1.2, the three stages in series at 44.1kHz have a combined group delay of:
- Stage 1: (55-1)/2 = 27 samples
- Stage 2: (11-1)/2 = 5 samples  
- Stage 3: (7-1)/2 = 3 samples
- Total: 35 samples at 44.1kHz

In the true polyphase cascade, the group delay depends on the composite filter's length and the decimation factor. The composite filter has ~245 taps at 352.8kHz, which is (245-1)/2 = 122 samples at 352.8kHz, or 122/8 = 15.25 samples at 44.1kHz. This is different from 35 samples.

This affects:
1. `spu94_get_total_latency_samples()` -- reported to DAW hosts for plugin delay compensation.
2. The latency compensation delay buffer (28 samples for ADPCM alignment).
3. Golden file alignment (audio is shifted by a different number of samples).

**Why it happens:** The v1.2 cascade processes each stage in series, each adding its own group delay. The polyphase cascade's effective delay is the composite filter's delay divided by the decimation factor, which is shorter because the stages operate concurrently at elevated rates.

**Consequences:** Time-misaligned buses producing comb filtering. DAW hosts compensating for wrong latency, causing audible flanging.

**Prevention:**
1. Measure the actual group delay of the v1.3 implementation empirically: feed an impulse, find the peak output sample index.
2. Update `SPU94_LATENCY_SAMPLES` or add a separate DAC latency constant.
3. The ADPCM latency compensation buffer (28 samples) is independent of DAC delay -- it compensates for ADPCM block delay, not DAC filter delay. Verify this assumption holds by checking that ADPCM+DAC latency comp still aligns the buses correctly.
4. If the latency change breaks golden file alignment, this is expected and should be part of the golden file transition plan (Pitfall C3).

**Detection:** Impulse response test comparing peak position to expected group delay. Existing `test_fir_chain_latency.c` pattern.

**Phase to address:** Integration phase.

---

### M5: v1.2 Mode Preservation -- Don't Delete What Works

**What goes wrong:** If v1.3 replaces `spu94_dac_fir_step()` entirely, users and tests lose the ability to reproduce v1.2 output. The 55 DAC golden files become permanently orphaned.

**Why it happens:** The refactor instinct is to replace, not to add alongside. The v1.2 function "isn't needed anymore."

**Consequences:** No A/B comparison during development. No regression gate against v1.2 behavior. Existing DAC golden files become useless.

**Prevention:**
1. Keep `spu94_dac_fir_step()` as-is. Add `spu94_dac_fir_step_8x()` alongside it.
2. Add a mode enum or toggle: `spu94_set_dac_mode(state, SPU94_DAC_APPROX)` vs `spu94_set_dac_mode(state, SPU94_DAC_TRUE_8X)`. Default to `SPU94_DAC_TRUE_8X` for v1.3, but preserve the v1.2 path.
3. This follows the existing pattern of `dac_fir_enabled` / `dac_noise_enabled` independent toggles. A `dac_mode` enum is a natural extension.
4. v1.2 golden files remain valid for `SPU94_DAC_APPROX` mode. New goldens are generated for `SPU94_DAC_TRUE_8X` mode.

**Detection:** CI runs goldens for both modes.

**Phase to address:** API design, before implementation begins.

---

### M6: The Composite Filter Shortcut -- Tempting But Wrong

**What goes wrong:** Since the three-stage cascade followed by 8x decimation is equivalent to a single long FIR at 44.1kHz (the composite filter from `build_composite()`), a tempting shortcut is: compute the composite coefficients, quantize to Q15, and run a single convolution. This is simpler to implement (no phase tracking, no cascaded stages) and produces the correct frequency response.

The problem: the composite filter has ~245 taps at 352.8kHz, which after 8x decimation means ~31 effective taps at 44.1kHz. But many of these taps are non-zero (the half-band zero property does not survive convolution of upsampled stages). The resulting single FIR would have ~31 taps, all non-zero, requiring ~16 MACs with symmetry folding -- comparable to v1.2's 22 MACs.

However, the composite approach **loses the inter-stage interaction** that produces the AK4309's characteristic passband ripple. The v1.2 approach (cascade at 44.1kHz) and the true polyphase approach (cascade at elevated rates) both preserve the stage-by-stage ripple accumulation. The composite filter is the mathematical limit -- it is what the cascade converges to. But quantization effects differ: quantizing each stage's coefficients independently and cascading produces a different result than quantizing the composite. The cascade accumulates quantization noise from each stage (Q15 truncation at each stage boundary), which is part of the DAC's sonic character.

**Why it happens:** The composite filter is the "right answer" from a signal processing textbook. But SPU-94 is modeling a specific piece of hardware that cascades physical filter stages with quantized coefficients and intermediate truncation.

**Consequences:** Slightly different passband ripple pattern. Possibly inaudible, but departs from the hardware-faithful approach that is SPU-94's core value.

**Prevention:**
1. Implement as a true three-stage cascade, not as a composite filter.
2. Each stage must truncate its output to int16 before passing to the next stage, matching the real AK4309's inter-stage precision.
3. Use the composite filter only as a verification reference (compare the cascade's frequency response against it to confirm they are close).

**Detection:** Frequency response comparison between cascade and composite implementations.

**Phase to address:** Design decision, before implementation.

---

## Minor Pitfalls

### N1: Circular Buffer Index Type

**What goes wrong:** The current `spu94_dac_fir_state` uses `uint8_t` for delay line indices. Maximum delay line is 55 taps (Stage 1), well within uint8 range. Polyphase sub-filters have shorter delay lines (~28 taps max), so uint8 remains sufficient. No issue.

**Prevention:** Keep uint8_t. Only promote to uint16_t if any buffer exceeds 255 entries.

---

### N2: In-Place Processing Contract

**What goes wrong:** `spu94_process()` supports in-place operation (`L_out == L_in`). The 8x polyphase implementation must not use the output buffer as scratch space for intermediate rate conversions. Since the polyphase processes one input sample at a time and produces one output sample, there is no intermediate buffer needed -- the in-place contract is preserved naturally.

**Prevention:** Maintain the per-sample-in, per-sample-out contract from v1.2. No block-level intermediate buffers.

---

### N3: `dac_filter_design.py` Needs a New Verification Mode

**What goes wrong:** The design script verifies the composite cascade response but does not verify the polyphase implementation's response. A `--verify-polyphase` mode should compare the Python polyphase implementation against the float cascade response to confirm they are equivalent.

**Prevention:** Add a `--verify-polyphase` flag to the design script that simulates the polyphase cascade with Q15 quantization and compares against the float composite. This becomes the reference for the C implementation's frequency response test.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Design / API | M5 (v1.2 preservation), M3 (phase tracking), M6 (composite shortcut) | Define mode enum; choose cascade-not-composite; design phase tracking before coding |
| Polyphase FIR implementation | C1 (overflow), C2 (implementation rewrite), C4 (cascade order), M1 (CPU cost) | Re-derive proofs; keep coefficients unchanged; implement polyphase not naive 8x |
| Integration into spu94_process.c | C5 (noise aliasing), M4 (latency), C3 (golden files) | Keep noise at 44.1kHz; measure group delay; archive v1.2 goldens first |
| Verification | C3 (golden transition), N3 (design script) | Delta characterization test; DAC-off identity assertion; add --verify-polyphase |
| I/O surface updates | M5 (mode toggle in CLI/Python/JUCE) | Expose mode enum through all surfaces |

---

## Key Architectural Insight

The central design decision for v1.3 is how to structure the polyphase computation. There are three valid approaches, ranked by recommendation:

**1. Three-stage cascade at 44.1kHz using polyphase identity (RECOMMENDED)**

Each stage is decomposed into two polyphase sub-filters. For one 44.1kHz input sample:
- Stage 1: compute one output from the "real sample" sub-filter. The "zero" sub-filter contributes only on alternate outputs (which we decimate away for the final output, but need for feeding Stage 2).
- Actually, for interpolation, BOTH sub-filter outputs are needed to feed the next stage. The cascade must produce 2 outputs from Stage 1 (at 88.2kHz), then 4 from Stage 2, then 8 from Stage 3, and keep 1.

This means the "only compute what you need" optimization requires careful analysis of which Stage 3 output aligns with the decimation point, then back-tracking to determine which Stage 2 and Stage 1 outputs are needed.

For a half-band 2x interpolation filter, the polyphase decomposition means:
- Phase 0 (real sample input): output = sum of non-zero coefficients * input history
- Phase 1 (zero input): output = center_tap * current_real_sample (because all other coefficients multiplying the zero-stuffed zero are zero, and the center tap multiplies the real sample)

Wait -- this needs more careful analysis. For a half-band filter with zero coefficients at odd positions (except center), the polyphase sub-filters are:
- E0(z) = coefficients at even indices = all the non-zero coefficients + the zero coefficients at even positions that happen to be zero
- E1(z) = coefficients at odd indices = center tap (non-zero) + all-zeros elsewhere

So polyphase Phase 1 output = center_tap * input_sample. This is trivial -- one multiply.
Polyphase Phase 0 output = convolution of non-zero even-indexed coefficients with input history. This is the same as the folded-form computation in v1.2, minus the center tap.

For the cascade, each Stage produces 2 outputs per input. Only the outputs that feed into the "kept" decimation path need to be computed. For 8x interpolation followed by 8x decimation, only 1 of 8 Stage 3 outputs is needed. This propagates back: only some Stage 2 and Stage 1 outputs are needed.

The exact computation tree depends on which of the 8 output phases aligns with the decimation point. For a causal system starting at phase 0, the analysis is:
- We want Stage 3 output at phase k (k=0..7)
- Stage 3 phase k requires Stage 2 outputs at phases k/2 and (k-1)/2 (approximately)
- This depends on the filter's group delay and phase alignment

**This is genuinely complex. The implementation should start with the naive approach (compute all 8 outputs), verify correctness, then optimize by pruning unnecessary computations.**

**2. Naive 8x (compute all, decimate) -- GOOD FOR VERIFICATION**

Zero-stuff to 352.8kHz, run all three stages at the elevated rate, take every 8th output. 8x the computation of v1.2, but dead simple to implement and verify. Use this as a reference implementation, then optimize.

**3. Composite filter (AVOID)**

Single long FIR. Loses inter-stage quantization behavior. Only use for frequency response verification.

---

## Sources

- `src/spu94/spu94_dac_fir.c` -- accumulator width proofs, folded-form implementation
- `src/spu94/spu94_dac_fir_coef.c` -- Q15 coefficients, DC gain measurements, pair tables
- `src/spu94/spu94_dac_fir_internal.h` -- dimension constants, `_Static_assert` guards
- `src/spu94/spu94_process.c` -- signal flow, DAC integration point (lines 115-122)
- `src/spu94/spu94_io_chain.c` -- FIR chain architecture, phase tracking pattern
- `src/spu94/spu94_state_internal.h` -- state struct layout, `SPU94_STATE_SIZE_MAX` guard
- `include/spu94/spu94_q15.h` -- Q15 arithmetic primitives, accumulator semantics
- `include/spu94/spu94_dac_fir.h` -- public API, state struct definition
- `tools/dac_filter_design.py` -- coefficient design, `build_composite()`, `--verify` mode
- Polyphase FIR theory: Vaidyanathan, "Multirate Systems and Filter Banks"
- Half-band filter properties: Crochiere & Rabiner, "Multirate Digital Signal Processing"

---

*Pitfalls research for: v1.3 True 8x Oversampled DAC milestone for SPU-94*
*Researched: 2026-04-30*
