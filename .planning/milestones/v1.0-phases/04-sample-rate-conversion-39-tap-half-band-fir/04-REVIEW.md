---
phase: 04-sample-rate-conversion-39-tap-half-band-fir
reviewed: 2026-04-20T00:00:00Z
depth: standard
files_reviewed: 25
files_reviewed_list:
  - docs/BIBLIOGRAPHY.md
  - docs/DECISIONS.md
  - include/spu94/spu94.h
  - src/spu94/CMakeLists.txt
  - src/spu94/spu94_fir.c
  - src/spu94/spu94_fir_coef.c
  - src/spu94/spu94_fir_internal.h
  - src/spu94/spu94_io_chain.c
  - src/spu94/spu94_state_internal.h
  - tests/python/CMakeLists.txt
  - tests/python/derive_fir_reference.py
  - tests/python/fuzz_fir.py
  - tests/unit/CMakeLists.txt
  - tests/unit/fir/CMakeLists.txt
  - tests/unit/fir/test_fir_bit_identity.c
  - tests/unit/fir/test_fir_chain_latency.c
  - tests/unit/fir/test_fir_coef_table.c
  - tests/unit/fir/test_fir_dc.c
  - tests/unit/fir/test_fir_decimate.c
  - tests/unit/fir/test_fir_err_overflow_taps.c
  - tests/unit/fir/test_fir_frequency_sweep.c
  - tests/unit/fir/test_fir_impulse.c
  - tests/unit/fir/test_fir_interpolate.c
  - tests/unit/fir/test_fir_overflow_proof.c
  - tests/unit/fir/test_fir_round_trip_transparency.c
findings:
  critical: 0
  warning: 2
  info: 6
  total: 8
status: issues_found
---

# Phase 4: Code Review Report

**Reviewed:** 2026-04-20
**Depth:** standard
**Files Reviewed:** 25
**Status:** issues_found

## Summary

Phase 4 lands the 39-tap half-band FIR sample-rate converter (decimator + interpolator + chain wrapper), the coefficient table, a dual literal/folded audit reference, the D-05/D-06 err + overflow taps, and a broad test battery (bit-identity, overflow-proof, impulse, DC, frequency sweep, round-trip transparency, latency, err/overflow taps, chain latency) plus a 10^6-step Python/ctypes fuzz harness.

Overall quality is high. The D-02 accumulator-width proof is sound: worst-case |acc| = sum(|h|) * 32768 = 0xB9A6 * 0x8000 = 0x5CD30000, comfortably inside INT32_MAX with ~2.79 dB headroom; the bit-identity and overflow-proof tests pin both the algebraic and achievable-by-int16 bounds. Circular-buffer index math is correct (modulo-39 with uint8 indices, cast back to uint8 after the modulo). Sat_s16 is the canonical clamp. The NULL-state guards at the chain_step wrapper are consistent (zero outputs before return); the per-stage decimator/interpolator functions have tighter internal guards that match their contract as documented in the internal header.

Two correctness-adjacent warnings: (1) the SPU94_FIR_CASCADE_CLAMP alternate path contains a C99/C11 signed-left-shift of a potentially negative value (`(int32_t)sat_s16(running >> 15) << 15`), which is technically undefined behavior per C99 §6.5.7p4; (2) the Python fuzz harness assumes CANARY_OFFSET (4096) is safely beyond sizeof(struct spu94_state), but does not use the available `spu94_state_size()` accessor to validate this at runtime, so a future struct growth past 4KB would silently convert a legitimate write into a spurious canary-drift failure.

Six info-level findings cover docstring drift, code duplication in interp-phase-0/phase-1 helpers, an inconsistent NULL-state-output contract between `spu94_fir_decimate` and `spu94_fir_chain_step`, and a stale asm literal in a comment.

No critical issues. No security vulnerabilities. No correctness bugs on the production hot path (cascade-clamp path is opt-in and never enabled in production builds per the D-03 default).

## Warnings

### WR-01: Undefined behavior in cascade-clamp path: signed left shift of negative value

**File:** `src/spu94/spu94_fir.c:116, 124`
**Issue:** The `SPU94_FIR_CASCADE_CLAMP` alternate path runs `(int32_t)sat_s16(running >> 15) << 15` after every accumulation. `sat_s16` returns int16 in `[-32768, 32767]`; casting to int32 preserves the sign. Left-shifting a **negative** signed int32 by 15 is undefined behavior under C99 §6.5.7p4 ("If E1 has a signed type and nonnegative value ... otherwise, the behavior is undefined"). C++20 and C23 defined this as two's-complement, but SPU-94 targets `-std=c99 -pedantic -Werror` per API-07 / spu94.h comments, where it is UB. UBSan will flag this if any test binary is ever compiled with `-DSPU94_FIR_CASCADE_CLAMP` under ubsan.

