/* src/spu94/spu94_voice.c
 * Phase 27: Per-voice tick implementation for the v1.8 PSX Voice Engine.
 *
 * Processing order (S5 — decode before interpolate, then advance counter):
 *   1. Decode ADPCM block if needed (has_block == 0)
 *   2. Gaussian interpolation from gauss_ring
 *   3. Apply per-voice volume (Q15 multiply)
 *   4. Advance pitch counter and push samples into ring
 *
 * RT-safety: no malloc, no locks, no syscalls, no fopen/printf.
 * Verified by nm -u on the object file (Phase 5 CI gate pattern).
 *
 * Pitfall prevention:
 *   C1: DECODE-ONLY from RAM — no encode call in this file.
 *   C2: gauss_ring[4] is per-voice — never references state->gauss_ring_l/r.
 *   C7: Pitch clamped to 0x3FFF at key_on and re-checked in tick.
 *   S5: Decode-before-interpolate ordering strictly followed.
 *   M6: Block address advances by 16 bytes (SPU94_ADPCM_BLOCK_BYTES).
 */

#include <spu94/spu94_voice.h>
#include <spu94/spu94_gauss.h>
#include <spu94/spu94_q15.h>
#include <spu94/spu94_adpcm.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void spu94_voice_init(spu94_voice_t *v) {
    if (v == NULL) return;
    memset(v, 0, sizeof(*v));
}

void spu94_voice_key_on(spu94_voice_t *v, uint32_t start_addr,
                        uint16_t pitch, int16_t vol_l, int16_t vol_r) {
    if (v == NULL) return;

    /* C7: pitch 0 treated as 0x1000 (same as coloration path convention) */
    if (pitch == 0) pitch = 0x1000;
    /* C7: mandatory clamp to hardware maximum */
    if (pitch > 0x3FFF) pitch = 0x3FFF;

    v->pitch = pitch;
    v->pitch_counter = 0;
    v->current_addr = start_addr;
    v->sample_start_addr = start_addr;
    v->vol_l = vol_l;
    v->vol_r = vol_r;

    /* Reset decoder state */
    v->adpcm_state.old = 0;
    v->adpcm_state.older = 0;

    /* Reset Gaussian ring (S3: zero on KON is authentic PS1 behavior) */
    v->gauss_ring[0] = 0;
    v->gauss_ring[1] = 0;
    v->gauss_ring[2] = 0;
    v->gauss_ring[3] = 0;
    v->gauss_ring_pos = 0;

    /* Reset decode buffer state */
    v->has_block = 0;
    v->decode_buf_pos = 0;

    v->active = 1;
}

void spu94_voice_key_off(spu94_voice_t *v) {
    if (v == NULL) return;
    /* Phase 28 will replace with ADSR release; for now, immediate silence. */
    v->active = 0;
}

void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      int16_t *out_l, int16_t *out_r) {
    if (v == NULL || out_l == NULL || out_r == NULL) return;

    if (v->active == 0) {
        *out_l = 0;
        *out_r = 0;
        return;
    }

    if (voice_ram == NULL || voice_ram_size == 0) {
        *out_l = 0;
        *out_r = 0;
        return;
    }

    /* ---------------------------------------------------------------
     * STEP 1 — Decode block if needed (S5: decode before interpolate)
     * --------------------------------------------------------------- */
    if (!v->has_block) {
        /* T-27-01: Bounds check — refuse to read past voice_ram */
        if (v->current_addr + SPU94_ADPCM_BLOCK_BYTES > voice_ram_size) {
            v->active = 0;
            *out_l = 0;
            *out_r = 0;
            return;
        }
        /* C1: decode-only from RAM, no encode call */
        spu94_adpcm_decode_block(&v->adpcm_state,
            voice_ram + v->current_addr, v->decode_buf);
        /* M6: advance address by 16 bytes (one ADPCM block) */
        v->current_addr += SPU94_ADPCM_BLOCK_BYTES;
        v->decode_buf_pos = 0;
        v->has_block = 1;
    }

    /* ---------------------------------------------------------------
     * STEP 2 — Gaussian interpolation
     * Exact formula from spu94_process.c lines 121-141.
     * --------------------------------------------------------------- */
    {
        const uint8_t gi = (uint8_t)((v->pitch_counter >> 4) & 0xFF);
        const uint8_t wp = v->gauss_ring_pos;
        const int16_t s0 = v->gauss_ring[(wp + 0) & 3];
        const int16_t s1 = v->gauss_ring[(wp + 1) & 3];
        const int16_t s2 = v->gauss_ring[(wp + 2) & 3];
        const int16_t s3 = v->gauss_ring[(wp + 3) & 3];
        int32_t interpolated =
              (int32_t)spu94_gauss_table[0x0FF - gi] * (int32_t)s0
            + (int32_t)spu94_gauss_table[0x1FF - gi] * (int32_t)s1
            + (int32_t)spu94_gauss_table[0x100 + gi] * (int32_t)s2
            + (int32_t)spu94_gauss_table[0x000 + gi] * (int32_t)s3;
        int16_t gauss_out = sat_s16(interpolated >> 15);

        /* ---------------------------------------------------------------
         * STEP 3 — Apply per-voice volume (VOICE-04)
         * --------------------------------------------------------------- */
        *out_l = q15_mul_truncate(gauss_out, v->vol_l);
        *out_r = q15_mul_truncate(gauss_out, v->vol_r);
    }

    /* ---------------------------------------------------------------
     * STEP 4 — Advance pitch counter and push samples into ring
     * --------------------------------------------------------------- */
    {
        uint16_t old_ctr = v->pitch_counter;
        /* C7: re-clamp pitch in tick (defensive; key_on already clamps) */
        uint16_t effective_pitch = (v->pitch > 0x3FFF) ? 0x3FFF : v->pitch;
        uint16_t new_ctr = (uint16_t)(old_ctr + effective_pitch);
        uint16_t samples_consumed = (uint16_t)((new_ctr >> 12) - (old_ctr >> 12));
        if (new_ctr < old_ctr) samples_consumed = 1;  /* 16-bit wrap guard */
        v->pitch_counter = new_ctr & 0x0FFF;

        for (uint16_t s = 0; s < samples_consumed; s++) {
            /* Push decoded sample into Gaussian ring */
            v->gauss_ring[v->gauss_ring_pos] = v->decode_buf[v->decode_buf_pos];
            v->gauss_ring_pos = (uint8_t)((v->gauss_ring_pos + 1u) & 3u);
            v->decode_buf_pos++;

            if (v->decode_buf_pos >= SPU94_ADPCM_BLOCK_SAMPLES) {
                /* Trigger decode of next block on next tick */
                v->has_block = 0;
                v->decode_buf_pos = 0;
                /* current_addr already advanced at decode time (M6) */

                /* If we still have samples to consume but no more block,
                 * break and let next tick decode. This prevents reading
                 * a block boundary mid-sample-consumption loop. */
                if (s + 1 < samples_consumed) {
                    break;
                }
            }
        }
    }
}
