/* tests/unit/voice/test_voice_tick.c
 * Phase 27: Unity unit tests for spu94_voice_tick.
 *
 * Tests cover:
 *   - Inactive voice produces silence
 *   - key_on resets state correctly
 *   - Pitch 0 treated as 0x1000
 *   - Pitch > 0x3FFF clamped to 0x3FFF (VOICE-03 / C7)
 *   - First tick decodes block and returns non-zero for non-silent input
 *   - Volume scaling: vol_l=0 -> out_l=0
 *   - NULL voice_ram / zero size -> safety (no crash, silence)
 */

#include "unity.h"
#include <spu94/spu94_voice.h>
#include <spu94/spu94_adsr.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Helper: encode a simple non-silent ADPCM block.
 * We use a single impulse (nibble +7 at shift 0) which produces a large
 * decoded sample value. */
static void make_test_block(uint8_t block[16]) {
    memset(block, 0, 16);
    /* byte 0: shift=0, filter=0 */
    block[0] = 0x00;
    /* byte 1: flags = 0x01 (END, no repeat — one-shot) */
    block[1] = 0x01;
    /* byte 2: low nibble = 7 (+7), high nibble = 0 */
    block[2] = 0x07;
    /* Remaining bytes are zero (produces samples 0 after the impulse fades) */
}

/* Helper: make a multi-block sample that doesn't end immediately.
 * No loop flags are set — the voice runs until it exceeds RAM bounds.
 * Phase 29 note: flag=0x00 on all blocks so loop-end logic is NOT triggered;
 * use dedicated loop test helpers for loop-flag behavior. */
static void make_long_sample(uint8_t *ram, uint32_t num_blocks) {
    for (uint32_t b = 0; b < num_blocks; b++) {
        memset(ram + b * 16, 0, 16);
        ram[b * 16 + 0] = 0x00; /* shift=0, filter=0 */
        ram[b * 16 + 1] = 0x00; /* no flags (continuous playback) */
        /* Non-silent: put some data in each block */
        ram[b * 16 + 2] = 0x37; /* nibbles +7, +3 */
        ram[b * 16 + 3] = 0x25; /* nibbles +5, +2 */
    }
}

/* ---------------------------------------------------------------
 * Test: inactive voice produces silence
 * --------------------------------------------------------------- */
void test_inactive_voice_produces_silence(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[16];
    make_test_block(ram);

    int16_t out_l = 999, out_r = 999;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);

    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
}

/* ---------------------------------------------------------------
 * Test: key_on resets state
 * --------------------------------------------------------------- */
void test_key_on_resets_state(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    TEST_ASSERT_EQUAL_UINT8(1, v.active);
    TEST_ASSERT_EQUAL_UINT16(0, v.pitch_counter);
    TEST_ASSERT_EQUAL_UINT8(0, v.has_block);
    TEST_ASSERT_EQUAL_UINT32(0, v.current_addr);
    TEST_ASSERT_EQUAL_UINT16(0x1000, v.pitch);
    TEST_ASSERT_EQUAL_INT16(0x7FFF, v.vol_l);
    TEST_ASSERT_EQUAL_INT16(0x7FFF, v.vol_r);
    TEST_ASSERT_EQUAL_INT16(0, v.gauss_ring[0]);
    TEST_ASSERT_EQUAL_INT16(0, v.gauss_ring[1]);
    TEST_ASSERT_EQUAL_INT16(0, v.gauss_ring[2]);
    TEST_ASSERT_EQUAL_INT16(0, v.gauss_ring[3]);
}

/* ---------------------------------------------------------------
 * Test: pitch 0 is treated as 0x1000
 * --------------------------------------------------------------- */
void test_pitch_zero_becomes_0x1000(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    spu94_voice_key_on(&v, 0, 0x0000, 0x7FFF, 0x7FFF);

    TEST_ASSERT_EQUAL_UINT16(0x1000, v.pitch);
}

/* ---------------------------------------------------------------
 * Test: pitch > 0x3FFF is clamped (VOICE-03 / C7)
 * --------------------------------------------------------------- */
