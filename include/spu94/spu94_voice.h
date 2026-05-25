/* include/spu94/spu94_voice.h
 * Phase 27: Per-voice state struct and tick API for the v1.8 PSX Voice Engine.
 *
 * Each spu94_voice_t represents one of 24 PS1 SPU voices. The struct owns
 * all per-voice mutable state: pitch counter, ADPCM decoder state, Gaussian
 * ring buffer, decode buffer, and volume. No state is shared with the ADPCM
 * coloration bus (C1, C2) or with other voices.
 *
 * RT-safety: spu94_voice_tick is called from the audio callback. It performs
 * no heap allocation, no locks, no syscalls. All buffers are caller-provided.
 *
 * Phase 27 defers: loop_start_addr, loop_adpcm_state, ADSR fields,
 * reverb_on, endx. These will be added to spu94_voice_t in Phases 28-29
 * without breaking the Phase 27 API.
 */
#ifndef SPU94_VOICE_H
#define SPU94_VOICE_H

#include <spu94/spu94.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_adsr.h>
#include <spu94/spu94_sweep.h>
#include <spu94/spu94_noise.h>
#include <spu94/spu94_spu_ram.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-voice state struct.
 *
 * Field notes:
 *   - gauss_ring[4]: ISOLATED per voice — never references the coloration
 *     bus's state->gauss_ring_l/r (C2 prevention).
 *   - adpcm_state: ISOLATED per voice — never references the coloration
 *     bus's state->adpcm_state_l/r (C1 prevention).
 *   - vol_l/vol_r: full signed range per SVOL-01; negative = polarity flip.
 *     Range -0x4000..+0x3FFF matches PS1 hardware volume registers.
 *   - has_block: gate flag for decode-before-interpolate order (S5).
 *   - pitch_counter: 4.12 fixed-point. Bits 12+ = sample index advancement.
 *     Bits 4-11 = Gaussian interpolation index.
 */
typedef struct {
    uint32_t  current_addr;       /* byte offset of current ADPCM block in voice RAM */
    uint32_t  sample_start_addr;  /* byte offset of sample start (for key-on reset) */
    uint16_t  pitch;              /* 0x0001..0x3FFF; 0x1000 = 44.1 kHz playback rate */
    uint16_t  pitch_counter;      /* 4.12 fixed-point; bits 12+: sample, bits 4-11: Gauss */
    spu94_adpcm_state adpcm_state;    /* filter history (old/older) across blocks */
    int16_t   decode_buf[28];     /* current decoded block (SPU94_ADPCM_BLOCK_SAMPLES) */
    uint8_t   decode_buf_pos;     /* next sample index in decode_buf (0..27) */
    uint8_t   has_block;          /* 1 = decode_buf is valid; 0 = need to decode next block */
    int16_t   gauss_ring[4];      /* last 4 decoded samples for Gaussian interpolation */
    uint8_t   gauss_ring_pos;     /* write head in gauss_ring (0..3) */
    int16_t   vol_l;              /* per-voice left volume (-0x4000..+0x3FFF, signed; negative = phase inversion per SVOL-01) */
    int16_t   vol_r;              /* per-voice right volume (-0x4000..+0x3FFF, signed; negative = phase inversion per SVOL-01) */
    int16_t   outx;              /* Phase 34/SVOL-04: post-ADSR, pre-volume output for PMON (Phase 35). Stored every tick. */
    spu94_adsr_state_t adsr;     /* Phase 28: per-voice ADSR envelope state */
    spu94_sweep_t sweep_l;       /* Phase 37: per-voice left volume sweep */
    spu94_sweep_t sweep_r;       /* Phase 37: per-voice right volume sweep */
    /* Phase 50: Internal mod bus -- per-voice noise-to-pitch/vol/pan routing (MOD-01..05) */
    int16_t noise_mod_pitch_depth; /* bipolar (-0x7FFF..+0x7FFF): noise -> pitch offset. 0 = off */
    int16_t noise_mod_vol_depth;   /* unipolar (0..0x7FFF): noise -> volume offset. 0 = off */
    int16_t noise_mod_pan_depth;   /* unipolar (0..0x7FFF): noise -> stereo divergence. 0 = off */
    /* Phase 29: Loop mechanics fields (LOOP-01..05; C4, C5 pitfall prevention) */
    uint32_t  loop_addr;          /* byte offset latched on Loop-Start flag (LOOP-02) */
    int16_t   loop_adpcm_old;     /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    int16_t   loop_adpcm_older;   /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    uint8_t   endx;               /* set on Loop-End; cleared on KON (LOOP-05 / M3) */
    uint8_t   active;             /* 1 = voice is playing; 0 = silent */
    uint8_t   loop_enabled;      /* 1 = GUI-driven loop; end_addr loops back to loop_addr */
    uint32_t  end_addr;           /* byte offset to stop at (0 = no limit, use loop flags) */
} spu94_voice_t;

/* Initialize a voice struct to silence. Zero all fields; active=0. */
void spu94_voice_init(spu94_voice_t *v);

