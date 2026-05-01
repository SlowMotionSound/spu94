/* src/spu94/spu94_dac_fir.c -- Phase 6 Plan 01
 *
 * Three cascaded half-band FIR stages modeling the AK4309 DAC's digital
 * interpolation filter. Folded-form + zero-skip optimization (D-01):
 * 15 + 4 + 3 = 22 multiplies total per sample through the cascade.
 *
 * All three stages operate at 44.1 kHz on every call (Pitfall 5 -- NOT
 * at increasing rates). The cascade reproduces the passband ripple
 * character at the audio rate; the upsampling is already accounted for
 * in the coefficient design (Phase 5).
 *
 * Delay-line convention: circular buffer. delay[idx] holds the OLDEST
 * slot. Push writes delay[idx] then advances idx = (idx + 1) % ntaps.
 * Read at logical position k (0 = newest, ntaps-1 = oldest) uses
 * delay[(idx + ntaps - 1 - k) % ntaps]. Same convention as spu94_fir.c.
 *
 * DC gain (Pitfall 6 -- NOT compensated):
 *   Stage 1: sum = 0x7F8E (32654 signed, DC gain -0.030 dB)
 *   Stage 2: sum = 0x801A (-32742 signed / 32794 unsigned, DC gain +0.007 dB)
 *   Stage 3: sum = 0x7FF4 (32756 signed, DC gain -0.003 dB)
 *   Cascade: approximately -0.027 dB. Not compensated (matches project
 *            convention from spu94_fir.c -- bit-faithful).
 */
#include "spu94_dac_fir_internal.h"
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <string.h>

/* ========================================================================
 * Accumulator Width Proof -- Stage 1 (55-tap half-band)
 *
 * Folded form: center tap + 14 symmetric pairs. Pre-added pairs change
 * the worst-case input range from int16 to int17 (sum of two int16 values).
 *
 * Center tap: |0x4000| * |INT16_MIN| = 16384 * 32768 = 536,870,912
 * 14 pairs: sum(|coef[k]| * max(|x[k]+x[N-1-k]|)) for each pair
 *   Worst-case pair sum: +65534 for same-sign coefs, +65536 for
 *   opposite-sign coefs (INT16_MAX + INT16_MAX vs INT16_MIN + INT16_MIN).
 *
 * Total worst-case: 1,904,643,762 = 0x71868EB2
 * INT32_MAX: 2,147,483,647 = 0x7FFFFFFF
 * Headroom: 1.04 dB (0.17 bits)
 *
 * int32 is sufficient. Note tight margin; if a future modification needs
 * more headroom, promote to int64 per the D-02 seam.
 *
 * Validated empirically by tests/unit/dac_fir/test_dac_fir_overflow_proof.c.
 * ========================================================================
 */

/* ========================================================================
 * Accumulator Width Proof -- Stage 2 (11-tap half-band)
 *
 * Folded form: center tap + 3 symmetric pairs = 4 multiplies.
 *
 * Total worst-case: 1,336,455,240 = 0x4FA8B048
 * INT32_MAX: 2,147,483,647 = 0x7FFFFFFF
 * Headroom: 4.12 dB
 *
 * int32 is sufficient.
 *
 * Validated empirically by tests/unit/dac_fir/test_dac_fir_overflow_proof.c.
 * ========================================================================
 */

/* ========================================================================
 * Accumulator Width Proof -- Stage 3 (7-tap half-band)
 *
 * Folded form: center tap + 2 symmetric pairs = 3 multiplies.
 *
 * Total worst-case: 1,221,048,126 = 0x48C7B73E
 * INT32_MAX: 2,147,483,647 = 0x7FFFFFFF
 * Headroom: 4.90 dB
 *
 * int32 is sufficient.
 *
 * Validated empirically by tests/unit/dac_fir/test_dac_fir_overflow_proof.c.
 * ========================================================================
 */

/* -------------------------------------------------------------------- */
/* Delay line helpers (parameterized by ntaps, same pattern as           */
/* fir_read_tap/fir_push in spu94_fir.c).                               */
/* -------------------------------------------------------------------- */

/* Read logical tap k (0 = newest, ntaps-1 = oldest) from a circular
 * buffer with idx pointing at the oldest slot (next-to-write). */
static inline int16_t dac_fir_read_tap(const int16_t *delay, uint8_t idx,
                                       unsigned k, unsigned ntaps) {
    unsigned pos = ((unsigned)idx + ntaps - 1u - k) % ntaps;
    return delay[pos];
}

/* Push a new sample into the ring buffer. Advances idx. */
static inline void dac_fir_push(int16_t *delay, uint8_t *idx,
                                int16_t sample, unsigned ntaps) {
    delay[*idx] = sample;
    *idx = (uint8_t)(((unsigned)(*idx) + 1u) % ntaps);
}

