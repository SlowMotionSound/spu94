/* tests/unit/adpcm/test_adpcm_encode.c
 * M2 Phase 1 Plan 02 Task 2: Encoder unit tests.
 *
 * Verifies round-trip consistency, determinism, encoder invariants
 * (valid shift/filter ranges, nibble clamping, flag passthrough),
 * reconstructed-state correctness, shift=12 UB guard, signal-adaptive
 * shift selection, and multi-block state carry.
 */

#include "unity.h"
#include <spu94/spu94_adpcm.h>
#include <stdint.h>
#include <string.h>

/* Unity lifecycle hooks. */
void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Silence round-trip: encode 28 zeros -> decode produces 28 zeros     */
/* ------------------------------------------------------------------ */
void test_encode_silence(void)
{
    spu94_adpcm_state enc_st = {0, 0};
    int16_t in[28];
    uint8_t block[16];
    memset(in, 0, sizeof(in));

    spu94_adpcm_encode_block(&enc_st, in, 0x00, block);

    /* Decode the encoded block. */
    spu94_adpcm_state dec_st = {0, 0};
    int16_t out[28];
    spu94_adpcm_decode_block(&dec_st, block, out);

    for (int i = 0; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Deterministic round-trip: encode+decode twice -> bit-identical       */
/* ------------------------------------------------------------------ */
void test_encode_decode_roundtrip_deterministic(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 1000 - 14000);
    }

    /* First pass. */
    spu94_adpcm_state enc1 = {0, 0};
    uint8_t block1[16];
    spu94_adpcm_encode_block(&enc1, in, 0x00, block1);

    spu94_adpcm_state dec1 = {0, 0};
    int16_t out1[28];
    spu94_adpcm_decode_block(&dec1, block1, out1);

    /* Second pass (fresh states). */
    spu94_adpcm_state enc2 = {0, 0};
    uint8_t block2[16];
    spu94_adpcm_encode_block(&enc2, in, 0x00, block2);

    spu94_adpcm_state dec2 = {0, 0};
    int16_t out2[28];
    spu94_adpcm_decode_block(&dec2, block2, out2);

    TEST_ASSERT_EQUAL_MEMORY(out1, out2, sizeof(out1));
    TEST_ASSERT_EQUAL_MEMORY(block1, block2, sizeof(block1));
}

/* ------------------------------------------------------------------ */
/* Encode-decode state consistency: encoder state matches decoder state */
/* ------------------------------------------------------------------ */
void test_encode_decode_consistency(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 500);
    }

    spu94_adpcm_state enc_st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&enc_st, in, 0x00, block);

    spu94_adpcm_state dec_st = {0, 0};
    int16_t out[28];
    spu94_adpcm_decode_block(&dec_st, block, out);

    TEST_ASSERT_EQUAL_INT16(enc_st.old, dec_st.old);
    TEST_ASSERT_EQUAL_INT16(enc_st.older, dec_st.older);
}

/* ------------------------------------------------------------------ */
/* Valid shift: block[0] & 0x0F in range 0-12                          */
/* ------------------------------------------------------------------ */
void test_encode_produces_valid_shift(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 500);
    }

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    int shift = block[0] & 0x0F;
    TEST_ASSERT_LESS_OR_EQUAL(12, shift);
}

/* ------------------------------------------------------------------ */
/* Valid filter: (block[0] >> 4) & 0x07 in range 0-4                   */
/* ------------------------------------------------------------------ */
void test_encode_produces_valid_filter(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 500);
    }

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    int filter = (block[0] >> 4) & 0x07;
    TEST_ASSERT_LESS_OR_EQUAL(4, filter);
}

/* ------------------------------------------------------------------ */
/* Flags passthrough: encode with flags=0x07 -> block[1] == 0x07       */
/* ------------------------------------------------------------------ */
void test_encode_flags_passed_through(void)
{
    int16_t in[28];
    memset(in, 0, sizeof(in));

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x07, block);

    TEST_ASSERT_EQUAL_UINT8(0x07, block[1]);
}

/* ------------------------------------------------------------------ */
/* State uses reconstructed: encoder state matches decoding from scratch*/
/* ------------------------------------------------------------------ */
void test_encode_state_uses_reconstructed(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 1000);
    }

    spu94_adpcm_state enc_st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&enc_st, in, 0x00, block);

    /* Decode from scratch with zeroed state. */
    spu94_adpcm_state dec_st = {0, 0};
    int16_t out[28];
    spu94_adpcm_decode_block(&dec_st, block, out);

    TEST_ASSERT_EQUAL_INT16(enc_st.old, dec_st.old);
    TEST_ASSERT_EQUAL_INT16(enc_st.older, dec_st.older);
}

