/* src/spu94/spu94_register_io.c
 * Phase 2 Plan 03 Task 1: engine-layer typed register accessors.
 *
 * Two layers per CONTEXT D-01:
 *   - This file IS the engine layer (spu94_set_reg_i16/u16, spu94_get_reg_*).
 *   - The facade layer (35 hand-written wrappers) lives in
 *     include/spu94/spu94_register_facade.h.
 *
 * Signedness enforcement (D-02, D-07, D-08):
 *   - Each register has a fixed type (i16 for v*, u16 for d-prefix/m-prefix + mBASE).
 *   - Calling the wrong-typed setter returns SPU94_TYPE_MISMATCH and does
 *     NOT mutate active or pending state.
 *   - Out-of-range reg ids are no-ops returning SPU94_UNKNOWN_REG / 0.
 *
 * Write-timing routing (D-04, D-05, ADR-0005):
 *   - IMMEDIATE: store into reg_values[] AND mirror into pending_values[]
 *     (so spu94_get_reg_*_pending returns the same value — D-06 contract).
 *   - TICK_LATCHED: store into pending_values[] only; set the pending_mask
 *     bit so spu94_apply_pending_writes flushes it at the next tick.
 *   - mBASE is the only IMMEDIATE u16; it triggers spu94_mbase_on_write
 *     (Plan 03 stub in spu94_write_policy.c; Plan 04 lifts it to the real
 *     snap-on-write handler per ADR-0006 / D-11 seam).
 */

#include "spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------- */
/* Signedness classification                                                 */
/* ------------------------------------------------------------------------- */

/* 64-bit packed mask: bit i set iff register i is u16 (d-prefix/m-prefix family).
 * 23 bits set (mBASE + 22 d-prefix/m-prefix); 12 bits clear (the v* gain registers);
 * upper 29 bits reserved. Hand-maintained per the signedness table in
 * 02-03-PLAN.md <interfaces>. The hand-audit cost is one read of this
 * literal; macro-generation would obscure the mapping (D-03 principle). */
static const uint64_t spu94_reg_is_u16_mask =
    (UINT64_C(1) << SPU94_REG_mBASE)   |
    (UINT64_C(1) << SPU94_REG_dAPF1)   |
    (UINT64_C(1) << SPU94_REG_dAPF2)   |
    (UINT64_C(1) << SPU94_REG_mLSAME)  |
    (UINT64_C(1) << SPU94_REG_mRSAME)  |
    (UINT64_C(1) << SPU94_REG_mLCOMB1) |
    (UINT64_C(1) << SPU94_REG_mRCOMB1) |
    (UINT64_C(1) << SPU94_REG_mLCOMB2) |
    (UINT64_C(1) << SPU94_REG_mRCOMB2) |
    (UINT64_C(1) << SPU94_REG_dLSAME)  |
    (UINT64_C(1) << SPU94_REG_dRSAME)  |
    (UINT64_C(1) << SPU94_REG_mLDIFF)  |
    (UINT64_C(1) << SPU94_REG_mRDIFF)  |
    (UINT64_C(1) << SPU94_REG_mLCOMB3) |
    (UINT64_C(1) << SPU94_REG_mRCOMB3) |
    (UINT64_C(1) << SPU94_REG_mLCOMB4) |
    (UINT64_C(1) << SPU94_REG_mRCOMB4) |
    (UINT64_C(1) << SPU94_REG_dLDIFF)  |
    (UINT64_C(1) << SPU94_REG_dRDIFF)  |
    (UINT64_C(1) << SPU94_REG_mLAPF1)  |
    (UINT64_C(1) << SPU94_REG_mRAPF1)  |
    (UINT64_C(1) << SPU94_REG_mLAPF2)  |
    (UINT64_C(1) << SPU94_REG_mRAPF2);

spu94_reg_type_t spu94_reg_type(spu94_reg_t reg) {
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        /* Out-of-range: arbitrary — return I16. Callers check
         * spu94_set_reg_* return code for SPU94_UNKNOWN_REG instead. */
        return SPU94_REG_TYPE_I16;
    }
    return (spu94_reg_is_u16_mask & (UINT64_C(1) << reg))
        ? SPU94_REG_TYPE_U16
        : SPU94_REG_TYPE_I16;
}

