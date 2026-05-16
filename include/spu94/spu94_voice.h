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
 *   - vol_l/vol_r: declared int16_t to allow phase inversion (S2), but
 *     Phase 27 documents unsigned semantics (0-32767) per VOICE-04.
 *     Negative = polarity flip, which is correct SPU behavior.
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
    int16_t   vol_l;              /* per-voice left volume (0..32767, unsigned semantics) */
    int16_t   vol_r;              /* per-voice right volume (0..32767, unsigned semantics) */
    spu94_adsr_state_t adsr;     /* Phase 28: per-voice ADSR envelope state */
    /* Phase 29: Loop mechanics fields (LOOP-01..05; C4, C5 pitfall prevention) */
    uint32_t  loop_addr;          /* byte offset latched on Loop-Start flag (LOOP-02) */
    int16_t   loop_adpcm_old;     /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    int16_t   loop_adpcm_older;   /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    uint8_t   endx;               /* set on Loop-End; cleared on KON (LOOP-05 / M3) */
    uint8_t   active;             /* 1 = voice is playing; 0 = silent */
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
 *
 * RT-safe: no heap, no locks, no syscalls. */
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
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
    int16_t       master_vol_l;        /* MIX-03: Q15, applied after voice sum */
    int16_t       master_vol_r;        /* MIX-03: Q15, applied after voice sum */
    uint8_t       enabled;             /* gate: 0 = voice engine bypassed entirely */
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
