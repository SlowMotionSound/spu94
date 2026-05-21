/* tests/unit/process/test_process_dac_toggle_transitions.c -- Phase 9 Plan 02
 *
 * Integration-level DAC tests covering toggle transitions, reverb
 * interaction, ADPCM composition, and FIR sub-toggle transitions.
 * These behaviors are NOT covered by test_process_dac_integration.c
 * (which handles: defaults, set/get, null safety, master gate,
 * FIR-only, noise-only, both-on, state reset, L/R decorrelation).
 *
 * Pattern: same setUp/tearDown + set_unity_passthrough as
 * test_process_dac_integration.c.
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

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_a[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_a[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state_a = NULL;

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[SPU94_WORK_BUF_MAX_BYTES];

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_c[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_c[SPU94_WORK_BUF_MAX_BYTES];

void setUp(void) {
    state_a = spu94_init(state_buf_a, sizeof state_buf_a,
                         work_buf_a, sizeof work_buf_a);
    TEST_ASSERT_NOT_NULL(state_a);
}

void tearDown(void) {
    state_a = NULL;
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
 * Test 1: DAC on -> off -> on produces bit-identical output
 *
 * D-10 item 1. Strategy:
 *   State A: fresh init, DAC on, process 256 samples.
 *   State B: fresh init, DAC on, process 256 samples to dirty state,
 *            DAC off (state reset), DAC on again, process 256 samples
 *            of SAME input as State A's first batch.
 *   After re-enable, State B's DAC starts from a clean state identical
 *   to State A's initial state. With Off preset (no reverb tail) and
 *   identical input, outputs must match bit-for-bit.
 * ----------------------------------------------------------------------- */
static void test_dac_on_off_on_bit_identical(void) {
    const uint32_t N = 256;
    int16_t input[256], dirty_input[256];

    /* Deterministic input (same PRNG as existing tests) */
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
        dirty_input[i] = (int16_t)(((i * 6271 + 5678) % 30000) - 15000);
    }

    /* State A: fresh, Off preset, DAC on, process input */
    set_unity_passthrough(state_a);
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_noise_enabled(state_a, 1);

    int16_t out_a_l[256], out_a_r[256];
    spu94_process(state_a, input, input, out_a_l, out_a_r, N);

    /* State B: fresh, Off preset, DAC on, dirty, DAC off, DAC on, process */
    spu94_state *sb = spu94_init(state_buf_b, sizeof state_buf_b,
                                  work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(sb);
    set_unity_passthrough(sb);
    spu94_set_dac_enabled(sb, 1);
    spu94_set_dac_fir_enabled(sb, 1);
    spu94_set_dac_noise_enabled(sb, 1);

    /* Dirty the DAC state */
    int16_t discard_l[256], discard_r[256];
    spu94_process(sb, dirty_input, dirty_input, discard_l, discard_r, N);

    /* Toggle off (resets DAC state) then back on. Phase 22: also toggle
     * latency_comp off/on so the Stage B fir-match buffers (which
     * persist across DAC toggles by design -- they're latency-comp
     * state, not DAC state) are flushed too. Without this, the previous
     * spu94_process call left dry/ADPCM history in the fir_lc buffers,
     * and the second run would see non-zero output[0..57] from that
     * residue. */
    spu94_set_dac_enabled(sb, 0);
    spu94_set_latency_comp(sb, 0);
    spu94_set_dac_enabled(sb, 1);
    spu94_set_dac_fir_enabled(sb, 1);
    spu94_set_dac_noise_enabled(sb, 1);
    spu94_set_latency_comp(sb, 1);

    /* Process same input as State A */
    int16_t out_b_l[256], out_b_r[256];
    spu94_process(sb, input, input, out_b_l, out_b_r, N);

    /* Must be bit-identical */
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_l, out_b_l, N);
    TEST_ASSERT_EQUAL_INT16_ARRAY(out_a_r, out_b_r, N);
}

/* -----------------------------------------------------------------------
 * Test 2: DAC + reverb interaction
 *
 * D-10 item 2. Process through Hall preset with DAC enabled vs disabled.
 * Outputs must differ -- catches wiring bugs where DAC silently no-ops
 * when reverb is active.
 * ----------------------------------------------------------------------- */
static void test_dac_reverb_interaction(void) {
    const uint32_t N = 512;
    int16_t input[512];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* State A: Hall preset, DAC disabled (default) */
    set_unity_passthrough(state_a);
    spu94_load_preset(state_a, SPU94_PRESET_HALL);
    spu94_set_dry_send(state_a, 0x7FFF);

    int16_t out_nodac_l[512], out_nodac_r[512];
    spu94_process(state_a, input, input, out_nodac_l, out_nodac_r, N);

    /* State B: Hall preset, DAC enabled */
    spu94_state *sb = spu94_init(state_buf_b, sizeof state_buf_b,
                                  work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(sb);
    set_unity_passthrough(sb);
    spu94_load_preset(sb, SPU94_PRESET_HALL);
    spu94_set_dry_send(sb, 0x7FFF);
    spu94_set_dac_enabled(sb, 1);
    spu94_set_dac_fir_enabled(sb, 1);
    spu94_set_dac_noise_enabled(sb, 1);

    int16_t out_dac_l[512], out_dac_r[512];
    spu94_process(sb, input, input, out_dac_l, out_dac_r, N);

    /* Outputs must differ (DAC alters signal when reverb is active) */
    int differ = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_dac_l[i] != out_nodac_l[i] || out_dac_r[i] != out_nodac_r[i]) {
            differ = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differ,
        "DAC-on output must differ from DAC-off when reverb is active");
}

