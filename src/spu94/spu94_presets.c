/* src/spu94/spu94_presets.c -- Phase 5 Plan 01.
 *
 * The ten PS1 SPU factory reverb preset register tables. Values sourced
 * via the D-07 three-source audit:
 *   BIB-011 nocash psx-spx "SPU Reverb Examples" (primary)
 *   BIB-012 hitmen c02 spu.txt (corroborating second source)
 *   BIB-013 Sony Psy-Q LIBSND (preset-ID ordering + name mapping only)
 * Cell-level equality confirmed for 334/350 cells by
 * tests/python/verify_preset_sources.py against the two audit CSVs.
 * The 16 remaining cells (Off preset, m-prefix registers) are resolved
 * in .planning/research/05-preset-values-audit-resolutions.md per the
 * documented priority chain BIB-013 > BIB-011 > BIB-012 -- BIB-011
 * wins, so Off carries nocash's 0x0001 defensive buffer-offset values
 * rather than hitmen's all-zero.
 *
 * Provenance ADR lands in Plan 05. No prose transcribed; values only.
 */
#include <spu94/spu94.h>
#include <stdint.h>

/* _Static_assert: the struct layout matches the 35-register contract. */
_Static_assert(sizeof(((const spu94_preset_t *)0)->regs) / sizeof(int16_t)
               == SPU94_REG__COUNT,
               "spu94_preset_t.regs length must equal SPU94_REG__COUNT");

/* Each row is 35 int16 values in SPU94 register enum order. Unsigned-family
 * registers (d-prefix/m-prefix + mBASE) are stored as int16 bit-patterns via
 * (int16_t) casts; Plan 03's spu94_load_preset reinterprets via (uint16_t)
 * before calling spu94_set_reg_u16. Inline hex literals preserve the exact
 * bit pattern regardless of int16_t signedness.
 *
 * reg_idx 0..2 (vLOUT, vROUT, mBASE) are 0x0000 for every preset -- neither
 * nocash nor hitmen publishes per-preset values for those global registers
 * (they are configured by the caller outside the preset surface).
 *
 * Ordering (see spu94_registers.h): vLOUT, vROUT, mBASE, dAPF1, dAPF2,
 * vIIR, vCOMB1..4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1,
 * mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3,
 * mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2,
 * vLIN, vRIN. */

