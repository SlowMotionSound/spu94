/* tests/unit/process/test_process_adpcm.c -- M2 Phase 2 Plan 02
 *
 * Integration tests for ADPCM pipeline wiring in spu94_process.
 * Covers requirements ADPCM-INT-01 through ADPCM-INT-06:
 *
 *   INT-01: ADPCM encode+decode wired upstream of FIR, toggle via set/get
 *   INT-02: Double-buffer strategy, 28-sample latency when enabled
 *   INT-03: Total latency: 86 when enabled, 58 when disabled
 *   INT-04: State zeroed by init/reset, mid-stream toggle discards partial
 *   INT-05: Off by default, existing tests unchanged, state within cap
 *   INT-06: (rt_safety gates -- covered by existing rt_safety suite)
 *
 * Pattern: same setUp/tearDown as test_process_basic.c. Includes
 * spu94_state_internal.h for direct struct inspection (INT-04 tests).
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_adpcm.h>
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
 * Test 1: ADPCM off by default (ADPCM-INT-05)
 * ----------------------------------------------------------------------- */
static void test_adpcm_off_by_default(void) {
    TEST_ASSERT_EQUAL_INT(0, spu94_get_adpcm_enabled(state));
    TEST_ASSERT_EQUAL_UINT32(SPU94_LATENCY_SAMPLES,
                             spu94_get_total_latency_samples(state));
}

/* -----------------------------------------------------------------------
 * Test 2: Toggle set/get (ADPCM-INT-01)
 * ----------------------------------------------------------------------- */
static void test_adpcm_toggle_set_get(void) {
    /* Enable */
    spu94_set_adpcm_enabled(state, 1);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_adpcm_enabled(state));

    /* Disable */
    spu94_set_adpcm_enabled(state, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_adpcm_enabled(state));

    /* Non-zero normalizes to 1 */
    spu94_set_adpcm_enabled(state, 42);
    TEST_ASSERT_EQUAL_INT(1, spu94_get_adpcm_enabled(state));
}

/* -----------------------------------------------------------------------
 * Test 3: NULL safety (ADPCM-INT-01)
 * ----------------------------------------------------------------------- */
static void test_adpcm_null_safety(void) {
    /* No crash */
    spu94_set_adpcm_enabled(NULL, 1);

    /* NULL returns 0 (disabled) */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_adpcm_enabled(NULL));

    /* NULL returns default latency */
    TEST_ASSERT_EQUAL_UINT32(SPU94_LATENCY_SAMPLES,
                             spu94_get_total_latency_samples(NULL));
}

/* -----------------------------------------------------------------------
 * Test 4: Disabled matches baseline (ADPCM-INT-05)
 *
 * Two fresh states with ADPCM disabled should produce bit-identical output.
 * Also: enable then disable ADPCM should restore baseline behavior.
 * ----------------------------------------------------------------------- */
static void test_adpcm_disabled_matches_baseline(void) {
    const uint32_t N = 128;
    int16_t impulse[128];
    memset(impulse, 0, sizeof impulse);
    impulse[0] = 16000;

    /* State A: never touched ADPCM (default off) */
    int16_t out_a_l[128], out_a_r[128];
    spu94_process(state, impulse, impulse, out_a_l, out_a_r, N);

    /* State B: fresh init, also default off */
    spu94_state *state_b = spu94_init(state_buf_b, sizeof state_buf_b,
                                       work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_b);

    int16_t out_b_l[128], out_b_r[128];
    spu94_process(state_b, impulse, impulse, out_b_l, out_b_r, N);

    /* Bit-identical */
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_l, out_b_l, N);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_r, out_b_r, N);

    /* Now: enable ADPCM on a fresh state, then disable, feed same input.
     * Output should match baseline. */
    spu94_state *state_c = spu94_init(state_buf_b, sizeof state_buf_b,
                                       work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_c);
    spu94_set_adpcm_enabled(state_c, 1);
    spu94_set_adpcm_enabled(state_c, 0);

    int16_t out_c_l[128], out_c_r[128];
    spu94_process(state_c, impulse, impulse, out_c_l, out_c_r, N);

    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_l, out_c_l, N);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_r, out_c_r, N);
}

