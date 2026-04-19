/* src/spu94/spu94_state.c
 * Phase 2 Plan 01 Task 1: spu94_state private definition + lifecycle impl.
 *
 * Invariants:
 *   - No heap allocations (API-01, API-09). State storage is caller-provided.
 *   - No libc dependencies beyond the freestanding subset. <string.h> is NOT
 *     included; zero-fills use byte loops over unsigned char * casts so the
 *     verify-no-heap-symbols.sh linker check stays clean and MCU targets
 *     without a hosted libc can link the library as-is.
 *   - _Static_assert guards pin sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX
 *     and alignof(...) <= SPU94_STATE_ALIGN_MAX; any future plan that grows
 *     the struct past these bounds fails the build rather than shipping a
 *     too-small shell-type.
 *
 * Layout notes:
 *   reg_values[35] + pending_values[35] + pending_mask are declared in Plan 01
 *   even though Plans 02/03 are what populate them. Declaring them now sizes
 *   SPU94_STATE_SIZE_MAX correctly on the first pass and means no subsequent
 *   plan needs to bump the macro for a field the chassis was already sized for.
 *   The 35 figure matches the 33-register count + vLIN + vRIN per
 *   02-RESEARCH.md § Register Inventory.
 */

#include <spu94/spu94.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Internal struct definition                                                */
/* ------------------------------------------------------------------------- */

struct spu94_state {
    /* Caller-provided work buffer (D-13). Not freed by this library. */
    unsigned char *work_buf;
    size_t         work_buf_size;

    /* BufferAddress — byte offset into the reverb work buffer.
     * Initial value per 02-RESEARCH.md § BufferAddress Arithmetic:
     *   mBASE = 0 at init; writing mBASE sets BufferAddress := mBASE,
     *   so the initial BufferAddress is also 0. The wrap formula
     *   `MAX(mBASE, (addr+2) AND 0x7FFFE)` is Plan 04's concern. */
    uint32_t       buffer_address;

    /* Active register values (Plan 02 populates; declared now to size the
     * state correctly on the first pass). 35 = 33-register canonical count
     * + vLIN + vRIN per RESEARCH.md § Register Inventory Interpretation. */
    int16_t        reg_values[35];

    /* Pending (tick-latched) register values (Plan 03). pending_mask has one
     * bit per register; at tick start, set bits indicate slots to copy
     * pending -> active. 35 bits used; upper 29 bits of the u64 reserved. */
    int16_t        pending_values[35];
    uint64_t       pending_mask;
};

/* Pin the shell-type bounds. A future plan that grows the struct past these
 * limits fails the build; the fix is an intentional macro bump in spu94.h. */
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "spu94_state grew beyond SPU94_STATE_SIZE_MAX; bump the macro in spu94.h");
_Static_assert(alignof(struct spu94_state) <= SPU94_STATE_ALIGN_MAX,
    "spu94_state alignment requirement exceeds SPU94_STATE_ALIGN_MAX");

/* ------------------------------------------------------------------------- */
/* Helpers (freestanding — no libc)                                          */
/* ------------------------------------------------------------------------- */

/* Byte-loop zero-fill. We do not include <string.h>; a hand-rolled loop keeps
 * the library clear of libc imports (verify-no-heap-symbols.sh invariant) and
 * satisfies BUILD-07 grep-guard. The compiler may vectorize this body. */
static void spu94_zero_bytes(void *dst, size_t n) {
    unsigned char *p = (unsigned char *)dst;
    for (size_t i = 0; i < n; ++i) {
        p[i] = 0u;
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

size_t spu94_state_size(void) {
    return sizeof(struct spu94_state);
}

spu94_state *spu94_init(void *state_buf, size_t state_buf_size,
                        void *work_buf,  size_t work_buf_size) {
    /* Argument validation — reject without touching caller memory (T-02-01). */
    if (state_buf == NULL) {
        return NULL;
    }
    if (state_buf_size < sizeof(struct spu94_state)) {
        return NULL;
    }
    if (((uintptr_t)state_buf) % SPU94_STATE_ALIGN_MAX != 0u) {
        return NULL;
    }
    /* NULL work_buf is only legal with zero size (don't-care configuration). */
    if (work_buf == NULL && work_buf_size > 0) {
        return NULL;
    }

    struct spu94_state *s = (struct spu94_state *)state_buf;

    /* Zero every internal field (T-02-03: stale caller bytes do not leak
     * into post-init state). Followed by invariant setup. */
    spu94_zero_bytes(s, sizeof(*s));

    s->work_buf       = (unsigned char *)work_buf;
    s->work_buf_size  = work_buf_size;
    s->buffer_address = 0u; /* mBASE = 0 at init; snap-on-write makes addr=0 */

    return s;
}

void spu94_reset(spu94_state *state) {
    if (state == NULL) {
        return;
    }

    /* Preserve caller-owned memory pointers across the reset.
     * Per 02-RESEARCH.md § State-Allocation Pattern reset contract:
     *   - zero registers, clear pending_mask, restore BufferAddress = mBASE (0)
     *   - zero the work buffer
     *   - do NOT change the work_buf pointer or size. */
    unsigned char *saved_work      = state->work_buf;
    size_t         saved_work_size = state->work_buf_size;

    /* Zero the work buffer, if present. */
    if (saved_work != NULL) {
        spu94_zero_bytes(saved_work, saved_work_size);
    }

    /* Zero every internal field, then restore the invariants. */
    spu94_zero_bytes(state, sizeof(*state));
    state->work_buf       = saved_work;
    state->work_buf_size  = saved_work_size;
    state->buffer_address = 0u;
}

void spu94_destroy(spu94_state *state) {
    if (state == NULL) {
        return;
    }
    /* Security hygiene: zero the state bytes so any sensitive register
     * content (e.g., preset data) does not outlive the lifetime.
     * Does NOT release memory — caller owns state_buf and work_buf. */
    spu94_zero_bytes(state, sizeof(*state));
}
