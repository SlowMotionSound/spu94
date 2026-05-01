/* test_preset_parse.c -- Phase 13 Plan 02 Task 2B
 *
 * Parser edge-case tests proving:
 *   D-08: missing keys retain engine defaults
 *   D-09: unknown keys silently ignored
 *   Comments, blank lines, Windows line endings, malformed lines tolerated
 *   Register restoration via key matching
 *   DAC toggle parsing
 *   No version requirement for forward compatibility
 *
 * 12 tests covering PRE-02 parser robustness.
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

void setUp(void) {
    state = spu94_init(state_buf, sizeof state_buf, work_buf, sizeof work_buf);
    TEST_ASSERT_NOT_NULL(state);
    spu94_reset(state);
}

void tearDown(void) { state = NULL; }

/* -----------------------------------------------------------------------
 * Error handling tests
 * ----------------------------------------------------------------------- */

static void test_parse_null_state(void)
{
    spu94_result_t rc = spu94_preset_load(NULL, "version=1\n", 10);
    TEST_ASSERT_EQUAL_INT((int)SPU94_INVALID_STATE, (int)rc);
}

static void test_parse_null_buf(void)
{
    spu94_result_t rc = spu94_preset_load(state, NULL, 0);
    TEST_ASSERT_EQUAL_INT((int)SPU94_INVALID_ARG, (int)rc);
}

static void test_parse_empty_buf(void)
{
    spu94_result_t rc = spu94_preset_load(state, "", 0);
    TEST_ASSERT_EQUAL_INT((int)SPU94_INVALID_ARG, (int)rc);
}

/* -----------------------------------------------------------------------
 * Tolerance tests (D-08, D-09)
 * ----------------------------------------------------------------------- */

static void test_parse_missing_keys_retain_defaults(void)
{
    /* After reset, defaults include latency_comp=1, dac_true_oversample=1.
     * Load a minimal preset that only sets input_gain. Everything else must
     * remain at its default. */
    int16_t default_dry_fader = spu94_get_dry_fader(state);
    int default_latency = spu94_get_latency_comp(state);
    int default_dac_tos = spu94_get_dac_true_oversample(state);

    const char *preset = "version=1\n[mixer]\ninput_gain=0x4000\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);

    /* The one key we set */
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));

    /* Defaults retained (D-08) */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        (uint16_t)default_dry_fader, (uint16_t)spu94_get_dry_fader(state),
        "dry_fader should retain default (D-08)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(default_latency,
        spu94_get_latency_comp(state),
        "latency_comp should retain default 1 (D-08)");
    TEST_ASSERT_EQUAL_INT_MESSAGE(default_dac_tos,
        spu94_get_dac_true_oversample(state),
        "dac_true_oversample should retain default 1 (D-08)");
}

static void test_parse_unknown_keys_ignored(void)
{
    const char *preset = "version=1\n[mixer]\ninput_gain=0x4000\nfake_key=0xDEAD\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));
}

static void test_parse_comment_lines_skipped(void)
{
    const char *preset =
        "version=1\n# this is a comment\n[mixer]\n"
        "# another comment\ninput_gain=0x4000\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));
}

static void test_parse_blank_lines_skipped(void)
{
    const char *preset =
        "version=1\n\n\n[mixer]\n\ninput_gain=0x4000\n\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));
}

/* -----------------------------------------------------------------------
 * Register and DAC restoration tests
 * ----------------------------------------------------------------------- */

static void test_parse_register_restoration(void)
{
    const char *preset =
        "version=1\n[registers]\nvLOUT=0x6000\nvROUT=0x5000\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x6000,
        (uint16_t)spu94_get_reg_i16(state, SPU94_REG_vLOUT));
    TEST_ASSERT_EQUAL_HEX16(0x5000,
        (uint16_t)spu94_get_reg_i16(state, SPU94_REG_vROUT));
}

static void test_parse_dac_toggles(void)
{
    const char *preset =
        "version=1\n[dac]\n"
        "dac_enabled=0\ndac_fir_enabled=1\n"
        "dac_noise_enabled=0\ndac_true_oversample=0\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_enabled(state));
    TEST_ASSERT_EQUAL_INT(1, spu94_get_dac_fir_enabled(state));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_noise_enabled(state));
    TEST_ASSERT_EQUAL_INT(0, spu94_get_dac_true_oversample(state));
}

/* -----------------------------------------------------------------------
 * Format resilience tests
 * ----------------------------------------------------------------------- */

static void test_parse_no_version_line_still_works(void)
{
    /* Jump straight to a section -- parser should not require version=1 */
    const char *preset = "[mixer]\ninput_gain=0x3000\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x3000, (uint16_t)spu94_get_input_gain(state));
}

static void test_parse_windows_line_endings(void)
{
    const char *preset = "version=1\r\n[mixer]\r\ninput_gain=0x4000\r\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));
}

static void test_parse_malformed_line_no_equals(void)
{
    const char *preset =
        "version=1\n[mixer]\nthis line has no equals sign\ninput_gain=0x4000\n";
    spu94_result_t rc = spu94_preset_load(state, preset, strlen(preset));
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)rc);
    TEST_ASSERT_EQUAL_HEX16(0x4000, (uint16_t)spu94_get_input_gain(state));
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_null_state);
    RUN_TEST(test_parse_null_buf);
    RUN_TEST(test_parse_empty_buf);
    RUN_TEST(test_parse_missing_keys_retain_defaults);
    RUN_TEST(test_parse_unknown_keys_ignored);
    RUN_TEST(test_parse_comment_lines_skipped);
    RUN_TEST(test_parse_blank_lines_skipped);
    RUN_TEST(test_parse_register_restoration);
    RUN_TEST(test_parse_dac_toggles);
    RUN_TEST(test_parse_no_version_line_still_works);
    RUN_TEST(test_parse_windows_line_endings);
    RUN_TEST(test_parse_malformed_line_no_equals);
    return UNITY_END();
}