/* -----------------------------------------------------------------------
 * Test 3: DAC + ADPCM composition
 *
 * D-10 item 3. Both coloration stages enabled simultaneously. All three
 * configurations (DAC only, ADPCM only, both) must produce different
 * output -- proves they compose, not stomp.
 * ----------------------------------------------------------------------- */
static void test_dac_adpcm_composition(void) {
    const uint32_t N = 512;
    int16_t input[512];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* State A: DAC only */
    set_unity_passthrough(state_a);
    spu94_load_preset(state_a, SPU94_PRESET_ROOM);
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_noise_enabled(state_a, 1);

    int16_t out_dac_l[512], out_dac_r[512];
    spu94_process(state_a, input, input, out_dac_l, out_dac_r, N);

    /* State B: ADPCM only */
    spu94_state *sb = spu94_init(state_buf_b, sizeof state_buf_b,
                                  work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(sb);
    set_unity_passthrough(sb);
    spu94_load_preset(sb, SPU94_PRESET_ROOM);
    spu94_set_adpcm_enabled(sb, 1);
    spu94_set_adpcm_fader(sb, 0x7FFF);
    spu94_set_adpcm_send(sb, 0x7FFF);

    int16_t out_adpcm_l[512], out_adpcm_r[512];
    spu94_process(sb, input, input, out_adpcm_l, out_adpcm_r, N);

    /* State C: both DAC + ADPCM */
    spu94_state *sc = spu94_init(state_buf_c, sizeof state_buf_c,
                                  work_buf_c, sizeof work_buf_c);
    TEST_ASSERT_NOT_NULL(sc);
    set_unity_passthrough(sc);
    spu94_load_preset(sc, SPU94_PRESET_ROOM);
    spu94_set_dac_enabled(sc, 1);
    spu94_set_dac_fir_enabled(sc, 1);
    spu94_set_dac_noise_enabled(sc, 1);
    spu94_set_adpcm_enabled(sc, 1);
    spu94_set_adpcm_fader(sc, 0x7FFF);
    spu94_set_adpcm_send(sc, 0x7FFF);

    int16_t out_both_l[512], out_both_r[512];
    spu94_process(sc, input, input, out_both_l, out_both_r, N);

    /* DAC-only != ADPCM-only */
    int diff_dac_adpcm = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_dac_l[i] != out_adpcm_l[i] || out_dac_r[i] != out_adpcm_r[i]) {
            diff_dac_adpcm = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_dac_adpcm,
        "DAC-only must differ from ADPCM-only");

    /* DAC-only != both */
    int diff_dac_both = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_dac_l[i] != out_both_l[i] || out_dac_r[i] != out_both_r[i]) {
            diff_dac_both = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_dac_both,
        "DAC-only must differ from both-on");

    /* ADPCM-only != both */
    int diff_adpcm_both = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (out_adpcm_l[i] != out_both_l[i] || out_adpcm_r[i] != out_both_r[i]) {
            diff_adpcm_both = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_adpcm_both,
        "ADPCM-only must differ from both-on");
}

/* -----------------------------------------------------------------------
 * Test 4: FIR sub-toggle transitions
 *
 * Verify FIR sub-toggle can be independently disabled and re-enabled:
 *   - Both on (FIR + noise): segment 1
 *   - Disable FIR only (noise still on): segment 2
 *   - Re-enable FIR: segment 3
 *
 * Segment 3 output must be non-zero (FIR is processing again).
 * Segment 2 must differ from segments 1 and 3 (noise-only vs both).
 * ----------------------------------------------------------------------- */
static void test_dac_fir_sub_toggle_transitions(void) {
    const uint32_t N = 256;
    int16_t input[256];
    for (uint32_t i = 0; i < N; i++) {
        input[i] = (int16_t)(((i * 7919 + 1234) % 30000) - 15000);
    }

    /* Off preset, unity passthrough, DAC on with both sub-toggles */
    set_unity_passthrough(state_a);
    spu94_set_dac_enabled(state_a, 1);
    spu94_set_dac_fir_enabled(state_a, 1);
    spu94_set_dac_noise_enabled(state_a, 1);

    /* Segment 1: both on */
    int16_t seg1_l[256], seg1_r[256];
    spu94_process(state_a, input, input, seg1_l, seg1_r, N);

    /* Segment 2: disable FIR only */
    spu94_set_dac_fir_enabled(state_a, 0);
    int16_t seg2_l[256], seg2_r[256];
    spu94_process(state_a, input, input, seg2_l, seg2_r, N);

    /* Segment 3: re-enable FIR */
    spu94_set_dac_fir_enabled(state_a, 1);
    int16_t seg3_l[256], seg3_r[256];
    spu94_process(state_a, input, input, seg3_l, seg3_r, N);

    /* Segment 3 must have non-zero output (FIR is processing) */
    int has_nonzero = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (seg3_l[i] != 0 || seg3_r[i] != 0) {
            has_nonzero = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(has_nonzero,
        "Re-enabled FIR must produce non-zero output");

    /* Segment 2 (noise-only) must differ from segment 1 (both-on) */
    int diff_1_2 = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (seg1_l[i] != seg2_l[i] || seg1_r[i] != seg2_r[i]) {
            diff_1_2 = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_1_2,
        "Noise-only segment must differ from both-on segment");

    /* Segment 2 (noise-only) must differ from segment 3 (both re-enabled) */
    int diff_2_3 = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (seg2_l[i] != seg3_l[i] || seg2_r[i] != seg3_r[i]) {
            diff_2_3 = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(diff_2_3,
        "Noise-only segment must differ from re-enabled both segment");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_dac_on_off_on_bit_identical);
    RUN_TEST(test_dac_reverb_interaction);
    RUN_TEST(test_dac_adpcm_composition);
    RUN_TEST(test_dac_fir_sub_toggle_transitions);
    return UNITY_END();
}