/* -----------------------------------------------------------------------
 * Test 5: ADPCM enabled differs from disabled (ADPCM-INT-01)
 *
 * ADPCM coloration must alter the signal. Under ADR-Phase-6-G wet-only
 * wiring, spu94_process with all-zero registers outputs silence regardless
 * of input, so we cannot observe ADPCM differences at the 44.1 kHz output.
 * Instead, we verify the ADPCM stage is wired by checking that the internal
 * state diverges: with ADPCM enabled, adpcm_out_buf holds decoded audio
 * (ADPCM-colored), while without ADPCM the buffer stays zero.
 *
 * Additionally, we load the Hall preset so actual non-zero audio passes
 * through the reverb chain, and check that the outputs differ between
 * ADPCM-enabled and disabled paths.
 * ----------------------------------------------------------------------- */
static void test_adpcm_enabled_differs_from_disabled(void) {
    /* Load Hall preset so audio actually passes through the reverb chain */
    spu94_result_t rc = spu94_load_preset(state, SPU94_PRESET_HALL);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);

    /* Prime the state with one tick to commit TICK_LATCHED registers */
    {
        int16_t z = 0;
        spu94_process(state, &z, &z, &z, &z, 1);
    }

    /* Feed a longer signal through the reverb (need > 58 samples for
     * FIR group delay + some reverb buildup) */
    const uint32_t N = 512;
    int16_t input[512];
    for (uint32_t i = 0; i < N; i++) {
        /* Pseudo-random signal with enough energy for reverb */
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    int16_t out_off_l[512], out_off_r[512];
    spu94_process(state, input, input, out_off_l, out_off_r, N);

    /* ADPCM-enabled state: fresh init with same Hall preset */
    spu94_state *state_on = spu94_init(state_buf_b, sizeof state_buf_b,
                                        work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(state_on);
    rc = spu94_load_preset(state_on, SPU94_PRESET_HALL);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);

    /* Prime */
    {
        int16_t z = 0;
        spu94_process(state_on, &z, &z, &z, &z, 1);
    }

    spu94_set_adpcm_enabled(state_on, 1);

    int16_t out_on_l[512], out_on_r[512];
    spu94_process(state_on, input, input, out_on_l, out_on_r, N);

    /* Outputs must differ (ADPCM adds 28-sample latency and coloration) */
    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_on_l[i] != out_off_l[i] || out_on_r[i] != out_off_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "ADPCM-enabled output must differ from disabled output with Hall preset");
}

/* -----------------------------------------------------------------------
 * Test 6: ADPCM 28-sample latency (ADPCM-INT-02)
 *
 * Feed the same input through ADPCM-enabled and ADPCM-disabled paths.
 * Use the reverb-bypass path (Off preset with zero gains) so the
 * FIR passthrough is clean. The ADPCM-enabled output peak should
 * appear 28 samples later than the disabled peak.
 * ----------------------------------------------------------------------- */
