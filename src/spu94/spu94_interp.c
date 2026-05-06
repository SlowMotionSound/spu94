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

    /* Map position to segment index and fractional distance within segment.
     * 9 waypoints define 8 segments (indices 0..7). */
    float scaled = position * (float)(SPU94_INTERP_WAYPOINT_COUNT - 1); /* 0..8 */
    int seg = (int)scaled;          /* segment index 0..7 (or 8 at endpoint) */
    float frac = scaled - (float)seg; /* fractional distance within segment */

    /* Clamp segment to valid range (handles position == 1.0 exactly,
     * where scaled == 8.0 and seg == 8). */
    if (seg >= SPU94_INTERP_WAYPOINT_COUNT - 1) {
        seg = SPU94_INTERP_WAYPOINT_COUNT - 2;  /* last valid segment = 7 */
        frac = 1.0f;
    }

    /* Get pointers to the two adjacent preset register arrays. */
    const int16_t *a = spu94_presets[spu94_interp_waypoints[seg]].regs;
    const int16_t *b = spu94_presets[spu94_interp_waypoints[seg + 1]].regs;

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

        /* INTERP-04: At exact waypoint positions (frac == 0.0 or 1.0),
         * write the preset value directly to avoid floating-point rounding
         * drift. This guarantees bit-identical output at waypoints. */
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
