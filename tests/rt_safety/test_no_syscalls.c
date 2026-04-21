/* Enable POSIX sigaction (C11 + -std=c11 + no compiler extensions). Must
 * precede any standard-header include. */
#define _POSIX_C_SOURCE 199309L

/* tests/rt_safety/test_no_syscalls.c -- Phase 5 Plan 04, D-09c.
 *
 * Signal-bracketed steady-state harness. strace wrapper (test_no_syscalls.sh)
 * parses the log for the two SIGUSR1 markers and asserts zero non-signal
 * syscalls between them. Init / teardown syscalls (mmap, brk, arch_prctl,
 * etc.) happen BEFORE the first SIGUSR1 and AFTER the second; they are
 * excluded from the steady-state count by the parser.
 *
 * Per 05-RESEARCH.md § D-09c: this isolates the RT-safety contract from
 * normal process startup noise. The 10^5-iteration block is large enough
 * to surface any accidental per-call syscall (one accidental clock_gettime
 * call in the hot path would show up as 10^5 syscalls in the window).
 *
 * NOTE on signal handler: SIGUSR1 must have an installed handler (or be
 * ignored) -- the POSIX default action is to terminate the process. We
 * install a no-op handler during init so the raise() calls deliver-and-
 * return. sigaction() calls happen BEFORE the first marker and are
 * excluded from the steady-state count (rt_sigaction syscalls appear
 * only in the init region of the strace log). The rt_sigreturn frames
 * for the two handler exits that DO fall in the steady-state region are
 * explicitly subtracted by the shell parser.
 */
#include <spu94/spu94.h>
#include <signal.h>
#include <stdalign.h>
#include <stdint.h>

static void sigusr1_noop(int sig) { (void)sig; }

int main(void) {
    /* Install a no-op SIGUSR1 handler so raise() delivers-and-returns
     * instead of terminating the process. Done BEFORE the first marker
     * so the sigaction syscall is in the init-exclude region. */
    struct sigaction sa = {0};
    sa.sa_handler = sigusr1_noop;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGUSR1, &sa, NULL) != 0) return 2;

    /* All init here -- syscalls during init (brk, mmap, arch_prctl,
     * sigaction, exit_group, etc.) happen BEFORE the first SIGUSR1
     * marker and are excluded from the count. */
    static alignas(SPU94_STATE_ALIGN_MAX)
        unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    static unsigned char work_buf[64 * 1024];

    spu94_state *state = spu94_init(state_buf, sizeof state_buf,
                                    work_buf, sizeof work_buf);
    if (state == NULL) return 1;
    (void)spu94_load_preset(state, SPU94_PRESET_HALL);

    /* Stack buffers -- no further allocation in the steady state. */
    int16_t Lin[1024]  = {0};
    int16_t Rin[1024]  = {0};
    int16_t Lout[1024];
    int16_t Rout[1024];

    /* Marker #1: start of steady state. SIGUSR1 itself does count as a
     * syscall in strace output, but the sh wrapper locates markers by
     * the "--- SIGUSR1 ---" signal-delivery lines (which are NOT
     * normal syscall lines); the rt_sigreturn frame exit is also
     * subtracted as per the sh parser. */
    raise(SIGUSR1);

    /* Steady-state loop: 10^5 iterations of spu94_process with a 1024-sample
     * block. The loop variable and buffers are stack-resident; no new
     * allocations, no I/O, no signal handlers fire during the loop. */
    for (int i = 0; i < 100000; i++) {
        spu94_process(state, Lin, Rin, Lout, Rout, 1024);
    }

    /* Marker #2: end of steady state. */
    raise(SIGUSR1);

    /* Teardown -- syscalls here excluded from the count. */
    spu94_flush(state, Lout, Rout, 1024);
    spu94_destroy(state);
    return 0;
}
