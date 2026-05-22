/* src/spu94/spu94_adsr.c
 * Phase 28: PS1-faithful ADSR envelope generator implementation.
 *
 * Algorithm per tick (nocash psx-spx reference):
 *
 *   CounterIncrement = 0x8000 >> max(0, ShiftValue - 11)
 *   AdsrStep (increase) = +(7 - StepValue) << max(0, 11 - ShiftValue)
 *   AdsrStep (decrease) = -(8 - StepValue) << max(0, 11 - ShiftValue)
 *
 *   Attack (ADSR-03 fake exponential):
 *     If attack_exp AND level > 0x6000: CounterIncrement /= 4
 *     counter += CounterIncrement
 *     If bit 15 set: apply +AdsrStep; clear bit 15
 *     Clamp level to [0, 0x7FFF]; if >= 0x7FFF -> transition to DECAY
 *
 *   Decay (ADSR-04 real exponential):
 *     AdsrStep = -8 << max(0, 11 - decay_shift)
 *     Before applying: AdsrStep = AdsrStep * level / 0x8000
 *     When level <= sustain_target: transition to SUSTAIN
 *
 *   Sustain:
 *     Same mechanism as attack (increase) or decay (decrease).
 *     Runs indefinitely until key_off.
 *
 *   Release:
 *     AdsrStep = -(8 - 0) << max(0, 11 - release_shift)
 *     If release_exp: AdsrStep = AdsrStep * level / 0x8000
 *     When level <= 0: set level = 0, transition to ADSR_OFF
 *
 * RT-safety: no malloc, no locks, no syscalls, no fopen/printf.
 * Pitfall prevention:
 *   C3: counter-accumulate mechanism (not fixed-rate ramp)
 *   T-28-01: int32_t intermediates for all multiply-accumulate
 *   T-28-03: level clamped to [0, 0x7FFF] after every step
 *   M2: sustain target = (sustain_level + 1) * 0x800 (never zero)
 */

#include <spu94/spu94_adsr.h>
#include <stddef.h>

/* Helper: max(a, b) for int */
static inline int adsr_max(int a, int b) {
    return (a > b) ? a : b;
}

void spu94_adsr_init(spu94_adsr_state_t *a) {
    if (a == NULL) return;
    a->attack_shift = 0;
    a->attack_step = 0;
    a->attack_exp = 0;
    a->decay_shift = 0;
    a->sustain_level = 0;
    a->sustain_shift = 0;
    a->sustain_step = 0;
    a->sustain_exp = 0;
    a->sustain_dir = 0;
    a->release_shift = 0;
    a->release_exp = 0;
    a->phase = ADSR_OFF;
    a->level = 0;
    a->counter = 0;
    a->tick_count = 0;
    a->enabled = 0;
}

void spu94_adsr_key_on(spu94_adsr_state_t *a) {
    if (a == NULL) return;
    a->phase = ADSR_ATTACK;
    a->level = 0;
    a->counter = 0;
    a->tick_count = 0;
}

void spu94_adsr_key_off(spu94_adsr_state_t *a) {
    if (a == NULL) return;
    /* Transition to release from any phase */
    if (a->phase == ADSR_OFF) return;  /* already off, nothing to do */
    a->phase = ADSR_RELEASE;
    a->counter = 0;  /* reset counter for release timing */
}