void test_pitch_clamp_to_0x3FFF(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    spu94_voice_key_on(&v, 0, 0x4000, 0x7FFF, 0x7FFF);
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, v.pitch);

    spu94_voice_key_on(&v, 0, 0xFFFF, 0x7FFF, 0x7FFF);
    TEST_ASSERT_EQUAL_UINT16(0x3FFF, v.pitch);
}

/* ---------------------------------------------------------------
 * Test: first tick decodes block and produces non-zero output
 * --------------------------------------------------------------- */
void test_first_tick_decodes_and_outputs(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Create a 4-block sample so we don't run out immediately */
    uint8_t ram[64];
    make_long_sample(ram, 4);

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run several ticks to get past the initial zero ring buffer */
    int16_t out_l = 0, out_r = 0;
    int found_nonzero = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        if (out_l != 0 || out_r != 0) {
            found_nonzero = 1;
            break;
        }
    }

    /* After a few ticks, the ring should have non-zero samples and
     * Gaussian interpolation should produce non-zero output */
    TEST_ASSERT_TRUE(found_nonzero);
    TEST_ASSERT_EQUAL_UINT8(1, v.has_block);
}

/* ---------------------------------------------------------------
 * Test: vol_l=0 produces out_l=0, vol_r scales non-zero
 * --------------------------------------------------------------- */
void test_volume_scaling(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[64];
    make_long_sample(ram, 4);

    /* vol_l=0, vol_r=max */
    spu94_voice_key_on(&v, 0, 0x1000, 0x0000, 0x7FFF);

    int16_t out_l = 0, out_r = 0;
    /* Run enough ticks to get non-zero Gaussian output */
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    /* out_l should be 0 (volume=0), out_r may be non-zero */
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    /* We can't guarantee out_r is non-zero at every specific tick,
     * but let's run more ticks and check */
    int found_r_nonzero = 0;
    for (int i = 0; i < 20; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        TEST_ASSERT_EQUAL_INT16(0, out_l);
        if (out_r != 0) found_r_nonzero = 1;
    }
    TEST_ASSERT_TRUE(found_r_nonzero);
}

/* ---------------------------------------------------------------
 * Test: NULL voice_ram -> silence, no crash
 * --------------------------------------------------------------- */
void test_null_voice_ram_safety(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    int16_t out_l = 999, out_r = 999;
    spu94_voice_tick(&v, NULL, 0, &out_l, &out_r);

    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
}

/* ---------------------------------------------------------------
 * Test: zero-size voice_ram -> silence, no crash
 * --------------------------------------------------------------- */
void test_zero_size_ram_safety(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    uint8_t ram[16];
    make_test_block(ram);

    int16_t out_l = 999, out_r = 999;
    spu94_voice_tick(&v, ram, 0, &out_l, &out_r);

    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
}

/* ---------------------------------------------------------------
 * Test: key_off silences voice
 * --------------------------------------------------------------- */
void test_key_off_silences(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[64];
    make_long_sample(ram, 4);

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run a few ticks to get audio going */
    int16_t out_l, out_r;
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    spu94_voice_key_off(&v);
    TEST_ASSERT_EQUAL_UINT8(0, v.active);

    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
}

/* ---------------------------------------------------------------
 * Test: voice auto-stops when it runs out of RAM
 * --------------------------------------------------------------- */
void test_voice_stops_at_ram_boundary(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Single block sample: 16 bytes */
    uint8_t ram[16];
    make_test_block(ram);

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run enough ticks to consume the one block (28 samples at pitch 0x1000 = 28 ticks) */
    int16_t out_l, out_r;
    for (int i = 0; i < 40; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    /* Voice should have deactivated (ran past the single block) */
    TEST_ASSERT_EQUAL_UINT8(0, v.active);
}

/* ---------------------------------------------------------------
 * Phase 28 ADSR Integration Tests
 * --------------------------------------------------------------- */

/* Test: ADSR bypass (enabled=0) produces same output as Phase 27 */
void test_adsr_bypass_matches_phase27(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[64];
    make_long_sample(ram, 4);

    /* ADSR disabled (default from init) */
    TEST_ASSERT_EQUAL_UINT8(0, v.adsr.enabled);

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run ticks — output should match Phase 27 behavior (full amplitude) */
    int16_t out_l, out_r;
    int found_nonzero = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        if (out_l != 0) found_nonzero = 1;
    }
    TEST_ASSERT_TRUE(found_nonzero);
    TEST_ASSERT_EQUAL_UINT8(1, v.active);
}

