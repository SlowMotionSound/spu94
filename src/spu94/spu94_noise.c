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
    /* STUB: implementation in Task 2 (GREEN phase) */
    (void)ng;
}
