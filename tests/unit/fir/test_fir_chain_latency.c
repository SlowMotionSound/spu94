/* tests/unit/fir/test_fir_chain_latency.c -- Phase 4 Plan 03 Task 2
 *
 * D-09 latency contract (corrected during Plan 03 execution — see
 * SPU94_LATENCY_SAMPLES comment in include/spu94/spu94.h):
 *   - SPU94_LATENCY_SAMPLES macro == 58u
 *   - spu94_get_latency_samples() == 58u
 *   - empirical impulse-response peak at 44.1 kHz output t=57 or t=59
 *     (tied; ±1 sample tolerance per 04-RESEARCH § 7)
 *   - bit-identical output across spu94_reset (Pitfall 5 catcher)
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

static void test_latency_macro_matches_accessor(void) {
    TEST_ASSERT_EQUAL_UINT32(58u, SPU94_LATENCY_SAMPLES);
    TEST_ASSERT_EQUAL_UINT32(SPU94_LATENCY_SAMPLES,
                             spu94_get_latency_samples());
}

static void test_latency_empirical_matches_api(void) {
    spu94_state *s = fresh_state_local();
    int16_t outputs[80];
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(s, in, in, &out_l, &out_r);
        outputs[i] = out_l;
    }
    /* argmax |outputs[i]| */
    int peak_idx = 0;
    int32_t peak_mag = 0;
    for (int i = 0; i < 80; ++i) {
        int32_t mag = (outputs[i] < 0) ? -(int32_t)outputs[i]
                                       :  (int32_t)outputs[i];
        if (mag > peak_mag) { peak_mag = mag; peak_idx = i; }
    }
    /* ±1 sample tolerance per 04-RESEARCH section Test-Vector Library § 7. */
    TEST_ASSERT_INT_WITHIN(1, (int)spu94_get_latency_samples(), peak_idx);
}

static void test_latency_monotonic_across_resets(void) {
    spu94_state *s = fresh_state_local();
    int16_t run1[80];
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(s, in, in, &out_l, &out_r);
        run1[i] = out_l;
    }
    spu94_reset(s);
    int16_t run2[80];
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(s, in, in, &out_l, &out_r);
        run2[i] = out_l;
    }
    for (int i = 0; i < 80; ++i) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(run1[i], run2[i],
            "post-reset run must match pre-reset run (Pitfall 5)");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_latency_macro_matches_accessor);
    RUN_TEST(test_latency_empirical_matches_api);
    RUN_TEST(test_latency_monotonic_across_resets);
    return UNITY_END();
}
