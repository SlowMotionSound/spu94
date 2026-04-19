/* tests/unit/registers/test_register_io.c
 * Phase 2 Plan 03 Task 1 (TDD RED): exercise the engine-layer typed
 * register-IO accessors with signedness validation, plus the snapshot
 * wiring against state->reg_values[].
 *
 * Asserts:
 *   - spu94_set_reg_i16 / _u16 store and round-trip correctly when the
 *     accessor's signedness matches the register's type.
 *   - Wrong-typed access (e.g., set_u16 on vIIR) returns SPU94_TYPE_MISMATCH
 *     and leaves both active and pending state unchanged (D-07/D-08).
 *   - Out-of-range reg ids return SPU94_UNKNOWN_REG on set, 0 on get.
 *   - NULL state on getters returns 0; on setters returns SPU94_UNKNOWN_REG.
 *   - spu94_reg_type returns the correct family for every register.
 *   - spu94_snapshot_registers reads the active reg_values[] (no longer
 *     a zero-fill stub once Plan 03 wires it).
 *   - For an IMMEDIATE-policy v* register, get == get_pending always.
 *   - For a TICK_LATCHED d*/m* register, after a write get != get_pending
 *     until the next spu94_tick.
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stddef.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[1024];

static spu94_state *fresh_state(void) {
    return spu94_init(g_state_buf, sizeof(g_state_buf), g_work_buf, sizeof(g_work_buf));
}

void test_set_get_i16_roundtrip_vIIR(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_i16(s, SPU94_REG_vIIR, -32000));
    TEST_ASSERT_EQUAL_INT16(-32000, spu94_get_reg_i16(s, SPU94_REG_vIIR));
    /* IMMEDIATE policy: pending should match active. */
    TEST_ASSERT_EQUAL_INT16(-32000, spu94_get_reg_i16_pending(s, SPU94_REG_vIIR));
}

void test_set_get_u16_roundtrip_dAPF1_pending(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* dAPF1 is TICK_LATCHED: write goes to pending, active stays 0 until tick. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_dAPF1, 0x1234u));
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1));

    /* After a tick, pending flushes into active. */
    spu94_tick(s);
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1));
}

void test_type_mismatch_i16_on_unsigned_reg_leaves_state_unchanged(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* mBASE is u16 — calling _i16 must return TYPE_MISMATCH and not mutate. */
    TEST_ASSERT_EQUAL_INT(SPU94_TYPE_MISMATCH,
        spu94_set_reg_i16(s, SPU94_REG_mBASE, 0x1234));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16(s, SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16_pending(s, SPU94_REG_mBASE));
}

void test_type_mismatch_u16_on_signed_reg_leaves_state_unchanged(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* vIIR is i16 — calling _u16 must return TYPE_MISMATCH and not mutate. */
    TEST_ASSERT_EQUAL_INT(SPU94_TYPE_MISMATCH,
        spu94_set_reg_u16(s, SPU94_REG_vIIR, 0x8000u));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16(s, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16_pending(s, SPU94_REG_vIIR));
}

void test_set_out_of_range_returns_unknown_reg(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    spu94_reg_t bogus = (spu94_reg_t)100;
    TEST_ASSERT_EQUAL_INT(SPU94_UNKNOWN_REG, spu94_set_reg_i16(s, bogus, 1));
    TEST_ASSERT_EQUAL_INT(SPU94_UNKNOWN_REG, spu94_set_reg_u16(s, bogus, 1u));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16(s, bogus));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16(s, bogus));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16_pending(s, bogus));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16_pending(s, bogus));
}

void test_null_state_set_returns_unknown_reg_get_returns_zero(void) {
    TEST_ASSERT_EQUAL_INT(SPU94_UNKNOWN_REG, spu94_set_reg_i16(NULL, SPU94_REG_vIIR, 1));
    TEST_ASSERT_EQUAL_INT(SPU94_UNKNOWN_REG, spu94_set_reg_u16(NULL, SPU94_REG_mBASE, 1u));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16(NULL, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16(NULL, SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_INT16(0, spu94_get_reg_i16_pending(NULL, SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_reg_u16_pending(NULL, SPU94_REG_mBASE));
}

void test_reg_type_classification(void) {
    /* All 12 v* are I16. */
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vLOUT));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vROUT));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vCOMB1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vCOMB2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vCOMB3));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vCOMB4));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vWALL));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vAPF1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vAPF2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vLIN));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_I16, spu94_reg_type(SPU94_REG_vRIN));

    /* mBASE + 22 d-prefix / m-prefix registers are U16. */
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLSAME));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRSAME));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLCOMB1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRCOMB1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLCOMB2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRCOMB2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dLSAME));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dRSAME));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLDIFF));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRDIFF));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLCOMB3));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRCOMB3));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLCOMB4));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRCOMB4));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dLDIFF));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_dRDIFF));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLAPF1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRAPF1));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mLAPF2));
    TEST_ASSERT_EQUAL_INT(SPU94_REG_TYPE_U16, spu94_reg_type(SPU94_REG_mRAPF2));
}

