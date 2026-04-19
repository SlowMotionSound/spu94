/* tests/unit/registers/test_register_edges.c
 * Phase 2 Plan 05 Task 1: edge-value preservation per register. Covers
 * INT16_MIN/MAX/0 for every i16 register and 0/0x0001/0x7FFF/0x8000/0xFFFF
 * for every u16 register. Includes the vIIR=-0x8000 round-trip pin (the
 * ADR-0002 anomaly itself -- output negation -- is a Phase 3 algorithm test
 * in TEST-06; here we only verify the register stores -0x8000 round-trip).
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

/* ---- Test: every i16 register accepts INT16_MIN, INT16_MAX, 0 ---- */

void test_int16_min_max_zero_every_i16(void) {
    static const int16_t values[] = { INT16_MIN, INT16_MAX, 0 };
    int seen = 0;
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (reg_is_u16(r)) continue;     /* only i16 */
        ++seen;
        for (size_t k = 0; k < sizeof(values)/sizeof(values[0]); ++k) {
            setup_state();
            char msg[80];
            snprintf(msg, sizeof(msg), "reg %d (%s) value=%d",
                     i, spu94_reg_name(r), (int)values[k]);
            spu94_result_t res = spu94_set_reg_i16(g_state, r, values[k]);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK, (int)res, msg);
            /* IMMEDIATE policy for all v*; no tick required. */
            TEST_ASSERT_EQUAL_INT16_MESSAGE(values[k],
                spu94_get_reg_i16(g_state, r), msg);
        }
    }
    TEST_ASSERT_EQUAL_INT(12, seen);     /* 12 i16 registers */
}

/* ---- Test: every u16 register accepts 0, 0x0001, 0x7FFF, 0x8000, 0xFFFF ---- */

void test_u16_boundary_every_u16(void) {
    static const uint16_t values[] = {
        0x0000u, 0x0001u, 0x7FFFu, 0x8000u, 0xFFFFu
    };
    int seen = 0;
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        if (!reg_is_u16(r)) continue;    /* only u16 */
        ++seen;
        for (size_t k = 0; k < sizeof(values)/sizeof(values[0]); ++k) {
            setup_state();
            char msg[80];
            snprintf(msg, sizeof(msg), "reg %d (%s) value=0x%04X",
                     i, spu94_reg_name(r), (unsigned)values[k]);
            spu94_result_t res = spu94_set_reg_u16(g_state, r, values[k]);
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK, (int)res, msg);
            /* mBASE is IMMEDIATE; the 22 d-prefix/m-prefix are TICK_LATCHED.
             * Tick unconditionally -- IMMEDIATE registers see no harm from
             * a tick. */
            spu94_tick(g_state);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(values[k],
                spu94_get_reg_u16(g_state, r), msg);
        }
    }
    TEST_ASSERT_EQUAL_INT(23, seen);     /* 23 u16 registers (mBASE + 22) */
}

/* ---- Test: vIIR round-trip of INT16_MIN (-0x8000 = -32768) ---- */

void test_vIIR_roundtrip_of_minus_0x8000(void) {
    /* This is a round-trip test only. The ADR-0002 anomaly -- vIIR=-0x8000
     * causing negation of the final reverb result -- is a Phase 3 algorithm
     * test (TEST-06), NOT a Phase 2 register-storage test. Plan 05's job is
     * to confirm the register STORES the bit pattern correctly; what the
     * algorithm DOES with -0x8000 is the Phase 3 algorithm's responsibility. */
    setup_state();
    spu94_result_t res = spu94_set_reg_i16(g_state, SPU94_REG_vIIR, INT16_MIN);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)res);
    /* IMMEDIATE; no tick required. */
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, spu94_get_reg_i16(g_state, SPU94_REG_vIIR));
    /* Verify the bit pattern via snapshot too: int16 value -32768 == 0x8000. */
    int16_t snap[SPU94_REG__COUNT];
    spu94_snapshot_registers(g_state, snap);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, snap[SPU94_REG_vIIR]);
    TEST_ASSERT_EQUAL_HEX16((uint16_t)0x8000u, (uint16_t)snap[SPU94_REG_vIIR]);
}

/* ---- Test: zero is a meaningful value for every register ---- */

void test_zero_meaningful_for_every_reg(void) {
    /* RESEARCH.md Register Inventory "zero-value meaning" column: every
     * register treats 0 as a valid value (not an error). Verify by writing
     * 0 to every register and reading 0 back. */
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        spu94_reg_t r = (spu94_reg_t)i;
        setup_state();
        char msg[80];
        snprintf(msg, sizeof(msg), "reg %d (%s) zero",
                 i, spu94_reg_name(r));
        if (reg_is_u16(r)) {
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK,
                (int)spu94_set_reg_u16(g_state, r, 0u), msg);
            spu94_tick(g_state);
            TEST_ASSERT_EQUAL_UINT16_MESSAGE(0,
                spu94_get_reg_u16(g_state, r), msg);
        } else {
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK,
                (int)spu94_set_reg_i16(g_state, r, 0), msg);
            spu94_tick(g_state);
            TEST_ASSERT_EQUAL_INT16_MESSAGE(0,
                spu94_get_reg_i16(g_state, r), msg);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_int16_min_max_zero_every_i16);
    RUN_TEST(test_u16_boundary_every_u16);
    RUN_TEST(test_vIIR_roundtrip_of_minus_0x8000);
    RUN_TEST(test_zero_meaningful_for_every_reg);
    return UNITY_END();
}
