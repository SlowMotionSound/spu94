/* src/spu94/spu94_state_internal.h
 *
 * INTERNAL header for libspu94 — not installed, never on the public include
 * path. Holds the layout of struct spu94_state so that every Phase-2 internal
 * translation unit (spu94_state.c, spu94_register_io.c, spu94_write_policy.c,
 * spu94_pending.c, spu94_tick.c, spu94_registers.c) can manipulate the same
 * fields without re-defining the struct. Re-defining the struct in multiple
 * TUs is ODR-unsafe — this header is the single home.
 *
 * Path: src/spu94/spu94_state_internal.h. Sub-headers under src/ are reachable
 * via "spu94_state_internal.h" from any TU in src/spu94/.
 *
 * NOTE: This header is NEVER referenced from anything under include/spu94/.
 * Public callers see only the opaque forward declaration in spu94_registers.h.
 */
#ifndef SPU94_STATE_INTERNAL_H
#define SPU94_STATE_INTERNAL_H

#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>

struct spu94_state {
    /* Caller-provided work buffer (D-13). Not released by the library. */
    unsigned char *work_buf;
    size_t         work_buf_size;

    /* BufferAddress — byte offset into the reverb work buffer. Per
     * 02-RESEARCH.md A4: mBASE = 0 at init, snap-on-write makes addr = 0. */
    uint32_t       buffer_address;

    /* Active register values (Plan 03 populates via spu94_set_reg_*). */
    int16_t        reg_values[SPU94_REG__COUNT];

    /* Shadow register values for the TICK_LATCHED policy (Plan 03). At tick
     * start, every bit set in pending_mask triggers pending_values[i] ->
     * reg_values[i] and the bit is cleared. For IMMEDIATE-policy registers,
     * pending_values[] is kept in sync with reg_values[] so the
     * spu94_get_reg_*_pending readback always returns a meaningful value. */
    int16_t        pending_values[SPU94_REG__COUNT];
    uint64_t       pending_mask;  /* bit i set -> pending_values[i] awaits flush */
};

/* Pin the shell-type bounds. A future plan that grows the struct past these
 * limits fails the build; the fix is an intentional macro bump in spu94.h. */
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");

#endif /* SPU94_STATE_INTERNAL_H */
