/* tests/unit/dac_noise/test_dac_noise_lfsr.c -- Phase 6 Plan 02 Task 2
 *
 * LFSR behavioral tests: non-zero init, non-zero output, absorbing-state
 * proof, no short period, deterministic reproducibility.
 */
#include "unity.h"
#include <spu94/spu94_dac_noise.h>

void setUp(void) {}
void tearDown(void) {}

/* After spu94_dac_noise_init, state->lfsr must be non-zero.
 * Zero is an absorbing state for Galois LFSRs. */
static void test_init_sets_nonzero_lfsr(void) {
    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st);
    TEST_ASSERT_NOT_EQUAL_UINT32(0u, st.lfsr);
}

/* Run 1000 steps; at least one output must be non-zero.
 * If the LFSR is working, this is a near-certainty. */
static void test_step_produces_nonzero_output(void) {
    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st);

    int any_nonzero = 0;
    for (int i = 0; i < 1000; i++) {
        int16_t out = spu94_dac_noise_step(&st);
        if (out != 0) {
            any_nonzero = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(any_nonzero,
        "Expected at least one non-zero output in 1000 steps");
}

/* Deliberately bypass init by setting lfsr=0. After the 2nd-order HP
 * filter transient settles (takes ~3 samples because x[n] is constant
 * at -64 and the HP difference equation converges), all outputs must
 * be zero, proving why spu94_dac_noise_init is required.
 *
 * The first few samples are non-zero due to the HP filter's step response
 * to the constant x=-64 input. Once x[n]=x[n-1]=x[n-2], the 2nd-order
 * difference y = x - 2*x + x = 0. */
static void test_zero_lfsr_produces_silence(void) {
    spu94_dac_noise_state st;
    st.lfsr = 0;
    st.x_prev = 0;
    st.x_prev2 = 0;

    /* Let the HP filter transient settle */
    for (int i = 0; i < 10; i++) {
        (void)spu94_dac_noise_step(&st);
    }

    /* After settling, LFSR=0 produces constant x, so HP output is zero */
    for (int i = 0; i < 100; i++) {
        int16_t out = spu94_dac_noise_step(&st);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(0, out,
            "Zero LFSR must produce silence after HP filter settles");
    }
}

/* Run 100,000 steps. The LFSR state must never return to its initial seed
 * value within those steps. This catches short-period polynomials without
 * running the full 2^32 cycle. */
static void test_lfsr_no_short_period(void) {
    spu94_dac_noise_state st;
    spu94_dac_noise_init(&st);

    uint32_t seed = st.lfsr;

    /* Step once to move away from the seed */
    spu94_dac_noise_step(&st);

    for (int i = 1; i < 100000; i++) {
        spu94_dac_noise_step(&st);
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(seed, st.lfsr,
            "LFSR returned to seed -- short period detected");
    }
}

/* Two identically initialized states must produce identical output
 * sequences for 100 steps. */
static void test_deterministic(void) {
    spu94_dac_noise_state st_a, st_b;
    spu94_dac_noise_init(&st_a);
    spu94_dac_noise_init(&st_b);

    for (int i = 0; i < 100; i++) {
        int16_t a = spu94_dac_noise_step(&st_a);
        int16_t b = spu94_dac_noise_step(&st_b);
        TEST_ASSERT_EQUAL_INT16(a, b);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_nonzero_lfsr);
    RUN_TEST(test_step_produces_nonzero_output);
    RUN_TEST(test_zero_lfsr_produces_silence);
    RUN_TEST(test_lfsr_no_short_period);
    RUN_TEST(test_deterministic);
    return UNITY_END();
}
