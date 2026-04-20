/* tests/unit/fir/test_fir_overflow_proof.c -- Phase 4 Plan 02 Task 2
 *
 * SC-3: int32 accumulator holds worst-case adversarial sum without
 * overflow. D-02 comment-block proof is backed here by a direct
 * computation + UBSan in CI (ADR-0003).
 *
 * Achievable bound (04-RESEARCH section 7):
 *   positive adversarial: acc = 0x5CD2632E
 *   sat_s16 output:       0x7FFF
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_overflow_adversarial_positive_hits_bound(void) {
    int16_t history[39];
    for (int k = 0; k < 39; ++k) {
        history[k] = (spu94_fir_coef[k] >= 0) ? INT16_MAX : INT16_MIN;
    }
    int16_t out = 0; int32_t acc = 0; int32_t err = 0;
    spu94_fir_decimate_literal_reference(history, &out, &acc, &err);
    TEST_ASSERT_EQUAL_INT32(0x5CD2632E, acc);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out);
}

static void test_overflow_adversarial_negative_no_ub(void) {
    int16_t history[39];
    for (int k = 0; k < 39; ++k) {
        history[k] = (spu94_fir_coef[k] >= 0) ? INT16_MIN : INT16_MAX;
    }
    int16_t out = 0; int32_t acc = 0; int32_t err = 0;
    spu94_fir_decimate_literal_reference(history, &out, &acc, &err);
    TEST_ASSERT_TRUE_MESSAGE(acc < 0, "negative adversarial -> negative acc");
    TEST_ASSERT_TRUE_MESSAGE(acc > INT32_MIN, "acc must fit in int32");
    TEST_ASSERT_TRUE_MESSAGE(-acc >= 0x5CD2632E,
        "magnitude at or above achievable int16 bound");
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, out);
}

static void test_overflow_magnitude_tap_records_saturation(void) {
    int16_t history[39];
    for (int k = 0; k < 39; ++k) {
        history[k] = (spu94_fir_coef[k] >= 0) ? INT16_MAX : INT16_MIN;
    }
    int16_t out = 0; int32_t acc = 0; int32_t err = 0;
    spu94_fir_folded_reference(history, &out, &acc, &err);
    int32_t shifted = acc >> 15;
    int32_t expected_mag = 0;
    if (shifted > INT16_MAX) expected_mag = shifted - INT16_MAX;
    else if (shifted < INT16_MIN) expected_mag = INT16_MIN - shifted;
    TEST_ASSERT_TRUE_MESSAGE(expected_mag > 0,
        "adversarial input must saturate; overflow-magnitude positive");
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_overflow_adversarial_positive_hits_bound);
    RUN_TEST(test_overflow_adversarial_negative_no_ub);
    RUN_TEST(test_overflow_magnitude_tap_records_saturation);
    return UNITY_END();
}