/* Test: when ADSR phase is OFF and enabled=1, voice sets active=0 */
void test_adsr_off_silences_voice(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[64];
    make_long_sample(ram, 4);

    /* Enable ADSR, set to fast attack, then force phase to OFF */
    v.adsr.enabled = 1;
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Force ADSR to OFF state (simulating release completion) */
    v.adsr.phase = ADSR_OFF;
    v.adsr.level = 0;

    int16_t out_l = 999, out_r = 999;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);

    TEST_ASSERT_EQUAL_UINT8(0, v.active);
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
}

/* Test: key_off with ADSR enabled enters Release, not immediate silence */
void test_key_off_enters_release(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[64];
    make_long_sample(ram, 4);

    v.adsr.enabled = 1;
    v.adsr.attack_shift = 0;  /* fastest attack */
    v.adsr.attack_step = 0;
    v.adsr.attack_exp = 0;
    v.adsr.decay_shift = 0;
    v.adsr.sustain_level = 7;
    v.adsr.release_shift = 0;
    v.adsr.release_exp = 1;

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run a few ticks to get into sustain */
    int16_t out_l, out_r;
    for (int i = 0; i < 100; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    /* Key off should enter release, not set active=0 */
    spu94_voice_key_off(&v);
    TEST_ASSERT_EQUAL_UINT8(1, v.active);
    TEST_ASSERT_EQUAL(ADSR_RELEASE, v.adsr.phase);
}

/* Test: ADSR attack ramps output amplitude over time.
 * We verify that ADSR level increases over successive ticks during attack,
 * proving the envelope is not at unity from tick 1 (constant amplitude). */
void test_adsr_attack_ramps_output(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    uint8_t ram[128];
    make_long_sample(ram, 8);

    v.adsr.enabled = 1;
    v.adsr.attack_shift = 0;   /* Fastest attack: fires every tick */
    v.adsr.attack_step = 3;    /* Smallest step: (7-3)=4 << 11 = 8192 per tick */
    v.adsr.attack_exp = 0;
    v.adsr.decay_shift = 0;
    v.adsr.sustain_level = 15; /* target = 0x8000 -> clamped to 0x7FFF */
    v.adsr.sustain_shift = 31; /* sustain forever */
    v.adsr.release_shift = 0;
    v.adsr.release_exp = 1;

    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* After key_on, ADSR level starts at 0. First tick should produce
     * near-zero output (ADSR level is 0 before tick fires). */
    int16_t out_l, out_r;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    int16_t level_after_1 = v.adsr.level;

    /* Run a few more ticks — level should be increasing */
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    int16_t level_after_2 = v.adsr.level;

    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    int16_t level_after_3 = v.adsr.level;

    /* ADSR level must be increasing during attack */
    TEST_ASSERT_TRUE(level_after_2 > level_after_1);
    TEST_ASSERT_TRUE(level_after_3 > level_after_2);

    /* And the level should not be at max yet (step=8192, max=32767,
     * needs 4 ticks to reach max) — after 3 ticks: 3*8192 = 24576 */
    TEST_ASSERT_TRUE(v.adsr.level < 0x7FFF);
    TEST_ASSERT_EQUAL(ADSR_ATTACK, v.adsr.phase);
}

/* ---------------------------------------------------------------
 * Full pipeline smoke test: ADSR shape through spu94_voice_tick
 * with known parameters.
 *
 * This test drives a single voice with a synthetic DC-like ADPCM block
 * through attack, sustain, and release, asserting the envelope shape
 * at key points.
 * --------------------------------------------------------------- */
void test_adsr_full_pipeline_attack_sustain_release(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Create a multi-block sample with maximum-amplitude content.
     * Use shift=0, filter=0, all nibbles=7 -> each decoded sample is +7.
     * The Gaussian ring stabilizes at ~7 after a few ticks. */
    uint8_t ram[512];
    memset(ram, 0, sizeof(ram));
    uint32_t num_blocks = sizeof(ram) / 16;
    for (uint32_t b = 0; b < num_blocks; b++) {
        ram[b * 16 + 0] = 0x00;  /* shift=0, filter=0 */
        ram[b * 16 + 1] = 0x00;  /* no flags (continuous) */
        /* Fill all 14 data bytes with 0x77 (nibbles +7, +7) */
        for (int j = 2; j < 16; j++) {
            ram[b * 16 + j] = 0x77;
        }
    }
    /* No end flag — sample runs until RAM bounds check.
     * Phase 29: setting 0x01 would trigger one-shot termination. */

    /* Configure voice */
    v.adsr.enabled = 1;
    v.adsr.attack_shift = 0;    /* fastest attack: fires every tick */
    v.adsr.attack_step = 3;     /* step = (7-3) << 11 = 8192 per tick */
    v.adsr.attack_exp = 1;      /* fake exponential above 0x6000 */
    v.adsr.decay_shift = 0;     /* fast decay */
    v.adsr.sustain_level = 7;   /* target = 8 * 0x800 = 0x4000 */
    v.adsr.sustain_shift = 31;  /* sustain holds forever */
    v.adsr.sustain_step = 0;
    v.adsr.sustain_exp = 0;
    v.adsr.sustain_dir = 0;
    v.adsr.release_shift = 0;   /* fastest release */
    v.adsr.release_exp = 1;

    /* pitch=0x1000 = 1:1 playback, vol=max */
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* --- ATTACK PHASE ---
     * Run several ticks. Output should be increasing as ADSR ramps up. */
    int16_t out_l, out_r;
    int16_t prev_abs = 0;
    int attack_increasing = 0;

    /* Warm up the Gaussian ring (first few ticks may be 0 from ring cold start) */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    /* Now measure — the ring should have non-zero samples.
     * Track whether output generally increases. */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        int16_t abs_out = out_l > 0 ? out_l : (int16_t)(-out_l);
        if (abs_out > prev_abs) attack_increasing = 1;
        prev_abs = abs_out;
    }
    /* ADSR level is increasing during attack (or already reached sustain) */
    TEST_ASSERT_TRUE(attack_increasing || v.adsr.phase >= ADSR_DECAY);

    /* --- Run until SUSTAIN ---
     * The ADSR should transition attack->decay->sustain */
    for (int i = 0; i < 200; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        if (v.adsr.phase == ADSR_SUSTAIN) break;
    }
    TEST_ASSERT_EQUAL(ADSR_SUSTAIN, v.adsr.phase);

    /* Sustain level should be at target 0x4000 */
    TEST_ASSERT_EQUAL_INT16(0x4000, v.adsr.level);

    /* Output should be non-zero (sustained) */
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    TEST_ASSERT_TRUE(out_l != 0 || out_r != 0);

    /* Record sustain-level output for later comparison */
    int16_t sustain_out = out_l > 0 ? out_l : (int16_t)(-out_l);
    TEST_ASSERT_TRUE(sustain_out > 0);

    /* --- RELEASE PHASE ---
     * Key off. Output should decrease. */
    spu94_voice_key_off(&v);
    TEST_ASSERT_EQUAL(ADSR_RELEASE, v.adsr.phase);
    TEST_ASSERT_EQUAL_UINT8(1, v.active);  /* still active during release */

    /* Run a few ticks — output should be decreasing */
    int16_t release_out_first = 0;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    release_out_first = out_l > 0 ? out_l : (int16_t)(-out_l);

    int16_t release_out_later = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }
    release_out_later = out_l > 0 ? out_l : (int16_t)(-out_l);

    /* Later output should be less than or equal to first release output
     * (decreasing amplitude during release) */
    TEST_ASSERT_TRUE(release_out_later <= release_out_first);

    /* --- Run until voice goes silent ---
     * ADSR should reach OFF, voice active=0 */
    for (int i = 0; i < 10000; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
        if (v.active == 0) break;
    }
    TEST_ASSERT_EQUAL_UINT8(0, v.active);
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    TEST_ASSERT_EQUAL_INT16(0, out_r);
    TEST_ASSERT_EQUAL(ADSR_OFF, v.adsr.phase);
}

