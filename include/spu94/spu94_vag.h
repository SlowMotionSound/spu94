#ifndef SPU94_VAG_H
#define SPU94_VAG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPU94_VAG_HEADER_BYTES 48

typedef struct {
    uint32_t version;
    uint32_t data_size;     /* bytes of ADPCM data after header (num_blocks * 16) */
    uint32_t sample_rate;
    char     name[16];      /* null-terminated ASCII, zero-padded */
} spu94_vag_header;

/* VAG block flag values relevant to SPU-94.
 *
 * Flag byte layout (block[1] of each 16-byte ADPCM block):
 *   Bit 0: END / Loop-End — last block; triggers jump to loop_start_addr
 *   Bit 1: REPEAT — if set alongside bit 0, voice keeps playing after loop jump;
 *          if clear alongside bit 0, voice mutes (one-shot termination)
 *   Bit 2: LOOP_START — auto-latch this block's address as loop_start_addr
 *
 * Note: SPU94_VAG_FLAG_END (0x01) serves double duty as both "end of sample"
 * and "loop end jump trigger" — they are the same bit. */
#define SPU94_VAG_FLAG_NORMAL       0x00
#define SPU94_VAG_FLAG_END          0x01  /* bit 0: end / loop-end */
#define SPU94_VAG_FLAG_LOOP_REPEAT  0x02  /* bit 1: keep playing after loop end */
#define SPU94_VAG_FLAG_LOOP_START   0x04  /* bit 2: auto-latch loop start addr */
#define SPU94_VAG_FLAG_TERMINATOR   0x07

/* Read and validate a 48-byte VAG header from buf.
 * Returns 0 on success, -1 if magic != "VAGp".
 * Accepts any version value on read (per ADPCM-IO-03).
 * Per D-08: operates on caller-provided buffer, zero heap. */
int spu94_vag_read_header(const uint8_t buf[SPU94_VAG_HEADER_BYTES],
                          spu94_vag_header *out);

/* Write a 48-byte VAG v2 header into buf.
 * Per D-08: caller provides the buffer. Zero heap.
 * version is always 2 on write (per ADPCM-IO-04). */
void spu94_vag_write_header(uint8_t buf[SPU94_VAG_HEADER_BYTES],
                            uint32_t data_size,
                            uint32_t sample_rate,
                            const char *name);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_VAG_H */
