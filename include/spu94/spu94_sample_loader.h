/* include/spu94/spu94_sample_loader.h
 * Phase 27: Off-hot-path WAV-to-ADPCM encoder for loading samples into
 * the dedicated voice RAM buffer.
 *
 * This function is NEVER called from spu94_process or spu94_voice_tick.
 * It is a load-time function only (C1, S6). No heap, no syscalls, but
 * it IS computationally expensive (brute-force encoder) — call from a
 * non-audio thread or during initialization.
 */
#ifndef SPU94_SAMPLE_LOADER_H
#define SPU94_SAMPLE_LOADER_H

#include <stdint.h>
#include <spu94/spu94_spu_ram.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Encode a mono int16 PCM buffer into ADPCM blocks and write into voice_ram.
 *
 * pcm:         input samples (mono; stereo = two calls with different ram_offset).
 * num_samples: number of int16 samples in pcm.
 * voice_ram:   destination byte array (caller-owned, SPU94_SPU_RAM_BYTES).
 * ram_offset:  byte offset in voice_ram where encoded blocks begin.
 * ram_size:    total size of voice_ram in bytes (bounds check limit).
 * loop_enable: if 1, final block flag = 0x03 (END|REPEAT); if 0, flag = 0x01 (END).
 *
 * Returns: number of bytes written (>= 0), or -1 on error (NULL input, overflow).
 *
 * Bounds check (T-27-02 / RAM-03): returns -1 if ram_offset + encoded_size
 * would exceed ram_size.
 *
 * RT-safety: NOT called from the audio callback. Uses spu94_adpcm_encode_block
 * which is computationally expensive but heap-free. */
int32_t spu94_sample_encode_to_ram(
    const int16_t *pcm, uint32_t num_samples,
    uint8_t *voice_ram, uint32_t ram_offset, uint32_t ram_size,
    int loop_enable);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_SAMPLE_LOADER_H */
