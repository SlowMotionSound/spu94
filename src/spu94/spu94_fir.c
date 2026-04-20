/* src/spu94/spu94_fir.c -- Phase 4 Plan 02
 *
 * FIR stage function bodies. Folded-form production (D-01) + literal
 * 39-multiply audit reference (D-01 audit witness). D-02 int32
 * accumulator with no-overflow proof in the comment block below.
 * D-03 clamp-once default; D-04 cascade-clamp via compile-time
 * #ifdef SPU94_FIR_CASCADE_CLAMP (default undefined -- clamp-once
 * regime). D-05 overflow-magnitude tap on every stage output;
 * D-06 aggregate post-shift err-tap per 04-RESEARCH Pattern 1
 * reconciliation (aggregate interpretation is bit-faithful to D-03;
 * strict per-multiply would engage D-04 cascade-clamp -- documented).
 *
 * Delay-line convention: circular buffer. delay[idx] holds the OLDEST
 * slot. Push writes delay[idx] then advances idx = (idx + 1) % 39.
 * Read at logical position k (0 = newest, 38 = oldest) uses
 * delay[(idx + 38 - k) % 39]. Matches the semantic of the Python
 * shift-register reference in tests/python/derive_fir_reference.py
 * (bit-identity asserted by test_fir_bit_identity.c).
 *
 * Pitfall 4 (ADR-0005): production stage functions are called from
 * exactly ONE site -- spu94_fir_chain_step (Plan 03). Tests call
 * directly. spu94_fir_decimate_literal_reference is called from tests
 * only, never from production.
 */
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <stddef.h>

/* ========================================================================
 * Accumulator Width Proof (D-02) -- see 04-RESEARCH section Accumulator
 * Width Proof for the derivation.
 *
 * The decimator sums 39 products of (int16 coefficient * int16 input
 * sample) before a single arithmetic-right-shift by 15 and a final
 * sat_s16 to int16. Worst-case adversarial input (each sample matches
 * the sign of its coefficient to maximize the aligned sum) yields
 * accumulator magnitude 0x5CD30000 = 1,557,331,968. This fits in int32
 * (INT32_MAX = 0x7FFFFFFF = 2,147,483,647) with 2.79 dB / 0.46 bits of
 * headroom.
 *
 * The interpolator's phase-0 subfilter is more relaxed: worst-case
 * 0x3CD30000 = 1,020,461,056, leaving 6.46 dB / 1.07 bits of headroom.
 * The phase-1 subfilter is the center tap alone, trivially safe.
 *
 * int32 is sufficient for Phase 4 as scoped. If a future composition
 * (additional accumulation stages, cascading intermediate clamps,
 * Q30 coefficient promotion) would tighten the decimator margin below
 * zero bits, promote the accumulator type to int64 per the D-02 seam.
 * This requires no caller change: the accumulator is a local in the
 * FIR stage function, and sat_s16 accepts both widths via the existing
 * q15 primitives.
 *
 * Bounds derived analytically (sum |h[k]| * |INT16_MIN| for the
 * decimator) and validated empirically by tests/unit/fir/
 * test_fir_overflow_proof.c which drives the accumulator to the bound
 * 0x5CD2632E (the achievable-by-int16 ceiling, slightly less than the
 * analytic 0x5CD30000 which requires |x|=32768 on both sides) under
 * adversarial input and asserts both the specific bit-pattern of the
 * accumulator and the absence of any UBSan signed-overflow trap.
 *
 * Sum of |h[k]| for the 39-tap table: 47,526 = 0xB9A6.
 * INT16_MAX_MAGNITUDE: 32,768 = 0x8000 (abs(INT16_MIN)).
 * Product: 47,526 * 32,768 = 1,557,331,968. QED.
 * ========================================================================
 */

/* Read the logical tap k (0 = newest, 38 = oldest) from a 39-slot
 * circular buffer with `idx` pointing at the oldest slot (next-to-write). */
static inline int16_t fir_read_tap(const int16_t delay[39],
                                   uint8_t idx, unsigned k) {
    unsigned pos = ((unsigned)idx + 38u - k) % 39u;
    return delay[pos];
}

/* Push a new sample into the ring buffer. Advances idx. */
static inline void fir_push(int16_t delay[39], uint8_t *idx,
                            int16_t sample) {
    delay[*idx] = sample;
    *idx = (uint8_t)(((unsigned)(*idx) + 1u) % 39u);
}

