/* src/spu94/spu94_envelope_step.c -- Phase 37 Plan 01
 *
 * Shared counter-accumulate envelope stepping math extracted from spu94_adsr.c.
 * Used by both ADSR envelopes and volume sweep to guarantee bit-identical
 * timing behavior between the two systems.
 *
 * RT-safety: no malloc, no locks, no syscalls, no printf.
 */

#include <spu94/spu94_envelope_step.h>
#include <stddef.h>

static inline int env_max(int a, int b) {
    return (a > b) ? a : b;
}

int spu94_envelope_step(spu94_envelope_state_t *state,
                        uint8_t shift,
                        uint8_t step_index,
                        uint8_t decrease,
                        uint8_t exponential,
                        uint8_t phase_negative) {
    if (state == NULL) return 0;

    int32_t level = state->level;

    /* CounterIncrement = 0x8000 >> max(0, shift - 11) */
    int shift_amt = env_max(0, (int)shift - 11);
    uint32_t counter_increment = (uint32_t)0x8000 >> shift_amt;

    /* Base step magnitude */
    int step_shift = env_max(0, 11 - (int)shift);
    int32_t step;
    if (decrease) {
        step = -((int32_t)(8 - step_index) << step_shift);
    } else {
        step = (int32_t)(7 - step_index) << step_shift;
    }

    /* Fake exponential increase: slow down above 0x6000 */
    if (exponential && !decrease && level > 0x6000) {
        counter_increment >>= 2;
    }

    /* Accumulate counter */
    state->counter += counter_increment;

    /* Check bit 15: if set, fire step and clear bit 15 */
    if (!(state->counter & 0x8000u)) {
        return 0;
    }

    state->counter &= ~0x8000u;

    /* Exponential decrease: scale step by current level */
    if (exponential && decrease) {
        step = (step * level) / (int32_t)0x8000;
        /* Anti-stall guard: ensure at least -1 step when level > 0 */
        if (step == 0 && level > 0) step = -1;
    }

    /* Negative phase: invert step sign (sweep only; ADSR passes 0) */
    if (phase_negative) {
        step = -step;
    }

    level += step;
    state->level = level;
    return 1;
}
