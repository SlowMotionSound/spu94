/* tests/unit/voice/test_sample_loader.c
 * Phase 27: Unity unit tests for spu94_sample_encode_to_ram.
 *
 * Tests cover:
 *   - NULL pcm returns -1
 *   - Correct byte count for N samples (ceil(N/28) * 16)
 *   - Overflow: ram_offset + encoded_size > ram_size returns -1
 *   - Last block flag = 0x01 (END) when loop_enable=0
 *   - Last block flag = 0x03 (END|REPEAT) when loop_enable=1
 *   - Round-trip: encode + decode produces plausible audio
 *   - Zero ram_size with 28 samples returns -1 (overflow guard)
 */

#include "unity.h"
#include <spu94/spu94_sample_loader.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* ---------------------------------------------------------------
 * Test: NULL pcm returns -1
 * --------------------------------------------------------------- */
void test_null_pcm_returns_error(void) {
    uint8_t ram[256];
    int32_t result = spu94_sample_encode_to_ram(NULL, 28, ram, 0, 256, 0);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/* ---------------------------------------------------------------
 * Test: NULL voice_ram returns -1
 * --------------------------------------------------------------- */
void test_null_ram_returns_error(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));
    int32_t result = spu94_sample_encode_to_ram(pcm, 28, NULL, 0, 256, 0);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/* ---------------------------------------------------------------
 * Test: zero num_samples returns -1
 * --------------------------------------------------------------- */
void test_zero_samples_returns_error(void) {
    uint8_t ram[256];
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));
    int32_t result = spu94_sample_encode_to_ram(pcm, 0, ram, 0, 256, 0);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/* ---------------------------------------------------------------
 * Test: correct byte count for exactly 28 samples (1 block = 16 bytes)
 * --------------------------------------------------------------- */
void test_one_block_byte_count(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));
    pcm[0] = 1000; /* non-silent */

    uint8_t ram[256];
    memset(ram, 0, sizeof(ram));

    int32_t result = spu94_sample_encode_to_ram(pcm, 28, ram, 0, 256, 0);
    TEST_ASSERT_EQUAL_INT32(16, result);
}

/* ---------------------------------------------------------------
 * Test: correct byte count for 29 samples (2 blocks = 32 bytes)
 * --------------------------------------------------------------- */
void test_two_blocks_byte_count(void) {
    int16_t pcm[29];
    memset(pcm, 0, sizeof(pcm));
    pcm[0] = 5000;

    uint8_t ram[256];
    memset(ram, 0, sizeof(ram));

    int32_t result = spu94_sample_encode_to_ram(pcm, 29, ram, 0, 256, 0);
    /* ceil(29/28) = 2 blocks => 32 bytes */
    TEST_ASSERT_EQUAL_INT32(32, result);
}

/* ---------------------------------------------------------------
 * Test: overflow guard - encoded size exceeds available RAM
 * --------------------------------------------------------------- */
void test_overflow_returns_error(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));

    uint8_t ram[16];

    /* ram_offset=0, ram_size=0, need 16 bytes -> overflow */
    int32_t result = spu94_sample_encode_to_ram(pcm, 28, ram, 0, 0, 0);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/* ---------------------------------------------------------------
 * Test: overflow with non-zero offset
 * --------------------------------------------------------------- */
void test_offset_overflow_returns_error(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));

    uint8_t ram[32];

    /* ram_offset=20, ram_size=32, need 16 bytes -> 20+16=36 > 32 */
    int32_t result = spu94_sample_encode_to_ram(pcm, 28, ram, 20, 32, 0);
    TEST_ASSERT_EQUAL_INT32(-1, result);
}

/* ---------------------------------------------------------------
 * Test: last block flag = 0x01 (END) when loop_enable=0
 * --------------------------------------------------------------- */
void test_last_block_flag_end_no_loop(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));
    pcm[0] = 1000;

    uint8_t ram[256];
    memset(ram, 0, sizeof(ram));

    spu94_sample_encode_to_ram(pcm, 28, ram, 0, 256, 0);

    /* block[1] is the flag byte */
    TEST_ASSERT_EQUAL_UINT8(SPU94_VAG_FLAG_END, ram[1]);
}

