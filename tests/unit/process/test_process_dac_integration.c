/* tests/unit/process/test_process_dac_integration.c -- Phase 7 Plan 03
 *
 * Integration tests for DAC toggle hierarchy and DAC processing within
 * spu94_process. Covers: toggle defaults, set/get, null safety, master
 * gate behavior, FIR-only, noise-only, both-on, state reset on disable,
 * and L/R noise decorrelation (WR-02).
 *
 * Pattern: same setUp/tearDown as test_process_adpcm.c. Includes
 * spu94_state_internal.h for direct struct inspection.
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
 * Test 1: All DAC toggles default off
 * ----------------------------------------------------------------------- */
static void test_dac_toggles_default_off(void) {
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_enabled(state));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_fir_enabled(state));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_noise_enabled(state));
}

/* -----------------------------------------------------------------------
 * Test 2: Set/get round-trip for all 3 DAC toggles
 * ----------------------------------------------------------------------- */
static void test_dac_toggle_set_get(void) {
    /* dac_enabled */
    spu94_set_dac_enabled(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_enabled(state));
    spu94_set_dac_enabled(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_enabled(state));
    spu94_set_dac_enabled(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_enabled(state));

    /* dac_fir_enabled */
    spu94_set_dac_fir_enabled(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_fir_enabled(state));
    spu94_set_dac_fir_enabled(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_fir_enabled(state));
    spu94_set_dac_fir_enabled(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_fir_enabled(state));

    /* dac_noise_enabled */
    spu94_set_dac_noise_enabled(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_noise_enabled(state));
    spu94_set_dac_noise_enabled(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_noise_enabled(state));
    spu94_set_dac_noise_enabled(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_noise_enabled(state));
}

/* -----------------------------------------------------------------------
 * Test 3: NULL safety for all DAC setters/getters
 * ----------------------------------------------------------------------- */
static void test_dac_toggle_null_safety(void) {
    /* Setters: no crash */
    spu94_set_dac_enabled(NULL, 1);
    spu94_set_dac_fir_enabled(NULL, 1);
    spu94_set_dac_noise_enabled(NULL, 1);

    /* Getters: return 0 */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_enabled(NULL));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_fir_enabled(NULL));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_noise_enabled(NULL));
}

/* -----------------------------------------------------------------------
 * Test 4: Master toggle off bypasses all DAC processing
 *
 * With DAC master=0, FIR=1, noise=1: output should match a baseline
 * with all DAC off. With DAC master=1: output should differ.
 * ----------------------------------------------------------------------- */
static void test_dac_master_off_bypasses_processing(void) {
    const uint32_t N = 256;
    int16_t input[256], out_base_l[256], out_base_r[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* Baseline: DAC master=0 (default), sub-toggles=1 (should be ignored) */
    set_unity_passthrough(state);
    spu94_set_dac_enabled(state, 0);
    spu94_set_dac_fir_enabled(state, 1);
    spu94_set_dac_noise_enabled(state, 1);
    spu94_process(state, input, input, out_base_l, out_base_r, N);

    /* DAC master=1, FIR=1, noise=1 on a fresh state */
    spu94_state *state_on = spu94_init(state_buf_b, sizeof state_buf_b,
                                        work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_on);
    set_unity_passthrough(state_on);
    spu94_set_dac_enabled(state_on, 1);
    spu94_set_dac_fir_enabled(state_on, 1);
    spu94_set_dac_noise_enabled(state_on, 1);

    int16_t out_on_l[256], out_on_r[256];
    spu94_process(state_on, input, input, out_on_l, out_on_r, N);

    /* DAC-on output must differ from baseline (DAC alters signal) */
    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_on_l[i] != out_base_l[i] || out_on_r[i] != out_base_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "DAC master=1 output must differ from master=0 output");
}

/* -----------------------------------------------------------------------
 * Test 5: DAC FIR only (master=1, FIR=1, noise=0)
 * ----------------------------------------------------------------------- */
static void test_dac_fir_only(void) {
    const uint32_t N = 256;
    int16_t input[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* Baseline: no DAC */
    set_unity_passthrough(state);
    int16_t out_base_l[256], out_base_r[256];
    spu94_process(state, input, input, out_base_l, out_base_r, N);

    /* FIR only */
    spu94_state *state_fir = spu94_init(state_buf_b, sizeof state_buf_b,
                                         work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_fir);
    set_unity_passthrough(state_fir);
    spu94_set_dac_enabled(state_fir, 1);
    spu94_set_dac_fir_enabled(state_fir, 1);
    spu94_set_dac_noise_enabled(state_fir, 0);

    int16_t out_fir_l[256], out_fir_r[256];
    spu94_process(state_fir, input, input, out_fir_l, out_fir_r, N);

    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_fir_l[i] != out_base_l[i] || out_fir_r[i] != out_base_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "DAC FIR-only output must differ from no-DAC baseline");
}

/* -----------------------------------------------------------------------
 * Test 6: DAC noise only (master=1, FIR=0, noise=1)
 * ----------------------------------------------------------------------- */
static void test_dac_noise_only(void) {
    const uint32_t N = 256;
    int16_t input[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* Baseline: no DAC */
    set_unity_passthrough(state);
    int16_t out_base_l[256], out_base_r[256];
    spu94_process(state, input, input, out_base_l, out_base_r, N);

    /* Noise only */
    spu94_state *state_noise = spu94_init(state_buf_b, sizeof state_buf_b,
                                           work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_noise);
    set_unity_passthrough(state_noise);
    spu94_set_dac_enabled(state_noise, 1);
    spu94_set_dac_fir_enabled(state_noise, 0);
    spu94_set_dac_noise_enabled(state_noise, 1);

    int16_t out_noise_l[256], out_noise_r[256];
    spu94_process(state_noise, input, input, out_noise_l, out_noise_r, N);

    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_noise_l[i] != out_base_l[i] || out_noise_r[i] != out_base_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "DAC noise-only output must differ from no-DAC baseline");
}

