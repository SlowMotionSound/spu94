/* src/spu94/spu94_sweep.c -- Phase 37 Plan 02
 *
 * Per-voice volume sweep implementation. Uses spu94_envelope_step for the
 * counter-accumulate math, then applies quadrant-specific clamping.
 *
 * Clamping boundaries (ADR-0059):
 *   phase=0, increase: clamp at +0x7FFF
 *   phase=0, decrease: clamp at 0x0000
 *   phase=1, increase: clamp at -0x7FFF (deeper negative)
 *   phase=1, decrease: clamp at 0x0000 (toward zero)
 *   Exception: phase bit ignored when direction=decrease AND mode=exponential
 *
 * RT-safety: no malloc, no locks, no syscalls, no printf.
 */

#include <spu94/spu94_sweep.h>
#include <spu94/spu94_envelope_step.h>
#include <stddef.h>
#include <string.h>

void spu94_sweep_init(spu94_sweep_t *sw) {
    if (sw == NULL) return;
    memset(sw, 0, sizeof(*sw));
}

void spu94_sweep_tick(spu94_sweep_t *sw) {
    if (sw == NULL || !sw->active) return;

    spu94_envelope_state_t env;
    env.level = (int32_t)sw->level;
    env.counter = sw->counter;

    /* ADR-0059: phase bit is ignored when direction=decrease AND mode=exponential */
    uint8_t effective_phase = sw->phase;
    if (sw->direction && sw->mode) {
        effective_phase = 0;
    }

    spu94_envelope_step(&env, sw->shift, sw->step,
                        sw->direction, sw->mode, effective_phase);

    int32_t level = env.level;

    /* Quadrant clamping */
    if (effective_phase == 0) {
        /* Positive phase */
        if (sw->direction == 0) {
            /* Increase: clamp at +0x7FFF */
            if (level > 0x7FFF) level = 0x7FFF;
        } else {
            /* Decrease: clamp at 0 */
            if (level < 0) level = 0;
        }
    } else {
        /* Negative phase */
        if (sw->direction == 0) {
            /* Increase (toward -0x7FFF): clamp at -0x7FFF */
            if (level < -0x7FFF) level = -0x7FFF;
        } else {
            /* Decrease (toward 0): clamp at 0 */
            if (level > 0) level = 0;
        }
    }

    sw->level = (int16_t)level;
    sw->counter = env.counter;

    /* Retrigger: auto-reverse direction when level hits clamping boundary.
     * Only fires when retrigger_enable=1. Preserves v1.9 one-shot behavior
     * when retrigger_enable=0. (RTR-01, RTR-03, RTR-04) */
    if (sw->retrigger_enable) {
        int16_t boundary;
        if (effective_phase == 0) {
            boundary = (sw->direction == 0) ? 0x7FFF : 0;
        } else {
            boundary = (sw->direction == 0) ? -0x7FFF : 0;
        }
        if (sw->level == boundary) {
            sw->direction ^= 1;   /* flip direction */
            sw->counter = 0;      /* clean start for new half-cycle */
        }
    }
}

void spu94_sweep_configure(spu94_sweep_t *sw,
                           uint8_t mode, uint8_t direction, uint8_t phase,
                           uint8_t shift, uint8_t step,
                           uint8_t retrigger_enable) {
    if (sw == NULL) return;
    sw->mode = mode;
    sw->direction = direction;
    sw->phase = phase;
    sw->shift = shift;
    sw->step = step;
    sw->retrigger_enable = retrigger_enable;
    sw->counter = 0;
    sw->active = 1;
}
