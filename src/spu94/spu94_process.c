/* src/spu94/spu94_process.c -- Phase 5 Plan 02.
 *
 * spu94_process: 44.1 kHz int16 stereo block-based audio entry point
 *   (D-01). Planar L/R pointers (D-01); any block size N >= 1 (D-03);
 *   in-place L_out == L_in / R_out == R_in allowed (D-04).
 *
 * spu94_flush: drain trailing reverb tail by feeding internal silence
 *   (D-02). Shares the single-sample body with spu94_process.
 *
 * Mix-bus wiring (D-05): both functions write state->mix_bus_l and
 * state->mix_bus_r before each spu94_fir_chain_step call. The reverb
 * body at spu94_reverb.c reads those fields where it previously
 * hardcoded zero.
 *
 * Pitfall 4 (ADR-0005): spu94_fir_chain_step has exactly TWO call
 * sites in the public audio path -- spu94_process (non-zero inputs)
 * and spu94_flush (zero inputs). Separate entry points are
 * intentional: the "drain" concept is named API surface (D-02), not
 * a CLI implementation detail. Both paths run the same per-sample math.
 */
#include <spu94/spu94.h>
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
        const int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        const int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;
        /* D-05: populate the mailbox before the chain step. The reverb
         * body at spu94_reverb.c reads these fields at the moment
         * the reverb stage runs (once per 22.05 kHz tick = every other
         * 44.1 kHz call). For non-retained phase the write is harmless
         * (the tick doesn't fire). */
        state->mix_bus_l = l;
        state->mix_bus_r = r;
        int16_t lo = 0, ro = 0;
        spu94_fir_chain_step(state, l, r, &lo, &ro);
        if (L_out != NULL) L_out[i] = lo;
        if (R_out != NULL) R_out[i] = ro;
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
