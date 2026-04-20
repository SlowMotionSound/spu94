/* tests/unit/reverb/test_reverb_diff_iir.c — Phase 3 Plan 02 Task 3
 *
 * CORE-05 (DIFF IIR stage bit-exactness) + CORE-08 (vIIR=-0x8000 anomaly)
 * + TEST-06 (dedicated anomaly + non-anomaly controls).
 *
 * Distinguishing feature vs SAME IIR: the wall tap pairing is cross-side.
 * The L-side write to [mLDIFF] reads its wall tap through dRDIFF (R's
 * delay register). The R-side write to [mRDIFF] reads its wall tap
 * through dLDIFF. One test explicitly exercises this cross-side shape.
 *
 * Expected values are derived via tests/python/derive_reverb_reference.py
 * (Pitfall 9: no GPL emulator in the derivation chain).
 *
 * Register layout used by most tests:
 *   mLDIFF = 8  -> write at byte 16;  mLDIFF-2 = 6  -> tap_prev at byte 12
 *   mRDIFF = 12 -> write at byte 24;  mRDIFF-2 = 10 -> tap_prev at byte 20
 *   dLDIFF = 4  -> R-side tap_d at byte 8  (cross-side)
 *   dRDIFF = 6  -> L-side tap_d at byte 12 (cross-side)
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

static void set_diff_regs(spu94_state *s,
                          uint16_t mLDIFF, uint16_t mRDIFF,
                          uint16_t dLDIFF, uint16_t dRDIFF)
{
    spu94_set_reg_u16(s, SPU94_REG_mLDIFF, mLDIFF);
    spu94_set_reg_u16(s, SPU94_REG_mRDIFF, mRDIFF);
    spu94_set_reg_u16(s, SPU94_REG_dLDIFF, dLDIFF);
    spu94_set_reg_u16(s, SPU94_REG_dRDIFF, dRDIFF);
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
 * CORE-08 anomaly (DIFF IIR side).
 * Same derivation shape as SAME IIR anomaly — both stages apply the same
 * D-10 branch. With zero-initialized buf and identical Lin=Rin=0x1000,
 * vWALL=0x4000, vIIR=INT16_MIN, both writes produce +0x1000 (the
 * negation of the pre-anomaly -0x1000 result).
 * --------------------------------------------------------------------- */
static void test_diff_iir_anomaly_vIIR_INT16_MIN_negates(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, /*mLDIFF=*/8, /*mRDIFF=*/12,
                     /*dLDIFF=*/4, /*dRDIFF=*/6);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x1000, 0x1000, INT16_MIN, 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1000, buf_read_i16(s, 8),
        "DIFF IIR vIIR=INT16_MIN anomaly: [mLDIFF] negated");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1000, buf_read_i16(s, 12),
        "DIFF IIR vIIR=INT16_MIN anomaly: [mRDIFF] negated");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_diff_iir,
        "All multiplies have zero truncation remainder");
}

/* ---------------------------------------------------------------------
 * TEST-06 control: vIIR = INT16_MIN + 1 does NOT negate.
 * Per-side iir_prod remainder = 0x1000 (same derivation as SAME IIR
 * control). L+R = 0x2000 = 8192.
 * --------------------------------------------------------------------- */
static void test_diff_iir_control_vIIR_INT16_MIN_plus_1_no_negate(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, 8, 12, 4, 6);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x1000, 0x1000,
                          (int16_t)(INT16_MIN + 1), 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x1000, buf_read_i16(s, 8),
        "DIFF IIR vIIR=INT16_MIN+1 control: [mLDIFF] not negated");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x1000, buf_read_i16(s, 12),
        "DIFF IIR vIIR=INT16_MIN+1 control: [mRDIFF] not negated");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(8192, s->err_diff_iir,
        "Two iir_prod remainders of 4096");
}

/* ---------------------------------------------------------------------
 * Second control: vIIR = INT16_MAX. Normal positive multiply.
 * Per-side iir_prod remainder = 0x7000 = 28672. L+R = 57344.
 * --------------------------------------------------------------------- */
static void test_diff_iir_control_vIIR_INT16_MAX_no_negate(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, 8, 12, 4, 6);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x1000, 0x1000, INT16_MAX, 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x0FFF, buf_read_i16(s, 8),
        "DIFF IIR vIIR=INT16_MAX control: [mLDIFF] normal multiply");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x0FFF, buf_read_i16(s, 12),
        "DIFF IIR vIIR=INT16_MAX control: [mRDIFF] normal multiply");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(57344, s->err_diff_iir,
        "Two iir_prod remainders of 0x7000");
}

/* ---------------------------------------------------------------------
 * Pitfall-1 edge: pre-anomaly result = INT16_MIN at each write.
 * Derivation identical to SAME IIR version — the negation-through-int32
 * pattern is the same at both stages.
 * Uses isolated d* offsets (40/42) so tap_d reads don't collide with
 * tap_prev reads.
 * --------------------------------------------------------------------- */
static void test_diff_iir_anomaly_INT16_MIN_result_clamps_to_MAX(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, /*mLDIFF=*/8, /*mRDIFF=*/12,
                     /*dLDIFF=*/40, /*dRDIFF=*/42);
    buf_write_i16(s, 6,  -1);  /* mLDIFF-2 */
    buf_write_i16(s, 10, -1);  /* mRDIFF-2 */
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, INT16_MAX, INT16_MAX, INT16_MIN, INT16_MAX);

    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MAX, buf_read_i16(s, 8),
        "Pitfall-1 edge: sat_s16(-(INT16_MIN)) = INT16_MAX, no UB");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MAX, buf_read_i16(s, 12),
        "Pitfall-1 edge (R side)");
}