static void test_adpcm_latency_28_samples(void) {
    /* Use Off preset (all registers zero = no reverb contribution).
     * With wet-only output (ADR-Phase-6-G), all-zero registers means
     * silence out for any input. Instead, use raw FIR passthrough behavior:
     * With all registers zero, spu94_process outputs zeros regardless of
     * input. So we measure the ADPCM latency differently:
     *
     * We feed a steady tone and check when the ADPCM output buffer
     * starts emitting non-zero values versus the direct path. The
     * first SPU94_ADPCM_BLOCK_SAMPLES (28) output samples from ADPCM
     * should be zeros (from the initial zero-filled output buffer),
     * while the disabled path passes the input immediately.
     *
     * Since spu94_process with zero registers produces silence for both
     * paths, we instead directly test the ADPCM buffer mechanism by
     * inspecting the l/r values that reach spu94_fir_chain_step. We do
     * this by comparing two states' internal behavior.
     *
     * Simplest approach: feed constant nonzero input and check that the
     * first 28 samples of ADPCM-enabled output are silence (from the
     * initial zero-filled output buffer), while the same state with
     * ADPCM disabled would pass the input through immediately.
     *
     * We verify this by direct struct inspection: after feeding N < 28
     * samples with ADPCM enabled, adpcm_out_buf should still be zeros
     * (initial state) while adpcm_in_buf has accumulated the input.
     */
    spu94_set_adpcm_enabled(state, 1);

    /* Feed a steady tone of 10000 for 56 samples (exactly 2 ADPCM blocks) */
    const uint32_t N = 56;
    int16_t input[56];
    int16_t out_l[56], out_r[56];
    for (uint32_t i = 0; i < N; i++) input[i] = 10000;

    spu94_process(state, input, input, out_l, out_r, N);

    /* After processing, adpcm_buf_pos should be 0 (56 = 2 full blocks) */
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_buf_pos);

    /* The first block (samples 0-27) was emitted from the initial
     * zero-filled output buffer. Since all registers are zero (no reverb),
     * the actual 44.1 kHz output is silence regardless. But we can verify
     * the ADPCM mechanism worked by checking the output buffers now hold
     * decoded audio from the second block (which encoded 10000 values). */
    int has_nonzero = 0;
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        if (state->adpcm_out_buf_l[i] != 0) {
            has_nonzero = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(has_nonzero,
        "After 2 ADPCM blocks, output buffer should hold decoded audio");

    /* Verify the latency concept: create a fresh state, enable ADPCM,
     * feed 28 samples. The values that reached the FIR chain for those
     * 28 samples should all be zero (from the initial output buffer). */
    spu94_state *fresh = spu94_init(state_buf_b, sizeof state_buf_b,
                                     work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(fresh);
    spu94_set_adpcm_enabled(fresh, 1);

    /* Before processing: output buffer is all zeros */
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, fresh->adpcm_out_buf_l[i]);
    }

    /* Feed 27 samples (not yet a full block) */
    int16_t input27[27];
    int16_t discard_l[27], discard_r[27];
    for (int i = 0; i < 27; i++) input27[i] = 10000;
    spu94_process(fresh, input27, input27, discard_l, discard_r, 27);

    /* Output buffer should STILL be zeros (no complete block yet) */
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, fresh->adpcm_out_buf_l[i]);
    }

    /* Feed one more sample to complete the first block */
    int16_t one_sample = 10000;
    int16_t dl, dr;
    spu94_process(fresh, &one_sample, &one_sample, &dl, &dr, 1);

    /* NOW the output buffer should have decoded audio (first block processed) */
    has_nonzero = 0;
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        if (fresh->adpcm_out_buf_l[i] != 0) {
            has_nonzero = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(has_nonzero,
        "After first complete ADPCM block, output buffer should have decoded audio");
}

/* -----------------------------------------------------------------------
 * Test 7: Latency report (ADPCM-INT-03)
 * ----------------------------------------------------------------------- */
static void test_adpcm_latency_report(void) {
    /* Disabled: 58 */
    TEST_ASSERT_EQUAL_UINT32(58u, spu94_get_total_latency_samples(state));

    /* Enable: 86 (58 + 28) */
    spu94_set_adpcm_enabled(state, 1);
    TEST_ASSERT_EQUAL_UINT32(86u, spu94_get_total_latency_samples(state));

    /* Disable: back to 58 */
    spu94_set_adpcm_enabled(state, 0);
    TEST_ASSERT_EQUAL_UINT32(58u, spu94_get_total_latency_samples(state));

    /* Backward compat: spu94_get_latency_samples always returns 58 */
    TEST_ASSERT_EQUAL_UINT32(58u, spu94_get_latency_samples());
}

/* -----------------------------------------------------------------------
 * Test 8: State zeroed by init (ADPCM-INT-04)
 * ----------------------------------------------------------------------- */
