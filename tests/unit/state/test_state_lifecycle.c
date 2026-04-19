/* tests/unit/state/test_state_lifecycle.c
 * Phase 2 Plan 01 Task 1 (TDD — RED)
 *
 * Unity tests for the spu94_state lifecycle contract:
 *   - spu94_state_size() determinism + bound
 *   - spu94_init() argument validation (NULL, undersize, misalignment, work-buf
 *     inconsistency) and happy path
 *   - spu94_reset() work-buffer zeroing + pointer preservation
 *   - spu94_destroy() null-safety
 *
 * Contracts per 02-01-PLAN.md <behavior> + 02-RESEARCH.md "State-Allocation
 * Pattern" + 02-CONTEXT.md D-14.
 *
 * All buffers are caller-owned (D-13) and use alignas(SPU94_STATE_ALIGN_MAX)
 * to satisfy 02-RESEARCH.md Pitfall 2.
 */
#include "unity.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

/* Unity lifecycle hooks — required by the framework. */
void setUp(void) {}
void tearDown(void) {}

/* -------------------------------------------------------------------------- */
/* spu94_state_size()                                                         */
/* -------------------------------------------------------------------------- */

void test_state_size_deterministic(void) {
    size_t s1 = spu94_state_size();
    size_t s2 = spu94_state_size();
    TEST_ASSERT_EQUAL_size_t(s1, s2);
    TEST_ASSERT_TRUE_MESSAGE(s1 > 0, "state size must be > 0");
    TEST_ASSERT_TRUE_MESSAGE(s1 <= SPU94_STATE_SIZE_MAX,
        "state size must fit in SPU94_STATE_SIZE_MAX");
}

/* -------------------------------------------------------------------------- */
/* spu94_init — failure modes                                                 */
/* -------------------------------------------------------------------------- */

void test_init_null_state_buf(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[64];
    TEST_ASSERT_NULL(spu94_init(NULL, SPU94_STATE_SIZE_MAX, work, sizeof work));
}

void test_init_undersized_state_buf(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[64];
    TEST_ASSERT_NULL(spu94_init(state_buf, 1, work, sizeof work));
    /* Also test size == spu94_state_size() - 1 boundary */
    size_t need = spu94_state_size();
    TEST_ASSERT_TRUE(need > 0);
    TEST_ASSERT_NULL(spu94_init(state_buf, need - 1, work, sizeof work));
}

void test_init_misaligned_state_buf(void) {
    /* Allocate with padding so buf + 1 is always inside the allocation
     * and guaranteed misaligned vs SPU94_STATE_ALIGN_MAX (>=2). */
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char
        state_buf[SPU94_STATE_SIZE_MAX + SPU94_STATE_ALIGN_MAX];
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[64];
    TEST_ASSERT_NULL(spu94_init(state_buf + 1, SPU94_STATE_SIZE_MAX,
                                work, sizeof work));
}

void test_init_inconsistent_work_buf(void) {
    /* NULL work_buf + non-zero work_buf_size is a caller bug. */
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    TEST_ASSERT_NULL(spu94_init(state_buf, SPU94_STATE_SIZE_MAX, NULL, 128));
}

/* -------------------------------------------------------------------------- */
/* spu94_init — happy path + zero-size work buf                               */
/* -------------------------------------------------------------------------- */

void test_init_success(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[64];
    spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
                                work, sizeof work);
    TEST_ASSERT_NOT_NULL(s);
    /* Destroy safe after init. */
    spu94_destroy(s);
}

void test_init_accepts_null_workbuf_with_zero_size(void) {
    /* Zero-sized work-buffer calls are a legal "don't-care" configuration:
     * no advance/tick will be issued, so NULL + 0 is OK. */
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    spu94_destroy(s);
}

/* -------------------------------------------------------------------------- */
/* spu94_destroy                                                              */
/* -------------------------------------------------------------------------- */

void test_destroy_null_safe(void) {
    /* Must not crash. */
    spu94_destroy(NULL);
    TEST_PASS();
}

/* -------------------------------------------------------------------------- */
/* spu94_reset                                                                */
/* -------------------------------------------------------------------------- */

void test_reset_zeros_work_buffer(void) {
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[128];
    /* Pre-fill work buffer with a sentinel. */
    for (size_t i = 0; i < sizeof work; ++i) work[i] = 0xCDu;

    spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
                                work, sizeof work);
    TEST_ASSERT_NOT_NULL(s);
    /* Sentinel survives init (init does NOT zero the caller's work buf per D-14). */
    /* Reset: contract zeroes the work buffer. */
    spu94_reset(s);
    for (size_t i = 0; i < sizeof work; ++i) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00u, work[i],
            "spu94_reset must zero the caller's work buffer");
    }
    spu94_destroy(s);
}

void test_reset_null_safe(void) {
    spu94_reset(NULL);
    TEST_PASS();
}

void test_reset_then_destroy_chain(void) {
    /* Sanity: init → reset → destroy completes without crashing and the
     * lifecycle boundary behaves consistently. */
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf[SPU94_STATE_SIZE_MAX];
    alignas(SPU94_STATE_ALIGN_MAX) unsigned char work[64];
    spu94_state *s = spu94_init(state_buf, SPU94_STATE_SIZE_MAX,
                                work, sizeof work);
    TEST_ASSERT_NOT_NULL(s);
    spu94_reset(s);
    spu94_destroy(s);
    TEST_PASS();
}

/* -------------------------------------------------------------------------- */
/* Result-code enum presence                                                  */
/* -------------------------------------------------------------------------- */

void test_result_enum_values(void) {
    /* D-07 + CONTEXT Claude's Discretion: fourth code is SPU94_TYPE_MISMATCH. */
    TEST_ASSERT_EQUAL_INT(0, (int)SPU94_OK);
    /* Distinct values — only SPU94_OK is pinned to 0; the others are
     * append-only, so we only assert they are not equal to SPU94_OK. */
    TEST_ASSERT_NOT_EQUAL((int)SPU94_OK, (int)SPU94_CLAMPED);
    TEST_ASSERT_NOT_EQUAL((int)SPU94_OK, (int)SPU94_UNKNOWN_REG);
    TEST_ASSERT_NOT_EQUAL((int)SPU94_OK, (int)SPU94_TYPE_MISMATCH);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_state_size_deterministic);
    RUN_TEST(test_init_null_state_buf);
    RUN_TEST(test_init_undersized_state_buf);
    RUN_TEST(test_init_misaligned_state_buf);
    RUN_TEST(test_init_inconsistent_work_buf);
    RUN_TEST(test_init_success);
    RUN_TEST(test_init_accepts_null_workbuf_with_zero_size);
    RUN_TEST(test_destroy_null_safe);
    RUN_TEST(test_reset_zeros_work_buffer);
    RUN_TEST(test_reset_null_safe);
    RUN_TEST(test_reset_then_destroy_chain);
    RUN_TEST(test_result_enum_values);
    return UNITY_END();
}
