/* tests/api/c99_consumer.c
 * API-07 compile-only test: prove include/spu94/spu94.h compiles under
 * -std=c99 -pedantic -Werror -Wall -Wextra. This file does not exercise the
 * runtime contract -- it proves the header's FREESTANDING-C99 claim. Runtime
 * tests live in tests/unit/state/.
 *
 * Linking against spu94_static additionally proves every Plan-01 public
 * symbol resolves at link time.
 */
#include <spu94/spu94.h>
#include <stdint.h>
#include <stddef.h>

int main(void) {
    /* Reference every Plan-01 public symbol so the linker proves each is present. */
    size_t s = spu94_state_size();
    (void)s;

    spu94_state *p = spu94_init((void *)0, 0, (void *)0, 0); /* expected NULL */
    (void)p;
    spu94_reset((spu94_state *)0);
    spu94_destroy((spu94_state *)0);

    /* Touch each enum value so an accidental reorder/remove fails to compile. */
    spu94_result_t r0 = SPU94_OK;
    spu94_result_t r1 = SPU94_CLAMPED;
    spu94_result_t r2 = SPU94_UNKNOWN_REG;
    spu94_result_t r3 = SPU94_TYPE_MISMATCH;
    (void)r0; (void)r1; (void)r2; (void)r3;

    /* Exercise the size/alignment macros so a future removal fails here. */
    size_t bound = (size_t)SPU94_STATE_SIZE_MAX;
    size_t align = (size_t)SPU94_STATE_ALIGN_MAX;
    (void)bound; (void)align;

    return 0;
}
