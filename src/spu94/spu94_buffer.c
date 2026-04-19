/* src/spu94/spu94_buffer.c
 * Phase 2 Plan 04 Task 1: BufferAddress wrap arithmetic + mBASE snap-on-write
 * + the public spu94_get_buffer_address observability accessor.
 *
 * Functions provided:
 *   - spu94_buffer_advance(state) — internal, called once per stereo tick from
 *     spu94_tick AFTER spu94_apply_pending_writes (Pitfall 4: still exactly
 *     one call site each).
 *   - spu94_mbase_on_write(state, new_mbase) — internal, the real body of
 *     the side-effect handler whose Plan 03 stub previously lived in
 *     spu94_write_policy.c. Stub removed there; ODR preserved (one definition
 *     in this TU).
 *   - spu94_get_buffer_address(state) — public observability accessor (D-23).
 *
 * BufferAddress wrap formula (CORE-03; per psx-spx; see ADR-0006 + RESEARCH.md
 * § BufferAddress Arithmetic):
 *
 *     buffer_address = max(mBASE, (buffer_address + 2) AND 0x7FFFE)
 *
 * - Byte addressing. The +2 step advances by one 16-bit halfword per stereo tick.
 * - 0x7FFFE = 0b0111_1111_1111_1111_1110 — bit 0 clear (halfword alignment),
 *   high bit at the top of the 19-bit 512KB-2 SPU RAM window.
 * - The max(mBASE, ...) clause floors the running address to mBASE even
 *   without the snap-on-write side effect; the snap is the additional
 *   stateful event that fires at write time (RESEARCH.md § mBASE Side-Effect
 *   Evidence — primary-source language from nocash, paraphrased in ADR-0006).
 *
 * mBASE snap-on-write (ADR-0006):
 *
 *     On any write to mBASE with value N:
 *         state->reg_values[SPU94_REG_mBASE] = (int16_t)N    (engine layer)
 *         state->buffer_address = (uint32_t)N                (this file)
 *
 * No implicit work-buffer clear. The jump can produce audible discontinuity
 * in an active reverb tail; that is hardware-accurate and SPU-94's default.
 *
 * D-11 seam: spu94_mbase_on_write is the single internal symbol that the
 * future SPU-94 Controllers milestone may re-link to alternative behaviors
 * (floor-only, crossfade-on-write, clear-and-snap) without touching engine
 * code. Phase 2 keeps the handler internal, not runtime-swappable, per
 * RESEARCH.md Open Question #5 — promoting it to a function pointer is
 * additive Controllers work via a new ADR.
 *
 * Bit-faithfulness note (T-02-18, ADR-0006): the snap passes the written
 * value through VERBATIM. An odd mBASE produces an odd buffer_address for
 * exactly one step, until the next advance clears bit 0 via the AND 0x7FFFE
 * mask. Do NOT add `& ~1u` to the snap — doing so would diverge from the
 * primary-source-faithful interpretation and break Plan 05's T-02-28 fuzz
 * invariant.
 */

#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>

/* Internal-only forward declarations satisfy -Werror=missing-prototypes
 * without exposing these symbols on a public header. The matching forward
 * declarations live in:
 *   - src/spu94/spu94_tick.c        (sole caller of spu94_buffer_advance)
 *   - src/spu94/spu94_register_io.c (sole caller of spu94_mbase_on_write)
 * spu94_get_buffer_address is declared publicly in include/spu94/spu94.h
 * (Plan 04 addition). */
void spu94_buffer_advance(spu94_state *state);
void spu94_mbase_on_write(spu94_state *state, uint16_t new_mbase);

void spu94_buffer_advance(spu94_state *state) {
    if (state == (spu94_state *)0) {
        return;
    }
    /* mBASE is stored as int16_t in reg_values[]; reinterpret as u16 then
     * widen to u32 for the byte-address arithmetic (the m* halving-by-8
     * convention used by Phase 3 against the work buffer is a separate
     * concern — RESEARCH.md Pitfall 3). */
    uint32_t mbase = (uint32_t)(uint16_t)state->reg_values[SPU94_REG_mBASE];
    uint32_t advanced = (state->buffer_address + 2u) & 0x7FFFEu;
    /* Inline ternary instead of a max() macro — keeps the operation visible
     * (acceptance criterion in 02-04-PLAN.md: no max-macro hiding intent). */
    state->buffer_address = (advanced > mbase) ? advanced : mbase;
}

void spu94_mbase_on_write(spu94_state *state, uint16_t new_mbase) {
    if (state == (spu94_state *)0) {
        return;
    }
    /* ADR-0006: snap. Verbatim — no bit-0 mask, no work-buffer clear. */
    state->buffer_address = (uint32_t)new_mbase;
}

uint32_t spu94_get_buffer_address(const spu94_state *state) {
    if (state == (const spu94_state *)0) {
        return 0u;
    }
    return state->buffer_address;
}
