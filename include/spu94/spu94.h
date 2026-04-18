#ifndef SPU94_H
#define SPU94_H

#ifdef __cplusplus
extern "C" {
#endif

/* SPU-94 public API umbrella header.
 * Phase 1: exposes only the Q15 helper surface (spu94_q15.h).
 * Phase 2+: adds spu94_state, init/reset/destroy, register I/O, spu94_process.
 */
#include <spu94/spu94_q15.h>

#ifdef __cplusplus
}
#endif

#endif /* SPU94_H */
