/* tests/unit/reverb/test_reverb_apf1.c — Phase 3 Plan 03 Task 3
 *
 * CORE-05: APF1 stage bit-exactness.
 *
 * Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
 *   Lout = Lout - vAPF1*[mLAPF1-dAPF1]       ;step 1 (subtract)
 *   [mLAPF1] = Lout                           ;step 2 (store intermediate)
 *   Lout = Lout*vAPF1 + [mLAPF1-dAPF1]       ;step 3 (feedback)
 *
 * Pitfall 7 (APF feedback edge at INT16_MIN): the recursive structure
 * amplifies edge-case drift. Test battery includes the triple-INT16_MIN
 * case (Lin = vAPF1 = tap_delayed = INT16_MIN).
 *
 * Expected values derived via tests/python/derive_reverb_reference.py
 * (Pitfall 9: no GPL emulator in the derivation chain). Each test case
 * carries a hand-derivation comment.
 *
 * Register layout (buffer_address==0 post-init):
 *   mLAPF1 = 4 (store byte 8); mLAPF1 - dAPF1 = 2 (tap byte 4)
 *   mRAPF1 = 16 (store byte 32); mRAPF1 - dAPF1 = 14 (tap byte 28)
 *   dAPF1 = 2
 * No overlap between any L read/write and any R read/write.
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

/* Seed mLAPF1 / mRAPF1 via public API (TICK_LATCHED) then flush. */
static void set_apf1_regs(spu94_state *s) {
    spu94_set_reg_u16(s, SPU94_REG_mLAPF1, 4);
    spu94_set_reg_u16(s, SPU94_REG_mRAPF1, 16);
    spu94_apply_pending_writes(s);
}

/* White-box halfword read/write mirroring reverb_buf_read/write semantics. */
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
 * Zero: Lin=Rin=0, vAPF1=0, tap=0 -> Lout=Rout=0, err=0, stored=0.
 * --------------------------------------------------------------------- */
static void test_apf1_zero(void) {
    spu94_state *s = make_state();
    set_apf1_regs(s);
    s->err_apf1 = 0;

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_apf1(s, 0, /*dAPF1=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16(0, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT16(0, buf_read_i16(s, 4));   /* [mLAPF1] */
    TEST_ASSERT_EQUAL_INT16(0, buf_read_i16(s, 16));  /* [mRAPF1] */
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf1);
}

/* ---------------------------------------------------------------------
 * Pass-through vAPF1 = 0.
 * Derivation:
 *   prod1 = q15_mul(0, tap_delayed=0) = 0, err=0.
 *   step1 = q15_add_sat(Lin, sat_s16(-0)) = Lin.
 *   [mLAPF1] = Lin.
 *   prod2 = q15_mul(step1=Lin, vAPF1=0) = 0, err=0.
 *   step3 = q15_add_sat(0, tap_delayed=0) = 0.
 *   Lout = 0; [mLAPF1] = Lin=0x1000; [mRAPF1] = Rin=0x2000.
 * --------------------------------------------------------------------- */
