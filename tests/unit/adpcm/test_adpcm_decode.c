/* tests/unit/adpcm/test_adpcm_decode.c
 * M2 Phase 1 Plan 01 Task 2: Known-vector unit tests for ADPCM decoder.
 *
 * Covers all 5 filter coefficient pairs, shift extremes (0, 6, 12, 13, 14, 15),
 * positive/negative saturation clamping, nibble ordering (low first),
 * sign extension, state carry across blocks, filter index clamping,
 * and flag byte return value.
 *
 * All expected values are hand-computed from the decode formula:
 *   shifted   = sign_extended_nibble << (12 - shift)
 *   predicted = (old * f0 + older * f1 + 32) >> 6   (ASR)
 *   sample    = sat_s16(shifted + predicted)
 */

/* COVERAGE MAP — ADPCM-TEST-01 known-vector checklist
 *   all-zero block          : test_decode_all_zero_block
 *   single-impulse          : test_decode_single_impulse
 *   filter 0 (known state)  : test_decode_single_impulse (f0=0, trivial)
 *   filter 1 (known state)  : test_decode_filter1_prediction
 *   filter 2 (known state)  : test_decode_filter2_prediction
 *   filter 3 (known state)  : test_decode_filter3
 *   filter 4 (known state)  : test_decode_filter4
 *   shift 0                 : test_decode_shift0
 *   shift 6                 : test_decode_shift6
 *   shift 12                : test_decode_shift12
 *   shift 13 (maps to 9)    : test_decode_shift13_maps_to_9
 *   shift 14 (maps to 9)    : test_decode_shift14_maps_to_9
 *   shift 15 (maps to 9)    : test_decode_shift15_maps_to_9
 *   clamp positive overflow : test_decode_clamp_positive
 *   clamp negative overflow : test_decode_clamp_negative
 *   state carry (2 blocks)  : test_decode_state_carry
 */

#include "unity.h"
#include <spu94/spu94_adpcm.h>
#include <stdint.h>
#include <string.h>

/* Unity lifecycle hooks. */
void setUp(void) {}
void tearDown(void) {}

/* Helper: build a 16-byte ADPCM block from parameters.
 * nibbles[28] holds signed nibble values (-8..+7); only the low 4 bits
 * of each are packed into the block (low nibble first per byte). */
static void build_block(uint8_t block[16], int shift, int filter,
                        uint8_t flags, const int8_t nibbles[28])
{
    memset(block, 0, 16);
    block[0] = (uint8_t)((filter << 4) | (shift & 0x0F));
    block[1] = flags;
    for (int i = 0; i < 14; i++) {
        uint8_t lo = (uint8_t)(nibbles[i * 2]     & 0x0F);
        uint8_t hi = (uint8_t)(nibbles[i * 2 + 1] & 0x0F);
        block[2 + i] = lo | (uint8_t)(hi << 4);
    }
}

