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
 * Reverb work-buffer tap helpers (Plan 02)
 *
 * Nocash register values for m* / d* are halfword indexes (u16, 0..0xFFFF
 * with `-2` meaning "one halfword earlier in the ring"). Byte offset =
 * halfword_idx * 2. The 0x7FFFE mask matches Phase 2 Plan 04's
 * buffer_advance wrap rule (byte-aligned halfword ring at max 0x80000
 * bytes). Pitfall 1 reminder: halfword subtraction wraps naturally in
 * uint16_t; callers pass `(reg_value - 2)` as a u16 and the helper
 * computes the byte offset.
 *
 * If work_buf_size is smaller than 0x80000 (caller supplies a smaller
 * buffer — legal per Phase 2 init contract), out-of-range reads return
 * 0 and writes are discarded; the 0x7FFFE mask alone already gives the
 * hardware-correct address, and the extra bounds check is defensive.
 * Little-endian halfword format matches the Phase 2 buffer TU
 * convention (host-endian-agnostic serialization of the ring).
 * ===================================================================== */
static inline int16_t reverb_buf_read(const spu94_state *s,
                                      uint16_t halfword_offset)
{
    if (s->work_buf == (unsigned char *)0) return 0;
    uint32_t byte_off = (s->buffer_address
                         + (uint32_t)halfword_offset * 2u) & 0x7FFFEu;
    if ((size_t)byte_off + 1u >= s->work_buf_size) return 0;
    /* Little-endian halfword read. */
    return (int16_t)((uint16_t)s->work_buf[byte_off]
                   | ((uint16_t)s->work_buf[byte_off + 1u] << 8));
}

