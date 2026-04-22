/* tests/unit/preset/test_preset_nonzero_tail.c -- Phase 5 Plan 03 Task 3.
 *
 * ROADMAP Phase 5 SC-2 behavioral proof: every factory preset, loaded via
 * spu94_load_preset, interacts correctly with spu94_process + spu94_flush.
 *
 *   1. test_nonzero_tail_per_non_off_preset
 *       For each preset id in [SPU94_PRESET_ROOM .. SPU94_PRESET_DELAY]:
 *         spu94_reset(state) -> spu94_load_preset(state, id) ->
 *         spu94_tick(state) (flushes pending d-prefix/m-prefix to active)
 *         -> feed 100 samples of deterministic white noise via spu94_process
 *         -> spu94_flush(state, L, R, 1000) -> assert max(|L|) > 0 AND
 *         max(|R|) > 0 across the 1100 output samples.
 *
 *   2. test_off_preset_silences_input
 *       Fresh state + spu94_load_preset(Off) + spu94_tick +
 *       spu94_process(100 noise samples) + spu94_flush(1000)
 *       -> every output sample == 0. Off preset's vLOUT/vROUT = 0 gates
 *       the wet path, and under ADR-Phase-6-G's wet-only 44.1 kHz output
 *       wiring the 44.1 kHz output IS the wet path, so even non-silent
 *       input must produce silent output.
 *
 * ADR-Phase-6-G (wet-only output): chain_step_impl now feeds the reverb
 * body's LeftOutput/RightOutput into spu94_fir_interpolate instead of the
 * dry decimator samples. That makes Off's output gating (vLOUT/vROUT=0)
 * propagate through to the 44.1 kHz stream, which is what test 2 here
 * pins behaviorally.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>   /* abs */

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[256 * 1024];  /* 256 KB reverb scratch */
static spu94_state *state = NULL;

/* Deterministic noise: 32-bit LCG, seed 0xC0FFEE (Phase 2 Plan 05 precedent).
 * Mapped to [INT16_MIN+1, INT16_MAX-1] to avoid the exact-INT16_MIN anomaly
 * edge. */
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

/* Drive `feed_samples` pseudo-white-noise samples through spu94_process,
 * then `flush_samples` through spu94_flush. Returns max |output| across
 * both phases. */
static int32_t max_abs_output_after_drive(int feed_samples, int flush_samples) {
    int32_t max_abs = 0;
    int16_t Lin, Rin, Lout, Rout;
    for (int i = 0; i < feed_samples; i++) {
        Lin = noise_sample();
        Rin = noise_sample();
        spu94_process(state, &Lin, &Rin, &Lout, &Rout, 1);
        if (abs((int)Lout) > max_abs) max_abs = abs((int)Lout);
        if (abs((int)Rout) > max_abs) max_abs = abs((int)Rout);
    }
    for (int i = 0; i < flush_samples; i++) {
        spu94_flush(state, &Lout, &Rout, 1);
        if (abs((int)Lout) > max_abs) max_abs = abs((int)Lout);
        if (abs((int)Rout) > max_abs) max_abs = abs((int)Rout);
    }
    return max_abs;
}

/* Sub-test 1: every non-Off preset produces non-zero output for non-silent
 * input. Per-preset max|output| asymmetry captured in failure messages for
 * cell-specific diagnosis. Under ADR-Phase-6-G wet-only wiring, a "non-zero
 * tail" now means the wet reverb output actually reached the 44.1 kHz
 * stream. */
static void test_nonzero_tail_per_non_off_preset(void) {
    for (spu94_preset_id_t id = SPU94_PRESET_ROOM;
         id < SPU94_PRESET__COUNT; id++) {
        spu94_reset(state);
        reseed();
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_load_preset(state, id));
        /* Commit pending d-prefix/m-prefix values via one tick before
         * audio begins (per D-08 split-policy documentation). */
        spu94_tick(state);
        const int32_t ma = max_abs_output_after_drive(100, 1000);
        char msg[128];
        snprintf(msg, sizeof msg,
                 "non-Off preset %d produced zero-max output (expected > 0)",
                 (int)id);
        TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(0, ma, msg);
    }
}

/* Sub-test 2: Off preset + noise input -> silent output. Under
 * ADR-Phase-6-G wet-only wiring, Off's vLOUT/vROUT = 0 gates the
 * wet path to zero, and the 44.1 kHz output IS the wet path, so
 * even non-silent input produces silent output. This is the
 * behavioral premise the original plan encoded and the bug
 * recorded in .planning/debug/resolved/reverb-not-in-audio-path.md
 * had silently invalidated. */
static void test_off_preset_silences_input(void) {
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(state, SPU94_PRESET_OFF));
    /* Flush the 16 staged m-prefix 0x0001 defensive pending values to
     * active. */
    spu94_tick(state);

    const int32_t ma = max_abs_output_after_drive(100, 1000);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, ma,
        "Off preset + non-silent input must produce silent output under "
        "ADR-Phase-6-G wet-only wiring (vLOUT/vROUT=0 gates the wet path).");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nonzero_tail_per_non_off_preset);
    RUN_TEST(test_off_preset_silences_input);
    return UNITY_END();
}
