/* src/spu94/spu94_adpcm.c
 * M2 Phase 1 Plan 01 Task 1: PS1 SPU ADPCM decoder implementation.
 *
 * Decodes one 16-byte ADPCM block into 28 int16_t PCM samples using the
 * five fixed Sony filter coefficient pairs. All arithmetic is integer-only,
 * using ASR (>> 6) per ADR-0001 discipline. Zero heap. No spu94_state
 * dependency -- the decoder is a standalone module with its own 4-byte state.
 *
 * Key hardware-faithful behaviors:
 *   - Shift values 13-15 map to shift 9 (psx-spx documented behavior)
 *   - Filter indices 5-7 clamp to filter 4 (emulator consensus)
 *   - Nibble ordering: low nibble first within each data byte
 *   - Sign extension: nibbles >= 8 sign-extend to negative (-8..-1)
 *   - Prediction uses reconstructed (clamped) samples, not pre-clamp values
 *   - Rounding bias: +32 before >>6 (hardware-faithful)
 */

#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_q15.h>

/* ------------------------------------------------------------------------- */
/* Filter coefficient tables                                                 */
/* ------------------------------------------------------------------------- */

const int16_t spu94_adpcm_f0[5] = {  0, 60, 115,  98, 122 };
const int16_t spu94_adpcm_f1[5] = {  0,  0, -52, -55, -60 };

/* ------------------------------------------------------------------------- */
/* Block decoder                                                             */
/* ------------------------------------------------------------------------- */

uint8_t spu94_adpcm_decode_block(
    spu94_adpcm_state *state,
    const uint8_t      block[SPU94_ADPCM_BLOCK_BYTES],
    int16_t            out[SPU94_ADPCM_BLOCK_SAMPLES])
{
    /* Parse header byte: low nibble = shift, high nibble = filter index. */
    int shift  = block[0] & 0x0F;
    int filter = (block[0] >> 4) & 0x07;

    /* Clamp out-of-range shift/filter per hardware behavior. */
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 4;

    /* Look up filter coefficients. */
    int32_t f0 = spu94_adpcm_f0[filter];
    int32_t f1 = spu94_adpcm_f1[filter];

    /* Load state into locals for the decode loop. */
    int32_t old   = state->old;
    int32_t older = state->older;

    /* Decode 14 data bytes (bytes 2-15), 2 nibbles each = 28 samples. */
    for (int i = 0; i < 14; i++) {
        uint8_t byte = block[2 + i];

        /* Low nibble first, then high nibble. */
        int nibbles[2];
        nibbles[0] = byte & 0x0F;
        nibbles[1] = (byte >> 4) & 0x0F;

        for (int n = 0; n < 2; n++) {
            /* Sign-extend 4-bit nibble to signed range -8..+7. */
            int nib = nibbles[n];
            if (nib >= 8) nib -= 16;

            /* Shift: place the nibble at the appropriate bit position. */
            int32_t shifted = (int32_t)nib << (12 - shift);

            /* Predict from previous reconstructed samples (ASR, +32 bias). */
            int32_t predicted = (old * f0 + older * f1 + 32) >> 6;

            /* Sum and clamp to int16 range. */
            int32_t sample = shifted + predicted;
            int16_t clamped = sat_s16(sample);

            /* Store output sample. */
            out[i * 2 + n] = clamped;

            /* Update prediction state with the reconstructed (clamped) value. */
            older = old;
            old = (int32_t)clamped;
        }
    }

    /* Write back state for cross-block continuity. */
    state->old   = (int16_t)old;
    state->older = (int16_t)older;

    /* Return the flag byte for caller to handle loop/end markers. */
    return block[1];
}
