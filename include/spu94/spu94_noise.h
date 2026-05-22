/* include/spu94/spu94_noise.h -- Phase 36 Plan 01
 *
 * Public API for the PS1 SPU global noise generator. Produces pseudo-random
 * noise via a 16-bit Fibonacci-style LFSR with taps at bits 15, 12, 11, 10
 * (XNOR -- XOR with 1). The noise level is shared by all NON-enabled voices.
 *
 * This is SEPARATE from spu94_dac_noise.h (32-bit Galois LFSR for AK4309
 * delta-sigma quantization modeling). Different polynomial, different purpose,
 * different spectral character. SPU noise is flat-spectrum percussion/texture;
 * DAC noise is +12 dB/octave shaped quantization error.
 *
 * WARNING: spu94_noise_gen_init() MUST be called before first use.
 * memset(ng, 0, sizeof(*ng)) is NOT equivalent -- it sets the LFSR level
 * to zero, which is an absorbing state (output = silence forever).
 */
#ifndef SPU94_NOISE_H
#define SPU94_NOISE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* [CITED: nocash psxspx-spu-noise-generator.htm]
 * [CITED: DuckStation spu.cpp Reset() -- seed = 1] */
typedef struct {
    int16_t  level;       /* current noise output (-0x8000..+0x7FFF) */
    int32_t  timer;       /* countdown timer (MUST be signed for < 0 check) */
    uint8_t  shift;       /* 0..15 from SPUCNT[13:10] (NoiseShift) */
    uint8_t  step;        /* 4..7 from SPUCNT[9:8]+4 (NoiseStep) */
} spu94_noise_gen_t;

/* Initialize the noise generator to its reset state.
 * Sets level=1 (seed), timer=0, shift=0, step=4 (minimum).
 * MUST be called instead of memset (seed=0 is absorbing). */
void spu94_noise_gen_init(spu94_noise_gen_t *ng);

/* Advance the noise generator by one tick (44.1 kHz rate).
 * Decrements timer by step. On underflow, shifts LFSR left and inserts
 * the parity bit (computed from PRE-shift level). Reloads timer.
 * If still negative, reloads again (double-reload) without re-shifting.
 *
 * Called ONCE per mixer tick, before the voice loop (NON-08). */
void spu94_noise_gen_tick(spu94_noise_gen_t *ng);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_NOISE_H */
