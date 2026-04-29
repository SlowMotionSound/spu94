/* src/spu94/spu94_dac_fir_coef.c -- Phase 6 Plan 01
 *
 * Coefficient tables for the AK4309 interpolation filter model.
 * Three cascaded half-band FIR stages at 44.1 kHz.
 *
 * Source: python3 tools/dac_filter_design.py --export-c
 * The Q15 hex values below are VERBATIM from the --export-c output (D-03).
 * Do NOT edit manually -- regenerate from the design script if changes
 * are needed.
 *
 * Accumulator width proofs for each stage are documented in
 * src/spu94/spu94_dac_fir.c (D-02). Empirical validation in
 * tests/unit/dac_fir/test_dac_fir_overflow_proof.c.
 *
 * DC gain per stage (Pitfall 6 -- NOT compensated):
 *   Stage 1: sum = 0x7F8E (DC gain -0.030 dB)
 *   Stage 2: sum = 0x801A (DC gain +0.007 dB, wraps to -32742 signed)
 *   Stage 3: sum = 0x7FF4 (DC gain -0.003 dB)
 *   Cascade: approximately -0.027 dB. Matches project convention from
 *            spu94_fir.c (bit-faithful, no compensation).
 */
#include "spu94_dac_fir_internal.h"

/* 55-tap half-band FIR, Q15 (29 non-zero) */
const int16_t dac_interp_stage1[DAC_FIR_STAGE1_NTAPS] = {
    0xFFA4, 0x0000, 0x0056, 0x0000, 0xFF82, 0x0000, 0x00B0, 0x0000,
    0xFF10, 0x0000, 0x0141, 0x0000, 0xFE59, 0x0000, 0x022C, 0x0000,
    0xFD25, 0x0000, 0x03D0, 0x0000, 0xFAC0, 0x0000, 0x07BB, 0x0000,
    0xF2AC, 0x0000, 0x28A9, 0x4000, 0x28A9, 0x0000, 0xF2AC, 0x0000,
    0x07BB, 0x0000, 0xFAC0, 0x0000, 0x03D0, 0x0000, 0xFD25, 0x0000,
    0x022C, 0x0000, 0xFE59, 0x0000, 0x0141, 0x0000, 0xFF10, 0x0000,
    0x00B0, 0x0000, 0xFF82, 0x0000, 0x0056, 0x0000, 0xFFA4,
};

/* 11-tap half-band FIR, Q15 (7 non-zero) */
const int16_t dac_interp_stage2[DAC_FIR_STAGE2_NTAPS] = {
    0x0171, 0x0000, 0xF832, 0x0000, 0x266B, 0x3FFE, 0x266B, 0x0000,
    0xF832, 0x0000, 0x0171,
};

/* 7-tap half-band FIR, Q15 (5 non-zero) */
const int16_t dac_interp_stage3[DAC_FIR_STAGE3_NTAPS] = {
    0xFB99, 0x0000, 0x2461, 0x4000, 0x2461, 0x0000, 0xFB99,
};

/* -------------------------------------------------------------------- */
/* Symmetric pair index tables for folded-form FIR (D-01).               */
/*                                                                       */
/* Each row is {left_index, right_index} where:                          */
/* - coef[left] == coef[right] (half-band symmetry)                      */
/* - Both entries are non-zero (zero-skip)                               */
/* - left < right                                                        */
/*                                                                       */
/* Stage 1: 14 pairs. Non-zero even-indexed coefficients (excluding      */
/* center at index 27) are at indices 0,2,4,...,26 with symmetric        */
/* partners at 54,52,50,...,28.                                          */
/* Stage 2: 3 pairs at (0,10), (2,8), (4,6).                            */
/* Stage 3: 2 pairs at (0,6), (2,4).                                    */
/* -------------------------------------------------------------------- */

const unsigned dac_fir_stage1_pairs[DAC_FIR_STAGE1_NPAIRS][2] = {
    { 0, 54}, { 2, 52}, { 4, 50}, { 6, 48},
    { 8, 46}, {10, 44}, {12, 42}, {14, 40},
    {16, 38}, {18, 36}, {20, 34}, {22, 32},
    {24, 30}, {26, 28},
};

const unsigned dac_fir_stage2_pairs[DAC_FIR_STAGE2_NPAIRS][2] = {
    {0, 10}, {2, 8}, {4, 6},
};

const unsigned dac_fir_stage3_pairs[DAC_FIR_STAGE3_NPAIRS][2] = {
    {0, 6}, {2, 4},
};
