/* src/spu94/spu94_write_policy.c
 * Phase 2 Plan 03 Task 2: per-register write-timing policy table.
 *
 * The 35-entry table below IS the swappable seam (D-05, ADR-0005). SPU-94
 * pins all 35 entries to their PS1-faithful values; the future SPU-94
 * Controllers milestone (D-22, D-24) re-points the table by linking an
 * alternative translation unit that defines `spu94_write_policy_table`
 * differently — without touching engine code.
 *
 * Composition (per ADR-0005 / RESEARCH.md § Per-Register Policy Table):
 *   - 12 v* gain registers           : IMMEDIATE
 *   - mBASE                          : IMMEDIATE (+ side-effect handler
 *                                      now living in spu94_buffer.c per
 *                                      Plan 04 / ADR-0006).
 *   - 22 d-prefix/m-prefix delay/address regs    : TICK_LATCHED
 * Total: 13 IMMEDIATE + 22 TICK_LATCHED = 35.
 *
 * The mBASE side-effect handler is also defined in this file as a Plan-03
 * stub — defining the symbol here lets the library link cleanly while Plan
 * 04 owns the body. Move-the-stub note: Plan 04's <output> may relocate the
 * definition to src/spu94/spu94_buffer.c if that better isolates the
 * snap-on-write logic with the buffer-arithmetic surface.
 */

#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>

const spu94_write_policy_t spu94_write_policy_table[SPU94_REG__COUNT] = {
    /* ---- 12 v* gain-type registers : IMMEDIATE ---- */
    [SPU94_REG_vLOUT]   = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vROUT]   = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vIIR]    = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vCOMB1]  = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vCOMB2]  = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vCOMB3]  = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vCOMB4]  = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vWALL]   = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vAPF1]   = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vAPF2]   = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vLIN]    = SPU94_WRITE_IMMEDIATE,
    [SPU94_REG_vRIN]    = SPU94_WRITE_IMMEDIATE,

    /* ---- mBASE : IMMEDIATE + side-effect (Plan 04 wires the snap) ---- */
    [SPU94_REG_mBASE]   = SPU94_WRITE_IMMEDIATE,

    /* ---- 22 d-prefix/m-prefix delay/address registers : TICK_LATCHED ---- */
    [SPU94_REG_dAPF1]   = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_dAPF2]   = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLSAME]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRSAME]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLCOMB1] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRCOMB1] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLCOMB2] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRCOMB2] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_dLSAME]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_dRSAME]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLDIFF]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRDIFF]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLCOMB3] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRCOMB3] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLCOMB4] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRCOMB4] = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_dLDIFF]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_dRDIFF]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLAPF1]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRAPF1]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mLAPF2]  = SPU94_WRITE_TICK_LATCHED,
    [SPU94_REG_mRAPF2]  = SPU94_WRITE_TICK_LATCHED
};

/* The Plan-03 stub for the mBASE write-side-effect handler previously lived
 * here. Plan 04 lifted the real implementation to src/spu94/spu94_buffer.c
 * (alongside the BufferAddress wrap arithmetic this side effect interacts
 * with). ODR is preserved: exactly one definition of the handler symbol in
 * the linked library, now in spu94_buffer.o. The forward declaration in
 * spu94_register_io.c (the sole caller) is unchanged; it is satisfied at
 * link time by the new home. See ADR-0006 for the snap-on-write semantics. */