/* ---------------------------------------------------------------------
 * err invariant: exactly-divisible products produce zero remainder.
 * --------------------------------------------------------------------- */
static void test_diff_iir_err_zero_for_non_saturating(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, 8, 12, 4, 6);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x1000, 0x1000, 0x4000, 0);

    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_diff_iir,
        "All four Q15 multiplies exactly divisible by 2^15");
    TEST_ASSERT_EQUAL_INT16(0x800, buf_read_i16(s, 8));
    TEST_ASSERT_EQUAL_INT16(0x800, buf_read_i16(s, 12));
}

/* ---------------------------------------------------------------------
 * err invariant: non-divisible products produce nonzero remainder.
 * --------------------------------------------------------------------- */
static void test_diff_iir_err_nonzero_for_saturating(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, 8, 12, 4, 6);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x1000, 0x1000,
                          (int16_t)(INT16_MIN + 1), 0x4000);

    TEST_ASSERT_MESSAGE(s->err_diff_iir != 0,
        "Truncation remainder must be nonzero for non-divisible products");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(8192, s->err_diff_iir,
        "L iir_prod remainder 4096 + R iir_prod remainder 4096 = 8192");
}

/* ---------------------------------------------------------------------
 * CROSS-SIDE distinguishing test: the feature that makes DIFF IIR
 * structurally different from SAME IIR.
 *
 * Setup:
 *   Registers: mLDIFF=20, mRDIFF=24, dLDIFF=4, dRDIFF=6.
 *   (L writes byte 40; R writes byte 48. L's tap_prev at byte 36;
 *    R's tap_prev at byte 44. L's wall tap via dRDIFF = byte 12;
 *    R's wall tap via dLDIFF = byte 8. All slots isolated.)
 *   Seed: byte 12 = 0x4000 (L's cross-side wall tap only).
 *         Byte 8 (R's tap_d via dLDIFF) stays zero.
 *   Inputs: Lin=Rin=0x2000, vIIR=0x4000, vWALL=0x4000.
 *
 * DIFF IIR derivation:
 *   L side: tap_d (byte 12 via dRDIFF) = 0x4000.
 *     wall_prod = q15_mul(0x4000, 0x4000) = (0x10000000 >> 15) = 0x2000.
 *     acc = sat_s16(0x2000 + 0x2000) = 0x4000.
 *     tap_prev (byte 36) = 0. acc unchanged.
 *     iir_prod = q15_mul(0x4000, 0x4000) = 0x2000.
 *     result = sat_s16(0x2000 + 0) = 0x2000.  -> [mLDIFF] = 0x2000.
 *   R side: tap_d (byte 8 via dLDIFF) = 0 (not seeded).
 *     wall_prod = 0. acc = 0x2000. tap_prev (byte 44) = 0.
 *     iir_prod = q15_mul(0x2000, 0x4000) = 0x1000.
 *     result = 0x1000.  -> [mRDIFF] = 0x1000.
 *
 * The ONLY way L ends up at 0x2000 (vs R at 0x1000) is if L's wall tap
 * was sourced from dRDIFF, not dLDIFF. If the implementation
 * accidentally used dLDIFF for L (i.e. the SAME-IIR pairing), L would
 * read byte 8 (zero) and produce 0x1000 just like R. This test would
 * fail.
 *
 * Hand-verified via derive_reverb_reference.py ref_diff_iir (which
 * implements the cross-side pairing per nocash E1 and would catch a
 * C-side pairing regression).
 * --------------------------------------------------------------------- */
static void test_diff_iir_cross_side_pairing_dRDIFF_feeds_L(void) {
    spu94_state *s = make_state();
    set_diff_regs(s, /*mLDIFF=*/20, /*mRDIFF=*/24,
                     /*dLDIFF=*/4,  /*dRDIFF=*/6);
    /* Seed L's cross-side wall tap (dRDIFF = halfword 6, byte 12). */
    buf_write_i16(s, 6, (int16_t)0x4000);
    s->err_diff_iir = 0;

    spu94_reverb_diff_iir(s, 0x2000, 0x2000, 0x4000, 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x2000, buf_read_i16(s, 20),
        "L depends on dRDIFF tap (cross-side); a same-side pairing "
        "would incorrectly yield 0x1000 here");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1000, buf_read_i16(s, 24),
        "R depends on dLDIFF tap (cross-side); unseeded, so normal "
        "non-cross math produces 0x1000");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_diff_iir_anomaly_vIIR_INT16_MIN_negates);
    RUN_TEST(test_diff_iir_control_vIIR_INT16_MIN_plus_1_no_negate);
    RUN_TEST(test_diff_iir_control_vIIR_INT16_MAX_no_negate);
    RUN_TEST(test_diff_iir_anomaly_INT16_MIN_result_clamps_to_MAX);
    RUN_TEST(test_diff_iir_err_zero_for_non_saturating);
    RUN_TEST(test_diff_iir_err_nonzero_for_saturating);
    RUN_TEST(test_diff_iir_cross_side_pairing_dRDIFF_feeds_L);
    return UNITY_END();
}
