/* tests/unit/reverb/test_reverb_edges.c — Phase 3 Plan 04 Task 1
 *
 * TEST-07 acceptance (ROADMAP Phase 3 SC-4): Q15 saturation, truncation,
 * and signed-overflow edges at every stage. Each sub-test maps to a
 * specific Pitfall in 03-RESEARCH.md or to a specific ADR that Phase 3's
 * reverb network depends on.
 *
 * Cross-stage re-assertion: the primitive Q15 ADR-0001 semantic is already
 * covered by tests/unit/q15/test_q15.c; this TU re-asserts it at the
 * stage level to catch a future refactor where a stage accidentally uses
 * a different multiply helper (e.g. a rounding variant).
 *
 * Pitfall map:
 *   Pitfall 1  -> INT16_MIN-negation UB guards (APF subtracts, vIIR anomaly)
 *   Pitfall 7  -> APF feedback edge at INT16_MIN (re-assertion here; primary
 *                 in test_reverb_apf{1,2}.c)
 *   Pitfall 8  -> buffer_address unchanged across reverb_body
 *   ADR-0001   -> Q15 truncation direction toward -inf (ASR)
 *   D-07       -> comb cascading-sat mirror case
 *   D-11       -> per-stage err accumulator invariants
 */
#include "unity.h"
#include "../../../src/spu94/spu94_reverb_internal.h"
#include "../../../src/spu94/spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_q15.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <limits.h>
#include <stdalign.h>
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

/* White-box halfword write mirroring reverb_buf_write semantics. */
static void buf_write_i16(spu94_state *s, uint16_t halfword_idx, int16_t v) {
    uint32_t off = (s->buffer_address + (uint32_t)halfword_idx * 2u) & 0x7FFFEu;
    uint16_t u = (uint16_t)v;
    s->work_buf[off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}

/* ===================================================================
 * ADR-0001: Q15 truncation toward -inf re-asserted at output_scale.
 *
 * Lout = 1, vLOUT = -1. Full product = -1. ASR(-1 >> 15) = -1 (toward
 * -inf), NOT 0 (toward zero). sat_s16(-1) = -1. So LeftOutput = -1.
 * If a future refactor swapped the stage's multiply helper to a
 * round-toward-zero variant, LeftOutput would be 0 and this test fails.
 * =================================================================== */
static void test_output_scale_truncation_direction(void) {
    spu94_state *s = make_state();
    int32_t LO = 0x7BADu, RO = 0x7BADu;
    spu94_reverb_output_scale(s, (int16_t)1, (int16_t)1,
                              (int16_t)-1, (int16_t)-1, &LO, &RO);
    TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)-1, LO,
        "ADR-0001 ASR: -1 >> 15 = -1 (toward -inf), not 0 (toward zero)");
    TEST_ASSERT_EQUAL_INT32((int32_t)-1, RO);
    /* err_output_scale captures the truncation remainder:
     * rem = product - (shifted<<15) = -1 - (-1 << 15) = -1 + 32768 = 32767.
     * L+R = 65534. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(65534, s->err_output_scale,
        "Truncation remainder: 32767 per multiply x 2 sides");
}

/* ===================================================================
 * INT16_MIN^2 saturation — output_scale
 *
 * q15_mul(INT16_MIN, INT16_MIN) saturates to INT16_MAX (ADR-0001).
 * Both sides driven to INT16_MIN -> LeftOutput/RightOutput = INT16_MAX.
 * =================================================================== */
static void test_INT16_MIN_squared_saturates_in_output_scale(void) {
    spu94_state *s = make_state();
    int32_t LO = 0, RO = 0;
    spu94_reverb_output_scale(s, INT16_MIN, INT16_MIN,
                              INT16_MIN, INT16_MIN, &LO, &RO);
    TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)INT16_MAX, LO,
        "INT16_MIN^2 -> sat to INT16_MAX per ADR-0001");
    TEST_ASSERT_EQUAL_INT32((int32_t)INT16_MAX, RO);
    /* Product 2^30 is exactly divisible by 2^15; err = 0. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_output_scale,
        "INT16_MIN^2 product is exactly divisible by 2^15; err = 0");
}

/* ===================================================================
 * INT16_MIN^2 saturation — same_iir vWALL multiply
 *
 * Drive with vWALL = INT16_MIN, tap_d = INT16_MIN (seeded in work_buf at
 * offset dLSAME). All other inputs zero. The wall_prod = INT16_MAX
 * (saturated). With Lin=0, vIIR=0, iir_prod=0, result=iir_prod+tap_prev=0.
 * The critical assertion: no UB at the vWALL multiply, err=0 (INT16_MIN^2
 * is exactly divisible by 2^15), and the stage runs to completion.
 * =================================================================== */
