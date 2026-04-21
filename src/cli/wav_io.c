/* Phase 6 Plan 3 Task 1: Wave-0 stub for WAV I/O.
 *
 * This TU owns the single DR_WAV_IMPLEMENTATION expansion — dr_wav.h is
 * otherwise header-only and safe to #include into a neighbouring TU
 * without triggering multiple-definition errors. Task 2 replaces the
 * stub bodies with real drwav_init_file / drwav_read_pcm_frames_s16 /
 * drwav_init_file_write calls.
 */
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include "wav_io.h"

#include <stdio.h>
#include <string.h>

int spu94_cli_wav_load(const char *path, spu94_cli_wav_t *wav,
                      char *err_buf, size_t err_buf_size) {
    (void)path;
    if (wav) {
        memset(wav, 0, sizeof *wav);
    }
    if (err_buf && err_buf_size) {
        snprintf(err_buf, err_buf_size, "WAV loader not implemented (Wave 0 stub)");
    }
    return 1;
}

int spu94_cli_wav_write(const char *path,
                       const int16_t *L, const int16_t *R,
                       uint64_t num_frames, uint32_t sample_rate,
                       char *err_buf, size_t err_buf_size) {
    (void)path; (void)L; (void)R;
    (void)num_frames; (void)sample_rate;
    if (err_buf && err_buf_size) {
        snprintf(err_buf, err_buf_size, "WAV writer not implemented (Wave 0 stub)");
    }
    return 1;
}
