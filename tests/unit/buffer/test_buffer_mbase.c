/* tests/unit/buffer/test_buffer_mbase.c
 * Phase 2 Plan 05 Task 2: mBASE snap-on-write behavior + the work-buffer-
 * unchanged invariant. Closes ADR-0006's test obligation: snap-on-write +
 * no implicit work-buffer clear. The audible discontinuity that the snap
 * can produce is hardware-accurate per ADR-0006.
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

#define WORK_BUF_SIZE 4096

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_work_buf[WORK_BUF_SIZE];
static spu94_state *g_state = (spu94_state *)0;

static void setup_state(void) {
    g_state = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX,
                         g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(g_state);
}

/* ---- Test: writing mBASE snaps buffer_address immediately (no tick) ---- */

void test_mbase_snap_on_write_immediate(void) {
    setup_state();
    /* Advance buffer_address to a known non-zero value via 5 ticks. */
    for (int i = 0; i < 5; ++i) spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT32(10u, spu94_get_buffer_address(g_state));

    /* Write mBASE; buffer_address must update immediately, no tick needed. */
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x2000u));
    TEST_ASSERT_EQUAL_UINT32(0x2000u, spu94_get_buffer_address(g_state));
}

/* ---- Test: snap repeats on every mBASE write ---- */

void test_mbase_snap_multiple_times(void) {
    setup_state();
    static const uint16_t snap_values[] = {
        0x1000u, 0x4000u, 0x0010u, 0xFFFEu, 0x0000u, 0x8000u
    };
    for (size_t k = 0; k < sizeof(snap_values)/sizeof(snap_values[0]); ++k) {
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_set_reg_u16(g_state, SPU94_REG_mBASE, snap_values[k]));
        TEST_ASSERT_EQUAL_UINT32((uint32_t)snap_values[k],
            spu94_get_buffer_address(g_state));
    }
}

/* ---- Test: snapping mBASE to 0 from a non-zero address ---- */

void test_mbase_snap_to_zero(void) {
    setup_state();
    for (int i = 0; i < 10; ++i) spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT32(20u, spu94_get_buffer_address(g_state));

    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, spu94_get_buffer_address(g_state));
}

/* ---- Test: mBASE write does NOT mutate the caller's work buffer ----
 *
 * ADR-0006: "no implicit clear" -- the snap is a buffer_address
 * assignment, NOT a work-buffer reset. Sentinel-pattern verification. */

void test_mbase_does_not_mutate_work_buf(void) {
    setup_state();
    /* Pre-fill work_buf with a recognizable sentinel pattern. */
    for (size_t i = 0; i < WORK_BUF_SIZE; ++i) {
        g_work_buf[i] = 0xABu;
    }

    /* Snap mBASE to several different addresses. */
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x1234u);
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x5678u);
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x0000u);

    /* Every byte of work_buf must still be 0xAB. Format the mismatch with
     * the offending index + observed byte so a regression points straight
     * at the corrupted offset. TEST_FAIL_MESSAGE on first mismatch avoids
     * spamming on a long sequence of failures. */
    for (size_t i = 0; i < WORK_BUF_SIZE; ++i) {
        if (g_work_buf[i] != 0xABu) {
            char msg[96];
            snprintf(msg, sizeof msg,
                "work_buf[%zu] = 0x%02X (expected 0xAB) -- ADR-0006 violation: "
                "mBASE snap mutated caller work buffer",
                i, (unsigned)g_work_buf[i]);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

/* ---- Test: setting mBASE then ticking advances from the new mBASE ---- */

void test_mbase_set_then_tick_advances_from_mbase(void) {
    setup_state();
    /* Snap to 0x1000; buffer_address is now 0x1000. */
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x1000u);
    TEST_ASSERT_EQUAL_UINT32(0x1000u, spu94_get_buffer_address(g_state));
    /* Tick once: MAX(0x1000, (0x1000+2) AND 0x7FFFE) = MAX(0x1000, 0x1002) = 0x1002. */
    spu94_tick(g_state);
    TEST_ASSERT_EQUAL_UINT32(0x1002u, spu94_get_buffer_address(g_state));
}

/* ---- Test: pending read-back of mBASE matches active read (IMMEDIATE policy) ---- */

void test_mbase_pending_readback_consistent(void) {
    setup_state();
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x3000u);
    TEST_ASSERT_EQUAL_UINT16(0x3000u,
        spu94_get_reg_u16(g_state, SPU94_REG_mBASE));
    TEST_ASSERT_EQUAL_UINT16(0x3000u,
        spu94_get_reg_u16_pending(g_state, SPU94_REG_mBASE));
}

/* ---- Test: spu94_reset clears mBASE and buffer_address ---- */

void test_mbase_reset_behavior(void) {
    setup_state();
    spu94_set_reg_u16(g_state, SPU94_REG_mBASE, 0x5000u);
    TEST_ASSERT_EQUAL_UINT32(0x5000u, spu94_get_buffer_address(g_state));
    TEST_ASSERT_EQUAL_UINT16(0x5000u,
        spu94_get_reg_u16(g_state, SPU94_REG_mBASE));

    spu94_reset(g_state);

    TEST_ASSERT_EQUAL_UINT32(0u, spu94_get_buffer_address(g_state));
    TEST_ASSERT_EQUAL_UINT16(0u,
        spu94_get_reg_u16(g_state, SPU94_REG_mBASE));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mbase_snap_on_write_immediate);
    RUN_TEST(test_mbase_snap_multiple_times);
    RUN_TEST(test_mbase_snap_to_zero);
    RUN_TEST(test_mbase_does_not_mutate_work_buf);
    RUN_TEST(test_mbase_set_then_tick_advances_from_mbase);
    RUN_TEST(test_mbase_pending_readback_consistent);
    RUN_TEST(test_mbase_reset_behavior);
    return UNITY_END();
}
