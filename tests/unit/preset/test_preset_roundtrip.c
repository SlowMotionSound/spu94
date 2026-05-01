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

/* Second state for round-trip comparison (Plan 02) */
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf2[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf2[64 * 1024];
static spu94_state *state2 = NULL;

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
 * Round-trip fidelity tests (Phase 13 Plan 02 -- PRE-04)
 * ----------------------------------------------------------------------- */

/* Helper: init and reset state2 for round-trip comparison. */
static void init_state2(void)
{
    state2 = spu94_init(state_buf2, sizeof state_buf2,
                        work_buf2, sizeof work_buf2);
    TEST_ASSERT_NOT_NULL(state2);
    spu94_reset(state2);
}

static void test_roundtrip_hall_registers(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "round-trip test",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);

    init_state2();
    spu94_result_t lr = spu94_preset_load(state2, preset_buf, (size_t)rc);
    TEST_ASSERT_EQUAL_INT((int)SPU94_OK, (int)lr);

    /* Compare all 35 registers */
    int16_t regs1[SPU94_REG__COUNT];
    int16_t regs2[SPU94_REG__COUNT];
    spu94_snapshot_registers(state, regs1);
    spu94_snapshot_registers(state2, regs2);

    char msg[96];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        snprintf(msg, sizeof msg, "register %s mismatch",
                 spu94_reg_name((spu94_reg_t)r));
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(
            (uint16_t)regs1[r], (uint16_t)regs2[r], msg);
    }
}

static void test_roundtrip_hall_mixer(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "mixer test",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);

    init_state2();
    spu94_preset_load(state2, preset_buf, (size_t)rc);

    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_input_gain(state), spu94_get_input_gain(state2),
        "input_gain mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_dry_fader(state), spu94_get_dry_fader(state2),
        "dry_fader mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_patina_fader(state), spu94_get_patina_fader(state2),
        "patina_fader mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_dry_send(state), spu94_get_dry_send(state2),
        "dry_send mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_patina_send(state), spu94_get_patina_send(state2),
        "patina_send mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_reverb_fader(state), spu94_get_reverb_fader(state2),
        "reverb_fader mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_latency_comp(state), spu94_get_latency_comp(state2),
        "latency_comp mismatch");
}

static void test_roundtrip_hall_dac(void)
{
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int rc = spu94_preset_save(state, "Hall", "dac test",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);

    init_state2();
    spu94_preset_load(state2, preset_buf, (size_t)rc);

    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_enabled(state), spu94_get_dac_enabled(state2),
        "dac_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_fir_enabled(state), spu94_get_dac_fir_enabled(state2),
        "dac_fir_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_noise_enabled(state), spu94_get_dac_noise_enabled(state2),
        "dac_noise_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_true_oversample(state), spu94_get_dac_true_oversample(state2),
        "dac_true_oversample mismatch");
}

static void test_roundtrip_custom_state(void)
{
    /* Set up a non-default state exercising ALL field types */
    spu94_load_preset(state, SPU94_PRESET_DELAY);
    spu94_set_input_gain(state, 0x4000);
    spu94_set_dry_fader(state, 0x2000);
    spu94_set_patina_fader(state, 0x1000);
    spu94_set_dry_send(state, 0x3000);
    spu94_set_patina_send(state, 0x0800);
    spu94_set_reverb_fader(state, 0x6000);
    spu94_set_latency_comp(state, 0);
    spu94_set_dac_enabled(state, 0);
    spu94_set_dac_fir_enabled(state, 0);
    spu94_set_dac_noise_enabled(state, 1);
    spu94_set_dac_true_oversample(state, 0);

    int rc = spu94_preset_save(state, "Custom", "full roundtrip",
                               preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc);

    init_state2();
    spu94_preset_load(state2, preset_buf, (size_t)rc);

    /* Compare all 35 registers */
    int16_t regs1[SPU94_REG__COUNT];
    int16_t regs2[SPU94_REG__COUNT];
    spu94_snapshot_registers(state, regs1);
    spu94_snapshot_registers(state2, regs2);

    char msg[96];
    for (int r = 0; r < (int)SPU94_REG__COUNT; r++) {
        snprintf(msg, sizeof msg, "custom register %s mismatch",
                 spu94_reg_name((spu94_reg_t)r));
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(
            (uint16_t)regs1[r], (uint16_t)regs2[r], msg);
    }

    /* Compare 7 mixer fields */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_input_gain(state), spu94_get_input_gain(state2),
        "custom input_gain mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_dry_fader(state), spu94_get_dry_fader(state2),
        "custom dry_fader mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_patina_fader(state), spu94_get_patina_fader(state2),
        "custom patina_fader mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_dry_send(state), spu94_get_dry_send(state2),
        "custom dry_send mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_patina_send(state), spu94_get_patina_send(state2),
        "custom patina_send mismatch");
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        spu94_get_reverb_fader(state), spu94_get_reverb_fader(state2),
        "custom reverb_fader mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_latency_comp(state), spu94_get_latency_comp(state2),
        "custom latency_comp mismatch");

    /* Compare 4 DAC toggles */
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_enabled(state), spu94_get_dac_enabled(state2),
        "custom dac_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_fir_enabled(state), spu94_get_dac_fir_enabled(state2),
        "custom dac_fir_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_noise_enabled(state), spu94_get_dac_noise_enabled(state2),
        "custom dac_noise_enabled mismatch");
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        spu94_get_dac_true_oversample(state), spu94_get_dac_true_oversample(state2),
        "custom dac_true_oversample mismatch");
}

static void test_roundtrip_preserves_bytes_exact(void)
{
    /* Prove serialization determinism: save(load(save(s))) == save(s) */
    spu94_load_preset(state, SPU94_PRESET_HALL);
    spu94_set_input_gain(state, 0x5555);
    spu94_set_dac_enabled(state, 0);

    int rc1 = spu94_preset_save(state, "Det", "determinism",
                                preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN(0, rc1);

    /* Load into state2 */
    init_state2();
    spu94_preset_load(state2, preset_buf, (size_t)rc1);

    /* Save state2 to a second buffer */
    char preset_buf2[SPU94_PRESET_BUF_SIZE];
    int rc2 = spu94_preset_save(state2, "Det", "determinism",
                                preset_buf2, sizeof preset_buf2);
    TEST_ASSERT_GREATER_THAN(0, rc2);

    /* The two buffers must be identical */
    TEST_ASSERT_EQUAL_INT_MESSAGE(rc1, rc2,
        "save lengths differ after round-trip");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(preset_buf, preset_buf2,
        "save output differs after round-trip (determinism failure)");
}

/* -----------------------------------------------------------------------
 * main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    /* Plan 01: save format tests (1-15) */
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
    /* Plan 02: round-trip fidelity tests (16-20) */
    RUN_TEST(test_roundtrip_hall_registers);
    RUN_TEST(test_roundtrip_hall_mixer);
    RUN_TEST(test_roundtrip_hall_dac);
    RUN_TEST(test_roundtrip_custom_state);
    RUN_TEST(test_roundtrip_preserves_bytes_exact);
    return UNITY_END();
}
