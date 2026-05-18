/* src/spu94/spu94_state_internal.h
 *
 * INTERNAL header for libspu94 — not installed, never on the public include
 * path. Holds the layout of struct spu94_state so that every Phase-2 internal
 * translation unit (spu94_state.c, spu94_register_io.c, spu94_write_policy.c,
 * spu94_pending.c, spu94_tick.c, spu94_registers.c) can manipulate the same
 * fields without re-defining the struct. Re-defining the struct in multiple
 * TUs is ODR-unsafe — this header is the single home.
 *
 * Path: src/spu94/spu94_state_internal.h. Sub-headers under src/ are reachable
 * via "spu94_state_internal.h" from any TU in src/spu94/.
 *
 * NOTE: This header is NEVER referenced from anything under include/spu94/.
 * Public callers see only the opaque forward declaration in spu94_registers.h.
 */
#ifndef SPU94_STATE_INTERNAL_H
#define SPU94_STATE_INTERNAL_H

#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_dac_fir.h>
#include <spu94/spu94_dac_noise.h>
#include <stdint.h>
#include <stddef.h>

struct spu94_state {
    /* Caller-provided work buffer (D-13). Not released by the library. */
    unsigned char *work_buf;
    size_t         work_buf_size;

    /* BufferAddress — byte offset into the reverb work buffer. Per
     * 02-RESEARCH.md A4: mBASE = 0 at init, snap-on-write makes addr = 0. */
    uint32_t       buffer_address;

    /* Active register values (Plan 03 populates via spu94_set_reg_*). */
    int16_t        reg_values[SPU94_REG__COUNT];

    /* Shadow register values for the TICK_LATCHED policy (Plan 03). At tick
     * start, every bit set in pending_mask triggers pending_values[i] ->
     * reg_values[i] and the bit is cleared. For IMMEDIATE-policy registers,
     * pending_values[] is kept in sync with reg_values[] so the
     * spu94_get_reg_*_pending readback always returns a meaningful value. */
    int16_t        pending_values[SPU94_REG__COUNT];
    uint64_t       pending_mask;  /* bit i set -> pending_values[i] awaits flush */

    /* Phase 3 Plan 01 (D-11): per-stage truncation-err accumulators.
     * Every q15_mul_truncate_with_err call in the reverb network writes
     * its pre-saturation remainder to one of these via += accumulation.
     * Read-only observability (D-23); no public accessor yet (D-04).
     * Zeroed by spu94_reset (existing hand-rolled byte-loop covers). */
    int32_t        err_input_scale;
    int32_t        err_same_iir;
    int32_t        err_diff_iir;
    int32_t        err_comb;
    int32_t        err_apf1;
    int32_t        err_apf2;
    int32_t        err_output_scale;

    /* Phase 3 Plan 01 (D-11 extension): high-bits-lost observable on
     * the hard-clip stage. |input| - INT16_MAX for inputs outside
     * ±INT16_MAX, zero otherwise. Sibling to the err_* truncation-low-
     * bits stream — together they form the complete precision-loss
     * surface (drives future Controllers use cases per Deferred Ideas
     * in 03-CONTEXT.md). */
    int32_t        overflow_magnitude;

    /* -----------------------------------------------------------------
     * Phase 5 (D-05): mix-bus mailbox. spu94_process writes these
     * fields with the current 44.1 kHz input sample before each
     * spu94_fir_chain_step call. spu94_reverb_body reads them at
     * src/spu94/spu94_reverb.c where it previously hardcoded
     * left_in = 0, right_in = 0 (Phase 3 placeholder). Default-zero
     * on init/reset means Phase 3 body-level tests that never write
     * these fields continue to observe silent-input behavior (zero
     * blast radius). Grouped adjacent to the Phase 4 FIR block
     * because both are I/O-boundary state; easier to audit.
     * ----------------------------------------------------------------- */
    int16_t        mix_bus_l;
    int16_t        mix_bus_r;

