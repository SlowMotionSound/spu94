/* include/spu94/spu94_dac_noise.h -- Phase 6 Plan 02, updated Phase 11
 *
 * Public API for the AK4309 delta-sigma noise model. Generates shaped
 * noise matching the AK4309's 2nd-order noise transfer function
 * (1 - z^-1)^2, producing a +12 dB/octave rising spectrum calibrated
 * to approximately -90 dB RMS in the audio band.
 *
 * Phase 11 addition: spu94_dac_noise_step_8x() generates noise at the
 * 352.8kHz oversampled rate using DAC_NOISE_SHIFT_8X instead of
 * DAC_NOISE_SHIFT. Called by spu94_dac_fir_step_8x_with_noise at each
 * Stage 3 evaluation for spectrally shaped noise through the cascade.
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

/* Generate one shaped noise sample at 352.8kHz amplitude calibration.
 * Uses DAC_NOISE_SHIFT_8X instead of DAC_NOISE_SHIFT. Called by
 * spu94_dac_fir_step_8x_with_noise at each of the 8 Stage 3 evaluations.
 * The FIR cascade lowpass-shapes the noise on the way down to 44.1kHz,
 * producing the characteristic oversampling DAC noise floor. */
int16_t spu94_dac_noise_step_8x(spu94_dac_noise_state *state);

/* Initialize noise state with a caller-provided LFSR seed.
 * MUST be called instead of memset for this module (see warning above).
 * seed MUST be non-zero -- zero is an absorbing state (silence forever).
 * If seed is zero, falls back to default 0xACE1u.
 * Use different seeds per channel to decorrelate L/R noise (WR-02). */
void spu94_dac_noise_init(spu94_dac_noise_state *state, uint32_t seed);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_DAC_NOISE_H */
