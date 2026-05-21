/* tests/unit/process/test_process_latency_comp.c -- Phase 7 Plan 03,
 * extended in Phase 22 for FIR-match Stage B compensation.
 *
 * Integration tests for the two-stage latency compensation system:
 *   Stage A -- 28-sample ADPCM-match delay on dry going into reverb send
 *              (so dry and ADPCM enter the reverb send time-aligned).
 *              Active only when latency_comp + adpcm_enabled.
 *   Stage B -- 58-sample FIR-match delay on dry+ADPCM before master mix
 *              (so they emerge aligned with the FIR-delayed reverb tail).
 *              Active whenever latency_comp is on; absence of this stage
 *              previously caused PDC misalignment (PLUG-15 finding).
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
 * Test 4: Stage B alone (latency_comp ON, ADPCM OFF) delays dry by
 *          SPU94_LATENCY_SAMPLES (58) to align with the FIR reverb tail
 *          at the master mix.
 *
 * Phase 22 update: previously this test asserted that latency_comp was
 * a no-op when ADPCM was disabled. That was correct for Stage A only,
 * but ignored the dry-vs-reverb misalignment at the master mix stage
 * (PLUG-15). The new contract: latency_comp ON always engages Stage B,
 * delaying dry by 58 samples regardless of ADPCM state. Without Stage B,
 * dry/ADPCM at the master mix lead the FIR-delayed reverb tail by 58
 * samples, breaking host PDC alignment for any plugin mix involving
 * both dry and reverb (and breaking passthrough configs outright).
 * ----------------------------------------------------------------------- */
static void test_latency_comp_stage_b_without_adpcm(void) {
    /* Pure dry passthrough: dry_fader=unity, everything else zero. */
    spu94_set_input_gain  (state, 0x7FFF);
    spu94_set_dry_fader   (state, 0x7FFF);
    spu94_set_dry_send    (state, 0);
    spu94_set_reverb_fader(state, 0);
    spu94_set_adpcm_fader(state, 0);
    spu94_set_adpcm_send (state, 0);
    spu94_set_adpcm_enabled(state, 0);
    spu94_set_latency_comp(state, 1);

    const uint32_t N = 96;
    int16_t input[96], out_l[96], out_r[96];
    memset(input, 0, sizeof input);
    input[0] = 10000;

    spu94_process(state, input, input, out_l, out_r, N);

    /* Samples 0..57 should be zero (Stage B 58-sample delay buffer
     * starts zero-filled and the impulse propagates through one slot
     * per tick). */
    for (uint32_t i = 0; i < SPU94_LATENCY_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_l[i],
            "Dry bus output before 58-sample Stage B delay should be zero");
    }
    /* Sample 58 should be non-zero (impulse emerges from Stage B). */
    TEST_ASSERT_TRUE_MESSAGE(out_l[SPU94_LATENCY_SAMPLES] != 0,
        "Dry bus output at sample 58 should be non-zero (impulse through Stage B)");
}

/* -----------------------------------------------------------------------
 * Test 5: Stage A + Stage B (latency_comp ON, ADPCM ON) delays dry by
 *          28 + 58 = 86 samples total at the master mix.
 *
 * Phase 22 update: previously this test asserted a single 28-sample
 * delay (Stage A only). The current contract: Stage A delays dry into
 * the reverb send (28), and Stage B delays dry into the master mix by
 * an additional 58 to match the FIR group delay. Total delay through
 * the dry bus at the master mix is 28 + 58 = 86 samples, matching the
 * value returned by spu94_get_total_latency_samples() when ADPCM is on.
 * ----------------------------------------------------------------------- */
static void test_latency_comp_dry_total_delay_with_adpcm(void) {
    spu94_set_input_gain  (state, 0x7FFF);
    spu94_set_dry_fader   (state, 0x7FFF);
    spu94_set_dry_send    (state, 0);
    spu94_set_reverb_fader(state, 0);
    spu94_set_adpcm_fader(state, 0);
    spu94_set_adpcm_send (state, 0);
    spu94_set_adpcm_enabled(state, 1);
    spu94_set_latency_comp(state, 1);

    const uint32_t total_delay = SPU94_LATENCY_SAMPLES + 28u; /* 58 + 28 = 86 */
    const uint32_t N = 128;
    int16_t input[128], out_l[128], out_r[128];
    memset(input, 0, sizeof input);
    input[0] = 10000;

    spu94_process(state, input, input, out_l, out_r, N);

    /* Samples 0..(total_delay - 1) must be zero (both stages start
     * zero-filled; the impulse propagates one slot per tick through
     * Stage A then Stage B). */
    for (uint32_t i = 0; i < total_delay; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_l[i],
            "Dry bus output before Stage A+B total delay should be zero");
    }
    /* Sample `total_delay` should be non-zero (impulse fully emerged). */
    TEST_ASSERT_TRUE_MESSAGE(out_l[total_delay] != 0,
        "Dry bus output at total-delay index should be non-zero");
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
    spu94_set_adpcm_fader(state, 0);
    spu94_set_adpcm_send(state, 0);
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

    /* Stage B fir-match buffers must also be zeroed (Phase 22) */
    TEST_ASSERT_EQUAL_UINT8(0, state->fir_lc_pos);
    for (unsigned i = 0; i < SPU94_LATENCY_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, state->fir_lc_dry_buf_l[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->fir_lc_dry_buf_r[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->fir_lc_adpcm_buf_l[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->fir_lc_adpcm_buf_r[i]);
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
    RUN_TEST(test_latency_comp_stage_b_without_adpcm);
    RUN_TEST(test_latency_comp_dry_total_delay_with_adpcm);
    RUN_TEST(test_latency_comp_off_no_delay);
    RUN_TEST(test_latency_comp_state_reset_on_disable);
    return UNITY_END();
}
