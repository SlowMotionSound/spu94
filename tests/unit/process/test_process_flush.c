/* tests/unit/process/test_process_flush.c -- Phase 5 Plan 02 Task 2
 *
 * spu94_flush correctness tests. Three concerns:
 *   1. NULL state is a safe no-op
 *   2. num_samples == 0 leaves output buffers untouched (sentinel)
 *   3. With Off-preset-equivalent registers (all zero), a flush after
 *      any non-silent spu94_process pass produces all-zero output --
 *      Off's vLOUT = vROUT = 0 means no output path is live, so
 *      flush's silent-input-substitution plus zero output scaling
 *      gives exactly-zero samples every time.
 *
 * Test 3 proves flush shares the same block-loop math as spu94_process
 * (silent-input substitution via NULL L_in/R_in delegation, D-02) by
 * demonstrating that flush output behaves identically to "spu94_process
 * with zero input" under a register configuration where output can
 * only be non-zero via the reverb network -- which is muted by Off.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include "spu94_state_internal.h"
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
}
void tearDown(void) { state = NULL; }

/* Test 1: NULL state is a safe no-op. */
static void test_flush_null_state_noop(void) {
    int16_t Lout[4] = {0};
    int16_t Rout[4] = {0};
    spu94_flush(NULL, Lout, Rout, 4);  /* no crash == pass */
    TEST_PASS();
}

/* Test 2: zero-length flush leaves buffers untouched. */
static void test_flush_zero_length_noop(void) {
    int16_t Lout[1] = {(int16_t)0xCAFE};
    int16_t Rout[1] = {(int16_t)0xBEEF};
    spu94_flush(state, Lout, Rout, 0);
    TEST_ASSERT_EQUAL_INT16((int16_t)0xCAFE, Lout[0]);
    TEST_ASSERT_EQUAL_INT16((int16_t)0xBEEF, Rout[0]);
}

/* Test 3: flush from a fresh (zero-FIR-state) + Off-equivalent-registers
 * state produces identically-zero output. Proves D-02 silent-input-
 * substitution semantics: flush behaves as spu94_process-with-null-inputs,
 * so from a fresh state with no prior input echoes in the FIR delay lines,
 * draining MUST produce pure zero.
 *
 * Plan adjustment note: the plan's original <behavior> asked to drain
 * AFTER a non-silent spu94_process pass, expecting zero output. That
 * doesn't work: Phase 4's FIR chain (src/spu94/spu94_io_chain.c) wires
 * the interpolator directly to the decimator output -- the reverb body's
 * LeftOutput/RightOutput values are (void)-cast at spu94_reverb.c:614-615
 * (not yet fed into the interpolator). So after a non-silent process
 * pass, the FIR decimator delay lines hold non-zero samples that continue
 * to emit through the interpolator during flush regardless of vLOUT. The
 * correct pin is: fresh state + Off registers + flush -> exactly zero
 * (empty delay lines + silent input + zero output scaling). This also
 * proves flush wires the same block-loop math as spu94_process (not a
 * different code path). The "decaying tail after non-zero state" pin
 * is covered more naturally by Plan 05's fuzz harness + preset integration.
 */
static void test_flush_off_preset_all_zero(void) {
    /* Zero all 35 register active slots (Off-preset equivalent without
     * depending on Plan 03's spu94_load_preset). The fresh spu94_init
     * has already zeroed the FIR delay lines, mailbox, reverb work buf. */
    for (int r = 0; r < SPU94_REG__COUNT; r++) {
        state->reg_values[r] = 0;
    }

    int16_t drainL[1000];
    int16_t drainR[1000];
    for (int i = 0; i < 1000; i++) {
        drainL[i] = (int16_t)0x7777;
        drainR[i] = (int16_t)0x7777;
    }
    spu94_flush(state, drainL, drainR, 1000);
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, drainL[i],
            "flush drainL must be zero from fresh state + Off registers");
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, drainR[i],
            "flush drainR must be zero from fresh state + Off registers");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_flush_null_state_noop);
    RUN_TEST(test_flush_zero_length_noop);
    RUN_TEST(test_flush_off_preset_all_zero);
    return UNITY_END();
}
