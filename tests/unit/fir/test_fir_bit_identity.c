/* tests/unit/fir/test_fir_bit_identity.c -- Phase 4 Plan 02 Task 2
 *
 * D-01 audit-witness: folded == literal under D-03 clamp-once. See
 * 04-RESEARCH section 8 for the algebraic proof; this test is the
 * empirical witness across 10^5 random + 1 adversarial input.
 *
 * Guard: bit-identity only holds under clamp-once. SPU94_FIR_CASCADE_CLAMP
 * must be UNDEFINED when this TU is built.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <stdint.h>

#ifdef SPU94_FIR_CASCADE_CLAMP
#error "test_fir_bit_identity requires SPU94_FIR_CASCADE_CLAMP undefined (D-03 clamp-once regime)."
#endif

void setUp(void) {}
void tearDown(void) {}

/* Deterministic Xorshift64 -- no external RNG dep; seed pinned for
 * reproducibility across CI and local runs. */
static uint64_t g_rng = 0xC0FFEE12345678ABULL;
static int16_t rng_s16(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (int16_t)(g_rng & 0xFFFF);
}

static void test_bit_identity_random(void) {
    for (int iter = 0; iter < 100000; ++iter) {
        int16_t history[39];
        for (int k = 0; k < 39; ++k) history[k] = rng_s16();
        int16_t out_lit = 0, out_fold = 0;
        int32_t acc_lit = 0, acc_fold = 0;
        int32_t err_lit = 0, err_fold = 0;
        spu94_fir_decimate_literal_reference(history, &out_lit,
                                             &acc_lit, &err_lit);
        spu94_fir_folded_reference(history, &out_fold,
                                   &acc_fold, &err_fold);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(acc_lit, acc_fold,
            "acc mismatch in 10^5 random (D-01 audit)");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_lit, out_fold,
            "out mismatch in 10^5 random (D-01 audit)");
        TEST_ASSERT_EQUAL_INT32_MESSAGE(err_lit, err_fold,
            "err mismatch in 10^5 random (D-01 audit)");
    }
}

static void test_bit_identity_adversarial(void) {
    /* 04-RESEARCH section 8 last paragraph: pattern demonstrates folded ==
     * literal under clamp-once, divergence under cascade-clamp. */
    int16_t history[39];
    for (int k = 0; k < 39; ++k) {
        history[k] = (spu94_fir_coef[k] >= 0) ? INT16_MAX : INT16_MIN;
    }
    int16_t out_lit = 0, out_fold = 0;
    int32_t acc_lit = 0, acc_fold = 0;
    int32_t err_lit = 0, err_fold = 0;
    spu94_fir_decimate_literal_reference(history, &out_lit,
                                         &acc_lit, &err_lit);
    spu94_fir_folded_reference(history, &out_fold,
                               &acc_fold, &err_fold);
    TEST_ASSERT_EQUAL_INT32(acc_lit, acc_fold);
    TEST_ASSERT_EQUAL_INT16(out_lit, out_fold);
    TEST_ASSERT_EQUAL_INT32(err_lit, err_fold);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bit_identity_random);
    RUN_TEST(test_bit_identity_adversarial);
    return UNITY_END();
}
