#ifndef SPU94_H
#define SPU94_H

#ifdef __cplusplus
extern "C" {
#endif

/* SPU-94 public API umbrella header.
 * Phase 1: Q15 helper surface (spu94_q15.h).
 * Phase 2 (Plan 01): opaque spu94_state type, size/alignment macros,
 *                    spu94_result_t enum, lifecycle API (init/reset/destroy).
 * Later Phase 2 plans: register I/O, write-timing policy, buffer arithmetic.
 * Later phases: spu94_tick(), spu94_process(), presets, Python bindings.
 *
 * C99 freestanding conformance (API-07, API-09):
 *   This header compiles clean under -std=c99 -pedantic -Werror and through
 *   an extern "C" C++ consumer. Only <stdint.h>, <stddef.h>, <limits.h>,
 *   <stdbool.h> (freestanding C99) and <stdalign.h> (freestanding C11) may
 *   appear here; no <stdlib.h>, <string.h>, or <stdio.h>.
 *
 * Thread-safety:
 *   A spu94_state is NOT thread-safe. Concurrent access from multiple
 *   threads requires external synchronization. The library performs no
 *   locking in the hot path (PROJECT.md constraint).
 */

#include <stddef.h>
#include <stdint.h>
#include <spu94/spu94_q15.h>

/* spu94_result_t MUST be declared before <spu94/spu94_registers.h> is
 * included: the engine-layer setter declarations in spu94_registers.h
 * (Plan 03) return spu94_result_t. Reordering this include makes the
 * type visible to those signatures without forcing spu94_registers.h
 * to take a transitive dependency on the umbrella header. */

/* ------------------------------------------------------------------------- */
/* Result codes (D-07)                                                       */
/* ------------------------------------------------------------------------- */

/* Return-code enum for the register-write API (Plan 03 onward). Declared in
 * Plan 01 so downstream plans can reference it without header churn.
 *
 * Contract:
 *   - SPU94_OK == 0; callers may write `if (spu94_set_* (...))` to detect any
 *     non-OK outcome.
 *   - New codes are APPEND-ONLY. Existing names and numeric values are stable
 *     across minor-version bumps. Callers that ignore the return value
 *     continue to work when new codes are added.
 *   - Data behavior is bit-faithful regardless of the return code (D-08):
 *     clamping/wrapping happens per PS1 hardware; the code describes it.
 */
typedef enum {
    SPU94_OK            = 0,
    SPU94_CLAMPED       = 1, /* value was saturated to fit the register */
    SPU94_UNKNOWN_REG   = 2, /* register id out of range — no-op write   */
    SPU94_TYPE_MISMATCH = 3  /* signed/unsigned accessor mismatch        */
} spu94_result_t;

#include <spu94/spu94_registers.h>
#include <spu94/spu94_register_facade.h>

/* ------------------------------------------------------------------------- */
/* State sizing and alignment                                                */
/* ------------------------------------------------------------------------- */

/* Upper bound for static/stack-allocated spu94_state storage (MCU / embedded
 * callers). The internal struct is guarded by
 *   _Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX, ...)
 * in the internal header that owns the struct definition
 * (src/spu94/spu94_state_internal.h), so growing the private struct past this
 * bound fails the build. Chosen generously (RESEARCH.md A7) — modest future
 * field additions in Plans 02-05 do not force a bump.
 *
 * Typical caller idiom (D-12):
 *   alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
 *   spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
 *                               work_buf, work_buf_size);
 */
#define SPU94_STATE_SIZE_MAX  16384u

/* Alignment requirement for the caller-provided state buffer. 16 bytes covers
 * int64_t, future SIMD, and every target toolchain in the ADR-0001 matrix
 * (MCU 8-byte alignment + headroom). See 02-RESEARCH.md Pitfall 2. */
#define SPU94_STATE_ALIGN_MAX 16u

/* ------------------------------------------------------------------------- */
/* Opaque state handle (D-12)                                                */
/* ------------------------------------------------------------------------- */

/* The opaque spu94_state type is forward-declared in <spu94/spu94_registers.h>
 * (included above). Callers may only hold spu94_state * (not by value); the
 * real struct is private to src/spu94/spu94_state.c. We do NOT re-typedef it
 * here — duplicate typedefs of the same name break -std=c99 -pedantic
 * (allowed in C11+ but not C99). API-07 requires this header to compile under
 * C99-pedantic, so the typedef has a single home. */

/* spu94_result_t is declared above (before <spu94/spu94_registers.h>) so the
 * Plan 03 engine-layer setter signatures can refer to it without circular
 * includes. The duplicate typedef previously lived here in Plan 01; moved
 * upward in Plan 03 with no API change. */

/* ------------------------------------------------------------------------- */
/* Lifecycle API (D-14)                                                      */
/* ------------------------------------------------------------------------- */

/* Return the exact number of bytes the caller must provide for state storage.
 * Deterministic: repeated calls in the same process return the same value.
 * Guaranteed to be <= SPU94_STATE_SIZE_MAX. */
size_t        spu94_state_size(void);

/* Initialize a spu94_state in caller-allocated memory.
 *
 * Arguments:
 *   state_buf       Caller-owned state storage, at least SPU94_STATE_SIZE_MAX
 *                   bytes, aligned to SPU94_STATE_ALIGN_MAX.
 *   state_buf_size  Usable size of state_buf. Must be >= spu94_state_size().
 *   work_buf        Caller-owned reverb work buffer (D-13). May be NULL if
 *                   work_buf_size == 0 (zero-sized "don't-care" configuration:
 *                   no tick/advance will be issued).
 *   work_buf_size   Size of work_buf in bytes.
 *
 * Returns:
 *   Non-NULL spu94_state* on success — identical to state_buf cast to the
 *     opaque type. Internal fields are zeroed; BufferAddress = mBASE = 0.
 *   NULL if any of the following hold (the library does NOT mutate caller
 *     memory on failure):
 *       state_buf == NULL
 *       state_buf_size < spu94_state_size()
 *       state_buf is not aligned to SPU94_STATE_ALIGN_MAX
 *       work_buf == NULL && work_buf_size > 0  (caller bug)
 *
 * This function performs NO heap allocation. */
spu94_state  *spu94_init(void *state_buf, size_t state_buf_size,
                         void *work_buf,  size_t work_buf_size);

/* Restore the state to post-init invariants without re-running init.
 * Per the state-allocation pattern (02-RESEARCH.md § State-Allocation Pattern):
 * zeros all internal registers, clears pending_mask, restores BufferAddress
 * to mBASE (= 0), and zeroes the caller's work buffer. The work-buffer
 * POINTER is preserved. Safe to call with state == NULL (no-op). */
void          spu94_reset(spu94_state *state);

/* Zero the state's internal bytes (security hygiene per T-02-03). Does NOT
 * release memory — the caller owns state_buf and work_buf. Safe to call
 * with state == NULL (no-op). After spu94_destroy, the state pointer is
 * invalid until the caller re-runs spu94_init on the underlying buffer. */
void          spu94_destroy(spu94_state *state);

/* ------------------------------------------------------------------------- */
/* Per-tick processing entry point (D-19, ADR-0004)                          */
/* ------------------------------------------------------------------------- */

/* The atomic per-22.05 kHz-tick processing entry point. Phase 3 implements
 * the reverb algorithm inside this function. Phase 5's spu94_process is
 * built as a loop calling spu94_tick per stereo tick.
 *
 * Plan 02 ships a no-op stub body. The observability contract is:
 * between two spu94_tick calls, any public accessor (snapshot_registers,
 * buffer_address, etc.) observes a consistent instantaneous state.
 *
 * Null-safe: spu94_tick(NULL) is a no-op. */
void          spu94_tick(spu94_state *state);

/* ------------------------------------------------------------------------- */
/* BufferAddress observability (CORE-03, D-23, ADR-0006)                     */
/* ------------------------------------------------------------------------- */

/* Return the current BufferAddress — byte offset into the reverb work
 * buffer that the wrap-formula advance reads/writes (per ADR-0006). The
 * value is updated once per spu94_tick() (advance), and additionally
 * snapped to the new mBASE on any successful write to mBASE
 * (snap-on-write). NULL state returns 0. The accessor is read-only;
 * mutation happens only through spu94_tick (advance) or via writing
 * mBASE (snap). */
uint32_t      spu94_get_buffer_address(const spu94_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_H */
