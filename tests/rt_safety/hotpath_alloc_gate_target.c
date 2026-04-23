/* Enable POSIX raise() + sigaction (C11 + -std=c11 + no extensions). Must
 * precede any standard-header include. Matches test_no_syscalls.c. */
#define _POSIX_C_SOURCE 199309L

/* tests/rt_safety/hotpath_alloc_gate_target.c -- BUILD-06 D-20 hot-path target.
 *
 * Companion binary for tests/rt_safety/hotpath_alloc_gate.sh.
 *
 * Initializes SPU-94 with the HALL preset, brackets a 10^5-iteration
 * spu94_process hot loop with two SIGUSR1 markers, cleans up. The shell
 * wrapper invokes this binary under `strace -e trace=brk,mmap,mmap2,munmap,
 * mremap`; any heap syscall between the two marker lines fails the gate.
 *
 * Pattern mirror of tests/rt_safety/test_no_syscalls.c (Phase 5 Plan 04).
 * Phase 5's gate was a broad zero-syscall gate; this Phase 7 variant narrows
 * the filter to heap-only syscalls and pairs with a NEGATIVE meta-target
 * (hotpath_alloc_gate_target_with_malloc.c) that proves the gate detects
 * real malloc hits rather than just passing on every binary.
 */
#include <spu94/spu94.h>
#include <signal.h>
#include <stdalign.h>
#include <stdint.h>

/* ITER 100000 -> 10^8 samples processed across the hot window -- large
 * enough that any accidental per-call heap syscall would produce >= 10^5
 * hits in the strace log. */
#define ITER         100000u
#define N_SAMPLES    1024u
/* 512 KB -- matches the PS1 SPU reverb RAM ceiling; large enough for the
 * long-tail presets (Hall, Space Echo, Delay) to never hit the wrap edge
 * inside the 10^5 iterations. */
#define WORK_BUF_BYTES 524288u

static void sigusr1_noop(int sig) { (void)sig; }

static alignas(SPU94_STATE_ALIGN_MAX)
    unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static alignas(16) unsigned char work_buf[WORK_BUF_BYTES];
static int16_t Lin[N_SAMPLES], Rin[N_SAMPLES];
static int16_t Lout[N_SAMPLES], Rout[N_SAMPLES];

int main(void) {
    /* No-op SIGUSR1 handler installed BEFORE the first marker so the
     * rt_sigaction syscall lands in the init-exclude region. Default
     * action would terminate the process -- not useful for strace
     * bracket parsing. */
    struct sigaction sa = {0};
    sa.sa_handler = sigusr1_noop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) return 2;

    spu94_state *s = spu94_init(state_buf, sizeof state_buf,
                                work_buf,  sizeof work_buf);
    if (s == NULL) return 1;
    (void)spu94_load_preset(s, SPU94_PRESET_HALL);

    /* Trivial deterministic input pattern -- any pattern works; we only
     * care about syscall surface, not audio output. */
    for (unsigned i = 0; i < N_SAMPLES; i++) {
        Lin[i] = Rin[i] = (int16_t)(i & 0x3FFF);
    }

    /* START marker: strace logs '--- SIGUSR1 ---' on delivery; the shell
     * parser locates it via grep and counts heap syscalls in the window
     * strictly BETWEEN the two markers. */
    raise(SIGUSR1);

    for (unsigned i = 0; i < ITER; i++) {
        spu94_process(s, Lin, Rin, Lout, Rout, N_SAMPLES);
    }

    /* END marker */
    raise(SIGUSR1);

    spu94_destroy(s);
    return 0;
}
