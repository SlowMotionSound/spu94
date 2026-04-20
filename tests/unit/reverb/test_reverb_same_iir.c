/* tests/unit/reverb/test_reverb_same_iir.c — Phase 3 Plan 02 Task 3
 *
 * CORE-05 (SAME IIR stage bit-exactness) + CORE-08 (vIIR=-0x8000 anomaly)
 * + TEST-06 (dedicated anomaly + non-anomaly controls).
 *
 * Expected values are derived via tests/python/derive_reverb_reference.py
 * (Pitfall 9: no GPL emulator in the derivation chain). The derivation
 * for each row is shown in the case's comment — hand-traced against
 * nocash E1 L and E1 R lines:
 *   [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]
 *   [mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]
 *
 * Register layout used by every test (buffer_address==0 post-init):
 *   mLSAME = 8  -> write at byte 16;  mLSAME-2 = 6  -> tap_prev at byte 12
 *   mRSAME = 12 -> write at byte 24;  mRSAME-2 = 10 -> tap_prev at byte 20
 *   dLSAME = 4  -> tap_d at byte 8;   dRSAME = 6   -> tap_d at byte 12
 * (No overlap between the L-write and R's reads; L's tap_prev and R's
 * tap_d share byte 12 for a read-read, never a read-after-write.)
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
    /* Zero the work buffer every time — each test starts clean. */
    for (size_t i = 0; i < sizeof(g_work_buf); ++i) g_work_buf[i] = 0;
    spu94_state *s = spu94_init(g_state_buf, sizeof(g_state_buf),
                                g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

/* Stage the d/m delay/memory registers via public API (TICK_LATCHED
 * policy); flush pending writes so spu94_get_reg_u16 inside the reverb
 * stage returns the values we set. Without this the IIR stage would
 * read the zero-initialized shadow values. */
static void set_same_regs(spu94_state *s,
                          uint16_t mLSAME, uint16_t mRSAME,
                          uint16_t dLSAME, uint16_t dRSAME)
{
    spu94_set_reg_u16(s, SPU94_REG_mLSAME, mLSAME);
    spu94_set_reg_u16(s, SPU94_REG_mRSAME, mRSAME);
    spu94_set_reg_u16(s, SPU94_REG_dLSAME, dLSAME);
    spu94_set_reg_u16(s, SPU94_REG_dRSAME, dRSAME);
    spu94_apply_pending_writes(s);
}

/* Read back a signed 16-bit halfword from the work buffer at
 * (buffer_address + halfword_idx * 2) & 0x7FFFE, little-endian. Mirrors
 * reverb_buf_read's semantics so the test reads what the stage wrote. */
static int16_t buf_read_i16(const spu94_state *s, uint16_t halfword_idx) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    return (int16_t)((uint16_t)s->work_buf[off]
                   | ((uint16_t)s->work_buf[off + 1u] << 8));
}

/* Write a signed 16-bit halfword into the work buffer (for seeding
 * initial state before driving the stage). */
static void buf_write_i16(spu94_state *s, uint16_t halfword_idx, int16_t v) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    uint16_t u = (uint16_t)v;
    s->work_buf[off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}

/* ---------------------------------------------------------------------
 * CORE-08 vIIR=INT16_MIN anomaly: final result gets negated.
 *
 * Derivation (L side; R mirrors exactly):
 *   Lin=0x1000, vWALL=0x4000, vIIR=INT16_MIN. init buf all zero.
 *   tap_d=0, tap_prev=0. wall_prod = q15_mul(0, 0x4000) = 0, err=0.
 *   acc = sat_s16(0x1000 + 0) = 0x1000.
 *   acc = sat_s16(0x1000 + sat_s16(-0)) = 0x1000.
 *   iir_prod = q15_mul(0x1000, INT16_MIN) =
 *     sat_s16((0x1000 * -0x8000) >> 15) = sat_s16(-0x1000) = -0x1000.
 *     remainder = 0 (product exactly divisible by 2^15).
 *   result = sat_s16(-0x1000 + 0) = -0x1000.
 *   Anomaly (vIIR == INT16_MIN): result = sat_s16(-(int32_t)(-0x1000)) = +0x1000.
 *   -> [mLSAME] = +0x1000.  (mirror: [mRSAME] = +0x1000.)
 * --------------------------------------------------------------------- */
