/* tests/unit/fir/test_fir_dc.c -- Phase 4 Plan 03 Task 2
 *
 * SC-2: DC round-trip through the full 44.1 kHz chain (reverb bypassed)
 * produces a settled DC output with no drift over 200 samples. Reference
 * value CHAIN_DC_SETTLED = 0x01FF (= 511) obtained via
 *   python3 tests/python/derive_fir_reference.py --dump-chain-tables
 * for input = +0x0400 = 1024. DC gain is ~0.499 per sample (half-band
 * filter at DC sums to 0x7FFE / 2 for the phase-0 subfilter; the
 * even-index subset sums to 0x3FFF, and ASR-by-15 of 0x3FFF * 0x0400 =
 * 0x0FFFC00 >> 15 = 0x1FF = 511).
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

/* Python-dumped reference: chain_step(reverb_bypass=True) on a sustained
 * +0x0400 input settles to +0x01FF after ~80 samples. */
#define CHAIN_DC_SETTLED ((int16_t)0x01FF)

static void test_chain_dc_steady_state(void) {
    spu94_state *s = fresh_state_local();
    int16_t out_l = 0, out_r = 0;
    /* Drive 150 samples to fully settle (2 * 39-sample delay lines). */
    for (int i = 0; i < 150; ++i) {
        spu94_fir_chain_step_reverb_bypass(s, 0x0400, 0x0400,
                                           &out_l, &out_r);
    }
    /* Assert last 50 outputs are all equal (no drift). */
    int16_t expected = 0;
    for (int i = 0; i < 50; ++i) {
        spu94_fir_chain_step_reverb_bypass(s, 0x0400, 0x0400,
                                           &out_l, &out_r);
        if (i == 0) { expected = out_l; }
        TEST_ASSERT_EQUAL_INT16_MESSAGE(expected, out_l,
            "DC output must not drift after settling");
        TEST_ASSERT_EQUAL_INT16(expected, out_r);
    }
    /* Assert exact value matches Python reference. */
    TEST_ASSERT_EQUAL_INT16(CHAIN_DC_SETTLED, expected);
}

static void test_chain_dc_negative(void) {
    spu94_state *s = fresh_state_local();
    int16_t out_l = 0, out_r = 0;
    for (int i = 0; i < 200; ++i) {
        spu94_fir_chain_step_reverb_bypass(s, -(int16_t)0x0400,
                                           -(int16_t)0x0400,
                                           &out_l, &out_r);
    }
    /* Settled negative DC should be close to -CHAIN_DC_SETTLED.
     * May differ by 1 LSB due to ASR truncation direction (ADR-0001). */
    TEST_ASSERT_INT16_WITHIN(1, -(int16_t)CHAIN_DC_SETTLED, out_l);
    TEST_ASSERT_INT16_WITHIN(1, -(int16_t)CHAIN_DC_SETTLED, out_r);
}

static void test_chain_dc_zero_input_zero_output(void) {
    spu94_state *s = fresh_state_local();
    for (int i = 0; i < 80; ++i) {
        int16_t out_l = 42, out_r = 42;  /* sentinel -- should be overwritten to 0 */
        spu94_fir_chain_step_reverb_bypass(s, 0, 0, &out_l, &out_r);
        TEST_ASSERT_EQUAL_INT16(0, out_l);
        TEST_ASSERT_EQUAL_INT16(0, out_r);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_chain_dc_steady_state);
    RUN_TEST(test_chain_dc_negative);
    RUN_TEST(test_chain_dc_zero_input_zero_output);
    return UNITY_END();
}
