/* tests/unit/dac_fir/test_dac_fir_overflow_proof.c -- Phase 6 Plan 01 Task 3
 *
 * Accumulator width proof: empirical validation that int32 holds the
 * worst-case adversarial sum without overflow for all three DAC FIR stages.
 *
 * Uses the test-visible wrapper spu94_dac_fir_test_stage_apply() to
 * exercise each stage independently. The delay line is constructed with
 * adversarial values: each sample matches the coefficient sign to maximize
 * the accumulator magnitude.
 *
 * Delay-line convention: circular buffer. With idx=0, tap k reads
 * delay[(ntaps - 1 - k) % ntaps], so delay[ntaps-1-k] holds the value
 * for logical tap k.
 *
 * Achievable bounds (int16 input only, folded form):
 *   Stage 1: acc = 0x71864EB2 (1,904,627,378)
 *   Stage 2: acc = 0x4FA8704A (1,336,438,858)
 *   Stage 3: acc = 0x48C7773E (1,221,031,742)
 *
 * All safely below INT32_MAX = 0x7FFFFFFF = 2,147,483,647.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_dac_fir_internal.h"
#include <stdint.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: fill a delay line with adversarial values for a given stage.
 * For positive adversarial: if coef[k] >= 0, sample at tap k = INT16_MAX;
 * else sample at tap k = INT16_MIN.
 * With idx=0, tap k reads delay[ntaps-1-k]. */
static void fill_adversarial_positive(int16_t *delay, const int16_t *coef,
                                      unsigned ntaps) {
    for (unsigned k = 0; k < ntaps; ++k) {
        delay[ntaps - 1 - k] = (coef[k] >= 0) ? INT16_MAX : INT16_MIN;
    }
}

static void fill_adversarial_negative(int16_t *delay, const int16_t *coef,
                                      unsigned ntaps) {
    for (unsigned k = 0; k < ntaps; ++k) {
        delay[ntaps - 1 - k] = (coef[k] >= 0) ? INT16_MIN : INT16_MAX;
    }
}

/* ---- Stage 1 ---- */

static void test_stage1_adversarial_positive(void) {
    int16_t delay[DAC_FIR_STAGE1_NTAPS];
    fill_adversarial_positive(delay, dac_interp_stage1, DAC_FIR_STAGE1_NTAPS);
    int16_t out = spu94_dac_fir_test_stage_apply(1, delay, 0);
    /* Output must saturate to INT16_MAX (accumulator >> 15 exceeds int16 range) */
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out);
}

static void test_stage1_adversarial_negative(void) {
    int16_t delay[DAC_FIR_STAGE1_NTAPS];
    fill_adversarial_negative(delay, dac_interp_stage1, DAC_FIR_STAGE1_NTAPS);
    int16_t out = spu94_dac_fir_test_stage_apply(1, delay, 0);
    /* Output must saturate to INT16_MIN */
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, out);
}

/* ---- Stage 2 ---- */

static void test_stage2_adversarial_positive(void) {
    int16_t delay[DAC_FIR_STAGE2_NTAPS];
    fill_adversarial_positive(delay, dac_interp_stage2, DAC_FIR_STAGE2_NTAPS);
    int16_t out = spu94_dac_fir_test_stage_apply(2, delay, 0);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out);
}

/* ---- Stage 3 ---- */

static void test_stage3_adversarial_positive(void) {
    int16_t delay[DAC_FIR_STAGE3_NTAPS];
    fill_adversarial_positive(delay, dac_interp_stage3, DAC_FIR_STAGE3_NTAPS);
    int16_t out = spu94_dac_fir_test_stage_apply(3, delay, 0);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out);
}

/* ---- All stages: no UBSan overflow ---- */

static void test_all_stages_no_ub(void) {
    /* If UBSan is enabled in the CI build, any signed overflow in the
     * accumulator would trap. This test exercises all three stages with
     * adversarial input to confirm no trap fires. The sat_s16 output
     * confirms the result is correctly clamped. */
    int16_t d1[DAC_FIR_STAGE1_NTAPS];
    int16_t d2[DAC_FIR_STAGE2_NTAPS];
    int16_t d3[DAC_FIR_STAGE3_NTAPS];

    fill_adversarial_positive(d1, dac_interp_stage1, DAC_FIR_STAGE1_NTAPS);
    fill_adversarial_positive(d2, dac_interp_stage2, DAC_FIR_STAGE2_NTAPS);
    fill_adversarial_positive(d3, dac_interp_stage3, DAC_FIR_STAGE3_NTAPS);

    int16_t o1 = spu94_dac_fir_test_stage_apply(1, d1, 0);
    int16_t o2 = spu94_dac_fir_test_stage_apply(2, d2, 0);
    int16_t o3 = spu94_dac_fir_test_stage_apply(3, d3, 0);

    /* All saturate to INT16_MAX */
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, o1);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, o2);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, o3);

    /* Negative adversarial */
    fill_adversarial_negative(d1, dac_interp_stage1, DAC_FIR_STAGE1_NTAPS);
    fill_adversarial_negative(d2, dac_interp_stage2, DAC_FIR_STAGE2_NTAPS);
    fill_adversarial_negative(d3, dac_interp_stage3, DAC_FIR_STAGE3_NTAPS);

    o1 = spu94_dac_fir_test_stage_apply(1, d1, 0);
    o2 = spu94_dac_fir_test_stage_apply(2, d2, 0);
    o3 = spu94_dac_fir_test_stage_apply(3, d3, 0);

    TEST_ASSERT_EQUAL_INT16(INT16_MIN, o1);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, o2);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, o3);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stage1_adversarial_positive);
    RUN_TEST(test_stage1_adversarial_negative);
    RUN_TEST(test_stage2_adversarial_positive);
    RUN_TEST(test_stage3_adversarial_positive);
    RUN_TEST(test_all_stages_no_ub);
    return UNITY_END();
}
