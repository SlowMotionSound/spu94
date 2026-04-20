/* src/spu94/spu94_fir_coef.c — Phase 4 Plan 01
 *
 * 39-tap half-band FIR coefficients for the PS1 SPU reverb sample-rate
 * converter (44.1 <-> 22.05 kHz). Shared between decimator and interpolator
 * (D-08). Symmetric about index 19. Center tap 0x4000 (Q15 0.5). Odd-
 * offset-from-center positions are zero (half-band Type I structure).
 *
 * Sum of coefficients = 0x7FFE (DC gain ~= 0.999939 in Q15).
 * Sum of |coefficients| = 0xB9A6 = 47526 (worst-case accumulator L1 norm).
 * Non-zero coefficient magnitudes (distinct): 1, 2, 10, 35, 103, 266,
 *   616, 1332, 2960, 10246, 16384 — eleven distinct values including
 *   the center tap.
 *
 * Provenance: see docs/BIBLIOGRAPHY.md entries BIB-005 / BIB-006 / BIB-007.
 * Bit-integrity pinned by tests/unit/fir/test_fir_coef_table.c via SHA-256.
 *
 * D-11 + D-12 discipline: integer values only; no prose, no source
 * citations inline. All context is in the bibliography.
 */
#include <stdint.h>
#include "spu94_fir_internal.h"

const int16_t spu94_fir_coef[39] = {
    -0x0001,  /*  0 */
     0x0000,  /*  1 */
     0x0002,  /*  2 */
     0x0000,  /*  3 */
    -0x000A,  /*  4 */
     0x0000,  /*  5 */
     0x0023,  /*  6 */
     0x0000,  /*  7 */
    -0x0067,  /*  8 */
     0x0000,  /*  9 */
     0x010A,  /* 10 */
     0x0000,  /* 11 */
    -0x0268,  /* 12 */
     0x0000,  /* 13 */
     0x0534,  /* 14 */
     0x0000,  /* 15 */
    -0x0B90,  /* 16 */
     0x0000,  /* 17 */
     0x2806,  /* 18 */
     0x4000,  /* 19  -- center tap, Q15 0.5 */
     0x2806,  /* 20 */
     0x0000,  /* 21 */
    -0x0B90,  /* 22 */
     0x0000,  /* 23 */
     0x0534,  /* 24 */
     0x0000,  /* 25 */
    -0x0268,  /* 26 */
     0x0000,  /* 27 */
     0x010A,  /* 28 */
     0x0000,  /* 29 */
    -0x0067,  /* 30 */
     0x0000,  /* 31 */
     0x0023,  /* 32 */
     0x0000,  /* 33 */
    -0x000A,  /* 34 */
     0x0000,  /* 35 */
     0x0002,  /* 36 */
     0x0000,  /* 37 */
    -0x0001,  /* 38 */
};

_Static_assert(sizeof(spu94_fir_coef) / sizeof(spu94_fir_coef[0]) == 39,
               "FIR coefficient table must be exactly 39 entries");
