/* tests/unit/process/test_process_mix_bus.c -- Phase 5 Plan 02 Task 2
 *
 * D-05 mailbox proof tests for struct spu94_state::mix_bus_l / mix_bus_r.
 * Three concerns:
 *   1. Post-init: mix_bus_l == mix_bus_r == 0 (spu94_init wholesale zero-fill)
 *   2. Post-reset after non-zero writes: mix_bus_l == mix_bus_r == 0
 *   3. Mailbox write followed by spu94_tick is observed by spu94_reverb_body
 *      -- proven via state->err_input_scale
 *
 * PROOF MECHANISM (test 3):
 * ------------------------
 * CR-01 fix (2026-04-22): spu94_reverb_input_scale is now a Q15 multiply
 * (q15_mul_truncate_with_err, product >> 15 with sat_s16), so
 * err_input_scale accumulates the per-multiply truncation remainder like
 * every other err_* accumulator. We use err_input_scale here as the
 * plan's original <behavior> sketch intended.
 *
 * Pre-CR-01-fix this test used overflow_magnitude instead because
 * input_scale was a raw int16 x int16 widening multiply with no >>15,
 * which meant err_input_scale was structurally zero but overflow_magnitude
 * fired on any mailbox write large enough to drive the raw product past
 * INT16_MAX. That code path was the CR-01 bug (output pinned to ~-5 dBFS
 * regardless of input amplitude); fixing it inverts the two fields'
 * utility as mailbox-read proofs.
 *
 * Mechanism:
 *   - mix_bus_l = 0x1234 (4660), vLIN = 0x4000 (16384 = Q15 0.5)
 *   - q15 multiply: 4660 * 16384 = 0x048D_0000; >>15 = 0x0918 = 2328;
 *     remainder = 0x048D_0000 - (0x0918 << 15) = 0 (exactly divisible).
 *     Pick a value that isn't divisible:
 *   - mix_bus_l = 0x1235 (4661), vLIN = 0x4567 (17767)
 *   - q15 multiply: 4661 * 17767 = 0x0534_0D2B; >>15 = 0x0A68;
 *     remainder = 0x0534_0D2B - (0x0A68 << 15) = 0x0D2B != 0
 *
 * This is a direct, robust proof that the reverb body read the mailbox:
 * zero mailbox -> zero err_input_scale; non-zero mailbox with a non-
 * divisible product -> non-zero err_input_scale. If the reverb body
 * hardcoded left_in/right_in = 0, err_input_scale would remain zero.
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
 * See file header for mechanism. Uses err_input_scale (the plan's
 * originally intended field, now viable after CR-01 fix made
 * input_scale a Q15 multiply). */
static void test_mix_bus_tick_observes_write(void) {
    /* Configure vLIN/vRIN with values that (combined with mailbox
     * writes below) produce a Q15 product with a non-zero truncation
     * remainder. 0x4567 = 17767; 17767 * 4661 = 0x0534_0D2B;
     * >>15 = 0x0A68; remainder = 0x0D2B != 0. */
    state->reg_values[SPU94_REG_vLIN] = (int16_t)0x4567;
    state->reg_values[SPU94_REG_vRIN] = (int16_t)0x4567;

    /* Control case: mailbox = 0 -> err_input_scale must stay zero
     * after the tick (zero operand -> zero product -> zero remainder). */
    state->err_input_scale = 0;
    state->mix_bus_l = 0;
    state->mix_bus_r = 0;
    spu94_tick(state);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, state->err_input_scale,
        "err_input_scale must stay zero when mailbox is zero "
        "(proves input_scale wasn't driven by residual state)");

    /* Experimental case: mailbox = non-zero -> err_input_scale must
     * be non-zero after the tick. Proves the reverb body read the
     * mailbox (if it still hardcoded zero, the input-scale product
     * would be zero and the truncation remainder would be zero). */
    state->err_input_scale = 0;
    state->mix_bus_l = (int16_t)0x1235;  /* 4661, non-divisible product */
    state->mix_bus_r = (int16_t)0x1235;
    spu94_tick(state);
    TEST_ASSERT_NOT_EQUAL_INT32_MESSAGE(0, state->err_input_scale,
        "err_input_scale must be non-zero after non-zero mailbox + tick "
        "(D-05 proof: reverb body read state->mix_bus_l/r)");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mix_bus_init_zero);
    RUN_TEST(test_mix_bus_reset_clears);
    RUN_TEST(test_mix_bus_tick_observes_write);
    return UNITY_END();
}