/* ---------------------------------------------------------------
 * Phase 29: Loop Mechanics Tests (LOOP-01..05)
 * --------------------------------------------------------------- */

/* Test A: Loop-Start latch — block with LOOP_START flag causes loop_addr
 * and filter state snapshot to be latched. (LOOP-02, C4, C5) */
void test_loop_start_latches_address(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Build 2-block stream:
     * Block 0 (offset 0): flags = LOOP_START (bit 2)
     * Block 1 (offset 16): flags = 0 (no flags) */
    uint8_t ram[32];
    memset(ram, 0, sizeof(ram));
    /* Block 0: shift=0, filter=0, flag=LOOP_START, silence samples */
    ram[0]  = 0x00;
    ram[1]  = SPU94_VAG_FLAG_LOOP_START;  /* bit 2 = 0x04 */
    /* Block 1: shift=0, filter=0, no flags, silence */
    ram[16] = 0x00;
    ram[17] = 0x00;

    /* key_on with ADSR disabled (bypass), start at offset 0 */
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Tick once — at pitch 0x1000, the first tick triggers decode of block 0 */
    int16_t out_l, out_r;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);

    /* After block 0 is decoded, loop_addr should be latched to address of block 0 */
    TEST_ASSERT_EQUAL_UINT32(0, v.loop_addr);

    /* Filter state snapshot should match adpcm_state after decode.
     * With shift=0, filter=0, all-zero samples: old=0, older=0 */
    TEST_ASSERT_EQUAL_INT16(0, v.loop_adpcm_old);
    TEST_ASSERT_EQUAL_INT16(0, v.loop_adpcm_older);
}

