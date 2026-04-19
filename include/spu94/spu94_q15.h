#ifndef SPU94_Q15_H
#define SPU94_Q15_H

#include <stdint.h>
#include <limits.h>

/* C++ consumers (API-07): C uses the _Static_assert keyword (C11+); C++ uses
 * static_assert (C++11+). Provide a project-prefixed macro that expands to
 * the right form in each language so this header compiles cleanly through an
 * extern "C" C++ consumer without forcing a C-style #include of <assert.h>.
 *
 * Project-prefixed (SPU94_*) instead of redefining the C reserved-identifier
 * `_Static_assert`: names beginning with an underscore + uppercase letter are
 * reserved to the implementation in both C and C++ (C17 §7.1.3, C++17
 * [reserved.names]). A pedantic standard library that uses `_Static_assert`
 * as an internal macro could clash with a header-level redefinition. The
 * SPU94_STATIC_ASSERT shim avoids that risk while keeping every use site
 * portable across C and C++. */
#ifdef __cplusplus
#  define SPU94_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define SPU94_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

/* SPU-94 Q15 fixed-point helpers.
 * PUBLIC API (D-08): consumers may use these directly.
 * Header-only, static inline (D-05, D-07): no separate .c file, no
 * always_inline, no portability macro wrappers in Phase 1.
 *
 * Rounding policy: arithmetic shift right (ASR), round toward -infinity
 * on negative intermediates. See docs/DECISIONS.md ADR-0001 (Plan 03).
 *
 * Saturation policy: int16 range [INT16_MIN, INT16_MAX]. The INT16_MIN *
 * INT16_MIN edge case saturates to INT16_MAX (the mathematically-correct
 * +2^15 does not fit in int16_t).
 *
 * Compile-time assumption: the target compiler emits arithmetic right
 * shift for signed negative operands. C17 §6.5.7/5 leaves this
 * implementation-defined; every mainstream compiler on two's-complement
 * hardware (gcc, clang, arm-none-eabi-gcc per ADR-0001 target list)
 * emits ASR. If this assumption fails, the SPU94_STATIC_ASSERT below fires
 * at compile time.
 */

/* Guard per ADR-0001 Consequences: confirm ASR semantics at compile time. */
SPU94_STATIC_ASSERT((((int16_t)-1) >> 1) == -1,
    "SPU-94 assumes arithmetic right shift (ASR) for signed negative shifts. "
    "Target compiler does not satisfy this; see DECISIONS.md ADR-0001.");

/* Saturate a 32-bit signed value to int16 range. */
static inline int16_t sat_s16(int32_t x) {
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}

/* D-18: Error-observation tap for the Error Accumulator use case
 * (see .planning/notes/2026-04-19-error-accumulator-concept.md). ADR-0004
 * documents this as an intentional public seam, not accidental API surface.
 *
 * Same math as q15_mul_truncate (ADR-0001 semantics: ASR >>15, sat_s16):
 *   full_product = (int32_t)a * (int32_t)b;
 *   shifted      = full_product >> 15;   // ASR, verified by SPU94_STATIC_ASSERT
 *   result       = sat_s16(shifted);
 *
 * Additionally, when err_out != NULL, writes the discarded truncation
 * remainder:
 *   *err_out = (int16_t)(full_product - ((int32_t)shifted << 15));
 *
 * The remainder is the bits the >>15 shift threw away, in the range
 * [0, 32767] for non-negative products and [-32768, -1] for negative
 * products under ASR semantics. It always fits in int16_t.
 *
 * Saturation: on the INT16_MIN*INT16_MIN edge case, `result` is INT16_MAX
 * (ADR-0001). The remainder written to *err_out is the PRE-SATURATION value
 * (the truncation remainder from the >>15 shift, not the additional
 * saturation discard). For INT16_MIN^2 specifically, remainder = 0 because
 * the product (+2^30) is exactly divisible by 2^15. Callers that care about
 * the saturation discard can infer it from `result == INT16_MAX && shifted
 * > INT16_MAX` — the discard equals shifted - INT16_MAX.
 *
 * err_out == NULL is permitted; when NULL, the function is indistinguishable
 * from q15_mul_truncate. */
static inline int16_t q15_mul_truncate_with_err(int16_t a, int16_t b, int16_t *err_out) {
    int32_t product = (int32_t)a * (int32_t)b;
    int32_t shifted = product >> 15;  /* ASR, per SPU94_STATIC_ASSERT above */
    if (err_out != (int16_t *)0) {
        /* Remainder = product - (shifted << 15). Always fits in int16_t
         * because `shifted` captures all bits except the low 15. */
        int32_t remainder = product - (shifted << 15);
        *err_out = (int16_t)remainder;
    }
    return sat_s16(shifted);
}

/* Signed Q15 multiply, ASR-direction truncation, saturated to int16.
 *
 * Contract:
 *   result = sat_s16( ((int32_t)a * (int32_t)b) >> 15 )
 *
 * Edge case: INT16_MIN * INT16_MIN produces intermediate +2^30; >> 15 = +2^15;
 * +2^15 does not fit in int16_t, so sat_s16 clamps to INT16_MAX.
 *
 * Implemented as a thin wrapper around q15_mul_truncate_with_err passing
 * err_out = NULL (Plan 02 / ADR-0004). Bit-identical to the Phase 1 version
 * by construction; the Phase 1 q15 reference table continues to pass.
 *
 * See include/spu94/spu94_q15.h header block for full policy + ADR-0001 reference. */
static inline int16_t q15_mul_truncate(int16_t a, int16_t b) {
    return q15_mul_truncate_with_err(a, b, (int16_t *)0);
}

/* Saturating signed add in int16 range.
 * Widens to int32_t, adds, saturates. No UB. */
static inline int16_t q15_add_sat(int16_t a, int16_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    return sat_s16(sum);
}

#endif /* SPU94_Q15_H */
