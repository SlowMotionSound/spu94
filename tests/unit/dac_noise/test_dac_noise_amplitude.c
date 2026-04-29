/* tests/unit/dac_noise/test_dac_noise_amplitude.c -- Phase 6 Plan 02 Task 2
 *
 * Verifies noise RMS is near the -90 dB target per D-04.
 * Generous +/-10 dB window per D-04/D-06 -- the shaping character matters
 * more than the absolute amplitude, and the exact level is tunable via
 * DAC_NOISE_SHIFT.
 *
 * Float math (log10, sqrt) is acceptable in test files -- the float-free
 * gate applies to libspu94, not test executables.
 */
#include "unity.h"
#include <spu94/spu94_dac_noise.h>
#include <math.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* Generate 1 second of noise (44100 samples), compute RMS in dB relative
 * to full scale (32768). Assert within -100 to -80 dB window. */
static void test_noise_rms_near_minus_90dB(void) {
    #define N_SAMPLES  44100
    #define FULL_SCALE 32768.0

    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st, 0xACE1u);

    int64_t sum_sq = 0;
    for (int i = 0; i < N_SAMPLES; i++) {
        int16_t out = spu94_dac_noise_step(&st);
        sum_sq += (int64_t)out * (int64_t)out;
    }

    double rms = sqrt((double)sum_sq / (double)N_SAMPLES);
    double rms_dB = 20.0 * log10(rms / FULL_SCALE);

    char msg[256];
    snprintf(msg, sizeof(msg),
        "Noise RMS = %.1f dB (expected ~-90 dB, window -100 to -80 dB). "
        "RMS = %.4f LSBs",
        rms_dB, rms);

    /* -100 to -80 dB window (generous per D-04/D-06).
     * Use TEST_ASSERT_TRUE because Unity double precision is not enabled. */
    TEST_ASSERT_TRUE_MESSAGE(rms_dB >= -100.0, msg);
    TEST_ASSERT_TRUE_MESSAGE(rms_dB <= -80.0, msg);

    #undef N_SAMPLES
    #undef FULL_SCALE
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_noise_rms_near_minus_90dB);
    return UNITY_END();
}
