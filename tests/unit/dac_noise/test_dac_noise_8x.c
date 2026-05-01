/* tests/unit/dac_noise/test_dac_noise_8x.c -- Phase 11 Plan 01 Task 2
 *
 * Verifies noise injection at 352.8kHz through the FIR cascade:
 *   1. In-band RMS within -100 to -80 dB window
 *   2. Output is non-silent (noise is actually injected)
 *   3. Noise injection produces different output from noiseless path
 *
 * Float math (log10, sqrt) is acceptable in test files -- the float-free
 * gate applies to libspu94, not test executables.
 */
#include "unity.h"
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Test 1: Feed silence through spu94_dac_fir_step_8x_with_noise for 44100
 * samples. Measure output RMS. Assert between -100 and -80 dB. */
static void test_noise_8x_rms_in_band(void) {
    spu94_dac_fir_state fir;
    spu94_dac_fir_init(&fir);

    spu94_dac_noise_state noise;
    spu94_dac_noise_init(&noise, 0xACE1u);

    int64_t sum_sq = 0;
    for (int i = 0; i < 44100; i++) {
        int16_t out = spu94_dac_fir_step_8x_with_noise(&fir, &noise, 0);
        sum_sq += (int64_t)out * (int64_t)out;
    }

    double rms = sqrt((double)sum_sq / 44100.0);
    double rms_dB = 20.0 * log10(rms / 32768.0);

    char msg[256];
    snprintf(msg, sizeof(msg),
        "8x noise RMS = %.1f dB (expected ~-90 dB, window -100 to -80 dB). "
        "RMS = %.4f LSBs",
        rms_dB, rms);

    TEST_ASSERT_TRUE_MESSAGE(rms_dB >= -100.0, msg);
    TEST_ASSERT_TRUE_MESSAGE(rms_dB <= -80.0, msg);
}

/* Test 2: Feed silence through the combined function for 256 samples.
 * Assert at least one output is non-zero (noise is being injected). */
static void test_noise_8x_not_silent(void) {
    spu94_dac_fir_state fir;
    spu94_dac_fir_init(&fir);

    spu94_dac_noise_state noise;
    spu94_dac_noise_init(&noise, 0xACE1u);

    int nonzero = 0;
    for (int i = 0; i < 256; i++) {
        int16_t out = spu94_dac_fir_step_8x_with_noise(&fir, &noise, 0);
        if (out != 0) nonzero++;
    }

    TEST_ASSERT_TRUE_MESSAGE(nonzero > 0,
        "Expected at least one non-zero output from noise injection");
}

/* Test 3: Feed identical input through both noiseless and noisy paths.
 * Assert outputs differ (proves noise injection is active). */
static void test_noise_8x_differs_from_noiseless(void) {
    spu94_dac_fir_state fir_clean;
    spu94_dac_fir_init(&fir_clean);

    spu94_dac_fir_state fir_noisy;
    spu94_dac_fir_init(&fir_noisy);

    spu94_dac_noise_state noise;
    spu94_dac_noise_init(&noise, 0xACE1u);

    int differ_count = 0;
    for (int i = 0; i < 64; i++) {
        int16_t out_clean = spu94_dac_fir_step_8x(&fir_clean, 1000);
        int16_t out_noisy = spu94_dac_fir_step_8x_with_noise(&fir_noisy, &noise, 1000);
        if (out_clean != out_noisy) differ_count++;
    }

    char msg[128];
    snprintf(msg, sizeof(msg),
        "Expected at least 1 differing sample, got %d", differ_count);
    TEST_ASSERT_TRUE_MESSAGE(differ_count > 0, msg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_noise_8x_rms_in_band);
    RUN_TEST(test_noise_8x_not_silent);
    RUN_TEST(test_noise_8x_differs_from_noiseless);
    return UNITY_END();
}
