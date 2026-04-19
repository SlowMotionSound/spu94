#ifndef SPU94_REGISTER_FACADE_H
#define SPU94_REGISTER_FACADE_H

/* SPU-94 Phase 2 Plan 03 Task 3: hand-written facade layer (D-01, D-03).
 *
 * Two-layer API per CONTEXT D-01:
 *   - Engine layer: spu94_set_reg_i16/u16, spu94_get_reg_i16/u16,
 *     spu94_get_reg_i16/u16_pending. Declared in <spu94/spu94_registers.h>.
 *     Used internally and by Python bindings (Phase 6).
 *   - Facade layer (this header): 35 hand-written `static inline` per-register
 *     wrappers — three per register (set, get, get_pending) — for readable
 *     call sites in C consumers and presets.
 *
 * Why hand-written, not macro-generated (D-03):
 *   The auditability of "every wrapper maps the right register name to the
 *   right signedness" is a core deliverable of a bit-faithful library. A
 *   reader can scan this file in five minutes and confirm the mapping by
 *   reading. Macro-generation would hide that mapping behind table syntax;
 *   the maintenance cost of these 105 one-line wrappers is worth paying
 *   for the audit cost it saves.
 *
 * Cost: zero at runtime. Each wrapper is `static inline` — the compiler
 * inlines the call into the consumer; no symbol enters the library.
 *
 * Coverage: 35 setters + 35 getters + 35 pending-getters = 105 wrappers.
 *
 * Contract per wrapper:
 *   spu94_set_<NAME>(spu94_state *s, T value)        -> spu94_result_t
 *   spu94_get_<NAME>(const spu94_state *s)           -> T
 *   spu94_get_<NAME>_pending(const spu94_state *s)   -> T
 * where T is int16_t for v*-prefix registers and uint16_t for the
 * mBASE / d*-prefix / m*-prefix registers (per CONTEXT D-02).
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>

/* ============================================================== */
/* Routing / base registers (outside the 0x1DC0..0x1DFE block)    */
/* ============================================================== */

static inline spu94_result_t spu94_set_vLOUT(spu94_state *s, int16_t v)         { return spu94_set_reg_i16(s, SPU94_REG_vLOUT, v); }
static inline int16_t        spu94_get_vLOUT(const spu94_state *s)              { return spu94_get_reg_i16(s, SPU94_REG_vLOUT); }
static inline int16_t        spu94_get_vLOUT_pending(const spu94_state *s)      { return spu94_get_reg_i16_pending(s, SPU94_REG_vLOUT); }

static inline spu94_result_t spu94_set_vROUT(spu94_state *s, int16_t v)         { return spu94_set_reg_i16(s, SPU94_REG_vROUT, v); }
static inline int16_t        spu94_get_vROUT(const spu94_state *s)              { return spu94_get_reg_i16(s, SPU94_REG_vROUT); }
static inline int16_t        spu94_get_vROUT_pending(const spu94_state *s)      { return spu94_get_reg_i16_pending(s, SPU94_REG_vROUT); }

static inline spu94_result_t spu94_set_mBASE(spu94_state *s, uint16_t v)        { return spu94_set_reg_u16(s, SPU94_REG_mBASE, v); }
static inline uint16_t       spu94_get_mBASE(const spu94_state *s)              { return spu94_get_reg_u16(s, SPU94_REG_mBASE); }
static inline uint16_t       spu94_get_mBASE_pending(const spu94_state *s)      { return spu94_get_reg_u16_pending(s, SPU94_REG_mBASE); }

/* ============================================================== */
/* Reverb block — gain/coefficient registers (vIIR..vAPF2)        */
/* ============================================================== */

static inline spu94_result_t spu94_set_dAPF1(spu94_state *s, uint16_t v)        { return spu94_set_reg_u16(s, SPU94_REG_dAPF1, v); }
static inline uint16_t       spu94_get_dAPF1(const spu94_state *s)              { return spu94_get_reg_u16(s, SPU94_REG_dAPF1); }
static inline uint16_t       spu94_get_dAPF1_pending(const spu94_state *s)      { return spu94_get_reg_u16_pending(s, SPU94_REG_dAPF1); }