/* ------------------------------------------------------------------------- */
/* Cross-TU collaborators (forward declarations)                             */
/* ------------------------------------------------------------------------- */

/* Defined in spu94_write_policy.c (Plan 03 Task 2). */
extern const spu94_write_policy_t spu94_write_policy_table[SPU94_REG__COUNT];

/* Defined in spu94_write_policy.c (Plan 03 Task 2 stub; Plan 04 replaces).
 * Called from spu94_set_reg_u16 when reg == SPU94_REG_mBASE so Plan 04 can
 * land the snap-on-write side effect (ADR-0006) by editing one function. */
void spu94_mbase_on_write(struct spu94_state *state, uint16_t new_mbase);

/* ------------------------------------------------------------------------- */
/* Engine-layer API                                                          */
/* ------------------------------------------------------------------------- */

spu94_result_t spu94_set_reg_i16(spu94_state *state, spu94_reg_t reg, int16_t value) {
    if (state == (spu94_state *)0) {
        return SPU94_UNKNOWN_REG;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return SPU94_UNKNOWN_REG;
    }
    if (spu94_reg_type(reg) != SPU94_REG_TYPE_I16) {
        return SPU94_TYPE_MISMATCH;
    }

    /* All 12 i16 registers (vLOUT, vROUT, vIIR, vCOMB1..4, vWALL, vAPF1/2,
     * vLIN, vRIN) are IMMEDIATE per ADR-0005. The policy-table lookup is
     * skipped here for clarity — every i16 path is IMMEDIATE. */
    state->reg_values[reg]     = value;
    state->pending_values[reg] = value;  /* keep pending in sync (D-06) */
    state->pending_mask &= ~(UINT64_C(1) << reg);
    return SPU94_OK;
}

spu94_result_t spu94_set_reg_u16(spu94_state *state, spu94_reg_t reg, uint16_t value) {
    if (state == (spu94_state *)0) {
        return SPU94_UNKNOWN_REG;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return SPU94_UNKNOWN_REG;
    }
    if (spu94_reg_type(reg) != SPU94_REG_TYPE_U16) {
        return SPU94_TYPE_MISMATCH;
    }

    /* Bit-reinterpret the u16 into the i16 storage cell. The reg_values[]
     * array uses int16_t as a 16-bit storage type for both families; the
     * spu94_get_reg_u16 reader casts back to uint16_t. No data loss. */
    int16_t stored = (int16_t)value;

    spu94_write_policy_t policy = spu94_write_policy_table[reg];
    if (policy == SPU94_WRITE_IMMEDIATE) {
        /* mBASE is the only IMMEDIATE u16; the v* registers are i16. */
        state->reg_values[reg]     = stored;
        state->pending_values[reg] = stored;
        state->pending_mask &= ~(UINT64_C(1) << reg);
        if (reg == SPU94_REG_mBASE) {
            spu94_mbase_on_write(state, value);
        }
    } else {
        /* TICK_LATCHED: stage into pending; do NOT touch reg_values[].
         * spu94_apply_pending_writes will flush at the next tick. */
        state->pending_values[reg] = stored;
        state->pending_mask |= (UINT64_C(1) << reg);
    }
    return SPU94_OK;
}

int16_t spu94_get_reg_i16(const spu94_state *state, spu94_reg_t reg) {
    if (state == (const spu94_state *)0) {
        return 0;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return 0;
    }
    return state->reg_values[reg];
}

uint16_t spu94_get_reg_u16(const spu94_state *state, spu94_reg_t reg) {
    if (state == (const spu94_state *)0) {
        return 0u;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return 0u;
    }
    return (uint16_t)state->reg_values[reg];
}

int16_t spu94_get_reg_i16_pending(const spu94_state *state, spu94_reg_t reg) {
    if (state == (const spu94_state *)0) {
        return 0;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return 0;
    }
    return state->pending_values[reg];
}

uint16_t spu94_get_reg_u16_pending(const spu94_state *state, spu94_reg_t reg) {
    if (state == (const spu94_state *)0) {
        return 0u;
    }
    if ((int)reg < 0 || (int)reg >= (int)SPU94_REG__COUNT) {
        return 0u;
    }
    return (uint16_t)state->pending_values[reg];
}
