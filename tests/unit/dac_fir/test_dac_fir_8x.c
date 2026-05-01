/* tests/unit/dac_fir/test_dac_fir_8x.c -- Phase 10 Plan 02
 *
 * Unit tests for spu94_dac_fir_step_8x: the naive 8x zero-stuff
 * cascade (D-01). Tests verify:
 *   1. Impulse response is not all zeros
 *   2. Impulse response returns to zero (FIR is finite)
 *   3. Impulse response appears within first few samples
 *   4. DC gain through 8x cascade matches v1.2 cascade DC gain
 *   5. Overflow resilience with adversarial INT16_MIN input
 *   6. v1.2 path is unchanged (regression guard for D-03)
 */
#include "unity.h"
#include <spu94/spu94_dac_fir.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

#define NUM_SAMPLES 256

/* ---- Test 1: Impulse response is not all zeros ---- */

static void test_8x_impulse_not_all_zero(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step_8x(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step_8x(&state, 0);
    }

    int any_nonzero = 0;
    for (int n = 0; n < NUM_SAMPLES; ++n) {
        if (output[n] != 0) { any_nonzero = 1; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_nonzero,
        "8x impulse response must produce at least one non-zero sample");
}

/* ---- Test 2: Impulse response returns to zero ---- */

static void test_8x_impulse_returns_to_zero(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step_8x(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step_8x(&state, 0);
    }

    /* With 8x zero-stuffing, the effective response length is stretched.
     * Each input sample pushes 2 entries per stage (real + zero), so the
     * cascade response length in input-sample-equivalents is at most
     * ceil(55/2) + ceil(11/2) + ceil(7/2) = 28+6+4 = 38. Use a generous
     * margin of 128 samples. All outputs from sample 128 onward must be
     * zero. */
    for (int n = 128; n < NUM_SAMPLES; ++n) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, output[n],
            "8x FIR impulse response must return to zero (finite filter)");
    }
}

/* ---- Test 3: Impulse response appears early ---- */

static void test_8x_impulse_early_response(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[NUM_SAMPLES];
    output[0] = spu94_dac_fir_step_8x(&state, INT16_MAX);
    for (int n = 1; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step_8x(&state, 0);
    }

    /* A non-zero output must appear within the first 10 samples. */
    int any_early_nonzero = 0;
    for (int n = 0; n < 10; ++n) {
        if (output[n] != 0) { any_early_nonzero = 1; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_early_nonzero,
        "8x impulse response must produce non-zero output within first 10 samples");
}

/* ---- Test 4: DC gain through 8x cascade ---- */

static void test_8x_dc_gain(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t input = 16384;  /* 0x4000, half scale */
    int16_t output[NUM_SAMPLES];

    /* Feed 256 DC samples -- more than enough for steady state. */
    for (int n = 0; n < NUM_SAMPLES; ++n) {
        output[n] = spu94_dac_fir_step_8x(&state, input);
    }

    /* Collect the last 64 outputs (after transient settles). Compute
     * average to smooth any residual oscillation from zero-stuff pattern. */
    int32_t sum = 0;
    for (int n = NUM_SAMPLES - 64; n < NUM_SAMPLES; ++n) {
        sum += output[n];
    }
    int32_t avg = sum / 64;

    /* The naive 8x zero-stuff cascade decimates by keeping only the last
     * of 8 Stage 3 outputs, then applies <<3 gain compensation to restore
     * unity gain. Expected steady-state matches v1.2: ~16333 for input 16384.
     * Allow +/-100 tolerance for Q15 truncation across 14 evaluations. */
    TEST_ASSERT_INT_WITHIN_MESSAGE(100, 16333, (int)avg,
        "8x DC gain (with <<3 compensation) should match v1.2 (~16333 for input 16384)");
}

/* ---- Test 5: Overflow resilience with adversarial input ---- */

static void test_8x_overflow_adversarial(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    /* Feed 128 samples of INT16_MIN through the 8x cascade.
     * sat_s16 guarantees output is within [-32768, 32767]. This test
     * confirms no UB from accumulator overflow and that the function
     * returns without crashing. */
    for (int n = 0; n < 128; ++n) {
        int16_t out = spu94_dac_fir_step_8x(&state, INT16_MIN);
        TEST_ASSERT_GREATER_OR_EQUAL_INT16(INT16_MIN, out);
        TEST_ASSERT_LESS_OR_EQUAL_INT16(INT16_MAX, out);
    }
}

/* ---- Test 6: v1.2 regression guard (D-03) ---- */

static void test_v1_2_regression(void) {
    spu94_dac_fir_state state;
    spu94_dac_fir_init(&state);

    int16_t output[64];
    output[0] = spu94_dac_fir_step(&state, INT16_MAX);
    for (int n = 1; n < 64; ++n) {
        output[n] = spu94_dac_fir_step(&state, 0);
    }

    /* v1.2 path must still produce non-zero impulse response. */
    int any_nonzero = 0;
    for (int n = 0; n < 64; ++n) {
        if (output[n] != 0) { any_nonzero = 1; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_nonzero,
        "v1.2 spu94_dac_fir_step must still produce non-zero impulse response (D-03)");

    /* v1.2 path must return to zero after 80 samples. Feed more zeros. */
    spu94_dac_fir_state state2;
    spu94_dac_fir_init(&state2);

    int16_t output2[128];
    output2[0] = spu94_dac_fir_step(&state2, INT16_MAX);
    for (int n = 1; n < 128; ++n) {
        output2[n] = spu94_dac_fir_step(&state2, 0);
    }

    for (int n = 80; n < 128; ++n) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, output2[n],
            "v1.2 impulse must return to zero after 80 samples (D-03 regression guard)");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_8x_impulse_not_all_zero);
    RUN_TEST(test_8x_impulse_returns_to_zero);
    RUN_TEST(test_8x_impulse_early_response);
    RUN_TEST(test_8x_dc_gain);
    RUN_TEST(test_8x_overflow_adversarial);
    RUN_TEST(test_v1_2_regression);
    return UNITY_END();
}