    /* -----------------------------------------------------------------
     * Phase 4: sample-rate-conversion FIR state + observability.
     * D-08 per-channel delay lines (4 rings x 39 int16 + 4 uint8
     *   indices). D-05 overflow-magnitude taps (2 int32). D-06
     *   aggregate post-shift err taps (2 int32). uint8 phase
     *   trackers for decimator and interpolator (Pitfall 7 single
     *   source of truth). Zeroed wholesale by spu94_reset (existing
     *   byte-loop covers). Layout confirmed by 04-RESEARCH
     *   Runtime State Inventory + Accumulator Width Proof.
     * ----------------------------------------------------------------- */
    int16_t        fir_delay_l_in[39];
    int16_t        fir_delay_r_in[39];
    uint8_t        fir_idx_l_in;
    uint8_t        fir_idx_r_in;
    int16_t        fir_delay_l_out[39];
    int16_t        fir_delay_r_out[39];
    uint8_t        fir_idx_l_out;
    uint8_t        fir_idx_r_out;
    uint8_t        fir_decimate_phase;
    uint8_t        fir_interpolate_phase;
    int32_t        err_fir_decimator;
    int32_t        err_fir_interpolator;
    int32_t        fir_overflow_decimator;
    int32_t        fir_overflow_interpolator;

    /* Phase 4 Plan 03 (Pitfall 7): cached interpolator phase-1 output.
     * The wrapper emits phase-0 on retained-phase 44.1 kHz calls, then
     * caches phase-1 here. The next 44.1 kHz call (non-retained phase)
     * emits the cached value. Single source of truth for phase ordering. */
    int16_t        fir_pending_l_phase1;
    int16_t        fir_pending_r_phase1;

    /* -----------------------------------------------------------------
     * Phase 6 Plan 06 (ADR-Phase-6-G): reverb wet-output mailbox.
     * Symmetric with mix_bus_l/r on the input side. spu94_reverb_body
     * writes the final LeftOutput/RightOutput (int16, post-output-scale,
     * already gated by vLOUT/vROUT) into these fields once per 22.05 kHz
     * tick. chain_step_impl zeroes them before each tick and reads them
     * after, feeding the result into spu94_fir_interpolate as the 22.05
     * kHz sample that seeds the 44.1 kHz output stream.
     *
     * Default-zero on init/reset (byte-loop in spu94_reset covers).
     *
     * The test-only reverb-bypass path (spu94_fir_chain_step_reverb_bypass)
     * skips spu94_tick entirely; these fields stay zero, so the
     * interpolator receives silence and the bypass path produces
     * silence. This matches ADR-Phase-6-G's wet-only output contract.
     *
     * Placed at the END of the struct (not adjacent to mix_bus_l/r
     * where it belongs logically) so that hand-typed byte-offsets in
     * tests/python/fuzz_process.py (per D-17) stay valid without a
     * forced reprobe. The next struct-layout change that legitimately
     * grows the pre-FIR or FIR blocks will have to update those offsets
     * regardless; grouping this mailbox at the tail is a one-line
     * audit concession, not a design choice.
     * ----------------------------------------------------------------- */
    int16_t        reverb_out_l;
    int16_t        reverb_out_r;

    /* -----------------------------------------------------------------
     * ADPCM voice path: downsample → encode/decode → Gaussian upsample.
     * Simulates a PS1 SPU voice playing pre-encoded ADPCM at a
     * selectable sample rate, reconstructed to 44.1 kHz via Gaussian
     * interpolation.
     * ----------------------------------------------------------------- */
    uint8_t            adpcm_enabled;       /* 0=off (default), 1=on */
    uint8_t            gauss_enabled;       /* Gaussian interpolation on output */
    uint8_t            aa_filter_enabled;   /* anti-alias filter before downsample */

    /* Voice pitch: 0x0001..0x1000.  0x1000 = 44.1 kHz (1:1, no decimation).
     * 0x0800 = 22.05 kHz, 0x005C ≈ 4000 Hz. Default 0x0800. */
    uint16_t           voice_pitch;

    /* Single voice counter (PS1-faithful): accumulates voice_pitch per
     * 44.1 kHz tick. Bits 12+: sample index (triggers decode/ring advance).
     * Bits 4-11: Gaussian interpolation index. Same register drives both. */
    uint16_t           voice_counter;       /* 4.12 fixed-point accumulator */
    int16_t            decim_prev_l;        /* previous input for AA filter */
    int16_t            decim_prev_r;

