/* src/spu94/spu94_tick.c
 * Phase 2 Plan 02 Task 2 / Plan 03 Task 2: per-tick processing entry point.
 *
 * Order of operations (D-19, ADR-0004, ADR-0005):
 *   1. Flush every pending TICK_LATCHED register write.   <- Plan 03 (this)
 *   2. Advance the work-buffer address (wrap formula).    <- Plan 04
 *   3. Run the reverb network.                            <- Phase 3
 *
 * Pitfall 4 (Pending-Writes Leak Into Wrong Tick):
 *   spu94_apply_pending_writes is called from EXACTLY one location — the
 *   first line of this function. Any future change that introduces a second
 *   call site violates the contract documented in ADR-0005.
 *
 * Null-safe: spu94_tick(NULL) is a no-op.
 */

#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>

void spu94_tick(spu94_state *state) {
    if (state == (spu94_state *)0) {
        return;
    }
    /* Step 1: flush pending writes BEFORE any algorithm step reads a
     * register value, so the L and R half-cycles of this tick observe a
     * consistent set of address/delay registers. */
    spu94_apply_pending_writes(state);
    /* Plan 04 will add: spu94_buffer_advance(state); */
    /* Phase 3 will add: the reverb-network computation. */
}
