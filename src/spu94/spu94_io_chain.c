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
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
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

/* ADPCM-INT-01: toggle coloration stage. */
void spu94_set_adpcm_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->adpcm_enabled) {
        /* ADPCM-INT-04: discard partial accumulation buffer on disable.
         * Zero output buffer so no stale audio leaks on re-enable. */
        state->adpcm_buf_pos = 0;
        for (int j = 0; j < SPU94_ADPCM_BLOCK_SAMPLES; j++) {
            state->adpcm_out_buf_l[j] = 0;
            state->adpcm_out_buf_r[j] = 0;
        }
        /* Reset codec state so re-enable starts clean */
        state->adpcm_state_l.old = 0;
        state->adpcm_state_l.older = 0;
        state->adpcm_state_r.old = 0;
        state->adpcm_state_r.older = 0;
    }
    state->adpcm_enabled = enabled ? 1 : 0;
}

int spu94_get_adpcm_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->adpcm_enabled;
}

void spu94_set_gauss_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->gauss_enabled) {
        for (int j = 0; j < 4; j++) {
            state->gauss_ring_l[j] = 0;
            state->gauss_ring_r[j] = 0;
        }
        state->gauss_ring_pos = 0;
        state->gauss_out_pos = 0;
        state->voice_counter = 0;
    }
    state->gauss_enabled = enabled ? 1 : 0;
}

int spu94_get_gauss_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->gauss_enabled;
}

void spu94_set_voice_pitch(spu94_state *state, uint16_t pitch) {
    if (state == NULL) return;
    if (pitch < 0x005C) pitch = 0x005C;
    if (pitch > 0x1000) pitch = 0x1000;
    state->voice_pitch = pitch;
}

uint16_t spu94_get_voice_pitch(const spu94_state *state) {
    if (state == NULL) return 0x1000;
    return state->voice_pitch ? state->voice_pitch : 0x1000;
}

void spu94_set_aa_filter_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    state->aa_filter_enabled = enabled ? 1 : 0;
}

int spu94_get_aa_filter_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->aa_filter_enabled;
}

/* DAC FIR group delay at 44.1kHz output rate (Phase 11 DSP-07).
 * v1.2: all stages at 44.1kHz = (55-1)/2 + (11-1)/2 + (7-1)/2 = 35
 * v1.3: stages at true rates = 27/2 + 5/4 + 3/8 = 15.125, rounded to 15 */
#define DAC_FIR_GROUP_DELAY_V12  35u
#define DAC_FIR_GROUP_DELAY_V13  15u

/* ADPCM-INT-03 + Phase 11 DSP-07: total latency including ADPCM block
 * delay and mode-aware DAC FIR group delay. */
uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    uint32_t lat = SPU94_LATENCY_SAMPLES;
    if (state->adpcm_enabled)
        lat += SPU94_ADPCM_BLOCK_SAMPLES;
    if (state->dac_enabled && state->dac_fir_enabled)
        lat += state->dac_true_oversample
            ? DAC_FIR_GROUP_DELAY_V13
            : DAC_FIR_GROUP_DELAY_V12;
    return lat;
}

/* -----------------------------------------------------------------------
 * Send/return mixer faders (Phase 7, D-01 through D-06)
 *
 * Six Q15 fader/send values control the mixer architecture. All values
 * are Q15 int16 in range [0x0000, 0x7FFF]. No parameter smoothing --
 * values land immediately as raw register writes (D-06).
 * ----------------------------------------------------------------------- */

void spu94_set_input_gain(spu94_state *state, int16_t gain) {
    if (state == NULL) return;
    state->input_gain = gain;
}
int16_t spu94_get_input_gain(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->input_gain;
}

void spu94_set_dry_fader(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->dry_fader = level;
}
int16_t spu94_get_dry_fader(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dry_fader;
}

void spu94_set_patina_fader(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->patina_fader = level;
}
int16_t spu94_get_patina_fader(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->patina_fader;
}

void spu94_set_dry_send(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->dry_send = level;
}
int16_t spu94_get_dry_send(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dry_send;
}

void spu94_set_patina_send(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->patina_send = level;
}
int16_t spu94_get_patina_send(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->patina_send;
}

