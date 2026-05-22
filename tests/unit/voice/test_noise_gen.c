/* tests/unit/voice/test_noise_gen.c
 * Phase 36 Plan 01: Unity unit tests for spu94_noise_gen (SPU global LFSR).
 *
 * Tests cover:
 *   NON-01: LFSR seed, deterministic sequence, parity formula
 *   NON-02: Timer decrement, reload, double-reload
 *   NON-03: Frequency varies with shift (per-voice pitch has no effect)
 *   NON-08: Single global generator (implicitly tested by shared state)
 */

#include "unity.h"
#include <spu94/spu94_noise.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------
 * NON-01: After init, level == 1 (seed), timer == 0, step == 4, shift == 0
 * --------------------------------------------------------------- */
void test_lfsr_seed_one(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);

    TEST_ASSERT_EQUAL_INT16(1, ng.level);
    TEST_ASSERT_EQUAL_INT32(0, ng.timer);
    TEST_ASSERT_EQUAL_UINT8(0, ng.shift);
    TEST_ASSERT_EQUAL_UINT8(4, ng.step);
}

/* ---------------------------------------------------------------
 * NON-01: Deterministic LFSR sequence from seed=1.
 * At shift=15 (reload=4), step=4, LFSR shifts every tick.
 * Expected sequence: 1 -> 3 -> 7 -> 15 -> 31
 * (left shift + parity bit 1 while all tapped bits are 0)
 * --------------------------------------------------------------- */
void test_lfsr_deterministic_sequence(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);
    ng.shift = 15;  /* reload = 0x20000 >> 15 = 4 */

    /* Tick 1: timer=0-4=-4, parity from level=1 -> bit15=0,bit12=0,bit11=0,bit10=0 -> parity=1
     * level = (1<<1)|1 = 3. timer=-4+4=0. */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(3, ng.level,
        "NON-01: After 1 tick at shift=15, level should be 3 (seed 1 << 1 | parity 1)");

    /* Tick 2: parity from level=3 -> 0^0^0^0^1=1, level=(3<<1)|1=7 */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, ng.level,
        "NON-01: After 2 ticks, level should be 7");

    /* Tick 3: parity from level=7 -> 0^0^0^0^1=1, level=(7<<1)|1=15 */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(15, ng.level,
        "NON-01: After 3 ticks, level should be 15");

    /* Tick 4: parity from level=15 -> 0^0^0^0^1=1, level=(15<<1)|1=31 */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(31, ng.level,
        "NON-01: After 4 ticks, level should be 31");
}

/* ---------------------------------------------------------------
 * NON-02: Timer decrement and reload.
 * shift=0 (reload=0x20000=131072), step=4.
 * Tick 1: timer=0-4=-4, shifts LFSR, timer=-4+131072=131068. No double-reload.
 * Tick 2: timer=131068-4=131064 >= 0, no LFSR shift. Level unchanged.
 * --------------------------------------------------------------- */
void test_timer_decrement_and_reload(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);
    /* shift=0 (default), step=4 (default) */

    /* Tick 1: timer goes negative, LFSR shifts, timer reloads */
    spu94_noise_gen_tick(&ng);
    int16_t after_tick1 = ng.level;
    TEST_ASSERT_EQUAL_INT16_MESSAGE(3, after_tick1,
        "NON-02: First tick should shift LFSR (timer underflowed from 0)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(131068, ng.timer,
        "NON-02: Timer should reload to 0x20000 - step = 131068");

    /* Tick 2: timer=131068-4=131064 >= 0, no shift */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(after_tick1, ng.level,
        "NON-02: Second tick should NOT shift LFSR (timer still positive)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(131064, ng.timer,
        "NON-02: Timer should be 131064 after second tick");
}

/* ---------------------------------------------------------------
 * NON-02: Double-reload without re-shifting.
 * shift=15 (reload=4), step=7.
 * Tick 1: timer=0-7=-7, shifts LFSR once, timer=-7+4=-3, still<0, timer=-3+4=1.
 * Only ONE LFSR shift despite two reloads.
 * --------------------------------------------------------------- */
void test_double_reload(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);
    ng.shift = 15;
    ng.step = 7;

    /* Tick 1: double-reload but only one shift */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(3, ng.level,
        "NON-02: Double-reload: level should be 3 (only ONE shift from seed 1)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(1, ng.timer,
        "NON-02: Double-reload: timer=-7+4+4=1");

    /* Tick 2: timer=1-7=-6, shifts once, timer=-6+4=-2, timer=-2+4=2 */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(7, ng.level,
        "NON-02: Second tick after double-reload: level should be 7 (one more shift)");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(2, ng.timer,
        "NON-02: Timer after second double-reload tick should be 2");
}

/* ---------------------------------------------------------------
 * NON-03: Different shift values produce different LFSR update rates.
 * Higher shift = more frequent updates (shorter timer reload).
 * --------------------------------------------------------------- */
