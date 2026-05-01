/* tests/unit/process/test_process_dac_mode_toggle.c -- Phase 11 Plan 02
 *
 * Integration tests for the A/B mode toggle (dac_true_oversample):
 *   - set/get API + NULL safety
 *   - default value after init (v1.3 ON)
 *   - v1.2 vs v1.3 produce different output
 *   - latency reporting for both modes
 *   - latency unchanged when DAC is off
 *
 * Pattern: follows test_process_dac_toggle_transitions.c (two-state
 * fixture for A/B comparison).
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include "spu94_state_internal.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Shared fixtures (two states for A/B comparison)
 * ----------------------------------------------------------------------- */

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_a[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_a[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state_a = NULL;

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state_b = NULL;

void setUp(void) {
    state_a = spu94_init(state_buf_a, sizeof state_buf_a,
                         work_buf_a, sizeof work_buf_a);
    TEST_ASSERT_NOT_NULL(state_a);
    state_b = spu94_init(state_buf_b, sizeof state_buf_b,
                         work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_b);
}

void tearDown(void) {
    state_a = NULL;
    state_b = NULL;
}

/* -----------------------------------------------------------------------
 * Helper: set mixer faders to unity passthrough.
 * ----------------------------------------------------------------------- */
static void set_unity_passthrough(spu94_state *s) {
    spu94_set_input_gain(s,   0x7FFF);
    spu94_set_dry_fader(s,    0x7FFF);
    spu94_set_reverb_fader(s, 0x7FFF);
    spu94_set_dry_send(s,     0x7FFF);
}

/* -----------------------------------------------------------------------
 * Test 1: dac_true_oversample set/get + NULL safety
 * ----------------------------------------------------------------------- */
static void test_dac_true_oversample_set_get(void) {
    spu94_set_dac_true_oversample(state_a, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_true_oversample(state_a));
    spu94_set_dac_true_oversample(state_a, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_true_oversample(state_a));
    /* NULL safety */
    spu94_set_dac_true_oversample(NULL, 1);  /* must not crash */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_true_oversample(NULL));
}

/* -----------------------------------------------------------------------
 * Test 2: default after init is v1.3 (dac_true_oversample=1)
 * ----------------------------------------------------------------------- */
static void test_dac_true_oversample_default(void) {
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_true_oversample(state_a));
}

/* -----------------------------------------------------------------------
 * Test 3: v1.2 and v1.3 produce different output
 *
 * Two states with identical configuration except dac_true_oversample.
 * Process the same 256-sample deterministic input. Outputs must differ.
 * ----------------------------------------------------------------------- */
static void test_v12_v13_produce_different_output(void) {
    set_unity_passthrough(state_a);
    set_unity_passthrough(state_b);
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_noise_enabled(state_a, 1);
    spu94_set_dac_true_oversample(state_a, 0); /* v1.2 */

    spu94_set_dac_enabled(state_b, 1);
    spu94_set_dac_fir_enabled(state_b, 1);
    spu94_set_dac_noise_enabled(state_b, 1);
    spu94_set_dac_true_oversample(state_b, 1); /* v1.3 */

    #define N 256
    int16_t input[N];
    for (int i = 0; i < N; i++)
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);

    int16_t out_a_l[N], out_a_r[N], out_b_l[N], out_b_r[N];
    spu94_process(state_a, input, input, out_a_l, out_a_r, N);
    spu94_process(state_b, input, input, out_b_l, out_b_r, N);

    int diffs = 0;
    for (int i = 0; i < N; i++) {
        if (out_a_l[i] != out_b_l[i]) diffs++;
    }
    TEST_ASSERT_TRUE_MESSAGE(diffs > 0,
        "v1.2 and v1.3 produced identical output -- mode toggle not working");
    #undef N
}

/* -----------------------------------------------------------------------
 * Test 4: latency v1.3 with DAC FIR on = 73 (58 + 15)
 * ----------------------------------------------------------------------- */
static void test_latency_v13_dac_fir_on(void) {
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_true_oversample(state_a, 1);
    TEST_ASSERT_EQUAL_UINT32(73u, spu94_get_total_latency_samples(state_a));
}

/* -----------------------------------------------------------------------
 * Test 5: latency v1.2 with DAC FIR on = 93 (58 + 35)
 * ----------------------------------------------------------------------- */
static void test_latency_v12_dac_fir_on(void) {
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_true_oversample(state_a, 0);
    TEST_ASSERT_EQUAL_UINT32(93u, spu94_get_total_latency_samples(state_a));
}

/* -----------------------------------------------------------------------
 * Test 6: latency with DAC off is unchanged (58)
 * ----------------------------------------------------------------------- */
static void test_latency_dac_off_unchanged(void) {
    TEST_ASSERT_EQUAL_UINT32(58u, spu94_get_total_latency_samples(state_a));
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dac_true_oversample_set_get);
    RUN_TEST(test_dac_true_oversample_default);
    RUN_TEST(test_v12_v13_produce_different_output);
    RUN_TEST(test_latency_v13_dac_fir_on);
    RUN_TEST(test_latency_v12_dac_fir_on);
    RUN_TEST(test_latency_dac_off_unchanged);
    return UNITY_END();
}
