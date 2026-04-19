#ifndef SPU94_REGISTERS_H
#define SPU94_REGISTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Forward declaration of the opaque state handle. We do NOT #include
 * <spu94/spu94.h> here because the umbrella header itself includes us; that
 * would create a circular include. Callers that want the lifecycle API
 * include <spu94/spu94.h>; this header only needs the type to exist as a
 * type name for the spu94_snapshot_registers signature. */
typedef struct spu94_state spu94_state;

/* ------------------------------------------------------------------------- */
/* Register identity surface (Phase 2 Plan 02)                               */
/* ------------------------------------------------------------------------- */

/* All reverb-affecting SPU registers. Sequential values (D-15) so callers can
 * iterate 0..SPU94_REG__COUNT and Python bindings get a clean IntEnum.
 *
 * Ordering intent: vLOUT/vROUT/mBASE first (routing/base, outside the reverb
 * block), then the reverb block 0x1DC0..0x1DFE in ascending hardware offset.
 * If the ordering changes, update the hw_offset and name tables in
 * src/spu94/spu94_registers.c — the _Static_assert guards the count but not
 * the ordering.
 *
 * The full PS1 absolute address is 0x1F801D00 + spu94_reg_hw_offset(reg). */
typedef enum {
    SPU94_REG_vLOUT = 0,
    SPU94_REG_vROUT,
    SPU94_REG_mBASE,
    SPU94_REG_dAPF1,
    SPU94_REG_dAPF2,
    SPU94_REG_vIIR,
    SPU94_REG_vCOMB1,
    SPU94_REG_vCOMB2,
    SPU94_REG_vCOMB3,
    SPU94_REG_vCOMB4,
    SPU94_REG_vWALL,
    SPU94_REG_vAPF1,
    SPU94_REG_vAPF2,
    SPU94_REG_mLSAME,
    SPU94_REG_mRSAME,
    SPU94_REG_mLCOMB1,
    SPU94_REG_mRCOMB1,
    SPU94_REG_mLCOMB2,
    SPU94_REG_mRCOMB2,
    SPU94_REG_dLSAME,
    SPU94_REG_dRSAME,
    SPU94_REG_mLDIFF,
    SPU94_REG_mRDIFF,
    SPU94_REG_mLCOMB3,
    SPU94_REG_mRCOMB3,
    SPU94_REG_mLCOMB4,
    SPU94_REG_mRCOMB4,
    SPU94_REG_dLDIFF,
    SPU94_REG_dRDIFF,
    SPU94_REG_mLAPF1,
    SPU94_REG_mRAPF1,
    SPU94_REG_mLAPF2,
    SPU94_REG_mRAPF2,
    SPU94_REG_vLIN,
    SPU94_REG_vRIN,
    SPU94_REG__COUNT    /* sentinel — keep last, equals 35 */
} spu94_reg_t;

/* Return the PS1 hardware register offset (low 16 bits of the absolute
 * 0x1F801xxx address) for `reg`. Returns 0xFFFF if reg is out of range. */
uint16_t spu94_reg_hw_offset(spu94_reg_t reg);

/* Return the bare register name (e.g., "vIIR", not "SPU94_REG_vIIR").
 * Returns NULL if reg is out of range. String has static storage duration. */
const char *spu94_reg_name(spu94_reg_t reg);

/* Atomically snapshot every register's ACTIVE value (see D-20, D-23).
 * Writes SPU94_REG__COUNT int16 values into `out` in enum order (index 0 = vLOUT).
 * Signed and unsigned registers are both stored as int16 in the snapshot;
 * callers that need the unsigned reading reinterpret bits per spu94_reg_name.
 * Plan 02 provides the declaration; Plan 03 provides register-value storage.
 * `out == NULL` is a no-op. NULL state with non-NULL out zeroes the buffer. */
void spu94_snapshot_registers(const spu94_state *state, int16_t out[SPU94_REG__COUNT]);

/* ------------------------------------------------------------------------- */
/* Register signedness classification (Plan 03)                              */
/* ------------------------------------------------------------------------- */

/* Each register has a fixed signedness family per CONTEXT D-02:
 *   - I16: the 12 v*-prefix gain registers (vLOUT, vROUT, vIIR, vCOMB1..4,
 *     vWALL, vAPF1, vAPF2, vLIN, vRIN).
 *   - U16: mBASE plus the 22 d-prefix / m-prefix-prefix delay/address registers.
 * Total: 12 + 23 = 35. */
