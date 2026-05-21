/* src/spu94/spu94_process.c -- Phase 7: send/return mixer architecture.
 *
 * spu94_process: 44.1 kHz int16 stereo block-based audio entry point
 *   (D-01). Planar L/R pointers (D-01); any block size N >= 1 (D-03);
 *   in-place L_out == L_in / R_out == R_in allowed (D-04).
 *
 * spu94_flush: drain trailing reverb tail by feeding internal silence
 *   (D-02). Shares the single-sample body with spu94_process.
 *
 * Signal flow (Phase 7, D-01 through D-12):
 *   1. Input gain: scale input by Q15 fader
 *   2. ADPCM coloration -> ADPCM bus (separable block, D-03)
 *   3. Dry bus with latency compensation (D-07, D-08)
 *   4. Reverb sends: weighted sum of dry + ADPCM sends
 *   5. Reverb: unchanged chain_step internals (decimate -> tick -> interp)
 *   6. Master mixer: three-bus sum (dry/ADPCM/reverb) with sat_s16
 *   7. DAC section: FIR + noise on master output (D-09 through D-12)
 *
 * Mix-bus wiring (ADR-Phase-5-B + ADR-Phase-6-I): this function does NOT
 * write state->mix_bus_l/r. chain_step_impl owns that write on the
 * retained phase. Raw 44.1 kHz samples are NEVER the reverb's input.
 *
 * Pitfall 4 (ADR-0005): spu94_fir_chain_step has exactly TWO call
 * sites in the public audio path -- spu94_process (non-zero inputs)
 * and spu94_flush (zero inputs). Both paths run the same per-sample math.
 */
#include <spu94/spu94.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_gauss.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include <spu94/spu94_voice.h>
#include <spu94/spu94_spu_ram.h>
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* -----------------------------------------------------------------------
 * v1.8 Voice Engine — Phase 30: Full 24-voice mixer (MIX-01 through MIX-06).
 *
 * Replaces the Phase 27 scaffolding with spu94_voice_mixer_t.
 *
 * Pitfall prevention:
 *   C6: voice_ram inside s_mixer is distinct from state->work_buf (reverb).
 *   C8: pending_kon/pending_koff applied at tick start, not mid-tick.
 *   S1: int32 accumulation, sat_s16 only at output.
 *   MIX-06: voice dry + ADPCM coloration coexist (summed into ADPCM slot).
 *   VOICE-06: 24 isolated spu94_voice_t structs with per-voice gauss_ring.
 * ----------------------------------------------------------------------- */
static spu94_voice_mixer_t s_mixer;
static uint8_t             s_mixer_init = 0;

/* Phase 31 accessor: expose the file-scope mixer for external use. */
spu94_voice_mixer_t *spu94_get_voice_mixer(void) {
    if (!s_mixer_init) {
        spu94_voice_mixer_init(&s_mixer);
        s_mixer_init = 1;
    }
    return &s_mixer;
}

