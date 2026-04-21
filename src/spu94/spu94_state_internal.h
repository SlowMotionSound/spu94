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
};

/* Pin the shell-type bounds. A future plan that grows the struct past these
 * limits fails the build; the fix is an intentional macro bump in spu94.h. */
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");

#endif /* SPU94_STATE_INTERNAL_H */
