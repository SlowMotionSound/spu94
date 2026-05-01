/* test_preset_roundtrip.c -- Phase 13 Plan 01 Task 2
 *
 * Validates spu94_preset_save output format: error handling, version header,
 * metadata, section structure, field coverage, hex format, and edge cases.
 * 15 tests covering PRE-01, PRE-03, PRE-05.
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf[64 * 1024];
static spu94_state *state = NULL;
static char preset_buf[SPU94_PRESET_BUF_SIZE];

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}

void tearDown(void) { state = NULL; }

/* -----------------------------------------------------------------------
 * Error handling tests
 * ----------------------------------------------------------------------- */

static void test_save_null_state_returns_error(void)
{
    int rc = spu94_preset_save(NULL, "x", "y", preset_buf, sizeof preset_buf);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

static void test_save_null_buf_returns_error(void)
{
    int rc = spu94_preset_save(state, "x", "y", NULL, sizeof preset_buf);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

static void test_save_zero_bufsize_returns_error(void)
{
    int rc = spu94_preset_save(state, "x", "y", preset_buf, 0);
    TEST_ASSERT_EQUAL_INT(-1, rc);
}

/* -----------------------------------------------------------------------
 * Format structure tests
 * ----------------------------------------------------------------------- */

static void test_save_version_header(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "test",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);
    /* version=1 must be the very first line */
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "version=1\n"));
    TEST_ASSERT_TRUE_MESSAGE(
        strstr(preset_buf, "version=1\n") == preset_buf,
        "version=1 must be the first line of output");
}

static void test_save_metadata_fields(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "test",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "name=Hall\n"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "description=test\n"));
}

static void test_save_section_headers(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "\n[registers]\n"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "\n[mixer]\n"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "\n[dac]\n"));
}

static void test_save_section_comments(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "# SPU reverb registers"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "# Mixer faders"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "# DAC coloration"));
}

/* -----------------------------------------------------------------------
 * Field coverage tests
 * ----------------------------------------------------------------------- */

static void test_save_register_count(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    /* Count occurrences of "=0x" between [registers] and [mixer] */
    const char *reg_start = strstr(preset_buf, "[registers]");
    const char *mix_start = strstr(preset_buf, "[mixer]");
    TEST_ASSERT_NOT_NULL(reg_start);
    TEST_ASSERT_NOT_NULL(mix_start);
    TEST_ASSERT_TRUE(mix_start > reg_start);

    int count = 0;
    const char *p = reg_start;
    while (p < mix_start) {
        p = strstr(p, "=0x");
        if (!p || p >= mix_start) break;
        count++;
        p += 3;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(35, count,
        "Expected exactly 35 register entries (SPU94_REG__COUNT) between [registers] and [mixer]");
}

static void test_save_register_names_present(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    char pattern[64];
    char msg[128];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        const char *name = spu94_reg_name((spu94_reg_t)r);
        snprintf(pattern, sizeof pattern, "%s=", name);
        snprintf(msg, sizeof msg, "Register key '%s=' not found in output", name);
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(preset_buf, pattern), msg);
    }
}

static void test_save_mixer_fields_present(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "input_gain=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dry_fader=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "patina_fader=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dry_send=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "patina_send=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "reverb_fader=0x"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "latency_comp="));
}

static void test_save_dac_fields_present(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dac_enabled="));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dac_fir_enabled="));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dac_noise_enabled="));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "dac_true_oversample="));
}

static void test_save_no_adpcm_field(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    /* ADPCM toggle is not serialized per D-06 */
    TEST_ASSERT_NULL(strstr(preset_buf, "adpcm"));
}

/* -----------------------------------------------------------------------
 * Hex format tests
 * ----------------------------------------------------------------------- */

static void test_save_hex_format_4digit(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_preset_save(state, "Hall", "test", preset_buf, sizeof preset_buf);

    /* vLOUT in the Hall preset should be non-zero (it's 0x7FFF).
     * Find "vLOUT=0x" and verify exactly 4 hex digits followed by newline. */
    const char *p = strstr(preset_buf, "vLOUT=0x");
    TEST_ASSERT_NOT_NULL_MESSAGE(p, "vLOUT=0x not found in output");
    p += strlen("vLOUT=0x");  /* now points to first hex digit */

    /* Check 4 hex digits */
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        int is_hex = (c >= '0' && c <= '9') ||
                     (c >= 'A' && c <= 'F') ||
                     (c >= 'a' && c <= 'f');
        char hmsg[64];
        snprintf(hmsg, sizeof hmsg, "Position %d of hex value is not hex: '%c'", i, c);
        TEST_ASSERT_TRUE_MESSAGE(is_hex, hmsg);
    }
    /* 5th character must be newline (exactly 4 digits) */
    TEST_ASSERT_EQUAL_CHAR_MESSAGE('\n', p[4],
        "Hex value should be exactly 4 digits (5th char must be newline)");
}

/* -----------------------------------------------------------------------
 * Edge case tests
 * ----------------------------------------------------------------------- */

static void test_save_null_name_description(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, NULL, NULL,
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);
    /* Empty values, not the string "null" */
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "name=\n"));
    TEST_ASSERT_NOT_NULL(strstr(preset_buf, "description=\n"));
    TEST_ASSERT_NULL(strstr(preset_buf, "null"));
}

static void test_save_buf_too_small(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "test", preset_buf, 10);
    TEST_ASSERT_EQUAL_INT(-2, rc);
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_save_null_state_returns_error);
    RUN_TEST(test_save_null_buf_returns_error);
    RUN_TEST(test_save_zero_bufsize_returns_error);
    RUN_TEST(test_save_version_header);
    RUN_TEST(test_save_metadata_fields);
    RUN_TEST(test_save_section_headers);
    RUN_TEST(test_save_section_comments);
    RUN_TEST(test_save_register_count);
    RUN_TEST(test_save_register_names_present);
    RUN_TEST(test_save_mixer_fields_present);
    RUN_TEST(test_save_dac_fields_present);
    RUN_TEST(test_save_no_adpcm_field);
    RUN_TEST(test_save_hex_format_4digit);
    RUN_TEST(test_save_null_name_description);
    RUN_TEST(test_save_buf_too_small);
    return UNITY_END();
}
