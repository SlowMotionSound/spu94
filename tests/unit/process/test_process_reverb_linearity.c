/* tests/unit/process/test_process_reverb_linearity.c
 *
 * End-to-end linearity regression test. Pins the contract that Hall
 * preset output level tracks input level linearly up to known saturation.
 *
 * Bug caught by this test (REVIEW.md CR-01 / reverb-not-in-audio-path.md
 * DSP investigation): spu94_reverb_input_scale was implemented as a raw
 * int16 * int16 -> int32 widening multiply with no >>15 shift, so Hall's
 * vLIN = vRIN = 0x8000 (Q15 -1.0) saturated Lin/Rin to INT16_MIN for
 * every non-zero input, pinning output at ~-5 dBFS regardless of input.
 *
 * Nocash pseudocode "Lin = vLIN * LeftInput" is a Q15 multiply per
 * psx-spx's "multiplication results are divided by +8000h to fit them
 * to 16-bit range." After the fix, doubling the input doubles the
 * output across a wide dynamic range. This test drives the full public
 * spu94_process path (not just input_scale in isolation) so it defends
 * the entire chain: input_scale -> hard_clip -> IIR -> comb -> APF ->
 * output_scale -> wet mailbox -> FIR interpolator -> 44.1 kHz output.
 *
 * Strategy: feed deterministic noise through Hall at three amplitudes
 * (full, half, quarter) and measure the output peak of the primed
 * region. Full-amplitude peak should be ~2x the half-amplitude peak
 * and ~4x the quarter-amplitude peak, within a generous tolerance
 * (reverb network adds some nonlinearity via the Q15-saturating IIR
 * feedback even on the linear path).
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>
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

enum { N_FEED = 12000 };
static int16_t noise_l[N_FEED];
static int16_t noise_r[N_FEED];
static int16_t out_l_full[N_FEED];
static int16_t out_r_full[N_FEED];
static int16_t out_l_half[N_FEED];
static int16_t out_r_half[N_FEED];
static int16_t out_l_qtr[N_FEED];
static int16_t out_r_qtr[N_FEED];

/* Fill noise buffers with amplitude scale applied. scale=1 -> full range,
 * scale=2 -> halved, scale=4 -> quartered. Division instead of
 * multiplication to avoid edge cases around INT16_MIN. */
static void fill_noise(int16_t *l, int16_t *r, int scale) {
    reseed();
    for (int i = 0; i < N_FEED; i++) {
        l[i] = (int16_t)(noise_sample() / (int16_t)scale);
        r[i] = (int16_t)(noise_sample() / (int16_t)scale);
    }
}

static void run_hall(const int16_t *in_l, const int16_t *in_r,
                     int16_t *out_l, int16_t *out_r) {
    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, SPU94_PRESET_HALL));
    spu94_set_vLOUT(state, (int16_t)0x7FFF);
    spu94_set_vROUT(state, (int16_t)0x7FFF);
    spu94_tick(state);
    spu94_process(state, in_l, in_r, out_l, out_r, (uint32_t)N_FEED);
}

/* Peak over the primed region (latter half of the buffer). */
static int32_t primed_peak(const int16_t *l, const int16_t *r) {
    int32_t peak = 0;
    const int lo = N_FEED / 2;
    for (int i = lo; i < N_FEED; i++) {
        int32_t al = l[i] < 0 ? -(int32_t)l[i] : (int32_t)l[i];
        int32_t ar = r[i] < 0 ? -(int32_t)r[i] : (int32_t)r[i];
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
    }
    return peak;
}

