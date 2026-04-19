/* src/spu94/spu94_pending.c
 * Phase 2 Plan 03 Task 2: pending-write shadow flush.
 *
 * spu94_apply_pending_writes() iterates state->pending_mask, copying
 * pending_values[i] into reg_values[i] for every set bit, then clearing
 * the entire mask in one assignment.
 *
 * Pitfall 4 (pending leak): this function is called from EXACTLY ONE
 * location — the first line of spu94_tick(). Any future change that
 * introduces a second call site violates the contract documented in
 * ADR-0005 and risks re-applying the same pending value twice or
 * partially-applying writes mid-tick. A future grep-based CI guard could
 * mechanically enforce this if drift becomes a concern.
 */

#include "spu94_state_internal.h"
#include <spu94/spu94_registers.h>
#include <stddef.h>
#include <stdint.h>

void spu94_apply_pending_writes(spu94_state *state) {
    if (state == (spu94_state *)0) {
        return;
    }
    uint64_t mask = state->pending_mask;
    /* At most 35 bits; a simple sequential scan is fine and fully
     * branch-predictable. __builtin_ctzll iteration would be marginally
     * faster on sparse masks but adds compiler-portability concerns
     * (clang on Windows has no __builtin_ctzll without intrin shims).
     * Deferred unless profiling shows this loop in a hot path. */
    /* size_t index matches the spu94_zero_bytes / spu94_snapshot_registers
     * sibling style and removes the signed/unsigned mismatch against
     * SPU94_REG__COUNT. The shift `UINT64_C(1) << i` is well-defined: i is
     * in [0, SPU94_REG__COUNT) = [0, 35), well below the 64-bit operand
     * width, so no UB on the unsigned literal. */
    for (size_t i = 0; i < (size_t)SPU94_REG__COUNT; ++i) {
        if (mask & (UINT64_C(1) << i)) {
            state->reg_values[i] = state->pending_values[i];
        }
    }
    state->pending_mask = 0u;
}
