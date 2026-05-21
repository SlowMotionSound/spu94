/* tests/unit/preset/test_preset_golden_roundtrip.c -- Phase 15 Plan 01
 *
 * Integration-level golden round-trip test: prove that preset save/load
 * produces bit-identical AUDIO output, not just matching state.
 *
 * Two configurations:
 *   1. Factory Hall preset (default mixer wiring, DAC off)
 *   2. Custom state: Delay base + non-default mixer faders + flipped DAC toggles
 *
 * Each test: configure engine A, drive deterministic noise through spu94_process
 * + spu94_flush, save state, load into fresh engine B, drive same noise,
 * compare output sample-by-sample. Bit-identical or fail.
 *
 * This is the capstone verification for v1.4 Preset System. Phase 13 proved
 * state round-trip (registers, mixer, DAC match after save/load). This phase
 * proves that matching state produces matching AUDIO through the DSP pipeline.
 */

#include "unity.h"
#include <spu94/spu94.h>
#include <spu94/spu94_register_facade.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* abs */

/* ----------------------------------------------------------------------- */
/* Test constants                                                          */
/* ----------------------------------------------------------------------- */

#define FEED_SAMPLES  2048
#define FLUSH_SAMPLES 1024
#define TOTAL_SAMPLES (FEED_SAMPLES + FLUSH_SAMPLES)

/* ----------------------------------------------------------------------- */
/* Two independent engine state sets                                       */
/* ----------------------------------------------------------------------- */

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_a[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_a[256 * 1024];   /* 256 KB reverb scratch */
static spu94_state *engine_a = NULL;

static alignas(SPU94_STATE_ALIGN_MAX) unsigned char state_buf_b[SPU94_STATE_SIZE_MAX];
static unsigned char work_buf_b[256 * 1024];
static spu94_state *engine_b = NULL;

static char preset_buf[SPU94_PRESET_BUF_SIZE];

/* Output buffers */
static int16_t out_a_L[TOTAL_SAMPLES], out_a_R[TOTAL_SAMPLES];
static int16_t out_b_L[TOTAL_SAMPLES], out_b_R[TOTAL_SAMPLES];

/* ----------------------------------------------------------------------- */
/* Deterministic noise generator (exact LCG from test_preset_nonzero_tail) */
/* ----------------------------------------------------------------------- */

static uint32_t lcg_seed;
static int16_t noise_sample(void) {
    lcg_seed = lcg_seed * 1103515245u + 12345u;
    const int32_t v = (int32_t)(lcg_seed >> 16) & 0xFFFF;
    return (int16_t)(v - 32767);
}
static void reseed(void) { lcg_seed = 0x00C0FFEE; }

/* ----------------------------------------------------------------------- */
/* Helper: drive deterministic noise and collect output                     */
/* ----------------------------------------------------------------------- */

static void drive_and_collect(spu94_state *engine,
                              int16_t *out_L, int16_t *out_R,
                              int feed_samples, int flush_samples)
{
    int16_t Lin, Rin, Lout, Rout;

    /* Reseed so both engines get identical input sequences */
    reseed();

    /* Feed phase: deterministic noise through spu94_process */
    for (int i = 0; i < feed_samples; i++) {
        Lin = noise_sample();
        Rin = noise_sample();
        spu94_process(engine, &Lin, &Rin, &Lout, &Rout, 1);
        out_L[i] = Lout;
        out_R[i] = Rout;
    }

    /* Flush phase: drain reverb tail */
    for (int i = 0; i < flush_samples; i++) {
        spu94_flush(engine, &Lout, &Rout, 1);
        out_L[feed_samples + i] = Lout;
        out_R[feed_samples + i] = Rout;
    }
}

/* ----------------------------------------------------------------------- */
/* Helper: configure engine A with factory Hall preset                      */
/* ----------------------------------------------------------------------- */

static void configure_engine_a_factory(void)
{
    spu94_reset(engine_a);
    spu94_load_preset(engine_a, SPU94_PRESET_HALL);
    spu94_set_vLOUT(engine_a, (int16_t)0x7FFF);
    spu94_set_vROUT(engine_a, (int16_t)0x7FFF);
    spu94_set_input_gain(engine_a, 0x7FFF);
    spu94_set_dry_fader(engine_a, 0x7FFF);
    spu94_set_reverb_fader(engine_a, 0x7FFF);
    spu94_set_dry_send(engine_a, 0x7FFF);
    spu94_tick(engine_a);  /* commit pending registers */
}

/* ----------------------------------------------------------------------- */
/* Helper: configure engine A with custom Delay + non-default everything   */
/* ----------------------------------------------------------------------- */

static void configure_engine_a_custom(void)
{
    spu94_reset(engine_a);
    spu94_load_preset(engine_a, SPU94_PRESET_DELAY);
    spu94_set_vLOUT(engine_a, (int16_t)0x7FFF);
    spu94_set_vROUT(engine_a, (int16_t)0x7FFF);

    /* Custom mixer faders (all non-default) */
    spu94_set_input_gain(engine_a, 0x4000);
    spu94_set_dry_fader(engine_a, 0x2000);
    spu94_set_adpcm_fader(engine_a, 0x1000);
    spu94_set_dry_send(engine_a, 0x3000);
    spu94_set_adpcm_send(engine_a, 0x0800);
    spu94_set_reverb_fader(engine_a, 0x6000);
    spu94_set_latency_comp(engine_a, 0);

    /* Flipped DAC toggles (all non-default) */
    spu94_set_dac_enabled(engine_a, 1);
    spu94_set_dac_fir_enabled(engine_a, 0);
    spu94_set_dac_noise_enabled(engine_a, 1);
    spu94_set_dac_true_oversample(engine_a, 0);

    spu94_tick(engine_a);  /* commit pending registers */
}

/* ----------------------------------------------------------------------- */
/* Unity setUp / tearDown                                                  */
/* ----------------------------------------------------------------------- */

void setUp(void) {
    engine_a = spu94_init(state_buf_a, sizeof state_buf_a,
                          work_buf_a, sizeof work_buf_a);
    TEST_ASSERT_NOT_NULL(engine_a);

    engine_b = spu94_init(state_buf_b, sizeof state_buf_b,
                          work_buf_b, sizeof work_buf_b);
    TEST_ASSERT_NOT_NULL(engine_b);
}

void tearDown(void) {
    engine_a = NULL;
    engine_b = NULL;
}

/* ----------------------------------------------------------------------- */
/* Test 1: Factory Hall preset golden round-trip                           */
/* ----------------------------------------------------------------------- */

static void test_golden_roundtrip_factory_hall(void)
{
    /* Configure and drive engine A */
    configure_engine_a_factory();
    drive_and_collect(engine_a, out_a_L, out_a_R, FEED_SAMPLES, FLUSH_SAMPLES);

    /* Sanity check: engine A actually produced audio */
    int32_t max_abs = 0;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        if (abs((int)out_a_L[i]) > max_abs) max_abs = abs((int)out_a_L[i]);
        if (abs((int)out_a_R[i]) > max_abs) max_abs = abs((int)out_a_R[i]);
    }
    TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(0, max_abs,
        "Engine A (Hall) produced silent output -- sanity check failed");

    /* Save engine A state */
    int saved = spu94_preset_save(engine_a, "GoldenHall", "factory roundtrip",
                                  preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, saved,
        "spu94_preset_save failed for Hall configuration");

    /* Load into fresh engine B */
    spu94_reset(engine_b);
    spu94_result_t lr = spu94_preset_load(engine_b, preset_buf, (size_t)saved);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK, (int)lr,
        "spu94_preset_load failed for Hall round-trip");
    spu94_tick(engine_b);  /* commit loaded registers */

    /* Drive engine B with identical noise */
    drive_and_collect(engine_b, out_b_L, out_b_R, FEED_SAMPLES, FLUSH_SAMPLES);

    /* Compare sample-by-sample: must be bit-identical */
    char msg[128];
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        snprintf(msg, sizeof msg,
                 "L sample %d: engine_a=%d engine_b=%d", i,
                 out_a_L[i], out_b_L[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_a_L[i], out_b_L[i], msg);
        snprintf(msg, sizeof msg,
                 "R sample %d: engine_a=%d engine_b=%d", i,
                 out_a_R[i], out_b_R[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_a_R[i], out_b_R[i], msg);
    }
}

