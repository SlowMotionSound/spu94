/* include/spu94/spu94_adsr.h
 * Phase 28: PS1-faithful ADSR envelope generator per voice.
 *
 * The PS1 SPU ADSR does NOT step the envelope every tick. It uses a counter
 * that accumulates a CounterIncrement each tick; the actual envelope step
 * fires only when bit 15 of the counter becomes set. This produces the
 * characteristic quantized timing of the PS1 envelope.
 *
 * Attack: linear (or fake-exponential above 0x6000 — step rate /= 4).
 * Decay: always exponential (step proportional to current level).
 * Sustain: configurable linear/exponential, increase/decrease.
 * Release: exponential decrease toward 0; voice silences when level reaches 0.
 *
 * RT-safety: no malloc, no locks, no syscalls, no printf.
 * All arithmetic uses int32_t intermediates to prevent overflow (T-28-01).
 * Level is clamped to [0, 0x7FFF] after every step (T-28-03).
 *
 * Reference: nocash psx-spx "SPU Volume and ADSR Generator"
 * Pitfall prevention: C3 (counter mechanism), M2 (sustain level 0 != zero)
 */
#ifndef SPU94_ADSR_H
#define SPU94_ADSR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADSR_OFF = 0,      /* voice is silent; skip further ticking */
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
} spu94_adsr_phase_t;

/* Per-voice ADSR envelope state.
 *
 * Register fields are loaded by the caller before spu94_adsr_key_on().
 * Runtime state (phase, level, counter) is managed by the ADSR engine.
 *
 * Field ranges:
 *   attack_shift:   0..31  (nocash "Shift" field)
 *   attack_step:    0..3   (nocash "Step" field; maps to +7,+6,+5,+4)
 *   attack_exp:     0 or 1 (1 = fake exponential mode above 0x6000)
 *   decay_shift:    0..15
 *   sustain_level:  0..15  (target = (val+1)*0x800; M2: val=0 -> 0x800)
 *   sustain_shift:  0..31
 *   sustain_step:   0..3
 *   sustain_exp:    0 or 1
 *   sustain_dir:    0 = increase, 1 = decrease
 *   release_shift:  0..31
 *   release_exp:    0 or 1 (1 = exponential, 0 = linear)
 */
typedef struct {
    /* Register fields -- loaded from voice register values before key_on */
    uint8_t  attack_shift;        /* 0..31 */
    uint8_t  attack_step;         /* 0..3 (maps to +7, +6, +5, +4) */
    uint8_t  attack_exp;          /* 1 = fake exponential above 0x6000 */
    uint8_t  decay_shift;         /* 0..15 */
    uint8_t  decay_step;          /* 0..3 (maps to -8, -7, -6, -5) */
    uint8_t  sustain_level;       /* 0..15; target = (val+1)*0x800 */
    uint8_t  sustain_shift;       /* 0..31 */
    uint8_t  sustain_step;        /* 0..3 */
    uint8_t  sustain_exp;         /* 1 = exponential */
    uint8_t  sustain_dir;         /* 0 = increase, 1 = decrease */
    uint8_t  release_shift;       /* 0..31 */
    uint8_t  release_step;        /* 0..3 (maps to -8, -7, -6, -5) */
    uint8_t  release_exp;         /* 1 = exponential */

    /* Runtime state */
    spu94_adsr_phase_t phase;
    int16_t  level;               /* current envelope level 0..0x7FFF */
    uint32_t counter;             /* accumulator; step fires when bit 15 set */

    uint32_t tick_count;          /* ticks since key_on; for live display tracking */

    /* Control */
    uint8_t  enabled;             /* 0 = bypass (level always 0x7FFF) */
} spu94_adsr_state_t;

/* Initialize ADSR state to defaults (phase=OFF, level=0, enabled=0).
 * Caller should set register fields and enabled=1 before key_on. */
void spu94_adsr_init(spu94_adsr_state_t *a);

/* Reset ADSR to attack phase: level=0, counter=0, phase=ADSR_ATTACK.
 * Register fields must already be configured by the caller. */
void spu94_adsr_key_on(spu94_adsr_state_t *a);

/* Transition to release phase from any current phase.
 * Counter resets to 0; level begins decaying toward 0. */
void spu94_adsr_key_off(spu94_adsr_state_t *a);

/* Advance the ADSR envelope by one 44.1 kHz tick.
 * Returns the current envelope level (0..0x7FFF).
 *
 * When enabled=0 (bypass mode): returns 0x7FFF immediately without
 * modifying any state.
 *
 * When phase=ADSR_OFF: returns 0 without modifying state. */
int16_t spu94_adsr_tick(spu94_adsr_state_t *a);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_ADSR_H */
