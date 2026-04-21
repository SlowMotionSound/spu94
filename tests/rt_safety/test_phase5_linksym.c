/* tests/rt_safety/test_phase5_linksym.c -- Phase 5 Plan 04.
 *
 * Minimal harness that references every Phase 5 public symbol so the
 * static linker pulls their code + transitive dependencies into this
 * binary. nm / readelf assertions in test_no_heap.sh + verify-no-locks.sh
 * run against this binary's link closure + against libspu94.so; any
 * heap-allocator or lock symbol reached by the Phase 5 code paths
 * surfaces here even if other code in the library doesn't reach it.
 *
 * The main() runs one full process + flush + load_preset cycle so any
 * lazy-init or constructor code also runs during the nm-audit window.
 * The audit is a STATIC-linker check (nm -u on the binary), not a
 * runtime check -- main() does NOT need to exit cleanly for the audit
 * to be valid. Return code still propagated for sanity.
 */
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>

int main(void) {
    /* Caller-owned state + work buffers. No heap. */
    static alignas(SPU94_STATE_ALIGN_MAX)
        unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    static unsigned char work_buf[64 * 1024];

    spu94_state *state = spu94_init(state_buf, sizeof state_buf,
                                    work_buf, sizeof work_buf);
    if (state == NULL) return 1;

    /* Reference spu94_load_preset (Phase 5 symbol #3). */
    (void)spu94_load_preset(state, SPU94_PRESET_HALL);

    int16_t Lin[64] = {0}, Rin[64] = {0};
    int16_t Lout[64], Rout[64];

    /* Reference spu94_process (Phase 5 symbol #1). */
    spu94_process(state, Lin, Rin, Lout, Rout, 64);

    /* Reference spu94_flush (Phase 5 symbol #2). */
    spu94_flush(state, Lout, Rout, 64);

    spu94_destroy(state);
    return 0;
}