void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        /* spu94_slew_tick moved to spu94_tick (22.05 kHz) so register
         * slew steps align 1:1 with reverb body executions. Running here
         * at 44.1 kHz produced 2-step register jumps per reverb tick. */

        int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;

        /* 1. Input gain (D-04, D-05) */
        l = q15_mul_truncate(l, state->input_gain);
        r = q15_mul_truncate(r, state->input_gain);

        /* ADPCM bus samples — declared before voice engine block so both
         * the voice engine injection AND the ADPCM coloration path can write
         * them. Default = passthrough (input signal). */
        int16_t adpcm_l = l, adpcm_r = r;

        /* v1.8 Voice Engine — Phase 30: full 24-voice mixer.
         * MIX-05: voice_dry_l/r goes into ADPCM slot (dry DAC path).
         * MIX-05: voice_rev_l/r feeds into send_l/r before spu94_fir_chain_step.
         * MIX-06: voice output and ADPCM coloration coexist — both summed. */
        int16_t voice_dry_l = 0, voice_dry_r = 0;
        int16_t voice_rev_l = 0, voice_rev_r = 0;

        if (!s_mixer_init) {
            spu94_voice_mixer_init(&s_mixer);
            s_mixer_init = 1;
        }

        if (s_mixer.enabled) {
            spu94_voice_mixer_tick(&s_mixer,
                &voice_dry_l, &voice_dry_r,
                &voice_rev_l, &voice_rev_r);
        }

        /* 2. ADPCM voice path (PS1-faithful single-counter architecture):
         *    One pitch counter drives both sample decimation and Gaussian
         *    interpolation — bits 12+ = sample advancement, bits 4-11 =
         *    Gaussian table index. Matches real SPU voice hardware. */
        if (state->adpcm_enabled) {
            const uint16_t pitch = state->voice_pitch ? state->voice_pitch : 0x1000;

            /* AA filter runs every tick unconditionally */
            if (state->aa_filter_enabled) {
                int32_t alpha = pitch;  /* Q12: 0x1000 = 1.0 */
                state->decim_prev_l = (int16_t)(
                    ((int32_t)l * alpha +
                     (int32_t)state->decim_prev_l * (0x1000 - alpha)) >> 12);
                state->decim_prev_r = (int16_t)(
                    ((int32_t)r * alpha +
                     (int32_t)state->decim_prev_r * (0x1000 - alpha)) >> 12);
            } else {
                state->decim_prev_l = l;
                state->decim_prev_r = r;
            }

            /* Advance single pitch counter */
            uint16_t old_counter = state->voice_counter;
            state->voice_counter += pitch;
            uint16_t samples_consumed =
                (state->voice_counter >> 12) - (old_counter >> 12);
            if (state->voice_counter < old_counter)
                samples_consumed = 1;  /* 16-bit wrap */
            state->voice_counter &= 0x0FFF;  /* keep fractional part only */

            /* For each sample consumed: feed ADPCM + advance Gaussian ring */
            for (uint16_t s = 0; s < samples_consumed; s++) {
                /* Push input to ADPCM encode buffer */
                state->adpcm_in_buf_l[state->adpcm_buf_pos] = state->decim_prev_l;
                state->adpcm_in_buf_r[state->adpcm_buf_pos] = state->decim_prev_r;
                state->adpcm_buf_pos++;

                if (state->adpcm_buf_pos == SPU94_ADPCM_BLOCK_SAMPLES) {
                    uint8_t block[SPU94_ADPCM_BLOCK_BYTES];

                    spu94_adpcm_encode_block(&state->adpcm_state_l,
                        state->adpcm_in_buf_l, 0, block);
                    spu94_adpcm_decode_block(&state->adpcm_state_l,
                        block, state->adpcm_out_buf_l);

                    spu94_adpcm_encode_block(&state->adpcm_state_r,
                        state->adpcm_in_buf_r, 0, block);
                    spu94_adpcm_decode_block(&state->adpcm_state_r,
                        block, state->adpcm_out_buf_r);

                    state->adpcm_buf_pos = 0;
                    state->gauss_out_pos = 0;
                }

                /* Push decoded sample into Gaussian ring */
                uint8_t wp = state->gauss_ring_pos;
                state->gauss_ring_l[wp] = state->adpcm_out_buf_l[state->gauss_out_pos];
                state->gauss_ring_r[wp] = state->adpcm_out_buf_r[state->gauss_out_pos];
                state->gauss_ring_pos = (uint8_t)((wp + 1u) & 3u);
                state->gauss_out_pos++;
                if (state->gauss_out_pos >= SPU94_ADPCM_BLOCK_SAMPLES)
                    state->gauss_out_pos = 0;
            }

            /* Gaussian interpolation: index from fractional part of same counter */
            if (state->gauss_enabled) {
                const uint8_t gi = (uint8_t)((state->voice_counter >> 4) & 0xFF);
                const uint8_t wp = state->gauss_ring_pos;
                const int16_t s0 = state->gauss_ring_l[(wp + 0) & 3];
                const int16_t s1 = state->gauss_ring_l[(wp + 1) & 3];
                const int16_t s2 = state->gauss_ring_l[(wp + 2) & 3];
                const int16_t s3 = state->gauss_ring_l[(wp + 3) & 3];
                int32_t gl = (int32_t)spu94_gauss_table[0x0FF - gi] * (int32_t)s0
                           + (int32_t)spu94_gauss_table[0x1FF - gi] * (int32_t)s1
                           + (int32_t)spu94_gauss_table[0x100 + gi] * (int32_t)s2
                           + (int32_t)spu94_gauss_table[0x000 + gi] * (int32_t)s3;
                adpcm_l = (int16_t)(gl >> 15);

                const int16_t r0 = state->gauss_ring_r[(wp + 0) & 3];
                const int16_t r1 = state->gauss_ring_r[(wp + 1) & 3];
                const int16_t r2 = state->gauss_ring_r[(wp + 2) & 3];
                const int16_t r3 = state->gauss_ring_r[(wp + 3) & 3];
                int32_t gr = (int32_t)spu94_gauss_table[0x0FF - gi] * (int32_t)r0
                           + (int32_t)spu94_gauss_table[0x1FF - gi] * (int32_t)r1
                           + (int32_t)spu94_gauss_table[0x100 + gi] * (int32_t)r2
                           + (int32_t)spu94_gauss_table[0x000 + gi] * (int32_t)r3;
                adpcm_r = (int16_t)(gr >> 15);
            } else {
                /* Gauss off: output last decoded sample (zero-order hold) */
                uint8_t prev = (state->gauss_ring_pos + 3u) & 3u;
                adpcm_l = state->gauss_ring_l[prev];
                adpcm_r = state->gauss_ring_r[prev];
            }
        }
        /* When adpcm_enabled=0: adpcm_l/r remain as passthrough (input signal),
         * which is the correct behavior — no coloration applied. */

        /* Sampler drive: Q12 gain (0x1000 = unity) with sat_s16 clipping. */
        if (state->sampler_drive != 0x1000) {
            voice_dry_l = sat_s16(((int32_t)voice_dry_l * state->sampler_drive) >> 12);
            voice_dry_r = sat_s16(((int32_t)voice_dry_r * state->sampler_drive) >> 12);
            voice_rev_l = sat_s16(((int32_t)voice_rev_l * state->sampler_drive) >> 12);
            voice_rev_r = sat_s16(((int32_t)voice_rev_r * state->sampler_drive) >> 12);
        }

        /* MIX-06: Voice engine has its own bus (sampler_fader/sampler_send),
         * independent from the ADPCM coloration path (adpcm_fader/adpcm_send).
         * voice_dry_l/r feeds sampler_fader at master mix.
         * voice_rev_l/r feeds sampler_send at reverb input. */

        /* 3. Dry bus with latency compensation (D-07, D-08) */
        int16_t dry_l = l, dry_r = r;
        if (state->latency_comp && state->adpcm_enabled) {
            int16_t delayed_l = state->delay_buf_l[state->delay_pos];
            int16_t delayed_r = state->delay_buf_r[state->delay_pos];
            state->delay_buf_l[state->delay_pos] = l;
            state->delay_buf_r[state->delay_pos] = r;
            if (++state->delay_pos >= 28) state->delay_pos = 0;
            dry_l = delayed_l;
            dry_r = delayed_r;
        }

        /* 4. Reverb sends: dry + ADPCM + sampler, each with own send level */
        int16_t send_l = sat_s16((int32_t)q15_mul_truncate(dry_l,       state->dry_send)
                               + (int32_t)q15_mul_truncate(adpcm_l,    state->adpcm_send)
                               + (int32_t)q15_mul_truncate(voice_rev_l, state->sampler_send));
        int16_t send_r = sat_s16((int32_t)q15_mul_truncate(dry_r,       state->dry_send)
                               + (int32_t)q15_mul_truncate(adpcm_r,    state->adpcm_send)
                               + (int32_t)q15_mul_truncate(voice_rev_r, state->sampler_send));

        /* 5. Reverb: unchanged chain internals; only the input changes */
        int16_t rev_l = 0, rev_r = 0;
        spu94_fir_chain_step(state, send_l, send_r, &rev_l, &rev_r);

        /* 5b. Stage B latency comp: when latency_comp is on, delay the dry
         * and ADPCM buses by SPU94_LATENCY_SAMPLES (58) at this point so
         * they enter the master mixer time-aligned with the FIR-delayed
         * reverb tail. Without this stage, dry+reverb mixes smear by 58
         * samples on transients, and a Dry=1/Reverb=0 passthrough produces
         * output 58 samples ahead of host-PDC-compensated parallel tracks
         * (PLUG-15 null test cannot null without this).
         *
         * When latency_comp is off, this stage is a no-op -- the dry and
         * ADPCM contributions reach the master mix without extra delay,
         * preserving the historically authentic PS1 SPU behavior (dry
         * leads reverb by the FIR group delay; intentional creative
         * smearing). */
        int16_t mix_dry_l = dry_l,    mix_dry_r = dry_r;
        int16_t mix_adpcm_l = adpcm_l, mix_adpcm_r = adpcm_r;
        if (state->latency_comp) {
            const uint8_t pos = state->fir_lc_pos;
            mix_dry_l = state->fir_lc_dry_buf_l[pos];
            mix_dry_r = state->fir_lc_dry_buf_r[pos];
            mix_adpcm_l = state->fir_lc_adpcm_buf_l[pos];
            mix_adpcm_r = state->fir_lc_adpcm_buf_r[pos];
            state->fir_lc_dry_buf_l[pos] = dry_l;
            state->fir_lc_dry_buf_r[pos] = dry_r;
            state->fir_lc_adpcm_buf_l[pos] = adpcm_l;
            state->fir_lc_adpcm_buf_r[pos] = adpcm_r;
            state->fir_lc_pos = (uint8_t)((pos + 1u) % 58u);
        }