static inline spu94_result_t spu94_set_dAPF2(spu94_state *s, uint16_t v)        { return spu94_set_reg_u16(s, SPU94_REG_dAPF2, v); }
static inline uint16_t       spu94_get_dAPF2(const spu94_state *s)              { return spu94_get_reg_u16(s, SPU94_REG_dAPF2); }
static inline uint16_t       spu94_get_dAPF2_pending(const spu94_state *s)      { return spu94_get_reg_u16_pending(s, SPU94_REG_dAPF2); }

static inline spu94_result_t spu94_set_vIIR(spu94_state *s, int16_t v)          { return spu94_set_reg_i16(s, SPU94_REG_vIIR, v); }
static inline int16_t        spu94_get_vIIR(const spu94_state *s)               { return spu94_get_reg_i16(s, SPU94_REG_vIIR); }
static inline int16_t        spu94_get_vIIR_pending(const spu94_state *s)       { return spu94_get_reg_i16_pending(s, SPU94_REG_vIIR); }

static inline spu94_result_t spu94_set_vCOMB1(spu94_state *s, int16_t v)        { return spu94_set_reg_i16(s, SPU94_REG_vCOMB1, v); }
static inline int16_t        spu94_get_vCOMB1(const spu94_state *s)             { return spu94_get_reg_i16(s, SPU94_REG_vCOMB1); }
static inline int16_t        spu94_get_vCOMB1_pending(const spu94_state *s)     { return spu94_get_reg_i16_pending(s, SPU94_REG_vCOMB1); }

static inline spu94_result_t spu94_set_vCOMB2(spu94_state *s, int16_t v)        { return spu94_set_reg_i16(s, SPU94_REG_vCOMB2, v); }
static inline int16_t        spu94_get_vCOMB2(const spu94_state *s)             { return spu94_get_reg_i16(s, SPU94_REG_vCOMB2); }
static inline int16_t        spu94_get_vCOMB2_pending(const spu94_state *s)     { return spu94_get_reg_i16_pending(s, SPU94_REG_vCOMB2); }

static inline spu94_result_t spu94_set_vCOMB3(spu94_state *s, int16_t v)        { return spu94_set_reg_i16(s, SPU94_REG_vCOMB3, v); }
static inline int16_t        spu94_get_vCOMB3(const spu94_state *s)             { return spu94_get_reg_i16(s, SPU94_REG_vCOMB3); }
static inline int16_t        spu94_get_vCOMB3_pending(const spu94_state *s)     { return spu94_get_reg_i16_pending(s, SPU94_REG_vCOMB3); }

static inline spu94_result_t spu94_set_vCOMB4(spu94_state *s, int16_t v)        { return spu94_set_reg_i16(s, SPU94_REG_vCOMB4, v); }
static inline int16_t        spu94_get_vCOMB4(const spu94_state *s)             { return spu94_get_reg_i16(s, SPU94_REG_vCOMB4); }
static inline int16_t        spu94_get_vCOMB4_pending(const spu94_state *s)     { return spu94_get_reg_i16_pending(s, SPU94_REG_vCOMB4); }

static inline spu94_result_t spu94_set_vWALL(spu94_state *s, int16_t v)         { return spu94_set_reg_i16(s, SPU94_REG_vWALL, v); }
static inline int16_t        spu94_get_vWALL(const spu94_state *s)              { return spu94_get_reg_i16(s, SPU94_REG_vWALL); }
static inline int16_t        spu94_get_vWALL_pending(const spu94_state *s)      { return spu94_get_reg_i16_pending(s, SPU94_REG_vWALL); }

