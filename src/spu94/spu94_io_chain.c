/* src/spu94/spu94_io_chain.c -- Phase 4 Plan 03
 *
 * Internal 44.1 kHz FIR chain wrapper (D-07) + test-only reverb-bypass
 * variant + public spu94_get_latency_samples accessor (D-09).
 *
 * Pitfall 4 (ADR-0005): spu94_fir_decimate and spu94_fir_interpolate are
 * each called from exactly ONE site -- chain_step_impl below. Phase 5's
 * public spu94_process composes the non-bypass chain in a block-based
 * loop; tests use the bypass variant to isolate the FIR chain from the
 * reverb network.
 *
 * Pitfall 7: the interpolator phase-0/phase-1 emission ordering is
 * tracked by state->fir_interpolate_phase (single source of truth).
 * state->fir_pending_l_phase1 / fir_pending_r_phase1 cache the phase-1
 * sample emitted on the next call.
 *
 * State flow per 44.1 kHz call:
 *   push input into decimator delay lines (advance fir_idx_*_in)
 *   if retained phase:
 *     dec_valid=1 from spu94_fir_decimate
 *     [optionally call spu94_tick() -- reverb network runs at 22.05 kHz]
 *     call spu94_fir_interpolate() -- produces phase-0 + phase-1
 *     emit phase-0 now; cache phase-1; set fir_interpolate_phase=1
 *   else (discarded phase):
 *     dec_valid=0; emit cached phase-1; set fir_interpolate_phase=0
 *
 * Phase-5 stitching note: spu94_tick runs the Phase-3 reverb body with
 * the current register state. The reverb body reads zero mix-bus inputs
 * in Phase 4 -- Phase 5 will wire vLIN/vRIN into the mix-bus path so the
 * FIR decimator outputs feed the reverb indirectly.
 */
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <stdint.h>

/* Forward decl: spu94_tick lives in spu94_tick.c. Phase 4 calls it as the
 * 22.05 kHz reverb step. Phase 5 will additionally plumb mix-bus inputs
 * through vLIN/vRIN register writes BEFORE this chain_step call, so the
 * reverb body sees the FIR decimator outputs indirectly. For Phase 4,
 * spu94_tick runs with whatever register state the caller has set -- the
 * reverb_body reads zero mix-bus inputs (Phase 3 Plan 01 placeholder). */
void spu94_tick(spu94_state *state);

static void chain_step_impl(spu94_state *state,
                            int16_t l_in, int16_t r_in,
                            int16_t *l_out, int16_t *r_out,
                            int reverb_active) {
    int16_t dec_l = 0, dec_r = 0;
    int dec_valid = 0;
    spu94_fir_decimate(state, l_in, r_in, &dec_l, &dec_r, &dec_valid);

    if (dec_valid) {
        /* Retained phase -- produce new 22.05 kHz sample, then interpolate
         * into a 44.1 kHz pair (phase-0 emitted now, phase-1 cached).
         *
         * ADR-Phase-6-G wet-only wiring: on the production path
         * (reverb_active=1), the 22.05 kHz sample fed to the
         * interpolator is the reverb body's WET output (LeftOutput /
         * RightOutput from spu94_reverb_output_scale), NOT the dry
         * decimator output. The dry decimator output (dec_l, dec_r)
         * feeds the reverb INPUT via state->mix_bus_l/r (populated by
         * spu94_process before each call -- see spu94_process.c:40-41,
         * spu94_reverb.c:579-580). vLOUT/vROUT = 0 (default post-load,
         * Off preset) gates the wet path to silence -- the CLI
         * explicitly sets vLOUT/vROUT to a user-mix default after
         * preset load so rendered audio is audible.
         *
         * On the test-only reverb-bypass path (reverb_active=0) we
         * skip spu94_tick entirely AND route dry dec_l/dec_r straight
         * into the interpolator. This preserves the pre-ADR-Phase-6-G
         * semantics of spu94_fir_chain_step_reverb_bypass as a
         * "pure half-band round-trip" test harness: the FIR DSP
         * tests (tests/unit/fir/test_fir_impulse.c,
         * test_fir_chain_latency.c, test_fir_dc.c,
         * test_fir_round_trip_transparency.c,
         * test_fir_err_overflow_taps.c) pin the bit-faithful FIR
         * contract by driving known inputs through the chain with
         * the reverb network cut out -- they must see the dry input
         * round-trip through decimator -> interpolator, not silence.
         * This bypass path is NEVER reachable from production code
         * (spu94_process only calls spu94_fir_chain_step, which
         * passes reverb_active=1). */
        int16_t src_l, src_r;
        if (reverb_active) {
            state->reverb_out_l = 0;
            state->reverb_out_r = 0;
            spu94_tick(state);
            src_l = state->reverb_out_l;
            src_r = state->reverb_out_r;
        } else {
            /* Test-only dry passthrough. */
            src_l = dec_l;
            src_r = dec_r;
        }
        int16_t p0_l = 0, p0_r = 0, p1_l = 0, p1_r = 0;
        spu94_fir_interpolate(state, src_l, src_r,
                              &p0_l, &p0_r, &p1_l, &p1_r);
        if (l_out) { *l_out = p0_l; }
        if (r_out) { *r_out = p0_r; }
        state->fir_pending_l_phase1 = p1_l;
        state->fir_pending_r_phase1 = p1_r;
        state->fir_interpolate_phase = 1u;
    } else {
        /* Discarded phase -- emit cached phase-1. */
        if (l_out) { *l_out = state->fir_pending_l_phase1; }
        if (r_out) { *r_out = state->fir_pending_r_phase1; }
        state->fir_interpolate_phase = 0u;
    }
}

void spu94_fir_chain_step(spu94_state *state,
                          int16_t l_in_44k1, int16_t r_in_44k1,
                          int16_t *l_out_44k1, int16_t *r_out_44k1) {
    if (state == (spu94_state *)0) {
        if (l_out_44k1) { *l_out_44k1 = 0; }
        if (r_out_44k1) { *r_out_44k1 = 0; }
        return;
    }
    chain_step_impl(state, l_in_44k1, r_in_44k1,
                    l_out_44k1, r_out_44k1, /*reverb_active=*/1);
}

void spu94_fir_chain_step_reverb_bypass(spu94_state *state,
                                        int16_t l_in_44k1, int16_t r_in_44k1,
                                        int16_t *l_out_44k1, int16_t *r_out_44k1) {
    if (state == (spu94_state *)0) {
        if (l_out_44k1) { *l_out_44k1 = 0; }
        if (r_out_44k1) { *r_out_44k1 = 0; }
        return;
    }
    chain_step_impl(state, l_in_44k1, r_in_44k1,
                    l_out_44k1, r_out_44k1, /*reverb_active=*/0);
}

/* D-09: total round-trip FIR group delay at 44.1 kHz reference rate.
 * Value from 04-RESEARCH section Latency. One-line definition -- LTO
 * makes consumer call sites emit a constant return (e.g., mov eax, 38; ret). */
uint32_t spu94_get_latency_samples(void) {
    return SPU94_LATENCY_SAMPLES;
}
