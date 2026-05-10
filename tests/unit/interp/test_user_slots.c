/* tests/unit/interp/test_user_slots.c
 *
 * User-programmable waypoint slots (8 midpoints between Sony's 9 anchors).
 *
 * Sub-tests:
 *   1. NULL state / out-of-range slot -- API guards (no crash, no write)
 *   2. All slots empty -> bit-identical to v1.5 Sony-only interp at every
 *      waypoint position (extends INTERP-04 contract trivially)
 *   3. All slots empty -> midpoint between Sony anchors matches the v1.5
 *      math (transparent slots)
 *   4. Set / get / clear / is_filled round-trip
 *   5. Filled slot at exact midpoint position -> bit-identical to slot regs
 *      (extended INTERP-04: bit-identity at user waypoints too)
 *   6. Filled slot inserts a real segment break -- midpoint sample no longer
 *      equals the linear blend of the surrounding Sony anchors
 *   7. Sony anchor positions remain bit-identical with all slots filled
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *state = NULL;

/* Same perceptual order used by the interp engine. */
static const spu94_preset_id_t waypoint_order[9] = {
    SPU94_PRESET_HALF_ECHO, SPU94_PRESET_ROOM,     SPU94_PRESET_STUDIO_A,
    SPU94_PRESET_STUDIO_B,  SPU94_PRESET_STUDIO_C, SPU94_PRESET_HALL,
    SPU94_PRESET_SPACE_ECHO, SPU94_PRESET_ECHO,    SPU94_PRESET_DELAY
};

static int is_fixed_reg(spu94_reg_t r) {
    return r == SPU94_REG_vLOUT || r == SPU94_REG_vROUT ||
           r == SPU94_REG_vLIN  || r == SPU94_REG_vRIN  ||
           r == SPU94_REG_mBASE;
}

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}

void tearDown(void) { state = NULL; }

/* ---------- 1. API guards ---------- */
static void test_user_slot_api_guards(void) {
    int16_t buf[SPU94_REG__COUNT] = {0};

    /* NULL state: no crash, no effect. */
    spu94_interp_set_user_slot(NULL, 0, buf);
    spu94_interp_clear_user_slot(NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(NULL, 0));
    spu94_interp_get_user_slot(NULL, 0, buf);

    /* Out-of-range slot: no-op. */
    spu94_interp_set_user_slot(state, -1, buf);
    spu94_interp_set_user_slot(state, 8,  buf);
    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(state, -1));
    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(state, 8));

    /* NULL regs on set: no-op (slot stays empty). */
    spu94_interp_set_user_slot(state, 0, NULL);
    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(state, 0));
}

/* ---------- 2. All empty + Sony positions = bit-identical Sony presets ---------- */
static void test_empty_slots_sony_positions_bit_identical(void) {
    for (int w = 0; w < 9; w++) {
        float pos = (float)w / 8.0f;
        spu94_interp_set_morph(state, pos);
        spu94_tick(state);  /* flush TICK_LATCHED m/d-prefix registers */
        const int16_t *expected = spu94_presets[waypoint_order[w]].regs;
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            if (is_fixed_reg((spu94_reg_t)r)) continue;
            int16_t got = (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
                ? spu94_get_reg_i16(state, (spu94_reg_t)r)
                : (int16_t)spu94_get_reg_u16(state, (spu94_reg_t)r);
            TEST_ASSERT_EQUAL_INT16(expected[r], got);
        }
    }
}

/* ---------- 3. All empty + midpoint = v1.5 linear blend of two Sony anchors ---------- */
static void test_empty_slots_midpoint_matches_v15(void) {
    /* Position 0.0625 = halfway between Sony[0] and Sony[1]. With all slots
     * empty, must equal a linear blend (matches v1.5 behavior). */
    spu94_interp_set_morph(state, 0.0625f);
    spu94_tick(state);
    const int16_t *a = spu94_presets[waypoint_order[0]].regs;
    const int16_t *b = spu94_presets[waypoint_order[1]].regs;
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        int32_t expected;
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            int16_t va = a[r], vb = b[r];
            expected = (int32_t)va + (int32_t)((float)(vb - va) * 0.5f);
            int16_t got = spu94_get_reg_i16(state, (spu94_reg_t)r);
            TEST_ASSERT_EQUAL_INT16((int16_t)expected, got);
        } else {
            uint16_t va = (uint16_t)a[r], vb = (uint16_t)b[r];
            expected = (int32_t)va + (int32_t)((float)((int32_t)vb - (int32_t)va) * 0.5f);
            uint16_t got = spu94_get_reg_u16(state, (spu94_reg_t)r);
            TEST_ASSERT_EQUAL_UINT16((uint16_t)expected, got);
        }
    }
}

