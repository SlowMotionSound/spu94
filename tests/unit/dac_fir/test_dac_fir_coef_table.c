/* tests/unit/dac_fir/test_dac_fir_coef_table.c -- Phase 6 Plan 01 Task 3
 *
 * Coefficient table integrity invariants for the three-stage AK4309
 * interpolation filter. Catches accidental edits, transcription drift,
 * and copy-paste errors.
 *
 * Six invariant families (per stage):
 *   1. Length: array size matches NTAPS define.
 *   2. Center tap: correct value at ntaps/2.
 *   3. Symmetry: coef[k] == coef[ntaps-1-k] for all k.
 *   4. Half-band zeros: all odd-indexed entries except center are 0x0000.
 *   5. Sum of absolute values (L1 norm): feeds accumulator width proof.
 *   6. Signed sum (DC gain pre-shift): matches expected per-stage values.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_dac_fir_internal.h"
#include <stdint.h>

void setUp(void) {}
void tearDown(void) {}

/* ---- Stage 1 (55-tap) ---- */

static void test_stage1_length(void) {
    TEST_ASSERT_EQUAL_size_t(DAC_FIR_STAGE1_NTAPS,
        sizeof(dac_interp_stage1) / sizeof(dac_interp_stage1[0]));
}

static void test_stage1_center_tap(void) {
    TEST_ASSERT_EQUAL_INT16(0x4000, dac_interp_stage1[27]);
}

static void test_stage1_symmetry(void) {
    for (int k = 0; k < DAC_FIR_STAGE1_NTAPS; ++k) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(
            dac_interp_stage1[DAC_FIR_STAGE1_NTAPS - 1 - k],
            dac_interp_stage1[k],
            "Stage 1 symmetry: coef[k] == coef[54-k]");
    }
}

static void test_stage1_halfband_zeros(void) {
    int zero_count = 0;
    for (int k = 0; k < DAC_FIR_STAGE1_NTAPS; ++k) {
        if (k == 27) continue;  /* center tap -- not required to be zero */
        if ((k % 2) == 1) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(0, dac_interp_stage1[k],
                "Stage 1 half-band: odd k (off-center) must be zero");
            zero_count++;
        }
    }
    /* 55 taps, 27 odd indices, minus center (index 27 is odd) = 26 zeros */
    TEST_ASSERT_EQUAL_INT(26, zero_count);
}

static void test_stage1_abs_sum(void) {
    int32_t l1 = 0;
    for (int k = 0; k < DAC_FIR_STAGE1_NTAPS; ++k) {
        int16_t c = dac_interp_stage1[k];
        l1 += (int32_t)((c < 0) ? -c : c);
    }
    TEST_ASSERT_EQUAL_INT32(58126, l1);
}

static void test_stage1_signed_sum(void) {
    int32_t sum = 0;
    for (int k = 0; k < DAC_FIR_STAGE1_NTAPS; ++k) {
        sum += (int32_t)dac_interp_stage1[k];
    }
    /* DC gain: sum = 0x7F8E = 32654 */
    TEST_ASSERT_EQUAL_INT32(32654, sum);
}

/* ---- Stage 2 (11-tap) ---- */

static void test_stage2_length(void) {
    TEST_ASSERT_EQUAL_size_t(DAC_FIR_STAGE2_NTAPS,
        sizeof(dac_interp_stage2) / sizeof(dac_interp_stage2[0]));
}

static void test_stage2_center_tap(void) {
    /* Stage 2 center tap is 0x3FFE (16382), not 0x4000 */
    TEST_ASSERT_EQUAL_INT16(0x3FFE, dac_interp_stage2[5]);
}

static void test_stage2_symmetry(void) {
    for (int k = 0; k < DAC_FIR_STAGE2_NTAPS; ++k) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(
            dac_interp_stage2[DAC_FIR_STAGE2_NTAPS - 1 - k],
            dac_interp_stage2[k],
            "Stage 2 symmetry: coef[k] == coef[10-k]");
    }
}

static void test_stage2_halfband_zeros(void) {
    int zero_count = 0;
    for (int k = 0; k < DAC_FIR_STAGE2_NTAPS; ++k) {
        if (k == 5) continue;  /* center */
        if ((k % 2) == 1) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(0, dac_interp_stage2[k],
                "Stage 2 half-band: odd k (off-center) must be zero");
            zero_count++;
        }
    }
    /* 11 taps, 5 odd indices, minus center (index 5 is odd) = 4 zeros */
    TEST_ASSERT_EQUAL_INT(4, zero_count);
}