static void test_INT16_MIN_squared_saturates_in_same_iir_vWALL(void) {
    spu94_state *s = make_state();
    /* dLSAME=4 -> tap_d reads byte offset (0 + 4*2) & mask = 8.
     * mLSAME=6 -> write byte 12; mLSAME-2=4 -> tap_prev at byte 8, same
     * as tap_d. Avoid overlap: use dLSAME=4, mLSAME=8 so mLSAME-2=6,
     * tap_prev at byte 12 (distinct from tap_d at byte 8). */
    spu94_set_reg_u16(s, SPU94_REG_dLSAME, 4);   /* tap_d at halfword 4 -> byte 8 */
    spu94_set_reg_u16(s, SPU94_REG_mLSAME, 8);   /* write at halfword 8 -> byte 16; tap_prev at halfword 6 -> byte 12 */
    spu94_set_reg_u16(s, SPU94_REG_dRSAME, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRSAME, 14);
    spu94_apply_pending_writes(s);

    buf_write_i16(s, 4, INT16_MIN);  /* tap_d (L) */
    /* tap_prev at halfword 6 stays 0. */

    s->err_same_iir = 0;
    /* vWALL = INT16_MIN, vIIR = 0, Lin = Rin = 0. */
    spu94_reverb_same_iir(s, 0, 0, /*vIIR=*/0, /*vWALL=*/INT16_MIN);
    /* wall_prod = q15_mul(INT16_MIN, INT16_MIN) = INT16_MAX, err=0.
     * acc = sat(0 + 0x7FFF) = 0x7FFF. acc = sat(0x7FFF + sat(-0)) = 0x7FFF.
     * iir_prod = q15_mul(0x7FFF, 0) = 0.
     * result = sat(0 + 0) = 0.
     * vIIR != INT16_MIN, so no anomaly. Store 0 at [mLSAME].
     * err_same_iir = 0 (wall) + 0 (iir) + 0 (R side all zero) = 0. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_same_iir,
        "INT16_MIN^2 at vWALL: product exactly divisible; err remains 0");
    /* Main assertion: the call completed without UB. Implicit — UBSan
     * would have aborted if INT16_MIN^2 had triggered signed overflow. */
}

/* ===================================================================
 * INT16_MIN^2 saturation — comb (all 4 taps per side at INT16_MIN)
 *
 * v=(INT16_MIN,INT16_MIN,INT16_MIN,INT16_MIN), taps all INT16_MIN.
 * Each multiply q15_mul(INT16_MIN, INT16_MIN) = INT16_MAX, err=0.
 * Cascade: acc=0x7FFF; sat(0x7FFF+0x7FFF)=0x7FFF (x3).
 * Lout = Rout = INT16_MAX; err_comb = 0.
 * =================================================================== */
static void test_INT16_MIN_squared_saturates_in_comb(void) {
    spu94_state *s = make_state();
    /* Use distinct byte slots for the 8 taps. */
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB1, 2);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB3, 6);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB4, 8);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB1, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB2, 12);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB3, 14);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB4, 16);
    spu94_apply_pending_writes(s);
    for (uint16_t h = 2; h <= 16; h += 2) buf_write_i16(s, h, INT16_MIN);

    s->err_comb = 0;
    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN,
                      &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)INT16_MAX, Lout,
        "All 4 products saturate to INT16_MAX; cascade stays at INT16_MAX");
    TEST_ASSERT_EQUAL_INT16((int16_t)INT16_MAX, Rout);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_comb,
        "INT16_MIN^2 products are exactly divisible; err = 0");
}

