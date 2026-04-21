/* tests/unit/preset/test_preset_load_all.c -- Phase 5 Plan 03 Task 2.
 *
 * API-05 + D-08 proof: spu94_load_preset honors the Phase 2 split write
 * policy. Every v-prefix gain register and mBASE (IMMEDIATE policy) is
 * active-slot-visible immediately after load; every d-prefix and m-prefix
 * address/delay register (TICK_LATCHED policy) stages to the pending slot,
 * leaving the active slot unchanged until the next spu94_tick flushes the
 * pending-to-active transition.
 *
 * Six sub-tests (RUN_TEST calls):
 *   1. test_load_null_state_ok           -- NULL-safe per lifecycle convention
 *   2. test_load_out_of_range_id         -- SPU94_UNKNOWN_REG + no mutation
 *   3. test_load_each_preset_immediate_active
 *                                        -- 12 v-prefix I16 regs active-visible
 *                                           immediately for every preset
 *   4. test_load_each_preset_mbase_immediate_active
 *                                        -- mBASE (the sole IMMEDIATE U16)
 *                                           active-visible immediately
 *   5. test_load_each_preset_pending_staged
 *                                        -- 22 TICK_LATCHED U16 regs: pending
 *                                           holds preset value, active still 0
 *   6. test_load_post_tick_commits_latched
 *                                        -- after spu94_tick, every register's
 *                                           active equals preset value
 *
 * Sub-tests 3-6 each loop over all 10 presets internally (one RUN_TEST per
 * sub-function). Failure messages encode "preset=N reg=R" for cell-specific
 * diagnosis.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}
void tearDown(void) { state = NULL; }

/* Helper: is this U16 register IMMEDIATE? Only mBASE. All other U16 regs
 * (22 d-prefix / m-prefix address/delay registers) are TICK_LATCHED per
 * ADR-0005 / spu94_write_policy_table. */
static int is_immediate_u16(spu94_reg_t r) {
    return r == SPU94_REG_mBASE;
}

/* Test 1: NULL state -> returns SPU94_OK (lifecycle-null-safe convention,
 * D-12). Must not crash. */
static void test_load_null_state_ok(void) {
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
        (int)spu94_load_preset(NULL, SPU94_PRESET_HALL));
}

/* Test 2: out-of-range id -> SPU94_UNKNOWN_REG + no register mutation.
 * Proves the T-5-3 mitigation: a bad id leaves state as-is. */
static void test_load_out_of_range_id(void) {
    TEST_ASSERT_EQUAL_INT((int)SPU94_UNKNOWN_REG,
        (int)spu94_load_preset(state, SPU94_PRESET__COUNT));
    /* Every register -- both active slots (I16 readers) and U16 readers --
     * must still be zero (the post-spu94_reset baseline). */
    for (int r = 0; r < SPU94_REG__COUNT; r++) {
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            TEST_ASSERT_EQUAL_INT16(0,
                spu94_get_reg_i16(state, (spu94_reg_t)r));
        } else {
            TEST_ASSERT_EQUAL_UINT16(0,
                spu94_get_reg_u16(state, (spu94_reg_t)r));
        }
    }
}

/* Test 3: every I16 register (12 v-prefix gain regs) has active-slot value
 * matching the preset table immediately after load, for every preset. */
static void test_load_each_preset_immediate_active(void) {
    for (int id = 0; id < SPU94_PRESET__COUNT; id++) {
        spu94_reset(state);
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_load_preset(state, (spu94_preset_id_t)id));
        const spu94_preset_t *p = &spu94_presets[id];
        for (int r = 0; r < SPU94_REG__COUNT; r++) {
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                char msg[96];
                snprintf(msg, sizeof msg,
                         "preset=%d reg=%d (I16) active mismatch", id, r);
                TEST_ASSERT_EQUAL_INT16_MESSAGE(p->regs[r],
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            }
        }
    }
}

/* Test 4: mBASE (the single IMMEDIATE U16 register) has active-slot value
 * matching the preset immediately after load, for every preset. mBASE's
 * IMMEDIATE policy also fires spu94_mbase_on_write (snap-on-write per
 * ADR-0006); that side effect is covered by Phase 2 buffer tests. */
static void test_load_each_preset_mbase_immediate_active(void) {
    for (int id = 0; id < SPU94_PRESET__COUNT; id++) {
        spu94_reset(state);
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_load_preset(state, (spu94_preset_id_t)id));
        const uint16_t expected =
            (uint16_t)spu94_presets[id].regs[SPU94_REG_mBASE];
        char msg[96];
        snprintf(msg, sizeof msg, "preset=%d mBASE active mismatch", id);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected,
            spu94_get_reg_u16(state, SPU94_REG_mBASE), msg);
    }
}

/* Test 5: every TICK_LATCHED U16 register (22 d-prefix and m-prefix
 * address/delay regs, excluding mBASE) has the preset value in the
 * pending slot, while the active slot remains zero (post-reset value).
 * This is the D-08 "half-applied window" contract -- the one-tick latency
 * before d-prefix and m-prefix commit. */
static void test_load_each_preset_pending_staged(void) {
    for (int id = 0; id < SPU94_PRESET__COUNT; id++) {
        spu94_reset(state);
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_load_preset(state, (spu94_preset_id_t)id));
        const spu94_preset_t *p = &spu94_presets[id];
        for (int r = 0; r < SPU94_REG__COUNT; r++) {
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_U16 &&
                !is_immediate_u16((spu94_reg_t)r)) {
                const uint16_t expected = (uint16_t)p->regs[r];
                char msg[128];
                snprintf(msg, sizeof msg,
                         "preset=%d reg=%d (U16 TICK_LATCHED) pending mismatch",
                         id, r);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE(expected,
                    spu94_get_reg_u16_pending(state, (spu94_reg_t)r), msg);
                /* Active-slot must still be zero (pre-load value; state
                 * was just reset). */
                snprintf(msg, sizeof msg,
                         "preset=%d reg=%d (U16 TICK_LATCHED) active should be 0",
                         id, r);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE(0,
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        }
    }
}

/* Test 6: after one spu94_tick following a preset load, every register's
 * active value equals the preset value. This is the D-08 commit gate --
 * the tick flushes pending-to-active per ADR-0005 / spu94_apply_pending_writes,
 * so TICK_LATCHED regs catch up to the IMMEDIATE ones. */
static void test_load_post_tick_commits_latched(void) {
    for (int id = 0; id < SPU94_PRESET__COUNT; id++) {
        spu94_reset(state);
        TEST_ASSERT_EQUAL_INT((int)SPU94_OK,
            (int)spu94_load_preset(state, (spu94_preset_id_t)id));
        spu94_tick(state);
        const spu94_preset_t *p = &spu94_presets[id];
        for (int r = 0; r < SPU94_REG__COUNT; r++) {
            char msg[96];
            snprintf(msg, sizeof msg,
                     "preset=%d reg=%d active mismatch after tick", id, r);
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                TEST_ASSERT_EQUAL_INT16_MESSAGE(p->regs[r],
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else {
                TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)p->regs[r],
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_load_null_state_ok);
    RUN_TEST(test_load_out_of_range_id);
    RUN_TEST(test_load_each_preset_immediate_active);
    RUN_TEST(test_load_each_preset_mbase_immediate_active);
    RUN_TEST(test_load_each_preset_pending_staged);
    RUN_TEST(test_load_post_tick_commits_latched);
    return UNITY_END();
}
