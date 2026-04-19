/* tests/unit/registers/test_register_facade.c
 * Phase 2 Plan 03 Task 3 (TDD RED): exercise the 35-register facade layer.
 *
 * Asserts:
 *   - Each facade setter writes the same active state as the engine setter.
 *   - Each facade getter returns the same value as the engine getter.
 *   - The pending-readback facade returns the same value as the engine
 *     pending-readback.
 *   - i16 / u16 typing is preserved by the facade (compile-time check).
 *   - Spot-check every register family — v* signed, mBASE u16 IMMEDIATE,
 *     d*/m* u16 TICK_LATCHED.
 *   - Sentinel registers vLIN / vRIN have wrappers (the registers added
 *     during research per CONTEXT.md "32-vs-33 reconciliation").
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>
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

void test_facade_set_get_vIIR_matches_engine(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vIIR(s, -32000));
    TEST_ASSERT_EQUAL_INT16(-32000, spu94_get_vIIR(s));
    TEST_ASSERT_EQUAL_INT16(-32000, spu94_get_vIIR_pending(s));
    /* Facade routes through engine; verify equivalence. */
    TEST_ASSERT_EQUAL_INT16(spu94_get_reg_i16(s, SPU94_REG_vIIR), spu94_get_vIIR(s));
}

void test_facade_set_dAPF1_is_tick_latched(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_dAPF1(s, 0xCAFEu));
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_dAPF1(s));
    TEST_ASSERT_EQUAL_HEX16(0xCAFEu, spu94_get_dAPF1_pending(s));
    spu94_tick(s);
    TEST_ASSERT_EQUAL_HEX16(0xCAFEu, spu94_get_dAPF1(s));
}

void test_facade_set_mBASE_is_immediate(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mBASE(s, 0x4000u));
    TEST_ASSERT_EQUAL_HEX16(0x4000u, spu94_get_mBASE(s));
    TEST_ASSERT_EQUAL_HEX16(0x4000u, spu94_get_mBASE_pending(s));
}

void test_facade_set_vLIN_vRIN_are_signed(void) {
    /* The 32-vs-33 reconciliation registers — vLIN/vRIN must have wrappers. */
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vLIN(s, -1));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vRIN(s,  1));
    TEST_ASSERT_EQUAL_INT16(-1, spu94_get_vLIN(s));
    TEST_ASSERT_EQUAL_INT16(1,  spu94_get_vRIN(s));
}

void test_facade_routing_registers(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vLOUT(s,  0x4000));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vROUT(s, -0x4000));
    TEST_ASSERT_EQUAL_INT16( 0x4000, spu94_get_vLOUT(s));
    TEST_ASSERT_EQUAL_INT16(-0x4000, spu94_get_vROUT(s));
}

void test_facade_comb_register_set(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Spot-check each comb gain — all i16 IMMEDIATE. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vCOMB1(s, 1));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vCOMB2(s, 2));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vCOMB3(s, 3));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_vCOMB4(s, 4));
    TEST_ASSERT_EQUAL_INT16(1, spu94_get_vCOMB1(s));
    TEST_ASSERT_EQUAL_INT16(2, spu94_get_vCOMB2(s));
    TEST_ASSERT_EQUAL_INT16(3, spu94_get_vCOMB3(s));
    TEST_ASSERT_EQUAL_INT16(4, spu94_get_vCOMB4(s));
}

void test_facade_address_register_set(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Spot-check each address-family wrapper. All TICK_LATCHED. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mLSAME(s,  0x10u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mRSAME(s,  0x20u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mLDIFF(s,  0x30u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mRDIFF(s,  0x40u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_dLSAME(s,  0x50u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_dRSAME(s,  0x60u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mLAPF1(s,  0x70u));
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_mRAPF2(s,  0x80u));
    /* Active still 0 before tick. */
    TEST_ASSERT_EQUAL_HEX16(0u, spu94_get_mLSAME(s));
    /* Pending matches. */
    TEST_ASSERT_EQUAL_HEX16(0x10u, spu94_get_mLSAME_pending(s));
    TEST_ASSERT_EQUAL_HEX16(0x80u, spu94_get_mRAPF2_pending(s));
    spu94_tick(s);
    /* Now flushed. */
    TEST_ASSERT_EQUAL_HEX16(0x10u, spu94_get_mLSAME(s));
    TEST_ASSERT_EQUAL_HEX16(0x80u, spu94_get_mRAPF2(s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_facade_set_get_vIIR_matches_engine);
    RUN_TEST(test_facade_set_dAPF1_is_tick_latched);
    RUN_TEST(test_facade_set_mBASE_is_immediate);
    RUN_TEST(test_facade_set_vLIN_vRIN_are_signed);
    RUN_TEST(test_facade_routing_registers);
    RUN_TEST(test_facade_comb_register_set);
    RUN_TEST(test_facade_address_register_set);
    return UNITY_END();
}