static inline spu94_result_t spu94_set_vAPF1(spu94_state *s, int16_t v)         { return spu94_set_reg_i16(s, SPU94_REG_vAPF1, v); }
static inline int16_t        spu94_get_vAPF1(const spu94_state *s)              { return spu94_get_reg_i16(s, SPU94_REG_vAPF1); }
static inline int16_t        spu94_get_vAPF1_pending(const spu94_state *s)      { return spu94_get_reg_i16_pending(s, SPU94_REG_vAPF1); }

static inline spu94_result_t spu94_set_vAPF2(spu94_state *s, int16_t v)         { return spu94_set_reg_i16(s, SPU94_REG_vAPF2, v); }
static inline int16_t        spu94_get_vAPF2(const spu94_state *s)              { return spu94_get_reg_i16(s, SPU94_REG_vAPF2); }
static inline int16_t        spu94_get_vAPF2_pending(const spu94_state *s)      { return spu94_get_reg_i16_pending(s, SPU94_REG_vAPF2); }

/* ============================================================== */
/* Reverb block — address registers (m*-prefix and d*-prefix)      */
/* ============================================================== */

static inline spu94_result_t spu94_set_mLSAME(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mLSAME, v); }
static inline uint16_t       spu94_get_mLSAME(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mLSAME); }
static inline uint16_t       spu94_get_mLSAME_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mLSAME); }

static inline spu94_result_t spu94_set_mRSAME(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mRSAME, v); }
static inline uint16_t       spu94_get_mRSAME(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mRSAME); }
static inline uint16_t       spu94_get_mRSAME_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mRSAME); }

static inline spu94_result_t spu94_set_mLCOMB1(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mLCOMB1, v); }
static inline uint16_t       spu94_get_mLCOMB1(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mLCOMB1); }
static inline uint16_t       spu94_get_mLCOMB1_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mLCOMB1); }

static inline spu94_result_t spu94_set_mRCOMB1(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mRCOMB1, v); }
static inline uint16_t       spu94_get_mRCOMB1(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mRCOMB1); }
static inline uint16_t       spu94_get_mRCOMB1_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mRCOMB1); }

static inline spu94_result_t spu94_set_mLCOMB2(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mLCOMB2, v); }
static inline uint16_t       spu94_get_mLCOMB2(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mLCOMB2); }
static inline uint16_t       spu94_get_mLCOMB2_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mLCOMB2); }

static inline spu94_result_t spu94_set_mRCOMB2(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mRCOMB2, v); }
static inline uint16_t       spu94_get_mRCOMB2(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mRCOMB2); }
static inline uint16_t       spu94_get_mRCOMB2_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mRCOMB2); }

static inline spu94_result_t spu94_set_dLSAME(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_dLSAME, v); }
static inline uint16_t       spu94_get_dLSAME(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_dLSAME); }
static inline uint16_t       spu94_get_dLSAME_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_dLSAME); }

static inline spu94_result_t spu94_set_dRSAME(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_dRSAME, v); }
static inline uint16_t       spu94_get_dRSAME(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_dRSAME); }
static inline uint16_t       spu94_get_dRSAME_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_dRSAME); }

static inline spu94_result_t spu94_set_mLDIFF(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mLDIFF, v); }
static inline uint16_t       spu94_get_mLDIFF(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mLDIFF); }
static inline uint16_t       spu94_get_mLDIFF_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mLDIFF); }

static inline spu94_result_t spu94_set_mRDIFF(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mRDIFF, v); }
static inline uint16_t       spu94_get_mRDIFF(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mRDIFF); }
static inline uint16_t       spu94_get_mRDIFF_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mRDIFF); }

static inline spu94_result_t spu94_set_mLCOMB3(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mLCOMB3, v); }
static inline uint16_t       spu94_get_mLCOMB3(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mLCOMB3); }
static inline uint16_t       spu94_get_mLCOMB3_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mLCOMB3); }

static inline spu94_result_t spu94_set_mRCOMB3(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mRCOMB3, v); }
static inline uint16_t       spu94_get_mRCOMB3(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mRCOMB3); }
static inline uint16_t       spu94_get_mRCOMB3_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mRCOMB3); }

