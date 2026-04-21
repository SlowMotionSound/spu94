/* Phase 6 Plan 3 Task 2: WAV I/O (full dr_wav implementation).
 *
 * This TU owns the single DR_WAV_IMPLEMENTATION expansion; any other
 * CLI TU that wanted to use dr_wav could include <dr_wav.h> without
 * redefining the symbol — we do not need that yet. The wrapper keeps
 * dr_wav's types (drwav, drwav_data_format, drwav_uint64) firmly
 * inside this TU; main.c sees only the planar int16 `spu94_cli_wav_t`.
 *
 * Error discipline (D-05): single-line messages written to err_buf
 * WITHOUT the `spu94: error:` prefix — main.c adds that so every error
 * path flows through a single format site.
 */
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include "wav_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int spu94_cli_wav_load(const char *path, spu94_cli_wav_t *wav,
                      char *err_buf, size_t err_buf_size) {
    if (wav) {
        memset(wav, 0, sizeof *wav);
    }
    if (!path || !wav) {
        if (err_buf && err_buf_size) {
            snprintf(err_buf, err_buf_size, "internal error: NULL argument to wav_load");
        }
        return 1;
    }

    /* Pre-check file existence so "not found" is distinguishable from
     * "malformed WAV" in the user-visible error message. dr_wav's
     * drwav_init_file returns the same DRWAV_FALSE for both. */
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        snprintf(err_buf, err_buf_size, "input file '%s' not found", path);
        return 1;
    }
    fclose(fp);

    drwav dw;
    if (!drwav_init_file(&dw, path, NULL)) {
        snprintf(err_buf, err_buf_size,
                 "could not read WAV file '%s' (malformed header?)", path);
        return 1;
    }

    if (dw.channels != 2) {
        snprintf(err_buf, err_buf_size,
                 "WAV file '%s' has %u channels; stereo (2 channels) required",
                 path, (unsigned)dw.channels);
        drwav_uninit(&dw);
        return 1;
    }
    if (dw.sampleRate != 44100) {
        snprintf(err_buf, err_buf_size,
                 "WAV file '%s' has sample rate %u Hz; 44100 Hz required",
                 path, (unsigned)dw.sampleRate);
        drwav_uninit(&dw);
        return 1;
    }

    wav->sample_rate  = dw.sampleRate;
    wav->num_channels = dw.channels;
    wav->num_frames   = dw.totalPCMFrameCount;

    /* Read as interleaved int16 then deinterleave into planar L/R. The
     * SPU-94 public API uses planar buffers (D-01), so the deinterleave
     * lives in the CLI (application concern) not the library. */
    if (wav->num_frames == 0) {
        snprintf(err_buf, err_buf_size,
                 "WAV file '%s' has zero frames (empty audio)", path);
        drwav_uninit(&dw);
        return 1;
    }

    size_t interleaved_count = (size_t)wav->num_frames * wav->num_channels;
    int16_t *interleaved = (int16_t *)malloc(interleaved_count * sizeof(int16_t));
    if (!interleaved) {
        drwav_uninit(&dw);
        snprintf(err_buf, err_buf_size, "out of memory loading '%s'", path);
        return 1;
    }

    drwav_uint64 got = drwav_read_pcm_frames_s16(&dw, wav->num_frames, interleaved);
    drwav_uninit(&dw);
    if (got != wav->num_frames) {
        free(interleaved);
        snprintf(err_buf, err_buf_size,
                 "failed to read all frames from '%s' (got %llu of %llu)",
                 path,
                 (unsigned long long)got,
                 (unsigned long long)wav->num_frames);
        return 1;
    }

    wav->L = (int16_t *)malloc((size_t)wav->num_frames * sizeof(int16_t));
    wav->R = (int16_t *)malloc((size_t)wav->num_frames * sizeof(int16_t));
    if (!wav->L || !wav->R) {
        free(wav->L); free(wav->R); free(interleaved);
        wav->L = NULL; wav->R = NULL;
        snprintf(err_buf, err_buf_size, "out of memory deinterleaving '%s'", path);
        return 1;
    }
    for (uint64_t i = 0; i < wav->num_frames; ++i) {
        wav->L[i] = interleaved[i * 2u + 0u];
        wav->R[i] = interleaved[i * 2u + 1u];
    }
    free(interleaved);
    return 0;
}

int spu94_cli_wav_write(const char *path,
                       const int16_t *L, const int16_t *R,
                       uint64_t num_frames, uint32_t sample_rate,
                       char *err_buf, size_t err_buf_size) {
    if (!path || !L || !R) {
        if (err_buf && err_buf_size) {
            snprintf(err_buf, err_buf_size, "internal error: NULL argument to wav_write");
        }
        return 1;
    }

    drwav_data_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.container     = drwav_container_riff;
    fmt.format        = DR_WAVE_FORMAT_PCM;
    fmt.channels      = 2;
    fmt.sampleRate    = sample_rate;
    fmt.bitsPerSample = 16;

    drwav dw;
    if (!drwav_init_file_write(&dw, path, &fmt, NULL)) {
        snprintf(err_buf, err_buf_size,
                 "could not open '%s' for writing (permission denied?)", path);
        return 1;
    }

    /* Interleave + write in blocks to cap peak memory at 4096 * 2 * 2 bytes
     * = 16 KB regardless of output length. */
    enum { BLOCK = 4096u };
    int16_t interleaved[BLOCK * 2];
    for (uint64_t i = 0; i < num_frames; i += BLOCK) {
        uint64_t remaining = num_frames - i;
        uint64_t block = (remaining < (uint64_t)BLOCK) ? remaining : (uint64_t)BLOCK;
        for (uint64_t j = 0; j < block; ++j) {
            interleaved[j * 2u + 0u] = L[i + j];
            interleaved[j * 2u + 1u] = R[i + j];
        }
        drwav_uint64 written = drwav_write_pcm_frames(&dw, block, interleaved);
        if (written != block) {
            drwav_uninit(&dw);
            snprintf(err_buf, err_buf_size,
                     "failed to write all frames to '%s' (disk full?)", path);
            return 1;
        }
    }

    drwav_uninit(&dw);
    return 0;
}