/* ----------------------------------------------------------------------- */
/* Test 2: Custom Delay + non-default mixer/DAC golden round-trip          */
/* ----------------------------------------------------------------------- */

static void test_golden_roundtrip_custom_state(void)
{
    /* Configure and drive engine A */
    configure_engine_a_custom();
    drive_and_collect(engine_a, out_a_L, out_a_R, FEED_SAMPLES, FLUSH_SAMPLES);

    /* Sanity check: engine A actually produced audio */
    int32_t max_abs = 0;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        if (abs((int)out_a_L[i]) > max_abs) max_abs = abs((int)out_a_L[i]);
        if (abs((int)out_a_R[i]) > max_abs) max_abs = abs((int)out_a_R[i]);
    }
    TEST_ASSERT_GREATER_THAN_INT32_MESSAGE(0, max_abs,
        "Engine A (Custom) produced silent output -- sanity check failed");

    /* Save engine A state */
    int saved = spu94_preset_save(engine_a, "GoldenCustom", "custom roundtrip",
                                  preset_buf, sizeof preset_buf);
    TEST_ASSERT_GREATER_THAN_MESSAGE(0, saved,
        "spu94_preset_save failed for custom configuration");

    /* Load into fresh engine B */
    spu94_reset(engine_b);
    spu94_result_t lr = spu94_preset_load(engine_b, preset_buf, (size_t)saved);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)SPU94_OK, (int)lr,
        "spu94_preset_load failed for custom round-trip");
    spu94_tick(engine_b);  /* commit loaded registers */

    /* Drive engine B with identical noise */
    drive_and_collect(engine_b, out_b_L, out_b_R, FEED_SAMPLES, FLUSH_SAMPLES);

    /* Compare sample-by-sample: must be bit-identical */
    char msg[128];
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        snprintf(msg, sizeof msg,
                 "L sample %d: engine_a=%d engine_b=%d", i,
                 out_a_L[i], out_b_L[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_a_L[i], out_b_L[i], msg);
        snprintf(msg, sizeof msg,
                 "R sample %d: engine_a=%d engine_b=%d", i,
                 out_a_R[i], out_b_R[i]);
        TEST_ASSERT_EQUAL_INT16_MESSAGE(out_a_R[i], out_b_R[i], msg);
    }
}

/* ----------------------------------------------------------------------- */
/* main                                                                    */
/* ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_golden_roundtrip_factory_hall);
    RUN_TEST(test_golden_roundtrip_custom_state);
    return UNITY_END();
}