void spu94_set_reverb_fader(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->reverb_fader = level;
}
int16_t spu94_get_reverb_fader(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->reverb_fader;
}

void spu94_set_sampler_fader(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->sampler_fader = level;
}
int16_t spu94_get_sampler_fader(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->sampler_fader;
}

void spu94_set_sampler_send(spu94_state *state, int16_t level) {
    if (state == NULL) return;
    state->sampler_send = level;
}
int16_t spu94_get_sampler_send(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->sampler_send;
}

void spu94_set_sampler_drive(spu94_state *state, int32_t drive) {
    if (state == NULL) return;
    state->sampler_drive = drive;
}
int32_t spu94_get_sampler_drive(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->sampler_send;
}

/* -----------------------------------------------------------------------
 * Latency compensation (Phase 7, D-07, D-08)
 *
 * When ADPCM is enabled, it introduces a 28-sample block delay.
 * Latency compensation adds a matching 28-sample delay to the dry bus
 * so both arrive at the master mixer time-aligned.
 * Default: ON (set in spu94_init/reset after zero-fill).
 * OFF creates intentional comb filtering (creative effect).
 * ----------------------------------------------------------------------- */

void spu94_set_latency_comp(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->latency_comp) {
        /* Zero both delay stages and reset positions on disable: stale
         * samples in the ring buffers would otherwise leak out on the
         * next on->off->on cycle. */
        state->delay_pos = 0;
        for (int j = 0; j < 28; j++) {
            state->delay_buf_l[j] = 0;
            state->delay_buf_r[j] = 0;
        }
        state->fir_lc_pos = 0;
        for (int j = 0; j < 58; j++) {
            state->fir_lc_dry_buf_l[j] = 0;
            state->fir_lc_dry_buf_r[j] = 0;
            state->fir_lc_pat_buf_l[j] = 0;
            state->fir_lc_pat_buf_r[j] = 0;
        }
    }
    state->latency_comp = enabled ? 1 : 0;
}

int spu94_get_latency_comp(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->latency_comp;
}

/* -----------------------------------------------------------------------
 * DAC coloration section (Phase 7, D-09 through D-12)
 *
 * Master toggle + two independent sub-toggles.
 * When master is off, no DAC processing runs.
 * When master is on, FIR and noise each have independent sub-toggles.
 * All three on = faithful PS1 DAC behavior.
 * Default: all off.
 * ----------------------------------------------------------------------- */

void spu94_set_dac_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_enabled) {
        /* Reset both FIR channels on disable */
        spu94_dac_fir_init(&state->dac_fir_l);
        spu94_dac_fir_init(&state->dac_fir_r);
        /* Reset noise state -- must use init, not zero-fill (LFSR absorbing-state) */
        spu94_dac_noise_init(&state->dac_noise_l, 0xACE1u);
        spu94_dac_noise_init(&state->dac_noise_r, 0x1ECAu);
    }
    state->dac_enabled = enabled ? 1 : 0;
}

int spu94_get_dac_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_enabled;
}

void spu94_set_dac_fir_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_fir_enabled) {
        spu94_dac_fir_init(&state->dac_fir_l);
        spu94_dac_fir_init(&state->dac_fir_r);
    }
    state->dac_fir_enabled = enabled ? 1 : 0;
}

int spu94_get_dac_fir_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_fir_enabled;
}

void spu94_set_dac_noise_enabled(spu94_state *state, int enabled) {
    if (state == NULL) return;
    if (!enabled && state->dac_noise_enabled) {
        spu94_dac_noise_init(&state->dac_noise_l, 0xACE1u);
        spu94_dac_noise_init(&state->dac_noise_r, 0x1ECAu);
    }
    state->dac_noise_enabled = enabled ? 1 : 0;
}

int spu94_get_dac_noise_enabled(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_noise_enabled;
}

void spu94_set_dac_true_oversample(spu94_state *state, int enabled) {
    if (state == NULL) return;
    state->dac_true_oversample = enabled ? 1 : 0;
}

int spu94_get_dac_true_oversample(const spu94_state *state) {
    if (state == NULL) return 0;
    return state->dac_true_oversample;
}