static inline void reverb_buf_write(spu94_state *s,
                                    uint16_t halfword_offset,
                                    int16_t value)
{
    if (s->work_buf == (unsigned char *)0) return;
    uint32_t byte_off = (s->buffer_address
                         + (uint32_t)halfword_offset * 2u) & 0x7FFFEu;
    if ((size_t)byte_off + 1u >= s->work_buf_size) return;
    uint16_t u = (uint16_t)value;
    s->work_buf[byte_off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[byte_off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}

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

/* =====================================================================
 * Stage: SAME IIR (CORE-05, CORE-08, D-10) — Plan 02
 *
 * Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
 *   [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]  ;L-to-L
 *   [mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]  ;R-to-R
 *
 * D-10 anomaly: vIIR == -0x8000 negates the final memory-written result
 * (Pitfall 5: negation runs AFTER saturation, BEFORE the store). The
 * int32 widening guards against INT16_MIN-negation UB (Pitfall 1).
 *
 * D-11 scope (i): every Q15 multiply feeds state->err_same_iir via
 * q15_mul_truncate_with_err's pre-saturation remainder. Each side has
 * two multiplies (wall tap + iir); L+R = 4 remainders per call.
 * ===================================================================== */
void spu94_reverb_same_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap)
{
    if (state == (spu94_state *)0) return;

    /* L side: [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2] */
    {
        uint16_t dLSAME = spu94_get_reg_u16(state, SPU94_REG_dLSAME);
        uint16_t mLSAME = spu94_get_reg_u16(state, SPU94_REG_mLSAME);
        int16_t  tap_d    = reverb_buf_read(state, dLSAME);
        int16_t  tap_prev = reverb_buf_read(state, (uint16_t)(mLSAME - 2u));

        int16_t err = 0;
        int16_t wall_prod = q15_mul_truncate_with_err(tap_d, vWALL_snap, &err);
        state->err_same_iir += (int32_t)err;

        int16_t acc = q15_add_sat(Lin, wall_prod);
        /* Subtract tap_prev. Widen to int32 before negating to guard the
         * INT16_MIN-negation UB edge (Pitfall 1). */
        acc = q15_add_sat(acc, sat_s16(-(int32_t)tap_prev));

        err = 0;
        int16_t iir_prod = q15_mul_truncate_with_err(acc, vIIR_snap, &err);
        state->err_same_iir += (int32_t)err;

        int16_t result = q15_add_sat(iir_prod, tap_prev);

        /* D-10 anomaly branch: AFTER saturation, BEFORE memory write
         * (Pitfall 5). sat_s16 guards the INT16_MIN->+INT16_MAX edge. */
        if (vIIR_snap == INT16_MIN) {
            result = sat_s16(-(int32_t)result);
        }
        reverb_buf_write(state, mLSAME, result);
    }

    /* R side: [mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2] */
    {
        uint16_t dRSAME = spu94_get_reg_u16(state, SPU94_REG_dRSAME);
        uint16_t mRSAME = spu94_get_reg_u16(state, SPU94_REG_mRSAME);
        int16_t  tap_d    = reverb_buf_read(state, dRSAME);
        int16_t  tap_prev = reverb_buf_read(state, (uint16_t)(mRSAME - 2u));

        int16_t err = 0;
        int16_t wall_prod = q15_mul_truncate_with_err(tap_d, vWALL_snap, &err);
        state->err_same_iir += (int32_t)err;

        int16_t acc = q15_add_sat(Rin, wall_prod);
        acc = q15_add_sat(acc, sat_s16(-(int32_t)tap_prev));

        err = 0;
        int16_t iir_prod = q15_mul_truncate_with_err(acc, vIIR_snap, &err);
        state->err_same_iir += (int32_t)err;

        int16_t result = q15_add_sat(iir_prod, tap_prev);

        if (vIIR_snap == INT16_MIN) {
            result = sat_s16(-(int32_t)result);
        }
        reverb_buf_write(state, mRSAME, result);
    }
}

/* =====================================================================
 * Stage: DIFF IIR (CORE-05, CORE-08, D-10) — Plan 02
 *
 * Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
 *   [mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]  ;R-to-L
 *   [mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]  ;L-to-R
 *
 * Structurally identical to SAME IIR with ONE distinction: the wall tap
 * read uses the *cross-side* d* register. The L side's wall tap reads
 * via dRDIFF (not dLDIFF); the R side's reads via dLDIFF. This is the
 * "R-to-L / L-to-R" scattering that distinguishes DIFF from SAME.
 *
 * D-10 anomaly + D-11 per-multiply err tap apply identically to SAME IIR.
 * ===================================================================== */
void spu94_reverb_diff_iir(spu94_state *state,
                           int16_t Lin, int16_t Rin,
                           int16_t vIIR_snap, int16_t vWALL_snap)
{
    if (state == (spu94_state *)0) return;

    /* L side: [mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]
     * NOTE: cross-side tap — dRDIFF (not dLDIFF) feeds the L-side write. */
    {
        uint16_t dRDIFF = spu94_get_reg_u16(state, SPU94_REG_dRDIFF);
        uint16_t mLDIFF = spu94_get_reg_u16(state, SPU94_REG_mLDIFF);
        int16_t  tap_d    = reverb_buf_read(state, dRDIFF);
        int16_t  tap_prev = reverb_buf_read(state, (uint16_t)(mLDIFF - 2u));

        int16_t err = 0;
        int16_t wall_prod = q15_mul_truncate_with_err(tap_d, vWALL_snap, &err);
        state->err_diff_iir += (int32_t)err;

        int16_t acc = q15_add_sat(Lin, wall_prod);
        acc = q15_add_sat(acc, sat_s16(-(int32_t)tap_prev));

        err = 0;
        int16_t iir_prod = q15_mul_truncate_with_err(acc, vIIR_snap, &err);
        state->err_diff_iir += (int32_t)err;

        int16_t result = q15_add_sat(iir_prod, tap_prev);

        if (vIIR_snap == INT16_MIN) {
            result = sat_s16(-(int32_t)result);
        }
        reverb_buf_write(state, mLDIFF, result);
    }

    /* R side: [mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]
     * NOTE: cross-side tap — dLDIFF (not dRDIFF) feeds the R-side write. */
    {
        uint16_t dLDIFF = spu94_get_reg_u16(state, SPU94_REG_dLDIFF);
        uint16_t mRDIFF = spu94_get_reg_u16(state, SPU94_REG_mRDIFF);
        int16_t  tap_d    = reverb_buf_read(state, dLDIFF);
        int16_t  tap_prev = reverb_buf_read(state, (uint16_t)(mRDIFF - 2u));

        int16_t err = 0;
        int16_t wall_prod = q15_mul_truncate_with_err(tap_d, vWALL_snap, &err);
        state->err_diff_iir += (int32_t)err;

        int16_t acc = q15_add_sat(Rin, wall_prod);
        acc = q15_add_sat(acc, sat_s16(-(int32_t)tap_prev));

        err = 0;
        int16_t iir_prod = q15_mul_truncate_with_err(acc, vIIR_snap, &err);
        state->err_diff_iir += (int32_t)err;

        int16_t result = q15_add_sat(iir_prod, tap_prev);

        if (vIIR_snap == INT16_MIN) {
            result = sat_s16(-(int32_t)result);
        }
        reverb_buf_write(state, mRDIFF, result);
    }
}

/* =====================================================================
 * Stage: 4-tap COMB (CORE-05, D-07) — Plan 03
 *
 * Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
 *   Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2]
 *        + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
 *   Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2]
 *        + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]
 *
 * D-07 LOCKED: the 4-tap sum uses CASCADING sat_s16 after EACH add, NOT
 * an int32 accumulator with a single final clamp. Per the user's taste-
 * driven decision (CONTEXT.md § D-07), cascading clamps produce richer
 * distortion character at input extremes and a more interesting overflow
 * signal feeding the D-11 err_comb accumulator. Diverges from most
 * behavioral witnesses (DuckStation / Mednafen-PSX use int32 accumulate);
 * revert lever documented in ADR-0007 (landed Plan 04).
 *
 * q15_add_sat() internally widens to int32 and calls sat_s16, so each
 * invocation is one clamp point. Three cascading add+clamps per side
 * (after the first multiply establishes the initial accumulator).
 *
 * D-11 scope (i): every Q15 multiply feeds state->err_comb via
 * q15_mul_truncate_with_err's pre-saturation remainder. Four multiplies
 * per side; 8 remainders per call total.
 * ===================================================================== */
void spu94_reverb_comb(spu94_state *state,
                       int16_t vCOMB1_snap, int16_t vCOMB2_snap,
                       int16_t vCOMB3_snap, int16_t vCOMB4_snap,
                       int16_t *Lout_out, int16_t *Rout_out)
{
    if (state == (spu94_state *)0) return;
    if (Lout_out == (int16_t *)0 || Rout_out == (int16_t *)0) return;

    /* L side — read 4 taps via the m*COMB* halfword indexes. */
    uint16_t mLCOMB1 = spu94_get_reg_u16(state, SPU94_REG_mLCOMB1);
    uint16_t mLCOMB2 = spu94_get_reg_u16(state, SPU94_REG_mLCOMB2);
    uint16_t mLCOMB3 = spu94_get_reg_u16(state, SPU94_REG_mLCOMB3);
    uint16_t mLCOMB4 = spu94_get_reg_u16(state, SPU94_REG_mLCOMB4);

    int16_t tL1 = reverb_buf_read(state, mLCOMB1);
    int16_t tL2 = reverb_buf_read(state, mLCOMB2);
    int16_t tL3 = reverb_buf_read(state, mLCOMB3);
    int16_t tL4 = reverb_buf_read(state, mLCOMB4);

    int16_t e = 0;
    int16_t p1 = q15_mul_truncate_with_err(vCOMB1_snap, tL1, &e);
    state->err_comb += (int32_t)e;
    int16_t p2 = q15_mul_truncate_with_err(vCOMB2_snap, tL2, &e);
    state->err_comb += (int32_t)e;
    int16_t p3 = q15_mul_truncate_with_err(vCOMB3_snap, tL3, &e);
    state->err_comb += (int32_t)e;
    int16_t p4 = q15_mul_truncate_with_err(vCOMB4_snap, tL4, &e);
    state->err_comb += (int32_t)e;

    /* D-07 CASCADING sat_s16 — three clamp points per side. */
    int16_t accL = p1;
    accL = q15_add_sat(accL, p2);   /* sat #1 (D-07 cascade) */
    accL = q15_add_sat(accL, p3);   /* sat #2 */
    accL = q15_add_sat(accL, p4);   /* sat #3 */
    *Lout_out = accL;

    /* R side — mirror structure with mRCOMB1..4. */
    uint16_t mRCOMB1 = spu94_get_reg_u16(state, SPU94_REG_mRCOMB1);
    uint16_t mRCOMB2 = spu94_get_reg_u16(state, SPU94_REG_mRCOMB2);
    uint16_t mRCOMB3 = spu94_get_reg_u16(state, SPU94_REG_mRCOMB3);
    uint16_t mRCOMB4 = spu94_get_reg_u16(state, SPU94_REG_mRCOMB4);

    int16_t tR1 = reverb_buf_read(state, mRCOMB1);
    int16_t tR2 = reverb_buf_read(state, mRCOMB2);
    int16_t tR3 = reverb_buf_read(state, mRCOMB3);
    int16_t tR4 = reverb_buf_read(state, mRCOMB4);

    int16_t pR1 = q15_mul_truncate_with_err(vCOMB1_snap, tR1, &e);
    state->err_comb += (int32_t)e;
    int16_t pR2 = q15_mul_truncate_with_err(vCOMB2_snap, tR2, &e);
    state->err_comb += (int32_t)e;
    int16_t pR3 = q15_mul_truncate_with_err(vCOMB3_snap, tR3, &e);
    state->err_comb += (int32_t)e;
    int16_t pR4 = q15_mul_truncate_with_err(vCOMB4_snap, tR4, &e);
    state->err_comb += (int32_t)e;

    int16_t accR = pR1;
    accR = q15_add_sat(accR, pR2);
    accR = q15_add_sat(accR, pR3);
    accR = q15_add_sat(accR, pR4);
    *Rout_out = accR;
}

/* =====================================================================
 * Stage: APF1 (CORE-05) — Plan 03
 *
 * Nocash E1 (paraphrased, source: psx-spx.consoledev.net/soundprocessingunitspu/):
 *   Lout = Lout - vAPF1*[mLAPF1-dAPF1]       ;step 1 (subtract delayed tap)
 *   [mLAPF1] = Lout                           ;step 2 (store intermediate)
 *   Lout = Lout*vAPF1 + [mLAPF1-dAPF1]       ;step 3 (feedback output)
 *   (R side mirrors with mRAPF1)
 *
 * The delayed tap [mLAPF1-dAPF1] is read ONCE and reused in steps 1 and 3
 * (it is the value at that offset BEFORE the step-2 write to [mLAPF1]).
 * Step 2 writes to [mLAPF1], NOT to [mLAPF1-dAPF1], so there is no
 * read-after-write hazard within a single side.
 *
 * Pitfall 1 guard on the subtract: widen product to int32 before negate,
 * clamp via sat_s16, then q15_add_sat. This avoids UB for prod == INT16_MIN.
 *
 * Pitfall 7 (APF feedback edge): the intermediate step1 stored in memory
 * AND fed forward through the second multiply can be near-INT16_MIN. The
 * second multiply's saturation combined with the step-3 add is stable
 * because every operation goes through q15_mul_truncate_with_err + q15_add_sat.
 *
 * D-11 scope (i): 2 multiplies per side * 2 sides = 4 truncation remainders
 * per call, all accumulated into state->err_apf1.
 * ===================================================================== */
void spu94_reverb_apf1(spu94_state *state,
                       int16_t vAPF1_snap, uint16_t dAPF1_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout)
{
    if (state == (spu94_state *)0) return;
    if (Lout_inout == (int16_t *)0 || Rout_inout == (int16_t *)0) return;

    /* L side: feedback through mLAPF1 / dAPF1 offsets. */
    {
        uint16_t mLAPF1 = spu94_get_reg_u16(state, SPU94_REG_mLAPF1);
        uint16_t tap_offset = (uint16_t)(mLAPF1 - dAPF1_snap);
        int16_t tap_delayed = reverb_buf_read(state, tap_offset);
        int16_t Lin = *Lout_inout;

        int16_t e = 0;
        int16_t prod1 = q15_mul_truncate_with_err(vAPF1_snap, tap_delayed, &e);
        state->err_apf1 += (int32_t)e;

        /* Step 1: Lout = Lin - prod1. Pitfall-1 guard via int32 widen +
         * sat_s16 before the saturating add. */
        int16_t step1 = q15_add_sat(Lin, sat_s16(-(int32_t)prod1));

        /* Step 2: [mLAPF1] = step1 (intermediate stored). */
        reverb_buf_write(state, mLAPF1, step1);

        /* Step 3: Lout = step1 * vAPF1 + tap_delayed. */
        e = 0;
        int16_t prod2 = q15_mul_truncate_with_err(step1, vAPF1_snap, &e);
        state->err_apf1 += (int32_t)e;
        int16_t step3 = q15_add_sat(prod2, tap_delayed);

        *Lout_inout = step3;
    }

    /* R side: mirror with mRAPF1 / dAPF1. */
    {
        uint16_t mRAPF1 = spu94_get_reg_u16(state, SPU94_REG_mRAPF1);
        uint16_t tap_offset = (uint16_t)(mRAPF1 - dAPF1_snap);
        int16_t tap_delayed = reverb_buf_read(state, tap_offset);
        int16_t Rin = *Rout_inout;

        int16_t e = 0;
        int16_t prod1 = q15_mul_truncate_with_err(vAPF1_snap, tap_delayed, &e);
        state->err_apf1 += (int32_t)e;

        int16_t step1 = q15_add_sat(Rin, sat_s16(-(int32_t)prod1));
        reverb_buf_write(state, mRAPF1, step1);

        e = 0;
        int16_t prod2 = q15_mul_truncate_with_err(step1, vAPF1_snap, &e);
        state->err_apf1 += (int32_t)e;
        int16_t step3 = q15_add_sat(prod2, tap_delayed);

        *Rout_inout = step3;
    }
}

/* =====================================================================
 * Stage: APF2 (CORE-05) — Plan 03
 *
 * Structurally identical to APF1, with vAPF2 / dAPF2 / mLAPF2 / mRAPF2
 * substituted. Every Pitfall-1 + Pitfall-7 guard from APF1 applies here
 * verbatim. D-11 wiring feeds state->err_apf2 (4 remainders per call).
 * ===================================================================== */
void spu94_reverb_apf2(spu94_state *state,
                       int16_t vAPF2_snap, uint16_t dAPF2_snap,
                       int16_t *Lout_inout, int16_t *Rout_inout)
{
    if (state == (spu94_state *)0) return;
    if (Lout_inout == (int16_t *)0 || Rout_inout == (int16_t *)0) return;

    /* L side. */
    {
        uint16_t mLAPF2 = spu94_get_reg_u16(state, SPU94_REG_mLAPF2);
        uint16_t tap_offset = (uint16_t)(mLAPF2 - dAPF2_snap);
        int16_t tap_delayed = reverb_buf_read(state, tap_offset);
        int16_t Lin = *Lout_inout;

        int16_t e = 0;
        int16_t prod1 = q15_mul_truncate_with_err(vAPF2_snap, tap_delayed, &e);
        state->err_apf2 += (int32_t)e;

        int16_t step1 = q15_add_sat(Lin, sat_s16(-(int32_t)prod1));
        reverb_buf_write(state, mLAPF2, step1);

        e = 0;
        int16_t prod2 = q15_mul_truncate_with_err(step1, vAPF2_snap, &e);
        state->err_apf2 += (int32_t)e;
        int16_t step3 = q15_add_sat(prod2, tap_delayed);

        *Lout_inout = step3;
    }

    /* R side. */
    {
        uint16_t mRAPF2 = spu94_get_reg_u16(state, SPU94_REG_mRAPF2);
        uint16_t tap_offset = (uint16_t)(mRAPF2 - dAPF2_snap);
        int16_t tap_delayed = reverb_buf_read(state, tap_offset);
        int16_t Rin = *Rout_inout;

        int16_t e = 0;
        int16_t prod1 = q15_mul_truncate_with_err(vAPF2_snap, tap_delayed, &e);
        state->err_apf2 += (int32_t)e;

        int16_t step1 = q15_add_sat(Rin, sat_s16(-(int32_t)prod1));
        reverb_buf_write(state, mRAPF2, step1);

        e = 0;
        int16_t prod2 = q15_mul_truncate_with_err(step1, vAPF2_snap, &e);
        state->err_apf2 += (int32_t)e;
        int16_t step3 = q15_add_sat(prod2, tap_delayed);

        *Rout_inout = step3;
    }
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
    /* Plan 02: IIR coefficients snapshotted once per pair (D-08). */
    const int16_t vIIR_snap  = spu94_get_reg_i16(state, SPU94_REG_vIIR);
    const int16_t vWALL_snap = spu94_get_reg_i16(state, SPU94_REG_vWALL);
    /* Plan 03: comb + APF coefficient snapshots (D-08 pair-start freeze).
     * dAPF1 / dAPF2 are u16 TICK_LATCHED; snapshotting at body top
     * mirrors the v* symmetry and is resilient to future D-08 policy
     * tweaks. */
    const int16_t  vCOMB1_snap = spu94_get_reg_i16(state, SPU94_REG_vCOMB1);
    const int16_t  vCOMB2_snap = spu94_get_reg_i16(state, SPU94_REG_vCOMB2);
    const int16_t  vCOMB3_snap = spu94_get_reg_i16(state, SPU94_REG_vCOMB3);
    const int16_t  vCOMB4_snap = spu94_get_reg_i16(state, SPU94_REG_vCOMB4);
    const int16_t  vAPF1_snap  = spu94_get_reg_i16(state, SPU94_REG_vAPF1);
    const int16_t  vAPF2_snap  = spu94_get_reg_i16(state, SPU94_REG_vAPF2);
    const uint16_t dAPF1_snap  = spu94_get_reg_u16(state, SPU94_REG_dAPF1);
    const uint16_t dAPF2_snap  = spu94_get_reg_u16(state, SPU94_REG_dAPF2);

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

    /* Plan 02: SAME then DIFF IIR (CORE-05, CORE-08). Nocash order —
     * SAME IIR writes to [mLSAME]/[mRSAME] feed no DIFF tap, so the
     * two stages are independent within a pair; we keep nocash's
     * pseudocode order verbatim for readability. */
    spu94_reverb_same_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
    spu94_reverb_diff_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);

    /* Plan 03: 4-tap comb (D-07 cascading sat_s16) then APF1 + APF2.
     * Lout/Rout flow: comb -> apf1 -> apf2 -> output_scale. */
    int16_t Lout = 0;
    int16_t Rout = 0;
    spu94_reverb_comb(state, vCOMB1_snap, vCOMB2_snap,
                      vCOMB3_snap, vCOMB4_snap, &Lout, &Rout);
    spu94_reverb_apf1(state, vAPF1_snap, dAPF1_snap, &Lout, &Rout);
    spu94_reverb_apf2(state, vAPF2_snap, dAPF2_snap, &Lout, &Rout);

    int32_t LeftOutput = 0, RightOutput = 0;
    spu94_reverb_output_scale(state, Lout, Rout, vLOUT_snap, vROUT_snap,
                              &LeftOutput, &RightOutput);
    /* LeftOutput/RightOutput are not yet consumed — Phase 4 FIR will
     * read them when the 39-tap interpolator lands. */
    (void)LeftOutput;
    (void)RightOutput;
}
