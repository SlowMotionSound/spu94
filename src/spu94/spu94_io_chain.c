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
         * ADR-Phase-6-I (input wiring) + ADR-Phase-6-G (output wiring):
         * on the production path (reverb_active=1), the reverb sees the
         * 22.05 kHz band-limited decimator output as its INPUT, and the
         * reverb's 22.05 kHz wet OUTPUT feeds the interpolator. Both
         * writes happen here, inside this retained-phase branch:
         *
         *   state->mix_bus_l/r = dec_l/dec_r          (ADR-Phase-6-I)
         *   -> spu94_tick(state) -> spu94_reverb_body reads mix_bus_l/r
         *   -> state->reverb_out_l/r = wet            (ADR-Phase-6-G)
         *   -> spu94_fir_interpolate consumes reverb_out_l/r as src_l/r
         *
         * vLOUT/vROUT = 0 (Off preset, or a bare-bones override) gates
         * reverb_out_l/r to 0 here, which propagates through the
         * interpolator as silence on the 44.1 kHz output stream. The
         * non-Off factory presets now carry vLOUT = vROUT = 0x7FFF per
         * ADR-Phase-6-H so rendered audio is audible by default.
         *
         * On the test-only reverb-bypass path (reverb_active=0) we skip
         * spu94_tick entirely AND route dec_l/dec_r straight into the
         * interpolator -- mix_bus_l/r are NOT written because no tick
         * will consume them. This preserves the "pure half-band round-
         * trip" contract that FIR unit tests depend on
         * (tests/unit/fir/test_fir_impulse.c, test_fir_chain_latency.c,
         * test_fir_dc.c, test_fir_round_trip_transparency.c,
         * test_fir_err_overflow_taps.c). The bypass path is NEVER
         * reachable from production code -- spu94_process only calls
         * spu94_fir_chain_step, which passes reverb_active=1. */
        int16_t src_l, src_r;
        if (reverb_active) {
            /* ADR-Phase-6-I: feed the decimator's band-limited 22.05 kHz
             * sample into the reverb, not the raw 44.1 kHz input. */
            state->mix_bus_l = dec_l;
            state->mix_bus_r = dec_r;
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
