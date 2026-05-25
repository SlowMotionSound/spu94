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
#include <spu94/spu94_sweep.h>
#include <spu94/spu94_noise.h>
#include <spu94/spu94_gauss.h>
#include <spu94/spu94_q15.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

void spu94_voice_init(spu94_voice_t *v) {
    if (v == NULL) return;
    memset(v, 0, sizeof(*v));
    /* Phase 29: loop_addr=0, loop_adpcm_old=0, loop_adpcm_older=0, endx=0
     * are intentionally zero-init by memset (LOOP-02, LOOP-05). */
    spu94_adsr_init(&v->adsr);
    spu94_sweep_init(&v->sweep_l);
    spu94_sweep_init(&v->sweep_r);
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

    /* Phase 28: reset ADSR to attack (level=0, counter=0) */
    spu94_adsr_key_on(&v->adsr);

    /* SWEEP-07: KON deactivates sweep */
    v->sweep_l.active = 0;
    v->sweep_r.active = 0;

    /* M3: ENDX cleared on KON, not KOFF */
    v->endx = 0;

    v->active = 1;
}

void spu94_voice_key_off(spu94_voice_t *v) {
    if (v == NULL) return;
    /* Phase 28: enter ADSR release. Voice stays active until level reaches 0.
     * If ADSR is disabled, fall back to immediate silence (Phase 27 behavior). */
    if (v->adsr.enabled) {
        spu94_adsr_key_off(&v->adsr);
        /* voice remains active; ADSR_OFF transition in tick will clear active */
    } else {
        v->active = 0;
    }
}

