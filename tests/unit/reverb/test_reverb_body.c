/* tests/unit/reverb/test_reverb_body.c — Phase 3 Plan 04 Task 2
 *
 * SC-1 composition equivalence: spu94_reverb_body equals sequential
 * stage composition. For N random (seeded) register configurations,
 * compare the full-body path vs the stage-by-stage path byte-for-byte.
 *
 * Design: the test assembles two states A and B that start byte-
 * identical (same state_buf layout; same work_buf contents; same
 * register values).  Path A calls spu94_reverb_body once.  Path B
 * snapshots each v* / d* register explicitly and calls each stage in
 * the same order reverb_body does, using those snapshots.  Then the
 * test asserts that work_buf bytes, every err_* accumulator, and
 * overflow_magnitude are identical.
 *
 * Pitfall 8 is also re-asserted here (buffer_address unchanged across
 * reverb_body), complementing the primary assertion in test_reverb_edges.c.
 *
 * mBASE is explicitly NOT part of the randomized registers because
 * setting mBASE to a random value triggers the ADR-0006 snap-on-write
 * that jumps buffer_address to a large offset, causing most reverb
 * buf reads/writes to fall outside the bounds-checked work_buf and
 * thereby reducing the test's coverage of the data path.  We seed
 * buffer_address implicitly via the post-init zero and let the
 * reverb network actually exercise the work_buf region.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_reverb_internal.h"
#include "../../../src/spu94/spu94_state_internal.h"
#include <spu94/spu94.h>
#include <spu94/spu94_registers.h>
#include <stdint.h>
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char gA_state[SPU94_STATE_SIZE_MAX];
static alignas(SPU94_STATE_ALIGN_MAX) unsigned char gB_state[SPU94_STATE_SIZE_MAX];
static alignas(2) unsigned char gA_work[0x2000];
static alignas(2) unsigned char gB_work[0x2000];

/* Linear congruential generator — deterministic, simple, reproducible.
 * Numerical Recipes constants (not for cryptographic use). */
static uint32_t lcg(uint32_t *s) {
    *s = (*s) * 1103515245u + 12345u;
    return *s;
}

/* Core composition-equivalence check for a single seed. Called by each
 * seeded test wrapper. */