int16_t spu94_adsr_tick(spu94_adsr_state_t *a) {
    if (a == NULL) return 0;

    /* Bypass mode: return unity without touching state */
    if (!a->enabled) return 0x7FFF;

    /* ADSR_OFF: voice is silent */
    if (a->phase == ADSR_OFF) return 0;

    int32_t level = (int32_t)a->level;
    int32_t step = 0;
    uint32_t counter_increment = 0;

    switch (a->phase) {
    case ADSR_ATTACK: {
        /* CounterIncrement = 0x8000 >> max(0, shift - 11) */
        int shift_amt = adsr_max(0, (int)a->attack_shift - 11);
        counter_increment = (uint32_t)0x8000 >> shift_amt;

        /* AdsrStep = +(7 - attack_step) << max(0, 11 - shift) */
        int step_shift = adsr_max(0, 11 - (int)a->attack_shift);
        step = (int32_t)(7 - a->attack_step) << step_shift;

        /* ADSR-03: fake exponential above 0x6000 */
        if (a->attack_exp && level > 0x6000) {
            counter_increment >>= 2;  /* /= 4 */
        }

        /* Accumulate counter */
        a->counter += counter_increment;

        /* Check bit 15: if set, fire step and clear bit 15 */
        if (a->counter & 0x8000u) {
            a->counter &= ~0x8000u;
            level += step;
        }

        /* Clamp to [0, 0x7FFF] */
        if (level >= 0x7FFF) {
            level = 0x7FFF;
            a->phase = ADSR_DECAY;
            a->counter = 0;
        }
        if (level < 0) level = 0;
        break;
    }

    case ADSR_DECAY: {
        /* Decay is always exponential (step proportional to level) */
        /* AdsrStep base = -8 << max(0, 11 - decay_shift) */
        int step_shift = adsr_max(0, 11 - (int)a->decay_shift);
        step = -((int32_t)8 << step_shift);

        /* CounterIncrement = 0x8000 >> max(0, decay_shift - 11) */
        int shift_amt = adsr_max(0, (int)a->decay_shift - 11);
        counter_increment = (uint32_t)0x8000 >> shift_amt;

        /* Accumulate counter */
        a->counter += counter_increment;

        /* Check bit 15 */
        if (a->counter & 0x8000u) {
            a->counter &= ~0x8000u;
            /* Real exponential: step = step * level / 0x8000 (T-28-01) */
            int32_t scaled_step = (step * level) / (int32_t)0x8000;
            /* Ensure at least -1 step when level > 0 to prevent stalling */
            if (scaled_step == 0 && level > 0) scaled_step = -1;
            level += scaled_step;
        }

        /* Sustain target: (sustain_level + 1) * 0x800 (M2: never zero) */
        int32_t sustain_target = ((int32_t)a->sustain_level + 1) * 0x800;
        if (level <= sustain_target) {
            level = sustain_target;
            /* Clamp to max level if target exceeds it */
            if (level > 0x7FFF) level = 0x7FFF;
            a->phase = ADSR_SUSTAIN;
            a->counter = 0;
        }

        /* Clamp */
        if (level < 0) level = 0;
        if (level > 0x7FFF) level = 0x7FFF;
        break;
    }

    case ADSR_SUSTAIN: {
        /* Sustain can increase or decrease, linear or exponential */
        int step_shift = adsr_max(0, 11 - (int)a->sustain_shift);
        int shift_amt = adsr_max(0, (int)a->sustain_shift - 11);
        counter_increment = (uint32_t)0x8000 >> shift_amt;

        if (a->sustain_dir == 0) {
            /* Increase */
            step = (int32_t)(7 - a->sustain_step) << step_shift;

            /* Fake exponential for increase above 0x6000 */
            if (a->sustain_exp && level > 0x6000) {
                counter_increment >>= 2;
            }
        } else {
            /* Decrease: nocash spec uses base 8 for decrease formulas */
            step = -((int32_t)(8 - a->sustain_step) << step_shift);

            /* Real exponential for decrease */
            if (a->sustain_exp) {
                /* Will be applied after counter check */
            }
        }

        /* Accumulate counter */
        a->counter += counter_increment;

        /* Check bit 15 */
        if (a->counter & 0x8000u) {
            a->counter &= ~0x8000u;
            if (a->sustain_dir == 1 && a->sustain_exp) {
                /* Exponential decrease: step = step * level / 0x8000 */
                int32_t scaled_step = (step * level) / (int32_t)0x8000;
                if (scaled_step == 0 && level > 0) scaled_step = -1;
                level += scaled_step;
            } else {
                level += step;
            }
        }

        /* Clamp: sustain runs indefinitely until key_off */
        if (level < 0) level = 0;
        if (level > 0x7FFF) level = 0x7FFF;
        break;
    }

    case ADSR_RELEASE: {
        /* Release: AdsrStep = -(8 - 0) << max(0, 11 - release_shift)
         * nocash: release step value is always 0, so (8-0) = 8 */
        int step_shift = adsr_max(0, 11 - (int)a->release_shift);
        step = -((int32_t)8 << step_shift);

        /* CounterIncrement */
        int shift_amt = adsr_max(0, (int)a->release_shift - 11);
        counter_increment = (uint32_t)0x8000 >> shift_amt;

        /* Accumulate counter */
        a->counter += counter_increment;

        /* Check bit 15 */
        if (a->counter & 0x8000u) {
            a->counter &= ~0x8000u;
            if (a->release_exp) {
                /* Exponential: step = step * level / 0x8000 */
                int32_t scaled_step = (step * level) / (int32_t)0x8000;
                if (scaled_step == 0 && level > 0) scaled_step = -1;
                level += scaled_step;
            } else {
                level += step;
            }
        }

        /* When level reaches 0: transition to OFF */
        if (level <= 0) {
            level = 0;
            a->phase = ADSR_OFF;
        }
        break;
    }

    case ADSR_OFF:
    default:
        return 0;
    }

    a->level = (int16_t)level;
    a->tick_count++;
    return (int16_t)level;
}
