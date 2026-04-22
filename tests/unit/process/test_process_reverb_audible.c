/* tests/unit/process/test_process_reverb_audible.c
 *
 * End-to-end audibility regression test. Pins the contract that
 * spu94_process with a non-Off preset actually consumes the reverb
 * body's wet output — the bug documented in
 * .planning/debug/resolved/reverb-not-in-audio-path.md shipped
 * because no existing test asserted "output differs from
 * bypass-reverb-path output." ADR-Phase-6-G locks in the wet-only
 * 44.1 kHz output architecture this test defends.
 *
 * Three sub-tests:
 *
 *   1. test_hall_preset_produces_non_dry_output
 *       Hall preset + deterministic noise input -> output MUST be
 *       materially different from a bypass-reverb reference run on
 *       the same input. Compute sum-of-absolute-differences
 *       (robust, obvious, no floating point). Threshold is a
 *       per-sample average of >= 1000 LSB, chosen conservatively
 *       below the observed magnitude-of-the-fix-signal but well
 *       above any cross-seed noise floor.
 *
 *   2. test_off_preset_with_noise_input_is_silent
 *       Off preset's vLIN/vRIN/vLOUT/vROUT are all zero, so the
 *       reverb body gates everything. Under ADR-Phase-6-G's
 *       wet-only wiring, Off + non-silent input MUST produce
 *       silent output at 44.1 kHz as well.
 *
 *   3. test_hall_preset_tail_decays
 *       Hall + 100 noise samples, then flush(2000 silent samples).
 *       At least half of the 2000 flush samples must contain
 *       non-zero output — the reverb decay tail. A dry-passthrough
 *       implementation goes silent after the 39-tap FIR ring-down
 *       (~40 samples), so "tail extends deep into flush" is the
 *       behavioral discriminator.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>  /* abs */
#include <string.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[256 * 1024];
static spu94_state *state = NULL;

/* Deterministic LCG matching the pattern used elsewhere in the suite. */
static uint32_t lcg_seed;
static int16_t noise_sample(void) {
    lcg_seed = lcg_seed * 1103515245u + 12345u;
    const int32_t v = (int32_t)(lcg_seed >> 16) & 0xFFFF;
    return (int16_t)(v - 32767);
}
static void reseed(void) { lcg_seed = 0x00C0FFEE; }

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
    reseed();
}
void tearDown(void) { state = NULL; }

enum { N_FEED = 200 };
static int16_t noise_l[N_FEED];
static int16_t noise_r[N_FEED];

static void fill_noise_inputs(void) {
    reseed();
    for (int i = 0; i < N_FEED; i++) {
        noise_l[i] = noise_sample();
        noise_r[i] = noise_sample();
    }
}

/* Run spu94_process on a preset against a pre-baked noise buffer.
 * Caller provides the output buffers. */
static void run_preset(spu94_preset_id_t id,
                       int16_t *out_l, int16_t *out_r, int n) {
    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, id));
    spu94_tick(state);  /* commit d-prefix/m-prefix pending -> active */
    spu94_process(state, noise_l, noise_r, out_l, out_r, (uint32_t)n);
}

/* Sub-test 1: Hall preset produces output materially different from
 * the Off-preset reference (the only wet-free path available to a
 * black-box test: Off's vLOUT/vROUT = 0 gates wet-output to zero,
 * so Off-output and bypass-output are indistinguishable through the
 * public API). Sum-of-absolute-differences per sample >= 1000 LSB. */
