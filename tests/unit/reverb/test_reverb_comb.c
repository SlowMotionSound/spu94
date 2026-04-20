/* tests/unit/reverb/test_reverb_comb.c — Phase 3 Plan 03 Task 3
 *
 * CORE-05: 4-tap comb stage bit-exactness.
 *
 * D-07 LOCKED: the comb sum uses CASCADING sat_s16 after each add, NOT
 * int32 accumulation with a single final clamp. The distinguishing test
 * (test_comb_cascading_distinguishes_int32_accumulate) constructs inputs
 * where the two variants produce different output; the cascading value
 * is the one SPU-94 ships, and any regression to int32-accumulate will
 * fail this test.
 *
 * Expected values derived via tests/python/derive_reverb_reference.py::ref_comb
 * (Pitfall 9: no GPL emulator in the derivation chain). Each test case
 * carries a derivation comment tracing the nocash arithmetic.
 *
 * Register layout (buffer_address==0 post-init; halfword indexes chosen
 * so the 8 comb-tap reads (L+R * 4) fall at distinct byte offsets and
 * can be seeded cleanly from the test):
 *   mLCOMB1..4 = 2, 4, 6, 8     -> byte 4, 8, 12, 16
 *   mRCOMB1..4 = 10, 12, 14, 16 -> byte 20, 24, 28, 32
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

/* Seed the m*COMB* register slots via public API (TICK_LATCHED policy),
 * then flush pending writes so the stage function's spu94_get_reg_u16
 * reads the values we set. */
static void set_comb_regs(spu94_state *s) {
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB1, 2);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB3, 6);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB4, 8);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB1, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB2, 12);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB3, 14);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB4, 16);
    spu94_apply_pending_writes(s);
}

/* White-box helpers: write / read halfwords at the ring offset of a
 * halfword index (matches reverb_buf_read / reverb_buf_write semantics). */
static void buf_write_i16(spu94_state *s, uint16_t halfword_idx, int16_t v) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    uint16_t u = (uint16_t)v;
    s->work_buf[off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}

/* ---------------------------------------------------------------------
 * Zero case: all v*, all taps zero -> Lout = Rout = 0, err_comb = 0.
 * --------------------------------------------------------------------- */
static void test_comb_zero(void) {
    spu94_state *s = make_state();
    set_comb_regs(s);
    s->err_comb = 0;

    int16_t Lout = 0x1234, Rout = 0x5678;  /* sentinels; stage must overwrite. */
    spu94_reverb_comb(s, 0, 0, 0, 0, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16(0, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT32(0, s->err_comb);
}

/* ---------------------------------------------------------------------
 * Single-nonzero-tap case.
 * Derivation:
 *   Seed tL1 = 0x1000 at byte 4 (mLCOMB1=2 -> byte 4). All other taps = 0.
 *   v = (0x4000, 0, 0, 0). Products:
 *     p1 = q15_mul(0x4000, 0x1000) = (0x4000*0x1000)>>15 = 0x4000000>>15 = 0x0800, err=0
 *     p2 = p3 = p4 = 0.
 *   Cascade: acc=0x0800; sat(0x0800+0)=0x0800 (x3). Lout = 0x0800.
 *   R side: all-zero taps -> Rout = 0.
 *   err_comb = 0 (every product exactly divisible by 2^15).
 * --------------------------------------------------------------------- */
static void test_comb_single_nonzero_tap(void) {
    spu94_state *s = make_state();
    set_comb_regs(s);
    s->err_comb = 0;
    buf_write_i16(s, 2, 0x1000);  /* mLCOMB1 tap */

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, 0x4000, 0, 0, 0, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16((int16_t)0x0800, Lout);
    TEST_ASSERT_EQUAL_INT16(0, Rout);
    TEST_ASSERT_EQUAL_INT32(0, s->err_comb);
}

/* ---------------------------------------------------------------------
 * D-07 distinguishing case — cascading sat_s16 vs int32 accumulate.
 *
 * Inputs: v=(0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF); all taps 0x7FFF.
 * Products (q15_mul_truncate with ADR-0001 ASR semantics):
 *   p1 = q15_mul( 0x7FFF, 0x7FFF) = ( 0x3FFF0001 >> 15)            = 0x7FFE, err=1
 *   p2 = q15_mul( 0x7FFF, 0x7FFF) = same                           = 0x7FFE, err=1
 *   p3 = q15_mul(-0x7FFF, 0x7FFF) = (-0x3FFF0001 >> 15) saturated  = -0x7FFF (INT16_MIN+1), err=0x7FFF
 *   p4 = q15_mul(-0x7FFF, 0x7FFF) = same                           = -0x7FFF, err=0x7FFF
 *
 * CASCADING (D-07, the behavior SPU-94 ships):
 *   acc = p1 = 0x7FFE
 *   acc = sat_s16(0x7FFE + 0x7FFE) = sat_s16(0xFFFC)    = 0x7FFF  (sat #1)
 *   acc = sat_s16(0x7FFF + -0x7FFF) = 0                            (no sat)
 *   acc = sat_s16(0 + -0x7FFF)      = -0x7FFF                      (no sat)
 *   Final Lout = -0x7FFF (= -32767 = INT16_MIN + 1).
 *
 * INT32-ACCUMULATE (REJECTED variant — shown for reference):
 *   sum = 0x7FFE + 0x7FFE + (-0x7FFF) + (-0x7FFF) = -2
 *   sat_s16(-2) = -2
 *   REJECTED final Lout = -2.
 *
 * DIFFERENT by 32765 between the two variants. If the implementation
 * regresses to int32-accumulate, this test fails immediately.
 * Per-side err = 1+1+0x7FFF+0x7FFF = 65536. L+R total = 131072.
 * --------------------------------------------------------------------- */
static void test_comb_cascading_distinguishes_int32_accumulate(void) {
    spu94_state *s = make_state();
    set_comb_regs(s);
    s->err_comb = 0;
    /* Seed all 8 taps (L + R) with 0x7FFF. */
    for (uint16_t h = 2; h <= 16; h += 2) {
        buf_write_i16(s, h, (int16_t)0x7FFF);
    }

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF,
                         (int16_t)-0x7FFF, (int16_t)-0x7FFF, &Lout, &Rout);

    /* Cascading-sat answer: -0x7FFF. Int32-accumulate answer: -2 (rejected).
     * Any regression to int32-accumulate fails this assertion. */
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x7FFF, Lout,
        "D-07 cascading sat_s16: expected -0x7FFF; int32-accumulate would give -2");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x7FFF, Rout,
        "D-07 cascading sat_s16 (R side mirror)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(131072, s->err_comb,
        "8 multiplies; 4 saturating (-0x7FFF * 0x7FFF) contribute 0x7FFF err each");
}

