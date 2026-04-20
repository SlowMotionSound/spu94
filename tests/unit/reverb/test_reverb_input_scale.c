/* tests/unit/reverb/test_reverb_input_scale.c — Phase 3 Plan 01 Task 3
 *
 * Drives spu94_reverb_input_scale with crafted int16 (left, right, vLIN, vRIN)
 * tuples and asserts the int32 widened products bit-for-bit. Confirms the
 * stage performs the nocash "Lin = vLIN*LeftInput" multiplication without
 * saturation or shift (hard_clip handles saturation in the next stage).
 *
 * Also asserts that state->err_input_scale stays at zero — the stage
 * performs no >>15 truncation so the err accumulator is symmetrically
 * present but structurally always zero (D-11 scope i, documented in the
 * production code).
 */
#include "unity.h"
#include "../../../src/spu94/spu94_reverb_internal.h"
#include "../../../src/spu94/spu94_state_internal.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include <limits.h>
#include <stddef.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_work_buf[1024];
static spu94_state *g_state = (spu94_state *)0;

static void setup_state(void) {
    g_state = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX,
                         g_work_buf, sizeof(g_work_buf));
    TEST_ASSERT_NOT_NULL(g_state);
}

typedef struct {
    int16_t left_in;
    int16_t right_in;
    int16_t vLIN;
    int16_t vRIN;
    int32_t exp_Lin;
    int32_t exp_Rin;
    const char *name;
} input_scale_case_t;

static const input_scale_case_t g_cases[] = {
    { 0, 0, 0x1234, -0x5678, 0, 0, "zero inputs -> zero product" },
    { 7, (int16_t)0x4000, 0, 0, 0, 0, "zero coefficients -> zero" },
    { INT16_MAX, INT16_MAX, 1, 1,
      (int32_t)INT16_MAX, (int32_t)INT16_MAX, "unit scale max in" },
    { INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX,
      (int32_t)INT16_MAX * (int32_t)INT16_MAX,  /* 0x3FFF0001 */
      (int32_t)INT16_MAX * (int32_t)INT16_MAX,
      "max * max = 0x3FFF0001 (no saturation)" },
    { INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN,
      (int32_t)INT16_MIN * (int32_t)INT16_MIN,  /* +0x40000000 */
      (int32_t)INT16_MIN * (int32_t)INT16_MIN,
      "min * min = +0x40000000 (sign flip preserved in int32)" },
    { INT16_MIN, 0, INT16_MAX, 0,
      (int32_t)INT16_MIN * (int32_t)INT16_MAX, 0, "min*max L only" },
    { (int16_t)0x0100, (int16_t)0x0200, (int16_t)0x0300, (int16_t)0x0400,
      (int32_t)0x0100 * (int32_t)0x0300,  /* 0x30000 */
      (int32_t)0x0200 * (int32_t)0x0400,  /* 0x80000 */
      "generic small positive" },
};
static const size_t g_n = sizeof(g_cases) / sizeof(g_cases[0]);

static void test_input_scale_table(void) {
    setup_state();
    for (size_t i = 0; i < g_n; ++i) {
        int32_t Lin = 0xDEADBEEF, Rin = 0xDEADBEEF;
        spu94_reverb_input_scale(g_state,
                                 g_cases[i].left_in,  g_cases[i].right_in,
                                 g_cases[i].vLIN,     g_cases[i].vRIN,
                                 &Lin, &Rin);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(g_cases[i].exp_Lin, Lin, g_cases[i].name);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(g_cases[i].exp_Rin, Rin, g_cases[i].name);
    }
}

static void test_input_scale_err_is_zero(void) {
    /* The stage has no >>15 truncation, so err_input_scale must remain
     * zero across every call in the table. Run all cases then assert. */
    setup_state();
    TEST_ASSERT_EQUAL_INT32(0, g_state->err_input_scale);
    for (size_t i = 0; i < g_n; ++i) {
        int32_t Lin = 0, Rin = 0;
        spu94_reverb_input_scale(g_state,
                                 g_cases[i].left_in,  g_cases[i].right_in,
                                 g_cases[i].vLIN,     g_cases[i].vRIN,
                                 &Lin, &Rin);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(0, g_state->err_input_scale,
                                         "err_input_scale must stay zero "
                                         "(stage has no >>15 truncation)");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_input_scale_table);
    RUN_TEST(test_input_scale_err_is_zero);
    return UNITY_END();
}