/* -----------------------------------------------------------------------
 * Test 7: DAC both-on differs from each alone
 * ----------------------------------------------------------------------- */
static void test_dac_both_on_differs_from_each_alone(void) {
    const uint32_t N = 256;
    int16_t input[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* Run 1: FIR only */
    set_unity_passthrough(state);
    spu94_set_dac_enabled(state, 1);
    spu94_set_dac_fir_enabled(state, 1);
    spu94_set_dac_noise_enabled(state, 0);
    int16_t out_fir_l[256], out_fir_r[256];
    spu94_process(state, input, input, out_fir_l, out_fir_r, N);

    /* Run 2: noise only */
    spu94_state *s2 = spu94_init(state_buf_b, sizeof state_buf_b,
                                  work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(s2);
    set_unity_passthrough(s2);
    spu94_set_dac_enabled(s2, 1);
    spu94_set_dac_fir_enabled(s2, 0);
    spu94_set_dac_noise_enabled(s2, 1);
    int16_t out_noise_l[256], out_noise_r[256];
    spu94_process(s2, input, input, out_noise_l, out_noise_r, N);

    /* Run 3: both on -- fresh state */
    spu94_state *s3 = spu94_init(state_buf, sizeof state_buf,
                                  work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(s3);
    set_unity_passthrough(s3);
    spu94_set_dac_enabled(s3, 1);
    spu94_set_dac_fir_enabled(s3, 1);
    spu94_set_dac_noise_enabled(s3, 1);
    int16_t out_both_l[256], out_both_r[256];
    spu94_process(s3, input, input, out_both_l, out_both_r, N);

    /* both != fir-only */
    int diff_fir = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_both_l[i] != out_fir_l[i] || out_both_r[i] != out_fir_r[i]) {
            diff_fir = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_fir,
        "Both-on must differ from FIR-only");

    /* both != noise-only */
    int diff_noise = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_both_l[i] != out_noise_l[i] || out_both_r[i] != out_noise_r[i]) {
            diff_noise = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_noise,
        "Both-on must differ from noise-only");

    /* fir-only != noise-only */
    int diff_each = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_fir_l[i] != out_noise_l[i] || out_fir_r[i] != out_noise_r[i]) {
            diff_each = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_each,
        "FIR-only must differ from noise-only");
}

/* -----------------------------------------------------------------------
 * Test 8: DAC state reset on disable
 *
 * Enable DAC master, enable FIR and noise, feed samples. Disable DAC
 * master. Verify FIR state is zeroed and noise state is re-initialized
 * with non-zero seed.
 * ----------------------------------------------------------------------- */
static void test_dac_state_reset_on_disable(void) {
    set_unity_passthrough(state);
    spu94_set_dac_enabled(state, 1);
    spu94_set_dac_fir_enabled(state, 1);
    spu94_set_dac_noise_enabled(state, 1);

    /* Feed 100 samples to dirty DAC state */
    int16_t input[100], discard_l[100], discard_r[100];
    for (int i = 0; i < 100; i++) input[i] = 10000;
    spu94_process(state, input, input, discard_l, discard_r, 100);

    /* Disable DAC master */
    spu94_set_dac_enabled(state, 0);

    /* FIR state should be zeroed (stage1_idx reset to 0) */
    TEST_ASSERT_EQUAL_UINT8(0, state->dac_fir_l.stage1_idx);
    TEST_ASSERT_EQUAL_UINT8(0, state->dac_fir_r.stage1_idx);

    /* Noise state should be re-initialized with non-zero LFSR seed */
    TEST_ASSERT_TRUE_MESSAGE(state->dac_noise_l.lfsr != 0,
        "Noise L LFSR must be non-zero after reset (not zero-filled)");
    TEST_ASSERT_TRUE_MESSAGE(state->dac_noise_r.lfsr != 0,
        "Noise R LFSR must be non-zero after reset (not zero-filled)");
}

/* -----------------------------------------------------------------------
 * Test 9: DAC noise decorrelated between L and R channels (WR-02)
 *
 * Enable DAC master + noise. Feed silence through the mixer with
 * input_gain=0. DAC noise is added unconditionally when enabled, so
 * output = noise samples. L and R should differ because L uses seed
 * 0xACE1 and R uses seed 0x1ECA.
 * ----------------------------------------------------------------------- */
static void test_dac_noise_decorrelated_channels(void) {
    /* input_gain=0 so mixer output is silence; noise rides on top */
    spu94_set_input_gain(state, 0);
    spu94_set_dry_fader(state, 0x7FFF);
    spu94_set_dac_enabled(state, 1);
    spu94_set_dac_noise_enabled(state, 1);

    const uint32_t N = 256;
    int16_t input[256], out_l[256], out_r[256];
    memset(input, 0, sizeof input);

    spu94_process(state, input, input, out_l, out_r, N);

    /* L and R should differ on at least some samples (different seeds) */
    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_l[i] != out_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "DAC noise L and R must differ (different LFSR seeds per WR-02)");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dac_toggles_default_off);
    RUN_TEST(test_dac_toggle_set_get);
    RUN_TEST(test_dac_toggle_null_safety);
    RUN_TEST(test_dac_master_off_bypasses_processing);
    RUN_TEST(test_dac_fir_only);
    RUN_TEST(test_dac_noise_only);
    RUN_TEST(test_dac_both_on_differs_from_each_alone);
    RUN_TEST(test_dac_state_reset_on_disable);
    RUN_TEST(test_dac_noise_decorrelated_channels);
    return UNITY_END();
}