/* ---------------------------------------------------------------------
 * First-tap saturation: cascading clamps fire on the first add already.
 *
 * Derivation:
 *   v1=v2=0x7FFF, v3=v4=0; taps all 0x7FFF.
 *   p1=p2=0x7FFE (err=1 each); p3=p4=0.
 *   Cascade: acc=0x7FFE; sat(0x7FFE+0x7FFE=0xFFFC)=0x7FFF (sat #1);
 *            sat(0x7FFF+0)=0x7FFF; sat(0x7FFF+0)=0x7FFF. Lout=0x7FFF.
 *   err per side = 2; total = 4.
 * --------------------------------------------------------------------- */
static void test_comb_first_add_saturates(void) {
    spu94_state *s = make_state();
    set_comb_regs(s);
    s->err_comb = 0;
    for (uint16_t h = 2; h <= 16; h += 2) {
        buf_write_i16(s, h, (int16_t)0x7FFF);
    }

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF, 0, 0, &Lout, &Rout);

    TEST_ASSERT_EQUAL_INT16((int16_t)0x7FFF, Lout);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x7FFF, Rout);
    TEST_ASSERT_EQUAL_INT32(4, s->err_comb);
}

/* ---------------------------------------------------------------------
 * err_comb accumulates monotonically under saturating inputs.
 *
 * Two calls of the distinguishing case (err=131072 each); after 2 calls
 * err_comb = 262144. No overwrite / no overflow (int32 can hold ~2 billion).
 * --------------------------------------------------------------------- */
static void test_comb_err_accumulates(void) {
    spu94_state *s = make_state();
    set_comb_regs(s);
    s->err_comb = 0;
    for (uint16_t h = 2; h <= 16; h += 2) {
        buf_write_i16(s, h, (int16_t)0x7FFF);
    }

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF,
                         (int16_t)-0x7FFF, (int16_t)-0x7FFF, &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT32(131072, s->err_comb);

    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF,
                         (int16_t)-0x7FFF, (int16_t)-0x7FFF, &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(262144, s->err_comb,
        "err_comb must accumulate across calls, not reset");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_comb_zero);
    RUN_TEST(test_comb_single_nonzero_tap);
    RUN_TEST(test_comb_cascading_distinguishes_int32_accumulate);
    RUN_TEST(test_comb_first_add_saturates);
    RUN_TEST(test_comb_err_accumulates);
    return UNITY_END();
}