/* -------------------------------------------------------------------- */
/* Per-stage folded apply function. Adapts the fir_folded_apply pattern  */
/* from spu94_fir.c for a generic N-tap half-band stage with a pair     */
/* index table. D-01: skip zeros AND fold symmetry. D-03 clamp-once:    */
/* single shift + sat_s16 at the end.                                    */
/* -------------------------------------------------------------------- */
static int16_t dac_fir_stage_apply(const int16_t *delay, uint8_t idx,
                                   unsigned ntaps,
                                   const int16_t *coef,
                                   const unsigned (*pairs)[2],
                                   unsigned n_pairs) {
    unsigned center = ntaps / 2;
    int32_t acc = (int32_t)coef[center]
                * (int32_t)dac_fir_read_tap(delay, idx, center, ntaps);

    for (unsigned i = 0; i < n_pairs; ++i) {
        int16_t c = coef[pairs[i][0]];
        /* Pre-add symmetric pair before multiply (D-01 fold).
         * Widen to int32 before adding to avoid int16 overflow. */
        int32_t pair = (int32_t)dac_fir_read_tap(delay, idx, pairs[i][0], ntaps)
                     + (int32_t)dac_fir_read_tap(delay, idx, pairs[i][1], ntaps);
        acc += (int32_t)c * pair;
    }

    /* D-03 clamp-once: single shift + sat_s16 at the end.
     * ADR-0001 arithmetic-right-shift (truncation toward -inf). */
    return sat_s16(acc >> 15);
}

/* -------------------------------------------------------------------- */
/* Public API                                                            */
/* -------------------------------------------------------------------- */

void spu94_dac_fir_init(spu94_dac_fir_state *state) {
    memset(state, 0, sizeof(*state));
}

int16_t spu94_dac_fir_step(spu94_dac_fir_state *state, int16_t input) {
    /* Stage 1: 55-tap half-band, 14 pairs + 1 center = 15 multiplies */
    dac_fir_push(state->stage1_delay, &state->stage1_idx,
                 input, DAC_FIR_STAGE1_NTAPS);
    int16_t s1 = dac_fir_stage_apply(state->stage1_delay, state->stage1_idx,
                                     DAC_FIR_STAGE1_NTAPS,
                                     dac_interp_stage1,
                                     dac_fir_stage1_pairs,
                                     DAC_FIR_STAGE1_NPAIRS);

    /* Stage 2: 11-tap half-band, 3 pairs + 1 center = 4 multiplies */
    dac_fir_push(state->stage2_delay, &state->stage2_idx,
                 s1, DAC_FIR_STAGE2_NTAPS);
    int16_t s2 = dac_fir_stage_apply(state->stage2_delay, state->stage2_idx,
                                     DAC_FIR_STAGE2_NTAPS,
                                     dac_interp_stage2,
                                     dac_fir_stage2_pairs,
                                     DAC_FIR_STAGE2_NPAIRS);

    /* Stage 3: 7-tap half-band, 2 pairs + 1 center = 3 multiplies */
    dac_fir_push(state->stage3_delay, &state->stage3_idx,
                 s2, DAC_FIR_STAGE3_NTAPS);
    int16_t s3 = dac_fir_stage_apply(state->stage3_delay, state->stage3_idx,
                                     DAC_FIR_STAGE3_NTAPS,
                                     dac_interp_stage3,
                                     dac_fir_stage3_pairs,
                                     DAC_FIR_STAGE3_NPAIRS);

    return s3;
}

/* ========================================================================
 * Accumulator Width Proof -- 8x Zero-Stuffed Path
 *
 * With naive zero-stuffing (D-01), every other delay line entry pushed
 * is 0. This strictly reduces the accumulator worst case because the
 * folded-pair additions include zero-valued taps. For each stage, the
 * 8x worst-case accumulator sum is bounded by the v1.2 worst case:
 *
 *   Stage 1: <= 1,904,643,762 (0x71868EB2) -- same or fewer non-zero entries
 *   Stage 2: <= 1,336,455,240 (0x4FA8B048) -- same reasoning
 *   Stage 3: <= 1,221,048,126 (0x48C7B73E) -- same reasoning
 *
 * int32 accumulators remain sufficient. See v1.2 proofs above for
 * full derivation methodology.
 * ========================================================================
 */

