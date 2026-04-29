/* tests/unit/process/test_process_latency_comp.c -- Phase 7 Plan 03
 *
 * Integration tests for the 28-sample latency compensation delay buffer.
 * Covers: default-on (D-07), set/get, null safety, no-op when ADPCM off
 * (Pitfall 5), 28-sample impulse delay, bypass when disabled, state
 * reset on disable.
 *
 * Pattern: same setUp/tearDown as test_process_adpcm.c. Includes
 * spu94_state_internal.h for direct struct inspection.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include "spu94_state_internal.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * Shared fixtures
 * ----------------------------------------------------------------------- */

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state = NULL;

/* Second state for comparison tests */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[SPU94_WORK_BUF_MAX_BYTES];

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
}

void tearDown(void) {
    state = NULL;
}

/* -----------------------------------------------------------------------
 * Helper: set mixer faders to unity passthrough for process-level tests.
 * Without this, input_gain=0 means spu94_process outputs silence.
 * ----------------------------------------------------------------------- */
static void set_unity_passthrough(spu94_state *s) {
    spu94_set_input_gain(s,   0x7FFF);
    spu94_set_dry_fader(s,    0x7FFF);
    spu94_set_reverb_fader(s, 0x7FFF);
    spu94_set_dry_send(s,     0x7FFF);
}

/* -----------------------------------------------------------------------
 * Test 1: Latency compensation defaults to ON (D-07)
 * ----------------------------------------------------------------------- */
static void test_latency_comp_default_on(void) {
    TEST_ASSERT_EQUAL_INT(1, spu94_get_latency_comp(state));
}

/* -----------------------------------------------------------------------
 * Test 2: Set/get round-trip
 * ----------------------------------------------------------------------- */
static void test_latency_comp_set_get(void) {
    spu94_set_latency_comp(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_latency_comp(state));

    spu94_set_latency_comp(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_latency_comp(state));

    /* Non-zero normalizes to 1 */
    spu94_set_latency_comp(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_latency_comp(state));
}

/* -----------------------------------------------------------------------
 * Test 3: NULL safety
 * ----------------------------------------------------------------------- */
static void test_latency_comp_null_safety(void) {
    /* Setter: no crash */
    spu94_set_latency_comp(NULL, 1);

    /* Getter: returns 0 */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_latency_comp(NULL));
}

/* -----------------------------------------------------------------------
 * Test 4: Latency comp does nothing when ADPCM is off (Pitfall 5)
 *
 * With ADPCM disabled, latency_comp=1 vs latency_comp=0 should produce
 * identical output -- there is no ADPCM latency to compensate.
 * ----------------------------------------------------------------------- */
static void test_latency_comp_only_when_adpcm_enabled(void) {
    const uint32_t N = 128;
    int16_t input[128], out_comp_l[128], out_comp_r[128];
    for (uint32_t i = 0; i < N; i++) input[i] = 10000;

    /* State A: ADPCM off, latency_comp=1 (default) */
    set_unity_passthrough(state);
    spu94_set_latency_comp(state, 1);
    spu94_process(state, input, input, out_comp_l, out_comp_r, N);

    /* State B: ADPCM off, latency_comp=0 */
    spu94_state *state_b = spu94_init(state_buf_b, sizeof state_buf_b,
                                       work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_b);
    set_unity_passthrough(state_b);
    spu94_set_latency_comp(state_b, 0);

    int16_t out_nocomp_l[128], out_nocomp_r[128];
    spu94_process(state_b, input, input, out_nocomp_l, out_nocomp_r, N);

    /* Output should be identical (latency comp is a no-op when ADPCM is off) */
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_comp_l, out_nocomp_l, N);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_comp_r, out_nocomp_r, N);
}

/* -----------------------------------------------------------------------
 * Test 5: Latency comp delays dry bus by 28 samples
 *
 * Enable ADPCM + latency_comp=1. Set dry-only routing (dry_fader=max,
 * reverb_fader=0, patina_fader=0). Feed an impulse at sample 0.
 * The dry bus output should show the impulse delayed by 28 samples:
 * output[0..27] = 0, output[28] != 0.
 * ----------------------------------------------------------------------- */
