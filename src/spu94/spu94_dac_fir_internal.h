/* src/spu94/spu94_dac_fir_internal.h -- Phase 6 Plan 01
 *
 * INTERNAL header for the DAC interpolation filter. Not installed, never
 * on the public include path. Declares dimension constants, coefficient
 * table externs, symmetric pair index tables, and the test-visible
 * per-stage apply wrapper.
 *
 * Pattern: follows src/spu94/spu94_fir_internal.h (Phase 4).
 */
#ifndef SPU94_DAC_FIR_INTERNAL_H
#define SPU94_DAC_FIR_INTERNAL_H

#include <spu94/spu94_dac_fir.h>  /* spu94_dac_fir_state */
#include <stdint.h>

/* -------------------------------------------------------------------- */
/* Stage dimension constants.                                            */
/* -------------------------------------------------------------------- */
#define DAC_FIR_STAGE1_NTAPS  55
#define DAC_FIR_STAGE2_NTAPS  11
#define DAC_FIR_STAGE3_NTAPS   7

/* Non-zero symmetric pairs (excluding center tap). Used for folded-form
 * optimization (D-01). 14 + 3 + 2 = 19 pair multiplies, plus 3 center
 * taps = 22 total multiplies across the cascade. */
#define DAC_FIR_STAGE1_NPAIRS 14
#define DAC_FIR_STAGE2_NPAIRS  3
#define DAC_FIR_STAGE3_NPAIRS  2

/* -------------------------------------------------------------------- */
/* Compile-time verification: delay line dimensions in the state struct  */
/* must match the coefficient table dimensions (Pitfall 3 defense).      */
/* -------------------------------------------------------------------- */
_Static_assert(sizeof(((spu94_dac_fir_state *)0)->stage1_delay) / sizeof(int16_t) == DAC_FIR_STAGE1_NTAPS,
    "stage1_delay dimension must match DAC_FIR_STAGE1_NTAPS");
_Static_assert(sizeof(((spu94_dac_fir_state *)0)->stage2_delay) / sizeof(int16_t) == DAC_FIR_STAGE2_NTAPS,
    "stage2_delay dimension must match DAC_FIR_STAGE2_NTAPS");
_Static_assert(sizeof(((spu94_dac_fir_state *)0)->stage3_delay) / sizeof(int16_t) == DAC_FIR_STAGE3_NTAPS,
    "stage3_delay dimension must match DAC_FIR_STAGE3_NTAPS");

/* -------------------------------------------------------------------- */
/* Coefficient tables (verbatim from tools/dac_filter_design.py          */
/* --export-c). Defined in spu94_dac_fir_coef.c.                        */
/* -------------------------------------------------------------------- */
extern const int16_t dac_interp_stage1[DAC_FIR_STAGE1_NTAPS];
extern const int16_t dac_interp_stage2[DAC_FIR_STAGE2_NTAPS];
extern const int16_t dac_interp_stage3[DAC_FIR_STAGE3_NTAPS];

/* -------------------------------------------------------------------- */
/* Symmetric pair index tables for folded-form FIR. Each row is          */
/* {left_index, right_index} where coef[left] == coef[right] and both    */
/* are non-zero. Defined in spu94_dac_fir_coef.c.                       */
/* -------------------------------------------------------------------- */
extern const unsigned dac_fir_stage1_pairs[DAC_FIR_STAGE1_NPAIRS][2];
extern const unsigned dac_fir_stage2_pairs[DAC_FIR_STAGE2_NPAIRS][2];
extern const unsigned dac_fir_stage3_pairs[DAC_FIR_STAGE3_NPAIRS][2];

/* -------------------------------------------------------------------- */
/* Test-visible per-stage apply wrapper (Option B from plan).            */
/* Takes a stage number (1, 2, or 3) and a delay line with               */
/* delay[idx] = oldest convention (circular buffer). Applies the         */
/* folded-form FIR for that stage and returns the sat_s16 result.        */
/* Used by test_dac_fir_overflow_proof.c to exercise each stage          */
/* independently. Never on the production hot path.                      */
/* -------------------------------------------------------------------- */
int16_t spu94_dac_fir_test_stage_apply(unsigned stage_num,
                                       const int16_t *delay, uint8_t idx);

#endif /* SPU94_DAC_FIR_INTERNAL_H */