This path is opt-in (documented as "test binary may build with this defined; production never does"), so the production hot path is unaffected. But the path exists as a real compile-time seam (ADR-0015) and could be exercised by a future witness test.

**Fix:** Perform the inverse shift in unsigned space to dodge the UB, then cast back:
```c
/* Safe replacement for `(int32_t)sat_s16(running >> 15) << 15`. */
int16_t clamped = sat_s16(running >> 15);
running = (int32_t)((uint32_t)(int32_t)clamped << 15);
```
Or equivalently multiply by `(1 << 15)`, which is the strictly-conforming spelling for the intent (scale back up to the accumulator's 15-bit-fraction layout). Either form avoids the UB while preserving the two's-complement semantics the code obviously intends.

### WR-02: Fuzz canary offset not validated against struct size

**File:** `tests/python/fuzz_fir.py:65-69, 119-124`
**Issue:** `CANARY_OFFSET = 0x1000` (4096) is hardcoded with the comment "struct spu94_state is ~544 bytes at end of Plan 03 ... offset 0x1000 is deep inside the slack region and will never be touched." Good intent. Problem: if a future plan grows the struct past 4096 bytes (still legal — `SPU94_STATE_SIZE_MAX` is 16384), the canary sits **inside** the in-use struct footprint. spu94_reset's byte-loop would then zero it every reset, producing a persistent canary-drift failure that looks like a real OOB bug.

The harness already loads `lib.spu94_state_size` (line 84-85) but never calls it. A single guard at start-of-run catches the regression with no added cost.

**Fix:** Validate the offset against the real struct size before entering the fuzz loop:
```python
actual_size = lib.spu94_state_size()
if CANARY_OFFSET < actual_size:
    sys.exit(f"FAIL: CANARY_OFFSET ({CANARY_OFFSET:#x}) is inside "
             f"the struct footprint ({actual_size} bytes). "
             f"Struct grew; bump CANARY_OFFSET or rework canary placement.")
if CANARY_OFFSET + 4 > SPU94_STATE_SIZE_MAX:
    sys.exit(f"FAIL: CANARY_OFFSET + sizeof(uint32) overflows state buffer.")
```

## Info

### IN-01: Duplicated err/overflow-tap epilogue across three apply functions

**File:** `src/spu94/spu94_fir.c:143-162, 234-241, 255-262`
**Issue:** The identical post-shift block (compute shifted, err_aggregate, overflow magnitude, add to tap, return sat_s16) is copy-pasted across `fir_folded_apply`, `fir_interp_phase0_apply`, and `fir_interp_phase1_apply`. Three copies of the same 10 lines is a maintenance hazard: a future change to the D-05/D-06 tap math (e.g., adding saturation counting, switching to strict per-multiply err under cascade-clamp) must be made in three places or the stages silently diverge.

**Fix:** Extract into a static inline helper:
```c
static inline int16_t finalize_fir_stage(int32_t acc,
                                         int32_t *err_acc_inout,
                                         int32_t *overflow_acc_inout) {
    int32_t shifted = acc >> 15;
    *err_acc_inout += acc - ((int32_t)shifted << 15);
    int32_t mag = 0;
    if      (shifted > INT16_MAX) mag = shifted - INT16_MAX;
    else if (shifted < INT16_MIN) mag = INT16_MIN - shifted;
    *overflow_acc_inout += mag;
    return sat_s16(shifted);
}
```
Then each apply function ends with `return finalize_fir_stage(acc, err_acc_inout, overflow_acc_inout);`. Pitfall-8 summation-order invariant is preserved because the extraction is purely the post-summation epilogue.

### IN-02: Inconsistent NULL-state output contract between decimate and chain_step

**File:** `src/spu94/spu94_fir.c:175-178` vs `src/spu94/spu94_io_chain.c:78-82, 90-94`
**Issue:** `spu94_fir_chain_step` on NULL state zeros `*l_out_44k1` and `*r_out_44k1` (if non-NULL) before returning. `spu94_fir_decimate` on NULL state sets `*output_valid = 0` (if non-NULL) but leaves `*output_l` / `*output_r` unmodified. The semantics are internally consistent with each function's docstring, but callers migrating between the two APIs could miss the difference — particularly since every other test file initializes output locals to 0 before calling, masking the ambiguity.

**Fix:** Either (a) zero all output parameters on NULL-state in both functions, or (b) add an explicit comment in the public/internal headers documenting the asymmetry. Option (a) is a strictly-additive behavior change; option (b) is zero-cost doc alignment. I'd take option (a) for uniformity:
```c
void spu94_fir_decimate(...) {
    if (state == (spu94_state *)0) {
        if (output_l)     *output_l     = 0;
        if (output_r)     *output_r     = 0;
        if (output_valid) *output_valid = 0;
        return;
    }
    ...
}
```

### IN-03: Stale asm literal in comment (`mov eax, 38`)

**File:** `src/spu94/spu94_io_chain.c:101`
**Issue:** Comment says "LTO makes consumer call sites emit a constant return (e.g., `mov eax, 38; ret`)". The constant is now 58 after the Plan 03 correction documented in `spu94.h` (line 200 comment explicitly calls out the 38u -> 58u fix). The surrounding text ("Value from 04-RESEARCH section Latency") also obliquely refers to the stale value.

**Fix:**
```c
/* D-09: total round-trip FIR group delay at 44.1 kHz reference rate.
 * Value pinned by SPU94_LATENCY_SAMPLES in spu94.h (58u). LTO makes
 * consumer call sites emit a constant return (e.g., mov eax, 58; ret). */
```

### IN-04: SPU94_STATE_SIZE_MAX duplicated between C header and Python

**File:** `tests/python/fuzz_fir.py:56-62` vs `include/spu94/spu94.h:80-85`
**Issue:** The Python harness hand-syncs `SPU94_STATE_SIZE_MAX = 16384`, `SPU94_STATE_ALIGN_MAX = 16`, and `SPU94_LATENCY_SAMPLES_EXPECTED = 58`. These duplicate the C header definitions. The harness already calls `lib.spu94_get_latency_samples()` and cross-checks it, but for the size/align it accepts the hand-synced value blindly. A header change that bumps `SPU94_STATE_SIZE_MAX` to accommodate a growing struct would require a corresponding edit here — silent drift risk.

**Fix:** Nothing urgent for Phase 4 (Phase 6 will ship Python bindings with proper header introspection). Lightweight mitigation in the meantime: pass the C-side value through the `spu94_state_size()` accessor and assert `state_size <= SPU94_STATE_SIZE_MAX`:
```python
ALLOC_SIZE = lib.spu94_state_size()
if ALLOC_SIZE > SPU94_STATE_SIZE_MAX:
    sys.exit("header drift: spu94_state_size() > SPU94_STATE_SIZE_MAX")
```
This naturally pairs with WR-02's fix.

### IN-05: Fixture generator has O(K*N) sweep for Q15 attenuation fit

**File:** `tests/python/derive_fir_reference.py:353-363`
**Issue:** `cmd_dump_band_limited_fixture` fits ROUND_TRIP_ATTENUATION_Q15 by brute-forcing all 24577 candidate values in `[0x2000, 0x8001)`, computing max abs residual over ~88000 samples each. Pure numpy vectorized ops make this fast (sub-second on modern hardware), but the approach is wasteful — the optimal Q15 is a scalar minimization problem with a unique global optimum.

Since this runs at fixture-generation time only (not in any production or CI hot path) and the test is already offline, this is INFO-only. Flagging for future cleanup if the fixture ever grows.

**Fix (optional):** Compute the LSQ-optimal scale directly:
```python
# Numerator = sum(x * y), denominator = sum(x * x). Clamp to Q15 range.
num = int(np.sum(x_stable.astype(np.int64) * y_stable.astype(np.int64)))
den = int(np.sum(x_stable.astype(np.int64) * x_stable.astype(np.int64)))
q15_est = int(round(num * (1 << 15) / den))
q15 = max(0x2000, min(0x8000, q15_est))
# Then single-pass residual measurement at q15 only.
```

### IN-06: `test_overflow_adversarial_negative_no_ub` has ordering-sensitive assertion

**File:** `tests/unit/fir/test_fir_overflow_proof.c:36-38`
**Issue:** The negative-adversarial assertion computes `-acc` on line 38 (`TEST_ASSERT_TRUE_MESSAGE(-acc >= 0x5CD2632E, ...)`). If `acc == INT32_MIN`, `-acc` is UB. Line 37 asserts `acc > INT32_MIN` first, so under Unity's fail-fast semantics a real INT32_MIN would abort before line 38. But Unity's `TEST_ASSERT_*` macros set a failure flag and continue within the test function (they do not immediately return) — depending on the Unity config, line 38 may still execute after line 37 fails. Empirically, the achievable bound is 0x5CD2632E (well within int32), so acc never equals INT32_MIN and the UB is unreachable. The **correctness of the test is not at risk** because the D-02 proof rules out acc hitting INT32_MIN; this finding is about defensive coding against a future refactor that broadens the adversarial pattern.

**Fix:** Use the safer magnitude spelling:
```c
uint32_t abs_acc = (acc < 0) ? (uint32_t)(-(int64_t)acc) : (uint32_t)acc;
TEST_ASSERT_TRUE_MESSAGE(abs_acc >= 0x5CD2632Eu,
    "magnitude at or above achievable int16 bound");
```
Or cast acc to int64 before negating: `-(int64_t)acc`.

---

_Reviewed: 2026-04-20_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
