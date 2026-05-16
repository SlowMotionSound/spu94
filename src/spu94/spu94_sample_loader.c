/* src/spu94/spu94_sample_loader.c
 * Phase 27: Off-hot-path WAV-to-ADPCM encoder for voice RAM loading.
 *
 * RT-safety note: This function is NEVER called from spu94_process or
 * spu94_voice_tick. It is a load-time function only (C1, S6). No heap,
 * no syscalls, no locks — but the brute-force encoder IS CPU-intensive.
 */

#include <spu94/spu94_sample_loader.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

int32_t spu94_sample_encode_to_ram(
    const int16_t *pcm, uint32_t num_samples,
    uint8_t *voice_ram, uint32_t ram_offset, uint32_t ram_size,
    int loop_enable)
{
    if (pcm == NULL || voice_ram == NULL || num_samples == 0)
        return -1;

    /* Compute number of ADPCM blocks needed (ceiling division) */
    uint32_t num_blocks = (num_samples + 27u) / 28u;
    uint32_t bytes_needed = num_blocks * SPU94_ADPCM_BLOCK_BYTES;

    /* T-27-02 / RAM-03: bounds check with overflow guard.
     * Check bytes_needed <= ram_size - ram_offset, but guard against
     * underflow when ram_offset > ram_size. */
    if (ram_offset > ram_size || bytes_needed > ram_size - ram_offset)
        return -1;

    /* Initialize encoder state (zero for a fresh stream) */
    spu94_adpcm_state encode_state;
    encode_state.old = 0;
    encode_state.older = 0;

    for (uint32_t b = 0; b < num_blocks; b++) {
        /* Prepare 28-sample input block, zero-padding the partial last block */
        int16_t block_pcm[28];
        memset(block_pcm, 0, sizeof(block_pcm));

        uint32_t src_off = b * 28u;
        uint32_t count = (src_off + 28u <= num_samples) ? 28u : (num_samples - src_off);
        memcpy(block_pcm, pcm + src_off, count * sizeof(int16_t));

        int is_last = (b == num_blocks - 1) ? 1 : 0;

        /* Determine flag byte for this block:
         * - Non-last blocks: 0x00 (normal)
         * - Last block without loop: 0x01 (END)
         * - Last block with loop: 0x03 (END | REPEAT) */
        uint8_t flags = SPU94_VAG_FLAG_NORMAL;
        if (is_last) {
            flags = SPU94_VAG_FLAG_END;
            if (loop_enable) {
                flags |= SPU94_VAG_FLAG_LOOP_REPEAT;
            }
        }

        uint8_t *dest = voice_ram + ram_offset + b * SPU94_ADPCM_BLOCK_BYTES;
        spu94_adpcm_encode_block(&encode_state, block_pcm, flags, dest);
    }

    return (int32_t)bytes_needed;
}
