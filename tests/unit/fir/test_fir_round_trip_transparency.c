/* tests/unit/fir/test_fir_round_trip_transparency.c -- Phase 4 Plan 04 Task 1
 *
 * SC-2 round-trip transparency. Drive band-limited audio (1 kHz + 3 kHz +
 * 5 kHz mix, 2 s, Hann-fade-in/out) through spu94_fir_chain_step_reverb_
 * bypass and assert two oracles:
 *
 *   1. Bit-exact match vs Python reference (primary correctness oracle;
 *      any regression in the C chain diverges from the Python integer
 *      model and fails here).
 *
 *   2. Residual vs a Q15-scaled, latency-shifted copy of the input is
 *      below an empirically-derived threshold (secondary perceptual
 *      oracle; documents the "what this means" side -- the FIR chain is
 *      approximately a delay + scale, with residual error captured by
 *      ROUND_TRIP_MEASURED_RESIDUAL_LSB from the fixture generator).
 *
 * Fixture: tests/unit/fir/test_fir_round_trip_transparency_fixture.h
 *   generated via
 *     python3 tests/python/derive_fir_reference.py \
 *         --dump-band-limited-fixture tests/unit/fir/test_fir_round_trip_transparency_fixture.h
 *
 * The Q15 attenuation constant (ROUND_TRIP_ATTENUATION_Q15) and the
 * threshold (ROUND_TRIP_MAX_RESIDUAL_LSB) are derived *empirically* by
 * the Python generator. 04-RESEARCH 9.5 assumed a 0.977 cumulative gain
 * and an 80 LSB threshold; both assumed a full-rate two-filter cascade
 * rather than the 2:1 decimate/interpolate structure actually in use,
 * so the values are rederived in-band by the generator and pinned here.
 * See 04-04-SUMMARY.md "Deviations" for full rationale.
 */
#include "unity.h"
#include "../../../src/spu94/spu94_fir_internal.h"
#include <spu94/spu94.h>
#include <stdalign.h>
#include <stdint.h>
#include "test_fir_round_trip_transparency_fixture.h"

void setUp(void) {}
void tearDown(void) {}

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char g_state_buf[SPU94_STATE_SIZE_MAX];
static int16_t g_actual[BAND_LIMITED_FIXTURE_LEN];

static spu94_state *fresh_state_local(void) {
    spu94_state *s = spu94_init(g_state_buf, SPU94_STATE_SIZE_MAX, NULL, 0);
    TEST_ASSERT_NOT_NULL(s);
    return s;
}

static void test_round_trip_bit_exact_vs_python(void) {
    spu94_state *s = fresh_state_local();
    for (int n = 0; n < BAND_LIMITED_FIXTURE_LEN; ++n) {
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(
            s, BAND_LIMITED_FIXTURE[n], BAND_LIMITED_FIXTURE[n],
            &out_l, &out_r);
        /* L == R for mono input -- chain preserves channel independence. */
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_l, out_r,
            "chain_step_reverb_bypass L/R mirror broken for mono input");
        g_actual[n] = out_l;
    }
    TEST_ASSERT_EQUAL_INT16_ARRAY(BAND_LIMITED_EXPECTED, g_actual,
                                  BAND_LIMITED_FIXTURE_LEN);
}

/* Residual-threshold check. Re-runs the chain (independent state from
 * sub-test 1 so test ordering is irrelevant) and compares
 *   y[n + LATENCY] vs (x[n] * Q15_ATTENUATION) >> 15
 * over the central stable region. The threshold is 2x the measured
 * residual at fixture-generation time, with a floor of 80 LSB. */
static void test_round_trip_transparency_residual_below_threshold(void) {
    spu94_state *s = fresh_state_local();
    for (int n = 0; n < BAND_LIMITED_FIXTURE_LEN; ++n) {
        int16_t out_l = 0, out_r = 0;
        spu94_fir_chain_step_reverb_bypass(
            s, BAND_LIMITED_FIXTURE[n], BAND_LIMITED_FIXTURE[n],
            &out_l, &out_r);
        g_actual[n] = out_l;
    }

    /* Latency pin: use the value the fixture generator fit against. This
     * must agree with SPU94_LATENCY_SAMPLES (58u) as of Plan 03. The
     * assertion below is a belt+suspenders check; if the public macro
     * ever changes, the fixture must be regenerated. */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)ROUND_TRIP_LATENCY_SAMPLES,
                             SPU94_LATENCY_SAMPLES);

    const int latency = ROUND_TRIP_LATENCY_SAMPLES;
    const int start   = 4 * latency;
    const int end     = BAND_LIMITED_FIXTURE_LEN - 4 * latency;

    int32_t max_abs_residual = 0;
    for (int n = start; n < end; ++n) {
        int32_t scaled = ((int32_t)BAND_LIMITED_FIXTURE[n]
                          * (int32_t)ROUND_TRIP_ATTENUATION_Q15) >> 15;
        int32_t residual = (int32_t)g_actual[n + latency] - scaled;
        int32_t mag = residual < 0 ? -residual : residual;
        if (mag > max_abs_residual) {
            max_abs_residual = mag;
        }
    }
    TEST_ASSERT_LESS_OR_EQUAL_INT32_MESSAGE(
        (int32_t)ROUND_TRIP_MAX_RESIDUAL_LSB, max_abs_residual,
        "round-trip residual exceeded threshold -- regeneration needed "
        "or FIR chain regressed");
    /* Bonus: confirm we actually saw the measured residual (or lower).
     * If the C chain is bit-exact vs Python (sub-test 1), this must
     * equal ROUND_TRIP_MEASURED_RESIDUAL_LSB exactly. */
    TEST_ASSERT_EQUAL_INT32_MESSAGE(
        (int32_t)ROUND_TRIP_MEASURED_RESIDUAL_LSB, max_abs_residual,
        "residual does not match fixture-generation-time measurement; "
        "bit-exact oracle likely diverged");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_bit_exact_vs_python);
    RUN_TEST(test_round_trip_transparency_residual_below_threshold);
    return UNITY_END();
}
