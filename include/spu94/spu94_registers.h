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
 * In Plan 02's body, this function zero-fills `out` (stub behavior).
 * `out == NULL` is a no-op. */
void spu94_snapshot_registers(const spu94_state *state, int16_t out[SPU94_REG__COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_REGISTERS_H */
