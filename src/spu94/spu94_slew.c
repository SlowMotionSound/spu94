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
        /* Cubic ease-out interpolates against the start position on every
         * tick (slew_frac[r] = start + s(t) * (target - start)), so we
         * snapshot the start value at slew-arm time. */
        state->slew_start_frac[r] = cur_f;

        if (abs_d > state->slew_max_delta)
            state->slew_max_delta = abs_d;
    }

    state->slew_samples_remaining = state->slew_max_delta;
    state->slew_total_samples     = state->slew_max_delta;
    state->slew_active = (state->slew_max_delta > 0) ? 1 : 0;
}

int spu94_is_slewing(const spu94_state *state) {
    return (state && state->slew_active) ? 1 : 0;
}

/* Override the in-flight slew duration set by spu94_set_slew_targets.
 * Lets the caller dial slew rate independently of the morph magnitude.
 * Cubic ease-out reads slew_total_samples each tick to compute t, so this
 * just updates the budget. No-op if no slew is active. samples < 1
 * clamped to 1. */
void spu94_set_slew_duration(spu94_state *state, int32_t samples) {
    if (!state) return;
    if (!state->slew_active) return;
    if (samples < 1) samples = 1;

    state->slew_samples_remaining = samples;
    state->slew_total_samples     = samples;
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

    if (state->slew_samples_remaining <= 0 || state->slew_total_samples <= 0) {
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

    /* Cubic ease-out: t = elapsed/total, s(t) = 1 - (1-t)^3.
     * Decisive take-off, graceful settle. All registers use the same
     * curve. Empirical note: at slew durations up to ~500 ms, curve shape
     * is below the audible threshold — non-linear variants (smoothstep,
     * quartic, quintic, true exponential) and per-register schedules
     * (e.g. vIIR three-phase hold) were each verified to produce their
     * designed register trajectories but were A/B-indistinguishable from
     * each other in audio. The audible bottleneck is downstream (reverb
     * network integration time), so cubic stays as the simplest non-
     * linear option. */
    const int32_t elapsed = state->slew_total_samples - state->slew_samples_remaining + 1;
    float t = (float)elapsed / (float)state->slew_total_samples;
    if (t > 1.0f) t = 1.0f;
    const float oneMinusT = 1.0f - t;
    const float s = 1.0f - oneMinusT * oneMinusT * oneMinusT;

    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        if (state->slew_abs_delta[r] == 0) continue;

        float tgt_f;
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16)
            tgt_f = (float)state->slew_target[r];
        else
            tgt_f = (float)(uint16_t)state->slew_target[r];

        const float start_f = state->slew_start_frac[r];
        state->slew_frac[r] = start_f + s * (tgt_f - start_f);

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

/* Morph Grit (see include/spu94/spu94.h for full docs). Cheap runtime
 * flag — read by reverb body on every tap access. */
void spu94_set_morph_grit(spu94_state *state, int grit) {
    if (state == NULL) return;
    /* Anything other than FRACT clamps to INT (the hardware-faithful default). */
    state->morph_grit = (grit == SPU94_GRIT_FRACT)
        ? (uint8_t)SPU94_GRIT_FRACT
        : (uint8_t)SPU94_GRIT_INT;
}

int spu94_get_morph_grit(const spu94_state *state) {
    if (state == NULL) return SPU94_GRIT_INT;
    return (int)state->morph_grit;
}
