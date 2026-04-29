/* src/spu94/spu94_dac_noise.c -- Phase 6 Plan 02
 *
 * AK4309 delta-sigma noise model.
 *
 * The AK4309 is a 1-bit delta-sigma DAC whose quantization noise has a
 * characteristic rising spectral shape from the 2nd-order noise transfer
 * function (NTF). Rather than simulate the full delta-sigma modulator at
 * 352.8 kHz (384x oversampling -- 16.9 MHz processing for zero audible
 * benefit), we model the EFFECT: the shaped noise spectrum as it appears
 * in the audio band at 44.1 kHz.
 *
 * LFSR white noise source:
 *   Polynomial: x^32 + x^22 + x^2 + x^1 + 1 (Galois form)
 *   Feedback mask: 0x80200003
 *   Period: 2^32 - 1 = 4,294,967,295 (~27 hours at 44.1 kHz)
 *
 * 2nd-order highpass shaping:
 *   y[n] = x[n] - 2*x[n-1] + x[n-2]
 *   Equivalent to NTF(z) = (1 - z^-1)^2
 *   Produces +12 dB/octave spectral slope
 *
 * Amplitude target:
 *   ~-90 dB SNR per AK4309B datasheet (D-04). Paper target -- the datasheet
 *   number includes analog stages we don't model. Placeholder for M5 hardware
 *   calibration (D-05). The shaping character matters more than the absolute
 *   amplitude (D-06).
 *
 * Reference: .planning/research/DEEP-DELTA-SIGMA.md
 */
#include <spu94/spu94_dac_noise.h>
#include <spu94/spu94_q15.h>
#include <string.h>

/* LFSR polynomial: x^32 + x^22 + x^2 + x^1 + 1 (Galois form).
 * Maximal-length: period = 2^32 - 1 = 4,294,967,295.
 * At 44.1 kHz, repeats every ~27 hours -- effectively infinite. */
#define DAC_NOISE_LFSR_FEEDBACK  0x80200003u
#define DAC_NOISE_LFSR_SEED      0xACE1u  /* arbitrary non-zero seed */

/* Amplitude scaling: right-shift raw LFSR output to target ~-90 dB RMS
 * in the audio band after 2nd-order HP shaping. This is a compile-time
 * constant, tunable per D-06 ("treat the level as tunable later").
 * Derivation: RESEARCH.md Noise Amplitude Derivation section.
 * Validated by tests/unit/dac_noise/test_dac_noise_amplitude.c. */
#define DAC_NOISE_SHIFT  14

void spu94_dac_noise_init(spu94_dac_noise_state *state, uint32_t seed) {
    memset(state, 0, sizeof(*state));
    state->lfsr = seed ? seed : DAC_NOISE_LFSR_SEED;
}

int16_t spu94_dac_noise_step(spu94_dac_noise_state *state) {
    /* Step the Galois LFSR */
    uint32_t lfsr = state->lfsr;
    uint32_t bit = lfsr & 1u;
    lfsr >>= 1;
    if (bit) lfsr ^= DAC_NOISE_LFSR_FEEDBACK;
    state->lfsr = lfsr;

    /* Scale LFSR output to noise amplitude.
     * Take upper 16 bits, center around zero, then right-shift.
     * Upper bits of a Galois LFSR have better statistical properties
     * than the lower bits. */
    int16_t x = (int16_t)(((int32_t)(lfsr >> 16) - 32768) >> DAC_NOISE_SHIFT);

    /* 2nd-order highpass shaping: y[n] = x[n] - 2*x[n-1] + x[n-2]
     * This is the discrete NTF (1 - z^-1)^2 from delta-sigma theory.
     * Produces +12 dB/octave spectral slope. */
    int32_t y = (int32_t)x - 2 * (int32_t)state->x_prev + (int32_t)state->x_prev2;
    state->x_prev2 = state->x_prev;
    state->x_prev = x;

    return sat_s16(y);
}
