/* tests/unit/registers/test_register_identity.c
 * Phase 2 Plan 02 Task 1 (TDD RED): exercise the register-identity surface.
 *
 * Asserts:
 *   - SPU94_REG__COUNT = 35 at compile time.
 *   - Each enum member maps to its psx-spx hardware offset (spot checks).
 *   - Every reg in [0, 35) has a non-NULL name and a non-0xFFFF hw_offset.
 *   - Out-of-range reg ids return sentinel values (0xFFFF / NULL).
 *   - spu94_reg_name returns the bare name (e.g., "vIIR"), per CONTEXT D-17.
 *   - spu94_snapshot_registers stub zeroes the output buffer and tolerates NULL.
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Compile-time guard the test file itself: if the canonical 35-count drifts,
 * this test TU fails to compile, not just at runtime. */
_Static_assert((int)SPU94_REG__COUNT == 35,
    "test_register_identity expects SPU94_REG__COUNT == 35");

void test_count_is_35(void) {
    TEST_ASSERT_EQUAL_INT(35, (int)SPU94_REG__COUNT);
}

void test_hw_offsets_spot_check(void) {
    TEST_ASSERT_EQUAL_HEX16(0x1D84, spu94_reg_hw_offset(SPU94_REG_vLOUT));
    TEST_ASSERT_EQUAL_HEX16(0x1D86, spu94_reg_hw_offset(SPU94_REG_vROUT));
    TEST_ASSERT_EQUAL_HEX16(0x1DA2, spu94_reg_hw_offset(SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_HEX16(0x1DC0, spu94_reg_hw_offset(SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0x1DC4, spu94_reg_hw_offset(SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_HEX16(0x1DCE, spu94_reg_hw_offset(SPU94_REG_vWALL));
    TEST_ASSERT_EQUAL_HEX16(0x1DD4, spu94_reg_hw_offset(SPU94_REG_mLSAME));
    TEST_ASSERT_EQUAL_HEX16(0x1DFC, spu94_reg_hw_offset(SPU94_REG_vLIN));
    TEST_ASSERT_EQUAL_HEX16(0x1DFE, spu94_reg_hw_offset(SPU94_REG_vRIN));
}

void test_hw_offsets_out_of_range(void) {
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, spu94_reg_hw_offset((spu94_reg_t)-1));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, spu94_reg_hw_offset((spu94_reg_t)35));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, spu94_reg_hw_offset((spu94_reg_t)9999));
}

void test_names_spot_check(void) {
    /* Bare name, NOT prefixed (D-17 + RESEARCH Open Q #6). */
    TEST_ASSERT_NOT_NULL(spu94_reg_name(SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_STRING("vIIR", spu94_reg_name(SPU94_REG_vIIR));
    TEST_ASSERT_EQUAL_STRING("mBASE", spu94_reg_name(SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_STRING("vLIN",  spu94_reg_name(SPU94_REG_vLIN));
    TEST_ASSERT_EQUAL_STRING("vRIN",  spu94_reg_name(SPU94_REG_vRIN));
    TEST_ASSERT_EQUAL_STRING("vLOUT", spu94_reg_name(SPU94_REG_vLOUT));
}

void test_names_out_of_range(void) {
    /* Plan chose NULL for out-of-range (header documents this). */
    TEST_ASSERT_NULL(spu94_reg_name((spu94_reg_t)-1));
    TEST_ASSERT_NULL(spu94_reg_name((spu94_reg_t)35));
    TEST_ASSERT_NULL(spu94_reg_name((spu94_reg_t)9999));
}

void test_every_reg_has_offset_and_name(void) {
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        const char *nm = spu94_reg_name((spu94_reg_t)i);
        uint16_t    of = spu94_reg_hw_offset((spu94_reg_t)i);

        char msg[96];
        snprintf(msg, sizeof(msg), "reg id %d must have a non-NULL name", i);
        TEST_ASSERT_NOT_NULL_MESSAGE(nm, msg);

        snprintf(msg, sizeof(msg), "reg id %d (%s) must have non-0xFFFF offset",
                 i, nm ? nm : "<null>");
        TEST_ASSERT_TRUE_MESSAGE(of != 0xFFFFu, msg);
    }
}

void test_offsets_are_unique(void) {
    /* No two registers may share a hardware offset. */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        for (int j = i + 1; j < (int)SPU94_REG__COUNT; ++j) {
            uint16_t oi = spu94_reg_hw_offset((spu94_reg_t)i);
            uint16_t oj = spu94_reg_hw_offset((spu94_reg_t)j);
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "offset collision: reg %d (%s) and reg %d (%s) both at 0x%04X",
                     i, spu94_reg_name((spu94_reg_t)i),
                     j, spu94_reg_name((spu94_reg_t)j), oi);
            TEST_ASSERT_TRUE_MESSAGE(oi != oj, msg);
        }
    }
}

void test_snapshot_zero_fills(void) {
    /* Allocate caller buffer and pre-fill with non-zero garbage. The Plan 02
     * stub MUST zero the output. Plan 03 wires it to real register storage. */
    int16_t out[SPU94_REG__COUNT];
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) out[i] = (int16_t)0x7E7E;

    /* Build a state via the Plan 01 lifecycle so we can pass a non-NULL state. */
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char buf[SPU94_STATE_SIZE_MAX];
    spu94_state *s = spu94_init(buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);

    spu94_snapshot_registers(s, out);
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        char msg[96];
        snprintf(msg, sizeof(msg), "snapshot stub must zero out[%d]", i);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out[i], msg);
    }

    spu94_destroy(s);
}

void test_snapshot_tolerates_null_out(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char buf[SPU94_STATE_SIZE_MAX];
    spu94_state *s = spu94_init(buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    /* Should not crash. */
    spu94_snapshot_registers(s, NULL);
    spu94_destroy(s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_count_is_35);
    RUN_TEST(test_hw_offsets_spot_check);
    RUN_TEST(test_hw_offsets_out_of_range);
    RUN_TEST(test_names_spot_check);
    RUN_TEST(test_names_out_of_range);
    RUN_TEST(test_every_reg_has_offset_and_name);
    RUN_TEST(test_offsets_are_unique);
    RUN_TEST(test_snapshot_zero_fills);
    RUN_TEST(test_snapshot_tolerates_null_out);
    return UNITY_END();
}
