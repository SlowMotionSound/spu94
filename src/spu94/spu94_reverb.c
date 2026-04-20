/* src/spu94/spu94_reverb.c — Phase 3 Plan 01
 *
 * The reverb network computation. Invoked once per 22.05 kHz tick from
 * spu94_tick (D-06, Pitfall 4: single call site).
 *
 * Plan 01 implements: input_scale (widening multiply), hard_clip
 * (CORE-02, D-09), output_scale (Q15 multiply with err tap per D-11).
 * Plan 02 adds: same_iir, diff_iir (with vIIR=INT16_MIN anomaly, D-10).
 * Plan 03 adds: comb (cascading sat_s16 per D-07), apf1, apf2.
 *
 * D-08 snapshot policy: every v* register is read ONCE at the top of
 * spu94_reverb_body; snapshots are passed down to stage functions.
 * No stage function re-reads a v* register from state (Pitfall 4).
 *
 * Nocash source (paraphrased per DOCS-03): psx-spx.consoledev.net/
 * soundprocessingunitspu/ — SPU Reverb Formula section.
 */
#include "spu94_reverb_internal.h"
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_q15.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>

/* Forward decl — Phase 2 engine layer (spu94_register_io.c). */
int16_t  spu94_get_reg_i16(const spu94_state *state, spu94_reg_t reg);
uint16_t spu94_get_reg_u16(const spu94_state *state, spu94_reg_t reg);

/* =====================================================================
 * Stage: input_scale
 * Nocash: "Lin = vLIN * LeftInput; Rin = vRIN * RightInput"
 * Widens int16 x int16 to int32 (no shift here — the hard_clip stage
 * handles the saturation in the next stage, matching D-09).
 * ===================================================================== */
void spu94_reverb_input_scale(spu94_state *state,
                              int16_t left_in, int16_t right_in,
                              int16_t vLIN_snap, int16_t vRIN_snap,
                              int32_t *Lin_out, int32_t *Rin_out)
{
    (void)state;  /* err_input_scale stays zero — no truncation at
                   * this stage (no >>15 shift yet). Field exists for
                   * symmetry per D-11. */
    *Lin_out = (int32_t)left_in  * (int32_t)vLIN_snap;
    *Rin_out = (int32_t)right_in * (int32_t)vRIN_snap;
}

/* =====================================================================
 * Stage: hard_clip  (CORE-02, D-09)
 * Sat_s16 on the int32 input-scale output. Emits the overflow-
 * magnitude observable (D-11 extension) to feed future Controllers
 * use cases (drive meter, soft-clip warmth, etc.).
 * ===================================================================== */
void spu94_reverb_hard_clip(int32_t Lin_wide, int32_t Rin_wide,
                            int16_t *Lin_out, int16_t *Rin_out,
                            int32_t *overflow_out)
{
    *Lin_out = sat_s16(Lin_wide);
    *Rin_out = sat_s16(Rin_wide);

    if (overflow_out != (int32_t *)0) {
        /* overflow_magnitude = sum of |x| - INT16_MAX for inputs
         * outside ±INT16_MAX, 0 otherwise. Use int64 intermediate to
         * avoid INT32_MIN-negation UB (Pitfall 1 generalized). */
        int64_t l_abs = (Lin_wide < 0) ? -(int64_t)Lin_wide : (int64_t)Lin_wide;
        int64_t r_abs = (Rin_wide < 0) ? -(int64_t)Rin_wide : (int64_t)Rin_wide;
        int64_t lo = (l_abs > (int64_t)INT16_MAX) ? (l_abs - (int64_t)INT16_MAX) : (int64_t)0;
        int64_t ro = (r_abs > (int64_t)INT16_MAX) ? (r_abs - (int64_t)INT16_MAX) : (int64_t)0;
        int64_t sum = lo + ro;
        /* Sum is bounded by 2 * (INT32_MAX - INT16_MAX) < INT32_MAX;
         * cast to int32 is safe. */
        *overflow_out = (int32_t)sum;
    }
}

/* =====================================================================
 * Stage: output_scale (E5)
 * Nocash: "LeftOutput = Lout*vLOUT; RightOutput = Rout*vROUT"
 * Uses q15_mul_truncate_with_err (D-11 per-multiply err tap).
 * ===================================================================== */
void spu94_reverb_output_scale(spu94_state *state,
                               int16_t Lout, int16_t Rout,
                               int16_t vLOUT_snap, int16_t vROUT_snap,
                               int32_t *LeftOutput_out,
                               int32_t *RightOutput_out)
{
    int16_t err_l = 0, err_r = 0;
    int16_t L = q15_mul_truncate_with_err(Lout, vLOUT_snap, &err_l);
    int16_t R = q15_mul_truncate_with_err(Rout, vROUT_snap, &err_r);
    state->err_output_scale += (int32_t)err_l + (int32_t)err_r;
    *LeftOutput_out  = (int32_t)L;
    *RightOutput_out = (int32_t)R;
}

