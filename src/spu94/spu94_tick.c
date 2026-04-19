/* src/spu94/spu94_tick.c
 * Phase 2 Plan 02 Task 2: public spu94_tick entry point (D-19, ADR-0004).
 *
 * Plan 02 stub body. Future plans wire real work in:
 *   - Plan 03: apply_pending_writes routes through here at tick-start.
 *   - Plan 04: spu94_buffer_advance routes through here.
 *   - Phase 3: the full reverb algorithm lands inside this function.
 *   - Phase 5: spu94_process becomes a loop around spu94_tick.
 *
 * The empty body in Plan 02 is intentional: declaring the public symbol now
 * lets downstream consumers (Controllers exploration layer, Error
 * Accumulator project) compile and link against the contract while the
 * algorithm fills in over later phases. Per ADR-0004 the existence of the
 * function name is orthogonal to the correctness of the reverb math that
 * will eventually live inside it.
 */

#include <spu94/spu94.h>
#include <stdint.h>

void spu94_tick(spu94_state *state) {
    (void)state;
    /* Intentionally empty in Plan 02. */
}