/* ===================================================================
 * INT16_MIN^2 saturation — apf1 (Pitfall-7 compound case)
 *
 * Lin = Rin = vAPF1 = tap_delayed = INT16_MIN. Derivation mirrors
 * test_apf1_pitfall_7_INT16_MIN_triple in test_reverb_apf1.c:
 *   prod1 = q15_mul(INT16_MIN, INT16_MIN) = INT16_MAX, err=0
 *   step1 = sat(INT16_MIN + sat(-INT16_MAX)) = sat(INT16_MIN + -INT16_MAX)
 *         = sat(-0xFFFF) = INT16_MIN (Pitfall-1 guard keeps it well-defined)
 *   [mLAPF1] = INT16_MIN
 *   prod2 = q15_mul(INT16_MIN, INT16_MIN) = INT16_MAX
 *   step3 = sat(INT16_MAX + INT16_MIN) = -1
 * Lout = -1. This is the TEST-07 re-assertion at the APF1 stage.
 * =================================================================== */
static void test_INT16_MIN_squared_saturates_in_apf1(void) {
    spu94_state *s = make_state();
    spu94_set_reg_u16(s, SPU94_REG_mLAPF1, 4);
    spu94_set_reg_u16(s, SPU94_REG_mRAPF1, 16);
    spu94_apply_pending_writes(s);
    buf_write_i16(s, 2,  INT16_MIN);   /* L tap_delayed (mLAPF1 - dAPF1 = 4-2 = 2) */
    buf_write_i16(s, 14, INT16_MIN);   /* R tap_delayed (mRAPF1 - dAPF1 = 16-2 = 14) */

    s->err_apf1 = 0;
    int16_t Lout = INT16_MIN, Rout = INT16_MIN;
    spu94_reverb_apf1(s, /*vAPF1=*/INT16_MIN, /*dAPF1=*/2, &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-1, Lout,
        "Pitfall-1 + Pitfall-7 compound: Lout stays bounded at -1");
    TEST_ASSERT_EQUAL_INT16((int16_t)-1, Rout);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_apf1,
        "All 4 multiplies are INT16_MIN^2 -> err = 0");
}

/* ===================================================================
 * INT16_MIN^2 saturation — apf2 (structural mirror of apf1)
 * =================================================================== */
static void test_INT16_MIN_squared_saturates_in_apf2(void) {
    spu94_state *s = make_state();
    spu94_set_reg_u16(s, SPU94_REG_mLAPF2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mRAPF2, 16);
    spu94_apply_pending_writes(s);
    buf_write_i16(s, 2,  INT16_MIN);
    buf_write_i16(s, 14, INT16_MIN);

    s->err_apf2 = 0;
    int16_t Lout = INT16_MIN, Rout = INT16_MIN;
    spu94_reverb_apf2(s, /*vAPF2=*/INT16_MIN, /*dAPF2=*/2, &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT16((int16_t)-1, Lout);
    TEST_ASSERT_EQUAL_INT16((int16_t)-1, Rout);
    TEST_ASSERT_EQUAL_INT32(0, s->err_apf2);
}

/* ===================================================================
 * D-07 cascading-sat mirror case.
 *
 * Plan 03 Task 3's distinguishing test already pins the D-07 cascading
 * shape at v=(0x7FFF,0x7FFF,-0x7FFF,-0x7FFF). The mirror case reverses
 * the sign pattern: v=(-0x7FFF,-0x7FFF,0x7FFF,0x7FFF), all taps 0x7FFF.
 *
 *   p1 = p2 = q15_mul(-0x7FFF, 0x7FFF) = -0x7FFF (INT16_MIN+1), err=0x7FFF
 *   p3 = p4 = q15_mul( 0x7FFF, 0x7FFF) = 0x7FFE, err=1
 *
 * Cascading (D-07):
 *   acc = p1 = -0x7FFF
 *   acc = sat(-0x7FFF + -0x7FFF) = sat(-0xFFFE) = -0x8000   (sat #1; floor clamp)
 *   acc = sat(-0x8000 + 0x7FFE)  = -2
 *   acc = sat(-2 + 0x7FFE)       = 0x7FFC
 *   Lout = 0x7FFC
 *
 * Int32-accumulate (REJECTED):
 *   sum = -0x7FFF + -0x7FFF + 0x7FFE + 0x7FFE = -2
 *   sat_s16(-2) = -2
 * If the stage regressed to int32-accumulate, Lout would be -2 not 0x7FFC.
 * The gap is 32766 — any drift to the rejected variant fails loudly.
 *
 * Per-side err = 0x7FFF + 0x7FFF + 1 + 1 = 65536. L+R = 131072.
 * =================================================================== */
static void test_comb_cascading_sat_mirror_case(void) {
    spu94_state *s = make_state();
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB1, 2);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB3, 6);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB4, 8);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB1, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB2, 12);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB3, 14);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB4, 16);
    spu94_apply_pending_writes(s);
    for (uint16_t h = 2; h <= 16; h += 2) buf_write_i16(s, h, (int16_t)0x7FFF);

    s->err_comb = 0;
    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(s, (int16_t)-0x7FFF, (int16_t)-0x7FFF,
                         (int16_t) 0x7FFF, (int16_t) 0x7FFF, &Lout, &Rout);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFC, Lout,
        "D-07 cascading mirror: cascade reaches floor clamp then recovers "
        "to 0x7FFC. Int32-accumulate would give -2.");
    TEST_ASSERT_EQUAL_INT16((int16_t)0x7FFC, Rout);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(131072, s->err_comb,
        "Per-side err = 0x7FFF*2 + 1*2 = 65536; L+R = 131072");
}