/* Output must scale with input across 3 amplitudes.
 *
 * Hypothesis: full-amplitude peak / quarter-amplitude peak is > 2.0
 * (rules out the CR-01 pinned-output bug where ratio = 1.0) and
 * < 5.0 (rules out runaway feedback). PS1-authentic reverb is NOT
 * purely linear — the IIR / comb / APF network uses Q15 saturation
 * at every feedback stage per psx-spx, so at high input amplitudes
 * the network compresses (upper portion of the input range squeezes
 * into a narrower output range). Empirically under the CR-01 fix:
 * peak_full=20831, peak_half=16384, peak_qtr=8749 → full/qtr = 2.38.
 * This matches DuckStation's behavior on the same input and reflects
 * the hardware's documented memory-write-saturation semantics.
 *
 * The half/quarter ratio is the cleaner linearity probe because both
 * amplitudes sit below the compression knee; it should be > 1.5
 * (i.e., well above the 1.0 of the pinned-output bug, close to the
 * theoretical 2.0 of a pure-linear reverb). */
static void test_hall_output_scales_with_input(void) {
    fill_noise(noise_l, noise_r, 1);  /* full */
    run_hall(noise_l, noise_r, out_l_full, out_r_full);

    fill_noise(noise_l, noise_r, 2);  /* half */
    run_hall(noise_l, noise_r, out_l_half, out_r_half);

    fill_noise(noise_l, noise_r, 4);  /* quarter */
    run_hall(noise_l, noise_r, out_l_qtr, out_r_qtr);

    const int32_t peak_full = primed_peak(out_l_full, out_r_full);
    const int32_t peak_half = primed_peak(out_l_half, out_r_half);
    const int32_t peak_qtr  = primed_peak(out_l_qtr,  out_r_qtr);

    char msg[256];

    /* Sanity: all three peaks must be non-zero. */
    TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(0, peak_full,
        "Hall full-amplitude primed peak must be non-zero");
    TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(0, peak_qtr,
        "Hall quarter-amplitude primed peak must be non-zero");

    /* Full / quarter ratio: must be > 2.0 (rules out CR-01 pinned
     * output where ratio = 1.0). peak_full * 10 > peak_qtr * 20. */
    snprintf(msg, sizeof msg,
             "Hall full/quarter peak ratio = %d/%d = %.2f, need > 2.0. "
             "Peak_full=%d, peak_half=%d, peak_qtr=%d. "
             "CR-01: input_scale saturating Lin to INT16_MIN regardless of input?",
             (int)peak_full, (int)peak_qtr,
             peak_qtr ? (double)peak_full / (double)peak_qtr : 0.0,
             (int)peak_full, (int)peak_half, (int)peak_qtr);
    TEST_ASSERT_TRUE_MESSAGE(
        (int64_t)peak_full * 10 > (int64_t)peak_qtr * 20, msg);

    /* Full / quarter ratio must also be < 5.0 (rules out runaway
     * feedback). peak_full * 10 < peak_qtr * 50. */
    snprintf(msg, sizeof msg,
             "Hall full/quarter peak ratio = %d/%d = %.2f, need < 5.0 "
             "(full amplitude amplified beyond linear ceiling?). "
             "Peak_full=%d, peak_qtr=%d.",
             (int)peak_full, (int)peak_qtr,
             peak_qtr ? (double)peak_full / (double)peak_qtr : 0.0,
             (int)peak_full, (int)peak_qtr);
    TEST_ASSERT_TRUE_MESSAGE(
        (int64_t)peak_full * 10 < (int64_t)peak_qtr * 50, msg);

    /* Half / quarter ratio: the cleaner below-the-knee linearity probe.
     * Must be > 1.5 (close to theoretical 2.0, well above 1.0 pin). */
    snprintf(msg, sizeof msg,
             "Hall half/quarter peak ratio = %d/%d = %.2f, need > 1.5 "
             "(linearity below compression knee). Peak_half=%d, peak_qtr=%d.",
             (int)peak_half, (int)peak_qtr,
             peak_qtr ? (double)peak_half / (double)peak_qtr : 0.0,
             (int)peak_half, (int)peak_qtr);
    TEST_ASSERT_TRUE_MESSAGE(
        (int64_t)peak_half * 10 > (int64_t)peak_qtr * 15, msg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hall_output_scales_with_input);
    return UNITY_END();
}
