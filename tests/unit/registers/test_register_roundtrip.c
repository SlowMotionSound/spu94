/* tests/unit/registers/test_register_roundtrip.c
 * Phase 2 Plan 05 Task 1: per-register write+read round-trip for all 35
 * registers. TEST-02 (per-register unit tests) coverage. Follows the Phase 1
 * D-10 pattern: inline hand-constructed reference table with
 * {register, typed-write-value, expected-typed-read-value, policy}.
 *
 * For IMMEDIATE-policy registers (12 v* + mBASE), the active read matches
 * immediately after the typed setter. For TICK_LATCHED registers (the 22
 * d-prefix and m-prefix delay/address registers),
 * the pending read matches immediately and the active read matches after
 * spu94_tick().
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <spu94/spu94_register_facade.h>
#include <stdint.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdio.h>

void setUp(void) {}
void tearDown(void) {}

/* Module-scope state buffers (per the q15/registers test pattern). */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_work_buf[1024];
static spu94_state *g_state = (spu94_state *)0;

static void setup_state(void) {
    g_state = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX,
                         g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(g_state);
}

/* Classification helper: which registers are u16 (mBASE + 22 d-prefix /
 * m-prefix delay/address registers). The remaining 12 are i16 (v-prefix).
 * Mirrors the table in src/spu94/spu94_register_io.c. */
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

/* Per-register distinct-value generator. Distinct values catch routing bugs
 * (a write to register A landing in register B's storage). Use the low 16
 * bits of (0x1000 + i*0x111) -- never 0, always different per register. */
static uint16_t test_value_for(int i) {
    return (uint16_t)(0x1000u + (uint16_t)i * 0x111u);
}

/* ---- Test 1: write every register, tick once, read every register back ---- */

void test_every_register_roundtrips(void) {
    setup_state();

    /* Phase 1: write every register with its distinct test value via the
     * correctly-typed engine setter. */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        uint16_t v_u = test_value_for(i);
        spu94_result_t res;
        char msg[80];
        snprintf(msg, sizeof(msg), "set reg %d (%s)", i, spu94_reg_name(r));
        if (reg_is_u16(r)) {
            res = spu94_set_reg_u16(g_state, r, v_u);
        } else {
            res = spu94_set_reg_i16(g_state, r, (int16_t)v_u);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(SPU94_OK, (int)res, msg);
    }

    /* Phase 2: flush pending writes for the 22 TICK_LATCHED registers. */
    spu94_tick(g_state);

    /* Phase 3: read every register back via the correctly-typed engine
     * getter and verify the value matches. */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        uint16_t v_u = test_value_for(i);
        char msg[80];
        snprintf(msg, sizeof(msg), "read reg %d (%s)", i, spu94_reg_name(r));
        if (reg_is_u16(r)) {
            uint16_t got = spu94_get_reg_u16(g_state, r);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(v_u, got, msg);
        } else {
            int16_t got = spu94_get_reg_i16(g_state, r);
            TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)v_u, got, msg);
        }
    }
}

/* ---- Test 2: snapshot returns the same values that were written ---- */

void test_snapshot_matches_writes(void) {
    setup_state();
    /* Use a different value pattern so this test is independent of the prior
     * one (test ordering shouldn't affect outcomes). */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        uint16_t v = (uint16_t)(0x2000u + (uint16_t)i);
        if (reg_is_u16(r)) {
            spu94_set_reg_u16(g_state, r, v);
        } else {
            spu94_set_reg_i16(g_state, r, (int16_t)v);
        }
    }
    spu94_tick(g_state);

    int16_t out[SPU94_REG__COUNT];
    spu94_snapshot_registers(g_state, out);
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        int16_t expected = (int16_t)(uint16_t)(0x2000u + (uint16_t)i);
        char msg[80];
        snprintf(msg, sizeof(msg), "snapshot reg %d (%s)",
                 i, spu94_reg_name((spu94_reg_t)i));
        TEST_ASSERT_EQUAL_INT16_MESSAGE(expected, out[i], msg);
    }
}

/* ---- Test 3: facade wrapper parity with engine layer ---- */

void test_facade_wrapper_parity(void) {
    setup_state();

    /* IMMEDIATE-policy register (vIIR is i16, IMMEDIATE): facade write,
     * engine read AND facade read must both return the value -- and they
     * must do so without a tick. */
    spu94_set_vIIR(g_state, -31415);
    TEST_ASSERT_EQUAL_INT16(-31415, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_INT16(-31415, spu94_get_vIIR(g_state));
    TEST_ASSERT_EQUAL_INT16(-31415, spu94_get_vIIR_pending(g_state));

    /* TICK_LATCHED register (dAPF1 is u16, TICK_LATCHED): pending visible
     * immediately, active visible after tick. */
    spu94_set_dAPF1(g_state, 0xCAFE);
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, spu94_get_dAPF1_pending(g_state));
    /* Active is still 0 (initial value) until tick. */
    TEST_ASSERT_EQUAL_UINT16(0, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
    spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, spu94_get_reg_u16(g_state, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, spu94_get_dAPF1(g_state));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_register_roundtrips);
    RUN_TEST(test_snapshot_matches_writes);
    RUN_TEST(test_facade_wrapper_parity);
    return UNITY_END();
}
