/* tests/unit/fir/test_fir_impulse.c -- Phase 4 Plan 03 Task 2
 *
 * SC-1 chain-level: the full 44.1 kHz wrapper (with reverb bypassed)
 * produces a symmetric half-band impulse response. Reference values
 * from:
 *   python3 tests/python/derive_fir_reference.py --dump-chain-tables
 *
 * Phase-0 (even 44.1 kHz output indices) and phase-1 (odd indices) each
 * form their own symmetric subsequence — the decimator-interpolator
 * cascade is a polyphase structure, so the full-rate output is not
 * symmetric about a single integer axis but each polyphase stream is.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];

static spu94_state *fresh_state_local(void) {
    spu94_state *s = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

/* Python-dumped reference: 80 44.1 kHz outputs of chain_step(reverb_bypass=True)
 * with input = +INT16_MAX at t=0 followed by 79 zeros. Obtained via
 *   python3 tests/python/derive_fir_reference.py --dump-chain-tables */
static const int16_t chain_impulse_ref[80] = {
    (int16_t)0x0000, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000,
    (int16_t)0x0000, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000,
    (int16_t)0x0000, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000, (int16_t)0xFFFF, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000,
    (int16_t)0x0001, (int16_t)0x0000, (int16_t)0xFFFD, (int16_t)0x0000, (int16_t)0xFFFB, (int16_t)0x0000, (int16_t)0x0009, (int16_t)0x0000,
    (int16_t)0x0010, (int16_t)0x0000, (int16_t)0xFFE3, (int16_t)0x0000, (int16_t)0xFFD1, (int16_t)0x0000, (int16_t)0x004A, (int16_t)0xFFFF,
    (int16_t)0x0073, (int16_t)0x0000, (int16_t)0xFF52, (int16_t)0xFFFB, (int16_t)0xFEE5, (int16_t)0x0011, (int16_t)0x01B8, (int16_t)0xFFCC,
    (int16_t)0x01B4, (int16_t)0x0084, (int16_t)0xFDE5, (int16_t)0xFECC, (int16_t)0xF83E, (int16_t)0x0299, (int16_t)0x0F01, (int16_t)0xFA38,
    (int16_t)0x08AB, (int16_t)0x1402, (int16_t)0x08AB, (int16_t)0x1402, (int16_t)0x0F01, (int16_t)0xFA38, (int16_t)0xF83E, (int16_t)0x0299,
    (int16_t)0xFDE5, (int16_t)0xFECC, (int16_t)0x01B4, (int16_t)0x0084, (int16_t)0x01B8, (int16_t)0xFFCC, (int16_t)0xFEE5, (int16_t)0x0011,
    (int16_t)0xFF52, (int16_t)0xFFFB, (int16_t)0x0073, (int16_t)0x0000, (int16_t)0x004A, (int16_t)0xFFFF, (int16_t)0xFFD1, (int16_t)0x0000,
};
#define CHAIN_IMPULSE_PEAK_INDEX 57  /* empirical argmax (first of tied 57/59) */

static void test_chain_impulse_response_shape(void) {
    spu94_state *s = fresh_state_local();
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(s, in, in, &out_l, &out_r);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(chain_impulse_ref[i], out_l,
            "chain impulse L vs Python reference (SC-1 chain-level)");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(chain_impulse_ref[i], out_r,
            "chain impulse R (identical input) vs reference");
    }
}

/* The chain cascade is polyphase: phase-0 (even 44.1 kHz outputs) and
 * phase-1 (odd outputs) are produced by distinct subfilters. Each
 * subsequence is independently linear-phase symmetric, but their
 * symmetry axes straddle the "true" continuous-time latency (57.5):
 *   phase-0 (even indices)  symmetric about t=57 (axis between 56 & 58)
 *   phase-1 (odd indices)   symmetric about t=58 (axis between 57 & 59)
 * This is the defining signature of a polyphase-split linear-phase FIR
 * cascade; it matches the SPU94_LATENCY_SAMPLES = 58u nominal reported
 * value within the D-09 ±1-sample contract.
 */
static void test_chain_impulse_polyphase_symmetry(void) {
    spu94_state *s = fresh_state_local();
    int16_t outs[80];
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(s, in, in, &out_l, &out_r);
        outs[i] = out_l;
    }
    /* Phase-0 (even indices) symmetric with axis between t=56 and t=58:
     *   outs[56 - 2k] == outs[58 + 2k] for k = 0, 1, 2, ...
     * i.e., pairs (56,58), (54,60), (52,62), ... */
    for (int k = 0; k <= 19; ++k) {
        int lo = 56 - 2 * k;
        int hi = 58 + 2 * k;
        if (lo >= 0 && hi < 80) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(outs[lo], outs[hi],
                "phase-0 (even-index) impulse-stream symmetric across t=56/58");
        }
    }
    /* Phase-1 (odd indices) symmetric with axis between t=57 and t=59:
     *   outs[57 - 2k] == outs[59 + 2k] for k = 0, 1, 2, ...
     * i.e., pairs (57,59), (55,61), (53,63), ... -- the two tied peaks
     * at t=57 and t=59 are the closest-in pair (k=0). */
    for (int k = 0; k <= 19; ++k) {
        int lo = 57 - 2 * k;
        int hi = 59 + 2 * k;
        if (lo >= 0 && hi < 80) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(outs[lo], outs[hi],
                "phase-1 (odd-index) impulse-stream symmetric across t=57/59");
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_chain_impulse_response_shape);
    RUN_TEST(test_chain_impulse_polyphase_symmetry);
    return UNITY_END();
}