int16_t spu94_dac_fir_step_8x(spu94_dac_fir_state *state, int16_t input) {
    /* Stage 1: 2 evaluations at 88.2kHz (push real, evaluate; push 0, evaluate) */
    int16_t s1[2];

    dac_fir_push(state->stage1_delay, &state->stage1_idx,
                 input, DAC_FIR_STAGE1_NTAPS);
    s1[0] = dac_fir_stage_apply(state->stage1_delay, state->stage1_idx,
                                DAC_FIR_STAGE1_NTAPS,
                                dac_interp_stage1,
                                dac_fir_stage1_pairs,
                                DAC_FIR_STAGE1_NPAIRS);

    dac_fir_push(state->stage1_delay, &state->stage1_idx,
                 0, DAC_FIR_STAGE1_NTAPS);
    s1[1] = dac_fir_stage_apply(state->stage1_delay, state->stage1_idx,
                                DAC_FIR_STAGE1_NTAPS,
                                dac_interp_stage1,
                                dac_fir_stage1_pairs,
                                DAC_FIR_STAGE1_NPAIRS);

    /* Stage 2: 4 evaluations at 176.4kHz */
    int16_t s2[4];
    for (int i = 0; i < 2; i++) {
        dac_fir_push(state->stage2_delay, &state->stage2_idx,
                     s1[i], DAC_FIR_STAGE2_NTAPS);
        s2[2 * i] = dac_fir_stage_apply(state->stage2_delay, state->stage2_idx,
                                        DAC_FIR_STAGE2_NTAPS,
                                        dac_interp_stage2,
                                        dac_fir_stage2_pairs,
                                        DAC_FIR_STAGE2_NPAIRS);

        dac_fir_push(state->stage2_delay, &state->stage2_idx,
                     0, DAC_FIR_STAGE2_NTAPS);
        s2[2 * i + 1] = dac_fir_stage_apply(state->stage2_delay, state->stage2_idx,
                                            DAC_FIR_STAGE2_NTAPS,
                                            dac_interp_stage2,
                                            dac_fir_stage2_pairs,
                                            DAC_FIR_STAGE2_NPAIRS);
    }

    /* Stage 3: 8 evaluations at 352.8kHz. All 8 must execute because each
     * push advances the delay line state affecting future calls. Only the
     * last output survives decimation (DSP-06). */
    int16_t s3_last = 0;
    for (int j = 0; j < 4; j++) {
        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     s2[j], DAC_FIR_STAGE3_NTAPS);
        (void)dac_fir_stage_apply(state->stage3_delay, state->stage3_idx,
                                  DAC_FIR_STAGE3_NTAPS,
                                  dac_interp_stage3,
                                  dac_fir_stage3_pairs,
                                  DAC_FIR_STAGE3_NPAIRS);

        dac_fir_push(state->stage3_delay, &state->stage3_idx,
                     0, DAC_FIR_STAGE3_NTAPS);
        s3_last = dac_fir_stage_apply(state->stage3_delay, state->stage3_idx,
                                      DAC_FIR_STAGE3_NTAPS,
                                      dac_interp_stage3,
                                      dac_fir_stage3_pairs,
                                      DAC_FIR_STAGE3_NPAIRS);
    }

    /* Gain compensation: naive zero-stuff decimation (keep last of 8)
     * attenuates by 1/8. Shift left 3 to restore unity gain.
     * Phase 11 may revisit if noise floor or clipping behavior needs tuning. */
    return sat_s16((int32_t)s3_last << 3);
}

/* -------------------------------------------------------------------- */
/* Test-visible per-stage apply wrapper (Option B from plan).            */
/* Takes a stage number (1, 2, or 3) and a delay line with the same     */
/* circular buffer convention as the production code. Applies the        */
/* folded-form FIR for that stage and returns the sat_s16 result.        */
/*                                                                       */
/* Used by test_dac_fir_overflow_proof.c to exercise each stage          */
/* independently without cascading. Never on production hot path.        */
/* -------------------------------------------------------------------- */
int16_t spu94_dac_fir_test_stage_apply(unsigned stage_num,
                                       const int16_t *delay, uint8_t idx) {
    switch (stage_num) {
    case 1:
        return dac_fir_stage_apply(delay, idx,
                                   DAC_FIR_STAGE1_NTAPS,
                                   dac_interp_stage1,
                                   dac_fir_stage1_pairs,
                                   DAC_FIR_STAGE1_NPAIRS);
    case 2:
        return dac_fir_stage_apply(delay, idx,
                                   DAC_FIR_STAGE2_NTAPS,
                                   dac_interp_stage2,
                                   dac_fir_stage2_pairs,
                                   DAC_FIR_STAGE2_NPAIRS);
    case 3:
        return dac_fir_stage_apply(delay, idx,
                                   DAC_FIR_STAGE3_NTAPS,
                                   dac_interp_stage3,
                                   dac_fir_stage3_pairs,
                                   DAC_FIR_STAGE3_NPAIRS);
    default:
        return 0;
    }
}
