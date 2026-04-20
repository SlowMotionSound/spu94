/* tests/unit/reverb/test_reverb_hard_clip.c — Phase 3 Plan 01 Task 3
 *
 * CORE-02 "independently testable" acceptance (ROADMAP Phase 3 SC-2).
 * Drives spu94_reverb_hard_clip with crafted int32 inputs and asserts
 * the saturation output + the D-11-extension overflow_magnitude
 * observable bit-for-bit. This is an internal-header test per D-01.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_reverb_internal.h"
#include <stdint.h>
#include <limits.h>

void setUp(void) {}
void tearDown(void) {}

typedef struct {
    int32_t Lin_wide;
    int32_t Rin_wide;
    int16_t exp_Lin;
    int16_t exp_Rin;
    int64_t exp_overflow;  /* int64 to safely express INT32 extremes */
    const char *name;
} hard_clip_case_t;

static const hard_clip_case_t g_cases[] = {
    { 0, 0, 0, 0, 0, "zero/zero" },
    /* INT16_MAX: |x|=INT16_MAX exactly -> overflow contribution = 0. */
    { INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX, 0, "boundary pos in-range" },
    /* INT16_MIN: sat_s16 does NOT clip (INT16_MIN is in range), but
     * |INT16_MIN|=0x8000 exceeds INT16_MAX=0x7FFF by 1, so the
     * overflow-magnitude formula fires: each side contributes 1,
     * sum = 2. This is intended — overflow_magnitude measures "high
     * bits lost to clamping *or* distance from the positive ceiling"
     * per D-11 extension, which applies symmetrically around zero. */
    { INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN, 2,
      "INT16_MIN^2 magnitude exceeds INT16_MAX by 1 per side" },
    { (int32_t)0x10000, 0, INT16_MAX, 0,
      (int64_t)0x10000 - (int64_t)INT16_MAX, "pos overflow L" },
    { -(int32_t)0x10000, 0, INT16_MIN, 0,
      (int64_t)0x10000 - (int64_t)INT16_MAX, "neg overflow L" },
    { (int32_t)0x10000, -(int32_t)0x10000, INT16_MAX, INT16_MIN,
      2 * ((int64_t)0x10000 - (int64_t)INT16_MAX), "both overflow" },
    /* INT32 extremes: |INT32_MAX| = 0x7FFFFFFF, |INT32_MIN| = 0x80000000. */
    { INT32_MAX, INT32_MIN, INT16_MAX, INT16_MIN,
      ((int64_t)INT32_MAX - (int64_t)INT16_MAX) +
      ((int64_t)INT32_MAX + 1 - (int64_t)INT16_MAX),
      "INT32 extremes" },
};
static const size_t g_n = sizeof(g_cases) / sizeof(g_cases[0]);

static void test_hard_clip_table(void) {
    for (size_t i = 0; i < g_n; ++i) {
        int16_t Lin = 0, Rin = 0;
        int32_t overflow = 0;
        spu94_reverb_hard_clip(g_cases[i].Lin_wide, g_cases[i].Rin_wide,
                               &Lin, &Rin, &overflow);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(g_cases[i].exp_Lin, Lin, g_cases[i].name);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(g_cases[i].exp_Rin, Rin, g_cases[i].name);
        /* The production code casts the int64 sum to int32; the sum is
         * bounded by 2*(INT32_MAX - INT16_MAX) < INT32_MAX so the cast
         * is lossless. Compare as int32. */
        TEST_ASSERT_EQUAL_INT32_MESSAGE((int32_t)g_cases[i].exp_overflow,
                                         overflow, g_cases[i].name);
    }
}

static void test_hard_clip_null_overflow_out(void) {
    /* overflow_out == NULL must not crash; saturation still applied. */
    int16_t Lin = 42, Rin = 42;
    spu94_reverb_hard_clip((int32_t)0x10000, -(int32_t)0x10000,
                           &Lin, &Rin, (int32_t *)0);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, Lin);
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, Rin);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hard_clip_table);
    RUN_TEST(test_hard_clip_null_overflow_out);
    return UNITY_END();
}
