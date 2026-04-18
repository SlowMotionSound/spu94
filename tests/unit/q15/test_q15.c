/* tests/unit/q15/test_q15.c
 * Phase 1 Q15 unit tests. Hand-computed reference table per D-10.
 * Each entry is audited against ADR-0001: q15_mul_truncate = sat_s16((int32_t)a * b >> 15).
 * Inline table — no fixture loader (D-10).
 */
#include "unity.h"
#include <spu94/spu94_q15.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stddef.h>

/* Unity lifecycle hooks — required by the framework. */
void setUp(void) {}
void tearDown(void) {}

/* --- q15_mul_truncate: hand-computed reference table --- */
typedef struct { int16_t a; int16_t b; int16_t expected; const char *why; } q15_mul_case_t;

static const q15_mul_case_t mul_cases[] = {
    /* Identity + zero */
    {  0,        0,         0, "0 * 0" },
    {  0,        INT16_MAX, 0, "0 * MAX" },
    {  INT16_MAX, 0,        0, "MAX * 0" },
    {  0,        INT16_MIN, 0, "0 * MIN" },
    {  INT16_MIN, 0,        0, "MIN * 0" },

    /* Small positive products */
    {  1,        1,         0, "1 * 1 = 1 >> 15 = 0" },
    {  32,       1024,      1, "32 * 1024 = 32768 >> 15 = 1" },
    {  100,      100,       0, "100 * 100 = 10000 >> 15 = 0" },

    /* Full-scale positive */
    {  INT16_MAX, INT16_MAX, 32766, "32767*32767=1073676289, >>15 = 32766" },

    /* Full-scale mixed sign (distinguishes ASR from C-division) */
    {  INT16_MIN, INT16_MAX, -32767, "-32768*32767=-1073709056, ASR>>15 = -32767" },
    {  INT16_MAX, INT16_MIN, -32767, "commutes with above" },

    /* INT16_MIN^2 edge case — saturates to INT16_MAX */
    {  INT16_MIN, INT16_MIN, INT16_MAX, "(-32768)^2=+2^30, >>15=+32768, saturated to INT16_MAX" },

    /* ASR-vs-C-division distinguisher: -1 * 1 = -1 >> 15 */
    /* ASR of -1 (any shift amount, any positive shift) = -1. C division gives 0. */
    { -1,         1,        -1, "(-1) * 1 = -1; ASR>>15 = -1 (C-division would give 0)" },
    { -1,        -1,         0, "(-1) * (-1) = +1; >>15 = 0" },

    /* Small negative */
    { -32,        1024,     -1, "-32*1024 = -32768; ASR>>15 = -1" },
    { -100,       100,      -1, "-10000 ASR>>15 = -1 (not 0)" },

    /* Mid-range */
    {  16384,     16384,  8192, "0.5 * 0.5 in Q15 = 0.25 = 8192" },
    { -16384,     16384, -8192, "-0.5 * 0.5 in Q15 = -0.25 = -8192" },
};

void test_q15_mul_truncate_table(void) {
    size_t n = sizeof(mul_cases)/sizeof(mul_cases[0]);
    for (size_t i = 0; i < n; ++i) {
        int16_t got = q15_mul_truncate(mul_cases[i].a, mul_cases[i].b);
        char msg[160];
        snprintf(msg, sizeof(msg), "case %zu: %s (a=%d, b=%d, expected=%d, got=%d)",
                 i, mul_cases[i].why,
                 (int)mul_cases[i].a, (int)mul_cases[i].b,
                 (int)mul_cases[i].expected, (int)got);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(mul_cases[i].expected, got, msg);
    }
}

/* --- sat_s16 boundary table --- */
void test_sat_s16_boundaries(void) {
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, sat_s16(INT32_MAX));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, sat_s16(INT32_MIN));
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, sat_s16((int32_t)INT16_MAX + 1));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, sat_s16((int32_t)INT16_MIN - 1));
    TEST_ASSERT_EQUAL_INT16(0,         sat_s16(0));
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, sat_s16(INT16_MAX));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, sat_s16(INT16_MIN));
}

/* --- q15_add_sat table --- */
void test_q15_add_sat_table(void) {
    TEST_ASSERT_EQUAL_INT16(0,         q15_add_sat(0, 0));
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, q15_add_sat(INT16_MAX, 1));
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, q15_add_sat(INT16_MAX, INT16_MAX));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, q15_add_sat(INT16_MIN, -1));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, q15_add_sat(INT16_MIN, INT16_MIN));
    TEST_ASSERT_EQUAL_INT16(-1,        q15_add_sat(INT16_MAX, INT16_MIN));
    TEST_ASSERT_EQUAL_INT16(100,       q15_add_sat(50, 50));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_q15_mul_truncate_table);
    RUN_TEST(test_sat_s16_boundaries);
    RUN_TEST(test_q15_add_sat_table);
    return UNITY_END();
}
