/* include/spu94/spu94_dac_noise.h -- Phase 6 Plan 02
 *
 * Public API for the AK4309 delta-sigma noise model. Generates shaped
 * noise matching the AK4309's 2nd-order noise transfer function
 * (1 - z^-1)^2, producing a +12 dB/octave rising spectrum calibrated
 * to approximately -90 dB RMS in the audio band.
 *
 * Standalone module -- no spu94_state dependency. Follows the ADPCM
 * per-channel state precedent (see include/spu94/spu94_adpcm.h).
 *
 * WARNING: spu94_dac_noise_init() MUST be called before first use.
 * memset(state, 0, sizeof(*state)) is NOT equivalent -- it sets the
 * LFSR to zero, which is an absorbing state (output = silence forever).
 * This is the one exception to the project convention where zero-init
 * is correct.
 */
#ifndef SPU94_DAC_NOISE_H
#define SPU94_DAC_NOISE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t lfsr;      /* 32-bit Galois LFSR state */
    int16_t  x_prev;    /* x[n-1] for 2nd-order HP shaping */
    int16_t  x_prev2;   /* x[n-2] for 2nd-order HP shaping */
} spu94_dac_noise_state;

/* Generate one shaped noise sample at 44.1kHz.
 * Returns a Q15 noise sample with +12dB/octave spectral slope,
 * calibrated to approximately -90dB RMS in the audio band.
 *
 * WARNING: spu94_dac_noise_init() MUST be called before first use.
 * memset(state, 0, sizeof(*state)) is NOT equivalent -- it sets the
 * LFSR to zero, which is an absorbing state (output = silence forever).
 * This is the one exception to the project convention where zero-init
 * is correct. */
int16_t spu94_dac_noise_step(spu94_dac_noise_state *state);

/* Initialize noise state with a non-zero LFSR seed.
 * MUST be called instead of memset for this module (see warning above). */
void spu94_dac_noise_init(spu94_dac_noise_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_DAC_NOISE_H */