const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT] = {
    [SPU94_PRESET_OFF] = {
        .name = "Off",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x0000,  /* [ 3] dAPF1 */
            (int16_t)0x0000,  /* [ 4] dAPF2 */
            (int16_t)0x0000,  /* [ 5] vIIR */
            (int16_t)0x0000,  /* [ 6] vCOMB1 */
            (int16_t)0x0000,  /* [ 7] vCOMB2 */
            (int16_t)0x0000,  /* [ 8] vCOMB3 */
            (int16_t)0x0000,  /* [ 9] vCOMB4 */
            (int16_t)0x0000,  /* [10] vWALL */
            (int16_t)0x0000,  /* [11] vAPF1 */
            (int16_t)0x0000,  /* [12] vAPF2 */
            (int16_t)0x0001,  /* [13] mLSAME */
            (int16_t)0x0001,  /* [14] mRSAME */
            (int16_t)0x0001,  /* [15] mLCOMB1 */
            (int16_t)0x0001,  /* [16] mRCOMB1 */
            (int16_t)0x0001,  /* [17] mLCOMB2 */
            (int16_t)0x0001,  /* [18] mRCOMB2 */
            (int16_t)0x0000,  /* [19] dLSAME */
            (int16_t)0x0000,  /* [20] dRSAME */
            (int16_t)0x0001,  /* [21] mLDIFF */
            (int16_t)0x0001,  /* [22] mRDIFF */
            (int16_t)0x0001,  /* [23] mLCOMB3 */
            (int16_t)0x0001,  /* [24] mRCOMB3 */
            (int16_t)0x0001,  /* [25] mLCOMB4 */
            (int16_t)0x0001,  /* [26] mRCOMB4 */
            (int16_t)0x0000,  /* [27] dLDIFF */
            (int16_t)0x0000,  /* [28] dRDIFF */
            (int16_t)0x0001,  /* [29] mLAPF1 */
            (int16_t)0x0001,  /* [30] mRAPF1 */
            (int16_t)0x0001,  /* [31] mLAPF2 */
            (int16_t)0x0001,  /* [32] mRAPF2 */
            (int16_t)0x0000,  /* [33] vLIN */
            (int16_t)0x0000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_ROOM] = {
        .name = "Room",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x007D,  /* [ 3] dAPF1 */
            (int16_t)0x005B,  /* [ 4] dAPF2 */
            (int16_t)0x6D80,  /* [ 5] vIIR */
            (int16_t)0x54B8,  /* [ 6] vCOMB1 */
            (int16_t)0xBED0,  /* [ 7] vCOMB2 */
            (int16_t)0x0000,  /* [ 8] vCOMB3 */
            (int16_t)0x0000,  /* [ 9] vCOMB4 */
            (int16_t)0xBA80,  /* [10] vWALL */
            (int16_t)0x5800,  /* [11] vAPF1 */
            (int16_t)0x5300,  /* [12] vAPF2 */
            (int16_t)0x04D6,  /* [13] mLSAME */
            (int16_t)0x0333,  /* [14] mRSAME */
            (int16_t)0x03F0,  /* [15] mLCOMB1 */
            (int16_t)0x0227,  /* [16] mRCOMB1 */
            (int16_t)0x0374,  /* [17] mLCOMB2 */
            (int16_t)0x01EF,  /* [18] mRCOMB2 */
            (int16_t)0x0334,  /* [19] dLSAME */
            (int16_t)0x01B5,  /* [20] dRSAME */
            (int16_t)0x0000,  /* [21] mLDIFF */
            (int16_t)0x0000,  /* [22] mRDIFF */
            (int16_t)0x0000,  /* [23] mLCOMB3 */
            (int16_t)0x0000,  /* [24] mRCOMB3 */
            (int16_t)0x0000,  /* [25] mLCOMB4 */
            (int16_t)0x0000,  /* [26] mRCOMB4 */
            (int16_t)0x0000,  /* [27] dLDIFF */
            (int16_t)0x0000,  /* [28] dRDIFF */
            (int16_t)0x01B4,  /* [29] mLAPF1 */
            (int16_t)0x0136,  /* [30] mRAPF1 */
            (int16_t)0x00B8,  /* [31] mLAPF2 */
            (int16_t)0x005C,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_STUDIO_A] = {
        .name = "Studio A",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x0033,  /* [ 3] dAPF1 */
            (int16_t)0x0025,  /* [ 4] dAPF2 */
            (int16_t)0x70F0,  /* [ 5] vIIR */
            (int16_t)0x4FA8,  /* [ 6] vCOMB1 */
            (int16_t)0xBCE0,  /* [ 7] vCOMB2 */
            (int16_t)0x4410,  /* [ 8] vCOMB3 */
            (int16_t)0xC0F0,  /* [ 9] vCOMB4 */
            (int16_t)0x9C00,  /* [10] vWALL */
            (int16_t)0x5280,  /* [11] vAPF1 */
            (int16_t)0x4EC0,  /* [12] vAPF2 */
            (int16_t)0x03E4,  /* [13] mLSAME */
            (int16_t)0x031B,  /* [14] mRSAME */
            (int16_t)0x03A4,  /* [15] mLCOMB1 */
            (int16_t)0x02AF,  /* [16] mRCOMB1 */
            (int16_t)0x0372,  /* [17] mLCOMB2 */
            (int16_t)0x0266,  /* [18] mRCOMB2 */
            (int16_t)0x031C,  /* [19] dLSAME */
            (int16_t)0x025D,  /* [20] dRSAME */
            (int16_t)0x025C,  /* [21] mLDIFF */
            (int16_t)0x018E,  /* [22] mRDIFF */
            (int16_t)0x022F,  /* [23] mLCOMB3 */
            (int16_t)0x0135,  /* [24] mRCOMB3 */
            (int16_t)0x01D2,  /* [25] mLCOMB4 */
            (int16_t)0x00B7,  /* [26] mRCOMB4 */
            (int16_t)0x018F,  /* [27] dLDIFF */
            (int16_t)0x00B5,  /* [28] dRDIFF */
            (int16_t)0x00B4,  /* [29] mLAPF1 */
            (int16_t)0x0080,  /* [30] mRAPF1 */
            (int16_t)0x004C,  /* [31] mLAPF2 */
            (int16_t)0x0026,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_STUDIO_B] = {
        .name = "Studio B",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x00B1,  /* [ 3] dAPF1 */
            (int16_t)0x007F,  /* [ 4] dAPF2 */
            (int16_t)0x70F0,  /* [ 5] vIIR */
            (int16_t)0x4FA8,  /* [ 6] vCOMB1 */
            (int16_t)0xBCE0,  /* [ 7] vCOMB2 */
            (int16_t)0x4510,  /* [ 8] vCOMB3 */
            (int16_t)0xBEF0,  /* [ 9] vCOMB4 */
            (int16_t)0xB4C0,  /* [10] vWALL */
            (int16_t)0x5280,  /* [11] vAPF1 */
            (int16_t)0x4EC0,  /* [12] vAPF2 */
            (int16_t)0x0904,  /* [13] mLSAME */
            (int16_t)0x076B,  /* [14] mRSAME */
            (int16_t)0x0824,  /* [15] mLCOMB1 */
            (int16_t)0x065F,  /* [16] mRCOMB1 */
            (int16_t)0x07A2,  /* [17] mLCOMB2 */
            (int16_t)0x0616,  /* [18] mRCOMB2 */
            (int16_t)0x076C,  /* [19] dLSAME */
            (int16_t)0x05ED,  /* [20] dRSAME */
            (int16_t)0x05EC,  /* [21] mLDIFF */
            (int16_t)0x042E,  /* [22] mRDIFF */
            (int16_t)0x050F,  /* [23] mLCOMB3 */
            (int16_t)0x0305,  /* [24] mRCOMB3 */
            (int16_t)0x0462,  /* [25] mLCOMB4 */
            (int16_t)0x02B7,  /* [26] mRCOMB4 */
            (int16_t)0x042F,  /* [27] dLDIFF */
            (int16_t)0x0265,  /* [28] dRDIFF */
            (int16_t)0x0264,  /* [29] mLAPF1 */
            (int16_t)0x01B2,  /* [30] mRAPF1 */
            (int16_t)0x0100,  /* [31] mLAPF2 */
            (int16_t)0x0080,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_STUDIO_C] = {
        .name = "Studio C",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x00E3,  /* [ 3] dAPF1 */
            (int16_t)0x00A9,  /* [ 4] dAPF2 */
            (int16_t)0x6F60,  /* [ 5] vIIR */
            (int16_t)0x4FA8,  /* [ 6] vCOMB1 */
            (int16_t)0xBCE0,  /* [ 7] vCOMB2 */
            (int16_t)0x4510,  /* [ 8] vCOMB3 */
            (int16_t)0xBEF0,  /* [ 9] vCOMB4 */
            (int16_t)0xA680,  /* [10] vWALL */
            (int16_t)0x5680,  /* [11] vAPF1 */
            (int16_t)0x52C0,  /* [12] vAPF2 */
            (int16_t)0x0DFB,  /* [13] mLSAME */
            (int16_t)0x0B58,  /* [14] mRSAME */
            (int16_t)0x0D09,  /* [15] mLCOMB1 */
            (int16_t)0x0A3C,  /* [16] mRCOMB1 */
            (int16_t)0x0BD9,  /* [17] mLCOMB2 */
            (int16_t)0x0973,  /* [18] mRCOMB2 */
            (int16_t)0x0B59,  /* [19] dLSAME */
            (int16_t)0x08DA,  /* [20] dRSAME */
            (int16_t)0x08D9,  /* [21] mLDIFF */
            (int16_t)0x05E9,  /* [22] mRDIFF */
            (int16_t)0x07EC,  /* [23] mLCOMB3 */
            (int16_t)0x04B0,  /* [24] mRCOMB3 */
            (int16_t)0x06EF,  /* [25] mLCOMB4 */
            (int16_t)0x03D2,  /* [26] mRCOMB4 */
            (int16_t)0x05EA,  /* [27] dLDIFF */
            (int16_t)0x031D,  /* [28] dRDIFF */
            (int16_t)0x031C,  /* [29] mLAPF1 */
            (int16_t)0x0238,  /* [30] mRAPF1 */
            (int16_t)0x0154,  /* [31] mLAPF2 */
            (int16_t)0x00AA,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_HALL] = {
        .name = "Hall",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x01A5,  /* [ 3] dAPF1 */
            (int16_t)0x0139,  /* [ 4] dAPF2 */
            (int16_t)0x6000,  /* [ 5] vIIR */
            (int16_t)0x5000,  /* [ 6] vCOMB1 */
            (int16_t)0x4C00,  /* [ 7] vCOMB2 */
            (int16_t)0xB800,  /* [ 8] vCOMB3 */
            (int16_t)0xBC00,  /* [ 9] vCOMB4 */
            (int16_t)0xC000,  /* [10] vWALL */
            (int16_t)0x6000,  /* [11] vAPF1 */
            (int16_t)0x5C00,  /* [12] vAPF2 */
            (int16_t)0x15BA,  /* [13] mLSAME */
            (int16_t)0x11BB,  /* [14] mRSAME */
            (int16_t)0x14C2,  /* [15] mLCOMB1 */
            (int16_t)0x10BD,  /* [16] mRCOMB1 */
            (int16_t)0x11BC,  /* [17] mLCOMB2 */
            (int16_t)0x0DC1,  /* [18] mRCOMB2 */
            (int16_t)0x11C0,  /* [19] dLSAME */
            (int16_t)0x0DC3,  /* [20] dRSAME */
            (int16_t)0x0DC0,  /* [21] mLDIFF */
            (int16_t)0x09C1,  /* [22] mRDIFF */
            (int16_t)0x0BC4,  /* [23] mLCOMB3 */
            (int16_t)0x07C1,  /* [24] mRCOMB3 */
            (int16_t)0x0A00,  /* [25] mLCOMB4 */
            (int16_t)0x06CD,  /* [26] mRCOMB4 */
            (int16_t)0x09C2,  /* [27] dLDIFF */
            (int16_t)0x05C1,  /* [28] dRDIFF */
            (int16_t)0x05C0,  /* [29] mLAPF1 */
            (int16_t)0x041A,  /* [30] mRAPF1 */
            (int16_t)0x0274,  /* [31] mLAPF2 */
            (int16_t)0x013A,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_HALF_ECHO] = {
        .name = "Half Echo",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x0017,  /* [ 3] dAPF1 */
            (int16_t)0x0013,  /* [ 4] dAPF2 */
            (int16_t)0x70F0,  /* [ 5] vIIR */
            (int16_t)0x4FA8,  /* [ 6] vCOMB1 */
            (int16_t)0xBCE0,  /* [ 7] vCOMB2 */
            (int16_t)0x4510,  /* [ 8] vCOMB3 */
            (int16_t)0xBEF0,  /* [ 9] vCOMB4 */
            (int16_t)0x8500,  /* [10] vWALL */
            (int16_t)0x5F80,  /* [11] vAPF1 */
            (int16_t)0x54C0,  /* [12] vAPF2 */
            (int16_t)0x0371,  /* [13] mLSAME */
            (int16_t)0x02AF,  /* [14] mRSAME */
            (int16_t)0x02E5,  /* [15] mLCOMB1 */
            (int16_t)0x01DF,  /* [16] mRCOMB1 */
            (int16_t)0x02B0,  /* [17] mLCOMB2 */
            (int16_t)0x01D7,  /* [18] mRCOMB2 */
            (int16_t)0x0358,  /* [19] dLSAME */
            (int16_t)0x026A,  /* [20] dRSAME */
            (int16_t)0x01D6,  /* [21] mLDIFF */
            (int16_t)0x011E,  /* [22] mRDIFF */
            (int16_t)0x012D,  /* [23] mLCOMB3 */
            (int16_t)0x00B1,  /* [24] mRCOMB3 */
            (int16_t)0x011F,  /* [25] mLCOMB4 */
            (int16_t)0x0059,  /* [26] mRCOMB4 */
            (int16_t)0x01A0,  /* [27] dLDIFF */
            (int16_t)0x00E3,  /* [28] dRDIFF */
            (int16_t)0x0058,  /* [29] mLAPF1 */
            (int16_t)0x0040,  /* [30] mRAPF1 */
            (int16_t)0x0028,  /* [31] mLAPF2 */
            (int16_t)0x0014,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_SPACE_ECHO] = {
        .name = "Space Echo",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x033D,  /* [ 3] dAPF1 */
            (int16_t)0x0231,  /* [ 4] dAPF2 */
            (int16_t)0x7E00,  /* [ 5] vIIR */
            (int16_t)0x5000,  /* [ 6] vCOMB1 */
            (int16_t)0xB400,  /* [ 7] vCOMB2 */
            (int16_t)0xB000,  /* [ 8] vCOMB3 */
            (int16_t)0x4C00,  /* [ 9] vCOMB4 */
            (int16_t)0xB000,  /* [10] vWALL */
            (int16_t)0x6000,  /* [11] vAPF1 */
            (int16_t)0x5400,  /* [12] vAPF2 */
            (int16_t)0x1ED6,  /* [13] mLSAME */
            (int16_t)0x1A31,  /* [14] mRSAME */
            (int16_t)0x1D14,  /* [15] mLCOMB1 */
            (int16_t)0x183B,  /* [16] mRCOMB1 */
            (int16_t)0x1BC2,  /* [17] mLCOMB2 */
            (int16_t)0x16B2,  /* [18] mRCOMB2 */
            (int16_t)0x1A32,  /* [19] dLSAME */
            (int16_t)0x15EF,  /* [20] dRSAME */
            (int16_t)0x15EE,  /* [21] mLDIFF */
            (int16_t)0x1055,  /* [22] mRDIFF */
            (int16_t)0x1334,  /* [23] mLCOMB3 */
            (int16_t)0x0F2D,  /* [24] mRCOMB3 */
            (int16_t)0x11F6,  /* [25] mLCOMB4 */
            (int16_t)0x0C5D,  /* [26] mRCOMB4 */
            (int16_t)0x1056,  /* [27] dLDIFF */
            (int16_t)0x0AE1,  /* [28] dRDIFF */
            (int16_t)0x0AE0,  /* [29] mLAPF1 */
            (int16_t)0x07A2,  /* [30] mRAPF1 */
            (int16_t)0x0464,  /* [31] mLAPF2 */
            (int16_t)0x0232,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_ECHO] = {
        .name = "Echo",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x0001,  /* [ 3] dAPF1 */
            (int16_t)0x0001,  /* [ 4] dAPF2 */
            (int16_t)0x7FFF,  /* [ 5] vIIR */
            (int16_t)0x7FFF,  /* [ 6] vCOMB1 */
            (int16_t)0x0000,  /* [ 7] vCOMB2 */
            (int16_t)0x0000,  /* [ 8] vCOMB3 */
            (int16_t)0x0000,  /* [ 9] vCOMB4 */
            (int16_t)0x8100,  /* [10] vWALL */
            (int16_t)0x0000,  /* [11] vAPF1 */
            (int16_t)0x0000,  /* [12] vAPF2 */
            (int16_t)0x1FFF,  /* [13] mLSAME */
            (int16_t)0x0FFF,  /* [14] mRSAME */
            (int16_t)0x1005,  /* [15] mLCOMB1 */
            (int16_t)0x0005,  /* [16] mRCOMB1 */
            (int16_t)0x0000,  /* [17] mLCOMB2 */
            (int16_t)0x0000,  /* [18] mRCOMB2 */
            (int16_t)0x1005,  /* [19] dLSAME */
            (int16_t)0x0005,  /* [20] dRSAME */
            (int16_t)0x0000,  /* [21] mLDIFF */
            (int16_t)0x0000,  /* [22] mRDIFF */
            (int16_t)0x0000,  /* [23] mLCOMB3 */
            (int16_t)0x0000,  /* [24] mRCOMB3 */
            (int16_t)0x0000,  /* [25] mLCOMB4 */
            (int16_t)0x0000,  /* [26] mRCOMB4 */
            (int16_t)0x0000,  /* [27] dLDIFF */
            (int16_t)0x0000,  /* [28] dRDIFF */
            (int16_t)0x1004,  /* [29] mLAPF1 */
            (int16_t)0x1002,  /* [30] mRAPF1 */
            (int16_t)0x0004,  /* [31] mLAPF2 */
            (int16_t)0x0002,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    },
    [SPU94_PRESET_DELAY] = {
        .name = "Delay",
        .regs = {
            (int16_t)0x0000,  /* [ 0] vLOUT */
            (int16_t)0x0000,  /* [ 1] vROUT */
            (int16_t)0x0000,  /* [ 2] mBASE */
            (int16_t)0x0001,  /* [ 3] dAPF1 */
            (int16_t)0x0001,  /* [ 4] dAPF2 */
            (int16_t)0x7FFF,  /* [ 5] vIIR */
            (int16_t)0x7FFF,  /* [ 6] vCOMB1 */
            (int16_t)0x0000,  /* [ 7] vCOMB2 */
            (int16_t)0x0000,  /* [ 8] vCOMB3 */
            (int16_t)0x0000,  /* [ 9] vCOMB4 */
            (int16_t)0x0000,  /* [10] vWALL */
            (int16_t)0x0000,  /* [11] vAPF1 */
            (int16_t)0x0000,  /* [12] vAPF2 */
            (int16_t)0x1FFF,  /* [13] mLSAME */
            (int16_t)0x0FFF,  /* [14] mRSAME */
            (int16_t)0x1005,  /* [15] mLCOMB1 */
            (int16_t)0x0005,  /* [16] mRCOMB1 */
            (int16_t)0x0000,  /* [17] mLCOMB2 */
            (int16_t)0x0000,  /* [18] mRCOMB2 */
            (int16_t)0x1005,  /* [19] dLSAME */
            (int16_t)0x0005,  /* [20] dRSAME */
            (int16_t)0x0000,  /* [21] mLDIFF */
            (int16_t)0x0000,  /* [22] mRDIFF */
            (int16_t)0x0000,  /* [23] mLCOMB3 */
            (int16_t)0x0000,  /* [24] mRCOMB3 */
            (int16_t)0x0000,  /* [25] mLCOMB4 */
            (int16_t)0x0000,  /* [26] mRCOMB4 */
            (int16_t)0x0000,  /* [27] dLDIFF */
            (int16_t)0x0000,  /* [28] dRDIFF */
            (int16_t)0x1004,  /* [29] mLAPF1 */
            (int16_t)0x1002,  /* [30] mRAPF1 */
            (int16_t)0x0004,  /* [31] mLAPF2 */
            (int16_t)0x0002,  /* [32] mRAPF2 */
            (int16_t)0x8000,  /* [33] vLIN */
            (int16_t)0x8000   /* [34] vRIN */
        }
    }
};