static void test_adpcm_state_zeroed_by_init(void) {
    /* After spu94_init, all ADPCM fields should be zero */
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_enabled);
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_buf_pos);

    /* Codec states */
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.old);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.older);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_r.old);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_r.older);

    /* Output buffers */
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, state->adpcm_out_buf_l[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->adpcm_out_buf_r[i]);
    }

    /* Input buffers */
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, state->adpcm_in_buf_l[i]);
        TEST_ASSERT_EQUAL_INT16(0, state->adpcm_in_buf_r[i]);
    }
}

/* -----------------------------------------------------------------------
 * Test 9: State zeroed by reset (ADPCM-INT-04)
 * ----------------------------------------------------------------------- */
static void test_adpcm_state_zeroed_by_reset(void) {
    /* Enable ADPCM and feed some noise to dirty the state */
    spu94_set_adpcm_enabled(state, 1);

    int16_t noise[100], discard_l[100], discard_r[100];
    for (int i = 0; i < 100; i++) {
        noise[i] = (int16_t)((i % 2 == 0) ? 10000 : -10000);
    }
    spu94_process(state, noise, noise, discard_l, discard_r, 100);

    /* State should be dirty now */
    TEST_ASSERT_EQUAL_UINT8(1, state->adpcm_enabled);

    /* Reset */
    spu94_reset(state);

    /* After reset, ADPCM fields should be zeroed */
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_enabled);
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_buf_pos);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.old);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.older);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_r.old);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_r.older);

    /* Output buffers zeroed */
    for (int i = 0; i < SPU94_ADPCM_BLOCK_SAMPLES; i++) {
        TEST_ASSERT_EQUAL_INT16(0, state->adpcm_out_buf_l[i]);
    }

    /* Verify the get accessor also reports disabled */
    TEST_ASSERT_EQUAL_INT(0, spu94_get_adpcm_enabled(state));
}

/* -----------------------------------------------------------------------
 * Test 10: Mid-stream toggle discards partial buffer (ADPCM-INT-04)
 * ----------------------------------------------------------------------- */
static void test_adpcm_midstream_toggle_discards_partial(void) {
    spu94_set_adpcm_enabled(state, 1);

    /* Feed 14 samples (half a block) */
    int16_t input14[14], discard_l[14], discard_r[14];
    for (int i = 0; i < 14; i++) input14[i] = 10000;
    spu94_process(state, input14, input14, discard_l, discard_r, 14);

    /* buf_pos should be 14 */
    TEST_ASSERT_EQUAL_UINT8(14, state->adpcm_buf_pos);

    /* Disable ADPCM -- partial buffer should be discarded */
    spu94_set_adpcm_enabled(state, 0);

    /* buf_pos should be 0 */
    TEST_ASSERT_EQUAL_UINT8(0, state->adpcm_buf_pos);

    /* Output buffer should be zeroed (stale audio cleared on disable) */
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_out_buf_l[0]);

    /* Codec state should also be zeroed (clean re-enable) */
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.old);
    TEST_ASSERT_EQUAL_INT16(0, state->adpcm_state_l.older);
}

/* -----------------------------------------------------------------------
 * Test 11: State size under cap (ADPCM-INT-05)
 * ----------------------------------------------------------------------- */
static void test_adpcm_state_size_under_cap(void) {
    TEST_ASSERT_TRUE(spu94_state_size() <= SPU94_STATE_SIZE_MAX);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_adpcm_off_by_default);
    RUN_TEST(test_adpcm_toggle_set_get);
    RUN_TEST(test_adpcm_null_safety);
    RUN_TEST(test_adpcm_disabled_matches_baseline);
    RUN_TEST(test_adpcm_enabled_differs_from_disabled);
    RUN_TEST(test_adpcm_latency_28_samples);
    RUN_TEST(test_adpcm_latency_report);
    RUN_TEST(test_adpcm_state_zeroed_by_init);
    RUN_TEST(test_adpcm_state_zeroed_by_reset);
    RUN_TEST(test_adpcm_midstream_toggle_discards_partial);
    RUN_TEST(test_adpcm_state_size_under_cap);
    return UNITY_END();
}