void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t noise_level,
                      uint8_t non_enabled,
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
     * STEP 0 — Volume sweep (SWEEP-05: sweep IS the volume register)
     * Tick L/R sweep; if active, write back to vol_l/vol_r.
     * --------------------------------------------------------------- */
    if (v->sweep_l.active) {
        spu94_sweep_tick(&v->sweep_l);
        v->vol_l = v->sweep_l.level;
    }
    if (v->sweep_r.active) {
        spu94_sweep_tick(&v->sweep_r);
        v->vol_r = v->sweep_r.level;
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
        /* GUI-driven loop latch: snapshot ADPCM state BEFORE decode so that
         * restoring it later reproduces the block identically each iteration. */
        if (v->loop_enabled && v->current_addr == v->loop_addr) {
            v->loop_adpcm_old   = v->adpcm_state.old;
            v->loop_adpcm_older = v->adpcm_state.older;
        }

        /* LOOP-01: capture flag byte from block header byte 1 (C4) */
        uint8_t flag_byte = spu94_adpcm_decode_block(&v->adpcm_state,
            voice_ram + v->current_addr, v->decode_buf);
        /* M6: advance address by 16 bytes (one ADPCM block) */
        v->current_addr += SPU94_ADPCM_BLOCK_BYTES;
        v->decode_buf_pos = 0;
        v->has_block = 1;

        /* End-address check: stop or loop back depending on loop_enabled.
         * Match the VAG flag loop path: keep has_block = 1 so the just-decoded
         * block plays out before the next decode from loop_addr. */
        if (v->end_addr > 0 && v->current_addr >= v->end_addr) {
            if (v->loop_enabled && v->loop_addr < v->end_addr) {
                v->current_addr = v->loop_addr;
                v->adpcm_state.old   = v->loop_adpcm_old;
                v->adpcm_state.older = v->loop_adpcm_older;
            } else {
                if (v->adsr.enabled) {
                    v->adsr.phase = ADSR_OFF;
                    v->adsr.level = 0;
                } else {
                    v->active = 0;
                }
                *out_l = 0;
                *out_r = 0;
                return;
            }
        }

        /* --- Loop flag dispatch (LOOP-01 through LOOP-05; C4, C5, S3) --- */

        /* LOOP-02 / C4: Loop-Start — latch loop_addr and snapshot filter state.
         * The address latched is the address of THIS block (before the += 16 advance).
         * The filter state snapshot is taken AFTER decode — this is the state that will
         * be valid at the start of the next time the loop returns here (C5). */
        if (flag_byte & SPU94_VAG_FLAG_LOOP_START) {
            v->loop_addr = v->current_addr - SPU94_ADPCM_BLOCK_BYTES;
            v->loop_adpcm_old   = v->adpcm_state.old;
            v->loop_adpcm_older = v->adpcm_state.older;
        }

        /* LOOP-01 / C4: Loop-End — set ENDX regardless of repeat bit */
        if (flag_byte & SPU94_VAG_FLAG_END) {
            v->endx = 1;  /* LOOP-05: ENDX set; cleared only by KON (M3) */

            if (flag_byte & SPU94_VAG_FLAG_LOOP_REPEAT) {
                /* LOOP-03 / C5: Jump to loop_addr and restore filter state snapshot.
                 * S3: do NOT clear the Gaussian ring — let old samples be pushed out
                 * naturally over the next 3 ticks as new samples are decoded. */
                v->current_addr = v->loop_addr;
                v->adpcm_state.old   = v->loop_adpcm_old;
                v->adpcm_state.older = v->loop_adpcm_older;
                /* Force decode of the loop-start block on the next decode trigger.
                 * has_block is still 1 for the current block; current_addr now points
                 * to the loop-start block which will be decoded when has_block goes 0. */
            } else {
                /* LOOP-04: One-shot — loop end without repeat; mute the voice.
                 * C4: the voice stays nominally "active" at zero level; it does NOT
                 * get deactivated here unless ADSR is disabled. */
                if (v->adsr.enabled) {
                    /* Drive ADSR to terminal state — same as a completed release.
                     * The existing ADSR_OFF check in Step 2.5 will zero output and
                     * clear active on the next tick. */
                    v->adsr.phase = ADSR_OFF;
                    v->adsr.level = 0;
                } else {
                    v->active = 0;
                }
            }
        }
    }

    /* ---------------------------------------------------------------
     * STEP 2 — Gaussian interpolation, zero-order hold, or NON substitution
     * NON-04/NON-05: if NON enabled, substitute global noise level for Gauss
     * output. ADPCM decode (STEP 1) already ran — loop flags, ENDX are side
     * effects preserved even for NON voices (NON-06).
     * AA-01: gauss_bypass=1 skips the 4-tap table lookup and outputs
     *        the newest decoded sample (s3) directly — zero-order hold.
     * --------------------------------------------------------------- */
    {
        int16_t gauss_out;
        if (non_enabled) {
            /* NON-04/NON-05: substitute global noise level.
             * All NON voices receive the same noise_level per tick. */
            gauss_out = noise_level;
        } else {
            const uint8_t gi = (uint8_t)((v->pitch_counter >> 4) & 0xFF);
            const uint8_t wp = v->gauss_ring_pos;
            const int16_t s0 = v->gauss_ring[(wp + 0) & 3];
            const int16_t s1 = v->gauss_ring[(wp + 1) & 3];
            const int16_t s2 = v->gauss_ring[(wp + 2) & 3];
            const int16_t s3 = v->gauss_ring[(wp + 3) & 3];
            if (gauss_bypass) {
                /* AA-01: Zero-order hold — output newest sample without interpolation.
                 * At pitch-up this produces sample-skipping aliasing; at pitch-down
                 * this produces sample-repeating staircase artifacts. */
                gauss_out = s3;
            } else {
                int32_t interpolated =
                      (int32_t)spu94_gauss_table[0x0FF - gi] * (int32_t)s0
                    + (int32_t)spu94_gauss_table[0x1FF - gi] * (int32_t)s1
                    + (int32_t)spu94_gauss_table[0x100 + gi] * (int32_t)s2
                    + (int32_t)spu94_gauss_table[0x000 + gi] * (int32_t)s3;
                gauss_out = sat_s16(interpolated >> 15);
            }
        }

        /* ---------------------------------------------------------------
         * STEP 2.5 — ADSR envelope (ADSR-01..06)
         * spu94_adsr_tick returns current level 0..0x7FFF.
         * Level is applied as a Q15 multiply against the Gaussian output.
         * When ADSR transitions to OFF, voice is silenced immediately.
         * --------------------------------------------------------------- */
        {
            int16_t adsr_level = spu94_adsr_tick(&v->adsr);
            if (v->adsr.phase == ADSR_OFF && v->adsr.enabled) {
                v->active = 0;
                *out_l = 0;
                *out_r = 0;
                return;  /* skip Steps 3 and 4 — voice is done */
            }
            gauss_out = q15_mul_truncate(gauss_out, adsr_level);
        }

        /* ---------------------------------------------------------------
         * STEP 2.75 — Store VxOUTX (SVOL-04 / Phase 34)
         * Post-ADSR, pre-volume value for PMON (Phase 35).
         * Must be stored AFTER ADSR multiply, BEFORE volume multiply.
         * --------------------------------------------------------------- */
        v->outx = gauss_out;

        /* ---------------------------------------------------------------
         * STEP 2.9 — Internal mod bus (Phase 50: MOD-01..05)
         * Noise-to-volume and noise-to-pan modulation. Applied BEFORE
         * Step 3 so the modulated volumes are used in the final multiply.
         * Ephemeral per-tick — does NOT write back to v->vol_l/vol_r.
         * Early-out: skip entire block when all three depths are 0.
         * --------------------------------------------------------------- */
        int16_t mod_vol_l = v->vol_l;
        int16_t mod_vol_r = v->vol_r;
        if (v->noise_mod_pitch_depth | v->noise_mod_vol_depth | v->noise_mod_pan_depth) {
            /* MOD-02: Volume modulation — symmetric offset to both channels */
            if (v->noise_mod_vol_depth != 0) {
                int32_t vol_mod = ((int32_t)noise_level * (int32_t)v->noise_mod_vol_depth) >> 15;
                int32_t vl32 = (int32_t)mod_vol_l + vol_mod;
                int32_t vr32 = (int32_t)mod_vol_r + vol_mod;
                if (vl32 > 0x3FFF) vl32 = 0x3FFF;
                if (vl32 < -0x4000) vl32 = -0x4000;
                if (vr32 > 0x3FFF) vr32 = 0x3FFF;
                if (vr32 < -0x4000) vr32 = -0x4000;
                mod_vol_l = (int16_t)vl32;
                mod_vol_r = (int16_t)vr32;
            }
            /* MOD-03: Pan modulation — differential offset (L gets +noise*depth,
             * R gets -noise*depth) creating stereo jitter */
            if (v->noise_mod_pan_depth != 0) {
                int32_t pan_mod = ((int32_t)noise_level * (int32_t)v->noise_mod_pan_depth) >> 15;
                int32_t pl32 = (int32_t)mod_vol_l + pan_mod;
                int32_t pr32 = (int32_t)mod_vol_r - pan_mod;
                if (pl32 > 0x3FFF) pl32 = 0x3FFF;
                if (pl32 < -0x4000) pl32 = -0x4000;
                if (pr32 > 0x3FFF) pr32 = 0x3FFF;
                if (pr32 < -0x4000) pr32 = -0x4000;
                mod_vol_l = (int16_t)pl32;
                mod_vol_r = (int16_t)pr32;
            }
        }

        /* ---------------------------------------------------------------
         * STEP 3 — Apply per-voice volume (VOICE-04)
         * Uses mod_vol_l/mod_vol_r which include mod bus offsets (or
         * are identical to v->vol_l/vol_r when all depths are 0).
         * --------------------------------------------------------------- */
        *out_l = q15_mul_truncate(gauss_out, mod_vol_l);
        *out_r = q15_mul_truncate(gauss_out, mod_vol_r);
    }

    /* ---------------------------------------------------------------
     * STEP 4 — Advance pitch counter and push samples into ring
     * --------------------------------------------------------------- */
    {
        uint16_t old_ctr = v->pitch_counter;
        /* C7: re-clamp pitch in tick (defensive; key_on already clamps to 0x3FFF).
         * PMON (Phase 35) can set pitch to 0x4000 — this is spec-correct:
         * "IF Step > 3FFFh THEN Step = 4000h". Accept 0x4000, clamp above it. */
        uint16_t effective_pitch = (v->pitch > 0x4000) ? 0x4000 : v->pitch;

        /* MOD-01: Pitch modulation — offset effective pitch by noise * depth.
         * Applied per-tick (does NOT write back to v->pitch).
         * Clamp to 0..0x4000 (same range PMON allows). */
        if (v->noise_mod_pitch_depth != 0) {
            int32_t pitch_mod = ((int32_t)noise_level * (int32_t)v->noise_mod_pitch_depth) >> 15;
            int32_t mod_pitch = (int32_t)effective_pitch + pitch_mod;
            if (mod_pitch < 0) mod_pitch = 0;
            if (mod_pitch > 0x4000) mod_pitch = 0x4000;
            effective_pitch = (uint16_t)mod_pitch;
        }

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

/* LOOP-05: query the per-voice ENDX status bit.
 * Set on Loop-End, cleared only by KON (M3). */
uint8_t spu94_voice_get_endx(const spu94_voice_t *v) {
    if (v == NULL) return 0;
    return v->endx;
}

/* -----------------------------------------------------------------------
 * Phase 30: 24-Voice Mixer API (MIX-01 through MIX-06)
 *
 * C8: pending_kon/pending_koff semantics — writes latch into bitmasks,
 *     applied at START of next tick. KON wins over same-tick KOFF.
 * S1: "Accumulate in int32 before sat_s16 — see mixer tick in spu94_process.c"
 * MIX-06: "eon_flags gates reverb send per voice; does not affect dry accumulator"
 *
 * RT-safety: no malloc, no locks, no syscalls. All functions operate on
 * caller-provided struct pointers with no side effects.
 * ----------------------------------------------------------------------- */

void spu94_voice_mixer_init(spu94_voice_mixer_t *m) {
    if (m == NULL) return;
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < 24; i++) {
        spu94_voice_init(&m->voices[i]);
        spu94_voice_init(&m->pending_config[i]);
    }
    /* pending_kon, pending_koff, eon_flags, master_vol_l/r, enabled,
     * gauss_bypass are all zero from memset — correct defaults.
     * gauss_bypass=0 means Gaussian interpolation ON (PS1 faithful). */

    /* NON-08: initialize the global noise generator (seed=1, not 0) */
    spu94_noise_gen_init(&m->noise_gen);
}