/* ------------------------------------------------------------------ */
/* All-zero block                                                      */
/* ------------------------------------------------------------------ */
void test_decode_all_zero_block(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    for (int i = 0; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Single impulse: nibble[0]=1, shift=0, filter=0                      */
/* shifted = 1 << 12 = 4096, predicted = 0, out[0] = 4096             */
/* ------------------------------------------------------------------ */
void test_decode_single_impulse(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 1;

    build_block(block, 0, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(4096, out[0]);
    /* Filter 0 has f0=0, so no prediction propagation. Rest should be 0. */
    for (int i = 1; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Negative nibble: nibble[0]=0xF (-1), shift=0, filter=0              */
/* shifted = -1 << 12 = -4096                                         */
/* ------------------------------------------------------------------ */
void test_decode_negative_nibble(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = -1;  /* 0x0F in 4-bit unsigned = sign-extends to -1 */

    build_block(block, 0, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(-4096, out[0]);
}

/* ------------------------------------------------------------------ */
/* Filter 1: old=1000, older=0, nibble=0                               */
/* predicted = (1000*60 + 0*0 + 32) >> 6 = 60032 >> 6 = 938           */
/* ------------------------------------------------------------------ */
void test_decode_filter1_prediction(void)
{
    spu94_adpcm_state st = {1000, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 1, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(938, out[0]);
}

/* ------------------------------------------------------------------ */
/* Filter 2: old=1000, older=500, nibble=0                             */
/* predicted = (1000*115 + 500*(-52) + 32) >> 6                        */
/*           = (115000 - 26000 + 32) >> 6 = 89032 >> 6 = 1391         */
/* ------------------------------------------------------------------ */
void test_decode_filter2_prediction(void)
{
    spu94_adpcm_state st = {1000, 500};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 2, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(1391, out[0]);
}

/* ------------------------------------------------------------------ */
/* Filter 3: old=1000, older=500, nibble=0                             */
/* predicted = (1000*98 + 500*(-55) + 32) >> 6                         */
/*           = (98000 - 27500 + 32) >> 6 = 70532 >> 6 = 1102          */
/* ------------------------------------------------------------------ */
void test_decode_filter3(void)
{
    spu94_adpcm_state st = {1000, 500};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 3, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(1102, out[0]);
}

/* ------------------------------------------------------------------ */
/* Filter 4: old=1000, older=500, nibble=0                             */
/* predicted = (1000*122 + 500*(-60) + 32) >> 6                        */
/*           = (122000 - 30000 + 32) >> 6 = 92032 >> 6 = 1438         */
/* ------------------------------------------------------------------ */
void test_decode_filter4(void)
{
    spu94_adpcm_state st = {1000, 500};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 4, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(1438, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 6: nibble=7, shifted = 7 << (12-6) = 7 << 6 = 448            */
/* ------------------------------------------------------------------ */
void test_decode_shift6(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 7;

    build_block(block, 6, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(448, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 12: nibble=7, shifted = 7 << (12-12) = 7                     */
/* ------------------------------------------------------------------ */
void test_decode_shift12(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 7;

    build_block(block, 12, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(7, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 0: nibble=7, shifted = 7 << 12 = 28672                       */
/* ------------------------------------------------------------------ */
void test_decode_shift0(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 7;

    build_block(block, 0, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(28672, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 13 maps to 9: nibble=1, shifted = 1 << (12-9) = 8            */
/* ------------------------------------------------------------------ */
void test_decode_shift13_maps_to_9(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 1;

    build_block(block, 13, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(8, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 14 maps to 9: same result as shift 13                        */
/* ------------------------------------------------------------------ */
void test_decode_shift14_maps_to_9(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 1;

    build_block(block, 14, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(8, out[0]);
}

/* ------------------------------------------------------------------ */
/* Shift 15 maps to 9: same result as shift 13                        */
/* ------------------------------------------------------------------ */
void test_decode_shift15_maps_to_9(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 1;

    build_block(block, 15, 0, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(8, out[0]);
}

/* ------------------------------------------------------------------ */
/* Clamp positive: shift=0, filter=1, old=32000, nibble=7              */
/* shifted = 7 << 12 = 28672                                          */
/* predicted = (32000*60 + 0*0 + 32) >> 6 = 1920032 >> 6 = 30000      */
/* sample = 28672 + 30000 = 58672 -> clamped to 32767 (INT16_MAX)     */
/* ------------------------------------------------------------------ */
void test_decode_clamp_positive(void)
{
    spu94_adpcm_state st = {32000, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = 7;

    build_block(block, 0, 1, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(INT16_MAX, out[0]);
}

/* ------------------------------------------------------------------ */
/* Clamp negative: shift=0, filter=1, old=-32000, nibble=-8            */
/* shifted = -8 << 12 = -32768                                        */
/* predicted = (-32000*60 + 0*0 + 32) >> 6 = -1919968 >> 6            */
/* ASR: -1919968 / 64 = -29999.5, ASR rounds toward -inf = -30000     */
/* sample = -32768 + (-30000) = -62768 -> clamped to -32768 (INT16_MIN)*/
/* ------------------------------------------------------------------ */
void test_decode_clamp_negative(void)
{
    spu94_adpcm_state st = {-32000, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));
    nibbles[0] = -8;  /* 0x08 in 4-bit, sign-extends to -8 */

    build_block(block, 0, 1, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(INT16_MIN, out[0]);
}

/* ------------------------------------------------------------------ */
/* Nibble ordering: byte = 0x21 -> lo=1 (first), hi=2 (second)        */
/* shift=0, filter=0: out[0] = 1<<12 = 4096, out[1] = 2<<12 = 8192   */
/* ------------------------------------------------------------------ */
void test_decode_nibble_order(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];

    /* Build manually to control exact byte content. */
    memset(block, 0, 16);
    block[0] = 0x00;  /* shift=0, filter=0 */
    block[1] = 0x00;  /* flags */
    block[2] = 0x21;  /* lo=1, hi=2 */
    /* rest = 0 */

    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(4096, out[0]);
    TEST_ASSERT_EQUAL_INT16(8192, out[1]);
}

/* ------------------------------------------------------------------ */
/* State carry across blocks                                           */
/* Decode block A, then block B. B's first sample uses A's final state.*/
/* ------------------------------------------------------------------ */
void test_decode_state_carry(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block_a[16];
    int16_t out_a[28];
    int8_t nibbles_a[28];
    memset(nibbles_a, 0, sizeof(nibbles_a));
    nibbles_a[0] = 3;
    nibbles_a[1] = 5;

    /* Block A: shift=12, filter=0, so samples = nibble values directly. */
    build_block(block_a, 12, 0, 0x00, nibbles_a);
    spu94_adpcm_decode_block(&st, block_a, out_a);

    /* After block A: old = out_a[27], older = out_a[26].
     * With filter 0 (f0=0, f1=0) and shift=12, all nibbles after index 1
     * are 0, so out_a[2..27] = 0. Thus old=0, older=0.
     * But out_a[0]=3, out_a[1]=5, then out_a[2..27]=0.
     * Final state: old = out_a[27] = 0, older = out_a[26] = 0.
     *
     * Better test: use filter=1 so prediction propagates state. */

    /* Reset and redo with a more meaningful carry test. */
    spu94_adpcm_state st2 = {0, 0};
    uint8_t block1[16];
    int16_t out1[28];
    int8_t nib1[28];
    memset(nib1, 0, sizeof(nib1));
    nib1[0] = 7;  /* shift=0, filter=0: out[0] = 7<<12 = 28672 */

    build_block(block1, 0, 0, 0x00, nib1);
    spu94_adpcm_decode_block(&st2, block1, out1);

    /* After block 1: filter=0 means f0=0, so no prediction propagation.
     * out[0]=28672, out[1..27]=0. State: old=0, older=0.
     * That's not a useful carry test. Use a non-zero final sample. */

    /* Direct approach: set up a block where the last two samples are known. */
    spu94_adpcm_state st3 = {0, 0};
    uint8_t blk1[16];
    int16_t o1[28];
    int8_t n1[28];
    memset(n1, 0, sizeof(n1));
    n1[26] = 2;  /* second-to-last sample */
    n1[27] = 3;  /* last sample */

    /* shift=12, filter=0: out[i] = nibble << 0 = nibble. No prediction. */
    build_block(blk1, 12, 0, 0x00, n1);
    spu94_adpcm_decode_block(&st3, blk1, o1);

    /* State after block 1: old = o1[27] = 3, older = o1[26] = 2. */
    TEST_ASSERT_EQUAL_INT16(3, st3.old);
    TEST_ASSERT_EQUAL_INT16(2, st3.older);

    /* Block 2: filter=1 (f0=60, f1=0), shift=0, nibble[0]=0.
     * predicted = (3*60 + 2*0 + 32) >> 6 = (180 + 32) >> 6 = 212 >> 6 = 3.
     * shifted = 0. sample = 0 + 3 = 3. */
    uint8_t blk2[16];
    int16_t o2[28];
    int8_t n2[28];
    memset(n2, 0, sizeof(n2));

    build_block(blk2, 0, 1, 0x00, n2);
    spu94_adpcm_decode_block(&st3, blk2, o2);

    TEST_ASSERT_EQUAL_INT16(3, o2[0]);
}

/* ------------------------------------------------------------------ */
/* Filter clamp: filter field = 5 -> uses filter 4 coefficients        */
/* old=1000, older=500, nibble=0                                       */
/* Same as filter 4: (122000 - 30000 + 32) >> 6 = 92032 >> 6 = 1438   */
/* ------------------------------------------------------------------ */
void test_decode_filter_clamp_5to4(void)
{
    spu94_adpcm_state st = {1000, 500};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 5, 0x00, nibbles);
    spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_INT16(1438, out[0]);
}

/* ------------------------------------------------------------------ */
/* Returns flags: block[1] = 0x07 -> return value is 0x07              */
/* ------------------------------------------------------------------ */
void test_decode_returns_flags(void)
{
    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    int16_t out[28];
    int8_t nibbles[28];
    memset(nibbles, 0, sizeof(nibbles));

    build_block(block, 0, 0, 0x07, nibbles);
    uint8_t flags = spu94_adpcm_decode_block(&st, block, out);

    TEST_ASSERT_EQUAL_UINT8(0x07, flags);
}

/* ------------------------------------------------------------------ */
/* Filter coefficient table verification                               */
/* ------------------------------------------------------------------ */
void test_filter_coefficients(void)
{
    TEST_ASSERT_EQUAL_INT16(  0, spu94_adpcm_f0[0]);
    TEST_ASSERT_EQUAL_INT16( 60, spu94_adpcm_f0[1]);
    TEST_ASSERT_EQUAL_INT16(115, spu94_adpcm_f0[2]);
    TEST_ASSERT_EQUAL_INT16( 98, spu94_adpcm_f0[3]);
    TEST_ASSERT_EQUAL_INT16(122, spu94_adpcm_f0[4]);

    TEST_ASSERT_EQUAL_INT16(  0, spu94_adpcm_f1[0]);
    TEST_ASSERT_EQUAL_INT16(  0, spu94_adpcm_f1[1]);
    TEST_ASSERT_EQUAL_INT16(-52, spu94_adpcm_f1[2]);
    TEST_ASSERT_EQUAL_INT16(-55, spu94_adpcm_f1[3]);
    TEST_ASSERT_EQUAL_INT16(-60, spu94_adpcm_f1[4]);
}

/* ------------------------------------------------------------------ */
/* Main: Unity test runner                                             */
/* ------------------------------------------------------------------ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_decode_all_zero_block);
    RUN_TEST(test_decode_single_impulse);
    RUN_TEST(test_decode_negative_nibble);
    RUN_TEST(test_decode_filter1_prediction);
    RUN_TEST(test_decode_filter2_prediction);
    RUN_TEST(test_decode_filter3);
    RUN_TEST(test_decode_filter4);
    RUN_TEST(test_decode_shift0);
    RUN_TEST(test_decode_shift6);
    RUN_TEST(test_decode_shift12);
    RUN_TEST(test_decode_shift13_maps_to_9);
    RUN_TEST(test_decode_shift14_maps_to_9);
    RUN_TEST(test_decode_shift15_maps_to_9);
    RUN_TEST(test_decode_clamp_positive);
    RUN_TEST(test_decode_clamp_negative);
    RUN_TEST(test_decode_nibble_order);
    RUN_TEST(test_decode_state_carry);
    RUN_TEST(test_decode_filter_clamp_5to4);
    RUN_TEST(test_decode_returns_flags);
    RUN_TEST(test_filter_coefficients);

    return UNITY_END();
}
