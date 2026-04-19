/* tests/unit/registers/test_register_types.c
 * Phase 2 Plan 05 Task 1: signedness preservation + TYPE_MISMATCH rejection
 * for every register. Covers CONTEXT D-02 (signed/unsigned distinction is
 * structural), D-07 (status code), D-08 (data is bit-faithful regardless of
 * reporting -- a TYPE_MISMATCH is a pure no-op on data).
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_work_buf[1024];
static spu94_state *g_state = (spu94_state *)0;

static void setup_state(void) {
    g_state = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX,
                         g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(g_state);
}

/* Mirror of the table in test_register_roundtrip.c. */
static int reg_is_u16(spu94_reg_t r) {
    switch (r) {
        case SPU94_REG_mBASE:
        case SPU94_REG_dAPF1: case SPU94_REG_dAPF2:
        case SPU94_REG_mLSAME: case SPU94_REG_mRSAME:
        case SPU94_REG_mLCOMB1: case SPU94_REG_mRCOMB1:
        case SPU94_REG_mLCOMB2: case SPU94_REG_mRCOMB2:
        case SPU94_REG_dLSAME: case SPU94_REG_dRSAME:
        case SPU94_REG_mLDIFF: case SPU94_REG_mRDIFF:
        case SPU94_REG_mLCOMB3: case SPU94_REG_mRCOMB3:
        case SPU94_REG_mLCOMB4: case SPU94_REG_mRCOMB4:
        case SPU94_REG_dLDIFF: case SPU94_REG_dRDIFF:
        case SPU94_REG_mLAPF1: case SPU94_REG_mRAPF1:
        case SPU94_REG_mLAPF2: case SPU94_REG_mRAPF2:
            return 1;
        default:
            return 0;
    }
}

/* ---- Test: spu94_reg_type classifier matches expected types per register ---- */

void test_reg_type_classifier(void) {
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        spu94_reg_type_t want = reg_is_u16(r) ? SPU94_REG_TYPE_U16 : SPU94_REG_TYPE_I16;
        spu94_reg_type_t got = spu94_reg_type(r);
        char msg[80];
        snprintf(msg, sizeof(msg), "reg %d (%s) type", i, spu94_reg_name(r));
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)want, (int)got, msg);
    }
}

/* ---- Test: spu94_set_reg_u16 on an i16 register returns TYPE_MISMATCH and
 *           leaves both active and pending storage unchanged ---- */

void test_type_mismatch_u16_on_i16(void) {
    setup_state();
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (reg_is_u16(r)) continue;        /* only i16 registers */

        char msg[80];
        snprintf(msg, sizeof(msg), "reg %d (%s)", i, spu94_reg_name(r));

        /* Read pre-write state for both active and pending. */
        int16_t pre_active = spu94_get_reg_i16(g_state, r);
        int16_t pre_pending = spu94_get_reg_i16_pending(g_state, r);

        /* Wrong-typed write must be rejected. */
        spu94_result_t res = spu94_set_reg_u16(g_state, r, 0x1234u);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_TYPE_MISMATCH, (int)res, msg);

        /* Active and pending must be unchanged. */
        TEST_ASSERT_EQUAL_INT16_MESSAGE(pre_active,
            spu94_get_reg_i16(g_state, r), msg);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(pre_pending,
            spu94_get_reg_i16_pending(g_state, r), msg);
    }
}

/* ---- Test: spu94_set_reg_i16 on a u16 register returns TYPE_MISMATCH and
 *           leaves both active and pending storage unchanged ---- */

void test_type_mismatch_i16_on_u16(void) {
    setup_state();
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (!reg_is_u16(r)) continue;       /* only u16 registers */

        char msg[80];
        snprintf(msg, sizeof(msg), "reg %d (%s)", i, spu94_reg_name(r));

        uint16_t pre_active = spu94_get_reg_u16(g_state, r);
        uint16_t pre_pending = spu94_get_reg_u16_pending(g_state, r);

        spu94_result_t res = spu94_set_reg_i16(g_state, r, (int16_t)123);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_TYPE_MISMATCH, (int)res, msg);

        TEST_ASSERT_EQUAL_UINT16_MESSAGE(pre_active,
            spu94_get_reg_u16(g_state, r), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(pre_pending,
            spu94_get_reg_u16_pending(g_state, r), msg);
    }
}

/* ---- Test: out-of-range register IDs return SPU94_UNKNOWN_REG ---- */

void test_unknown_reg(void) {
    setup_state();
    spu94_result_t r1 = spu94_set_reg_i16(g_state, (spu94_reg_t)99, 0);
    TEST_ASSERT_EQUAL_INT((int)SPU94_UNKNOWN_REG, (int)r1);
    spu94_result_t r2 = spu94_set_reg_u16(g_state, (spu94_reg_t)42, 0);
    TEST_ASSERT_EQUAL_INT((int)SPU94_UNKNOWN_REG, (int)r2);
    /* SPU94_REG__COUNT itself is the sentinel and out of range. */
    spu94_result_t r3 = spu94_set_reg_i16(g_state, SPU94_REG__COUNT, 0);
    TEST_ASSERT_EQUAL_INT((int)SPU94_UNKNOWN_REG, (int)r3);
}

/* ---- Test: i16 preservation at edges (vIIR rep) ---- */

void test_i16_preservation_edges(void) {
    setup_state();
    spu94_result_t res;
    res = spu94_set_reg_i16(g_state, SPU94_REG_vIIR, INT16_MIN);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));

    res = spu94_set_reg_i16(g_state, SPU94_REG_vIIR, INT16_MAX);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));

    /* Interleaved with a wrong-typed write: the wrong-typed write must NOT
     * clobber the previously-correct value. */
    spu94_result_t bad = spu94_set_reg_u16(g_state, SPU94_REG_vIIR, 0xFFFFu);
    TEST_ASSERT_EQUAL_INT((int)SPU94_TYPE_MISMATCH, (int)bad);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));

    /* Then a correctly-typed -1 (= 0xFFFF as int16 bit pattern) succeeds. */
    res = spu94_set_reg_i16(g_state, SPU94_REG_vIIR, (int16_t)-1);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    TEST_ASSERT_EQUAL_INT16((int16_t)-1, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));
}

/* ---- Test: u16 preservation at edges (dAPF1 rep) ---- */

void test_u16_preservation_edges(void) {
    setup_state();
    spu94_result_t res;

    res = spu94_set_reg_u16(g_state, SPU94_REG_dAPF1, 0xFFFFu);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    spu94_tick(g_state);  /* dAPF1 is TICK_LATCHED */
    TEST_ASSERT_EQUAL_UINT16(0xFFFFu, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));

    res = spu94_set_reg_u16(g_state, SPU94_REG_dAPF1, 0x8000u);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT16(0x8000u, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));

    res = spu94_set_reg_u16(g_state, SPU94_REG_dAPF1, 0x0001u);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT16(0x0001u, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reg_type_classifier);
    RUN_TEST(test_type_mismatch_u16_on_i16);
    RUN_TEST(test_type_mismatch_i16_on_u16);
    RUN_TEST(test_unknown_reg);
    RUN_TEST(test_i16_preservation_edges);
    RUN_TEST(test_u16_preservation_edges);
    return UNITY_END();
}
