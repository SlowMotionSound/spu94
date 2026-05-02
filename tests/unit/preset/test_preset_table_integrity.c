#include "unity.h"
#include <spu94/spu94.h>
#include <string.h>

static const char *const expected_names[SPU94_PRESET__COUNT] = {
    "Off", "Room", "Studio A", "Studio B", "Studio C",
    "Hall", "Half Echo", "Space Echo", "Echo", "Delay", "Init"
};

/* Off-preset m-prefix (buffer-address) register indices that carry the
 * 0x0001 defensive values per BIB-011 (nocash). Established in the
 * Task 4 audit resolution at
 * .planning/research/05-preset-values-audit-resolutions.md.
 *
 * All 16 indices correspond to the exhaustive set of buffer-offset
 * registers (mLSAME, mRSAME, mLCOMB1..4 x {L,R}, mLDIFF, mRDIFF,
 * mLAPF1..2 x {L,R}). Every other Off register is zero. */
static const int off_nonzero_indices[16] = {
    13, 14, 15, 16, 17, 18,
    21, 22, 23, 24, 25, 26,
    29, 30, 31, 32,
};

/* Compile-time enum stability pins. */
_Static_assert(SPU94_PRESET_OFF        == 0, "OFF id stable");
_Static_assert(SPU94_PRESET_ROOM       == 1, "ROOM id stable");
_Static_assert(SPU94_PRESET_STUDIO_A   == 2, "STUDIO_A id stable");
_Static_assert(SPU94_PRESET_STUDIO_B   == 3, "STUDIO_B id stable");
_Static_assert(SPU94_PRESET_STUDIO_C   == 4, "STUDIO_C id stable");
_Static_assert(SPU94_PRESET_HALL       == 5, "HALL id stable");
_Static_assert(SPU94_PRESET_HALF_ECHO  == 6, "HALF_ECHO id stable");
_Static_assert(SPU94_PRESET_SPACE_ECHO == 7, "SPACE_ECHO id stable");
_Static_assert(SPU94_PRESET_ECHO       == 8, "ECHO id stable");
_Static_assert(SPU94_PRESET_DELAY      == 9, "DELAY id stable");
_Static_assert(SPU94_PRESET_INIT  == 10, "MONO_HALL id stable");
_Static_assert(SPU94_PRESET__COUNT     == 11, "count == 11");

void setUp(void)    {}
void tearDown(void) {}

static void test_count(void) {
    TEST_ASSERT_EQUAL_INT(11, (int)SPU94_PRESET__COUNT);
}

static void test_names_present_and_match(void) {
    for (int id = 0; id < SPU94_PRESET__COUNT; id++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(spu94_presets[id].name,
            "every preset has a non-NULL name");
        TEST_ASSERT_EQUAL_STRING(expected_names[id], spu94_presets[id].name);
    }
}

static void test_off_matches_audit(void) {
    /* Per Task 4 audit resolution (BIB-011 priority): Off has 0x0001 at
     * the 16 m-prefix register indices and 0x0000 everywhere else. */
    for (int r = 0; r < SPU94_REG__COUNT; r++) {
        int is_nonzero_index = 0;
        for (size_t k = 0; k < sizeof(off_nonzero_indices) / sizeof(off_nonzero_indices[0]); k++) {
            if (off_nonzero_indices[k] == r) {
                is_nonzero_index = 1;
                break;
            }
        }
        int16_t expected = is_nonzero_index ? (int16_t)0x0001 : (int16_t)0x0000;
        TEST_ASSERT_EQUAL_INT16_MESSAGE(expected,
            spu94_presets[SPU94_PRESET_OFF].regs[r],
            "Off preset register must match BIB-011 audit");
    }
}

static void test_regs_array_length_pinned(void) {
    TEST_ASSERT_EQUAL_size_t(
        (size_t)SPU94_REG__COUNT * sizeof(int16_t),
        sizeof(spu94_presets[0].regs));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_count);
    RUN_TEST(test_names_present_and_match);
    RUN_TEST(test_off_matches_audit);
    RUN_TEST(test_regs_array_length_pinned);
    return UNITY_END();
}
