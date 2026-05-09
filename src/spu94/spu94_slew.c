/* src/spu94/spu94_slew.c -- Phase 18: per-sample fractional register slewing.
 *
 * All 35 registers slew proportionally via fractional positions.
 * Address registers (u16) use linear-interpolated buffer reads in the
 * reverb body. Gain registers (i16) slew alongside to avoid coefficient
 * snapping artifacts. All registers converge simultaneously (Bresenham
 * proportional distribution) so L/R pairs stay in lockstep.
 *
 * spu94_slew_tick is called once per sample inside spu94_process.
 */
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>

void spu94_set_slew_targets(spu94_state *state,
                            const int16_t targets[SPU94_REG__COUNT]) {
    if (!state || !targets) return;

    state->slew_max_delta = 0;

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        state->slew_target[r] = targets[r];

        int32_t delta;
        float cur_f;
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            /* Preserve fractional position if already slewing */
            cur_f = (state->slew_active && state->slew_abs_delta[r] > 0)
                  ? state->slew_frac[r] : (float)state->reg_values[r];
            delta = (int32_t)targets[r] - (int32_t)(int16_t)(cur_f + (cur_f >= 0 ? 0.5f : -0.5f));
        } else {
            cur_f = (state->slew_active && state->slew_abs_delta[r] > 0)
                  ? state->slew_frac[r] : (float)(uint16_t)state->reg_values[r];
            delta = (int32_t)(uint16_t)targets[r]
                  - (int32_t)(uint16_t)(int32_t)cur_f;
        }

        int32_t abs_d = (delta >= 0) ? delta : -delta;
        state->slew_abs_delta[r] = abs_d;
        state->slew_frac[r] = cur_f;

        if (abs_d > state->slew_max_delta)
            state->slew_max_delta = abs_d;
    }

    /* Compute per-tick fractional increments (proportional to each delta). */
    if (state->slew_max_delta > 0) {
        float inv_max = 1.0f / (float)state->slew_max_delta;
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            float tgt_f;
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
                tgt_f = (float)state->slew_target[r];
            else
                tgt_f = (float)(uint16_t)state->slew_target[r];
            float delta_f = tgt_f - state->slew_frac[r];
            state->slew_inc[r] = delta_f * inv_max;
        }
    }

    state->slew_samples_remaining = state->slew_max_delta;
    state->slew_active = (state->slew_max_delta > 0) ? 1 : 0;
}

int spu94_is_slewing(const spu94_state *state) {
    return (state && state->slew_active) ? 1 : 0;
}

void spu94_slew_cancel(spu94_state *state) {
    if (!state) return;
    state->slew_active = 0;
    state->slew_max_delta = 0;
    /* Snap fractional positions to current integer values */
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_U16)
            state->slew_frac[r] = (float)(uint16_t)state->reg_values[r];
    }
}

void spu94_slew_tick(spu94_state *state) {
    if (!state->slew_active) return;

    if (state->slew_samples_remaining <= 0) {
        /* Converged: snap all registers to exact integer targets */
        for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
            if (state->slew_abs_delta[r] == 0) continue;
            state->reg_values[r] = state->slew_target[r];
            state->pending_values[r] = state->slew_target[r];
            state->pending_mask &= ~(UINT64_C(1) << r);
            if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
                state->slew_frac[r] = (float)state->slew_target[r];
            else
                state->slew_frac[r] = (float)(uint16_t)state->slew_target[r];
        }
        state->slew_active = 0;
        return;
    }

    /* Advance fractional positions and sync integer reg_values. */
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (state->slew_abs_delta[r] == 0) continue;
        state->slew_frac[r] += state->slew_inc[r];

        int16_t int_val;
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            int_val = (int16_t)(int32_t)(state->slew_frac[r] + (state->slew_frac[r] >= 0 ? 0.5f : -0.5f));
        } else {
            int_val = (int16_t)(uint16_t)(int32_t)state->slew_frac[r];
        }
        state->reg_values[r] = int_val;
        state->pending_values[r] = int_val;
        state->pending_mask &= ~(UINT64_C(1) << r);
    }

    state->slew_samples_remaining--;
}
