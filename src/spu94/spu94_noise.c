/* src/spu94/spu94_noise.c -- Phase 36 Plan 01
 *
 * PS1 SPU global noise generator implementation.
 * 16-bit Fibonacci-style LFSR with taps at bits 15, 12, 11, 10 (XNOR).
 * Timer-driven frequency control via SPUCNT shift/step fields.
 *
 * [CITED: nocash psxspx-spu-noise-generator.htm]
 */

#include <spu94/spu94_noise.h>
#include <string.h>

void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;
    memset(ng, 0, sizeof(*ng));
    ng->level = 1;    /* seed = 1 (NOT 0 -- zero is absorbing) */
    ng->step  = 4;    /* minimum step (SPUCNT[9:8] = 0 -> step = 0+4 = 4) */
}

void spu94_noise_gen_tick(spu94_noise_gen_t *ng) {
    if (ng == NULL) return;

    /* [CITED: nocash psxspx-spu-noise-generator.htm]
     * Line 2: Timer = Timer - NoiseStep */
    ng->timer -= (int32_t)ng->step;

    /* Line 3: ParityBit = Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
     * Computed from CURRENT level, BEFORE any shift (Pitfall 3 prevention) */
    uint16_t lvl = (uint16_t)ng->level;
    int parity = ((lvl >> 15) ^ (lvl >> 12) ^ (lvl >> 11) ^ (lvl >> 10) ^ 1) & 1;

    /* Line 4: IF Timer<0 THEN NoiseLevel = NoiseLevel*2 + ParityBit */
    if (ng->timer < 0) {
        ng->level = (int16_t)((uint16_t)(lvl << 1) | (uint16_t)parity);
    }

    /* Line 5: IF Timer<0 THEN Timer = Timer + (20000h SHR NoiseShift) */
    if (ng->timer < 0) {
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }

    /* Line 6: IF Timer<0 THEN Timer = Timer + (20000h SHR NoiseShift)
     * Double-reload: does NOT re-shift NoiseLevel, only reloads timer.
     * (Pitfall 2 prevention: only ONE LFSR shift per tick maximum) */
    if (ng->timer < 0) {
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }
}