/* ===================================================================
 * vIIR = INT16_MIN anomaly + INT16_MIN-negation guard (Pitfall 1).
 *
 * Re-assertion of the primary Plan 02 Task 3 test at the TEST-07 level.
 * Constructs state where the pre-anomaly result is -0x8000, forcing
 * sat_s16(-(int32_t)INT16_MIN) = sat_s16(+0x8000) = INT16_MAX. The
 * implementation-side `sat_s16(-(int32_t)result)` pattern keeps the
 * negation well-defined for the INT16_MIN edge.
 *
 * Setup: vIIR=INT16_MIN, vWALL=INT16_MIN, tap_d=INT16_MIN,
 * tap_prev=INT16_MIN, Lin=INT16_MIN.
 *   wall_prod = q15_mul(INT16_MIN, INT16_MIN) = 0x7FFF, err=0
 *   acc = sat(INT16_MIN + 0x7FFF) = -1
 *   acc = sat(-1 + sat(-INT16_MIN)) = sat(-1 + 0x7FFF) = 0x7FFE
 *   iir_prod = q15_mul(0x7FFE, INT16_MIN): product = -0x3FFF0000,
 *              shifted = -0x7FFE (ASR), sat = -0x7FFE, err=0
 *   result = sat(-0x7FFE + INT16_MIN) = sat(-0xFFFE) = -0x8000
 *   Anomaly: result = sat_s16(-(int32_t)(-0x8000)) = sat_s16(0x8000)
 *          = 0x7FFF  (INT16_MAX)
 * Asserted via the buffer-written [mLSAME] value.
 * =================================================================== */
static void test_vIIR_anomaly_INT16_MIN_negation_guard(void) {
    spu94_state *s = make_state();
    /* Setup: dLSAME=4, mLSAME=8 so tap_d at halfword 4 (byte 8) and
     * tap_prev at halfword (mLSAME-2)=6 (byte 12); write dest is
     * halfword 8 (byte 16). All distinct. Mirror for R side. */
    spu94_set_reg_u16(s, SPU94_REG_dLSAME, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLSAME, 8);
    spu94_set_reg_u16(s, SPU94_REG_dRSAME, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRSAME, 14);
    spu94_apply_pending_writes(s);

    buf_write_i16(s, 4,  INT16_MIN);  /* tap_d L */
    buf_write_i16(s, 6,  INT16_MIN);  /* tap_prev L (mLSAME-2=6) */
    buf_write_i16(s, 10, INT16_MIN);  /* tap_d R */
    buf_write_i16(s, 12, INT16_MIN);  /* tap_prev R (mRSAME-2=12) */

    s->err_same_iir = 0;
    spu94_reverb_same_iir(s, INT16_MIN, INT16_MIN,
                          /*vIIR=*/INT16_MIN, /*vWALL=*/INT16_MIN);
    /* Read back [mLSAME=8] at byte offset 16 and [mRSAME=14] at byte 28. */
    uint32_t off_L = 8u * 2u;
    int16_t stored_L = (int16_t)((uint16_t)s->work_buf[off_L]
                       | ((uint16_t)s->work_buf[off_L + 1u] << 8));
    uint32_t off_R = 14u * 2u;
    int16_t stored_R = (int16_t)((uint16_t)s->work_buf[off_R]
                       | ((uint16_t)s->work_buf[off_R + 1u] << 8));

    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)INT16_MAX, stored_L,
        "D-10 anomaly + Pitfall-1 guard: sat_s16(-INT16_MIN) = INT16_MAX");
    TEST_ASSERT_EQUAL_INT16((int16_t)INT16_MAX, stored_R);
}