static void run_body_equivalence(uint32_t seed) {
    /* 1. Seed both work buffers with the same deterministic noise. */
    uint32_t rng = seed;
    for (size_t i = 0; i < sizeof(gA_work); ++i) {
        gA_work[i] = (unsigned char)(lcg(&rng) & 0xFFu);
    }
    memcpy(gB_work, gA_work, sizeof(gB_work));

    /* 2. Init two states with the two distinct work buffers. */
    spu94_state *A = spu94_init(gA_state, sizeof(gA_state),
                                gA_work,  sizeof(gA_work));
    spu94_state *B = spu94_init(gB_state, sizeof(gB_state),
                                gB_work,  sizeof(gB_work));
    TEST_ASSERT_NOT_NULL(A);
    TEST_ASSERT_NOT_NULL(B);

    /* 3. Populate every register with a deterministic value (same RNG
     *    stream on both A and B). Skip mBASE (see file-level comment). */
    rng = seed ^ 0xA5A5A5A5u;
    for (int r = 0; r < SPU94_REG__COUNT; ++r) {
        uint32_t v32 = lcg(&rng);
        if ((spu94_reg_t)r == SPU94_REG_mBASE) {
            continue;  /* skip to keep buffer_address in the seeded range */
        }
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            int16_t v = (int16_t)(v32 & 0xFFFFu);
            spu94_set_reg_i16(A, (spu94_reg_t)r, v);
            spu94_set_reg_i16(B, (spu94_reg_t)r, v);
        } else {
            uint16_t v = (uint16_t)(v32 & 0xFFFFu);
            spu94_set_reg_u16(A, (spu94_reg_t)r, v);
            spu94_set_reg_u16(B, (spu94_reg_t)r, v);
        }
    }
    /* Flush TICK_LATCHED pending writes so both states' active register
     * values are identical when the stages read them. */
    spu94_apply_pending_writes(A);
    spu94_apply_pending_writes(B);

    /* 4. Sanity: A and B should have identical reg_values + work_buf
     *    before any reverb math runs. */
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(gA_work, gB_work, sizeof(gA_work),
        "Pre-reverb: A and B work_bufs must be byte-identical");
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(A->reg_values, B->reg_values,
        sizeof(A->reg_values),
        "Pre-reverb: A and B register files must be byte-identical");

    /* 5. Path A: full-body call. */
    uint32_t ba_before_A = A->buffer_address;
    spu94_reverb_body(A);
    uint32_t ba_after_A  = A->buffer_address;

    /* 6. Path B: reproduce reverb_body's exact stage sequence with
     *    snapshots read from B.  Must match the body's snapshot order
     *    and stage order exactly (see src/spu94/spu94_reverb.c). */
    const int16_t  vLIN_snap  = spu94_get_reg_i16(B, SPU94_REG_vLIN);
    const int16_t  vRIN_snap  = spu94_get_reg_i16(B, SPU94_REG_vRIN);
    const int16_t  vLOUT_snap = spu94_get_reg_i16(B, SPU94_REG_vLOUT);
    const int16_t  vROUT_snap = spu94_get_reg_i16(B, SPU94_REG_vROUT);
    const int16_t  vIIR_snap  = spu94_get_reg_i16(B, SPU94_REG_vIIR);
    const int16_t  vWALL_snap = spu94_get_reg_i16(B, SPU94_REG_vWALL);
    const int16_t  vC1        = spu94_get_reg_i16(B, SPU94_REG_vCOMB1);
    const int16_t  vC2        = spu94_get_reg_i16(B, SPU94_REG_vCOMB2);
    const int16_t  vC3        = spu94_get_reg_i16(B, SPU94_REG_vCOMB3);
    const int16_t  vC4        = spu94_get_reg_i16(B, SPU94_REG_vCOMB4);
    const int16_t  vA1        = spu94_get_reg_i16(B, SPU94_REG_vAPF1);
    const int16_t  vA2        = spu94_get_reg_i16(B, SPU94_REG_vAPF2);
    const uint16_t dA1_snap   = spu94_get_reg_u16(B, SPU94_REG_dAPF1);
    const uint16_t dA2_snap   = spu94_get_reg_u16(B, SPU94_REG_dAPF2);

    /* reverb_body hardcodes left_in=0, right_in=0 (no public mix-bus
     * feed yet — Phase 5 will populate). Path B must match. */
    const int16_t left_in  = 0;
    const int16_t right_in = 0;

    int32_t Lin_wide = 0, Rin_wide = 0;
    spu94_reverb_input_scale(B, left_in, right_in,
                             vLIN_snap, vRIN_snap,
                             &Lin_wide, &Rin_wide);

    int16_t Lin = 0, Rin = 0;
    int32_t overflow = 0;
    spu94_reverb_hard_clip(Lin_wide, Rin_wide, &Lin, &Rin, &overflow);
    B->overflow_magnitude += overflow;

    spu94_reverb_same_iir(B, Lin, Rin, vIIR_snap, vWALL_snap);
    spu94_reverb_diff_iir(B, Lin, Rin, vIIR_snap, vWALL_snap);

    int16_t Lout = 0, Rout = 0;
    spu94_reverb_comb(B, vC1, vC2, vC3, vC4, &Lout, &Rout);
    spu94_reverb_apf1(B, vA1, dA1_snap, &Lout, &Rout);
    spu94_reverb_apf2(B, vA2, dA2_snap, &Lout, &Rout);

    int32_t LeftOutput = 0, RightOutput = 0;
    spu94_reverb_output_scale(B, Lout, Rout, vLOUT_snap, vROUT_snap,
                              &LeftOutput, &RightOutput);
    /* ADR-Phase-6-G: spu94_reverb_body publishes its scaled wet output to
     * state->reverb_out_l / state->reverb_out_r so the FIR interpolator can
     * consume it. Path A called spu94_reverb_body and thus populated A's
     * mailbox; Path B ran the stage sequence manually. Pin the contract
     * by asserting Path A's mailbox equals the int16-cast of Path B's
     * LeftOutput / RightOutput. Without this assertion a future refactor
     * to reverb_body or reverb_output_scale could silently diverge. */
    TEST_ASSERT_EQUAL_INT16_MESSAGE(A->reverb_out_l, (int16_t)LeftOutput,
        "reverb_out_l mailbox must match stage-by-stage wet output (L)");
    TEST_ASSERT_EQUAL_INT16_MESSAGE(A->reverb_out_r, (int16_t)RightOutput,
        "reverb_out_r mailbox must match stage-by-stage wet output (R)");

    /* 7. Byte-identical work_buf across both paths. */
    TEST_ASSERT_EQUAL_MEMORY(gA_work, gB_work, sizeof(gA_work));

    /* 8. Every err_* accumulator matches. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_input_scale,  B->err_input_scale,  "err_input_scale");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_same_iir,     B->err_same_iir,     "err_same_iir");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_diff_iir,     B->err_diff_iir,     "err_diff_iir");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_comb,         B->err_comb,         "err_comb");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_apf1,         B->err_apf1,         "err_apf1");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_apf2,         B->err_apf2,         "err_apf2");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->err_output_scale, B->err_output_scale, "err_output_scale");
    TEST_ASSERT_EQUAL_INT32_MESSAGE(A->overflow_magnitude, B->overflow_magnitude,
                                    "overflow_magnitude");

    /* 9. Pitfall 8: reverb_body did not advance buffer_address. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ba_before_A, ba_after_A,
        "Pitfall 8: reverb_body must not advance buffer_address");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(A->buffer_address, B->buffer_address,
        "buffer_address must match across both paths");
}

/* Three seeded instantiations for coverage breadth. */
static void test_body_equivalence_seed_0x12345678(void) { run_body_equivalence(0x12345678u); }
static void test_body_equivalence_seed_0xCAFEBABE(void) { run_body_equivalence(0xCAFEBABEu); }
static void test_body_equivalence_seed_0xBADF00D (void) { run_body_equivalence(0x0BADF00Du); }

/* Pitfall 8 standalone check: reverb_body on a freshly initialized,
 * zero-register state must not mutate buffer_address. Complements the
 * seeded-state assertions inside run_body_equivalence. */
static void test_reverb_body_does_not_advance_buffer(void) {
    for (size_t i = 0; i < sizeof(gA_work); ++i) gA_work[i] = 0;
    spu94_state *s = spu94_init(gA_state, sizeof(gA_state),
                                gA_work,  sizeof(gA_work));
    TEST_ASSERT_NOT_NULL(s);
    uint32_t ba_before = s->buffer_address;
    spu94_reverb_body(s);
    TEST_ASSERT_EQUAL_UINT32(ba_before, s->buffer_address);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_body_equivalence_seed_0x12345678);
    RUN_TEST(test_body_equivalence_seed_0xCAFEBABE);
    RUN_TEST(test_body_equivalence_seed_0xBADF00D);
    RUN_TEST(test_reverb_body_does_not_advance_buffer);
    return UNITY_END();
}
