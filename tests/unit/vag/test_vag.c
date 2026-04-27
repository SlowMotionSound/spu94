/* Unit tests for VAG header read/write (Phase 3 Plan 01).
 *
 * Tests:
 *   1. Valid header parse
 *   2. Bad magic rejection
 *   3. Any-version acceptance (ADPCM-IO-03)
 *   4. Write-read roundtrip
 *   5. NULL name safety
 */

#include "unity.h"
#include <spu94/spu94_vag.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* --- Helper: write a big-endian uint32 into a buffer --- */
static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >>  8);
    p[3] = (uint8_t)(v);
}

/* --- Tests --- */

void test_vag_read_header_valid(void) {
    uint8_t buf[SPU94_VAG_HEADER_BYTES];
    memset(buf, 0, sizeof buf);

    /* Magic */
    buf[0] = 'V'; buf[1] = 'A'; buf[2] = 'G'; buf[3] = 'p';
    /* Version 2 at offset 0x04 */
    put_be32(buf + 0x04, 2);
    /* Data size at offset 0x0C */
    put_be32(buf + 0x0C, 0x100);
    /* Sample rate at offset 0x10 */
    put_be32(buf + 0x10, 44100);
    /* Name at offset 0x20 */
    memcpy(buf + 0x20, "testname", 8);

    spu94_vag_header out;
    int rc = spu94_vag_read_header(buf, &out);

    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(2, out.version);
    TEST_ASSERT_EQUAL_UINT32(0x100, out.data_size);
    TEST_ASSERT_EQUAL_UINT32(44100, out.sample_rate);
    TEST_ASSERT_EQUAL_STRING("testname", out.name);
}

void test_vag_read_header_bad_magic(void) {
    uint8_t buf[SPU94_VAG_HEADER_BYTES];
    memset(buf, 0, sizeof buf);
    buf[0] = 'X'; buf[1] = 'Y'; buf[2] = 'Z'; buf[3] = 'W';

    spu94_vag_header out;
    int rc = spu94_vag_read_header(buf, &out);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

void test_vag_read_header_accepts_any_version(void) {
    uint8_t buf[SPU94_VAG_HEADER_BYTES];
    memset(buf, 0, sizeof buf);
    buf[0] = 'V'; buf[1] = 'A'; buf[2] = 'G'; buf[3] = 'p';
    /* Version 0x20 (interleaved flag some tools use) */
    put_be32(buf + 0x04, 0x00000020);
    put_be32(buf + 0x0C, 256);
    put_be32(buf + 0x10, 22050);

    spu94_vag_header out;
    int rc = spu94_vag_read_header(buf, &out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(0x20, out.version);
}

void test_vag_write_header_roundtrip(void) {
    uint8_t buf[SPU94_VAG_HEADER_BYTES];
    spu94_vag_write_header(buf, 448, 44100, "hello");

    spu94_vag_header out;
    int rc = spu94_vag_read_header(buf, &out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_UINT32(2, out.version);
    TEST_ASSERT_EQUAL_UINT32(448, out.data_size);
    TEST_ASSERT_EQUAL_UINT32(44100, out.sample_rate);
    TEST_ASSERT_EQUAL_STRING("hello", out.name);
}

void test_vag_write_header_null_name(void) {
    uint8_t buf[SPU94_VAG_HEADER_BYTES];
    spu94_vag_write_header(buf, 256, 22050, NULL);

    spu94_vag_header out;
    int rc = spu94_vag_read_header(buf, &out);
    TEST_ASSERT_EQUAL_INT(0, rc);
    TEST_ASSERT_EQUAL_STRING("", out.name);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_vag_read_header_valid);
    RUN_TEST(test_vag_read_header_bad_magic);
    RUN_TEST(test_vag_read_header_accepts_any_version);
    RUN_TEST(test_vag_write_header_roundtrip);
    RUN_TEST(test_vag_write_header_null_name);
    return UNITY_END();
}