    /* ADPCM encode/decode buffers (unchanged block size of 28) */
    uint8_t            adpcm_buf_pos;       /* 0..27 accumulation index */
    int16_t            adpcm_in_buf_l[28];
    int16_t            adpcm_in_buf_r[28];
    int16_t            adpcm_out_buf_l[28]; /* decoded output buffer */
    int16_t            adpcm_out_buf_r[28];
    spu94_adpcm_state  adpcm_state_l;
    spu94_adpcm_state  adpcm_state_r;

    /* Gaussian interpolation: 4-tap FIR upsampler from voice rate to
     * 44.1 kHz. Ring buffer of last 4 decoded samples per channel.
     * Driven by the same voice_counter crossings — no separate state. */
    int16_t            gauss_ring_l[4];     /* [0]=oldest..[3]=newest */
    int16_t            gauss_ring_r[4];
    uint8_t            gauss_ring_pos;      /* write position in ring (0..3) */
    uint8_t            gauss_out_pos;       /* read position in decoded buf */

    /* -----------------------------------------------------------------
     * Phase 7 (DAC-INT / Mixer): send/return mixer state.
     * Six Q15 faders/sends, latency compensation delay buffer,
     * DAC section toggles and module state.
     *
     * Signal flow (D-01): input_gain -> bus split -> dry/patina buses
     * -> reverb sends -> reverb -> three-fader master mixer -> DAC
     * section -> output.
     *
     * All Q15 int16 faders/sends default to 0x0000 (silence) per
     * zero-init convention. Hosts MUST set fader values before
     * expecting audio output (mixer console metaphor).
     * ----------------------------------------------------------------- */

    /* Mixer controls -- Q15 int16, range [0x0000, 0x7FFF] (D-05) */
    int16_t        input_gain;        /* applied before bus split */
    int16_t        dry_fader;         /* dry bus level at master mixer */
    int16_t        patina_fader;      /* patina (ADPCM) bus level at master mixer */
    int16_t        dry_send;          /* dry bus -> reverb send level */
    int16_t        patina_send;       /* patina bus -> reverb send level */
    int16_t        reverb_fader;      /* reverb return level at master mixer */
    int16_t        sampler_fader;     /* voice engine level at master mixer */
    int16_t        sampler_send;      /* voice engine -> reverb send level */

    /* Latency compensation (D-07, D-08)
     *
     * Two stages, both gated by `latency_comp`:
     *
     * Stage A (28-sample, ADPCM-only): delay dry bus to match the ADPCM
     *   block delay so dry and patina enter the reverb send chain
     *   time-aligned (prevents reverb-send comb-filtering).
     *
     * Stage B (58-sample, FIR-match): delay dry and patina at the master
     *   mix stage to align with the FIR reverb chain's group delay
     *   (SPU94_LATENCY_SAMPLES = 58). Active whenever latency_comp is on,
     *   regardless of ADPCM. Without this, the dry/patina contributions
     *   to the master mix arrive 58 samples ahead of the reverb tail,
     *   producing transient smearing in dry+reverb mixes and breaking
     *   host PDC for passthrough configurations (PLUG-15 finding).
     */
    uint8_t        latency_comp;      /* 1=on (D-07 default), 0=off */
    uint8_t        delay_pos;         /* Stage A ring write pos, 0..27 */
    int16_t        delay_buf_l[28];   /* Stage A 28-sample delay, L */
    int16_t        delay_buf_r[28];   /* Stage A 28-sample delay, R */
    uint8_t        fir_lc_pos;        /* Stage B ring write pos, 0..57 */
    int16_t        fir_lc_dry_buf_l[58]; /* Stage B 58-sample dry delay, L */
    int16_t        fir_lc_dry_buf_r[58]; /* Stage B 58-sample dry delay, R */
    int16_t        fir_lc_pat_buf_l[58]; /* Stage B 58-sample patina delay, L */
    int16_t        fir_lc_pat_buf_r[58]; /* Stage B 58-sample patina delay, R */

