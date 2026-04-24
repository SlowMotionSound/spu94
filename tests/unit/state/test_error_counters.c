/* tests/unit/state/test_error_counters.c
 * M1 close-out Step 4 / ADR-0023
 *
 * Unity tests for the observable error-counter surface.
 *
 *   1. test_counter_zero_after_init       -- fresh state: oob_tap_count = 0
 *   2. test_counter_zero_on_null          -- NULL state: zeroed snapshot
 *   3. test_counter_increments_on_oob_tap -- hand-write m* past work_buf;
 *                                             one tick MUST bump the counter
 *   4. test_reset_clears_counter          -- spu94_reset zeros the counter
 *
 * "Clean tick produces zero OOB" is NOT asserted here — the reverb body
 * reads at `m* - 2` (wraps to halfword 0xFFFE when m* = 0), so any work
 * buffer smaller than ~128 KB will register OOB taps even with all
 * registers zero. That property is validated at the higher-level
 * modulation harness (M1 close-out Step 6) where the full
 * SPU94_WORK_BUF_MAX_BYTES buffer is in play.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>

/* Tight work buffer (1024 bytes = 512 halfwords) so that any m-prefix or d-prefix
 * register with value > ~256 produces an OOB access once the reverb
 * body runs. Hall et al. need far more than 1024 bytes; that's the
 * contract ADR-0022 now enforces at load_preset time. For these tests
 * we intentionally bypass load_preset and hand-write a single m*
 * register to prove the OOB counter is wired. */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[1024];

void setUp(void) {}
void tearDown(void) {}

static spu94_state *fresh_state(void) {
    spu94_state *s = spu94_init(state_buf, sizeof state_buf,
                                work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(s);
    spu94_reset(s);
    return s;
}

/* Test 1: counter is zero immediately after spu94_init + spu94_reset. */
void test_counter_zero_after_init(void) {
    spu94_state *s = fresh_state();
    spu94_error_counters_t c = spu94_get_error_counters(s);
    TEST_ASSERT_EQUAL_UINT64(0u, c.oob_tap_count);
}

/* Test 2: NULL state returns a zeroed snapshot (null-safety convention). */
void test_counter_zero_on_null(void) {
    spu94_error_counters_t c = spu94_get_error_counters(NULL);
    TEST_ASSERT_EQUAL_UINT64(0u, c.oob_tap_count);
}

/* Test 3: ticking a freshly-reset state (all m-prefix or d-prefix registers = 0) must
 * not produce any OOB — every address fits comfortably inside the
 * 1024-byte work buffer. */
void test_counter_zero_through_clean_tick(void) {
    spu94_state *s = fresh_state();
    spu94_tick(s);
    spu94_error_counters_t c = spu94_get_error_counters(s);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0u, c.oob_tap_count,
        "clean tick (all-zero registers) must not produce any OOB tap");
}

/* Test 4: manually set an m* register past the work buffer, tick, and
 * prove the counter increments. mLSAME = 0x0400 → byte offset 0x0800
 * (2048) which lies outside [0, 1024). The reverb body's SAME-IIR
 * stage reads at mLSAME-2 on EVERY tick; the counter MUST observe
 * that miss. We don't care exactly how many accesses miss per tick
 * (comb stages add more) — we care that at least one miss registers.
 *
 * Skip load_preset entirely (ADR-0022 would reject a 1024-byte work
 * buffer for any non-Off preset); hand-write one register to stage
 * the OOB. */
void test_counter_increments_on_oob_tap(void) {
    spu94_state *s = fresh_state();

    /* Stage the OOB register + TICK_LATCHED commit. mLSAME is the
     * SAME-IIR wall-tap address; one of the first taps the reverb
     * body reads. */
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_set_reg_u16(s, SPU94_REG_mLSAME, (uint16_t)0x0400));
    /* First tick applies the pending-write commit AND runs the reverb
     * body. Because the register committed at the START of the tick,
     * the reverb body in the SAME tick observes mLSAME = 0x0400 and
     * attempts an OOB read. */
    spu94_tick(s);

    spu94_error_counters_t c = spu94_get_error_counters(s);
    TEST_ASSERT_TRUE_MESSAGE(c.oob_tap_count > 0u,
        "reverb body with m* past work_buf must increment oob_tap_count");
}

/* Test 4: spu94_reset clears accumulated OOB counts so the counter is
 * a per-session observable rather than a forever-accumulator. */
void test_reset_clears_counter(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_set_reg_u16(s, SPU94_REG_mLSAME, (uint16_t)0x0400));
    spu94_tick(s);
    TEST_ASSERT_TRUE(spu94_get_error_counters(s).oob_tap_count > 0u);

    spu94_reset(s);
    TEST_ASSERT_EQUAL_UINT64_MESSAGE(0u,
        spu94_get_error_counters(s).oob_tap_count,
        "spu94_reset must zero the OOB counter");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_counter_zero_after_init);
    RUN_TEST(test_counter_zero_on_null);
    RUN_TEST(test_counter_increments_on_oob_tap);
    RUN_TEST(test_reset_clears_counter);
    return UNITY_END();
}
