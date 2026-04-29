/* tests/unit/process/test_process_reverb_audible.c
 *
 * End-to-end audibility regression test. Pins the contract that
 * spu94_process with a non-Off preset actually consumes the reverb
 * body's wet output -- the bug documented in
 * .planning/debug/resolved/reverb-not-in-audio-path.md shipped
 * because no existing test asserted "output differs from
 * bypass-reverb-path output." ADR-Phase-6-G locks in the wet-only
 * 44.1 kHz output architecture this test defends.
 *
 * Three sub-tests:
 *
 *   1. test_hall_preset_produces_non_dry_output
 *       Hall preset + deterministic noise input -> output MUST be
 *       materially different from a silent reference run (Off
 *       preset). Compute sum-of-absolute-differences across enough
 *       samples to let Hall's longest delay tap (dLSAME = 4544
 *       halfword ticks = 9088 samples at 44.1 kHz) prime.
 *
 *   2. test_off_preset_with_noise_input_is_silent
 *       Off preset's vLOUT/vROUT are 0 (and the CLI/tests do NOT
 *       override them for Off), so the reverb body gates everything.
 *       Under ADR-Phase-6-G's wet-only wiring, Off + non-silent input
 *       MUST produce silent 44.1 kHz output.
 *
 *   3. test_hall_preset_tail_decays
 *       Hall + 15000 noise samples (primes the longest delay tap),
 *       then flush(4000 silent samples). At least half of the 4000
 *       flush samples must contain non-zero output -- the reverb
 *       decay tail. A dry-passthrough implementation goes silent
 *       after the 39-tap FIR ring-down (~40 samples), so "tail
 *       extends deep into flush" is the behavioral discriminator.
 *
 * All three tests set vLOUT/vROUT to 0x7FFF after spu94_load_preset
 * for non-Off presets: the factory presets intentionally leave those
 * master-mix registers at 0 (per spu94_presets.c lines 32-34), and
 * with wet-only wiring the 44.1 kHz output IS the vLOUT/vROUT-scaled
 * wet path. This mirrors the CLI default (see src/cli/main.c in
 * the same ADR-Phase-6-G commit).
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>
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

/* N_FEED chosen to exceed Hall's longest delay tap (dLSAME = 4544
 * halfword ticks = 9088 44.1 kHz samples) plus priming margin, so
 * the reverb network has settled before we measure. */
enum { N_FEED = 10000 };
static int16_t noise_l[N_FEED];
static int16_t noise_r[N_FEED];
static int16_t out_l_buf[N_FEED];
static int16_t out_r_buf[N_FEED];

static void fill_noise_inputs(void) {
    reseed();
    for (int i = 0; i < N_FEED; i++) {
        noise_l[i] = noise_sample();
        noise_r[i] = noise_sample();
    }
}

/* Run spu94_process on a preset against a pre-baked noise buffer.
 * For non-Off presets, set vLOUT/vROUT = 0x7FFF (ADR-Phase-6-G
 * master-mix default). Caller supplies the output buffers. */
static void run_preset(spu94_preset_id_t id,
                       int16_t *out_l, int16_t *out_r, int n) {
    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, id));
    if (id != SPU94_PRESET_OFF) {
        spu94_set_vLOUT(state, (int16_t)0x7FFF);
        spu94_set_vROUT(state, (int16_t)0x7FFF);
        /* Phase 7: mixer faders default to 0 (silence). Set unity gains
         * so audio passes through the mixer to the reverb and output.
         * Off preset stays at zero faders to preserve its silence contract. */
        spu94_set_input_gain(state, 0x7FFF);
        spu94_set_dry_fader(state, 0x7FFF);
        spu94_set_reverb_fader(state, 0x7FFF);
        spu94_set_dry_send(state, 0x7FFF);
    }
    spu94_tick(state);  /* commit d-prefix/m-prefix pending -> active */
    spu94_process(state, noise_l, noise_r, out_l, out_r, (uint32_t)n);
}

/* Sub-test 1: Hall preset produces output materially different from
 * the Off-preset reference. Off + noise -> silence; Hall + noise ->
 * wet reverb tail. Threshold sized from "reverb output after network
 * primes" -- we expect wet-amplitude on the order of thousands of
 * LSB at 0x7FFF master-mix, averaged across the second half of the
 * N_FEED samples (first half is network-priming). Sum-of-absolute-
 * differences across the LATTER HALF is the robust metric: the
 * first ~5000 samples are priming. */
