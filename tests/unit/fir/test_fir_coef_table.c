/* tests/unit/fir/test_fir_coef_table.c -- Phase 4 Plan 01 Task 3
 *
 * Coefficient-table integrity invariants. These catch accidental edits,
 * transcription drift between src/spu94/spu94_fir_coef.c and the
 * verbatim Python transcription in tests/python/derive_fir_reference.py,
 * and copy-paste errors from alternate published sources.
 *
 * Five invariants (04-RESEARCH Coefficient Table -- Symmetry + half-band
 * verification):
 *   1. Length == 39.
 *   2. Center tap coef[19] == 0x4000.
 *   3. Symmetric about index 19: coef[k] == coef[38-k] for k in 0..19.
 *   4. Half-band Type I zero pattern: coef[k] == 0 for all odd k except
 *      k=19 (center). Equivalently, for all k where |k-19| is a non-zero
 *      even integer.
 *   5. Sum = 0x7FFE (DC gain pre-shift). Sum of |coef| = 0xB9A6 = 47526
 *      (accumulator L1 worst-case divisor, feeds the D-02 width proof).
 *
 * Pitfall 4 (04-RESEARCH): a single-bit transcription error in one of the
 * non-zero magnitudes (e.g., 0x2806 -> 0x2807) would pass invariants 1-4
 * but fail invariant 5's sum checks. SHA-256 pin is deferred to Plan 04
 * Python audit (ctest cannot easily consume a C SHA-256 dep).
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

static void test_coef_length(void) {
    TEST_ASSERT_EQUAL_size_t(39u,
        sizeof(spu94_fir_coef) / sizeof(spu94_fir_coef[0]));
}

static void test_coef_center_tap(void) {
    TEST_ASSERT_EQUAL_INT16(0x4000, spu94_fir_coef[19]);
}

static void test_coef_symmetry(void) {
    for (int k = 0; k <= 19; ++k) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(spu94_fir_coef[38 - k],
                                        spu94_fir_coef[k],
                                        "symmetry coef[k] == coef[38-k]");
    }
}

static void test_coef_halfband_zeros(void) {
    /* Half-band Type I: for every k where (k - 19) is a NON-ZERO even
     * integer, coef[k] must be zero. That is the 18 odd-indexed
     * positions excluding the center:
     *   k in {1, 3, 5, 7, 9, 11, 13, 15, 17, 21, 23, 25, 27, 29, 31,
     *         33, 35, 37}
     * (Equivalently: all k in 0..38 with k != 19 AND k is odd.) */
    int zero_positions_checked = 0;
    for (int k = 0; k < 39; ++k) {
        if (k == 19) continue;
        if ((k % 2) == 1) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(0, spu94_fir_coef[k],
                "half-band Type I: odd k (off-center) must be zero");
            zero_positions_checked++;
        }
    }
    TEST_ASSERT_EQUAL_INT(18, zero_positions_checked);
}

static void test_coef_sum_and_l1(void) {
    int32_t sum = 0;
    int32_t l1  = 0;
    for (int k = 0; k < 39; ++k) {
        sum += (int32_t)spu94_fir_coef[k];
        l1  += (int32_t)((spu94_fir_coef[k] < 0)
                         ? -spu94_fir_coef[k] :  spu94_fir_coef[k]);
    }
    /* Sum (DC gain, pre-shift): 0x7FFE = 32766 (very close to unity). */
    TEST_ASSERT_EQUAL_INT32(0x7FFE, sum);
    /* Sum of |h[k]|: 0xB9A6 = 47526. Feeds accumulator width proof:
     *   worst-case |acc| = sum(|h|) * |INT16_MIN| = 0xB9A6 * 0x8000
     *                    = 1557331968 = 0x5CD30000. */
    TEST_ASSERT_EQUAL_INT32(0xB9A6, l1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_coef_length);
    RUN_TEST(test_coef_center_tap);
    RUN_TEST(test_coef_symmetry);
    RUN_TEST(test_coef_halfband_zeros);
    RUN_TEST(test_coef_sum_and_l1);
    return UNITY_END();
}