static void test_stage2_abs_sum(void) {
    int32_t l1 = 0;
    for (int k = 0; k < DAC_FIR_STAGE2_NTAPS; ++k) {
        int16_t c = dac_interp_stage2[k];
        l1 += (int32_t)((c < 0) ? -c : c);
    }
    TEST_ASSERT_EQUAL_INT32(40786, l1);
}

static void test_stage2_signed_sum(void) {
    int32_t sum = 0;
    for (int k = 0; k < DAC_FIR_STAGE2_NTAPS; ++k) {
        sum += (int32_t)dac_interp_stage2[k];
    }
    /* DC gain: int32 sum wraps to 32794 when treating as unsigned 16-bit,
     * but as int32 this is simply -32742.
     * Actually: 0x0171*2 + (-0x07CE)*2 + 0x266B*2 + 0x3FFE
     *   = 369*2 + (-1998)*2 + 9835*2 + 16382
     *   = 738 + (-3996) + 19670 + 16382 = 32794
     * As int32 sum, this is just 32794. */
    TEST_ASSERT_EQUAL_INT32(32794, sum);
}

/* ---- Stage 3 (7-tap) ---- */

static void test_stage3_length(void) {
    TEST_ASSERT_EQUAL_size_t(DAC_FIR_STAGE3_NTAPS,
        sizeof(dac_interp_stage3) / sizeof(dac_interp_stage3[0]));
}

static void test_stage3_center_tap(void) {
    TEST_ASSERT_EQUAL_INT16(0x4000, dac_interp_stage3[3]);
}

static void test_stage3_symmetry(void) {
    for (int k = 0; k < DAC_FIR_STAGE3_NTAPS; ++k) {
        TEST_ASSERT_EQUAL_INT16_MESSAGE(
            dac_interp_stage3[DAC_FIR_STAGE3_NTAPS - 1 - k],
            dac_interp_stage3[k],
            "Stage 3 symmetry: coef[k] == coef[6-k]");
    }
}

static void test_stage3_halfband_zeros(void) {
    int zero_count = 0;
    for (int k = 0; k < DAC_FIR_STAGE3_NTAPS; ++k) {
        if (k == 3) continue;  /* center */
        if ((k % 2) == 1) {
            TEST_ASSERT_EQUAL_INT16_MESSAGE(0, dac_interp_stage3[k],
                "Stage 3 half-band: odd k (off-center) must be zero");
            zero_count++;
        }
    }
    /* 7 taps, 3 odd indices, minus center (index 3 is odd) = 2 zeros */
    TEST_ASSERT_EQUAL_INT(2, zero_count);
}

static void test_stage3_abs_sum(void) {
    int32_t l1 = 0;
    for (int k = 0; k < DAC_FIR_STAGE3_NTAPS; ++k) {
        int16_t c = dac_interp_stage3[k];
        l1 += (int32_t)((c < 0) ? -c : c);
    }
    TEST_ASSERT_EQUAL_INT32(37264, l1);
}

static void test_stage3_signed_sum(void) {
    int32_t sum = 0;
    for (int k = 0; k < DAC_FIR_STAGE3_NTAPS; ++k) {
        sum += (int32_t)dac_interp_stage3[k];
    }
    /* DC gain: sum = 0x7FF4 = 32756 */
    TEST_ASSERT_EQUAL_INT32(32756, sum);
}

int main(void) {
    UNITY_BEGIN();
    /* Stage 1 */
    RUN_TEST(test_stage1_length);
    RUN_TEST(test_stage1_center_tap);
    RUN_TEST(test_stage1_symmetry);
    RUN_TEST(test_stage1_halfband_zeros);
    RUN_TEST(test_stage1_abs_sum);
    RUN_TEST(test_stage1_signed_sum);
    /* Stage 2 */
    RUN_TEST(test_stage2_length);
    RUN_TEST(test_stage2_center_tap);
    RUN_TEST(test_stage2_symmetry);
    RUN_TEST(test_stage2_halfband_zeros);
    RUN_TEST(test_stage2_abs_sum);
    RUN_TEST(test_stage2_signed_sum);
    /* Stage 3 */
    RUN_TEST(test_stage3_length);
    RUN_TEST(test_stage3_center_tap);
    RUN_TEST(test_stage3_symmetry);
    RUN_TEST(test_stage3_halfband_zeros);
    RUN_TEST(test_stage3_abs_sum);
    RUN_TEST(test_stage3_signed_sum);
    return UNITY_END();
}