/* =======================================================================
 * Folded-form FIR apply (decimator + interpolator phase-0 share this
 * math shape). D-03 clamp-once: single shift + sat_s16 at the end.
 * D-05 overflow-magnitude tap (unconditional add).
 * D-06 aggregate post-shift err-tap (research-reconciled interpretation).
 *
 * Pitfall 8 (order-of-summation): fixed order -- center tap first, then
 * k = 0..18 pairs in ascending order, skipping zero coefficients. The
 * Python reference uses this same order. Any reordering (e.g., compiler
 * auto-parallelization) would diverge the aggregate err-tap; the
 * int-assoc summation order is a documented production invariant.
 * =======================================================================
 */
static int16_t fir_folded_apply(const int16_t delay[39], uint8_t idx,
                                int32_t *err_acc_inout,
                                int32_t *overflow_acc_inout) {
    int32_t acc;

#ifdef SPU94_FIR_CASCADE_CLAMP
    /* D-04 cascade-clamp variant: saturate after every accumulation. The
     * int32 intermediate is collapsed to int16 after each add, which
     * BREAKS bit-identity with the literal reference (04-RESEARCH sec 8).
     * Test binary may build with this defined; production never does.
     *
     * Semantically: sat_s16((acc + coef*pair) >> 15) with intermediate
     * clamping after each multiplier-accumulate). Straightforward
     * transliteration of the D-04 "cascading clamp" intent.
     */
    {
        int32_t running = (int32_t)spu94_fir_coef[19] *
                          (int32_t)fir_read_tap(delay, idx, 19u);
        /* WR-01: perform the <<15 rescale in unsigned space to avoid
         * C99 §6.5.7p4 UB on signed left-shift of a negative value
         * (sat_s16 can return a negative int16). Two's-complement
         * semantics are preserved by the round-trip through uint32_t. */
        running = (int32_t)((uint32_t)(int32_t)sat_s16(running >> 15) << 15);
        for (int k = 0; k < 19; ++k) {
            int16_t c = spu94_fir_coef[k];
            if (c == 0) continue;
            int32_t pair = (int32_t)fir_read_tap(delay, idx, (unsigned)k)
                         + (int32_t)fir_read_tap(delay, idx,
                                                 (unsigned)(38 - k));
            running += (int32_t)c * pair;
            /* WR-01: see comment above — unsigned <<15 rescale to avoid
             * C99 §6.5.7p4 UB on signed left-shift of negative value. */
            running = (int32_t)((uint32_t)(int32_t)
                                sat_s16(running >> 15) << 15);
        }
        acc = running;
    }
#else
    /* D-03 clamp-once default: no intermediate saturation. Bit-identical
     * to the literal 39-multiply form (04-RESEARCH sec 8). */
    acc = (int32_t)spu94_fir_coef[19] *
          (int32_t)fir_read_tap(delay, idx, 19u);
    for (int k = 0; k < 19; ++k) {
        int16_t c = spu94_fir_coef[k];
        if (c == 0) continue;
        int32_t pair = (int32_t)fir_read_tap(delay, idx, (unsigned)k)
                     + (int32_t)fir_read_tap(delay, idx,
                                             (unsigned)(38 - k));
        acc += (int32_t)c * pair;
    }
#endif

    /* D-03 single shift + sat_s16. ADR-0001 arithmetic-right-shift
     * (truncation toward -inf for negative values). */
    int32_t shifted = acc >> 15;

    /* D-06 aggregate post-shift err-tap (04-RESEARCH Pattern 1
     * reconciliation -- aggregate interpretation is bit-faithful to
     * D-03 clamp-once. Strict per-multiply err would engage D-04
     * cascade-clamp which is a DIFFERENT regime with DIFFERENT output). */
    int32_t err_aggregate = acc - ((int32_t)shifted << 15);
    *err_acc_inout += err_aggregate;

    /* D-05 overflow-magnitude tap: unconditional add, zero when
     * not saturating. No branches on the hot path (the if/else-if
     * selects the magnitude value; we always add). */
    int32_t mag = 0;
    if (shifted > INT16_MAX)        mag = shifted - INT16_MAX;
    else if (shifted < INT16_MIN)   mag = INT16_MIN - shifted;
    *overflow_acc_inout += mag;

    return sat_s16(shifted);
}

/* =======================================================================
 * Decimator (CORE-06). Pushes one 44.1 kHz stereo sample; produces one
 * 22.05 kHz stereo sample on retained phase (output_valid=1), discards
 * on non-retained phase (output_valid=0).
 * =======================================================================
 */
