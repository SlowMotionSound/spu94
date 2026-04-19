/* tests/unit/q15/test_q15.c
 * Phase 1 Q15 unit tests. Hand-computed reference table per D-10.
 * Each entry is audited against ADR-0001: q15_mul_truncate = sat_s16((int32_t)a * b >> 15).
 * Inline table — no fixture loader (D-10).
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_q15.h>
#include <stdalign.h>
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

/* --- q15_mul_truncate_with_err: error tap (Plan 02 Task 2 / D-18) --- */

/* Reproduces q15_mul_truncate's behavior bit-exactly when err_out == NULL,
 * AND additionally writes the truncation remainder when err_out != NULL.
 * Remainder formula: remainder = product - (shifted << 15). */
void test_q15_mul_truncate_with_err_null_matches_base(void) {
    /* For every Phase 1 reference case, _with_err(NULL) must equal _truncate. */
    size_t n = sizeof(mul_cases)/sizeof(mul_cases[0]);
    for (size_t i = 0; i < n; ++i) {
        int16_t base = q15_mul_truncate(mul_cases[i].a, mul_cases[i].b);
        int16_t got  = q15_mul_truncate_with_err(mul_cases[i].a, mul_cases[i].b, NULL);
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "case %zu: %s — _with_err(NULL) must equal _truncate",
                 i, mul_cases[i].why);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(base, got, msg);
    }
}

void test_q15_mul_truncate_with_err_remainder_table(void) {
    /* Hand-computed remainders. remainder = product - (shifted << 15). */
    int16_t err = 0x5A5A; /* sentinel: confirms the function writes */

    /* (100, 100): product=10000, shifted=0, remainder=10000 - 0 = 10000. */
    int16_t r = q15_mul_truncate_with_err(100, 100, &err);
    TEST_ASSERT_EQUAL_INT16(0,     r);
    TEST_ASSERT_EQUAL_INT16(10000, err);

    /* (32, 1024): product=32768, shifted=1, remainder=32768 - 32768 = 0. */
    err = 0x5A5A;
    r = q15_mul_truncate_with_err(32, 1024, &err);
    TEST_ASSERT_EQUAL_INT16(1, r);
    TEST_ASSERT_EQUAL_INT16(0, err);

    /* (1, 1): product=1, shifted=0, remainder=1. */
    err = 0x5A5A;
    r = q15_mul_truncate_with_err(1, 1, &err);
    TEST_ASSERT_EQUAL_INT16(0, r);
    TEST_ASSERT_EQUAL_INT16(1, err);

    /* (0, 0): product=0, shifted=0, remainder=0. */
    err = 0x5A5A;
    r = q15_mul_truncate_with_err(0, 0, &err);
    TEST_ASSERT_EQUAL_INT16(0, r);
    TEST_ASSERT_EQUAL_INT16(0, err);

    /* (-1, 1): product=-1, ASR>>15 = -1, remainder = -1 - (-1 << 15)
       = -1 - (-32768) = 32767. */
    err = 0x5A5A;
    r = q15_mul_truncate_with_err(-1, 1, &err);
    TEST_ASSERT_EQUAL_INT16(-1, r);
    TEST_ASSERT_EQUAL_INT16(32767, err);

    /* INT16_MAX^2: product=1073676289, shifted=32766, remainder
       = 1073676289 - (32766 << 15) = 1073676289 - 1073709056? Let's compute:
       32766 << 15 = 32766 * 32768 = 1073709056? No — actually 32766 * 32768
       = 1073676288. So remainder = 1073676289 - 1073676288 = 1. */
    err = 0x5A5A;
    r = q15_mul_truncate_with_err(INT16_MAX, INT16_MAX, &err);
    TEST_ASSERT_EQUAL_INT16(32766, r);
    TEST_ASSERT_EQUAL_INT16(1, err);
}

void test_q15_mul_truncate_with_err_saturation_remainder_is_pre_saturation(void) {
    /* INT16_MIN * INT16_MIN: product = +2^30 = 1073741824. ASR >>15 = +32768.
       sat_s16(+32768) = INT16_MAX. The header documents that the remainder is
       PRE-saturation: remainder = product - (shifted << 15)
                                  = 1073741824 - (32768 << 15)
                                  = 1073741824 - 1073741824
                                  = 0. */
    int16_t err = 0x5A5A;
    int16_t r = q15_mul_truncate_with_err(INT16_MIN, INT16_MIN, &err);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, r);
    TEST_ASSERT_EQUAL_INT16(0, err);
}

/* --- spu94_tick public stub (D-19) --- */

void test_spu94_tick_null_safe(void) {
    /* Should not crash. */
    spu94_tick(NULL);
}

void test_spu94_tick_noop_on_valid_state(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char buf[SPU94_STATE_SIZE_MAX];
    spu94_state *s = spu94_init(buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    /* Plan 02 stub — must not crash; observable side effects are Phase 3+. */
    spu94_tick(s);
    spu94_tick(s);
    spu94_destroy(s);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_q15_mul_truncate_table);
    RUN_TEST(test_sat_s16_boundaries);
    RUN_TEST(test_q15_add_sat_table);
    RUN_TEST(test_q15_mul_truncate_with_err_null_matches_base);
    RUN_TEST(test_q15_mul_truncate_with_err_remainder_table);
    RUN_TEST(test_q15_mul_truncate_with_err_saturation_remainder_is_pre_saturation);
    RUN_TEST(test_spu94_tick_null_safe);
    RUN_TEST(test_spu94_tick_noop_on_valid_state);
    return UNITY_END();
}
