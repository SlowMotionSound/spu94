/* VAG file format reader/writer for SPU-94.
 *
 * Phase 3, Plan 01: VAG header parse/write with explicit big-endian
 * byte-order conversion. No ntohl/htonl (per ADPCM-IO-03). No heap
 * allocation (per D-08). Caller provides all buffers.
 */

#include <spu94/spu94_vag.h>
#include <string.h>

/* --- Big-endian byte-order helpers (shift-based, portable) --- */

static inline uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3]);
}

static inline void write_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v);
}

/* --- Public API --- */

int spu94_vag_read_header(const uint8_t buf[SPU94_VAG_HEADER_BYTES],
                          spu94_vag_header *out) {
    /* Validate magic: "VAGp" = 0x56 0x41 0x47 0x70 (T-03-01 mitigation) */
    if (buf[0] != 'V' || buf[1] != 'A' || buf[2] != 'G' || buf[3] != 'p')
        return -1;

    out->version     = read_be32(buf + 0x04);
    out->data_size   = read_be32(buf + 0x0C);
    out->sample_rate = read_be32(buf + 0x10);

    memcpy(out->name, buf + 0x20, 16);
    out->name[15] = '\0';  /* T-03-03: force null-terminate to prevent over-read */

    return 0;
}

void spu94_vag_write_header(uint8_t buf[SPU94_VAG_HEADER_BYTES],
                            uint32_t data_size,
                            uint32_t sample_rate,
                            const char *name) {
    memset(buf, 0, SPU94_VAG_HEADER_BYTES);

    /* Magic */
    buf[0] = 'V'; buf[1] = 'A'; buf[2] = 'G'; buf[3] = 'p';

    /* Version 2 (per ADPCM-IO-04) */
    write_be32(buf + 0x04, 2);

    /* Data size and sample rate */
    write_be32(buf + 0x0C, data_size);
    write_be32(buf + 0x10, sample_rate);

    /* Name (up to 15 chars, null-terminated) */
    if (name) {
        size_t len = strlen(name);
        if (len > 15) len = 15;
        memcpy(buf + 0x20, name, len);
    }
}