void spu94_fir_decimate(spu94_state *state,
                        int16_t input_sample_l, int16_t input_sample_r,
                        int16_t *output_l, int16_t *output_r,
                        int *output_valid) {
    if (state == (spu94_state *)0) {
        if (output_valid) *output_valid = 0;
        return;
    }
    fir_push(state->fir_delay_l_in, &state->fir_idx_l_in, input_sample_l);
    fir_push(state->fir_delay_r_in, &state->fir_idx_r_in, input_sample_r);

    if (state->fir_decimate_phase != 0) {
        /* Discarded phase: advance phase, no output computed. */
        state->fir_decimate_phase = 0;
        if (output_valid) *output_valid = 0;
        return;
    }
    /* Retained phase: compute output for L and R. */
    state->fir_decimate_phase = 1;

    int16_t out_l = fir_folded_apply(state->fir_delay_l_in,
                                     state->fir_idx_l_in,
                                     &state->err_fir_decimator,
                                     &state->fir_overflow_decimator);
    int16_t out_r = fir_folded_apply(state->fir_delay_r_in,
                                     state->fir_idx_r_in,
                                     &state->err_fir_decimator,
                                     &state->fir_overflow_decimator);
    if (output_l) *output_l = out_l;
    if (output_r) *output_r = out_r;
    if (output_valid) *output_valid = 1;
}

/* =======================================================================
 * Interpolator phase-0 subfilter apply (folded-form over even-offset
 * non-zero coefficients). Shares the D-05 + D-06 tap discipline.
 * Same summation-order invariant as fir_folded_apply.
 * =======================================================================
 */
static int16_t fir_interp_phase0_apply(const int16_t delay[39], uint8_t idx,
                                       int32_t *err_acc_inout,
                                       int32_t *overflow_acc_inout) {
    /* Phase 0: non-zero coefficients at even k (k=0,2,4,...,18,20,22,...,38).
     * Fold via symmetry coef[k] == coef[38-k]. Pair (18, 20) first (the
     * pair spanning the center); then descending k order for remaining
     * pairs (keeps summation order monotonic and documented). */
    int32_t acc = (int32_t)spu94_fir_coef[18] *
                  ((int32_t)fir_read_tap(delay, idx, 18u)
                 + (int32_t)fir_read_tap(delay, idx, 20u));
    /* Remaining pairs (k=0,2,4,6,8,10,12,14,16) with symmetric partner
     * (38,36,34,32,30,28,26,24,22). */
    static const unsigned even_pairs[][2] = {
        {0u, 38u}, {2u, 36u}, {4u, 34u}, {6u, 32u}, {8u, 30u},
        {10u, 28u}, {12u, 26u}, {14u, 24u}, {16u, 22u},
    };
    for (size_t i = 0; i < sizeof(even_pairs)/sizeof(even_pairs[0]); ++i) {
        int16_t c = spu94_fir_coef[even_pairs[i][0]];
        if (c == 0) continue;
        int32_t pair = (int32_t)fir_read_tap(delay, idx, even_pairs[i][0])
                     + (int32_t)fir_read_tap(delay, idx, even_pairs[i][1]);
        acc += (int32_t)c * pair;
    }

    int32_t shifted = acc >> 15;
    int32_t err_aggregate = acc - ((int32_t)shifted << 15);
    *err_acc_inout += err_aggregate;
    int32_t mag = 0;
    if (shifted > INT16_MAX)        mag = shifted - INT16_MAX;
    else if (shifted < INT16_MIN)   mag = INT16_MIN - shifted;
    *overflow_acc_inout += mag;
    return sat_s16(shifted);
}

/* =======================================================================
 * Interpolator phase-1 subfilter apply (center tap only for half-band
 * Type I -- all other odd-offset coefficients are zero by construction;
 * test_fir_coef_table in Plan 01 asserts).
 * =======================================================================
 */
static int16_t fir_interp_phase1_apply(const int16_t delay[39], uint8_t idx,
                                       int32_t *err_acc_inout,
                                       int32_t *overflow_acc_inout) {
    int32_t acc = (int32_t)spu94_fir_coef[19] *
                  (int32_t)fir_read_tap(delay, idx, 19u);
    int32_t shifted = acc >> 15;
    int32_t err_aggregate = acc - ((int32_t)shifted << 15);
    *err_acc_inout += err_aggregate;
    int32_t mag = 0;
    if (shifted > INT16_MAX)        mag = shifted - INT16_MAX;
    else if (shifted < INT16_MIN)   mag = INT16_MIN - shifted;
    *overflow_acc_inout += mag;
    return sat_s16(shifted);
}

