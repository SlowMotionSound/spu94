/* tests/unit/dac_noise/test_dac_noise_spectral.c -- Phase 6 Plan 02 Task 2
 *
 * Verifies the +12 dB/octave spectral slope characteristic of the
 * 2nd-order highpass noise shaping. Uses the Goertzel algorithm to
 * measure power at two frequencies two octaves apart and asserts the
 * expected ~24 dB difference (with generous tolerance for noise variance).
 *
 * Float math is acceptable in test files -- the float-free gate applies
 * to libspu94, not test executables.
 */
#include "unity.h"
#include <spu94/spu94_dac_noise.h>
#include <math.h>

void setUp(void) {}
void tearDown(void) {}

/* Goertzel algorithm: compute |DFT[k]|^2 for a single bin k over N samples.
 * Returns the magnitude squared at the specified frequency bin. */
static double goertzel_mag_sq(const int16_t *samples, int N, int k) {
    double w = 2.0 * 3.14159265358979323846 * (double)k / (double)N;
    double coeff = 2.0 * cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < N; i++) {
        s0 = (double)samples[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

/* Verify +12 dB/octave spectral slope.
 *
 * Generate N=8192 noise samples, measure power at 2000 Hz and 8000 Hz
 * (two octaves apart at 44100 Hz sample rate). The expected difference
 * is 24 dB (+12 dB/oct * 2 octaves). Allow 18-30 dB window because
 * noise has variance -- the shaped spectrum is statistical, not
 * deterministic.
 *
 * To reduce variance, average over multiple independent runs. */
static void test_spectral_slope_12db_per_octave(void) {
    #define N_SAMPLES   8192
    #define N_RUNS      8
    #define SAMPLE_RATE 44100.0

    /* Frequency bins for Goertzel:
     * bin = round(freq / sample_rate * N) */
    int bin_f1 = (int)(2000.0 / SAMPLE_RATE * N_SAMPLES + 0.5);  /* ~372 */
    int bin_f2 = (int)(8000.0 / SAMPLE_RATE * N_SAMPLES + 0.5);  /* ~1486 */

    double total_power_f1 = 0.0;
    double total_power_f2 = 0.0;

    int16_t samples[N_SAMPLES];

    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st);

    for (int run = 0; run < N_RUNS; run++) {
        for (int i = 0; i < N_SAMPLES; i++) {
            samples[i] = spu94_dac_noise_step(&st);
        }
        total_power_f1 += goertzel_mag_sq(samples, N_SAMPLES, bin_f1);
        total_power_f2 += goertzel_mag_sq(samples, N_SAMPLES, bin_f2);
    }

    double avg_power_f1 = total_power_f1 / N_RUNS;
    double avg_power_f2 = total_power_f2 / N_RUNS;

    double power_f1_dB = 10.0 * log10(avg_power_f1);
    double power_f2_dB = 10.0 * log10(avg_power_f2);
    double slope_dB = power_f2_dB - power_f1_dB;

    char msg[256];
    snprintf(msg, sizeof(msg),
        "Spectral slope across 2 octaves (2kHz->8kHz): %.1f dB "
        "(expected ~24 dB, window 18-30 dB). "
        "power@2kHz=%.1f dB, power@8kHz=%.1f dB",
        slope_dB, power_f1_dB, power_f2_dB);

    /* +12 dB/octave * 2 octaves = 24 dB expected.
     * Window: 18-30 dB (generous for noise variance).
     * Use TEST_ASSERT_TRUE because Unity double precision is not enabled. */
    TEST_ASSERT_TRUE_MESSAGE(slope_dB >= 18.0, msg);
    TEST_ASSERT_TRUE_MESSAGE(slope_dB <= 30.0, msg);

    #undef N_SAMPLES
    #undef N_RUNS
    #undef SAMPLE_RATE
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_spectral_slope_12db_per_octave);
    return UNITY_END();
}