/* ---------- 4. set / get / clear / is_filled round-trip ---------- */
static void test_user_slot_round_trip(void) {
    int16_t in[SPU94_REG__COUNT];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) in[r] = (int16_t)(r * 37 - 100);

    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(state, 3));

    spu94_interp_set_user_slot(state, 3, in);
    TEST_ASSERT_EQUAL_INT(1, spu94_interp_user_slot_is_filled(state, 3));

    int16_t out[SPU94_REG__COUNT] = {0};
    spu94_interp_get_user_slot(state, 3, out);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++)
        TEST_ASSERT_EQUAL_INT16(in[r], out[r]);

    spu94_interp_clear_user_slot(state, 3);
    TEST_ASSERT_EQUAL_INT(0, spu94_interp_user_slot_is_filled(state, 3));
}

/* ---------- 5. Filled slot at its exact midpoint = bit-identical slot regs ---------- */
static void test_filled_slot_bit_identical_at_midpoint(void) {
    /* Use Sony Hall regs as the slot 0 contents -- guaranteed valid in-range. */
    const int16_t *hall = spu94_presets[SPU94_PRESET_HALL].regs;
    spu94_interp_set_user_slot(state, 0, hall);

    /* Slot 0 lives at position 1/16 = 0.0625. */
    spu94_interp_set_morph(state, 1.0f / 16.0f);
    spu94_tick(state);
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        int16_t got = (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
            ? spu94_get_reg_i16(state, (spu94_reg_t)r)
            : (int16_t)spu94_get_reg_u16(state, (spu94_reg_t)r);
        TEST_ASSERT_EQUAL_INT16(hall[r], got);
    }
}

/* ---------- 6. Filled slot creates a real segment break ---------- */
static void test_filled_slot_changes_interpolation(void) {
    /* Without slot: position 0.0625 is the linear blend of Sony[0] and Sony[1].
     * With slot 0 filled to Hall regs: position 0.0625 IS Hall (bit-identical).
     * The two states must differ on at least one non-fixed register --
     * proving the slot inserts a real waypoint, not a no-op pass-through. */
    spu94_interp_set_morph(state, 0.0625f);
    spu94_tick(state);
    int16_t before[SPU94_REG__COUNT];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        before[r] = (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
            ? spu94_get_reg_i16(state, (spu94_reg_t)r)
            : (int16_t)spu94_get_reg_u16(state, (spu94_reg_t)r);
    }

    const int16_t *hall = spu94_presets[SPU94_PRESET_HALL].regs;
    spu94_interp_set_user_slot(state, 0, hall);
    spu94_interp_set_morph(state, 0.0625f);
    spu94_tick(state);

    int differences = 0;
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (is_fixed_reg((spu94_reg_t)r)) continue;
        int16_t got = (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
            ? spu94_get_reg_i16(state, (spu94_reg_t)r)
            : (int16_t)spu94_get_reg_u16(state, (spu94_reg_t)r);
        if (got != before[r]) differences++;
    }
    TEST_ASSERT_GREATER_THAN_INT(0, differences);
}

/* ---------- 7. Sony positions stay bit-identical with all 8 slots filled ---------- */
static void test_sony_positions_intact_with_all_slots_filled(void) {
    /* Fill every slot with arbitrary registers. */
    int16_t junk[SPU94_REG__COUNT];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) junk[r] = (int16_t)((r * 91) ^ 0x4321);
    for (int s = 0; s < 8; s++) spu94_interp_set_user_slot(state, s, junk);

    /* Each Sony position must still produce its exact preset regs. */
    for (int w = 0; w < 9; w++) {
        float pos = (float)w / 8.0f;
        spu94_interp_set_morph(state, pos);
        spu94_tick(state);  /* flush TICK_LATCHED m/d-prefix registers */
        const int16_t *expected = spu94_presets[waypoint_order[w]].regs;
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            if (is_fixed_reg((spu94_reg_t)r)) continue;
            int16_t got = (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
                ? spu94_get_reg_i16(state, (spu94_reg_t)r)
                : (int16_t)spu94_get_reg_u16(state, (spu94_reg_t)r);
            TEST_ASSERT_EQUAL_INT16(expected[r], got);
        }
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_user_slot_api_guards);
    RUN_TEST(test_empty_slots_sony_positions_bit_identical);
    RUN_TEST(test_empty_slots_midpoint_matches_v15);
    RUN_TEST(test_user_slot_round_trip);
    RUN_TEST(test_filled_slot_bit_identical_at_midpoint);
    RUN_TEST(test_filled_slot_changes_interpolation);
    RUN_TEST(test_sony_positions_intact_with_all_slots_filled);
    return UNITY_END();
}