/* =====================================================================
 * Plan 02 / Plan 03 stage stubs
 *
 * The header spu94_reverb_internal.h declares five more stage functions
 * (same_iir, diff_iir, comb, apf1, apf2) that Plans 02 and 03 flesh out.
 * Plan 01's reverb body does NOT call any of them. We define empty
 * bodies here so:
 *   - The shared library links cleanly if anything outside the plan
 *     references a declared-but-undefined symbol.
 *   - Plans 02 / 03 have concrete function bodies to replace rather
 *     than create.
 * Each stub is intentionally a no-op: no state mutation, no buffer I/O.
 * ===================================================================== */

void spu94_reverb_same_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap)
{
    (void)state; (void)Lin; (void)Rin;
    (void)vIIR_snap; (void)vWALL_snap;
    /* Plan 02 body. */
}

void spu94_reverb_diff_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap)
{
    (void)state; (void)Lin; (void)Rin;
    (void)vIIR_snap; (void)vWALL_snap;
    /* Plan 02 body. */
}

void spu94_reverb_comb(spu94_state *state,
                       int16_t vCOMB1_snap, int16_t vCOMB2_snap,
                       int16_t vCOMB3_snap, int16_t vCOMB4_snap,
                       int16_t *Lout_out, int16_t *Rout_out)
{
    (void)state;
    (void)vCOMB1_snap; (void)vCOMB2_snap;
    (void)vCOMB3_snap; (void)vCOMB4_snap;
    if (Lout_out != (int16_t *)0) *Lout_out = 0;
    if (Rout_out != (int16_t *)0) *Rout_out = 0;
    /* Plan 03 body. */
}

void spu94_reverb_apf1(spu94_state *state,
                       int16_t vAPF1_snap, uint16_t dAPF1_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout)
{
    (void)state; (void)vAPF1_snap; (void)dAPF1_snap;
    (void)Lout_inout; (void)Rout_inout;
    /* Plan 03 body. */
}

void spu94_reverb_apf2(spu94_state *state,
                       int16_t vAPF2_snap, uint16_t dAPF2_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout)
{
    (void)state; (void)vAPF2_snap; (void)dAPF2_snap;
    (void)Lout_inout; (void)Rout_inout;
    /* Plan 03 body. */
}

/* =====================================================================
 * Top-level reverb body (D-05, D-06). Called from spu94_tick.
 * ===================================================================== */
void spu94_reverb_body(spu94_state *state)
{
    if (state == (spu94_state *)0) return;

    /* D-08: freeze v* snapshot at pair start. Both L and R halves of
     * this 22.05 kHz tick observe the same v* values. Mid-tick writes
     * to v* IMMEDIATE registers take effect on the NEXT tick. */
    const int16_t vLIN_snap  = spu94_get_reg_i16(state, SPU94_REG_vLIN);
    const int16_t vRIN_snap  = spu94_get_reg_i16(state, SPU94_REG_vRIN);
    const int16_t vLOUT_snap = spu94_get_reg_i16(state, SPU94_REG_vLOUT);
    const int16_t vROUT_snap = spu94_get_reg_i16(state, SPU94_REG_vROUT);
    /* (Plans 02/03 snapshot: vIIR, vWALL, vAPF1, vAPF2, vCOMB1..4,
     * dAPF1, dAPF2. In Plan 01 these are unread — the IIR/comb/APF
     * stages do not run yet.) */

    /* Phase 3 Plan 01: no public mix-bus feed yet. Phase 5's
     * spu94_process will populate left_in/right_in from the host's
     * int16 stereo stream. Until then the reverb body runs with
     * silent input, which is the correct no-op behavior. */
    const int16_t left_in = 0;
    const int16_t right_in = 0;

    int32_t Lin_wide = 0, Rin_wide = 0;
    spu94_reverb_input_scale(state, left_in, right_in,
                             vLIN_snap, vRIN_snap,
                             &Lin_wide, &Rin_wide);

    int16_t Lin = 0, Rin = 0;
    int32_t overflow = 0;
    spu94_reverb_hard_clip(Lin_wide, Rin_wide, &Lin, &Rin, &overflow);
    state->overflow_magnitude += overflow;
    (void)Lin; (void)Rin;  /* Plans 02/03 feed these into same_iir/diff_iir. */

    /* Plan 02 will insert here:
     *   spu94_reverb_same_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
     *   spu94_reverb_diff_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
     * Plan 03 will insert here:
     *   spu94_reverb_comb(state, vCOMB1_snap, ..., &Lout, &Rout);
     *   spu94_reverb_apf1(state, vAPF1_snap, dAPF1_snap, &Lout, &Rout);
     *   spu94_reverb_apf2(state, vAPF2_snap, dAPF2_snap, &Lout, &Rout);
     */
    int16_t Lout = 0;  /* Plans 02/03 replace with APF2 output. */
    int16_t Rout = 0;

    int32_t LeftOutput = 0, RightOutput = 0;
    spu94_reverb_output_scale(state, Lout, Rout, vLOUT_snap, vROUT_snap,
                              &LeftOutput, &RightOutput);
    /* LeftOutput/RightOutput are not yet consumed — Phase 4 FIR will
     * read them when the 39-tap interpolator lands. */
    (void)LeftOutput;
    (void)RightOutput;
}
