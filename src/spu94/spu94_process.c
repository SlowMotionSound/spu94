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
 *   2. ADPCM coloration -> patina bus (separable block, D-03)
 *   3. Dry bus with latency compensation (D-07, D-08)
 *   4. Reverb sends: weighted sum of dry + patina sends
 *   5. Reverb: unchanged chain_step internals (decimate -> tick -> interp)
 *   6. Master mixer: three-bus sum (dry/patina/reverb) with sat_s16
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
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"
#include <stdint.h>
#include <stddef.h>

void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;

        /* 1. Input gain (D-04, D-05) */
        l = q15_mul_truncate(l, state->input_gain);
        r = q15_mul_truncate(r, state->input_gain);

        /* 2. ADPCM coloration -> patina bus (D-03: separable block) */
        int16_t patina_l, patina_r;
        if (state->adpcm_enabled) {
            int16_t out_l = state->adpcm_out_buf_l[state->adpcm_buf_pos];
            int16_t out_r = state->adpcm_out_buf_r[state->adpcm_buf_pos];

            state->adpcm_in_buf_l[state->adpcm_buf_pos] = l;
            state->adpcm_in_buf_r[state->adpcm_buf_pos] = r;

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
            }

            patina_l = out_l;
            patina_r = out_r;
        } else {
            patina_l = l;
            patina_r = r;
        }

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

        /* 4. Reverb sends: sum of dry and patina sends (D-01) */
        int16_t send_l = sat_s16((int32_t)q15_mul_truncate(dry_l,    state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_l, state->patina_send));
        int16_t send_r = sat_s16((int32_t)q15_mul_truncate(dry_r,    state->dry_send)
                               + (int32_t)q15_mul_truncate(patina_r, state->patina_send));

        /* 5. Reverb: unchanged chain internals; only the input changes */
        int16_t rev_l = 0, rev_r = 0;
        spu94_fir_chain_step(state, send_l, send_r, &rev_l, &rev_r);

        /* 6. Master mixer: three-bus sum, int32 accumulation + sat_s16 (D-01) */
        int16_t out_l = sat_s16(
            (int32_t)q15_mul_truncate(dry_l,    state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_l, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_l,    state->reverb_fader));
        int16_t out_r = sat_s16(
            (int32_t)q15_mul_truncate(dry_r,    state->dry_fader)
          + (int32_t)q15_mul_truncate(patina_r, state->patina_fader)
          + (int32_t)q15_mul_truncate(rev_r,    state->reverb_fader));

        /* 7. DAC section (D-09 through D-12): master output only */
        if (state->dac_enabled) {
            if (state->dac_fir_enabled) {
                out_l = spu94_dac_fir_step(&state->dac_fir_l, out_l);
                out_r = spu94_dac_fir_step(&state->dac_fir_r, out_r);
            }
            if (state->dac_noise_enabled) {
                out_l = q15_add_sat(out_l, spu94_dac_noise_step(&state->dac_noise_l));
                out_r = q15_add_sat(out_r, spu94_dac_noise_step(&state->dac_noise_r));
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