/* Test B: Loop-End+Repeat — jump to loop_addr, restore filter state,
 * remain active, set ENDX. (LOOP-01, LOOP-03, LOOP-05, C5) */
void test_loop_end_repeat_jumps_to_loop_addr(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Build 2-block stream:
     * Block 0 (offset 0): flags = LOOP_START
     * Block 1 (offset 16): flags = LOOP_END | LOOP_REPEAT */
    uint8_t ram[32];
    memset(ram, 0, sizeof(ram));
    ram[0]  = 0x00;
    ram[1]  = SPU94_VAG_FLAG_LOOP_START;             /* 0x04 */
    ram[16] = 0x00;
    ram[17] = SPU94_VAG_FLAG_END | SPU94_VAG_FLAG_LOOP_REPEAT;  /* 0x01 | 0x02 = 0x03 */

    /* key_on with ADSR disabled, start at offset 0 */
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Tick through block 0 (28 samples at pitch 0x1000 = 28 ticks decodes block 0,
     * then 1 more tick triggers decode of block 1) */
    int16_t out_l, out_r;
    for (int i = 0; i < 29; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);
    }

    /* After block 1 is decoded: current_addr should have jumped to loop_addr (0) */
    TEST_ASSERT_EQUAL_UINT32(0, v.current_addr);

    /* Voice should still be active */
    TEST_ASSERT_EQUAL_UINT8(1, v.active);

    /* ENDX should be set (LOOP-05: set on Loop-End regardless of repeat) */
    TEST_ASSERT_EQUAL_UINT8(1, spu94_voice_get_endx(&v));
}

/* Test C: One-shot — Loop-End without Repeat silences voice and sets ENDX.
 * (LOOP-04, LOOP-05) */
