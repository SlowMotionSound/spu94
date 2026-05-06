/* tests/unit/interp/test_interp.c -- Phase 16 Plan 01 Task 1.
 *
 * TDD RED phase: unit tests for the preset interpolation engine.
 * Tests exercise all 5 INTERP requirements:
 *   INTERP-01: morph position maps to adjacent preset pair + fractional distance
 *   INTERP-02: linear interpolation of all 30 active registers between presets
 *   INTERP-03: fixed registers hold constant values regardless of morph
 *   INTERP-04: waypoint identity (bit-identical to Sony factory presets)
 *   INTERP-05: signed registers interpolate correctly through negative values
 *
 * Seven sub-tests:
 *   1. test_interp_null_state          -- NULL state no-crash (T-16-03)
 *   2. test_interp_clamp_below_zero    -- morph < 0.0 clamps to 0.0
 *   3. test_interp_clamp_above_one     -- morph > 1.0 clamps to 1.0
 *   4. test_interp_waypoint_identity   -- INTERP-04 (all 9 waypoints x 35 regs)
 *   5. test_interp_fixed_registers     -- INTERP-03 (5 fixed regs at 5 positions)
 *   6. test_interp_midpoint_linear     -- INTERP-02 (midpoint between Studio B/C)
 *   7. test_interp_signed_no_wraparound -- INTERP-05 (negative value interpolation)
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>

/* Static state and work buffer (following project canonical pattern). */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state = NULL;

/* Waypoint order (perceptual, confirmed by ear -- NOT enum order).
 * Index 0 = Half Echo (morph 0.0) through Index 8 = Delay (morph 1.0). */
static const spu94_preset_id_t waypoint_order[9] = {
    SPU94_PRESET_HALF_ECHO,   /* 0 -> morph 0/8 = 0.000 */
    SPU94_PRESET_ROOM,        /* 1 -> morph 1/8 = 0.125 */
    SPU94_PRESET_STUDIO_A,    /* 2 -> morph 2/8 = 0.250 */
    SPU94_PRESET_STUDIO_B,    /* 3 -> morph 3/8 = 0.375 */
    SPU94_PRESET_STUDIO_C,    /* 4 -> morph 4/8 = 0.500 */
    SPU94_PRESET_HALL,        /* 5 -> morph 5/8 = 0.625 */
    SPU94_PRESET_SPACE_ECHO,  /* 6 -> morph 6/8 = 0.750 */
    SPU94_PRESET_ECHO,        /* 7 -> morph 7/8 = 0.875 */
    SPU94_PRESET_DELAY        /* 8 -> morph 8/8 = 1.000 */
};

/* Helper: is this register one of the 5 fixed registers? */
static int is_fixed_reg(spu94_reg_t r) {
    return r == SPU94_REG_vLOUT ||
           r == SPU94_REG_vROUT ||
           r == SPU94_REG_vLIN  ||
           r == SPU94_REG_vRIN  ||
           r == SPU94_REG_mBASE;
}

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}

void tearDown(void) {
    state = NULL;
}

/* -------------------------------------------------------------------------
 * Test 1: NULL state is a no-op (must not crash). (T-16-03)
 * ------------------------------------------------------------------------- */