/* Key on: start playback from start_addr with given pitch and volume.
 * Resets pitch counter, decoder state, Gaussian ring. Sets active=1.
 * Calls spu94_adsr_key_on to reset envelope to attack phase (level=0).
 * Caller should configure voice->adsr register fields before calling key_on.
 * Pitch is clamped to 0x3FFF (C7 / VOICE-03). Pitch 0 is treated as 0x1000. */
void spu94_voice_key_on(spu94_voice_t *v, uint32_t start_addr,
                        uint16_t pitch, int16_t vol_l, int16_t vol_r);

/* Key off: enter ADSR release phase. Voice remains active until the
 * envelope level decays to 0, at which point spu94_voice_tick sets active=0.
 * If ADSR is disabled (adsr.enabled=0), reverts to immediate silence. */
void spu94_voice_key_off(spu94_voice_t *v);

/* Returns 1 if this voice has reached a loop-end block since the last KON,
 * 0 otherwise. Cleared on KON. Set on Loop-End. NOT cleared by KOFF (M3). */
uint8_t spu94_voice_get_endx(const spu94_voice_t *v);

/* Tick one voice: advance counter, decode if needed, Gaussian interpolate,
 * apply per-voice volume. Writes stereo output samples to *out_l, *out_r.
 *
 * voice_ram: pointer to the dedicated voice RAM buffer (C6: separate from
 *            reverb work buffer).
 * voice_ram_size: size in bytes (for bounds checking, T-27-01).
 * gauss_bypass: AA-01: 0 = 4-tap Gaussian interpolation (PS1 faithful),
 *               1 = zero-order hold (output newest sample, raw aliasing).
 * noise_level: NON-05: global noise value from spu94_noise_gen_t.level.
 * non_enabled: NON-04: 1 = output noise instead of ADPCM/Gauss, 0 = normal.
 *
 * RT-safe: no heap, no locks, no syscalls. */
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t noise_level,
                      uint8_t non_enabled,
                      int16_t *out_l, int16_t *out_r);

/* -----------------------------------------------------------------------
 * Phase 30: 24-Voice Mixer (MIX-01 through MIX-06)
 *
 * spu94_voice_mixer_t: caller-allocated struct holding all 24 voice states,
 * a dedicated 512 KB voice RAM buffer, pending KON/KOFF bitmasks (C8),
 * Master Volume L/R (MIX-03), and EON (reverb-on) flags per voice (MIX-02).
 *
 * Pending KON/KOFF semantics (C8/MIX-04): writes latch into pending bitmasks,
 * applied at START of next tick. KON wins over same-tick KOFF.
 *
 * S1: "Accumulate in int32 before sat_s16 — see mixer tick in spu94_process.c"
 * MIX-06: "eon_flags gates reverb send per voice; does not affect dry accumulator"
 * ----------------------------------------------------------------------- */

/* spu94_voice_mixer_t: 530452 bytes (~518 KB).
 * Breakdown: 24 voices * 128B + 512KB voice_ram + pending_config[24] * 128B
 * + control fields (12 bytes). Lives as file-scope static in spu94_process.c. */
typedef struct {
    spu94_voice_t voices[24];          /* VOICE-06: 24 isolated per-voice structs */
    uint8_t       voice_ram[SPU94_SPU_RAM_BYTES]; /* C6: separate from reverb work_buf */
    spu94_voice_t pending_config[24];  /* C8: config staged at key_on, applied at tick start */
    uint32_t      pending_kon;         /* C8/MIX-04: bitmask, applied at next tick start */
    uint32_t      pending_koff;        /* C8/MIX-04: bitmask, applied at next tick start */
    uint32_t      eon_flags;           /* MIX-02: bit N set = voice N sends to reverb */
    uint32_t      pmon_flags;          /* PMON-01: bit N set = voice N pitch modulated by voice N-1's outx. Bit 0 accepted but ignored (voice 0 has no predecessor). */
    uint32_t      non_flags;           /* NON-04: bit N set = voice N outputs noise instead of ADPCM/Gauss */
    spu94_noise_gen_t noise_gen;       /* NON-08: single global noise generator, ticked once before voice loop */
    int16_t       master_vol_l;        /* MIX-03: Q15, applied after voice sum */
    int16_t       master_vol_r;        /* MIX-03: Q15, applied after voice sum */
    uint8_t       enabled;             /* gate: 0 = voice engine bypassed entirely */
    uint8_t       gauss_bypass;        /* AA-01: 0 = Gauss interp (PS1 faithful), 1 = zero-order hold (raw aliasing) */
} spu94_voice_mixer_t;

/* Initialize mixer: zero all 24 voices, clear pending masks, clear eon_flags,
 * zero master volume, set enabled=0. */
void spu94_voice_mixer_init(spu94_voice_mixer_t *m);