void test_one_shot_silences_voice(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Build 1-block stream:
     * Block 0 (offset 0): flags = LOOP_END only (bit 0 set, bit 1 clear) */
    uint8_t ram[16];
    memset(ram, 0, sizeof(ram));
    ram[0] = 0x00;
    ram[1] = SPU94_VAG_FLAG_END;  /* 0x01 — loop end, no repeat */

    /* key_on with ADSR disabled */
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Tick once — triggers decode of block 0 with LOOP_END flag */
    int16_t out_l, out_r;
    spu94_voice_tick(&v, ram, sizeof(ram), &out_l, &out_r);

    /* ENDX should be set immediately on flag parse */
    TEST_ASSERT_EQUAL_UINT8(1, spu94_voice_get_endx(&v));

    /* With ADSR disabled, active should be 0 immediately */
    TEST_ASSERT_EQUAL_UINT8(0, v.active);
}

/* Test D: ENDX cleared by KON (LOOP-05, M3) */
void test_endx_cleared_by_key_on(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    /* Manually set endx = 1 */
    v.endx = 1;
    TEST_ASSERT_EQUAL_UINT8(1, spu94_voice_get_endx(&v));

    /* key_on should clear endx */
    uint8_t ram[32];
    memset(ram, 0, sizeof(ram));
    spu94_voice_key_on(&v, 0, 0x1000, 0x7FFF, 0x7FFF);

    TEST_ASSERT_EQUAL_UINT8(0, spu94_voice_get_endx(&v));
}

/* ---------------------------------------------------------------
 * Phase 30: Mixer API Tests (MIX-01 through MIX-06)
 * --------------------------------------------------------------- */

/* We use a file-scope static mixer because the struct is ~533 KB
 * (too large for the stack in some environments). */
static spu94_voice_mixer_t s_test_mixer;

/* Test: mixer init zeroes all voices and control fields */
void test_mixer_init_zeroes_all(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    /* All 24 voices should be inactive */
    for (int i = 0; i < 24; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.voices[i].active);
    }
    /* Pending masks should be zero */
    TEST_ASSERT_EQUAL_UINT32(0, s_test_mixer.pending_kon);
    TEST_ASSERT_EQUAL_UINT32(0, s_test_mixer.pending_koff);
    /* EON flags should be zero */
    TEST_ASSERT_EQUAL_UINT32(0, s_test_mixer.eon_flags);
    /* Master volume should be zero */
    TEST_ASSERT_EQUAL_INT16(0, s_test_mixer.master_vol_l);
    TEST_ASSERT_EQUAL_INT16(0, s_test_mixer.master_vol_r);
    /* Disabled by default */
    TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.enabled);
}

/* Test: key_on sets pending_kon bit (C8/MIX-04) */
void test_mixer_key_on_sets_pending(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    spu94_result_t rc = spu94_voice_mixer_key_on(&s_test_mixer, 5,
        0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    /* Bit 5 should be set in pending_kon */
    TEST_ASSERT_TRUE(s_test_mixer.pending_kon & (1u << 5));
    /* Voice should NOT be active yet (C8: deferred to next tick) */
    TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.voices[5].active);
}

/* Test: key_off sets pending_koff bit (C8/MIX-04) */
void test_mixer_key_off_sets_pending(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    spu94_result_t rc = spu94_voice_mixer_key_off(&s_test_mixer, 3);

    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    /* Bit 3 should be set in pending_koff */
    TEST_ASSERT_TRUE(s_test_mixer.pending_koff & (1u << 3));
}