void test_frequency_varies_with_shift(void) {
    /* Generator A: shift=15, step=4 -> reload=4. Shifts every tick. */
    spu94_noise_gen_t ng_fast;
    spu94_noise_gen_init(&ng_fast);
    ng_fast.shift = 15;

    /* Generator B: shift=14, step=4 -> reload=8. Shifts every 2nd tick. */
    spu94_noise_gen_t ng_slow;
    spu94_noise_gen_init(&ng_slow);
    ng_slow.shift = 14;

    int shifts_fast = 0, shifts_slow = 0;
    int16_t prev_fast = ng_fast.level;
    int16_t prev_slow = ng_slow.level;

    for (int i = 0; i < 100; i++) {
        spu94_noise_gen_tick(&ng_fast);
        spu94_noise_gen_tick(&ng_slow);
        if (ng_fast.level != prev_fast) shifts_fast++;
        if (ng_slow.level != prev_slow) shifts_slow++;
        prev_fast = ng_fast.level;
        prev_slow = ng_slow.level;
    }

    /* shift=15 should update ~100 times, shift=14 should update ~50 times */
    TEST_ASSERT_GREATER_THAN_MESSAGE(shifts_slow, shifts_fast,
        "NON-03: Higher shift should produce more LFSR shifts than lower shift");
    TEST_ASSERT_GREATER_THAN_MESSAGE(10, shifts_fast,
        "NON-03: shift=15 should have many updates in 100 ticks");
}

/* ---------------------------------------------------------------
 * NON-01: Parity computed from PRE-shift level, not post-shift.
 * Set level to a value where specific tap bits determine parity.
 *
 * level=0x0800 -> bit15=0, bit12=0, bit11=1, bit10=0 -> parity = 0^0^1^0^1 = 0
 * After shift: level = (0x0800 << 1) | 0 = 0x1000
 *
 * Contrast: if parity were computed from POST-shift level 0x1000:
 *   bit15=0, bit12=1, bit11=0, bit10=0 -> parity = 0^1^0^0^1 = 0 (same here)
 * Use a different value to distinguish:
 *
 * level=0x9C00 -> bit15=1, bit12=1, bit11=1, bit10=1 -> parity = 1^1^1^1^1 = 1
 * After shift: level = (0x9C00 << 1) | 1 = 0x3801
 * If parity from POST-shift (0x3801): bit15=0, bit12=0, bit11=1, bit10=1 -> 0^0^1^1^1 = 1
 * Same parity! Need different approach.
 *
 * Actually use: level=0x0C00 -> bit15=0, bit12=1, bit11=1, bit10=0 -> parity = 0^1^1^0^1 = 1
 * After shift: (0x0C00 << 1) | 1 = 0x1801
 * If parity from post-shift (0x1801): bit15=0, bit12=1, bit11=1, bit10=0 -> 0^1^1^0^1 = 1
 * Still same! The distinguishing test requires comparing the SEQUENCE, not a single step.
 * Since the parity formula is symmetric enough, the easiest test is to verify that
 * the sequence after 10+ shifts matches the reference Python-computed values.
 *
 * We verify deterministic sequence at shift=15 from a non-trivial seed.
 * --------------------------------------------------------------- */
void test_parity_from_pre_shift_level(void) {
    spu94_noise_gen_t ng;
    spu94_noise_gen_init(&ng);
    ng.shift = 15;
    ng.step = 4;

    /* Run 10 ticks to get past the trivial all-ones-parity region */
    for (int i = 0; i < 10; i++) {
        spu94_noise_gen_tick(&ng);
    }

    /* After 10 ticks at shift=15 from seed=1, level should be:
     * 1->3->7->15->31->63->127->255->511->1023->2047
     * Tick 10: level=0x07FF (2047)
     * Tick 11: parity from 0x07FF -> bit15=0,bit12=0,bit11=0,bit10=1 -> 0^0^0^1^1 = 0
     *          level = (0x07FF << 1) | 0 = 0x0FFE (4094) */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x07FF, (uint16_t)ng.level,
        "NON-01: After 10 ticks at shift=15, level should be 0x07FF (2047)");

    /* Tick 11: bit10 becomes 1, flipping parity to 0 (inserting 0 instead of 1) */
    spu94_noise_gen_tick(&ng);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0FFE, (uint16_t)ng.level,
        "NON-01: Parity from pre-shift 0x07FF should be 0, giving 0x0FFE after shift");
}

/* ---------------------------------------------------------------
 * Main
 * --------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_lfsr_seed_one);
    RUN_TEST(test_lfsr_deterministic_sequence);
    RUN_TEST(test_timer_decrement_and_reload);
    RUN_TEST(test_double_reload);
    RUN_TEST(test_frequency_varies_with_shift);
    RUN_TEST(test_parity_from_pre_shift_level);
    return UNITY_END();
}