/* ---------------------------------------------------------------
 * Test: last block flag = 0x03 (END|REPEAT) when loop_enable=1
 * --------------------------------------------------------------- */
void test_last_block_flag_end_with_loop(void) {
    int16_t pcm[28];
    memset(pcm, 0, sizeof(pcm));
    pcm[0] = 1000;

    uint8_t ram[256];
    memset(ram, 0, sizeof(ram));

    spu94_sample_encode_to_ram(pcm, 28, ram, 0, 256, 1);

    /* block[1] should be END | REPEAT = 0x01 | 0x02 = 0x03 */
    uint8_t expected = SPU94_VAG_FLAG_END | SPU94_VAG_FLAG_LOOP_REPEAT;
    TEST_ASSERT_EQUAL_UINT8(expected, ram[1]);
}

/* ---------------------------------------------------------------
 * Test: round-trip encode + decode produces plausible audio
 * --------------------------------------------------------------- */
void test_roundtrip_encode_decode(void) {
    /* Create a simple 28-sample ramp waveform */
    int16_t pcm[28];
    for (int i = 0; i < 28; i++) {
        pcm[i] = (int16_t)(i * 1000);
    }

    uint8_t ram[256];
    memset(ram, 0, sizeof(ram));

    int32_t bytes = spu94_sample_encode_to_ram(pcm, 28, ram, 0, 256, 0);
    TEST_ASSERT_EQUAL_INT32(16, bytes);

    /* Decode the block */
    spu94_adpcm_state dec_state;
    dec_state.old = 0;
    dec_state.older = 0;
    int16_t decoded[28];
    spu94_adpcm_decode_block(&dec_state, ram, decoded);

    /* Check: decoded output should be within ADPCM quantization error of input.
     * ADPCM is lossy, but a ramp from 0 to 27000 should decode to something
     * in the same ballpark. We allow generous tolerance (4096 per sample). */
    for (int i = 0; i < 28; i++) {
        int32_t diff = (int32_t)decoded[i] - (int32_t)pcm[i];
        if (diff < 0) diff = -diff;
        TEST_ASSERT_TRUE_MESSAGE(diff < 4096,
            "Round-trip decode error exceeds ADPCM tolerance");
    }
}

/* ---------------------------------------------------------------
 * Test: multi-block encoding with offset
 * --------------------------------------------------------------- */
void test_encode_with_offset(void) {
    int16_t pcm[56]; /* 2 blocks */
    for (int i = 0; i < 56; i++) {
        pcm[i] = (int16_t)(i * 500);
    }

    uint8_t ram[256];
    memset(ram, 0xFF, sizeof(ram)); /* fill with sentinel */

    int32_t bytes = spu94_sample_encode_to_ram(pcm, 56, ram, 32, 256, 0);
    TEST_ASSERT_EQUAL_INT32(32, bytes); /* 2 blocks = 32 bytes */

    /* Verify the first 32 bytes of ram are still sentinel (untouched) */
    for (int i = 0; i < 32; i++) {
        TEST_ASSERT_EQUAL_UINT8(0xFF, ram[i]);
    }

    /* Verify the last block's flag byte at offset 32 + 16 + 1 = 49 */
    TEST_ASSERT_EQUAL_UINT8(SPU94_VAG_FLAG_END, ram[32 + 16 + 1]);
}

/* ---------------------------------------------------------------
 * Main
 * --------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_null_pcm_returns_error);
    RUN_TEST(test_null_ram_returns_error);
    RUN_TEST(test_zero_samples_returns_error);
    RUN_TEST(test_one_block_byte_count);
    RUN_TEST(test_two_blocks_byte_count);
    RUN_TEST(test_overflow_returns_error);
    RUN_TEST(test_offset_overflow_returns_error);
    RUN_TEST(test_last_block_flag_end_no_loop);
    RUN_TEST(test_last_block_flag_end_with_loop);
    RUN_TEST(test_roundtrip_encode_decode);
    RUN_TEST(test_encode_with_offset);
    return UNITY_END();
}