    /* DAC section (D-09 through D-12) */
    uint8_t        dac_enabled;       /* master toggle, 0=off (default) */
    uint8_t        dac_fir_enabled;   /* FIR sub-toggle, 0=off (default) */
    uint8_t        dac_noise_enabled; /* noise sub-toggle, 0=off (default) */
    uint8_t        dac_true_oversample; /* 0=v1.2 approx, 1=v1.3 true 8x (Phase 11 CMP-01) */
    spu94_dac_fir_state   dac_fir_l;  /* FIR state, L channel */
    spu94_dac_fir_state   dac_fir_r;  /* FIR state, R channel */
    spu94_dac_noise_state dac_noise_l;/* noise state, L channel */
    spu94_dac_noise_state dac_noise_r;/* noise state, R channel */

    /* -----------------------------------------------------------------
     * ADR-0023 (Step 4 of M1 close-out): observable error counters.
     *
     * oob_tap_count counts every reverb-body halfword access (read or
     * write) whose computed byte offset lies outside [0, work_buf_size).
     * The reverb body fails-safe on such accesses (read returns 0,
     * write is discarded); the counter surfaces the occurrence so
     * callers and test harnesses can assert "zero OOB" as a correctness
     * invariant. Zeroed by spu94_reset (byte-loop covers).
     *
     * Sized uint64 so an extended pathological session cannot overflow the
     * counter inside the life of a single process (at a generous 10^9
     * OOB/sec, 2^64 counts take ~500 years).
     *
     * Append-only: new counters join this block AT THE TAIL to preserve
     * struct-offset stability for the fuzz harnesses (same "D-17 byte-
     * offset audit concession" as reverb_out_l/r above).
     * ----------------------------------------------------------------- */
    uint64_t       oob_tap_count;

    /* -----------------------------------------------------------------
     * Phase 18: per-sample register slewing for smooth morph.
     * spu94_set_slew_targets() loads target values and computes
     * Bresenham error state. spu94_process calls spu94_slew_tick()
     * once per sample, stepping each register by ±1 proportionally
     * so all registers converge at the same instant (no stereo skew).
     * ----------------------------------------------------------------- */
    int16_t        slew_target[SPU94_REG__COUNT];
    float          slew_frac[SPU94_REG__COUNT];   /* fractional register positions */
    int32_t        slew_abs_delta[SPU94_REG__COUNT];
    int32_t        slew_max_delta;
    int32_t        slew_samples_remaining;
    uint8_t        slew_active;

    /* Cubic ease-out slew. slew_start_frac records each register's value
     * at the moment the slew was armed; slew_total_samples is the original
     * sample budget. Each tick computes t = elapsed/total and applies
     * s(t) = 1 - (1-t)^3, then sets
     *   slew_frac[r] = start_frac[r] + s(t) * (target_f - start_frac[r])
     * Curve choice rationale documented in spu94_slew.c. */
    float          slew_start_frac[SPU94_REG__COUNT];
    int32_t        slew_total_samples;

    /* Morph Grit (binary):
     *   SPU94_GRIT_INT   (0, default) -- all reverb-body reads use integer
     *     halfword addressing. Faithful to PS1 hardware (no fractional
     *     addressing exists in the original silicon). Halfword stepping
     *     during morph excites the comb network's modes, producing the
     *     project's "alive" character.
     *   SPU94_GRIT_FRACT (1) -- all reads use linear interpolation between
     *     adjacent halfwords. Smoothes morph transitions by reading
     *     between integer halfword positions; the +-0.5 read/write
     *     mismatch in feedback loops also produces a textured patina. */
    uint8_t        morph_grit;

    /* User-programmable waypoints: 8 slots at the midpoints between Sony's
     * 9 anchors (positions 1/16, 3/16, ..., 15/16). When user_slot_filled[k]
     * is non-zero, the morph engine treats position (2k+1)/16 as a real
     * waypoint with registers from user_slots[k]. Empty slots are transparent:
     * interpolation passes through them as if they did not exist, so a fresh
     * project's audio is bit-identical to the v1.5 Sony-only path. */
    int16_t        user_slots[8][SPU94_REG__COUNT];
    uint8_t        user_slot_filled[8];
};

/* Pin the shell-type bounds. A future plan that grows the struct past these
 * limits fails the build; the fix is an intentional macro bump in spu94.h. */
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");

/* Phase 18: per-sample slew tick — called from spu94_process */
void spu94_slew_tick(spu94_state *state);

#endif /* SPU94_STATE_INTERNAL_H */
