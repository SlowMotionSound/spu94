/* tests/unit/fir/test_fir_interpolate.c -- Phase 4 Plan 02 Task 3
 *
 * Per-stage interpolator unit tests vs Python reference.
 * Reference values via `python3 tests/python/derive_fir_reference.py
 * --dump-test-tables`.
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

/* From derive_fir_reference.py --dump-test-tables. */
#define INTERP_IMPULSE_P0 ((int16_t)0xFFFF)  /* = -1  (coef[0] * 0x7FFF >> 15) */
#define INTERP_IMPULSE_P1 ((int16_t)0x0000)  /* center-tap reads oldest slot (zero) */
#define INTERP_DC_P0      ((int16_t)0x01FF)  /* = 511 */
#define INTERP_DC_P1      ((int16_t)0x0200)  /* = 512, hand-verified: (0x4000*0x0400)>>15 */

static void test_interpolate_impulse(void) {
    spu94_state *s = fresh_state_local();
    int16_t p0_l, p0_r, p1_l, p1_r;
    spu94_fir_interpolate(s, INT16_MAX, INT16_MAX,
                          &p0_l, &p0_r, &p1_l, &p1_r);
    TEST_ASSERT_EQUAL_INT16(INTERP_IMPULSE_P0, p0_l);
    TEST_ASSERT_EQUAL_INT16(INTERP_IMPULSE_P0, p0_r);
    TEST_ASSERT_EQUAL_INT16(INTERP_IMPULSE_P1, p1_l);
    TEST_ASSERT_EQUAL_INT16(INTERP_IMPULSE_P1, p1_r);
}

static void test_interpolate_dc_phase_gains(void) {
    spu94_state *s = fresh_state_local();
    int16_t p0_l, p0_r, p1_l, p1_r;
    int16_t last_p0 = 0, last_p1 = 0;
    for (int i = 0; i < 40; ++i) {
        spu94_fir_interpolate(s, 0x0400, 0x0400,
                              &p0_l, &p0_r, &p1_l, &p1_r);
        last_p0 = p0_l; last_p1 = p1_l;
    }
    /* Phase-1 center-tap is exact: (0x4000 * 0x0400) >> 15 = 0x0200. */
    TEST_ASSERT_EQUAL_INT16(0x0200, last_p1);
    TEST_ASSERT_EQUAL_INT16(INTERP_DC_P1, last_p1);
    /* Phase-0 settled: pasted from Python --dump-test-tables. */
    TEST_ASSERT_EQUAL_INT16(INTERP_DC_P0, last_p0);
}

static void test_interpolate_l_r_independence(void) {
    spu94_state *s = fresh_state_local();
    int16_t p0_l, p0_r, p1_l, p1_r;
    spu94_fir_interpolate(s, INT16_MAX, 0, &p0_l, &p0_r, &p1_l, &p1_r);
    /* Fresh state: R input 0 must produce R output 0 (D-08 separate state). */
    TEST_ASSERT_EQUAL_INT16(0, p0_r);
    TEST_ASSERT_EQUAL_INT16(0, p1_r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interpolate_impulse);
    RUN_TEST(test_interpolate_dc_phase_gains);
    RUN_TEST(test_interpolate_l_r_independence);
    return UNITY_END();
}
