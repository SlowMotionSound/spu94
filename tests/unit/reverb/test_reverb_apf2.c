/* tests/unit/reverb/test_reverb_apf2.c — Phase 3 Plan 03 Task 3
 *
 * CORE-05: APF2 stage bit-exactness. Structurally identical to APF1
 * with vAPF2 / dAPF2 / mLAPF2 / mRAPF2 / err_apf2 substituted. The
 * Pitfall-7 edge test from APF1 applies equivalently.
 *
 * Expected values derived via tests/python/derive_reverb_reference.py::ref_apf2
 * (Pitfall 9: no GPL emulator in the derivation chain).
 *
 * Register layout (buffer_address==0 post-init):
 *   mLAPF2 = 4 (store byte 8); mLAPF2 - dAPF2 = 2 (tap byte 4)
 *   mRAPF2 = 16 (store byte 32); mRAPF2 - dAPF2 = 14 (tap byte 28)
 *   dAPF2 = 2
 */
#include "unity.h"
#include "../../../src/spu94/spu94_reverb_internal.h"
#include "../../../src/spu94/spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <limits.h>
#include <stddef.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static alignas(2) unsigned char g_work_buf[0x1000];

static spu94_state *make_state(void) {
    for (size_t i = 0; i < sizeof(g_work_buf); ++i) g_work_buf[i] = 0;
    spu94_state *s = spu94_init(g_state_buf, sizeof(g_state_buf),
                                g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

static void set_apf2_regs(spu94_state *s) {
    spu94_set_reg_u16(s, SPU94_REG_mLAPF2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mRAPF2, 16);
    spu94_apply_pending_writes(s);
}

static int16_t buf_read_i16(const spu94_state *s, uint16_t halfword_idx) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    return (int16_t)((uint16_t)s->work_buf[off]
                   | ((uint16_t)s->work_buf[off + 1u] << 8));
}

static void buf_write_i16(spu94_state *s, uint16_t halfword_idx, int16_t v) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    uint16_t u = (uint16_t)v;
    s->work_buf[off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}

/* ---------------------------------------------------------------------
 * Zero case.
 * --------------------------------------------------------------------- */
static void test_apf2_zero(void) {
    spu94_state *s = make_state();
    set_apf2_regs(s);
    s->err_apf2 = 0;

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_apf2(s, 0, /*dAPF2=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16(0, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT16(0, buf_read_i16(s, 4));
    TEST_ASSERT_EQUAL_INT16(0, buf_read_i16(s, 16));
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf2);
}

/* ---------------------------------------------------------------------
 * Pass-through vAPF2 = 0 (same shape as APF1 pass-through).
 * --------------------------------------------------------------------- */
static void test_apf2_vAPF2_zero_passes_through_to_store(void) {
    spu94_state *s = make_state();
    set_apf2_regs(s);
    s->err_apf2 = 0;

    int16_t Lout = 0x1000, Rout = 0x2000;
    spu94_reverb_apf2(s, /*vAPF2=*/0, /*dAPF2=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16(0, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x1000, buf_read_i16(s, 4));
    TEST_ASSERT_EQUAL_INT16((int16_t)0x2000, buf_read_i16(s, 16));
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf2);
}

/* ---------------------------------------------------------------------
 * Feedback with seeded tap_delayed. Same derivation as APF1 feedback
 * test (identical recurrence, different register file).
 * Expected (via ref_apf2): Lout=0x1600, Rout=0x1B00; stored=0x1C00/0x2E00.
 * --------------------------------------------------------------------- */
static void test_apf2_feedback_seeded_tap(void) {
    spu94_state *s = make_state();
    set_apf2_regs(s);
    s->err_apf2 = 0;
    buf_write_i16(s, 2,  (int16_t)0x0800);
    buf_write_i16(s, 14, (int16_t)0x0400);

    int16_t Lout = (int16_t)0x2000, Rout = (int16_t)0x3000;
    spu94_reverb_apf2(s, (int16_t)0x4000, 2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16((int16_t)0x1600, Lout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x1B00, Rout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x1C00, buf_read_i16(s, 4));
    TEST_ASSERT_EQUAL_INT16((int16_t)0x2E00, buf_read_i16(s, 16));
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf2);
}

/* ---------------------------------------------------------------------
 * Pitfall 7: APF feedback-loop edge at INT16_MIN.
 * Lin = Rin = vAPF2 = tap_delayed = INT16_MIN.
 * Same derivation as APF1 (different register file only):
 *   prod1 = q15_mul(-0x8000, -0x8000) sat -> 0x7FFF.
 *   step1 = q15_add_sat(-0x8000, sat_s16(-(int32_t)0x7FFF)) = -0x8000.
 *   [mLAPF2] = -0x8000.
 *   prod2 = 0x7FFF.
 *   step3 = q15_add_sat(0x7FFF, -0x8000) = -1.
 *   Lout = -1.
 * --------------------------------------------------------------------- */
static void test_apf2_pitfall_7_INT16_MIN_triple(void) {
    spu94_state *s = make_state();
    set_apf2_regs(s);
    s->err_apf2 = 0;
    buf_write_i16(s, 2,  INT16_MIN);
    buf_write_i16(s, 14, INT16_MIN);

    int16_t Lout = INT16_MIN, Rout = INT16_MIN;
    spu94_reverb_apf2(s, INT16_MIN, 2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-1, Lout,
        "Pitfall 7 INT16_MIN-triple (APF2): feedback amplification stays bounded");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-1, Rout,
        "Pitfall 7 (R side)");
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, buf_read_i16(s, 4));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, buf_read_i16(s, 16));
}

/* ---------------------------------------------------------------------
 * err invariant: Lin=Rin=0x1234, vAPF2=0x5678, tap=0x3456. Via ref_apf2:
 * Lout=Rout=0x28C0, err=81152.
 * --------------------------------------------------------------------- */
static void test_apf2_err_nonzero_for_non_divisible(void) {
    spu94_state *s = make_state();
    set_apf2_regs(s);
    s->err_apf2 = 0;
    buf_write_i16(s, 2,  (int16_t)0x3456);
    buf_write_i16(s, 14, (int16_t)0x3456);

    int16_t Lout = (int16_t)0x1234, Rout = (int16_t)0x1234;
    spu94_reverb_apf2(s, (int16_t)0x5678, 2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16((int16_t)0x28C0, Lout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x28C0, Rout);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(81152, s->err_apf2,
        "Ref-derived err_apf2 = 81152");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_apf2_zero);
    RUN_TEST(test_apf2_vAPF2_zero_passes_through_to_store);
    RUN_TEST(test_apf2_feedback_seeded_tap);
    RUN_TEST(test_apf2_pitfall_7_INT16_MIN_triple);
    RUN_TEST(test_apf2_err_nonzero_for_non_divisible);
    return UNITY_END();
}