/* =======================================================================
 * Interpolator (CORE-07). One 22.05 kHz input pair -> TWO 44.1 kHz output
 * pairs (phase 0 + phase 1). Same 39-tap table as decimator (D-08 + 04-
 * RESEARCH Fact 2 -- jsgroth "rather strange" observation). DC gain per
 * 04-RESEARCH section 5: phase-0 ~= 0.47655, phase-1 = 0.5, averaged
 * ~= 0.488 per 44.1 kHz output sample. Not compensated (bit-faithful --
 * if PS1 doesn't compensate, we don't).
 * =======================================================================
 */
void spu94_fir_interpolate(spu94_state *state,
                           int16_t input_sample_l, int16_t input_sample_r,
                           int16_t *output_l_phase0, int16_t *output_r_phase0,
                           int16_t *output_l_phase1, int16_t *output_r_phase1) {
    if (state == (spu94_state *)0) return;
    fir_push(state->fir_delay_l_out, &state->fir_idx_l_out, input_sample_l);
    fir_push(state->fir_delay_r_out, &state->fir_idx_r_out, input_sample_r);

    int16_t p0_l = fir_interp_phase0_apply(state->fir_delay_l_out,
                                           state->fir_idx_l_out,
                                           &state->err_fir_interpolator,
                                           &state->fir_overflow_interpolator);
    int16_t p0_r = fir_interp_phase0_apply(state->fir_delay_r_out,
                                           state->fir_idx_r_out,
                                           &state->err_fir_interpolator,
                                           &state->fir_overflow_interpolator);
    int16_t p1_l = fir_interp_phase1_apply(state->fir_delay_l_out,
                                           state->fir_idx_l_out,
                                           &state->err_fir_interpolator,
                                           &state->fir_overflow_interpolator);
    int16_t p1_r = fir_interp_phase1_apply(state->fir_delay_r_out,
                                           state->fir_idx_r_out,
                                           &state->err_fir_interpolator,
                                           &state->fir_overflow_interpolator);
    if (output_l_phase0) *output_l_phase0 = p0_l;
    if (output_r_phase0) *output_r_phase0 = p0_r;
    if (output_l_phase1) *output_l_phase1 = p1_l;
    if (output_r_phase1) *output_r_phase1 = p1_r;
}

/* =======================================================================
 * D-01 literal 39-multiply audit reference. TEST-VISIBLE; never on
 * production hot path. Proves bit-identity of the folded form under
 * D-03 clamp-once via test_fir_bit_identity.c.
 *
 * history[0] = newest sample, history[38] = oldest.
 * =======================================================================
 */
void spu94_fir_decimate_literal_reference(const int16_t history[39],
                                          int16_t *output,
                                          int32_t *acc_out,
                                          int32_t *err_out) {
    int32_t acc = 0;
    for (int k = 0; k < 39; ++k) {
        acc += (int32_t)spu94_fir_coef[k] * (int32_t)history[k];
    }
    int32_t shifted = acc >> 15;
    int32_t err = acc - ((int32_t)shifted << 15);
    if (output)  *output  = sat_s16(shifted);
    if (acc_out) *acc_out = acc;
    if (err_out) *err_out = err;
}

/* =======================================================================
 * D-01 folded-form audit companion. history[0]=newest, history[38]=oldest.
 * Same math as fir_folded_apply (static helper) but reads from history[]
 * directly. Used only by test_fir_bit_identity.c to prove folded ==
 * literal under D-03 clamp-once. Never on production hot path.
 * Summation order matches fir_folded_apply exactly (Pitfall 8 -- fixed).
 * =======================================================================
 */
void spu94_fir_folded_reference(const int16_t history[39],
                                int16_t *output,
                                int32_t *acc_out,
                                int32_t *err_out) {
    int32_t acc = (int32_t)spu94_fir_coef[19] * (int32_t)history[19];
    for (int k = 0; k < 19; ++k) {
        int16_t c = spu94_fir_coef[k];
        if (c == 0) continue;
        int32_t pair = (int32_t)history[k] + (int32_t)history[38 - k];
        acc += (int32_t)c * pair;
    }
    int32_t shifted = acc >> 15;
    int32_t err = acc - ((int32_t)shifted << 15);
    if (output)  *output  = sat_s16(shifted);
    if (acc_out) *acc_out = acc;
    if (err_out) *err_out = err;
}
