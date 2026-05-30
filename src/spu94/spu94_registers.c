/* src/spu94/spu94_registers.c
 * Phase 2 Plan 02 Task 1: register-identity tables + accessors.
 *
 * Pairs with include/spu94/spu94_registers.h. Two parallel tables — one for
 * the PS1 hardware offset, one for the bare register name — both indexed by
 * spu94_reg_t. The _Static_assert below pins SPU94_REG__COUNT == 35 so any
 * future enum edit that drifts the count fails the build here.
 *
 * The register snapshot accessor lives here too; it reads the `reg_values[]`
 * storage in struct spu94_state.
 */

#include "spu94_state_internal.h"
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>

/* Pin the canonical 35 count. If the enum changes size, the build fails here
 * and forces an intentional update of both tables below. */
_Static_assert((int)SPU94_REG__COUNT == 35,
    "SPU94_REG__COUNT must equal 35 — phase context locked this count.");

/* PS1 hardware register offset table (psx-spx primary source). Designated
 * initializers keep the table robust to enum reordering: each row binds the
 * offset to the enum name, not to a positional index. */
static const uint16_t spu94_reg_hw_offsets[SPU94_REG__COUNT] = {
    [SPU94_REG_vLOUT]   = 0x1D84u,
    [SPU94_REG_vROUT]   = 0x1D86u,
    [SPU94_REG_mBASE]   = 0x1DA2u,
    [SPU94_REG_dAPF1]   = 0x1DC0u,
    [SPU94_REG_dAPF2]   = 0x1DC2u,
    [SPU94_REG_vIIR]    = 0x1DC4u,
    [SPU94_REG_vCOMB1]  = 0x1DC6u,
    [SPU94_REG_vCOMB2]  = 0x1DC8u,
    [SPU94_REG_vCOMB3]  = 0x1DCAu,
    [SPU94_REG_vCOMB4]  = 0x1DCCu,
    [SPU94_REG_vWALL]   = 0x1DCEu,
    [SPU94_REG_vAPF1]   = 0x1DD0u,
    [SPU94_REG_vAPF2]   = 0x1DD2u,
    [SPU94_REG_mLSAME]  = 0x1DD4u,
    [SPU94_REG_mRSAME]  = 0x1DD6u,
    [SPU94_REG_mLCOMB1] = 0x1DD8u,
    [SPU94_REG_mRCOMB1] = 0x1DDAu,
    [SPU94_REG_mLCOMB2] = 0x1DDCu,
    [SPU94_REG_mRCOMB2] = 0x1DDEu,
    [SPU94_REG_dLSAME]  = 0x1DE0u,
    [SPU94_REG_dRSAME]  = 0x1DE2u,
    [SPU94_REG_mLDIFF]  = 0x1DE4u,
    [SPU94_REG_mRDIFF]  = 0x1DE6u,
    [SPU94_REG_mLCOMB3] = 0x1DE8u,
    [SPU94_REG_mRCOMB3] = 0x1DEAu,
    [SPU94_REG_mLCOMB4] = 0x1DECu,
    [SPU94_REG_mRCOMB4] = 0x1DEEu,
    [SPU94_REG_dLDIFF]  = 0x1DF0u,
    [SPU94_REG_dRDIFF]  = 0x1DF2u,
    [SPU94_REG_mLAPF1]  = 0x1DF4u,
    [SPU94_REG_mRAPF1]  = 0x1DF6u,
    [SPU94_REG_mLAPF2]  = 0x1DF8u,
    [SPU94_REG_mRAPF2]  = 0x1DFAu,
    [SPU94_REG_vLIN]    = 0x1DFCu,
    [SPU94_REG_vRIN]    = 0x1DFEu
};

/* Bare-name table (per CONTEXT D-17 + RESEARCH Open Q #6). Returned strings
 * have static storage duration; callers may keep the pointer indefinitely. */
static const char *const spu94_reg_names[SPU94_REG__COUNT] = {
    [SPU94_REG_vLOUT]   = "vLOUT",
    [SPU94_REG_vROUT]   = "vROUT",
    [SPU94_REG_mBASE]   = "mBASE",
    [SPU94_REG_dAPF1]   = "dAPF1",
    [SPU94_REG_dAPF2]   = "dAPF2",
    [SPU94_REG_vIIR]    = "vIIR",
    [SPU94_REG_vCOMB1]  = "vCOMB1",
    [SPU94_REG_vCOMB2]  = "vCOMB2",
    [SPU94_REG_vCOMB3]  = "vCOMB3",
    [SPU94_REG_vCOMB4]  = "vCOMB4",
    [SPU94_REG_vWALL]   = "vWALL",
    [SPU94_REG_vAPF1]   = "vAPF1",
    [SPU94_REG_vAPF2]   = "vAPF2",
    [SPU94_REG_mLSAME]  = "mLSAME",
    [SPU94_REG_mRSAME]  = "mRSAME",
    [SPU94_REG_mLCOMB1] = "mLCOMB1",
    [SPU94_REG_mRCOMB1] = "mRCOMB1",
    [SPU94_REG_mLCOMB2] = "mLCOMB2",
    [SPU94_REG_mRCOMB2] = "mRCOMB2",
    [SPU94_REG_dLSAME]  = "dLSAME",
    [SPU94_REG_dRSAME]  = "dRSAME",
    [SPU94_REG_mLDIFF]  = "mLDIFF",
    [SPU94_REG_mRDIFF]  = "mRDIFF",
    [SPU94_REG_mLCOMB3] = "mLCOMB3",
    [SPU94_REG_mRCOMB3] = "mRCOMB3",
    [SPU94_REG_mLCOMB4] = "mLCOMB4",
    [SPU94_REG_mRCOMB4] = "mRCOMB4",
    [SPU94_REG_dLDIFF]  = "dLDIFF",
    [SPU94_REG_dRDIFF]  = "dRDIFF",
    [SPU94_REG_mLAPF1]  = "mLAPF1",
    [SPU94_REG_mRAPF1]  = "mRAPF1",
    [SPU94_REG_mLAPF2]  = "mLAPF2",
    [SPU94_REG_mRAPF2]  = "mRAPF2",
    [SPU94_REG_vLIN]    = "vLIN",
    [SPU94_REG_vRIN]    = "vRIN"
};

uint16_t spu94_reg_hw_offset(spu94_reg_t reg) {
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return 0xFFFFu;
    }
    return spu94_reg_hw_offsets[reg];
}

const char *spu94_reg_name(spu94_reg_t reg) {
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return (const char *)0;
    }
    return spu94_reg_names[reg];
}

/* Atomic snapshot — copies the 35 ACTIVE register values out of the state.
 * Plan 03 wires this to read state->reg_values[] (Plan 01 reserved the slot).
 *
 * Contract:
 *   - out == NULL  -> no-op.
 *   - state == NULL && out != NULL  -> zeroes out (deterministic for callers
 *     that snapshot before init has run, e.g., diagnostic dumps).
 *   - else  -> copy the 35 active values in enum order. */
void spu94_snapshot_registers(const spu94_state *state, int16_t out[SPU94_REG__COUNT]) {
    if (out == (int16_t *)0) {
        return;
    }
    if (state == (const spu94_state *)0) {
        for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
            out[i] = 0;
        }
        return;
    }
    for (int i = 0; i < (int)SPU94_REG__COUNT; ++i) {
        out[i] = state->reg_values[i];
    }
}
