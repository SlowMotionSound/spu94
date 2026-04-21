#ifndef SPU94_CLI_WAV_IO_H
#define SPU94_CLI_WAV_IO_H

#include <stdint.h>
#include <stddef.h>

/* Phase 6 Plan 3: WAV I/O helpers for the spu94 CLI.
 *
 * Thin wrapper over dr_wav (`vendor/dr_wav/dr_wav.h`) so `main.c` never
 * touches dr_wav types directly. Planar int16 L/R buffers match the
 * spu94_process / spu94_flush signatures exactly — no interleaving
 * round-trips in the hot path. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;   /* 44100 (enforced) */
    uint32_t num_channels;  /* 2 (enforced) */
    uint64_t num_frames;    /* stereo frame count */
    int16_t *L;             /* planar left — malloc'd by loader, caller frees */
    int16_t *R;             /* planar right — malloc'd by loader, caller frees */
} spu94_cli_wav_t;

/* Load a WAV file as planar int16 stereo at 44.1 kHz.
 * Returns 0 on success; non-zero on error.
 * On success, caller owns wav->L / wav->R and must free() them.
 * On error, wav is zeroed and err_buf receives a single-line human message
 * (no `spu94: error:` prefix; main.c adds that). */
int spu94_cli_wav_load(const char *path, spu94_cli_wav_t *wav,
                      char *err_buf, size_t err_buf_size);

/* Write planar int16 stereo to a 44.1 kHz PCM WAV file.
 * Returns 0 on success, non-zero on error.
 * Errors populate err_buf (single-line message). */
int spu94_cli_wav_write(const char *path,
                       const int16_t *L, const int16_t *R,
                       uint64_t num_frames, uint32_t sample_rate,
                       char *err_buf, size_t err_buf_size);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_CLI_WAV_IO_H */
