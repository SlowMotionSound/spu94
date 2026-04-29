/* tests/unit/dac_fir/test_dac_fir_impulse.c -- Phase 6 Plan 01 Task 3
 *
 * Impulse response sanity checks for the three-stage cascade.
 *
 * Feed a single impulse (INT16_MAX at sample 0, then zeros) into
 * spu94_dac_fir_step. Collect 128 output samples and verify:
 *   1. Output is not all zeros (filter produces a response).
 *   2. Output eventually returns to zero (FIR is finite -- after
 *      max(55, 11, 7) = 55 samples from last non-zero stage 1 output,
 *      plus cascading through stages 2 and 3, the total response
 *      length is at most 55 + 11 + 7 - 2 = 71 samples).
 *   3. The first non-zero output appears within the first few samples
 *      (the impulse must propagate through stage delay lines before
 *      reaching the center taps -- first non-zero at sample 2).
 */
#include "unity.h"
#include <spu94/spu94_dac_fir.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

#define NUM_SAMPLES 128

static void test_impulse_not_all_zero(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step(&state, 0);
    }

    /* At least one output sample must be non-zero. */
    int any_nonzero = 0;
    for (int n = 0; n < NUM_SAMPLES; ++n) {
        if (output[n] != 0) { any_nonzero = 1; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_nonzero,
        "Impulse response must produce at least one non-zero sample");
}

static void test_impulse_returns_to_zero(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step(&state, 0);
    }

    /* After sample 73 (55+11+7), all outputs should be zero.
     * Use a generous margin of 80 to account for cascade interaction. */
    for (int n = 80; n < NUM_SAMPLES; ++n) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, output[n],
            "FIR impulse response must return to zero (finite filter)");
    }
}

static void test_impulse_early_response(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step(&state, 0);
    }

    /* The impulse must propagate through the cascade delay lines.
     * Stage 1 has a non-zero coefficient at index 0 (the newest position),
     * which cascades through stages 2 and 3. The first non-zero output
     * appears within the first few samples (measured: sample 2).
     * Verify a non-zero sample appears within the first 10 samples. */
    int any_early_nonzero = 0;
    for (int n = 0; n < 10; ++n) {
        if (output[n] != 0) { any_early_nonzero = 1; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_early_nonzero,
        "Impulse response must produce non-zero output within first 10 samples");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_impulse_not_all_zero);
    RUN_TEST(test_impulse_returns_to_zero);
    RUN_TEST(test_impulse_early_response);
    return UNITY_END();
}