/* ===================================================================
 * Pitfall 1 — INT16_MIN tap subtraction.
 *
 * The SAME IIR has `acc - tap_prev`. When tap_prev = INT16_MIN, the
 * naive `-tap_prev` would be UB. The implementation uses
 * `sat_s16(-(int32_t)tap_prev)` which evaluates to INT16_MAX.
 *
 * Setup: Lin=0, vWALL=0, tap_prev=INT16_MIN, tap_d=0, vIIR=0x4000.
 *   wall_prod = q15_mul(0, 0) = 0
 *   acc = sat(0 + 0) = 0
 *   acc = sat(0 + sat(-INT16_MIN)) = sat(0 + INT16_MAX) = INT16_MAX
 *   iir_prod = q15_mul(INT16_MAX, 0x4000) = (0x7FFF * 0x4000)>>15
 *            = (0x1FFFC000)>>15 = 0x3FFF, err = 0x4000  (rem = 0x20000000)
 *   Wait: 0x7FFF * 0x4000 = 0x1FFFC000. ASR >>15 = 0x3FFF. rem = 0x1FFFC000 - (0x3FFF<<15)
 *                                                             = 0x1FFFC000 - 0x1FFF8000 = 0x4000.
 *   result = sat(0x3FFF + INT16_MIN) = sat(-0x4001) = -0x4001
 *   vIIR != INT16_MIN -> no anomaly. Store -0x4001.
 * =================================================================== */
static void test_INT16_MIN_tap_subtraction_pitfall1(void) {
    spu94_state *s = make_state();
    spu94_set_reg_u16(s, SPU94_REG_dLSAME, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLSAME, 8);
    spu94_set_reg_u16(s, SPU94_REG_dRSAME, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRSAME, 14);
    spu94_apply_pending_writes(s);

    buf_write_i16(s, 6,  INT16_MIN);   /* L tap_prev */
    buf_write_i16(s, 12, INT16_MIN);   /* R tap_prev */

    s->err_same_iir = 0;
    spu94_reverb_same_iir(s, 0, 0, /*vIIR=*/(int16_t)0x4000, /*vWALL=*/0);

    uint32_t off_L = 8u * 2u;
    int16_t stored_L = (int16_t)((uint16_t)s->work_buf[off_L]
                       | ((uint16_t)s->work_buf[off_L + 1u] << 8));
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-0x4001, stored_L,
        "Pitfall-1 guard: sat_s16(-(int32_t)INT16_MIN) = INT16_MAX keeps "
        "the subtraction well-defined; downstream result = -0x4001");
    /* err_same_iir per side: wall_prod rem = 0, iir_prod rem = 0x4000.
     * L+R = 2 * 0x4000 = 0x8000 = 32768. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(32768, s->err_same_iir,
        "iir_prod truncation rem = 0x4000 per side; L+R = 0x8000");
}

/* ===================================================================
 * err invariants — zero for non-saturating / zero input.
 *
 * All inputs zero, all registers zero, empty work_buf -> every err_*
 * field is zero after a full reverb_body call.
 * =================================================================== */