static void test_hall_preset_produces_non_dry_output(void) {
    static int16_t ref_l[N_FEED], ref_r[N_FEED];
    static int16_t hall_l[N_FEED], hall_r[N_FEED];

    fill_noise_inputs();

    run_preset(SPU94_PRESET_OFF, ref_l, ref_r, N_FEED);
    run_preset(SPU94_PRESET_HALL, hall_l, hall_r, N_FEED);

    /* Off-reference must be identically zero (separate contract pinned
     * by test 2 below; assert here as a cheap sanity check). */
    for (int i = 0; i < N_FEED; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, ref_l[i],
            "Off-preset reference run must be silent (L)");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, ref_r[i],
            "Off-preset reference run must be silent (R)");
    }

    /* Measure on the second half of the buffer so network is primed. */
    const int lo = N_FEED / 2;
    int64_t abs_diff_sum = 0;
    for (int i = lo; i < N_FEED; i++) {
        abs_diff_sum += abs((int)hall_l[i]);  /* ref is 0 here */
        abs_diff_sum += abs((int)hall_r[i]);
    }
    /* Threshold: per-sample average of 100 LSB across the 2*(N_FEED-lo)
     * diff terms. A dry-passthrough regression would produce Hall
     * output equal to a scaled copy of the input noise (since vLOUT
     * wasn't gating the production path under the old bug), but in
     * that case the Off reference would ALSO be non-silent, and the
     * guarding silence-assertion above would fail first. Under
     * wet-only wiring, Hall's primed reverb tail with deterministic
     * noise input yields RMS in the low-thousands range; 100 LSB
     * average is comfortably below that and well above any floor. */
    const int64_t min_expected = (int64_t)100 * (int64_t)(2 * (N_FEED - lo));
    char msg[200];
    snprintf(msg, sizeof msg,
             "Hall primed-region abs-sum=%lld, need >= %lld. "
             "Reverb wet path likely unwired (see ADR-Phase-6-G).",
             (long long)abs_diff_sum, (long long)min_expected);
    TEST_ASSERT_TRUE_MESSAGE(abs_diff_sum >= min_expected, msg);
}

/* Sub-test 2: Off preset + non-silent noise input -> silent output
 * at 44.1 kHz. ADR-Phase-6-G premise: vLOUT/vROUT = 0 gates the wet
 * path, and since the 44.1 kHz output IS the wet path under wet-only
 * wiring, the spu94_process output must be identically zero even
 * with large-amplitude input. */
static void test_off_preset_with_noise_input_is_silent(void) {
    fill_noise_inputs();
    /* Pre-fill output with sentinel so any non-zero write is visible. */
    for (int i = 0; i < N_FEED; i++) {
        out_l_buf[i] = (int16_t)0x7777;
        out_r_buf[i] = (int16_t)0x7777;
    }
    run_preset(SPU94_PRESET_OFF, out_l_buf, out_r_buf, N_FEED);

    for (int i = 0; i < N_FEED; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_l_buf[i],
            "Off preset + noise input must produce silent 44.1 kHz output (L). "
            "ADR-Phase-6-G wet-only wiring: vLOUT=0 gates output.");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out_r_buf[i],
            "Off preset + noise input must produce silent 44.1 kHz output (R).");
    }
}

/* Sub-test 3: Hall + N_FEED noise samples, then flush(FLUSH silent
 * samples). The reverb tail must extend meaningfully into the flush
 * region -- behavioral signature of a real reverb vs a dry-passthrough
 * FIR. Threshold: at least half of the 2*FLUSH samples (L+R) are
 * non-zero. */
static void test_hall_preset_tail_decays(void) {
    enum { FLUSH = 4000 };
    static int16_t tail_l[FLUSH], tail_r[FLUSH];

    fill_noise_inputs();

    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, SPU94_PRESET_HALL));
    spu94_set_vLOUT(state, (int16_t)0x7FFF);
    spu94_set_vROUT(state, (int16_t)0x7FFF);
    /* Phase 7: mixer faders default to 0 (silence). Set unity gains. */
    spu94_set_input_gain(state, 0x7FFF);
    spu94_set_dry_fader(state, 0x7FFF);
    spu94_set_reverb_fader(state, 0x7FFF);
    spu94_set_dry_send(state, 0x7FFF);
    spu94_tick(state);

    spu94_process(state, noise_l, noise_r, out_l_buf, out_r_buf, N_FEED);
    spu94_flush(state, tail_l, tail_r, FLUSH);

    int nonzero_count = 0;
    for (int i = 0; i < FLUSH; i++) {
        if (tail_l[i] != 0) nonzero_count++;
        if (tail_r[i] != 0) nonzero_count++;
    }
    /* With dry-passthrough wiring, the FIR delay lines empty out
     * after ~40 samples -> nonzero_count would be under 100. With
     * a real reverb tail driven by Hall's IIR/comb/APF feedback,
     * the tail persists for thousands of samples. */
    const int min_expected = FLUSH;  /* half of 2*FLUSH total samples */
    char msg[200];
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
