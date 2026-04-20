/* src/spu94/spu94_fir_internal.h — Phase 4 Plan 01
 *
 * INTERNAL header for libspu94 — not installed, never on the public
 * include path. Declares the sample-rate-conversion FIR stage functions
 * (D-01, D-08), the internal 44.1 kHz chain wrapper (D-07), the
 * test-only reverb-bypass wrapper, and the extern declaration for the
 * 39-tap coefficient table (D-11) defined in spu94_fir_coef.c.
 *
 * Call order (Plan 03 composes):
 *   spu94_fir_chain_step:
 *       spu94_fir_decimate (L+R push, retained phase produces 22.05 kHz pair)
 *    -> spu94_tick (when retained phase fires; existing Phase-2 body)
 *    -> spu94_fir_interpolate (produces TWO 44.1 kHz samples per tick)
 *       emits phase 0 on tick-aligned call, phase 1 on between-tick call.
 *
 * Pitfall 4 (single-call-site discipline, ADR-0005): every FIR helper has
 * exactly one caller. spu94_fir_decimate and spu94_fir_interpolate are
 * called ONLY from spu94_fir_chain_step (and from the reverb-bypass
 * variant and from test TUs). spu94_fir_chain_step is called from
 * Phase 5's future spu94_process body.
 *
 * Plan 01 (this plan) lands declarations + the coefficient table.
 * Plan 02 lands the folded-form decimate / interpolate bodies.
 * Plan 03 lands the spu94_fir_chain_step (+ reverb-bypass variant) body.
 * Plan 04 lands the test battery + ADRs + empirical witness classification.
 */
#ifndef SPU94_FIR_INTERNAL_H
#define SPU94_FIR_INTERNAL_H

#include <spu94/spu94.h>            /* spu94_state typedef */
#include <stdint.h>
#include <stddef.h>

/* -------------------------------------------------------------------- */
/* 39-tap half-band FIR coefficient table (D-11).                       */
/* Defined in spu94_fir_coef.c. Shared between decimator and            */
/* interpolator (D-08 — same table, separate delay-line state).         */
/* -------------------------------------------------------------------- */
extern const int16_t spu94_fir_coef[39];

/* -------------------------------------------------------------------- */
/* Decimator stage (CORE-06).                                            */
/* Pushes one 44.1 kHz stereo sample into the per-channel delay lines   */
/* (fir_delay_l_in / fir_delay_r_in) and advances the decimator phase   */
/* counter (fir_decimate_phase). When the retained phase fires (every   */
/* other call), computes the folded-form 39-tap FIR output for L and R  */
/* into *output_l / *output_r and sets *output_valid = 1. On the        */
/* discarded phase, *output_valid = 0 and *output_l / *output_r are     */
/* unmodified. Plan 02 body.                                             */
/* -------------------------------------------------------------------- */
void spu94_fir_decimate(spu94_state *state,
                        int16_t input_sample_l,
                        int16_t input_sample_r,
                        int16_t *output_l,
                        int16_t *output_r,
                        int *output_valid);

/* -------------------------------------------------------------------- */
/* Interpolator stage (CORE-07).                                         */
/* Pushes one 22.05 kHz stereo sample into the per-channel delay lines  */
/* (fir_delay_l_out / fir_delay_r_out) and computes BOTH phases of the  */
/* 2x upsampled 44.1 kHz output: phase 0 = every-other-tap subfilter,   */
/* phase 1 = center-tap passthrough (scaled by 0x4000 in Q15). Plan 02  */
/* body. Note: latency bookkeeping is per D-09 — 19 44.1-kHz-reference  */
/* samples at the interpolator stage.                                    */
/* -------------------------------------------------------------------- */
void spu94_fir_interpolate(spu94_state *state,
                           int16_t input_sample_l,
                           int16_t input_sample_r,
                           int16_t *output_l_phase0,
                           int16_t *output_r_phase0,
                           int16_t *output_l_phase1,
                           int16_t *output_r_phase1);

/* -------------------------------------------------------------------- */
/* Internal 44.1 kHz chain wrapper (D-07).                               */
/* Composes decimate -> spu94_tick -> interpolate. Consumes one stereo  */
/* int16 sample at 44.1 kHz; produces one stereo int16 sample at        */
/* 44.1 kHz. Never exposed on include/spu94/ (Phase 5 wraps via         */
/* spu94_process; Phase 4 keeps it internal). Plan 03 body.              */
/* -------------------------------------------------------------------- */
void spu94_fir_chain_step(spu94_state *state,
                          int16_t l_in_44k1,
                          int16_t r_in_44k1,
                          int16_t *l_out_44k1,
                          int16_t *r_out_44k1);

/* -------------------------------------------------------------------- */
/* Test-only reverb-bypass chain wrapper (04-CONTEXT Specifics,         */
/* 04-RESEARCH Decision Proposals). Same signature as                    */
/* spu94_fir_chain_step, but the middle step passes the 22.05 kHz       */
/* samples through unchanged instead of calling spu94_tick. Used by     */
/* DC-round-trip, round-trip-transparency, and impulse-response tests   */
/* that need to isolate the FIR chain from the reverb network.          */
/* Always compiled (not conditional) — Plan 03 body.                     */
/* -------------------------------------------------------------------- */
void spu94_fir_chain_step_reverb_bypass(spu94_state *state,
                                        int16_t l_in_44k1,
                                        int16_t r_in_44k1,
                                        int16_t *l_out_44k1,
                                        int16_t *r_out_44k1);

#endif /* SPU94_FIR_INTERNAL_H */
