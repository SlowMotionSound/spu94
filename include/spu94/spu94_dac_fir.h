/* include/spu94/spu94_dac_fir.h -- Phase 6 Plan 01
 *
 * Public API for the AK4309 interpolation filter model. Three cascaded
 * half-band FIR stages operating at 44.1 kHz in Q15 fixed-point. Reproduces
 * the top-octave passband ripple character of the AK4309 DAC's digital
 * interpolation stage -- the audible artifact that distinguishes PS1 audio
 * from a transparent DAC.
 *
 * Mono API -- caller invokes once per channel, each channel getting its own
 * state struct instance. Follows the ADPCM per-channel state precedent
 * (see include/spu94/spu94_adpcm.h, 06-RESEARCH Open Question 2).
 *
 * Coefficient source: tools/dac_filter_design.py --export-c (Phase 5).
 * Accumulator width proofs: see src/spu94/spu94_dac_fir.c comment blocks.
 */
#ifndef SPU94_DAC_FIR_H
#define SPU94_DAC_FIR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t stage1_delay[55];
    uint8_t stage1_idx;
    int16_t stage2_delay[11];
    uint8_t stage2_idx;
    int16_t stage3_delay[7];
    uint8_t stage3_idx;
} spu94_dac_fir_state;

/* Process one 44.1kHz Q15 sample through the three-stage cascade.
 * Returns the filtered sample. State carries across calls.
 * Mono API -- caller invokes once per channel (per Phase 6 RESEARCH
 * Open Question 2). */
int16_t spu94_dac_fir_step(spu94_dac_fir_state *state, int16_t input);

/* Zero-initialize all delay lines and indices.
 * Equivalent to memset(state, 0, sizeof(*state)). */
void spu94_dac_fir_init(spu94_dac_fir_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_DAC_FIR_H */
