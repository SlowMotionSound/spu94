/* include/spu94/spu94_spu_ram.h
 * Phase 27: SPU RAM sizing contract for the v1.8 PSX Voice Engine.
 *
 * Memory layout (v1.8 deliberate deviation from real PS1 hardware):
 *   Real PS1: One 512 KB address space shared between voice ADPCM sample
 *   data (low addresses) and reverb delay lines (high addresses, anchored
 *   by mBASE). Address collision is the caller's problem.
 *
 *   SPU-94 v1.8: Two SEPARATE 512 KB buffers:
 *     - Voice RAM: holds pre-encoded ADPCM sample data for 24 voices.
 *       Caller manages allocation within this buffer via byte offsets.
 *     - Reverb work buffer: existing spu94_state.work_buf, unchanged.
 *       The reverb engine uses mBASE-relative addressing into this buffer.
 *
 *   This deliberate separation (C6 / RAM-01) eliminates address-space
 *   collision risk at the cost of doubling the nominal RAM budget.
 *   Acceptable for a software implementation targeting desktop/plugin hosts.
 */
#ifndef SPU94_SPU_RAM_H
#define SPU94_SPU_RAM_H

/* Voice RAM size: 512 KB, matching the PS1's total SPU RAM capacity.
 * This is the maximum size of the voice ADPCM sample buffer.
 * Separate from SPU94_WORK_BUF_MAX_BYTES (reverb work buffer). */
#define SPU94_SPU_RAM_BYTES  0x80000u

#endif /* SPU94_SPU_RAM_H */