/* 6. Master mixer: four-bus sum, int32 accumulation + sat_s16 (D-01) */
        int16_t out_l = sat_s16(
            (int32_t)q15_mul_truncate(mix_dry_l,    state->dry_fader)
          + (int32_t)q15_mul_truncate(mix_adpcm_l,    state->adpcm_fader)
          + (int32_t)q15_mul_truncate(voice_dry_l,  state->sampler_fader)
          + (int32_t)q15_mul_truncate(rev_l,        state->reverb_fader));
        int16_t out_r = sat_s16(
            (int32_t)q15_mul_truncate(mix_dry_r,    state->dry_fader)
          + (int32_t)q15_mul_truncate(mix_adpcm_r,    state->adpcm_fader)
          + (int32_t)q15_mul_truncate(voice_dry_r,  state->sampler_fader)
          + (int32_t)q15_mul_truncate(rev_r,        state->reverb_fader));

        /* 7. DAC section: mode-selectable v1.2/v1.3 processing (Phase 11) */
        if (state->dac_enabled) {
            if (state->dac_true_oversample) {
                /* v1.3: true 8x oversampling with noise at 352.8kHz (D-03) */
                if (state->dac_fir_enabled && state->dac_noise_enabled) {
                    out_l = spu94_dac_fir_step_8x_with_noise(
                        &state->dac_fir_l, &state->dac_noise_l, out_l);
                    out_r = spu94_dac_fir_step_8x_with_noise(
                        &state->dac_fir_r, &state->dac_noise_r, out_r);
                } else if (state->dac_fir_enabled) {
                    out_l = spu94_dac_fir_step_8x(&state->dac_fir_l, out_l);
                    out_r = spu94_dac_fir_step_8x(&state->dac_fir_r, out_r);
                } else if (state->dac_noise_enabled) {
                    out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                    out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
                }
            } else {
                /* v1.2: approximate single-rate (D-02 fallback path) */
                if (state->dac_fir_enabled) {
                    out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
                    out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
                }
                if (state->dac_noise_enabled) {
                    out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                    out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
                }
            }
        }

        if (L_out != NULL) L_out[i] = out_l;
        if (R_out != NULL) R_out[i] = out_r;
    }
}

void spu94_flush(spu94_state *state,
                 int16_t *L_out, int16_t *R_out,
                 uint32_t num_samples) {
    /* D-02: silent-input drain. Delegates to spu94_process with NULL
     * L_in/R_in -- the process body substitutes zero for each channel.
     * Identical math path (Pitfall 4 single-body discipline). */
    spu94_process(state, NULL, NULL, L_out, R_out, num_samples);
}
