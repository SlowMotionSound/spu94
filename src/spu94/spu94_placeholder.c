/* spu94_placeholder.c
 * Phase 1 placeholder translation unit. Replaced in Phase 2 when real
 * reverb state machinery lands (CORE-03, CORE-04).
 * Kept non-empty so libspu94 has something to compile and link.
 * Uses only int16_t / int32_t / void — no forbidden tokens per BUILD-07.
 * (See scripts/ci/grep-guard.sh and Plan 01-02 for the forbidden-token list.)
 */
#include <spu94/spu94.h>
#include <stdint.h>

/* Version token. Not part of the public API; internal use only.
 * Phase 2 will replace with a real version macro. */
const int32_t spu94_internal_version = 0;