/* Test: KON wins over KOFF on same voice (C8) */
void test_mixer_key_on_wins_over_koff(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    /* Set koff first */
    spu94_voice_mixer_key_off(&s_test_mixer, 7);
    TEST_ASSERT_TRUE(s_test_mixer.pending_koff & (1u << 7));

    /* Then key_on same voice — koff bit must clear */
    spu94_voice_mixer_key_on(&s_test_mixer, 7,
        0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    TEST_ASSERT_TRUE(s_test_mixer.pending_kon & (1u << 7));
    TEST_ASSERT_FALSE(s_test_mixer.pending_koff & (1u << 7));
}

/* Test: set_eon sets and clears eon_flags bit (MIX-02) */
void test_mixer_eon_set_and_clear(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    /* Set EON for voice 7 */
    spu94_result_t rc = spu94_voice_mixer_set_eon(&s_test_mixer, 7, 1);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    TEST_ASSERT_TRUE(s_test_mixer.eon_flags & (1u << 7));

    /* Clear EON for voice 7 */
    rc = spu94_voice_mixer_set_eon(&s_test_mixer, 7, 0);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    TEST_ASSERT_FALSE(s_test_mixer.eon_flags & (1u << 7));
}

/* Test: load_sample bounds check (T-30-01) */
void test_mixer_load_sample_bounds(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    uint8_t data[16];
    memset(data, 0xAB, sizeof(data));

    /* Load at addr=0, size=16: should succeed */
    spu94_result_t rc = spu94_voice_mixer_load_sample(&s_test_mixer, 0, data, 16);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    /* Verify data was written */
    TEST_ASSERT_EQUAL_UINT8(0xAB, s_test_mixer.voice_ram[0]);

    /* Load at addr=SPU94_SPU_RAM_BYTES: should fail (T-30-01) */
    rc = spu94_voice_mixer_load_sample(&s_test_mixer, SPU94_SPU_RAM_BYTES, data, 1);
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG, rc);

    /* Load that would overflow: addr + size > SPU94_SPU_RAM_BYTES */
    rc = spu94_voice_mixer_load_sample(&s_test_mixer,
        SPU94_SPU_RAM_BYTES - 8, data, 16);
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG, rc);

    /* NULL source should fail */
    rc = spu94_voice_mixer_load_sample(&s_test_mixer, 0, NULL, 16);
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG, rc);
}

/* Test: out-of-range voice_idx returns SPU94_INVALID_ARG (T-30-02) */
void test_mixer_invalid_voice_idx(void) {
    spu94_voice_mixer_init(&s_test_mixer);

    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
        spu94_voice_mixer_key_on(&s_test_mixer, 24, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL));
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
        spu94_voice_mixer_key_on(&s_test_mixer, -1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL));
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
        spu94_voice_mixer_key_off(&s_test_mixer, 24));
    TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
        spu94_voice_mixer_set_eon(&s_test_mixer, 24, 1));
}

/* ---------------------------------------------------------------
 * Main
 * --------------------------------------------------------------- */
int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_inactive_voice_produces_silence);
    RUN_TEST(test_key_on_resets_state);
    RUN_TEST(test_pitch_zero_becomes_0x1000);
    RUN_TEST(test_pitch_clamp_to_0x3FFF);
    RUN_TEST(test_first_tick_decodes_and_outputs);
    RUN_TEST(test_volume_scaling);
    RUN_TEST(test_null_voice_ram_safety);
    RUN_TEST(test_zero_size_ram_safety);
    RUN_TEST(test_key_off_silences);
    RUN_TEST(test_voice_stops_at_ram_boundary);
    /* Phase 28 ADSR integration tests */
    RUN_TEST(test_adsr_bypass_matches_phase27);
    RUN_TEST(test_adsr_off_silences_voice);
    RUN_TEST(test_key_off_enters_release);
    RUN_TEST(test_adsr_attack_ramps_output);
    RUN_TEST(test_adsr_full_pipeline_attack_sustain_release);
    /* Phase 29 Loop Mechanics tests */
    RUN_TEST(test_loop_start_latches_address);
    RUN_TEST(test_loop_end_repeat_jumps_to_loop_addr);
    RUN_TEST(test_one_shot_silences_voice);
    RUN_TEST(test_endx_cleared_by_key_on);
    /* Phase 30 Mixer API tests */
    RUN_TEST(test_mixer_init_zeroes_all);
    RUN_TEST(test_mixer_key_on_sets_pending);
    RUN_TEST(test_mixer_key_off_sets_pending);
    RUN_TEST(test_mixer_key_on_wins_over_koff);
    RUN_TEST(test_mixer_eon_set_and_clear);
    RUN_TEST(test_mixer_load_sample_bounds);
    RUN_TEST(test_mixer_invalid_voice_idx);
    return UNITY_END();
}
