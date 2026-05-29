/* src/spu94/spu94_adsr.c
 * Phase 28: PS1-faithful ADSR envelope generator implementation.
 * Phase 37: refactored to use shared spu94_envelope_step() helper.
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
 *     AdsrStep base = -8 << max(0, 11 - decay_shift)
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
#include <spu94/spu94_envelope_step.h>
#include <stddef.h>

void spu94_adsr_init(spu94_adsr_state_t *a) {
    if (a == NULL) return;
    a->attack_shift = 0;
    a->attack_step = 0;
    a->attack_exp = 0;
    a->decay_shift = 0;
    a->decay_step = 0;
    a->sustain_level = 0;
    a->sustain_shift = 0;
    a->sustain_step = 0;
    a->sustain_exp = 0;
    a->sustain_dir = 0;
    a->release_shift = 0;
    a->release_step = 0;
    a->release_exp = 0;
    a->phase = ADSR_OFF;
    a->level = 0;
    a->counter = 0;
    a->tick_count = 0;
    a->enabled = 0;
    a->one_shot = 0;
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

    spu94_envelope_state_t env;
    env.level = (int32_t)a->level;
    env.counter = a->counter;

    int32_t level;

    switch (a->phase) {
    case ADSR_ATTACK: {
        spu94_envelope_step(&env, a->attack_shift, a->attack_step,
                            0 /* increase */, a->attack_exp,
                            0 /* phase_negative */);
        level = (int32_t)env.level;

        /* Clamp to [0, 0x7FFF] */
        if (level >= 0x7FFF) {
            level = 0x7FFF;
            a->phase = ADSR_DECAY;
            env.counter = 0;
        }
        if (level < 0) level = 0;
        break;
    }

    case ADSR_DECAY: {
        spu94_envelope_step(&env, a->decay_shift, a->decay_step,
                            1 /* decrease */, 1 /* exponential */,
                            0 /* phase_negative */);
        level = (int32_t)env.level;

        int32_t sustain_target = (a->sustain_level == 0) ? 0
            : ((int32_t)a->sustain_level + 1) * 0x800;
        if (level <= sustain_target) {
            level = sustain_target;
            if (level > 0x7FFF) level = 0x7FFF;
            if (sustain_target == 0) {
                a->phase = ADSR_OFF;
            } else if (a->one_shot) {
                a->phase = ADSR_RELEASE;
            } else {
                a->phase = ADSR_SUSTAIN;
            }
            env.counter = 0;
        }

        /* Clamp */
        if (level < 0) level = 0;
        if (level > 0x7FFF) level = 0x7FFF;
        break;
    }

    case ADSR_SUSTAIN: {
        spu94_envelope_step(&env, a->sustain_shift, a->sustain_step,
                            a->sustain_dir, a->sustain_exp,
                            0 /* phase_negative */);
        level = (int32_t)env.level;

        /* Clamp: sustain runs indefinitely until key_off */
        if (level < 0) level = 0;
        if (level > 0x7FFF) level = 0x7FFF;
        break;
    }

    case ADSR_RELEASE: {
        spu94_envelope_step(&env, a->release_shift, a->release_step,
                            1 /* decrease */, a->release_exp,
                            0 /* phase_negative */);
        level = (int32_t)env.level;

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
    a->counter = env.counter;
    a->tick_count++;
    return (int16_t)level;
}
