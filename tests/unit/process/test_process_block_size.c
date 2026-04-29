/* tests/unit/process/test_process_block_size.c -- Phase 5 Plan 05.
 *
 * Block-size-invariance proof: the block size is pure flow-control;
 * output must be bit-identical across any grouping of samples into
 * spu94_process calls. Reference = block-size-1 (sample-at-a-time).
 *
 * Sweep: {1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096}. A fresh-init
 * state + Hall preset + one tick establishes a deterministic starting
 * point; identical pseudo-input is driven through in chunks of each
 * sweep size and compared to the block-1 baseline sample-by-sample.
 *
 * Closes API-03's "any block size N >= 1" contract (D-03). Complements
 * tests/python/fuzz_process.py which exercises random block sizes; this
 * TU pins the specific sizes the plan calls out.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

#define TOTAL_SAMPLES 4096
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[256 * 1024];

static int16_t L_in[TOTAL_SAMPLES];
static int16_t R_in[TOTAL_SAMPLES];
static int16_t L_ref[TOTAL_SAMPLES];
static int16_t R_ref[TOTAL_SAMPLES];
static int16_t L_out[TOTAL_SAMPLES];
static int16_t R_out[TOTAL_SAMPLES];

/* Deterministic input generator: 32-bit LCG, seed 0xBEEF.
 * Same sequence every run. */
static void generate_input(void) {
    uint32_t s = 0x0000BEEF;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        s = s * 1103515245u + 12345u;
        L_in[i] = (int16_t)((s >> 16) & 0xFFFF);
        s = s * 1103515245u + 12345u;
        R_in[i] = (int16_t)((s >> 16) & 0xFFFF);
    }
}

/* Helper: fresh-init state + Hall preset + one tick to commit pending. */
static spu94_state *fresh_state(void) {
    spu94_state *s = spu94_init(state_buf, sizeof state_buf,
                                work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(s);
    spu94_reset(s);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(s, SPU94_PRESET_HALL));
    /* ADR-Phase-6-H: non-Off factory preset tables carry vLOUT=vROUT=0x7FFF
     * so spu94_load_preset alone yields audible output. No explicit
     * post-load master-send write needed here. */
    /* Phase 7: mixer faders default to 0 (silence). Set unity gains. */
    spu94_set_input_gain(s, 0x7FFF);
    spu94_set_dry_fader(s, 0x7FFF);
    spu94_set_reverb_fader(s, 0x7FFF);
    spu94_set_dry_send(s, 0x7FFF);
    spu94_tick(s);
    return s;
}

void setUp(void)    { generate_input(); }
void tearDown(void) {}

/* Pass 1: reference, block-size = 1. */
static void build_reference(void) {
    spu94_state *s = fresh_state();
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        spu94_process(s, &L_in[i], &R_in[i], &L_ref[i], &R_ref[i], 1);
    }
    spu94_destroy(s);
}

/* Pass 2: same input through block_size = B; compare to reference. */
static void check_block_size(uint32_t B) {
    spu94_state *s = fresh_state();
    for (int i = 0; i < TOTAL_SAMPLES; i += (int)B) {
        uint32_t chunk = B;
        if ((uint32_t)(TOTAL_SAMPLES - i) < B) chunk = (uint32_t)(TOTAL_SAMPLES - i);
        spu94_process(s, &L_in[i], &R_in[i], &L_out[i], &R_out[i], chunk);
    }
    spu94_destroy(s);
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        char msg[96];
        snprintf(msg, sizeof msg,
                 "block_size=%u: sample[%d] L=%d ref=%d R=%d ref=%d",
                 (unsigned)B, i, (int)L_out[i], (int)L_ref[i],
                 (int)R_out[i], (int)R_ref[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(L_ref[i], L_out[i], msg);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(R_ref[i], R_out[i], msg);
    }
}

static void test_block_size_invariance(void) {
    build_reference();
    const uint32_t sweep[] = {1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096};
    for (size_t i = 0; i < sizeof sweep / sizeof sweep[0]; i++) {
        check_block_size(sweep[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_block_size_invariance);
    return UNITY_END();
}