/* Key on a voice: validate voice_idx (0..23), set pending_kon bit.
 * Does NOT touch voice state yet (C8/MIX-04). Takes start_addr, pitch,
 * vol_l, vol_r, reverb_on flag, and optional ADSR config.
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_key_on(spu94_voice_mixer_t *m, int voice_idx,
    uint32_t start_addr, uint16_t pitch, int16_t vol_l, int16_t vol_r,
    int reverb_on, const spu94_adsr_state_t *adsr_cfg);

/* Key off a voice: validate voice_idx (0..23), set pending_koff bit.
 * Does NOT enter release yet (C8/MIX-04).
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_key_off(spu94_voice_mixer_t *m, int voice_idx);

/* Set or clear the reverb-on bit in eon_flags for a given voice.
 * This is a control-rate parameter; mid-tick changes are acceptable.
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_eon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);

/* Set or clear the PMON (pitch modulation) bit for a given voice.
 * When enabled, voice N's pitch step is modulated by voice N-1's outx.
 * Bit 0 is accepted but ignored (voice 0 has no predecessor).
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);

/* Set or clear the NON (noise on) bit for a given voice.
 * When enabled, voice outputs the global noise level instead of ADPCM/Gauss.
 * ADPCM decode still runs for side effects (loop flags, ENDX).
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx,
    int enabled);

/* Set the noise generator frequency parameters.
 * shift: 0..15 (SPUCNT[13:10], NoiseShift). Higher = more frequent LFSR shifts.
 * step_raw: 0..3 (SPUCNT[9:8], NoiseStep raw). Actual step = step_raw + 4.
 * Returns SPU94_INVALID_ARG if shift > 15 or step_raw > 3. */
spu94_result_t spu94_voice_mixer_set_noise_freq(spu94_voice_mixer_t *m,
    uint8_t shift, uint8_t step_raw);

/* Configure left volume sweep for a voice. Sets sweep_l parameters and
 * initializes sweep_l.level to the voice's current vol_l. Sets active=1.
 * retrigger_enable: 0=one-shot (v1.9), 1=auto-reverse at limits (RTR-02).
 * bipolar: 0=unipolar (single quadrant), 1=bipolar (crosses zero into
 *          opposite quadrant, enabling ring mod phase inversion).
 * shape: 0=triangle (auto-reverse), 1=saw down (reset to max), 2=saw up (reset to zero).
 * Returns SPU94_INVALID_ARG if voice_idx out of range or shape > 2. */
spu94_result_t spu94_voice_mixer_set_sweep_l(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase,
    uint8_t shift, uint8_t step, uint8_t retrigger_enable,
    uint8_t bipolar, uint8_t shape);

/* Configure right volume sweep for a voice. Sets sweep_r parameters and
 * initializes sweep_r.level to the voice's current vol_r. Sets active=1.
 * retrigger_enable: 0=one-shot (v1.9), 1=auto-reverse at limits (RTR-02).
 * bipolar: 0=unipolar (single quadrant), 1=bipolar (crosses zero into
 *          opposite quadrant, enabling ring mod phase inversion).
 * shape: 0=triangle (auto-reverse), 1=saw down (reset to max), 2=saw up (reset to zero).
 * Returns SPU94_INVALID_ARG if voice_idx out of range or shape > 2. */
spu94_result_t spu94_voice_mixer_set_sweep_r(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase,
    uint8_t shift, uint8_t step, uint8_t retrigger_enable,
    uint8_t bipolar, uint8_t shape);

/* Update the pitch register of a playing voice without re-triggering.
 * Takes effect on the next tick. Clamped to 0x3FFF.
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_pitch(spu94_voice_mixer_t *m, int voice_idx,
    uint16_t pitch);

/* Set noise modulation depths for internal mod bus (Phase 50: MOD-01..05).
 * All three depths are independently controllable per voice.
 * Pitch depth is bipolar (-0x7FFF..+0x7FFF); vol and pan depths are unipolar
 * (0..0x7FFF). When all depths are 0, the mod bus is bypassed (bit-identical
 * to unmodulated output). Modulation is applied per-tick inside spu94_voice_tick
 * at sample rate; it does NOT persist to the voice struct fields.
 * Returns SPU94_INVALID_ARG if voice_idx out of range. */
spu94_result_t spu94_voice_mixer_set_mod_bus(spu94_voice_mixer_t *m, int voice_idx,
    int16_t pitch_depth, int16_t vol_depth, int16_t pan_depth);

/* Load pre-encoded ADPCM blocks into mixer->voice_ram at given byte offset.
 * Validates addr + source_size <= SPU94_SPU_RAM_BYTES.
 * Returns SPU94_INVALID_ARG on bounds violation or NULL source. */
spu94_result_t spu94_voice_mixer_load_sample(spu94_voice_mixer_t *m,
    uint32_t addr, const uint8_t *source, uint32_t source_size);

/* Tick the mixer: apply pending KON/KOFF, run all 24 voices, accumulate
 * dry sum (int32) and reverb-send sum (int32), saturate to int16, apply
 * master volume. Outputs dry L/R and reverb-send L/R.
 * RT-safe: no heap, no locks, no syscalls. */
void spu94_voice_mixer_tick(spu94_voice_mixer_t *m,
    int16_t *dry_l, int16_t *dry_r,
    int16_t *rev_l, int16_t *rev_r);

/* Accessor: get pointer to the file-scope mixer instance (for Phase 31). */
spu94_voice_mixer_t *spu94_get_voice_mixer(void);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_VOICE_H */
