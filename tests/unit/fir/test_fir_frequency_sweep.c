/* tests/unit/fir/test_fir_frequency_sweep.c -- Phase 4 Plan 04 Task 1
 *
 * SC-1 frequency-domain axis. Drive spu94_fir_decimate with a Python-
 * derived log sweep 20 Hz -> 22 kHz at 44.1 kHz (44100 samples, Hann-
 * windowed) and assert every retained 22.05 kHz output sample matches
 * the Python reference bit-exactly.
 *
 * The frequency-domain *meaning* -- half-band lowpass shape with a -3 dB
 * point around 11.025 kHz and deep stopband above ~13 kHz -- is carried
 * by the analytic-DFT FREQ_SWEEP_REFERENCE table in the generated
 * header. This C test does NOT compute any FFT: grep-guard forbids
 * float/double in core sources, and the Python reference is already the
 * oracle. The analytic table is checked for basic sanity (DC bin near 0
 * dB, 20 kHz bin deep in the stopband) as a second, easier-to-read
 * cross-check that documents what "passing" means.
 *
 * Fixture: tests/unit/fir/test_fir_frequency_sweep_reference.h
 *   generated via
 *     python3 tests/python/derive_fir_reference.py \
 *         --dump-sweep-reference tests/unit/fir/test_fir_frequency_sweep_reference.h
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include "test_fir_frequency_sweep_reference.h"

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static int16_t g_actual[SWEEP_EXPECTED_22K05_LEN];

static spu94_state *fresh_state_local(void) {
    spu94_state *s = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

static void test_sweep_decimator_matches_python_reference(void) {
    spu94_state *s = fresh_state_local();
    size_t retained = 0;
    for (int n = 0; n < SWEEP_INPUT_44K1_LEN; ++n) {
        int16_t out_l = 0, out_r = 0;
        int valid = 0;
        spu94_fir_decimate(s, SWEEP_INPUT_44K1[n], SWEEP_INPUT_44K1[n],
                           &out_l, &out_r, &valid);
        if (valid) {
            /* Mono input -> L and R must agree every retained sample. */
            TEST_ASSERT_EQUAL_INT16_MESSAGE(out_l, out_r,
                "decimator L/R mirror assumption broken for mono input");
            TEST_ASSERT_TRUE_MESSAGE(
                retained < (size_t)SWEEP_EXPECTED_22K05_LEN,
                "retained-output count exceeded expected length");
            g_actual[retained] = out_l;
            ++retained;
        }
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        (uint32_t)SWEEP_EXPECTED_22K05_LEN, (uint32_t)retained,
        "decimator retained-phase count mismatch");
    /* Full-length bit-exact match against Python reference (SC-1). */
    TEST_ASSERT_EQUAL_INT16_ARRAY(SWEEP_EXPECTED_22K05, g_actual,
                                  SWEEP_EXPECTED_22K05_LEN);
}

/* Sanity-check the analytic-DFT reference table that carries the
 * frequency-domain interpretation of the sweep test. This is NOT a
 * measurement of the C implementation -- it's a cross-check that the
 * Python generator produced a table whose shape matches a half-band
 * filter (DC near 0 dB; 20 kHz deep in stopband). If the generator ever
 * regresses and emits garbage, this test catches it independently of
 * the bit-exact sweep assertion above. */
static void test_sweep_reference_table_shape(void) {
    TEST_ASSERT_EQUAL_INT(7, FREQ_SWEEP_REFERENCE_LEN);
    TEST_ASSERT_EQUAL_INT(0, FREQ_SWEEP_REFERENCE[0].freq_hz);
    /* DC bin should be within +/- 3 dB of 0 dB (half-band sum(h) = 0x7FFE
     * is just shy of 0x8000, so DC gain is ~-0.00017 dB -- trivially
     * inside 768 Q8 = 3 dB). */
    TEST_ASSERT_INT32_WITHIN(768, 0, FREQ_SWEEP_REFERENCE[0].expected_db_q8);

    TEST_ASSERT_EQUAL_INT(20000, FREQ_SWEEP_REFERENCE[6].freq_hz);
    /* 20 kHz must be deep stopband. -40 dB = -10240 Q8. */
    TEST_ASSERT_LESS_THAN_INT32(-10240, FREQ_SWEEP_REFERENCE[6].expected_db_q8);

    /* All tolerances pinned at 768 Q8 (+/-3 dB) per 04-RESEARCH 9.4. */
    for (int i = 0; i < FREQ_SWEEP_REFERENCE_LEN; ++i) {
        TEST_ASSERT_EQUAL_INT32(768, FREQ_SWEEP_REFERENCE[i].tolerance_db_q8);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sweep_decimator_matches_python_reference);
    RUN_TEST(test_sweep_reference_table_shape);
    return UNITY_END();
}
