/* src/spu94/spu94_reverb_internal.h — Phase 3 Plan 01
 *
 * INTERNAL header for libspu94 — not installed, never on the public
 * include path. Declares the seven nocash-derived reverb stage
 * functions (D-02) plus the top-level reverb body caller (D-05)
 * invoked from spu94_tick (D-06). Tests include this directly
 * analogous to how Phase 2 tests consume spu94_state_internal.h.
 */
#ifndef SPU94_REVERB_INTERNAL_H
#define SPU94_REVERB_INTERNAL_H

/* Include the umbrella header, not spu94_registers.h directly. The
 * registers sub-header's engine-layer setter signatures return
 * spu94_result_t (declared in spu94.h). Including the umbrella ensures
 * the type is visible whether this internal header is included first or
 * after spu94.h by a test TU. */
#include <spu94/spu94.h>            /* spu94_result_t + spu94_state */
#include <spu94/spu94_registers.h>  /* spu94_reg_t enum */
#include <stdint.h>

/* Top-level reverb-body caller. Invoked exactly once from spu94_tick
 * between apply_pending_writes and buffer_advance (D-06, Pitfall 4). */
void spu94_reverb_body(spu94_state *state);

/* Seven nocash-derived stage functions (D-02). Order of call:
 *   input_scale -> hard_clip -> same_iir -> diff_iir ->
 *   comb -> apf1 -> apf2 -> output_scale
 *
 * Each runs both L and R internally per nocash pseudocode structure.
 * Plan 01 implements input_scale, hard_clip, output_scale.
 * Plan 02 implements same_iir, diff_iir (with vIIR anomaly).
 * Plan 03 implements comb (cascading sat_s16 per D-07), apf1, apf2.
 *
 * v* register values are snapshotted at the top of spu94_reverb_body
 * (D-08: tick-latched snapshot, pair-start freeze) and passed down
 * as parameters to avoid the Pitfall-4 mid-tick-re-read hazard.
 */

/* Input scale: Lin = vLIN * LeftInput (int32 widened product).
 * left_in / right_in are the mix-bus inputs for this tick. Lin_out /
 * Rin_out receive the int32 products (pre-clip). */
void spu94_reverb_input_scale(spu94_state *state,
                              int16_t left_in, int16_t right_in,
                              int16_t vLIN_snap, int16_t vRIN_snap,
                              int32_t *Lin_out, int32_t *Rin_out);

/* Hard clip (CORE-02, D-09): sat_s16 on the int32 input-scale output.
 * Emits int16 Lin/Rin for the rest of the network. Additionally writes
 * the overflow-magnitude observable (D-11 extension): |x| - INT16_MAX
 * for |x| > INT16_MAX, zero otherwise. */
void spu94_reverb_hard_clip(int32_t Lin_wide, int32_t Rin_wide,
                            int16_t *Lin_out, int16_t *Rin_out,
                            int32_t *overflow_out);

/* SAME IIR (CORE-05, CORE-08, D-10). Plan 02. */
void spu94_reverb_same_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap);

/* DIFF IIR (CORE-05, CORE-08, D-10). Plan 02. */
void spu94_reverb_diff_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap);

/* 4-tap comb (CORE-05, D-07 cascading sat_s16 after each add).
 * Plan 03. */
void spu94_reverb_comb(spu94_state *state,
                       int16_t vCOMB1_snap, int16_t vCOMB2_snap,
                       int16_t vCOMB3_snap, int16_t vCOMB4_snap,
                       int16_t *Lout_out, int16_t *Rout_out);

/* APF1 (CORE-05). Plan 03. Reads/writes [mLAPF1] / [mRAPF1] via
 * dAPF1-offset tap, uses vAPF1. */
void spu94_reverb_apf1(spu94_state *state,
                       int16_t vAPF1_snap, uint16_t dAPF1_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout);

/* APF2 (CORE-05). Plan 03. */
void spu94_reverb_apf2(spu94_state *state,
                       int16_t vAPF2_snap, uint16_t dAPF2_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout);

/* Output scale: LeftOutput = Lout * vLOUT (int32 product).
 * Bit-faithful endpoint — emits int32 so callers can observe overflow
 * before any downstream saturation. */
void spu94_reverb_output_scale(spu94_state *state,
                               int16_t Lout, int16_t Rout,
                               int16_t vLOUT_snap, int16_t vROUT_snap,
                               int32_t *LeftOutput_out,
                               int32_t *RightOutput_out);

#endif /* SPU94_REVERB_INTERNAL_H */
