#ifndef SPU94_Q15_H
#define SPU94_Q15_H

#include <stdint.h>
#include <limits.h>

/* C++ consumers (API-07): C uses the _Static_assert keyword (C11+); C++ uses
 * static_assert (C++11+). Alias them so this header compiles cleanly through
 * an extern "C" C++ consumer without forcing a C-style #include of
 * <assert.h>. The aliasing is a no-op in C, where _Static_assert is already
 * the keyword. */
#ifdef __cplusplus
#  define _Static_assert(cond, msg) static_assert(cond, msg)
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
 * emits ASR. If this assumption fails, the _Static_assert below fires
 * at compile time.
 */

/* Guard per ADR-0001 Consequences: confirm ASR semantics at compile time. */
_Static_assert((((int16_t)-1) >> 1) == -1,
    "SPU-94 assumes arithmetic right shift (ASR) for signed negative shifts. "
    "Target compiler does not satisfy this; see DECISIONS.md ADR-0001.");

/* Saturate a 32-bit signed value to int16 range. */
static inline int16_t sat_s16(int32_t x) {
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}

/* Signed Q15 multiply, ASR-direction truncation, saturated to int16.
 *
 * Contract:
 *   result = sat_s16( ((int32_t)a * (int32_t)b) >> 15 )
 *
 * Edge case: INT16_MIN * INT16_MIN produces intermediate +2^30; >> 15 = +2^15;
 * +2^15 does not fit in int16_t, so sat_s16 clamps to INT16_MAX.
 *
 * See include/spu94/spu94_q15.h header block for full policy + ADR-0001 reference. */
static inline int16_t q15_mul_truncate(int16_t a, int16_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    int32_t shifted = product >> 15; /* ASR, per _Static_assert above */
    return sat_s16(shifted);
}

/* Saturating signed add in int16 range.
 * Widens to int32_t, adds, saturates. No UB. */
static inline int16_t q15_add_sat(int16_t a, int16_t b) {
    int32_t sum = (int32_t)a + (int32_t)b;
    return sat_s16(sum);
}

#endif /* SPU94_Q15_H */
