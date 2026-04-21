/* tests/unit/process/test_process_mix_bus.c -- Phase 5 Plan 02 Task 2
 *
 * D-05 mailbox proof tests for struct spu94_state::mix_bus_l / mix_bus_r.
 * Three concerns:
 *   1. Post-init: mix_bus_l == mix_bus_r == 0 (spu94_init wholesale zero-fill)
 *   2. Post-reset after non-zero writes: mix_bus_l == mix_bus_r == 0
 *   3. Mailbox write followed by spu94_tick is observed by spu94_reverb_body
 *      -- proven via state->overflow_magnitude
 *
 * PROOF MECHANISM (test 3):
 * ------------------------
 * Note on plan adjustment: the plan's <behavior> sketch proposed proving
 * mailbox observation via state->err_input_scale. That doesn't work --
 * spu94_reverb_input_scale (src/spu94/spu94_reverb.c:81-91) performs
 * a plain int16 x int16 -> int32 multiply (no >>15 shift at that stage),
 * so err_input_scale is permanently zero by construction regardless of
 * inputs. The comment at line 86 of spu94_reverb.c states this
 * explicitly: "err_input_scale stays zero -- no truncation at this
 * stage (no >>15 shift yet). Field exists for symmetry per D-11."
 *
 * Instead we use state->overflow_magnitude -- the hard_clip stage
 * observable. It records sum(|x| - INT16_MAX) for int32 inputs
 * whose magnitude exceeds INT16_MAX. Mechanism:
 *   - mix_bus_l = 0x0123 (291), vLIN = 0x4000 (16384 = Q15 0.5)
 *   - spu94_reverb_input_scale: Lin_wide = 291 * 16384 = 4,767,744 = 0x0048_C000
 *   - Since 0x48C000 > INT16_MAX (32767), spu94_reverb_hard_clip
 *     increments overflow_magnitude by (0x48C000 - 32767) = ~4,735,000
 *   - Control case (mailbox = 0): Lin_wide = 0, no overflow.
 *
 * This is a direct, robust proof that the reverb body read the mailbox:
 * zero mailbox -> zero overflow_magnitude; non-zero mailbox (with the
 * same non-zero vLIN/vRIN) -> non-zero overflow_magnitude. If the
 * reverb body hardcoded left_in/right_in = 0 (pre-Phase-5 behavior),
 * overflow_magnitude would remain zero. The test fails iff mix_bus_l/r
 * aren't being read.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include "spu94_state_internal.h"
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

/* Test 1: post-init, mix_bus_l/r are both zero. */
static void test_mix_bus_init_zero(void) {
    TEST_ASSERT_EQUAL_INT16(0, state->mix_bus_l);
    TEST_ASSERT_EQUAL_INT16(0, state->mix_bus_r);
}

/* Test 2: spu94_reset clears mailbox even after non-zero writes. */
static void test_mix_bus_reset_clears(void) {
    state->mix_bus_l = (int16_t)12345;
    state->mix_bus_r = (int16_t)-12345;
    spu94_reset(state);
    TEST_ASSERT_EQUAL_INT16(0, state->mix_bus_l);
    TEST_ASSERT_EQUAL_INT16(0, state->mix_bus_r);
}

/* Test 3: mailbox writes are observed by the reverb body (D-05 proof).
 * See file header for mechanism. Uses overflow_magnitude (not
 * err_input_scale -- the plan's proposed field is zero by construction
 * as documented at spu94_reverb.c:86). */
static void test_mix_bus_tick_observes_write(void) {
    /* Configure vLIN/vRIN so that a non-zero mix-bus product exceeds
     * INT16_MAX and drives hard_clip's overflow accumulator. 0x4000
     * is Q15 = 0.5; combined with mix_bus ~0x0123 the product is
     * 0x48C000 (well above 0x7FFF = INT16_MAX). */
    state->reg_values[SPU94_REG_vLIN] = (int16_t)0x4000;
    state->reg_values[SPU94_REG_vRIN] = (int16_t)0x4000;

    /* Control case: mailbox = 0 -> overflow_magnitude must stay zero
     * after the tick (no clip trigger because input-scale product = 0). */
    state->overflow_magnitude = 0;
    state->mix_bus_l = 0;
    state->mix_bus_r = 0;
    spu94_tick(state);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, state->overflow_magnitude,
        "overflow_magnitude must stay zero when mailbox is zero "
        "(proves hard_clip wasn't tripped by residual state)");

    /* Experimental case: mailbox = non-zero -> overflow_magnitude must
     * be non-zero after the tick. Proves the reverb body read the
     * mailbox (if it still hardcoded zero, the input-scale product
     * would be zero and hard_clip wouldn't fire). */
    state->overflow_magnitude = 0;
    state->mix_bus_l = (int16_t)0x0123;  /* 291, low bits set */
    state->mix_bus_r = (int16_t)0x3210;  /* 12816 */
    spu94_tick(state);
    TEST_ASSERT_NOT_EQUAL_INT32_MESSAGE(0, state->overflow_magnitude,
        "overflow_magnitude must be non-zero after non-zero mailbox + tick "
        "(D-05 proof: reverb body read state->mix_bus_l/r)");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mix_bus_init_zero);
    RUN_TEST(test_mix_bus_reset_clears);
    RUN_TEST(test_mix_bus_tick_observes_write);
    return UNITY_END();
}
