/* include/spu94/spu94_envelope_step.h -- Phase 37 Plan 01
 *
 * Shared counter-accumulate envelope stepping math used by both ADSR and
 * volume sweep. The PS1 SPU uses the identical mechanism for both: a counter
 * accumulates an increment each tick; when bit 15 becomes set, the actual
 * envelope step fires.
 *
 * [CITED: nocash psxspx "SPU Volume and ADSR Generator"]
 * Increase base: (7 - step) << max(0, 11 - shift)
 * Decrease base: -(8 - step) << max(0, 11 - shift) [ADR-0056]
 *
 * RT-safety: no malloc, no locks, no syscalls, no printf.
 */
#ifndef SPU94_ENVELOPE_STEP_H
#define SPU94_ENVELOPE_STEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal envelope state passed to the shared helper.
 * Callers (ADSR, sweep) construct this from their own structs.
 * Level is int32_t to hold unclamped intermediates — callers handle clamping. */
typedef struct {
    int32_t  level;     /* current envelope level (unclamped; caller clamps) */
    uint32_t counter;   /* accumulator; step fires when bit 15 set */
} spu94_envelope_state_t;

/* Advance the counter-accumulate envelope by one tick.
 *
 * Parameters:
 *   state          - level + counter (modified in place)
 *   shift          - 0..31, controls counter increment rate
 *   step_index     - 0..3, controls step magnitude
 *   decrease       - 0 = increase, 1 = decrease
 *   exponential    - 0 = linear, 1 = exponential
 *   phase_negative - 0 = positive phase, 1 = negative (inverts step sign;
 *                    used by sweep only; ADSR always passes 0)
 *
 * Returns 1 if a step was applied this tick, 0 if counter has not yet
 * triggered. The helper does NOT clamp -- callers handle their own
 * boundary logic (ADSR clamps to [0, 0x7FFF]; sweep clamps per quadrant).
 *
 * [CITED: nocash psxspx "SPU Volume and ADSR Generator"]
 * [CITED: DuckStation spu.cpp VolumeEnvelope::Tick] */
int spu94_envelope_step(spu94_envelope_state_t *state,
                        uint8_t shift,
                        uint8_t step_index,
                        uint8_t decrease,
                        uint8_t exponential,
                        uint8_t phase_negative);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_ENVELOPE_STEP_H */
