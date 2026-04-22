/* tests/unit/reverb/test_reverb_input_scale.c — Phase 3 Plan 01 Task 3
 *
 * CR-01 fix: stage is now a Q15 multiply per psx-spx ("multiplication
 * results are divided by +8000h to fit them to 16-bit range"). Drives
 * spu94_reverb_input_scale with crafted int16 (left, right, vLIN, vRIN)
 * tuples and asserts the Q15-scaled products plus per-multiply
 * truncation err accumulation into state->err_input_scale.
 *
 * Pre-fix: the stage performed a raw int16 * int16 widening multiply
 * with NO >>15 shift, and this test codified that behavior (which
 * Hall's vLIN=0x8000 preset then trivially saturated in hard_clip,
 * pinning output regardless of input). The RED linearity test in
 * tests/unit/process/test_process_reverb_linearity.c defends the fix
 * at the system level; this file defends it at the unit level.
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

/* Q15 reference: result = sat_s16( (int32 a * int32 b) >> 15 ).
 * INT16_MIN * INT16_MIN = +0x40000000, >>15 = +0x8000, sat_s16 -> INT16_MAX. */
static const input_scale_case_t g_cases[] = {
    { 0, 0, 0x1234, -0x5678, 0, 0, "zero inputs -> zero product" },
    { 7, (int16_t)0x4000, 0, 0, 0, 0, "zero coefficients -> zero" },
    /* INT16_MAX * 1 / 0x8000 = 0 (both operands small relative to Q15). */
    { INT16_MAX, INT16_MAX, 1, 1, 0, 0,
      "Q15 max * 1-LSB = near-zero (shifted out)" },
    { INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX,
      /* 0x7FFF^2 = 0x3FFF0001; >>15 = 0x7FFE. */
      (int32_t)0x7FFE, (int32_t)0x7FFE,
      "Q15 max * max = 0x7FFE" },
    { INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN,
      /* 0x40000000; >>15 = 0x8000; sat -> 0x7FFF. */
      (int32_t)INT16_MAX, (int32_t)INT16_MAX,
      "Q15 min * min = sat to INT16_MAX (ADR-0001)" },
    { INT16_MIN, 0, INT16_MAX, 0,
      /* 0x8000 * 0x7FFF = -0x3FFF8000; >>15 = -0x7FFF. */
      (int32_t)-INT16_MAX, 0, "Q15 min*max L only" },
    /* Hall preset case: vLIN=0x8000 (Q15 -1.0) passes input through at
     * unity with inversion. This is the specific case that CR-01 broke. */
    { 10000, -5000, (int16_t)0x8000, (int16_t)0x8000,
      /* 10000 * -32768 / 32768 = -10000; sat = -10000. */
      -10000, 5000,
      "Q15 vLIN=0x8000 (=-1.0) passes input through with inversion — CR-01 gate" },
    { (int16_t)0x0100, (int16_t)0x0200, (int16_t)0x0300, (int16_t)0x0400,
      /* 0x0100 * 0x0300 = 0x30000; >>15 = 6. 0x0200 * 0x0400 = 0x80000; >>15 = 16. */
      (int32_t)6, (int32_t)16,
      "Q15 generic small positive" },
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

static void test_input_scale_err_zero_on_clean_multiply(void) {
    /* Post-CR-01-fix: the stage is a Q15 multiply, so err_input_scale
     * accumulates the truncation remainder. It must be zero when both
     * sides produce a clean (exactly-divisible-by-2^15) product. All
     * zeros trivially qualifies. */
    setup_state();
    TEST_ASSERT_EQUAL_INT32(0, g_state->err_input_scale);
    int32_t Lin = 0, Rin = 0;
    spu94_reverb_input_scale(g_state, 0, 0, 0x0123, 0x4567, &Lin, &Rin);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(0, g_state->err_input_scale,
        "All-zero operands must produce zero truncation remainder");
}

static void test_input_scale_err_accumulates_on_truncation(void) {
    /* Post-CR-01-fix: the stage is a Q15 multiply, so a multiply that
     * is NOT exactly divisible by 2^15 accumulates a non-zero remainder.
     * Hall's vLIN=0x8000 case (generic small inputs) is the realistic
     * exercise: 10000 * -32768 = -327680000; >>15 = -10000; remainder =
     * -327680000 - (-10000 << 15) = -327680000 - (-327680000) = 0.
     * Use an input that IS non-divisible to get a non-zero err. */
    setup_state();
    int32_t Lin = 0, Rin = 0;
    /* 0x1234 * 0x5678 = 0x6260060; >>15 = 0xC4C; remainder = 0x60. */
    spu94_reverb_input_scale(g_state, 0x1234, 0x1234,
                             0x5678, 0x5678, &Lin, &Rin);
    TEST_ASSERT_NOT_EQUAL_INT32(0, g_state->err_input_scale);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_input_scale_table);
    RUN_TEST(test_input_scale_err_zero_on_clean_multiply);
    RUN_TEST(test_input_scale_err_accumulates_on_truncation);
    return UNITY_END();
}
