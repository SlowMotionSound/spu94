/* src/spu94/spu94_interp.c -- Phase 16 Plan 01 Task 2.
 *
 * Preset interpolation engine: maps a morph position (0.0 to 1.0) to
 * linearly interpolated SPU register values along a 9-preset continuum.
 *
 * Requirements fulfilled:
 *   INTERP-01: position -> (preset pair, fraction) mapping
 *   INTERP-02: linear interpolation of all 30 active registers
 *   INTERP-03: 5 fixed registers hold constant values
 *   INTERP-04: exact waypoint positions produce bit-identical preset registers
 *   INTERP-05: signed v-prefix registers interpolate correctly through negatives
 *
 * Threat mitigations:
 *   T-16-01: position clamped to [0.0, 1.0]
 *   T-16-02: NaN/Inf handled (NaN comparisons are false -> falls to seg=0 frac=0.0)
 *   T-16-03: NULL state guard at entry
 *
 * Performance: O(35) per call. No heap, no locks, no syscalls. rt_safety clean.
 */
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include "spu94_state_internal.h"
#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Waypoint table: 9 Sony factory presets in perceptual order.
 * Stored in .rodata -- no runtime allocation.
 * ------------------------------------------------------------------------- */
static const spu94_preset_id_t spu94_interp_waypoints[SPU94_INTERP_WAYPOINT_COUNT] = {
    SPU94_PRESET_HALF_ECHO,   /* 0: morph 0/8 = 0.000 */
    SPU94_PRESET_ROOM,        /* 1: morph 1/8 = 0.125 */
    SPU94_PRESET_STUDIO_A,    /* 2: morph 2/8 = 0.250 */
    SPU94_PRESET_STUDIO_B,    /* 3: morph 3/8 = 0.375 */
    SPU94_PRESET_STUDIO_C,    /* 4: morph 4/8 = 0.500 */
    SPU94_PRESET_HALL,        /* 5: morph 5/8 = 0.625 */
    SPU94_PRESET_SPACE_ECHO,  /* 6: morph 6/8 = 0.750 */
    SPU94_PRESET_ECHO,        /* 7: morph 7/8 = 0.875 */
    SPU94_PRESET_DELAY        /* 8: morph 8/8 = 1.000 */
};

/* -------------------------------------------------------------------------
 * Fixed register check.
 * These 5 registers are never interpolated -- they hold constant values
 * regardless of morph position (INTERP-03).
 * ------------------------------------------------------------------------- */
static int spu94_interp_is_fixed(spu94_reg_t r) {
    return r == SPU94_REG_vLOUT ||
           r == SPU94_REG_vROUT ||
           r == SPU94_REG_vLIN  ||
           r == SPU94_REG_vRIN  ||
           r == SPU94_REG_mBASE;
}

/* -------------------------------------------------------------------------
 * Waypoint accessors keyed by 1/16ths-of-the-dial index (0..16).
 *   Even idx (0,2,...,16) -> Sony anchor at spu94_interp_waypoints[idx/2].
 *   Odd  idx (1,3,...,15) -> User slot at user_slots[(idx-1)/2], present
 *                            only when user_slot_filled[(idx-1)/2] != 0.
 * Sony anchors guarantee termination of the left/right walks below.
 * ------------------------------------------------------------------------- */
static int spu94_interp_waypoint_is_filled(const spu94_state *state, int idx16) {
    if ((idx16 & 1) == 0) return 1;
    return state->user_slot_filled[(idx16 - 1) >> 1] != 0;
}

static const int16_t *spu94_interp_waypoint_regs(const spu94_state *state, int idx16) {
    if ((idx16 & 1) == 0) {
        return spu94_presets[spu94_interp_waypoints[idx16 >> 1]].regs;
    }
    return state->user_slots[(idx16 - 1) >> 1];
}

/* -------------------------------------------------------------------------
 * spu94_interp_set_morph -- the interpolation engine entry point.
 * ------------------------------------------------------------------------- */
