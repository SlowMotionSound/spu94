/* tests/unit/dac_fir/test_dac_fir_dc_gain.c -- Phase 6 Plan 01 Task 3
 *
 * DC gain verification through the three-stage cascade.
 *
 * Feed 256 samples of DC = 16384 (0x4000, half scale) into
 * spu94_dac_fir_step. After the delay lines fill (worst case: sample 73),
 * the output reaches steady state. The steady-state output represents
 * the cascaded DC gain of all three stages.
 *
 * Expected: 16333 (computed by Python simulation).
 * Tolerance: +/-2 LSB for Q15 truncation accumulation across stages.
 */
#include "unity.h"
#include <spu94/spu94_dac_fir.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static void test_dc_gain_cascade(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t input = 16384;  /* 0x4000, half scale */
    int16_t output = 0;

    /* Feed 256 DC samples -- more than enough for steady state
     * (55 + 11 + 7 = 73 samples for all delay lines to fill). */
    for (int n = 0; n < 256; ++n) {
        output = spu94_dac_fir_step(&state, input);
    }

    /* Expected cascade DC output: 16333 (Python-verified).
     * Allow +/-2 LSB tolerance. */
    TEST_ASSERT_INT16_WITHIN(2, 16333, output);
}

static void test_dc_gain_negative(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t input = -16384;
    int16_t output = 0;

    for (int n = 0; n < 256; ++n) {
        output = spu94_dac_fir_step(&state, input);
    }

    /* Symmetric: output should be -16333 +/-2 LSB. */
    TEST_ASSERT_INT16_WITHIN(2, -16333, output);
}

static void test_dc_zero_passthrough(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    /* DC = 0 must produce 0 output at steady state. */
    int16_t output = 0;
    for (int n = 0; n < 256; ++n) {
        output = spu94_dac_fir_step(&state, 0);
    }
    TEST_ASSERT_EQUAL_INT16(0, output);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dc_gain_cascade);
    RUN_TEST(test_dc_gain_negative);
    RUN_TEST(test_dc_zero_passthrough);
    return UNITY_END();
}