static void test_hall_preset_produces_non_dry_output(void) {
    static int16_t ref_l[N_FEED], ref_r[N_FEED];
    static int16_t hall_l[N_FEED], hall_r[N_FEED];

    fill_noise_inputs();

    /* Reference: Off preset. Under ADR-Phase-6-G, Off + noise -> silence.
     * That's the "no reverb present" baseline this test compares against.
     * If the implementation regresses to feeding dry-decimator into the
     * interpolator, hall_l/r will equal a scaled version of noise_l/r
     * and the diff sum stays small; ADR-Phase-6-G holds when the diff
     * sum is large because hall_l/r is a reverb tail and ref_l/r is
     * silence. */
    run_preset(SPU94_PRESET_OFF, ref_l, ref_r, N_FEED);
    run_preset(SPU94_PRESET_HALL, hall_l, hall_r, N_FEED);

    int64_t abs_diff_sum = 0;
    for (int i = 0; i < N_FEED; i++) {
        abs_diff_sum += abs((int)hall_l[i] - (int)ref_l[i]);
        abs_diff_sum += abs((int)hall_r[i] - (int)ref_r[i]);
    }
    /* Threshold: per-sample average of 1000 LSB across the 2*N_FEED
     * diff terms. A dry-passthrough bug would keep ref at 0 and hall
     * at ~dry-noise; but the old bug had BOTH paths producing dry
     * output (Off didn't gate at 44.1 kHz), making the diff near zero.
     * Under ADR-Phase-6-G, Off -> 0 and Hall -> wet tail, so the
     * diff is dominated by the hall magnitude. */
    const int64_t min_expected = (int64_t)1000 * (int64_t)(2 * N_FEED);
    char msg[160];
    snprintf(msg, sizeof msg,
             "Hall vs Off abs-diff-sum=%lld, need >= %lld. "
             "Reverb wet path likely unwired (see ADR-Phase-6-G).",
             (long long)abs_diff_sum, (long long)min_expected);
    TEST_ASSERT_TRUE_MESSAGE(abs_diff_sum >= min_expected, msg);
}

/* Sub-test 2: Off preset + non-silent noise input -> silent output
 * at 44.1 kHz. ADR-Phase-6-G premise: vLOUT/vROUT = 0 gates the wet
 * path, and since the 44.1 kHz output IS the wet path under wet-only
 * wiring, the CLI/spu94_process output must be identically zero. */
static void test_off_preset_with_noise_input_is_silent(void) {
    static int16_t out_l[N_FEED], out_r[N_FEED];

    fill_noise_inputs();
    /* Pre-fill output with sentinel so any non-zero write is visible. */
    for (int i = 0; i < N_FEED; i++) {
        out_l[i] = (int16_t)0x7777;
        out_r[i] = (int16_t)0x7777;
    }
    run_preset(SPU94_PRESET_OFF, out_l, out_r, N_FEED);

    for (int i = 0; i < N_FEED; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_l[i],
            "Off preset + noise input must produce silent 44.1 kHz output (L). "
            "ADR-Phase-6-G wet-only wiring: vLOUT=0 gates output.");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_r[i],
            "Off preset + noise input must produce silent 44.1 kHz output (R).");
    }
}

/* Sub-test 3: Hall + 100 noise samples, then flush(2000 silent
 * samples). The reverb tail must extend meaningfully into the
 * flush region — this is the behavioral signature of an actual
 * reverb vs a dry-passthrough FIR. Threshold: at least half of
 * the 2000 flush samples (across L+R) contain non-zero output. */
static void test_hall_preset_tail_decays(void) {
    enum { FEED = 100, FLUSH = 2000 };
    static int16_t feed_l[FEED], feed_r[FEED];
    static int16_t feed_out_l[FEED], feed_out_r[FEED];
    static int16_t tail_l[FLUSH], tail_r[FLUSH];

    reseed();
    for (int i = 0; i < FEED; i++) {
        feed_l[i] = noise_sample();
        feed_r[i] = noise_sample();
    }

    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, SPU94_PRESET_HALL));
    spu94_tick(state);

    spu94_process(state, feed_l, feed_r, feed_out_l, feed_out_r, FEED);
    spu94_flush(state, tail_l, tail_r, FLUSH);

    int nonzero_count = 0;
    for (int i = 0; i < FLUSH; i++) {
        if (tail_l[i] != 0) nonzero_count++;
        if (tail_r[i] != 0) nonzero_count++;
    }
    /* With dry-passthrough wiring, the FIR delay lines empty out
     * after ~40 samples -> nonzero_count would be under 100. With
     * a real reverb tail driven by Hall's IIR/comb/APF feedback,
     * the tail persists for hundreds to thousands of samples. */
    const int min_expected = FLUSH;  /* half of 2*FLUSH total samples */
    char msg[160];
    snprintf(msg, sizeof msg,
             "Hall flush-tail non-zero count=%d/%d (L+R), need >= %d. "
             "FIR-only ring-down decays by ~40 samples; real reverb "
             "tail persists. See ADR-Phase-6-G.",
             nonzero_count, 2 * FLUSH, min_expected);
    TEST_ASSERT_TRUE_MESSAGE(nonzero_count >= min_expected, msg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hall_preset_produces_non_dry_output);
    RUN_TEST(test_off_preset_with_noise_input_is_silent);
    RUN_TEST(test_hall_preset_tail_decays);
    return UNITY_END();
}
