---
phase: 06-dac-core-implementation
reviewed: 2026-04-28T12:00:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - include/spu94/spu94_dac_fir.h
  - src/spu94/spu94_dac_fir.c
  - src/spu94/spu94_dac_fir_coef.c
  - src/spu94/spu94_dac_fir_internal.h
  - include/spu94/spu94_dac_noise.h
  - src/spu94/spu94_dac_noise.c
findings:
  critical: 1
  warning: 2
  info: 0
  total: 3
status: issues_found
---

# Phase 6: Code Review Report

**Reviewed:** 2026-04-28T12:00:00Z
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

The DAC FIR filter implementation is well-structured, with thorough documentation, compile-time dimension checks via _Static_assert, and careful accumulator overflow proofs. The folded-form FIR with zero-skip optimization is correctly implemented. The noise module is compact and clearly documented.

Three issues were found: one blocker involving potential signed integer overflow in the FIR accumulator loop, and two warnings related to the noise module.

## Critical Issues

### CR-01: Potential int32 overflow in dac_fir_stage_apply accumulator from unchecked pair multiplication

**File:** `src/spu94/spu94_dac_fir.c:123`
**Issue:** The expression `(int32_t)c * pair` multiplies two `int32_t` operands. `c` is cast from `int16_t` (range [-32768, 32767]) and `pair` is the sum of two `int16_t` values (range [-65536, 65534]). While the current coefficients have a maximum absolute value of 0x4000 (16384), the type signature of the function accepts any `const int16_t *coef` -- a future coefficient change or a misuse of `spu94_dac_fir_test_stage_apply` with a crafted delay line could produce `pair` values that, when multiplied by `c`, exceed INT32_MAX. For example, if a coefficient were -32768 (INT16_MIN, a legal int16 value) and pair were -65536, the product would be +2,147,483,648 = INT32_MAX + 1, which is signed integer overflow (undefined behavior in C).

More concretely: the accumulator overflow proofs in the comment block (lines 30-80) prove that the **total accumulation** fits in int32 given the current coefficients, but they do not prove that each individual `(int32_t)c * pair` multiplication is free of UB. The proof implicitly relies on the fact that no current coefficient exceeds 16384 in magnitude, but this invariant is not enforced by the code and is not documented as a precondition.

**Fix:** Either (a) widen the multiplication to int64 for safety, which costs nothing on 64-bit targets and is negligible on 32-bit:
```c
acc += (int64_t)(int32_t)c * pair;
/* then at the end: return sat_s16((int32_t)(acc >> 15)); */
```
Or (b) add a documented precondition and a compile-time or static assertion that no coefficient exceeds 16384 in absolute value, making the proof explicit. Option (a) is preferred because it eliminates the UB risk entirely and makes the accumulator proofs unnecessary.

## Warnings

### WR-01: Noise LFSR produces only 4 distinct pre-shaping values -- extremely coarse quantization

**File:** `src/spu94/spu94_dac_noise.c:64`
**Issue:** The expression `((int32_t)(lfsr >> 16) - 32768) >> 14` maps 65536 possible upper-16-bit LFSR values to only 4 distinct values: {-2, -1, 0, 1}. After 2nd-order HP shaping (`y = x - 2*x_prev + x_prev2`), the output range is approximately [-6, +6], which is only about 3.7 bits of dynamic range. While the RMS level is correct for a -90dB noise floor, the noise has extremely poor spectral quality -- it is essentially a 2-bit signal being differenced, producing strong spectral lines and periodicity artifacts rather than the smooth rising-slope spectrum expected from a delta-sigma NTF. This will be audible as tonal artifacts rather than broadband noise, especially in quiet passages.

**Fix:** Reduce `DAC_NOISE_SHIFT` to a smaller value (e.g., 8 or 10) to preserve more input resolution, and compensate by scaling the 2nd-order difference output down instead. For example:
```c
#define DAC_NOISE_PRE_SHIFT   6   /* keep ~10 bits of LFSR resolution */
#define DAC_NOISE_POST_SHIFT  8   /* scale after HP shaping to hit -90dB target */

int16_t x = (int16_t)(((int32_t)(lfsr >> 16) - 32768) >> DAC_NOISE_PRE_SHIFT);
/* ... HP shaping ... */
return sat_s16(y >> DAC_NOISE_POST_SHIFT);
```
This preserves the spectral shape from the 2nd-order difference while maintaining broadband noise character. The exact shift values need tuning against the -90dB target.

### WR-02: LFSR seed is deterministic -- every instance produces identical noise sequence

**File:** `src/spu94/spu94_dac_noise.c:38,47-50`
**Issue:** `spu94_dac_noise_init` always seeds the LFSR with the constant `0xACE1`. In a stereo configuration (two instances, one per channel), both channels will produce identical noise sequences, causing the noise to be mono-correlated rather than independent. Real DAC quantization noise would be uncorrelated between channels. Additionally, every program run produces the same noise sequence, which is unrealistic (a real DAC's noise varies run-to-run).

**Fix:** Add a seed parameter to the init function, or accept a channel index and derive a per-channel seed:
```c
void spu94_dac_noise_init(spu94_dac_noise_state *state, uint32_t seed) {
    memset(state, 0, sizeof(*state));
    state->lfsr = seed ? seed : 0xACE1u;  /* guard against zero (absorbing state) */
}
```
Callers would pass different seeds per channel (e.g., `0xACE1` for L, `0x1ECA` for R). This is an API change, so it affects the public header.

---

_Reviewed: 2026-04-28T12:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