void test_immediate_policy_v_register_pending_equals_active(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* IMMEDIATE policy contract: get == get_pending after every write,
     * even before any tick. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_i16(s, SPU94_REG_vCOMB2, 12345));
    TEST_ASSERT_EQUAL_INT16(12345, spu94_get_reg_i16(s, SPU94_REG_vCOMB2));
    TEST_ASSERT_EQUAL_INT16(12345, spu94_get_reg_i16_pending(s, SPU94_REG_vCOMB2));
}

void test_mBASE_immediate_write_visible_without_tick(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* mBASE is IMMEDIATE (with side-effect). The config value itself is
     * visible immediately, before any tick call. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_mBASE, 0x4000u));
    TEST_ASSERT_EQUAL_HEX16(0x4000u, spu94_get_reg_u16(s, SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_HEX16(0x4000u, spu94_get_reg_u16_pending(s, SPU94_REG_mBASE));
}

void test_snapshot_reads_active_register_values(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_i16(s, SPU94_REG_vIIR, -1000));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_i16(s, SPU94_REG_vCOMB1, 4444));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_mBASE, 0x2000u));

    int16_t out[SPU94_REG__COUNT];
    spu94_snapshot_registers(s, out);
    TEST_ASSERT_EQUAL_INT16(-1000, out[SPU94_REG_vIIR]);
    TEST_ASSERT_EQUAL_INT16(4444,  out[SPU94_REG_vCOMB1]);
    /* mBASE u16 0x2000 stored as int16 with the same bit pattern. */
    TEST_ASSERT_EQUAL_HEX16(0x2000u, (uint16_t)out[SPU94_REG_mBASE]);
    /* dAPF1 was never written; stays 0. */
    TEST_ASSERT_EQUAL_INT16(0, out[SPU94_REG_dAPF1]);
}

void test_snapshot_with_null_state_zeroes_output(void) {
    int16_t out[SPU94_REG__COUNT];
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) out[i] = 0x55AA;
    spu94_snapshot_registers((const spu94_state *)0, out);
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

void test_tick_clears_pending_mask_for_all_pending_writes(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Stage three TICK_LATCHED writes. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_dAPF1,  0x0011u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_dAPF2,  0x0022u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_reg_u16(s, SPU94_REG_mLAPF1, 0x0033u));
    /* Active should still be 0; pending should hold the new values. */
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_mLAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x0011u, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x0022u, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_HEX16(0x0033u, spu94_get_reg_u16_pending(s, SPU94_REG_mLAPF1));

    spu94_tick(s);

    /* All three flush; further ticks are no-ops. */
    TEST_ASSERT_EQUAL_HEX16(0x0011u, spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x0022u, spu94_get_reg_u16(s, SPU94_REG_dAPF2));
    TEST_ASSERT_EQUAL_HEX16(0x0033u, spu94_get_reg_u16(s, SPU94_REG_mLAPF1));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_set_get_i16_roundtrip_vIIR);
    RUN_TEST(test_set_get_u16_roundtrip_dAPF1_pending);
    RUN_TEST(test_type_mismatch_i16_on_unsigned_reg_leaves_state_unchanged);
    RUN_TEST(test_type_mismatch_u16_on_signed_reg_leaves_state_unchanged);
    RUN_TEST(test_set_out_of_range_returns_unknown_reg);
    RUN_TEST(test_null_state_set_returns_unknown_reg_get_returns_zero);
    RUN_TEST(test_reg_type_classification);
    RUN_TEST(test_immediate_policy_v_register_pending_equals_active);
    RUN_TEST(test_mBASE_immediate_write_visible_without_tick);
    RUN_TEST(test_snapshot_reads_active_register_values);
    RUN_TEST(test_snapshot_with_null_state_zeroes_output);
    RUN_TEST(test_tick_clears_pending_mask_for_all_pending_writes);
    return UNITY_END();
}
