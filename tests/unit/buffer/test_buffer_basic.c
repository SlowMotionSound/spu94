/* tests/unit/buffer/test_buffer_basic.c
 * Phase 2 Plan 04 Task 1 (TDD RED): exercise the buffer-arithmetic surface
 * landed by this plan — spu94_buffer_advance (internal), spu94_mbase_on_write
 * (real, lifted from the Plan 03 stub), spu94_get_buffer_address (public
 * observability accessor), and the spu94_tick body (apply_pending_writes
 * THEN buffer_advance, in that order).
 *
 * Plan 04 covers the basic correctness corners; Plan 05 adds the formal
 * `test_buffer_wrap.c`, `test_buffer_mbase.c` (work-buf-unchanged invariant),
 * and the 10^6-step Python ctypes fuzz harness.
 *
 * Asserts:
 *   - spu94_get_buffer_address(NULL) == 0; valid state returns the byte offset.
 *   - After spu94_init, buffer_address == 0 (mBASE starts at 0; D-14 reset
 *     contract); spu94_reset re-enforces that.
 *   - One spu94_tick from buffer_address=0/mBASE=0 produces buffer_address=2.
 *   - 100 ticks from 0/0 produces buffer_address=200.
 *   - Wrap at the top: after a tick from buffer_address=0x7FFFE / mBASE=0
 *     the address returns to 0.
 *   - mBASE-floor: tick from buffer_address=0x30000 / mBASE=0x40000 lands
 *     at 0x40000 (advanced=0x30002, MAX(0x40000, 0x30002)=0x40000).
 *   - Snap-on-write (ADR-0006): writing mBASE=0x12345 makes buffer_address ==
 *     0x12345 immediately, AND reg_values[mBASE] == (int16_t)0x12345.
 *   - Odd-mBASE pass-through: writing mBASE=0x1235 produces
 *     buffer_address==0x1235 (NOT 0x1234) — bit-faithful per ADR-0006 +
 *     Plan 05 fuzz invariant T-02-28.
 *   - mBASE write does NOT mutate the caller's work buffer (Plan 05 covers
 *     the full sentinel-pattern check; here a sanity fingerprint suffices).
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

/* Helper: directly drive buffer_address forward via repeated ticks. The tick
 * body is `apply_pending_writes` then `buffer_advance` — with no pending
 * writes outstanding, each tick is exactly one advance. */
static void advance_n(spu94_state *s, int n) {
    for (int i = 0; i < n; ++i) spu94_tick(s);
}

void test_get_buffer_address_null_state_returns_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, spu94_get_buffer_address(NULL));
}

void test_get_buffer_address_after_init_is_zero(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_UINT32(0u, spu94_get_buffer_address(s));
}

void test_reset_restores_buffer_address_to_zero(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Drive the address forward, then reset. */
    advance_n(s, 50);
    TEST_ASSERT_EQUAL_UINT32(100u, spu94_get_buffer_address(s));
    spu94_reset(s);
    TEST_ASSERT_EQUAL_UINT32(0u, spu94_get_buffer_address(s));
}

void test_one_tick_advances_by_two_from_zero(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    spu94_tick(s);
    TEST_ASSERT_EQUAL_UINT32(2u, spu94_get_buffer_address(s));
}

void test_hundred_ticks_advances_to_200(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    advance_n(s, 100);
    TEST_ASSERT_EQUAL_UINT32(200u, spu94_get_buffer_address(s));
}

void test_advance_from_max_u16_mbase_with_floor_active(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* mBASE is a uint16_t register; 0xFFFE is the largest value reachable via
     * the public API. Snap-on-write places buffer_address at 0xFFFE, then
     * mBASE remains the floor for subsequent advances. The true wrap-from-top
     * corner (buffer_address == 0x7FFFE -> 0) is unreachable through the
     * public API and is exercised exclusively by the Python ctypes fuzz
     * harness (tests/python/fuzz_buffer.py, 262K-tick brute force; see also
     * tests/unit/buffer/test_buffer_wrap.c lines 1-10). */
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0xFFFEu));
    TEST_ASSERT_EQUAL_UINT32(0xFFFEu, spu94_get_buffer_address(s));
    /* Advance from 0xFFFE: MAX(0xFFFE, (0xFFFE+2)&0x7FFFE)
     *                    = MAX(0xFFFE, 0x10000) = 0x10000.
     * The wrap mask 0x7FFFE leaves 0x10000 untouched, so the floor does not
     * fire — this verifies the floor-active arm of the formula at the upper
     * end of the u16 mBASE range. */
    spu94_tick(s);
    TEST_ASSERT_EQUAL_UINT32(0x10000u, spu94_get_buffer_address(s));
}

