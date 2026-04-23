/* Enable POSIX raise() + sigaction. */
#define _POSIX_C_SOURCE 199309L

/* tests/rt_safety/hotpath_alloc_gate_target_with_malloc.c
 *
 * NEGATIVE META-TEST TARGET (D-20): identical in shape to
 * hotpath_alloc_gate_target.c EXCEPT that between the two SIGUSR1
 * markers we call malloc(64)/free(p) once per iteration, which routes
 * through `brk` or `mmap` in glibc and MUST be detected by the gate.
 *
 * This target is run through the same hotpath_alloc_gate.sh wrapper;
 * the expected outcome is exit 1 (heap syscall(s) reported). Proves
 * the gate is actually detecting real hits rather than passing on
 * every binary -- compensates for the risk that a bug in the parser
 * silently ignores everything.
 *
 * Never ship or link against anything real. Used only by ctest
 * (WILL_FAIL gate) + the pytest meta-test.
 */
#include <spu94/spu94.h>
#include <signal.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>   /* malloc / free -- the intentional poison */

#define ITER           100000u
#define N_SAMPLES      1024u
#define WORK_BUF_BYTES 524288u

static void sigusr1_noop(int sig) { (void)sig; }

static alignas(SPU94_STATE_ALIGN_MAX)
    unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static alignas(16) unsigned char work_buf[WORK_BUF_BYTES];
static int16_t Lin[N_SAMPLES], Rin[N_SAMPLES];
static int16_t Lout[N_SAMPLES], Rout[N_SAMPLES];

/* Sink volatile pointer: prevents the compiler from optimizing out the
 * malloc/free pair. Without a side effect visible to the compiler, a
 * pure malloc-then-free on a never-read pointer is dead code under -O2
 * and gets elided (we measured this: `nm` on the built binary showed
 * no undefined reference to malloc). Writing `p` to a volatile sink
 * forces the allocator call to be emitted. */
static void * volatile g_sink;

int main(void) {
    struct sigaction sa = {0};
    sa.sa_handler = sigusr1_noop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) return 2;

    spu94_state *s = spu94_init(state_buf, sizeof state_buf,
                                work_buf,  sizeof work_buf);
    if (s == NULL) return 1;
    (void)spu94_load_preset(s, SPU94_PRESET_HALL);

    for (unsigned i = 0; i < N_SAMPLES; i++) {
        Lin[i] = Rin[i] = (int16_t)(i & 0x3FFF);
    }

    raise(SIGUSR1);

    /* Poisoned hot loop: every iteration performs a heap round-trip
     * (malloc -> spu94_process -> free). We allocate 1 MiB -- well above
     * glibc's M_MMAP_THRESHOLD (default 128 KiB) -- so the allocator
     * routes each call through mmap/munmap directly (freelist reuse
     * bypasses brk for small sizes but cannot satisfy a 1 MiB alloc,
     * so mmap always fires). Only 16 iterations are needed to prove
     * the gate detects heap hits; keeping the loop short keeps this
     * negative target fast. The positive target still runs the full
     * 10^5 ITER to prove the clean path handles the worst case. */
    for (unsigned i = 0; i < 16u; i++) {
        void *p = malloc(1u << 20);  /* 1 MiB -- forces mmap */
        g_sink = p;                   /* defeat dead-code elimination */
        spu94_process(s, Lin, Rin, Lout, Rout, N_SAMPLES);
        free(p);
    }

    raise(SIGUSR1);

    spu94_destroy(s);
    return 0;
}