/* -------------------------------------------------------------------------- */
/* spu94_load_preset -- Phase 5 Plan 03 (API-05, D-08)                        */
/* -------------------------------------------------------------------------- */

/* Atomic bulk preset loader. Iterates the 35-register table for `id` and
 * dispatches to the Phase 2 engine-layer setters by signedness family.
 * ADR-0005's per-register write-policy table routes each call to IMMEDIATE
 * or TICK_LATCHED automatically -- no preset-specific bypass. The "half-
 * applied" window (v-prefix active now, d-prefix and m-prefix pending until
 * next tick) is the D-08 contract; inaudible at 22.05 kHz tick rate (~45 us).
 */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id) {
    /* Null-safe per lifecycle convention (D-12 carried from Phase 2). */
    if (state == NULL) return SPU94_OK;
    /* Bounds-check the id. SPU94_PRESET__COUNT is the sentinel = 10; any id
     * >= 10 is out of range. Defensive: also reject negatives (caller may
     * pass -1 cast). No register write occurs on rejection (T-5-3 mitigation). */
    if ((int)id < 0 || (int)id >= (int)SPU94_PRESET__COUNT) {
        return SPU94_UNKNOWN_REG;
    }
    const spu94_preset_t *p = &spu94_presets[id];
    /* Iterate every register in enum order; dispatch to the correct engine-
     * layer setter based on the register's signedness family. */
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        const int16_t raw = p->regs[r];
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            /* Engine-layer setter. Return value ignored -- any clamp is
             * bit-faithful per ADR-0008 and a preset cannot trigger
             * TYPE_MISMATCH (signedness matched by construction). */
            (void)spu94_set_reg_i16(state, (spu94_reg_t)r, raw);
        } else {
            /* U16 family: reinterpret the 16 bits. The preset table stores
             * uint16 bit-patterns in an int16 slot; the cast preserves the
             * exact bit pattern. */
            (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)raw);
        }
    }
    return SPU94_OK;
}
