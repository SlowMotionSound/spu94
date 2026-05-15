#ifndef SPU94_GAUSS_H
#define SPU94_GAUSS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PS1 SPU Gaussian interpolation table — 512 signed 16-bit entries.
 * Dumped from SPU silicon by nocash (psx-spx). Verified against
 * DuckStation and Mednafen; all three sources match exactly. */
extern const int16_t spu94_gauss_table[512];

#ifdef __cplusplus
}
#endif

#endif /* SPU94_GAUSS_H */