void spu94_interp_set_morph(spu94_state *state, float position) {
    /* T-16-03: NULL guard. */
    if (!state) return;

    /* T-16-01: Clamp position to [0.0, 1.0].
     * T-16-02: NaN fails both comparisons, falls through with position
     * unchanged. Negated comparisons catch NaN: IEEE 754 says NaN >= x is
     * always false, so !(NaN >= 0.0f) is true, routing NaN into the clamp. */
    if (!(position >= 0.0f)) position = 0.0f;
    if (!(position <= 1.0f)) position = 1.0f;

    /* Work in 1/16ths-of-the-dial space: 17 possible waypoints (9 Sony +
     * 8 user slots interleaved). Sony anchors at even indices are always
     * present; user slots at odd indices are present only when filled. */
    float scaled = position * 16.0f;            /* 0..16 */
    int   left   = (int)scaled;                  /* 0..16 */
    if (left >= 16) left = 16;                   /* clamp endpoint */
    int   right  = left + 1;
    if (right > 16) right = 16;

    /* Walk to the nearest filled waypoints. Index 0 and 16 are Sony anchors
     * (always filled), so the walks always terminate. */
    while (!spu94_interp_waypoint_is_filled(state, left))  left--;
    while (right <= 16 && !spu94_interp_waypoint_is_filled(state, right)) right++;

    float frac;
    if (left == right) {
        /* Position exactly at the endpoint (1.0): write the single waypoint
         * directly via the frac == 0 carve-out below. */
        frac = 0.0f;
    } else {
        frac = (scaled - (float)left) / (float)(right - left);
    }

    const int16_t *a = spu94_interp_waypoint_regs(state, left);
    const int16_t *b = spu94_interp_waypoint_regs(state, right);

    /* Iterate all 35 registers. */
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        const spu94_reg_t reg = (spu94_reg_t)r;

        /* INTERP-03: Fixed registers always get their constant values. */
        if (spu94_interp_is_fixed(reg)) {
            if (reg == SPU94_REG_vLOUT || reg == SPU94_REG_vROUT) {
                spu94_set_reg_i16(state, reg, (int16_t)0x7FFF);
            } else if (reg == SPU94_REG_vLIN || reg == SPU94_REG_vRIN) {
                spu94_set_reg_i16(state, reg, (int16_t)0x8000);
            } else { /* mBASE */
                spu94_set_reg_u16(state, reg, (uint16_t)0x0000);
            }
            continue;
        }

        /* INTERP-04 (extended): At exact waypoint positions (frac == 0.0 or
         * 1.0) write the source value directly to avoid floating-point drift.
         * Now applies at any filled waypoint -- Sony anchor or user slot. */
        if (frac == 0.0f) {
            if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16) {
                spu94_set_reg_i16(state, reg, a[r]);
            } else {
                spu94_set_reg_u16(state, reg, (uint16_t)a[r]);
            }
            continue;
        }
        if (frac == 1.0f) {
            if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16) {
                spu94_set_reg_i16(state, reg, b[r]);
            } else {
                spu94_set_reg_u16(state, reg, (uint16_t)b[r]);
            }
            continue;
        }

        /* INTERP-02 + INTERP-05: Interpolate between a[r] and b[r].
         * Dispatch by register signedness to handle negative values correctly. */
        if (spu94_reg_type(reg) == SPU94_REG_TYPE_I16) {
            /* Signed interpolation (INTERP-05).
             * Treats values as int16_t so negative numbers like vCOMB2 = -17184
             * interpolate through the signed range, not the unsigned range. */
            int16_t va = a[r];
            int16_t vb = b[r];
            int32_t interp = (int32_t)va + (int32_t)((float)(vb - va) * frac);
            /* Safety clamp to int16 range (should never trigger with Sony presets). */
            if (interp > 32767) interp = 32767;
            if (interp < -32768) interp = -32768;
            spu94_set_reg_i16(state, reg, (int16_t)interp);
        } else {
            /* Unsigned interpolation.
             * d-prefix and m-prefix registers are always positive buffer offsets.
             * Use int32 for the subtraction (vb could be < va). */
            uint16_t va = (uint16_t)a[r];
            uint16_t vb = (uint16_t)b[r];
            int32_t interp = (int32_t)va + (int32_t)((float)((int32_t)vb - (int32_t)va) * frac);
            if (interp > 65535) interp = 65535;
            if (interp < 0) interp = 0;
            spu94_set_reg_u16(state, reg, (uint16_t)interp);
        }
    }
}

/* -------------------------------------------------------------------------
 * User-programmable waypoint slot accessors.
 * ------------------------------------------------------------------------- */
void spu94_interp_set_user_slot(spu94_state *state, int slot,
                                const int16_t regs[SPU94_REG__COUNT]) {
    if (!state || !regs) return;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        state->user_slots[slot][r] = regs[r];
    }
    state->user_slot_filled[slot] = 1u;
}

void spu94_interp_clear_user_slot(spu94_state *state, int slot) {
    if (!state) return;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;
    state->user_slot_filled[slot] = 0u;
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        state->user_slots[slot][r] = 0;
    }
}

int spu94_interp_user_slot_is_filled(const spu94_state *state, int slot) {
    if (!state) return 0;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return 0;
    return state->user_slot_filled[slot] != 0u;
}

void spu94_interp_get_user_slot(const spu94_state *state, int slot,
                                int16_t out[SPU94_REG__COUNT]) {
    if (!state || !out) return;
    if (slot < 0 || slot >= SPU94_INTERP_USER_SLOT_COUNT) return;
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        out[r] = state->user_slots[slot][r];
    }
}