static void test_same_iir_anomaly_vIIR_INT16_MIN_negates(void) {
    spu94_state *s = make_state();
    set_same_regs(s, /*mLSAME=*/8, /*mRSAME=*/12,
                     /*dLSAME=*/4, /*dRSAME=*/6);
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, 0x1000, 0x1000, INT16_MIN, 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1000, buf_read_i16(s, 8),
        "vIIR=INT16_MIN anomaly: [mLSAME] should be negated "
        "(-0x1000 -> +0x1000)");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x1000, buf_read_i16(s, 12),
        "vIIR=INT16_MIN anomaly: [mRSAME] should be negated");
    /* No truncation remainder in this derivation. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_same_iir,
        "All multiplies in this row have zero truncation remainder");
}

/* ---------------------------------------------------------------------
 * TEST-06 control case: vIIR = INT16_MIN + 1 does NOT trigger anomaly.
 *
 * Derivation (L side; R mirrors):
 *   Same inputs as the anomaly test except vIIR = -0x7FFF.
 *   iir_prod = q15_mul(0x1000, -0x7FFF) =
 *     sat_s16((0x1000 * -0x7FFF) >> 15).
 *     product = 0x1000 * -0x7FFF = -0x7FFF000.
 *     floor(-0x7FFF000 / 0x8000) = -0x1000 (ASR per ADR-0001).
 *     remainder = -0x7FFF000 - (-0x1000 << 15) = 0x1000  (4096).
 *     sat_s16(-0x1000) = -0x1000.
 *   result = sat_s16(-0x1000 + 0) = -0x1000.
 *   No anomaly (vIIR != INT16_MIN): [mLSAME] = -0x1000.
 *   (Note the result equals the pre-anomaly value of the anomaly test,
 *    but the sign is opposite — that is the whole point of the control:
 *    it proves the anomaly branch is specifically keyed on INT16_MIN,
 *    not on "any large-magnitude negative vIIR".)
 *   Per-side err contribution: 4096 (iir_prod remainder) + 0 (wall). L+R total = 8192.
 * --------------------------------------------------------------------- */
static void test_same_iir_control_vIIR_INT16_MIN_plus_1_no_negate(void) {
    spu94_state *s = make_state();
    set_same_regs(s, 8, 12, 4, 6);
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, 0x1000, 0x1000,
                          (int16_t)(INT16_MIN + 1), 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x1000, buf_read_i16(s, 8),
        "vIIR=INT16_MIN+1 control: [mLSAME] should NOT be negated");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x1000, buf_read_i16(s, 12),
        "vIIR=INT16_MIN+1 control: [mRSAME] should NOT be negated");
    /* Control sanity: sign is OPPOSITE of the anomaly test above. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(8192, s->err_same_iir,
        "Two iir_prod truncation remainders (4096 each for L and R)");
}

/* ---------------------------------------------------------------------
 * Second control: vIIR = INT16_MAX does NOT trigger anomaly.
 * Proves the anomaly is specifically INT16_MIN, not "any extreme value".
 *
 * Derivation:
 *   vIIR = +0x7FFF, rest identical.
 *   iir_prod = q15_mul(0x1000, 0x7FFF):
 *     product = 0x1000 * 0x7FFF = 0x7FFF000  (134213632).
 *     shifted = product >> 15 = 0xFFF  (floor division: 4095.875 -> 4095).
 *     remainder = product - (shifted << 15) = 0x7FFF000 - 0x7FF8000
 *               = 0x7000 = 28672.
 *     sat_s16(0xFFF) = 0xFFF.
 *   result = sat_s16(0xFFF + 0) = 0xFFF.
 *   No anomaly. [mLSAME] = [mRSAME] = 0xFFF.
 *   err total = 2 * 28672 = 57344 (L + R iir_prod truncation remainders).
 * --------------------------------------------------------------------- */
static void test_same_iir_control_vIIR_INT16_MAX_no_negate(void) {
    spu94_state *s = make_state();
    set_same_regs(s, 8, 12, 4, 6);
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, 0x1000, 0x1000, INT16_MAX, 0x4000);

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x0FFF, buf_read_i16(s, 8),
        "vIIR=INT16_MAX control: [mLSAME] normal positive multiply");
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x0FFF, buf_read_i16(s, 12),
        "vIIR=INT16_MAX control: [mRSAME] normal positive multiply");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(57344, s->err_same_iir,
        "Two iir_prod truncation remainders (0x7000 each for L and R)");
}

