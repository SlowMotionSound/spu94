/* tests/unit/registers/test_register_policy.c
 * Phase 2 Plan 05 Task 1: per-register write-timing policy verification.
 * Covers ADR-0005 / CONTEXT D-04 / D-06: 12 v* + mBASE are IMMEDIATE; 22
 * d-prefix / m-prefix delay/address registers are TICK_LATCHED. The pending
 * read-back is the observable shadow created by the split policy.
 *
 * Closes ADR-0005's test obligation (per-register policy battery).
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>

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

/* IMMEDIATE policy: all 12 v* registers + mBASE (= 13 total). Everything
 * else is TICK_LATCHED. */
static int reg_is_immediate(spu94_reg_t r) {
    if (r == SPU94_REG_mBASE) return 1;
    return !reg_is_u16(r);  /* i16 (v-prefix) => IMMEDIATE */
}

/* ---- Test: every IMMEDIATE register's write is visible without a tick,
 *           and the pending read-back equals the active read-back ---- */

void test_every_immediate_reg(void) {
    setup_state();
    int seen = 0;
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (!reg_is_immediate(r)) continue;
        ++seen;

        char msg[80];
        snprintf(msg, sizeof(msg), "imm reg %d (%s)", i, spu94_reg_name(r));

        if (reg_is_u16(r)) {
            /* mBASE is the only u16 IMMEDIATE register. Note: writing mBASE
             * also fires the snap-on-write side effect (Plan 04, ADR-0006);
             * here we only check the register read-back, not buffer_address. */
            uint16_t v = (uint16_t)(0x4000u + (uint16_t)i);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK,
                (int)spu94_set_reg_u16(g_state, r, v), msg);
            /* Active and pending both equal the new value, NO TICK. */
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(v,
                spu94_get_reg_u16(g_state, r), msg);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(v,
                spu94_get_reg_u16_pending(g_state, r), msg);
        } else {
            int16_t v = (int16_t)(0x4000 + i);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK,
                (int)spu94_set_reg_i16(g_state, r, v), msg);
            TEST_ASSERT_EQUAL_INT16_MESSAGE(v,
                spu94_get_reg_i16(g_state, r), msg);
            TEST_ASSERT_EQUAL_INT16_MESSAGE(v,
                spu94_get_reg_i16_pending(g_state, r), msg);
        }
    }
    /* Sanity: 13 IMMEDIATE registers (12 v* + mBASE). */
    TEST_ASSERT_EQUAL_INT(13, seen);
}

/* ---- Test: every TICK_LATCHED register's write is visible only after a
 *           tick. Pending == new value; active == prior (= 0) until tick. ---- */

void test_every_tick_latched_reg(void) {
    setup_state();
    int seen = 0;
    /* Process each register independently so the post-tick assertion isn't
     * polluted by other registers' pending values. */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (reg_is_immediate(r)) continue;   /* skip the 13 IMMEDIATE regs */
        ++seen;

        char msg[80];
        snprintf(msg, sizeof(msg), "lat reg %d (%s)", i, spu94_reg_name(r));

        /* Re-init each iteration so the active value starts at 0. */
        setup_state();

        uint16_t v = (uint16_t)(0x5000u + (uint16_t)i);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK,
            (int)spu94_set_reg_u16(g_state, r, v), msg);

        /* Before tick: active == 0 (post-init), pending == new value. */
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0,
            spu94_get_reg_u16(g_state, r), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v,
            spu94_get_reg_u16_pending(g_state, r), msg);

        spu94_tick(g_state);

        /* After tick: active and pending both equal new value. */
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v,
            spu94_get_reg_u16(g_state, r), msg);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(v,
            spu94_get_reg_u16_pending(g_state, r), msg);
    }
    /* Sanity: 22 TICK_LATCHED registers. */
    TEST_ASSERT_EQUAL_INT(22, seen);
}

/* ---- Test: multiple pending writes are flushed atomically by a single tick ---- */

void test_multiple_pending(void) {
    setup_state();

    /* Three TICK_LATCHED registers, distinct values. */
    spu94_set_reg_u16(g_state, SPU94_REG_dAPF1,  0xAAAAu);
    spu94_set_reg_u16(g_state, SPU94_REG_dAPF2,  0xBBBBu);
    spu94_set_reg_u16(g_state, SPU94_REG_dLSAME, 0xCCCCu);

    /* Pending: each holds its written value. Active: still 0. */
    TEST_ASSERT_EQUAL_UINT16(0xAAAAu, spu94_get_reg_u16_pending(g_state, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT16(0xBBBBu, spu94_get_reg_u16_pending(g_state, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_UINT16(0xCCCCu, spu94_get_reg_u16_pending(g_state, SPU94_REG_dLSAME));
    TEST_ASSERT_EQUAL_UINT16(0, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT16(0, spu94_get_reg_u16(g_state, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_UINT16(0, spu94_get_reg_u16(g_state, SPU94_REG_dLSAME));

    spu94_tick(g_state);

    /* All three active values updated in one tick. */
    TEST_ASSERT_EQUAL_UINT16(0xAAAAu, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT16(0xBBBBu, spu94_get_reg_u16(g_state, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_UINT16(0xCCCCu, spu94_get_reg_u16(g_state, SPU94_REG_dLSAME));
}

/* ---- Test: mixing IMMEDIATE and TICK_LATCHED writes in one window ---- */

void test_mixed_immediate_and_tick_latched(void) {
    setup_state();

    /* IMMEDIATE: vIIR (i16). */
    spu94_set_reg_i16(g_state, SPU94_REG_vIIR, 0x1234);
    /* Visible immediately (no tick). */
    TEST_ASSERT_EQUAL_INT16(0x1234, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_INT16(0x1234, spu94_get_reg_i16_pending(g_state, SPU94_REG_vIIR));

    /* TICK_LATCHED: dAPF1 (u16). */
    spu94_set_reg_u16(g_state, SPU94_REG_dAPF1, 0xBEEFu);
    /* Active still 0; pending == 0xBEEF. */
    TEST_ASSERT_EQUAL_UINT16(0, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT16(0xBEEFu, spu94_get_reg_u16_pending(g_state, SPU94_REG_dAPF1));

    /* vIIR untouched by the dAPF1 write. */
    TEST_ASSERT_EQUAL_INT16(0x1234, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));

    spu94_tick(g_state);

    /* Both visible after tick; vIIR unchanged from before. */
    TEST_ASSERT_EQUAL_INT16(0x1234, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_UINT16(0xBEEFu, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_immediate_reg);
    RUN_TEST(test_every_tick_latched_reg);
    RUN_TEST(test_multiple_pending);
    RUN_TEST(test_mixed_immediate_and_tick_latched);
    return UNITY_END();
}