spu94_result_t spu94_voice_mixer_key_on(spu94_voice_mixer_t *m, int voice_idx,
    uint32_t start_addr, uint16_t pitch, int16_t vol_l, int16_t vol_r,
    int reverb_on, const spu94_adsr_state_t *adsr_cfg)
{
    /* T-30-02: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* C8/MIX-04: prepare pending config — build a voice_t from args.
     * This is staged here and applied at the START of the next tick. */
    spu94_voice_t *cfg = &m->pending_config[voice_idx];
    spu94_voice_init(cfg);
    cfg->sample_start_addr = start_addr;
    cfg->pitch = pitch;
    cfg->vol_l = vol_l;
    cfg->vol_r = vol_r;

    /* Stage ADSR config if provided */
    if (adsr_cfg != NULL) {
        cfg->adsr = *adsr_cfg;
    }

    /* Store reverb_on intent: we use a convention where the pending_config's
     * endx field (unused for pending) carries the reverb_on flag. The tick
     * application logic reads this to update eon_flags. */
    cfg->endx = (uint8_t)(reverb_on ? 1 : 0);

    /* C8: KON wins over same-tick KOFF */
    m->pending_kon |= (1u << voice_idx);
    m->pending_koff &= ~(1u << voice_idx);

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_key_off(spu94_voice_mixer_t *m, int voice_idx)
{
    /* T-30-02: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* C8/MIX-04: set pending_koff bit. Note: if KON was already pending for
     * this voice, it still wins when tick processes (KON checked first). But
     * per the API contract, setting KOFF after KON without a tick in between
     * is handled by the tick logic — not here. We just set the bit. */
    m->pending_koff |= (1u << voice_idx);

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_eon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    /* T-30-02: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* MIX-02: set or clear eon_flags bit immediately (control-rate param) */
    if (enabled) {
        m->eon_flags |= (1u << voice_idx);
    } else {
        m->eon_flags &= ~(1u << voice_idx);
    }

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    /* T-35-01: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* PMON-01: set or clear pmon_flags bit. Bit 0 is accepted here but
     * the mixer tick ignores it (voice 0 has no predecessor). */
    if (enabled) {
        m->pmon_flags |= (1u << voice_idx);
    } else {
        m->pmon_flags &= ~(1u << voice_idx);
    }

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled)
{
    /* T-36-01: validate voice_idx 0..23 */
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    /* NON-04: set or clear non_flags bit. All 24 voices can be NON. */
    if (enabled) {
        m->non_flags |= (1u << voice_idx);
    } else {
        m->non_flags &= ~(1u << voice_idx);
    }

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_noise_freq(spu94_voice_mixer_t *m,
    uint8_t shift, uint8_t step_raw)
{
    /* T-36-02: validate shift (0-15) and step_raw (0-3) */
    if (m == NULL || shift > 15 || step_raw > 3)
        return SPU94_INVALID_ARG;

    /* NON-03: noise frequency from SPUCNT register fields */
    m->noise_gen.shift = shift;
    m->noise_gen.step = step_raw + 4;  /* SPUCNT[9:8] + 4 = 4..7 */

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_sweep_l(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase,
    uint8_t shift, uint8_t step, uint8_t retrigger_enable,
    uint8_t bipolar, uint8_t shape)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    if (shift > 31 || step > 3)
        return SPU94_INVALID_ARG;
    if (shape > 2)
        return SPU94_INVALID_ARG;

    spu94_sweep_configure(&m->voices[voice_idx].sweep_l,
                          mode, direction, phase, shift, step, retrigger_enable,
                          bipolar, shape);
    m->voices[voice_idx].sweep_l.level = m->voices[voice_idx].vol_l;
    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_sweep_r(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase,
    uint8_t shift, uint8_t step, uint8_t retrigger_enable,
    uint8_t bipolar, uint8_t shape)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    if (shift > 31 || step > 3)
        return SPU94_INVALID_ARG;
    if (shape > 2)
        return SPU94_INVALID_ARG;

    spu94_sweep_configure(&m->voices[voice_idx].sweep_r,
                          mode, direction, phase, shift, step, retrigger_enable,
                          bipolar, shape);
    m->voices[voice_idx].sweep_r.level = m->voices[voice_idx].vol_r;
    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_pitch(spu94_voice_mixer_t *m, int voice_idx,
    uint16_t pitch)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    if (pitch == 0) pitch = 0x1000;
    if (pitch > 0x3FFF) pitch = 0x3FFF;
    m->voices[voice_idx].pitch = pitch;

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_mod_bus(spu94_voice_mixer_t *m, int voice_idx,
    int16_t pitch_depth, int16_t vol_depth, int16_t pan_depth)
{
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;

    m->voices[voice_idx].noise_mod_pitch_depth = pitch_depth;
    m->voices[voice_idx].noise_mod_vol_depth   = vol_depth;
    m->voices[voice_idx].noise_mod_pan_depth   = pan_depth;

    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_load_sample(spu94_voice_mixer_t *m,
    uint32_t addr, const uint8_t *source, uint32_t source_size)
{
    /* T-30-01: validate bounds */
    if (m == NULL || source == NULL)
        return SPU94_INVALID_ARG;
    if (addr + source_size > SPU94_SPU_RAM_BYTES)
        return SPU94_INVALID_ARG;
    if (source_size == 0)
        return SPU94_OK;  /* nothing to copy */

    memcpy(m->voice_ram + addr, source, source_size);
    return SPU94_OK;
}

void spu94_voice_mixer_tick(spu94_voice_mixer_t *m,
    int16_t *dry_l, int16_t *dry_r,
    int16_t *rev_l, int16_t *rev_r)
{
    if (m == NULL || dry_l == NULL || dry_r == NULL ||
        rev_l == NULL || rev_r == NULL) {
        if (dry_l) *dry_l = 0;
        if (dry_r) *dry_r = 0;
        if (rev_l) *rev_l = 0;
        if (rev_r) *rev_r = 0;
        return;
    }

    /* C8 / MIX-04: Apply pending KON/KOFF at start of tick, before any voice runs */
    for (int v = 0; v < 24; v++) {
        uint32_t bit = (uint32_t)1u << v;
        if (m->pending_kon & bit) {
            /* Copy pending_config into live voice, then key_on the live struct.
             * The pending_config carries staged ADSR and parameters. */
            spu94_voice_t *cfg = &m->pending_config[v];
            m->voices[v] = *cfg;

            /* Update eon_flags from pending config's reverb_on intent
             * (stored in pending_config.endx as a temporary flag) */
            if (cfg->endx) {
                m->eon_flags |= bit;
            } else {
                m->eon_flags &= ~bit;
            }

            /* Actually key on the live voice with the staged params */
            spu94_voice_key_on(&m->voices[v],
                cfg->sample_start_addr, cfg->pitch,
                cfg->vol_l, cfg->vol_r);
        }
        if (m->pending_koff & bit) {
            spu94_voice_key_off(&m->voices[v]);
        }
    }
    m->pending_kon  = 0;
    m->pending_koff = 0;

    /* NON-08: tick noise generator ONCE before voice loop */
    spu94_noise_gen_tick(&m->noise_gen);

    /* S1 / MIX-01: accumulate 24 voices in int32 to prevent overflow */
    int32_t dry_sum_l = 0, dry_sum_r = 0;
    int32_t rev_sum_l = 0, rev_sum_r = 0;
    for (int v = 0; v < 24; v++) {
        if (!m->voices[v].active) continue;

        /* ---------------------------------------------------------------
         * PMON-01: Pitch Modulation (Phase 35)
         *
         * If PMON is enabled for this voice AND it has a predecessor (v > 0),
         * modulate the pitch step using the previous voice's outx value.
         *
         * Formula (from nocash psx-spx, verified against DuckStation):
         *   Factor = outx(v-1) + 0x8000        (range 0x0000..0xFFFF)
         *   Step   = (base_pitch * Factor) >> 15
         *   if Step > 0x3FFF: Step = 0x4000     (PMON can exceed normal max)
         *   if Step < 0: Step = 0               (negative clamp)
         *
         * Save/restore pattern preserves the base pitch register for next tick.
         * PMON bit 0 is silently ignored (voice 0 has no predecessor: m1 pitfall).
         * --------------------------------------------------------------- */
        uint16_t saved_pitch = m->voices[v].pitch;
        if ((m->pmon_flags & (1u << v)) && v > 0) {
            int16_t prev_outx = m->voices[v - 1].outx;
            uint32_t factor = (uint32_t)((int32_t)prev_outx + 0x8000);
            uint32_t base_step = (uint32_t)m->voices[v].pitch;
            int32_t mod_step = (int32_t)((base_step * factor) >> 15);
            if (mod_step > 0x3FFF) mod_step = 0x4000;  /* m2: PMON clamp (not 0x3FFF) */
            if (mod_step < 0) mod_step = 0;
            m->voices[v].pitch = (uint16_t)mod_step;
        }

        /* NON-04: determine if this voice uses noise */
        uint8_t non_enabled = (m->non_flags & (1u << v)) ? 1 : 0;

        int16_t vl = 0, vr = 0;
        spu94_voice_tick(&m->voices[v],
                         m->voice_ram, SPU94_SPU_RAM_BYTES,
                         m->gauss_bypass,
                         m->noise_gen.level,  /* NON-05: same value for all voices */
                         non_enabled,
                         &vl, &vr);

        /* Restore original pitch register (PMON is per-tick, not persistent) */
        m->voices[v].pitch = saved_pitch;

        dry_sum_l += vl;
        dry_sum_r += vr;
        /* MIX-02: reverb send only for EON-flagged voices */
        if (m->eon_flags & ((uint32_t)1u << v)) {
            rev_sum_l += vl;
            rev_sum_r += vr;
        }
    }

    /* MIX-01: saturate accumulated sum to int16 */
    int16_t mixed_l = sat_s16(dry_sum_l);
    int16_t mixed_r = sat_s16(dry_sum_r);

    /* MIX-03: apply Master Volume L/R after summation */
    *dry_l = q15_mul_truncate(mixed_l, m->master_vol_l);
    *dry_r = q15_mul_truncate(mixed_r, m->master_vol_r);

    /* MIX-05: saturate reverb send sum */
    *rev_l = sat_s16(rev_sum_l);
    *rev_r = sat_s16(rev_sum_r);
}
