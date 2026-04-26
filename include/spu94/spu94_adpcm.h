#ifndef SPU94_ADPCM_H
#define SPU94_ADPCM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADPCM decoder state: two previous decoded samples.
 * Caller allocates and zero-initializes for a new stream.
 * State carries across block boundaries within a stream. */
typedef struct {
    int16_t old;    /* previous decoded sample */
    int16_t older;  /* sample before previous */
} spu94_adpcm_state;

/* 5 fixed SPU ADPCM filter coefficient pairs.
 * f0 (positive): {0, 60, 115, 98, 122}
 * f1 (negative): {0,  0, -52, -55, -60}
 * Defined in spu94_adpcm.c. Exposed for test/diagnostic use. */
extern const int16_t spu94_adpcm_f0[5];
extern const int16_t spu94_adpcm_f1[5];

/* Samples per ADPCM block */
#define SPU94_ADPCM_BLOCK_SAMPLES  28
/* Bytes per ADPCM block */
#define SPU94_ADPCM_BLOCK_BYTES    16

/* Decode one 16-byte ADPCM block into 28 int16_t samples.
 *
 * state: caller-allocated, zero-init for new stream. Updated in-place.
 * block: 16-byte ADPCM block (byte 0 = shift|filter, byte 1 = flags,
 *        bytes 2-15 = 14 data bytes with 2 nibbles each).
 * out:   28 decoded int16_t samples.
 *
 * Returns the flag byte (block[1]) for caller to handle loop logic.
 *
 * Arithmetic: (old*f0 + older*f1 + 32) >> 6 (ASR, per ADR-0001 discipline).
 * Shift 13-15 mapped to 9. Filter 5-7 clamped to 4. Clamp to int16 after
 * full prediction. Nibble order: low nibble first within each data byte. */
uint8_t spu94_adpcm_decode_block(
    spu94_adpcm_state *state,
    const uint8_t      block[SPU94_ADPCM_BLOCK_BYTES],
    int16_t            out[SPU94_ADPCM_BLOCK_SAMPLES]
);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_ADPCM_H */
