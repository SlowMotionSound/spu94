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

/* Forward declaration. spu94_buffer_advance is internal to the library
 * (defined in spu94_buffer.c, Plan 04). We do NOT publish it through any
 * include/spu94/ header — the only caller in Phase 2 is the tick body
 * below. Pitfall 4 still holds: exactly one call site per internal helper
 * that mutates per-tick state. */
void spu94_buffer_advance(spu94_state *state);

/* Forward declaration. spu94_reverb_body is internal to the library
 * (defined in spu94_reverb.c, Phase 3 Plan 01). Internal-header
 * symbol — not published through any include/spu94/ header.
 * Pitfall 4: exactly one call site — the tick body below. */
void spu94_reverb_body(spu94_state *state);

void spu94_tick(spu94_state *state) {
    if (state == (spu94_state *)0) {
        return;
    }
    /* Step 1: flush pending writes BEFORE any algorithm step reads a
     * register value, so the L and R half-cycles of this tick observe a
     * consistent set of address/delay registers. */
    spu94_apply_pending_writes(state);
    /* Step 2: advance the BufferAddress per the wrap formula
     * (CORE-03; Plan 04). After apply_pending so the formula sees the
     * latest mBASE — though mBASE is IMMEDIATE so this ordering is
     * defensive against future policy changes more than functionally
     * required today. */
    spu94_buffer_advance(state);
    /* Step 3: run the reverb-network computation (Phase 3 Plan 01 onward).
     * Reads v* snapshots (D-08), scales, clips (CORE-02, D-09), runs the
     * IIR/comb/APF network (Plans 02/03), and emits output-scale products. */
    spu94_reverb_body(state);
}