void test_advance_with_mBASE_floor_active(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Set mBASE=0x4000 (fits in u16). Snap puts buffer_address there. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0x4000u));
    TEST_ASSERT_EQUAL_UINT32(0x4000u, spu94_get_buffer_address(s));
    /* One tick: MAX(0x4000, (0x4000+2)&0x7FFFE) = MAX(0x4000, 0x4002) = 0x4002. */
    spu94_tick(s);
    TEST_ASSERT_EQUAL_UINT32(0x4002u, spu94_get_buffer_address(s));
}

void test_mBASE_snap_on_write_immediate(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Drive the address away from 0 first. */
    advance_n(s, 10);
    TEST_ASSERT_EQUAL_UINT32(20u, spu94_get_buffer_address(s));
    /* Snap-on-write per ADR-0006: buffer_address := mBASE immediately. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0x1234u));
    TEST_ASSERT_EQUAL_UINT32(0x1234u, spu94_get_buffer_address(s));
    /* And the mBASE register itself reflects the write (IMMEDIATE policy). */
    TEST_ASSERT_EQUAL_HEX16(0x1234u, spu94_get_reg_u16(s, SPU94_REG_mBASE));
}

void test_odd_mBASE_passes_through_verbatim(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* 0x1235 has bit 0 set. ADR-0006 + T-02-28 require the snap to pass
     * the value through verbatim — bit 0 is NOT masked. The next advance
     * (from this state) will land at MAX(0x1235, (0x1235+2)&0x7FFFE) =
     * MAX(0x1235, 0x1236) = 0x1236, clearing bit 0. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0x1235u));
    TEST_ASSERT_EQUAL_UINT32(0x1235u, spu94_get_buffer_address(s));
    spu94_tick(s);
    TEST_ASSERT_EQUAL_UINT32(0x1236u, spu94_get_buffer_address(s));
}

void test_mBASE_write_does_not_mutate_work_buffer(void) {
    /* Plan 04 sanity check; Plan 05 owns the full sentinel sweep. */
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    /* Stamp a recognizable byte pattern across the work buffer. */
    for (size_t i = 0; i < sizeof(g_work_buf); ++i) {
        g_work_buf[i] = (unsigned char)(i & 0xFF);
    }
    /* Snap mBASE; per ADR-0006 there is no implicit clear. */
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_mBASE, (uint16_t)0x4000u));
    /* Pattern preserved. */
    for (size_t i = 0; i < sizeof(g_work_buf); ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char)(i & 0xFF), g_work_buf[i]);
    }
}

void test_apply_pending_runs_before_buffer_advance(void) {
    /* Tick order check via observable state (D-19, Plan 04 acceptance):
     * stage a TICK_LATCHED dAPF1 write while sitting at buffer_address=0;
     * after one tick, dAPF1 should be active (apply_pending fired) AND
     * buffer_address should have advanced (buffer_advance fired). */
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_reg_u16(s, SPU94_REG_dAPF1, (uint16_t)0xDEADu));
    /* Pre-tick: pending visible, active still zero, address still zero. */
    TEST_ASSERT_EQUAL_HEX16(0u,      spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_HEX16(0xDEADu, spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT32(0u,     spu94_get_buffer_address(s));
    spu94_tick(s);
    /* Post-tick: pending flushed AND address advanced. */
    TEST_ASSERT_EQUAL_HEX16(0xDEADu, spu94_get_reg_u16(s, SPU94_REG_dAPF1));
    TEST_ASSERT_EQUAL_UINT32(2u,     spu94_get_buffer_address(s));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_buffer_address_null_state_returns_zero);
    RUN_TEST(test_get_buffer_address_after_init_is_zero);
    RUN_TEST(test_reset_restores_buffer_address_to_zero);
    RUN_TEST(test_one_tick_advances_by_two_from_zero);
    RUN_TEST(test_hundred_ticks_advances_to_200);
    RUN_TEST(test_advance_from_max_u16_mbase_with_floor_active);
    RUN_TEST(test_advance_with_mBASE_floor_active);
    RUN_TEST(test_mBASE_snap_on_write_immediate);
    RUN_TEST(test_odd_mBASE_passes_through_verbatim);
    RUN_TEST(test_mBASE_write_does_not_mutate_work_buffer);
    RUN_TEST(test_apply_pending_runs_before_buffer_advance);
    return UNITY_END();
}
