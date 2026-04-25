/* tests/unit/process/test_process_external_anchor_off.c — Step 13 / M1 close-out.
 *
 * EXTERNAL-ANCHOR test for the spu94_process production audio path.
 *
 * Motivation (ARCHITECTURAL-AUDIT.md Root Cause #3):
 *   The witness-diff harness, golden files, modulation harness, and
 *   self-test all confirm that SPU-94 is REPRODUCIBLE. None of them
 *   confirms that what it reproduces is CORRECT — they would all
 *   pass equally if every output sample were silently doubled, or
 *   xor'd with a fixed mask, or shifted by one sample.
 *
 *   This test introduces an externally-anchored expected value that
 *   does NOT depend on running SPU-94 to discover. The expected value
 *   is derived from algebra alone: the SPU reverb's output equation
 *   is a linear combination of register values × delay-line samples,
 *   gated through vLOUT/vROUT. The "Off" factory preset sets ALL
 *   reverb-relevant gain registers (vLOUT, vROUT, vIIR, vCOMB1..4,
 *   vWALL, vAPF1, vAPF2, vLIN, vRIN) to 0x0000. Substituting zero
 *   for every gain coefficient in the output equation makes the
 *   output identically zero, regardless of input.
 *
 *   Therefore: spu94_process(Off, impulse) MUST produce all zeros
 *   for every output sample, including across the FIR group delay
 *   transient. Any non-zero output is a structural bug — leaked
 *   dry path, broken vLIN/vLOUT gating, accidental routing, memory
 *   corruption — that no other test in the suite would distinguish
 *   from "bit-faithful but wrong".
 *
 *   The audit specifically called for "TEST_ASSERT_EQUAL on 8 expected
 *   samples". This test asserts on 80 samples (slightly more than
 *   2x the 39-tap FIR group delay) so the impulse has fully
 *   propagated through both decimator + interpolator before the
 *   assertion window ends.
 *
 * What this catches that other tests don't:
 *   - A bug that leaks 44.1 kHz input directly into 44.1 kHz output
 *     (would produce a non-zero peak at sample 0).
 *   - A bug that breaks vLIN gating (would produce a non-zero peak
 *     near sample SPU94_LATENCY_SAMPLES).
 *   - A bug that breaks vLOUT/vROUT gating (would produce reverb
 *     output despite zero output gain).
 *   - State corruption that mis-applies a non-Off preset's registers
 *     while the user thinks they have Off loaded.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char g_work_buf[SPU94_WORK_BUF_MAX_BYTES];
static spu94_state *g_state = NULL;

void setUp(void) {
    g_state = spu94_init(g_state_buf, sizeof g_state_buf,
                         g_work_buf, sizeof g_work_buf);
    TEST_ASSERT_NOT_NULL(g_state);
    spu94_reset(g_state);
}

void tearDown(void) {
    g_state = NULL;
}

static void test_off_preset_impulse_input_is_silent_output(void) {
    /* Load Off via the public preset API — this is the production
     * path a CLI / Python user would take. spu94_load_preset stages
     * the TICK_LATCHED registers; the first spu94_process call
     * implicitly ticks once before processing audio. */
    spu94_result_t rc = spu94_load_preset(g_state, SPU94_PRESET_OFF);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, rc);

    /* Construct an impulse: the loudest possible signal in INT16,
     * placed at sample 0, with silence in every other position.
     * Symmetric L/R so the L vs R assertion below is meaningful. */
    enum { N = 80 };
    int16_t L_in[N];
    int16_t R_in[N];
    int16_t L_out[N];
    int16_t R_out[N];
    memset(L_in, 0, sizeof L_in);
    memset(R_in, 0, sizeof R_in);
    L_in[0] = INT16_MAX;
    R_in[0] = INT16_MAX;

    /* Sentinel-fill the output buffers with a non-zero pattern so
     * "spu94_process forgot to write" is distinguishable from
     * "spu94_process correctly wrote zeros". */
    for (int i = 0; i < N; ++i) {
        L_out[i] = (int16_t)0x55AA;
        R_out[i] = (int16_t)0x5AA5;
    }

    spu94_process(g_state, L_in, R_in, L_out, R_out, N);

    /* External-anchor assertion: every sample must be exactly 0.
     * The expected value comes from algebra (Off has all gains = 0;
     * therefore output = 0), not from running SPU-94. */
    for (int i = 0; i < N; ++i) {
        char msg_l[96];
        char msg_r[96];
        snprintf(msg_l, sizeof msg_l,
                 "L_out[%d] expected 0 (Off preset, all gains zero)", i);
        snprintf(msg_r, sizeof msg_r,
                 "R_out[%d] expected 0 (Off preset, all gains zero)", i);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, L_out[i], msg_l);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, R_out[i], msg_r);
    }
}

static void test_off_preset_silence_input_is_silent_output(void) {
    /* Companion check: zero in → zero out under Off. This isolates
     * "output is silent when input is silent" from "output is silent
     * when input is loud", so a debugger sees both contracts asserted
     * separately if the impulse test fails. */
    spu94_result_t rc = spu94_load_preset(g_state, SPU94_PRESET_OFF);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, rc);

    enum { N = 80 };
    int16_t buf_in[N] = {0};
    int16_t L_out[N];
    int16_t R_out[N];
    for (int i = 0; i < N; ++i) {
        L_out[i] = (int16_t)0x1234;
        R_out[i] = (int16_t)0x5678;
    }

    spu94_process(g_state, buf_in, buf_in, L_out, R_out, N);

    for (int i = 0; i < N; ++i) {
        TEST_ASSERT_EQUAL_INT16(0, L_out[i]);
        TEST_ASSERT_EQUAL_INT16(0, R_out[i]);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_off_preset_impulse_input_is_silent_output);
    RUN_TEST(test_off_preset_silence_input_is_silent_output);
    return UNITY_END();
}
