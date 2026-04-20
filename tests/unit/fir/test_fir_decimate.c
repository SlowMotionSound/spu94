/* tests/unit/fir/test_fir_decimate.c -- Phase 4 Plan 02 Task 3
 *
 * Per-stage decimator unit tests. Validates bit-exactness vs the
 * Python reference (tests/python/derive_fir_reference.py). Reference
 * values produced by `python3 tests/python/derive_fir_reference.py
 * --dump-test-tables` and pasted below.
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

/* =======================================================================
 * Reference values from `python3 tests/python/derive_fir_reference.py
 * --dump-test-tables` (tests/python/derive_fir_reference.py).
 *
 * The first 20 retained outputs trace the 39-tap impulse response
 * sampled at every OTHER 44.1 kHz step (the retained phase); the
 * remaining 20 are zero because the impulse has decayed past the
 * end of the 39-sample delay line.
 * =======================================================================
 */
static const int16_t decimator_impulse_ref[40] = {
    (int16_t)0xFFFF, (int16_t)0x0001, (int16_t)0xFFF6, (int16_t)0x0022, (int16_t)0xFF99, (int16_t)0x0109, (int16_t)0xFD98, (int16_t)0x0533,
    (int16_t)0xF470, (int16_t)0x2805, (int16_t)0x2805, (int16_t)0xF470, (int16_t)0x0533, (int16_t)0xFD98, (int16_t)0x0109, (int16_t)0xFF99,
    (int16_t)0x0022, (int16_t)0xFFF6, (int16_t)0x0001, (int16_t)0xFFFF, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000,
    (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000,
    (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000, (int16_t)0x0000,
};

#define DECIMATOR_DC_SETTLED ((int16_t)0x03FF)  /* = 1023, from Python dump */

typedef struct { int valid; int16_t out_l; int16_t out_r; } dec_out_t;
static dec_out_t drive_decimate(spu94_state *s, int16_t l_in, int16_t r_in) {
    dec_out_t r = {0, 0, 0};
    spu94_fir_decimate(s, l_in, r_in, &r.out_l, &r.out_r, &r.valid);
    return r;
}

static void test_decimate_phase_alternation(void) {
    spu94_state *s = fresh_state_local();
    TEST_ASSERT_EQUAL_INT(1, drive_decimate(s, 0, 0).valid);
    TEST_ASSERT_EQUAL_INT(0, drive_decimate(s, 0, 0).valid);
    TEST_ASSERT_EQUAL_INT(1, drive_decimate(s, 0, 0).valid);
    TEST_ASSERT_EQUAL_INT(0, drive_decimate(s, 0, 0).valid);
    spu94_reset(s);
    TEST_ASSERT_EQUAL_INT(1, drive_decimate(s, 0, 0).valid);
    TEST_ASSERT_EQUAL_INT(0, drive_decimate(s, 0, 0).valid);
}

static void test_decimate_impulse_response(void) {
    spu94_state *s = fresh_state_local();
    int idx = 0;
    for (int i = 0; i < 80; ++i) {
        int16_t in = (i == 0) ? INT16_MAX : 0;
        dec_out_t r = drive_decimate(s, in, in);
        if (r.valid) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(decimator_impulse_ref[idx],
                                            r.out_l,
                                            "impulse L vs Python reference");
            TEST_ASSERT_EQUAL_INT16_MESSAGE(decimator_impulse_ref[idx],
                                            r.out_r,
                                            "impulse R (identical input) vs reference");
            idx++;
        }
    }
    TEST_ASSERT_EQUAL_INT(40, idx);
}

static void test_decimate_dc_steady_state(void) {
    spu94_state *s = fresh_state_local();
    int16_t last_l = 0; int count = 0;
    for (int i = 0; i < 200; ++i) {
        dec_out_t r = drive_decimate(s, 0x0400, 0x0400);
        if (r.valid) { last_l = r.out_l; count++; }
    }
    TEST_ASSERT_EQUAL_INT(100, count);
    TEST_ASSERT_EQUAL_INT16(DECIMATOR_DC_SETTLED, last_l);
}

static void test_decimate_l_r_independence(void) {
    spu94_state *s = fresh_state_local();
    dec_out_t r = drive_decimate(s, INT16_MAX, 0);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_INT16(0, r.out_r);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_decimate_phase_alternation);
    RUN_TEST(test_decimate_impulse_response);
    RUN_TEST(test_decimate_dc_steady_state);
    RUN_TEST(test_decimate_l_r_independence);
    return UNITY_END();
}
