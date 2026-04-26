/* src/spu94/spu94_adpcm_encode.c
 * M2 Phase 1 Plan 02 Task 1: PS1 SPU ADPCM encoder implementation.
 *
 * Encodes 28 int16_t PCM samples into one 16-byte ADPCM block using
 * brute-force search over 65 (filter, shift) combinations (5 filters x
 * 13 shifts 0-12) with int64 L2 (sum of squared errors) metric.
 *
 * Critical invariant: prediction state uses RECONSTRUCTED (decoded)
 * samples, not original PCM. The encoder embeds an internal decoder
 * copy to track this correctly. Using original PCM for prediction
 * is the single most common ADPCM encoder bug.
 *
 * Key properties:
 *   - Zero heap, integer-only, no float/double
 *   - Round-to-nearest nibble quantization
 *   - shift=12 UB guard: half_step=0 when shift_amount=0
 *   - Tiebreak: lower filter index, then lower shift (iteration order)
 *   - Deterministic: same input always produces same output
 */

#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <string.h>

void spu94_adpcm_encode_block(
    spu94_adpcm_state *state,
    const int16_t      in[SPU94_ADPCM_BLOCK_SAMPLES],
    uint8_t            flags,
    uint8_t            block[SPU94_ADPCM_BLOCK_BYTES])
{
    int64_t best_error = INT64_MAX;
    spu94_adpcm_state best_state = *state;
    int best_filter = 0;
    int best_shift = 0;
    int8_t best_nibbles[28];

    for (int f = 0; f < 5; f++) {
        int16_t f0 = spu94_adpcm_f0[f];
        int16_t f1 = spu94_adpcm_f1[f];

        for (int s = 0; s <= 12; s++) {
            spu94_adpcm_state trial = *state;  /* fork from committed */
            int64_t error = 0;
            int8_t nibbles[28];
            int shift_amount = 12 - s;

            for (int i = 0; i < 28; i++) {
                /* Predict from reconstructed state */
                int32_t predicted = ((int32_t)trial.old * f0
                                   + (int32_t)trial.older * f1 + 32) >> 6;

                /* Residual: what the nibble must encode */
                int32_t residual = (int32_t)in[i] - predicted;

                /* Quantize: round-to-nearest */
                int32_t half_step = (shift_amount > 0)
                                  ? (1 << (shift_amount - 1)) : 0;
                int32_t nib = (residual + half_step) >> shift_amount;

                /* Clamp to 4-bit signed range */
                if (nib > 7)  nib = 7;
                if (nib < -8) nib = -8;
                nibbles[i] = (int8_t)nib;

                /* Internal decoder: reconstruct exactly as decoder would */
                int32_t shifted = nib << shift_amount;
                int32_t sample = shifted + predicted;
                int16_t clamped = sat_s16(sample);

                /* Accumulate squared error (int64 to avoid overflow) */
                int32_t diff = (int32_t)in[i] - (int32_t)clamped;
                error += (int64_t)diff * (int64_t)diff;

                /* Update trial state with RECONSTRUCTED sample */
                trial.older = trial.old;
                trial.old = clamped;
            }

            /* Tiebreak: lower filter, then lower shift (iteration order
             * guarantees this with strict < comparison) */
            if (error < best_error) {
                best_error = error;
                best_state = trial;
                best_filter = f;
                best_shift = s;
                memcpy(best_nibbles, nibbles, 28);
            }
        }
    }

    /* Commit winning state */
    *state = best_state;

    /* Pack output block */
    memset(block, 0, 16);
    block[0] = (uint8_t)((best_filter << 4) | (best_shift & 0x0F));
    block[1] = flags;
    for (int i = 0; i < 14; i++) {
        uint8_t lo = (uint8_t)(best_nibbles[i * 2]     & 0x0F);
        uint8_t hi = (uint8_t)(best_nibbles[i * 2 + 1] & 0x0F);
        block[2 + i] = lo | (uint8_t)(hi << 4);
    }
}