/* ------------------------------------------------------------------ */
/* Shift=12 UB guard: all-zero input, no crash, valid output           */
/* ------------------------------------------------------------------ */
void test_encode_shift12_no_ub(void)
{
    int16_t in[28];
    memset(in, 0, sizeof(in));

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    int shift = block[0] & 0x0F;
    TEST_ASSERT_LESS_OR_EQUAL(12, shift);

    /* Verify round-trip produces zeros. */
    spu94_adpcm_state dec_st = {0, 0};
    int16_t out[28];
    spu94_adpcm_decode_block(&dec_st, block, out);
    for (int i = 0; i < 28; i++) {
        TEST_ASSERT_EQUAL_INT16(0, out[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Loud signal selects low shift: large amplitude -> shift 0-6         */
/* ------------------------------------------------------------------ */
void test_encode_loud_signal_selects_low_shift(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(30000 - i * 2000);
    }

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    int shift = block[0] & 0x0F;
    TEST_ASSERT_LESS_OR_EQUAL(6, shift);
}

/* ------------------------------------------------------------------ */
/* Quiet signal selects high shift: tiny amplitude -> shift >= 8       */
/* ------------------------------------------------------------------ */
void test_encode_quiet_signal_selects_high_shift(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i - 14);
    }

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    int shift = block[0] & 0x0F;
    TEST_ASSERT_GREATER_OR_EQUAL(8, shift);
}

/* ------------------------------------------------------------------ */
/* Multi-block state carry: encode 2 blocks, decode 2 -> states match  */
/* ------------------------------------------------------------------ */
void test_encode_multi_block_state_carry(void)
{
    /* Generate 56 samples (2 blocks worth). */
    int16_t samples[56];
    for (int i = 0; i < 56; i++) {
        samples[i] = (int16_t)(i * 400 - 11200);
    }

    /* Encode two blocks sequentially. */
    spu94_adpcm_state enc_st = {0, 0};
    uint8_t block_a[16], block_b[16];
    spu94_adpcm_encode_block(&enc_st, &samples[0],  0x00, block_a);
    spu94_adpcm_encode_block(&enc_st, &samples[28], 0x00, block_b);

    /* Decode two blocks sequentially. */
    spu94_adpcm_state dec_st = {0, 0};
    int16_t out_a[28], out_b[28];
    spu94_adpcm_decode_block(&dec_st, block_a, out_a);
    spu94_adpcm_decode_block(&dec_st, block_b, out_b);

    /* After both blocks, encoder and decoder states must match. */
    TEST_ASSERT_EQUAL_INT16(enc_st.old, dec_st.old);
    TEST_ASSERT_EQUAL_INT16(enc_st.older, dec_st.older);
}

/* ------------------------------------------------------------------ */
/* Nibbles in range: extract nibbles from encoded block, verify [-8,7] */
/* ------------------------------------------------------------------ */
void test_encode_nibbles_in_range(void)
{
    int16_t in[28];
    for (int i = 0; i < 28; i++) {
        in[i] = (int16_t)(i * 2000 - 28000);
    }

    spu94_adpcm_state st = {0, 0};
    uint8_t block[16];
    spu94_adpcm_encode_block(&st, in, 0x00, block);

    /* Extract and verify all 28 nibbles. */
    for (int i = 0; i < 14; i++) {
        int lo = block[2 + i] & 0x0F;
        int hi = (block[2 + i] >> 4) & 0x0F;

        /* Sign-extend to check range. */
        if (lo >= 8) lo -= 16;
        if (hi >= 8) hi -= 16;

        TEST_ASSERT_GREATER_OR_EQUAL(-8, lo);
        TEST_ASSERT_LESS_OR_EQUAL(7, lo);
        TEST_ASSERT_GREATER_OR_EQUAL(-8, hi);
        TEST_ASSERT_LESS_OR_EQUAL(7, hi);
    }
}

/* ------------------------------------------------------------------ */
/* Main: Unity test runner                                             */
/* ------------------------------------------------------------------ */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_encode_silence);
    RUN_TEST(test_encode_decode_roundtrip_deterministic);
    RUN_TEST(test_encode_decode_consistency);
    RUN_TEST(test_encode_produces_valid_shift);
    RUN_TEST(test_encode_produces_valid_filter);
    RUN_TEST(test_encode_flags_passed_through);
    RUN_TEST(test_encode_state_uses_reconstructed);
    RUN_TEST(test_encode_shift12_no_ub);
    RUN_TEST(test_encode_loud_signal_selects_low_shift);
    RUN_TEST(test_encode_quiet_signal_selects_high_shift);
    RUN_TEST(test_encode_multi_block_state_carry);
    RUN_TEST(test_encode_nibbles_in_range);

    return UNITY_END();
}