static void test_err_zero_for_non_saturating_all_stages(void) {
    spu94_state *s = make_state();
    /* All registers default to 0 post-init. */
    s->err_input_scale = 0;
    s->err_same_iir = 0;
    s->err_diff_iir = 0;
    s->err_comb = 0;
    s->err_apf1 = 0;
    s->err_apf2 = 0;
    s->err_output_scale = 0;
    s->overflow_magnitude = 0;

    spu94_reverb_body(s);

    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_input_scale,  "err_input_scale");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_same_iir,     "err_same_iir");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_diff_iir,     "err_diff_iir");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_comb,         "err_comb");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_apf1,         "err_apf1");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_apf2,         "err_apf2");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->err_output_scale, "err_output_scale");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, s->overflow_magnitude, "overflow_magnitude");
}

/* ===================================================================
 * err invariants — accumulates monotonically across two comb calls
 * under saturating input (D-07 distinguishing-case pattern).
 *
 * Existing test_comb_err_accumulates already pins this at stage level;
 * this re-asserts at the TEST-07 battery level with the specific
 * cross-stage invariant that err_comb grows >= 0 between calls.
 * =================================================================== */
static void test_err_accumulates_for_saturating_input(void) {
    spu94_state *s = make_state();
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB1, 2);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB2, 4);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB3, 6);
    spu94_set_reg_u16(s, SPU94_REG_mLCOMB4, 8);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB1, 10);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB2, 12);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB3, 14);
    spu94_set_reg_u16(s, SPU94_REG_mRCOMB4, 16);
    spu94_apply_pending_writes(s);
    for (uint16_t h = 2; h <= 16; h += 2) buf_write_i16(s, h, (int16_t)0x7FFF);

    s->err_comb = 0;
    int16_t Lout = 0, Rout = 0;

    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF,
                         (int16_t)-0x7FFF, (int16_t)-0x7FFF, &Lout, &Rout);
    int32_t err_after_1 = s->err_comb;
    TEST_ASSERT_MESSAGE(err_after_1 > 0, "err_comb grows under saturating input");

    spu94_reverb_comb(s, (int16_t)0x7FFF, (int16_t)0x7FFF,
                         (int16_t)-0x7FFF, (int16_t)-0x7FFF, &Lout, &Rout);
    int32_t err_after_2 = s->err_comb;
    TEST_ASSERT_MESSAGE(err_after_2 > err_after_1,
        "err_comb is monotonic across calls under saturating input");
}

/* ===================================================================
 * Pitfall 8 — buffer_address unchanged across reverb_body.
 *
 * reverb_body runs the reverb network but MUST NOT call buffer_advance
 * (that is Phase 2 Plan 04's job, invoked separately by spu94_tick).
 * Snapshot buffer_address before and after; they MUST be identical.
 * =================================================================== */
static void test_buffer_address_unchanged_across_reverb_body(void) {
    spu94_state *s = make_state();
    /* Snap mBASE to a non-zero value so buffer_address is not trivially
     * 0, then call reverb_body and assert no drift. */
    spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0x0040);
    /* mBASE is IMMEDIATE + snap-on-write (ADR-0006). No apply_pending needed
     * for an IMMEDIATE register; the snap fires at set_reg time. */

    uint32_t ba_before = s->buffer_address;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)0x0040, ba_before,
        "mBASE snap-on-write: buffer_address should now equal 0x0040");

    spu94_reverb_body(s);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ba_before, s->buffer_address,
        "reverb_body must NOT advance buffer_address (Pitfall 8). "
        "spu94_tick is the single caller of buffer_advance.");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_output_scale_truncation_direction);
    RUN_TEST(test_INT16_MIN_squared_saturates_in_output_scale);
    RUN_TEST(test_INT16_MIN_squared_saturates_in_same_iir_vWALL);
    RUN_TEST(test_INT16_MIN_squared_saturates_in_comb);
    RUN_TEST(test_INT16_MIN_squared_saturates_in_apf1);
    RUN_TEST(test_INT16_MIN_squared_saturates_in_apf2);
    RUN_TEST(test_comb_cascading_sat_mirror_case);
    RUN_TEST(test_vIIR_anomaly_INT16_MIN_negation_guard);
    RUN_TEST(test_INT16_MIN_tap_subtraction_pitfall1);
    RUN_TEST(test_err_zero_for_non_saturating_all_stages);
    RUN_TEST(test_err_accumulates_for_saturating_input);
    RUN_TEST(test_buffer_address_unchanged_across_reverb_body);
    return UNITY_END();
}