static inline spu94_result_t spu94_set_mLCOMB4(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mLCOMB4, v); }
static inline uint16_t       spu94_get_mLCOMB4(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mLCOMB4); }
static inline uint16_t       spu94_get_mLCOMB4_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mLCOMB4); }

static inline spu94_result_t spu94_set_mRCOMB4(spu94_state *s, uint16_t v)      { return spu94_set_reg_u16(s, SPU94_REG_mRCOMB4, v); }
static inline uint16_t       spu94_get_mRCOMB4(const spu94_state *s)            { return spu94_get_reg_u16(s, SPU94_REG_mRCOMB4); }
static inline uint16_t       spu94_get_mRCOMB4_pending(const spu94_state *s)    { return spu94_get_reg_u16_pending(s, SPU94_REG_mRCOMB4); }

static inline spu94_result_t spu94_set_dLDIFF(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_dLDIFF, v); }
static inline uint16_t       spu94_get_dLDIFF(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_dLDIFF); }
static inline uint16_t       spu94_get_dLDIFF_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_dLDIFF); }

static inline spu94_result_t spu94_set_dRDIFF(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_dRDIFF, v); }
static inline uint16_t       spu94_get_dRDIFF(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_dRDIFF); }
static inline uint16_t       spu94_get_dRDIFF_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_dRDIFF); }

static inline spu94_result_t spu94_set_mLAPF1(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mLAPF1, v); }
static inline uint16_t       spu94_get_mLAPF1(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mLAPF1); }
static inline uint16_t       spu94_get_mLAPF1_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mLAPF1); }

static inline spu94_result_t spu94_set_mRAPF1(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mRAPF1, v); }
static inline uint16_t       spu94_get_mRAPF1(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mRAPF1); }
static inline uint16_t       spu94_get_mRAPF1_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mRAPF1); }

static inline spu94_result_t spu94_set_mLAPF2(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mLAPF2, v); }
static inline uint16_t       spu94_get_mLAPF2(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mLAPF2); }
static inline uint16_t       spu94_get_mLAPF2_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mLAPF2); }

static inline spu94_result_t spu94_set_mRAPF2(spu94_state *s, uint16_t v)       { return spu94_set_reg_u16(s, SPU94_REG_mRAPF2, v); }
static inline uint16_t       spu94_get_mRAPF2(const spu94_state *s)             { return spu94_get_reg_u16(s, SPU94_REG_mRAPF2); }
static inline uint16_t       spu94_get_mRAPF2_pending(const spu94_state *s)     { return spu94_get_reg_u16_pending(s, SPU94_REG_mRAPF2); }

/* ============================================================== */
/* Reverb input gain registers (vLIN/vRIN — end of block)         */
/* ============================================================== */

static inline spu94_result_t spu94_set_vLIN(spu94_state *s, int16_t v)          { return spu94_set_reg_i16(s, SPU94_REG_vLIN, v); }
static inline int16_t        spu94_get_vLIN(const spu94_state *s)               { return spu94_get_reg_i16(s, SPU94_REG_vLIN); }
static inline int16_t        spu94_get_vLIN_pending(const spu94_state *s)       { return spu94_get_reg_i16_pending(s, SPU94_REG_vLIN); }

static inline spu94_result_t spu94_set_vRIN(spu94_state *s, int16_t v)          { return spu94_set_reg_i16(s, SPU94_REG_vRIN, v); }
static inline int16_t        spu94_get_vRIN(const spu94_state *s)               { return spu94_get_reg_i16(s, SPU94_REG_vRIN); }
static inline int16_t        spu94_get_vRIN_pending(const spu94_state *s)       { return spu94_get_reg_i16_pending(s, SPU94_REG_vRIN); }

#ifdef __cplusplus
}
#endif

#endif /* SPU94_REGISTER_FACADE_H */
