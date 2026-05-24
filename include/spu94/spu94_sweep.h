/* include/spu94/spu94_sweep.h -- Phase 37 Plan 02
 *
 * Per-voice volume sweep state machine. Each PS1 SPU voice has independent
 * L and R sweep envelopes that automatically ramp vol_l/vol_r using the same
 * counter-accumulate mechanism as ADSR (via spu94_envelope_step).
 *
 * Sweep modifies the volume register directly -- the volume register IS the
 * sweep's working state. There is no separate multiplier.
 *
 * [CITED: nocash psxspx "SPU Volume and ADSR Generator" -- sweep register]
 * [CITED: DuckStation spu.cpp VolumeSweep::Tick]
 *
 * RT-safety: no malloc, no locks, no syscalls, no printf.
 */
#ifndef SPU94_SWEEP_H
#define SPU94_SWEEP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* [CITED: nocash psxspx -- volume sweep register bits]
 * [CITED: DuckStation spu.cpp VolumeEnvelope struct] */
typedef struct {
    int16_t  level;       /* current sweep level (IS the volume) */
    uint32_t counter;     /* counter-accumulate state */
    uint8_t  shift;       /* 0..31: controls counter increment rate */
    uint8_t  step;        /* 0..3: controls step magnitude */
    uint8_t  mode;        /* 0 = linear, 1 = exponential */
    uint8_t  direction;   /* 0 = increase, 1 = decrease */
    uint8_t  phase;       /* 0 = positive, 1 = negative (inverts step sign) */
    uint8_t  active;      /* 0 = inactive, 1 = sweeping */
    uint8_t  retrigger_enable; /* 0 = one-shot (v1.9), 1 = auto-reverse at limits */
    uint8_t  start_direction;  /* initial direction stored at configure time for KON reset */
} spu94_sweep_t;

/* Initialize sweep to defaults: active=0, level=0, counter=0. */
void spu94_sweep_init(spu94_sweep_t *sw);

/* Advance sweep by one 44.1 kHz tick.
 * If !active, returns immediately.
 * Uses spu94_envelope_step for counter-accumulate math.
 * Handles quadrant clamping and the phase-bit-ignored-in-exp-decrease
 * exception (ADR-0059). */
void spu94_sweep_tick(spu94_sweep_t *sw);

/* Configure sweep parameters from register bits.
 * Sets mode/direction/phase/shift/step, sets active=1.
 * Level is NOT modified -- it should already be set to the current volume
 * by the caller (because the volume register IS the sweep working state). */
void spu94_sweep_configure(spu94_sweep_t *sw,
                           uint8_t mode, uint8_t direction, uint8_t phase,
                           uint8_t shift, uint8_t step,
                           uint8_t retrigger_enable);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_SWEEP_H */