typedef enum {
    SPU94_REG_TYPE_I16 = 0,
    SPU94_REG_TYPE_U16 = 1
} spu94_reg_type_t;

/* Return the signedness family of `reg`. Out-of-range reg ids return
 * SPU94_REG_TYPE_I16 (arbitrary — callers should use the engine setters'
 * SPU94_UNKNOWN_REG result code instead of relying on this for validation). */
spu94_reg_type_t spu94_reg_type(spu94_reg_t reg);

/* ------------------------------------------------------------------------- */
/* Per-register write-timing policy (Plan 03, ADR-0005)                      */
/* ------------------------------------------------------------------------- */

/* The split policy resolved by ADR-0005 (D-04):
 *   IMMEDIATE      — write becomes visible to the next read in the same tick.
 *                    Applies to all 12 v* gain registers AND mBASE (the latter
 *                    additionally fires the snap-on-write side effect).
 *   TICK_LATCHED   — write stages into a shadow slot; the active value
 *                    updates atomically at the next spu94_tick(). Applies to
 *                    the 22 d-prefix / m-prefix delay/address registers.
 *
 * The 35-entry table `spu94_write_policy_table[]` lives in
 * src/spu94/spu94_write_policy.c. It IS the swappable seam (D-05): the
 * future Controllers milestone re-points it without touching engine code. */
typedef enum {
    SPU94_WRITE_IMMEDIATE    = 0,
    SPU94_WRITE_TICK_LATCHED = 1
} spu94_write_policy_t;

/* ------------------------------------------------------------------------- */
/* Engine-layer typed register I/O (Plan 03, D-01)                           */
/* ------------------------------------------------------------------------- */

/* Two-layer API: the typed engine functions below are the bottom layer; the
 * 35 hand-written facade wrappers in <spu94/spu94_register_facade.h> forward
 * to them. The engine layer is what Python bindings (Phase 6) and iteration
 * loops (e.g., preset application in Phase 5) use directly.
 *
 * Signedness is runtime-enforced (D-02, D-07): calling the WRONG typed
 * function (e.g., spu94_set_reg_u16 on vIIR, or spu94_set_reg_i16 on dAPF1)
 * returns SPU94_TYPE_MISMATCH and leaves both active and pending state
 * unchanged (D-08).
 *
 * Out-of-range reg ids return SPU94_UNKNOWN_REG on set, 0 on get. NULL state
 * is treated identically to out-of-range.
 *
 * Writes honor the per-register write-timing policy (ADR-0005):
 *   IMMEDIATE      -> reg_values[reg] updates instantly; pending stays in
 *                     sync so the _pending readbacks return the active value.
 *   TICK_LATCHED   -> pending_values[reg] stages the new value;
 *                     reg_values[reg] is unchanged until the next tick.
 */
spu94_result_t spu94_set_reg_i16(spu94_state *state, spu94_reg_t reg, int16_t  value);
spu94_result_t spu94_set_reg_u16(spu94_state *state, spu94_reg_t reg, uint16_t value);

int16_t  spu94_get_reg_i16        (const spu94_state *state, spu94_reg_t reg);
uint16_t spu94_get_reg_u16        (const spu94_state *state, spu94_reg_t reg);

/* Pending-value read-backs (D-06, D-23). For IMMEDIATE-policy registers these
 * always equal the active reading. For TICK_LATCHED registers these return
 * what will be applied at the next spu94_tick() — the shadow created by the
 * split policy is observable, not hidden. */
int16_t  spu94_get_reg_i16_pending(const spu94_state *state, spu94_reg_t reg);
uint16_t spu94_get_reg_u16_pending(const spu94_state *state, spu94_reg_t reg);

/* Internal: apply every bit set in state->pending_mask. Copies
 * pending_values[i] into reg_values[i] and clears the bit. Per Pitfall 4
 * this is called from EXACTLY one location — the first line of spu94_tick.
 * Declared here so that spu94_tick.c can call it without duplicating the
 * declaration locally. Not part of the public API contract; subject to
 * change without notice. */
void spu94_apply_pending_writes(spu94_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_REGISTERS_H */