static void test_latency_comp_delays_dry_28_samples(void) {
    spu94_set_input_gain(state, 0x7FFF);
    spu94_set_dry_fader(state, 0x7FFF);
    spu94_set_dry_send(state, 0);
    spu94_set_reverb_fader(state, 0);
    spu94_set_patina_fader(state, 0);
    spu94_set_patina_send(state, 0);
    spu94_set_adpcm_enabled(state, 1);
    spu94_set_latency_comp(state, 1);

    const uint32_t N = 64;
    int16_t input[64], out_l[64], out_r[64];
    memset(input, 0, sizeof input);
    input[0] = 10000;

    spu94_process(state, input, input, out_l, out_r, N);

    /* Samples 0-27 should be zero (delay buffer initially zero) */
    for (uint32_t i = 0; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_l[i],
            "Dry bus output before 28-sample delay should be zero");
    }

    /* Sample 28 should be non-zero (impulse emerges from delay) */
    TEST_ASSERT_TRUE_MESSAGE(out_l[28] != 0,
        "Dry bus output at sample 28 should be non-zero (impulse through delay)");
}

/* -----------------------------------------------------------------------
 * Test 6: Latency comp OFF = no delay on dry bus
 *
 * Same setup as above but latency_comp=0. The impulse should appear
 * at the output without the 28-sample delay.
 * ----------------------------------------------------------------------- */
static void test_latency_comp_off_no_delay(void) {
    spu94_set_input_gain(state, 0x7FFF);
    spu94_set_dry_fader(state, 0x7FFF);
    spu94_set_dry_send(state, 0);
    spu94_set_reverb_fader(state, 0);
    spu94_set_patina_fader(state, 0);
    spu94_set_patina_send(state, 0);
    spu94_set_adpcm_enabled(state, 1);
    spu94_set_latency_comp(state, 0);

    const uint32_t N = 64;
    int16_t input[64], out_l[64], out_r[64];
    memset(input, 0, sizeof input);
    input[0] = 10000;

    spu94_process(state, input, input, out_l, out_r, N);

    /* With latency comp OFF, the impulse should appear at sample 0
     * (dry bus passes through directly, no delay) */
    TEST_ASSERT_TRUE_MESSAGE(out_l[0] != 0,
        "Dry bus output at sample 0 should be non-zero with latency comp OFF");
}

/* -----------------------------------------------------------------------
 * Test 7: State reset on disable -- delay buffer and position zeroed
 *
 * Enable latency_comp, feed 14 samples, check delay_pos == 14.
 * Disable latency_comp, check delay_pos == 0 and delay_buf zeroed.
 * ----------------------------------------------------------------------- */
static void test_latency_comp_state_reset_on_disable(void) {
    set_unity_passthrough(state);
    spu94_set_adpcm_enabled(state, 1);
    spu94_set_latency_comp(state, 1);

    /* Feed 14 samples to advance the delay position */
    int16_t input[14], discard_l[14], discard_r[14];
    for (int i = 0; i < 14; i++) input[i] = 10000;
    spu94_process(state, input, input, discard_l, discard_r, 14);

    /* delay_pos should be 14 (14 samples written into ring buffer) */
    TEST_ASSERT_EQUAL_UINT8(14, state->delay_pos);

    /* Disable latency compensation */
    spu94_set_latency_comp(state, 0);

    /* delay_pos should be zeroed */
    TEST_ASSERT_EQUAL_UINT8(0, state->delay_pos);

    /* delay buffer should be zeroed */
    TEST_ASSERT_EQUAL_INT16(0, state->delay_buf_l[0]);
    TEST_ASSERT_EQUAL_INT16(0, state->delay_buf_r[0]);

    /* Check a few more positions to be thorough */
    for (int i = 0; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16(0, state->delay_buf_l[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->delay_buf_r[i]);
    }
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_latency_comp_default_on);
    RUN_TEST(test_latency_comp_set_get);
    RUN_TEST(test_latency_comp_null_safety);
    RUN_TEST(test_latency_comp_only_when_adpcm_enabled);
    RUN_TEST(test_latency_comp_delays_dry_28_samples);
    RUN_TEST(test_latency_comp_off_no_delay);
    RUN_TEST(test_latency_comp_state_reset_on_disable);
    return UNITY_END();
}