static void test_apf1_vAPF1_zero_passes_through_to_store(void) {
    spu94_state *s = make_state();
    set_apf1_regs(s);
    s->err_apf1 = 0;

    int16_t Lout = 0x1000, Rout = 0x2000;
    spu94_reverb_apf1(s, /*vAPF1=*/0, /*dAPF1=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16(0, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x1000, buf_read_i16(s, 4));
    TEST_ASSERT_EQUAL_INT16((int16_t)0x2000, buf_read_i16(s, 16));
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf1);
}

/* ---------------------------------------------------------------------
 * Feedback with seeded tap_delayed (derived via ref_apf1).
 *
 * Layout: tap_delayed byte = mLAPF1-dAPF1 = 4-2 = 2 (byte 4).
 *         R tap_delayed byte = mRAPF1-dAPF1 = 16-2 = 14 (byte 28).
 * Seed L tap = 0x0800, R tap = 0x0400. Lin=0x2000, Rin=0x3000, vAPF1=0x4000.
 *
 * L side derivation:
 *   prod1 = q15_mul(0x4000, 0x0800) = (0x2000000 >> 15) = 0x0400, err=0.
 *   step1 = q15_add_sat(0x2000, sat_s16(-0x0400)) = 0x1C00.
 *   [mLAPF1=4] store at byte 8 = 0x1C00.
 *   prod2 = q15_mul(0x1C00, 0x4000) = (0x7000000 >> 15) = 0x0E00, err=0.
 *   step3 = q15_add_sat(0x0E00, 0x0800) = 0x1600.
 *   Lout = 0x1600.
 *
 * R side derivation (mirror, R tap = 0x0400, Rin=0x3000):
 *   prod1 = q15_mul(0x4000, 0x0400) = 0x0200.
 *   step1 = sat(0x3000 - 0x0200) = 0x2E00. [mRAPF1=16] = 0x2E00.
 *   prod2 = q15_mul(0x2E00, 0x4000) = 0x1700.
 *   step3 = 0x1700 + 0x0400 = 0x1B00. Rout = 0x1B00.
 * --------------------------------------------------------------------- */
static void test_apf1_feedback_seeded_tap(void) {
    spu94_state *s = make_state();
    set_apf1_regs(s);
    s->err_apf1 = 0;
    buf_write_i16(s, 2,  (int16_t)0x0800);  /* L tap_delayed */
    buf_write_i16(s, 14, (int16_t)0x0400);  /* R tap_delayed */

    int16_t Lout = (int16_t)0x2000, Rout = (int16_t)0x3000;
    spu94_reverb_apf1(s, /*vAPF1=*/(int16_t)0x4000, /*dAPF1=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16((int16_t)0x1600, Lout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x1B00, Rout);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1C00, buf_read_i16(s, 4),
        "[mLAPF1] intermediate store (step1)");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x2E00, buf_read_i16(s, 16),
        "[mRAPF1] intermediate store (R step1)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_apf1,
        "All products exactly divisible by 2^15; err must be zero");
}

/* ---------------------------------------------------------------------
 * Pitfall 7: APF feedback-loop edge at INT16_MIN.
 * Lin = Rin = vAPF1 = tap_delayed (L and R) = INT16_MIN.
 *
 * L side derivation (R mirrors):
 *   tap_delayed = INT16_MIN = -0x8000.
 *   prod1 = q15_mul(-0x8000, -0x8000): product = 0x40000000, shifted = 0x8000,
 *           sat_s16(0x8000) = 0x7FFF. err = 0 (exact divide).
 *   step1 = q15_add_sat(-0x8000, sat_s16(-(int32_t)0x7FFF))
 *         = q15_add_sat(-0x8000, -0x7FFF)
 *         = sat_s16(-0xFFFF) = -0x8000.
 *   [mLAPF1] = -0x8000  (stored intermediate).
 *   prod2 = q15_mul(-0x8000, -0x8000) = 0x7FFF (same as prod1), err=0.
 *   step3 = q15_add_sat(0x7FFF, -0x8000) = -1.
 *   Lout = -1.  (Hallmark of Pitfall 7 edge: Pitfall-1 guard prevents UB;
 *                feedback-amplified saturation stays bounded.)
 * --------------------------------------------------------------------- */
static void test_apf1_pitfall_7_INT16_MIN_triple(void) {
    spu94_state *s = make_state();
    set_apf1_regs(s);
    s->err_apf1 = 0;
    buf_write_i16(s, 2,  INT16_MIN);
    buf_write_i16(s, 14, INT16_MIN);

    int16_t Lout = INT16_MIN, Rout = INT16_MIN;
    spu94_reverb_apf1(s, /*vAPF1=*/INT16_MIN, /*dAPF1=*/2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-1, Lout,
        "Pitfall 7 INT16_MIN-triple: feedback amplification stays bounded");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-1, Rout,
        "Pitfall 7 (R side)");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MIN, buf_read_i16(s, 4),
        "Pitfall 1 guard: step1 stored at INT16_MIN (no negation UB)");
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, buf_read_i16(s, 16));
}

/* ---------------------------------------------------------------------
 * err invariant: non-divisible products yield nonzero err_apf1.
 * Inputs chosen via the Python reference to produce a predictable err.
 * Lin = Rin = 0x1234, vAPF1 = 0x5678, tap = 0x3456, dAPF1 = 2.
 * ref_apf1 output: Lout=Rout=0x28C0, err=81152.
 * --------------------------------------------------------------------- */
static void test_apf1_err_nonzero_for_non_divisible(void) {
    spu94_state *s = make_state();
    set_apf1_regs(s);
    s->err_apf1 = 0;
    buf_write_i16(s, 2,  (int16_t)0x3456);
    buf_write_i16(s, 14, (int16_t)0x3456);

    int16_t Lout = (int16_t)0x1234, Rout = (int16_t)0x1234;
    spu94_reverb_apf1(s, (int16_t)0x5678, 2, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x28C0, Lout,
        "Cross-checked against Python ref_apf1 output");
    TEST_ASSERT_EQUAL_INT16((int16_t)0x28C0, Rout);
    TEST_ASSERT_MESSAGE(s->err_apf1 != 0,
        "Non-divisible products must contribute truncation err");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(81152, s->err_apf1,
        "Ref-derived err_apf1 = 81152");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_apf1_zero);
    RUN_TEST(test_apf1_vAPF1_zero_passes_through_to_store);
    RUN_TEST(test_apf1_feedback_seeded_tap);
    RUN_TEST(test_apf1_pitfall_7_INT16_MIN_triple);
    RUN_TEST(test_apf1_err_nonzero_for_non_divisible);
    return UNITY_END();
}