/* ---------------------------------------------------------------------
 * Pitfall-1 edge: pre-anomaly result == INT16_MIN. The anomaly branch
 * widens through int32 before negating, so `-(-INT16_MIN)` = +0x8000
 * saturates to INT16_MAX rather than triggering UB.
 *
 * Derivation (using isolated d* offsets to keep L/R reads non-overlapping):
 *   mLSAME=8 (write byte 16); mLSAME-2=6 (tap_prev byte 12)
 *   mRSAME=12 (write byte 24); mRSAME-2=10 (tap_prev byte 20)
 *   dLSAME=40 (tap_d byte 80); dRSAME=42 (tap_d byte 84). Clean isolation.
 *   Seed buf: byte 12 = -1 (L's tap_prev); byte 20 = -1 (R's tap_prev).
 *   Inputs: Lin=Rin=INT16_MAX, vWALL=INT16_MAX, vIIR=INT16_MIN.
 *
 *   L side:
 *     tap_d = 0 (byte 80 unseeded).
 *     wall_prod = q15_mul(0, INT16_MAX) = 0.
 *     acc = sat_s16(INT16_MAX + 0) = INT16_MAX.
 *     acc = sat_s16(INT16_MAX + sat_s16(-(-1))) = sat_s16(INT16_MAX + 1)
 *         = INT16_MAX.
 *     iir_prod = q15_mul(INT16_MAX, INT16_MIN)
 *              = sat_s16((0x7FFF * -0x8000) >> 15)
 *              = sat_s16(-0x3FFF8000 >> 15)
 *              = sat_s16(-0x7FFF) = -0x7FFF.  remainder=0.
 *     result = sat_s16(-0x7FFF + (-1)) = sat_s16(-0x8000) = INT16_MIN.
 *     Anomaly: result = sat_s16(-(int32_t)INT16_MIN) = sat_s16(+0x8000)
 *            = INT16_MAX.
 *   R mirrors: [mRSAME] = INT16_MAX.
 * --------------------------------------------------------------------- */
static void test_same_iir_anomaly_INT16_MIN_result_clamps_to_MAX(void) {
    spu94_state *s = make_state();
    set_same_regs(s, /*mLSAME=*/8, /*mRSAME=*/12,
                     /*dLSAME=*/40, /*dRSAME=*/42);
    /* Seed tap_prev slots with -1 for both L and R. */
    buf_write_i16(s, 6,  -1);  /* mLSAME-2 */
    buf_write_i16(s, 10, -1);  /* mRSAME-2 */
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, INT16_MAX, INT16_MAX, INT16_MIN, INT16_MAX);

    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MAX, buf_read_i16(s, 8),
        "Pitfall-1 edge: sat_s16(-(INT16_MIN)) = INT16_MAX, no UB");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(INT16_MAX, buf_read_i16(s, 12),
        "Pitfall-1 edge (R side)");
}

/* ---------------------------------------------------------------------
 * err invariant: non-saturating, exactly-divisible-by-2^15 inputs leave
 * err_same_iir at zero. Uses vWALL=0 so the wall multiplies produce
 * product=0 exactly (no remainder), and vIIR=0x4000 × Lin=0x1000 so the
 * iir multiplies have product=0x4000000 -> shifted=0x800 with zero
 * remainder. Both sides contribute zero remainder.
 * --------------------------------------------------------------------- */
static void test_same_iir_err_zero_for_non_saturating(void) {
    spu94_state *s = make_state();
    set_same_regs(s, 8, 12, 4, 6);
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, 0x1000, 0x1000, 0x4000, 0);

    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_same_iir,
        "All four Q15 multiplies are exactly-divisible by 2^15; "
        "err_same_iir must stay zero");
    /* Also cross-check the actual stage output against the
     * hand-derivation: Lin=0x1000, vWALL=0 -> wall_prod=0; acc=0x1000;
     * tap_prev=0 -> acc unchanged; iir_prod = q15_mul(0x1000, 0x4000)
     * = (0x4000000 >> 15) = 0x800; result = sat_s16(0x800 + 0) = 0x800. */
    TEST_ASSERT_EQUAL_INT16(0x800, buf_read_i16(s, 8));
    TEST_ASSERT_EQUAL_INT16(0x800, buf_read_i16(s, 12));
}

/* ---------------------------------------------------------------------
 * err invariant: non-divisible inputs produce nonzero err_same_iir.
 * Inputs chosen so no result saturates (so every multiply's truncation
 * remainder is recoverable without confounding by saturation discard).
 * vIIR=INT16_MIN+1 used to match the control row's err=8192 derivation
 * (L iir_prod remainder 4096 + R iir_prod remainder 4096).
 * --------------------------------------------------------------------- */
static void test_same_iir_err_nonzero_for_saturating(void) {
    spu94_state *s = make_state();
    set_same_regs(s, 8, 12, 4, 6);
    s->err_same_iir = 0;

    spu94_reverb_same_iir(s, 0x1000, 0x1000,
                          (int16_t)(INT16_MIN + 1), 0x4000);

    TEST_ASSERT_MESSAGE(s->err_same_iir != 0,
        "Truncation remainder must be nonzero for non-divisible products");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(8192, s->err_same_iir,
        "L iir_prod remainder 4096 + R iir_prod remainder 4096 = 8192");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_same_iir_anomaly_vIIR_INT16_MIN_negates);
    RUN_TEST(test_same_iir_control_vIIR_INT16_MIN_plus_1_no_negate);
    RUN_TEST(test_same_iir_control_vIIR_INT16_MAX_no_negate);
    RUN_TEST(test_same_iir_anomaly_INT16_MIN_result_clamps_to_MAX);
    RUN_TEST(test_same_iir_err_zero_for_non_saturating);
    RUN_TEST(test_same_iir_err_nonzero_for_saturating);
    return UNITY_END();
}
