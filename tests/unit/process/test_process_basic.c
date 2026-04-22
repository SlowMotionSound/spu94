/* tests/unit/process/test_process_basic.c -- Phase 5 Plan 02 Task 2
 *
 * Block-loop correctness tests for spu94_process:
 *   1. NULL state is a safe no-op
 *   2. num_samples == 0 leaves output buffers untouched (sentinel fingerprint)
 *   3. Silence in -> silence out (Off-equivalent: all registers zero)
 *   4. Unit impulse + direct output path wired -> non-zero output in the
 *      [56, 60] window around SPU94_LATENCY_SAMPLES (=58) + zero-before-peak
 *   5. In-place aliasing (L_out == L_in, R_out == R_in) doesn't crash
 *
 * Rationale for test 4: Plan 02 does not depend on spu94_load_preset (Plan
 * 03). We directly poke state->reg_values[] for vLOUT/vROUT + the active
 * path, bypassing the engine-layer setters' TICK_LATCHED policy for test
 * isolation. This is the same pattern reverb/test_reverb_body.c uses to
 * bypass the write-timing policy when running whole-body equivalence
 * checks. Test asserts:
 *   - output[0..55] is all zero (group delay hasn't propagated yet)
 *   - |output[k]| is nonzero for SOME k in [56, 60] (tied-peak window
 *     per SPU94_LATENCY_SAMPLES = 58 +/- 1, same tolerance
 *     test_fir_chain_latency uses)
 * Exact magnitude is not asserted -- that's Phase 4's domain, already
 * covered.  This test pins the block-loop + group-delay-propagation
 * contract of spu94_process itself.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include "spu94_state_internal.h"
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
}
void tearDown(void) { state = NULL; }

/* Test 1: NULL state -> safe no-op (no crash). */
static void test_process_null_state_noop(void) {
    int16_t Lin[4] = {1, 2, 3, 4};
    int16_t Rin[4] = {-1, -2, -3, -4};
    int16_t Lout[4] = {0, 0, 0, 0};
    int16_t Rout[4] = {0, 0, 0, 0};
    spu94_process(NULL, Lin, Rin, Lout, Rout, 4);  /* no crash == pass */
    /* No crash is the pass criterion; Unity reaches here iff we didn't
     * SIGSEGV / abort. */
    TEST_PASS();
}

/* Test 2: num_samples == 0 -> output buffers untouched (sentinel). */
static void test_process_zero_length_noop(void) {
    int16_t Lin[1]  = {0};
    int16_t Rin[1]  = {0};
    int16_t Lout[1] = {(int16_t)0xABCD};
    int16_t Rout[1] = {(int16_t)0x5678};
    spu94_process(state, Lin, Rin, Lout, Rout, 0);
    TEST_ASSERT_EQUAL_INT16((int16_t)0xABCD, Lout[0]);
    TEST_ASSERT_EQUAL_INT16((int16_t)0x5678, Rout[0]);
}

/* Test 3: silence in, silence out. All registers default to zero after
 * spu94_init, so vLIN=vRIN=vLOUT=vROUT=0 and every reverb coefficient is
 * zero. Output must be identically zero for zero input. */
static void test_process_silence_in_silence_out(void) {
    int16_t Lin[1000] = {0};
    int16_t Rin[1000] = {0};
    int16_t Lout[1000];
    int16_t Rout[1000];
    /* Pre-fill output with sentinel so we can detect any write. */
    for (int i = 0; i < 1000; i++) {
        Lout[i] = (int16_t)0x1234;
        Rout[i] = (int16_t)0x5678;
    }
    spu94_process(state, Lin, Rin, Lout, Rout, 1000);
    for (int i = 0; i < 1000; i++) {
        TEST_ASSERT_EQUAL_INT16(0, Lout[i]);
        TEST_ASSERT_EQUAL_INT16(0, Rout[i]);
    }
}

/* Test 4 (RETIRED under ADR-Phase-6-G): unit impulse -> peak near latency.
 *
 * Under ADR-Phase-6-G wet-only wiring (Phase 6 Plan 06),
 * spu94_process's 44.1 kHz output is the reverb body's WET output
 * scaled by vLOUT/vROUT, NOT a dry-passthrough of the impulse
 * through the half-band FIR. With all registers zero (the Test 4
 * setup), vLIN/vRIN are 0 so the reverb input is silenced before it
 * reaches the body, and vLOUT/vROUT are 0 so the output would be
 * gated anyway. Either way, the impulse cannot propagate through
 * spu94_process to produce a dry-FIR-shaped peak at t = LATENCY.
 *
 * The internal-FIR group-delay contract is still pinned by
 * tests/unit/fir/test_fir_chain_latency.c (which uses
 * spu94_fir_chain_step_reverb_bypass -- the test-only dry
 * passthrough preserved for DSP-level FIR testing per the updated
 * docstring in src/spu94/spu94_fir_internal.h). The behavioral
 * audibility contract for spu94_process is pinned by
 * tests/unit/process/test_process_reverb_audible.c instead.
 *
 * This stub passes to keep the test-registration layout stable;
 * deletion would shift Unity test IDs in this TU and mask future
 * regressions more than it fixes. Do NOT revive the original body
 * without also reviving the dry-passthrough semantics for
 * spu94_process, which would contradict ADR-Phase-6-G.
 */
static void test_process_impulse_peak_near_latency(void) {
    TEST_PASS_MESSAGE("retired under ADR-Phase-6-G; see test_process_reverb_audible");
}

/* Test 5: in-place aliasing (L_out == L_in, R_out == R_in) is safe.
 * Bit-identity vs out-of-place is Plan 05's job; Plan 02 just proves
 * the sample-at-a-time loop doesn't trip UBSan / doesn't crash. */
static void test_process_inplace_no_crash(void) {
    int16_t buf_l[128];
    int16_t buf_r[128];
    for (int i = 0; i < 128; i++) {
        buf_l[i] = (int16_t)(i - 64);
        buf_r[i] = (int16_t)(64 - i);
    }
    /* In-place: L_out == L_in and R_out == R_in (both aliased). */
    spu94_process(state, buf_l, buf_r, buf_l, buf_r, 128);
    TEST_PASS();  /* No crash / no UBSan trip == pass. */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_process_null_state_noop);
    RUN_TEST(test_process_zero_length_noop);
    RUN_TEST(test_process_silence_in_silence_out);
    RUN_TEST(test_process_impulse_peak_near_latency);
    RUN_TEST(test_process_inplace_no_crash);
    return UNITY_END();
}