static void test_interp_null_state(void) {
    /* Should not crash or segfault. */
    spu94_interp_set_morph(NULL, 0.5f);
    /* If we got here, no crash. */
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Test 2: morph < 0.0 clamps to position 0.0 (Half Echo). (INTERP-01)
 * ------------------------------------------------------------------------- */
static void test_interp_clamp_below_zero(void) {
    spu94_interp_set_morph(state, -1.0f);

    /* One tick to flush any TICK_LATCHED registers. */
    spu94_tick(state);

    const spu94_preset_t *half_echo = &spu94_presets[SPU94_PRESET_HALF_ECHO];
    char msg[128];

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) {
            /* Fixed registers: check fixed values. */
            if (r == SPU94_REG_vLOUT || r == SPU94_REG_vROUT) {
                snprintf(msg, sizeof msg, "clamp_below reg=%d (vLOUT/vROUT)", r);
                TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFF,
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else if (r == SPU94_REG_vLIN || r == SPU94_REG_vRIN) {
                snprintf(msg, sizeof msg, "clamp_below reg=%d (vLIN/vRIN)", r);
                TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x8000,
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else { /* mBASE */
                snprintf(msg, sizeof msg, "clamp_below reg=%d (mBASE)", r);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)0x0000,
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        } else {
            /* Non-fixed registers: should match Half Echo preset. */
            snprintf(msg, sizeof msg, "clamp_below reg=%d", r);
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                TEST_ASSERT_EQUAL_INT16_MESSAGE(half_echo->regs[r],
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else {
                TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)half_echo->regs[r],
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Test 3: morph > 1.0 clamps to position 1.0 (Delay). (INTERP-01)
 * ------------------------------------------------------------------------- */
static void test_interp_clamp_above_one(void) {
    spu94_interp_set_morph(state, 2.0f);
    spu94_tick(state);

    const spu94_preset_t *delay = &spu94_presets[SPU94_PRESET_DELAY];
    char msg[128];

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) {
            if (r == SPU94_REG_vLOUT || r == SPU94_REG_vROUT) {
                snprintf(msg, sizeof msg, "clamp_above reg=%d (vLOUT/vROUT)", r);
                TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFF,
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else if (r == SPU94_REG_vLIN || r == SPU94_REG_vRIN) {
                snprintf(msg, sizeof msg, "clamp_above reg=%d (vLIN/vRIN)", r);
                TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x8000,
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else {
                snprintf(msg, sizeof msg, "clamp_above reg=%d (mBASE)", r);
                TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)0x0000,
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        } else {
            snprintf(msg, sizeof msg, "clamp_above reg=%d", r);
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                TEST_ASSERT_EQUAL_INT16_MESSAGE(delay->regs[r],
                    spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
            } else {
                TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)delay->regs[r],
                    spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Test 4: Waypoint identity -- at each of the 9 exact waypoint positions,
 * output registers are bit-identical to the Sony factory preset. (INTERP-04)
 * ------------------------------------------------------------------------- */
static void test_interp_waypoint_identity(void) {
    char msg[128];

    for (int w = 0; w < 9; w++) {
        float morph = (float)w / 8.0f;
        spu94_reset(state);
        spu94_interp_set_morph(state, morph);
        spu94_tick(state); /* Flush TICK_LATCHED registers. */

        const spu94_preset_t *preset = &spu94_presets[waypoint_order[w]];

        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            if (is_fixed_reg((spu94_reg_t)r)) {
                /* Fixed registers keep their constant values. */
                if (r == SPU94_REG_vLOUT || r == SPU94_REG_vROUT) {
                    snprintf(msg, sizeof msg, "waypoint=%d reg=%d (vLOUT/vROUT)", w, r);
                    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFF,
                        spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
                } else if (r == SPU94_REG_vLIN || r == SPU94_REG_vRIN) {
                    snprintf(msg, sizeof msg, "waypoint=%d reg=%d (vLIN/vRIN)", w, r);
                    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x8000,
                        spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
                } else {
                    snprintf(msg, sizeof msg, "waypoint=%d reg=%d (mBASE)", w, r);
                    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)0x0000,
                        spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
                }
            } else {
                /* Non-fixed registers must be bit-identical to the preset. */
                snprintf(msg, sizeof msg, "waypoint=%d reg=%d", w, r);
                if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
                    TEST_ASSERT_EQUAL_INT16_MESSAGE(preset->regs[r],
                        spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
                } else {
                    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)preset->regs[r],
                        spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
                }
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Test 5: Fixed registers hold constant values at multiple morph positions.
 * (INTERP-03)
 * ------------------------------------------------------------------------- */
static void test_interp_fixed_registers(void) {
    const float positions[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    char msg[128];

    for (int p = 0; p < 5; p++) {
        spu94_reset(state);
        spu94_interp_set_morph(state, positions[p]);
        spu94_tick(state);

        snprintf(msg, sizeof msg, "pos=%.4f vLOUT", (double)positions[p]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFF,
            spu94_get_reg_i16(state, SPU94_REG_vLOUT), msg);

        snprintf(msg, sizeof msg, "pos=%.4f vROUT", (double)positions[p]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x7FFF,
            spu94_get_reg_i16(state, SPU94_REG_vROUT), msg);

        snprintf(msg, sizeof msg, "pos=%.4f vLIN", (double)positions[p]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x8000,
            spu94_get_reg_i16(state, SPU94_REG_vLIN), msg);

        snprintf(msg, sizeof msg, "pos=%.4f vRIN", (double)positions[p]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)0x8000,
            spu94_get_reg_i16(state, SPU94_REG_vRIN), msg);

        snprintf(msg, sizeof msg, "pos=%.4f mBASE", (double)positions[p]);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)0x0000,
            spu94_get_reg_u16(state, SPU94_REG_mBASE), msg);
    }
}

/* -------------------------------------------------------------------------
 * Test 6: Midpoint interpolation -- morph 0.4375 is midway between
 * Studio B (waypoint 3, morph 3/8 = 0.375) and Studio C (waypoint 4,
 * morph 4/8 = 0.5). Each interpolating register should equal the integer
 * average of the two adjacent preset values. (INTERP-02)
 * ------------------------------------------------------------------------- */
static void test_interp_midpoint_linear(void) {
    /* Morph 0.4375 = midpoint of segment [3/8, 4/8] */
    spu94_interp_set_morph(state, 0.4375f);
    spu94_tick(state);

    const spu94_preset_t *studio_b = &spu94_presets[SPU94_PRESET_STUDIO_B];
    const spu94_preset_t *studio_c = &spu94_presets[SPU94_PRESET_STUDIO_C];
    char msg[128];

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;

        snprintf(msg, sizeof msg, "midpoint reg=%d", r);

        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            /* Signed midpoint: (a + b) / 2 with int16_t values. */
            int16_t va = studio_b->regs[r];
            int16_t vb = studio_c->regs[r];
            int32_t expected = ((int32_t)va + (int32_t)vb) / 2;
            TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)expected,
                spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
        } else {
            /* Unsigned midpoint: (a + b) / 2 with uint16_t values. */
            uint16_t va = (uint16_t)studio_b->regs[r];
            uint16_t vb = (uint16_t)studio_c->regs[r];
            uint32_t expected = ((uint32_t)va + (uint32_t)vb) / 2;
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expected,
                spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
        }
    }
}

/* -------------------------------------------------------------------------
 * Test 7: Signed interpolation -- verify v-prefix registers with negative
 * values interpolate correctly without unsigned wraparound. (INTERP-05)
 *
 * Specifically tests at morph 0.0625 (midpoint of segment 0: Half Echo -> Room).
 *   vCOMB2: Half Echo = 0xBCE0 = -17184, Room = 0xBED0 = -16688
 *     Expected = (-17184 + -16688) / 2 = -16936
 *   vWALL:  Half Echo = 0x8500 = -31488, Room = 0xBA80 = -17792
 *     Expected = (-31488 + -17792) / 2 = -24640
 * ------------------------------------------------------------------------- */
static void test_interp_signed_no_wraparound(void) {
    /* Morph 0.0625 = midpoint of segment [0/8, 1/8] = Half Echo -> Room */
    spu94_interp_set_morph(state, 0.0625f);
    spu94_tick(state);

    /* vCOMB2 test */
    int16_t vcomb2 = spu94_get_reg_i16(state, SPU94_REG_vCOMB2);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-16936, vcomb2,
        "vCOMB2 signed midpoint Half Echo->Room");

    /* vWALL test */
    int16_t vwall = spu94_get_reg_i16(state, SPU94_REG_vWALL);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)-24640, vwall,
        "vWALL signed midpoint Half Echo->Room");
}

/* -------------------------------------------------------------------------
 * Test 8: NaN and Infinity inputs produce valid state (T-16-02).
 * NaN must not cause UB -- negated comparisons route it to clamp.
 * +Inf clamps to 1.0 (Delay), -Inf clamps to 0.0 (Half Echo).
 * ------------------------------------------------------------------------- */
static void test_interp_nan_inf_safety(void) {
    #include <math.h>
    const spu94_preset_t *half_echo = &spu94_presets[SPU94_PRESET_HALF_ECHO];
    const spu94_preset_t *delay     = &spu94_presets[SPU94_PRESET_DELAY];
    char msg[128];

    /* NaN should produce Half Echo (same as position 0.0) */
    spu94_reset(state);
    spu94_interp_set_morph(state, NAN);
    spu94_tick(state);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        snprintf(msg, sizeof msg, "NaN reg=%d", r);
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(half_echo->regs[r],
                spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
        } else {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)half_echo->regs[r],
                spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
        }
    }

    /* +Infinity should produce Delay (same as position 1.0) */
    spu94_reset(state);
    spu94_interp_set_morph(state, INFINITY);
    spu94_tick(state);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        snprintf(msg, sizeof msg, "+Inf reg=%d", r);
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(delay->regs[r],
                spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
        } else {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)delay->regs[r],
                spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
        }
    }

    /* -Infinity should produce Half Echo (same as position 0.0) */
    spu94_reset(state);
    spu94_interp_set_morph(state, -INFINITY);
    spu94_tick(state);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        snprintf(msg, sizeof msg, "-Inf reg=%d", r);
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(half_echo->regs[r],
                spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
        } else {
            TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)half_echo->regs[r],
                spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
        }
    }
}

/* ========================================================================= */
/* Main                                                                      */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_interp_null_state);
    RUN_TEST(test_interp_clamp_below_zero);
    RUN_TEST(test_interp_clamp_above_one);
    RUN_TEST(test_interp_waypoint_identity);
    RUN_TEST(test_interp_fixed_registers);
    RUN_TEST(test_interp_midpoint_linear);
    RUN_TEST(test_interp_signed_no_wraparound);
    RUN_TEST(test_interp_nan_inf_safety);
    return UNITY_END();
}
