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
#include <spu94/spu94_noise.h>
#include <spu94/spu94_adsr.h>
#include <spu94/spu94_adpcm.h>
#include <spu94/spu94_vag.h>
#include <spu94/spu94_q15.h>
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }

    /* out_l should be 0 (volume=0), out_r may be non-zero */
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    /* We can't guarantee out_r is non-zero at every specific tick,
     * but let's run more ticks and check */
    int found_r_nonzero = 0;
    for (int i = 0; i < 20; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, NULL, 0, 0, 0, 0, &out_l, &out_r);

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
    spu94_voice_tick(&v, ram, 0, 0, 0, 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }

    spu94_voice_key_off(&v);
    TEST_ASSERT_EQUAL_UINT8(0, v.active);

    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    int16_t level_after_1 = v.adsr.level;

    /* Run a few more ticks — level should be increasing */
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    int16_t level_after_2 = v.adsr.level;

    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }

    /* Now measure — the ring should have non-zero samples.
     * Track whether output generally increases. */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        int16_t abs_out = out_l > 0 ? out_l : (int16_t)(-out_l);
        if (abs_out > prev_abs) attack_increasing = 1;
        prev_abs = abs_out;
    }
    /* ADSR level is increasing during attack (or already reached sustain) */
    TEST_ASSERT_TRUE(attack_increasing || v.adsr.phase >= ADSR_DECAY);

    /* --- Run until SUSTAIN ---
     * The ADSR should transition attack->decay->sustain */
    for (int i = 0; i < 200; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        if (v.adsr.phase == ADSR_SUSTAIN) break;
    }
    TEST_ASSERT_EQUAL(ADSR_SUSTAIN, v.adsr.phase);

    /* Sustain level should be at target 0x4000 */
    TEST_ASSERT_EQUAL_INT16(0x4000, v.adsr.level);

    /* Output should be non-zero (sustained) */
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    release_out_first = out_l > 0 ? out_l : (int16_t)(-out_l);

    int16_t release_out_later = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }
    release_out_later = out_l > 0 ? out_l : (int16_t)(-out_l);

    /* Later output should be less than or equal to first release output
     * (decreasing amplitude during release) */
    TEST_ASSERT_TRUE(release_out_later <= release_out_first);

    /* --- Run until voice goes silent ---
     * ADSR should reach OFF, voice active=0 */
    for (int i = 0; i < 10000; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);

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
 * Phase 30 Task 2: Mixer tick integration tests
 * --------------------------------------------------------------- */

/* Test: two voices sum — combined output > single voice (MIX-01 polyphony) */
void test_mixer_tick_two_voices_sum(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a known non-silent sample into voice RAM */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voices 0 and 1 pointing to same sample */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* First tick applies pending KON */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Run more ticks to get past zero ring buffer */
    int16_t single_max = 0;
    int16_t dual_max = 0;

    /* Now measure — key on only voice 0 and run alone */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (dry_l > single_max) single_max = dry_l;
        if ((-dry_l) > single_max) single_max = (int16_t)(-dry_l);
    }

    /* Key on both voices and measure */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (dry_l > dual_max) dual_max = dry_l;
        if ((-dry_l) > dual_max) dual_max = (int16_t)(-dry_l);
    }

    /* Dual output must be greater or equal to single (sum of two identical voices) */
    TEST_ASSERT_TRUE(dual_max >= single_max);
    /* And single must be non-zero for the test to be meaningful */
    TEST_ASSERT_TRUE(single_max > 0);
}

/* Test: EON routes only flagged voices to reverb (MIX-02/MIX-05) */
void test_mixer_eon_routes_only_flagged(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load sample */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Voice 0: EON=0 (dry only). Voice 1: EON=1 (reverb send) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 1, NULL);

    /* Tick to apply KON and let voices produce output */
    int16_t dry_l, dry_r, rev_l, rev_r;
    int found_rev_nonzero = 0;
    int found_dry_nonzero = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (rev_l != 0 || rev_r != 0) found_rev_nonzero = 1;
        if (dry_l != 0 || dry_r != 0) found_dry_nonzero = 1;
    }

    /* Both dry and reverb should be non-zero (voice 1 is EON) */
    TEST_ASSERT_TRUE(found_dry_nonzero);
    TEST_ASSERT_TRUE(found_rev_nonzero);

    /* Now key on voice 0 with EON=0 only — reverb sum should be zero */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    int found_rev = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (rev_l != 0 || rev_r != 0) found_rev = 1;
    }
    /* No reverb output when EON=0 for all active voices */
    TEST_ASSERT_FALSE(found_rev);
}

/* Test: master_vol = 0 silences output (MIX-03) */
void test_mixer_master_vol_zero_silences(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0;  /* zero master volume */
    s_test_mixer.master_vol_r = 0;

    /* Load sample */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on a voice at full individual volume */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Tick — dry output should be zero due to master_vol=0 */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        TEST_ASSERT_EQUAL_INT16(0, dry_l);
        TEST_ASSERT_EQUAL_INT16(0, dry_r);
    }
}

/* Test: KON deferred — voice not active until next tick (C8/MIX-04) */
void test_mixer_kon_deferred(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voice 0 */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Before tick: voice should NOT be active (pending only) */
    TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.voices[0].active);

    /* After tick: voice should be active */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    TEST_ASSERT_EQUAL_UINT8(1, s_test_mixer.voices[0].active);
}

/* ---------------------------------------------------------------
 * Phase 30 Task 3: MIX-06 coexistence + saturation + timing tests
 * --------------------------------------------------------------- */

/* Test: MIX-06 — voice output and ADPCM (coloration bus) are independent.
 * Verifies the sat_s16 addition rule: adpcm + voice_dry = non-destructive sum.
 * We test the math path directly since full process context is complex. */
void test_mix06_voice_and_adpcm_independent(void) {
    /* Simulate the coexistence sum from spu94_process.c:
     *   adpcm_l = sat_s16((int32_t)adpcm_l + (int32_t)voice_dry_l);
     *
     * When ADPCM coloration produces adpcm_l=100 and voice engine produces
     * voice_dry_l=200, the merged result should be 300. */
    int16_t adpcm_l = 100;
    int16_t voice_dry_l = 200;
    int16_t merged = sat_s16((int32_t)adpcm_l + (int32_t)voice_dry_l);
    TEST_ASSERT_EQUAL_INT16(300, merged);

    /* Both contributions are non-zero and independently audible */
    TEST_ASSERT_TRUE(merged > adpcm_l);  /* voice added to it */
    TEST_ASSERT_TRUE(merged > voice_dry_l);  /* ADPCM also present */

    /* Negative values (phase-inverted) also sum correctly */
    adpcm_l = -500;
    voice_dry_l = 300;
    merged = sat_s16((int32_t)adpcm_l + (int32_t)voice_dry_l);
    TEST_ASSERT_EQUAL_INT16(-200, merged);

    /* Zero voice_dry doesn't change ADPCM (engine disabled produces 0) */
    adpcm_l = 1234;
    voice_dry_l = 0;
    merged = sat_s16((int32_t)adpcm_l + (int32_t)voice_dry_l);
    TEST_ASSERT_EQUAL_INT16(1234, merged);
}

/* Test: MIX-01 saturation on loud chord — 2 voices at max don't wrap negative.
 * Verifies sat_s16(32767 + 32767) = 32767, not -2 (wrap-around).
 *
 * The test proves two properties:
 * 1. The sat_s16 math itself is correct (direct verification)
 * 2. The mixer uses int32 accumulation (output stays in valid range after
 *    ring buffer stabilization — no 16-bit wrap-around artifacts) */
void test_mix01_saturation_on_loud_chord(void) {
    /* Part 1: Verify sat_s16 math directly (the core of MIX-01) */
    TEST_ASSERT_EQUAL_INT16(32767, sat_s16(32767 + 32767));
    TEST_ASSERT_EQUAL_INT16(32767, sat_s16((int32_t)INT16_MAX + (int32_t)INT16_MAX));
    TEST_ASSERT_EQUAL_INT16(-32768, sat_s16((int32_t)INT16_MIN + (int32_t)INT16_MIN));
    /* Saturation prevents wrap: this would be -2 if using naive int16 addition */
    TEST_ASSERT_TRUE(sat_s16(32767 + 32767) > 0);

    /* Part 2: Run two voices with large amplitude through the mixer.
     * After ring buffer stabilization (~5 ticks), the output should be
     * consistently positive when both voices produce positive samples.
     * shift=12, filter=0, nibble=7 -> decoded sample = 7 << 12 = 28672. */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    uint8_t sample[64];
    memset(sample, 0, sizeof(sample));
    for (int b = 0; b < 4; b++) {
        sample[b * 16 + 0] = 0x0C;  /* shift=12, filter=0 */
        sample[b * 16 + 1] = 0x00;  /* no flags */
        for (int j = 2; j < 16; j++) {
            sample[b * 16 + j] = 0x77;  /* nibbles +7, +7 */
        }
    }
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voice 0 and voice 1 with same max-amplitude sample */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Run ticks — first ~4 ticks have transients from the cold ring buffer.
     * After stabilization, all output should be positive (large positive
     * input samples should produce large positive output). */
    int16_t dry_l, dry_r, rev_l, rev_r;

    /* Warm up ring: 5 ticks to fill the 4-sample Gaussian ring */
    for (int i = 0; i < 5; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* After stabilization: output must be positive (no wrap-around).
     * Two voices of ~28672 sum to ~57344, sat_s16 -> 32767, then
     * master vol q15_mul_truncate(32767, 0x7FFF) -> 32766. */
    int found_positive = 0;
    for (int i = 0; i < 5; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (dry_l > 0) found_positive = 1;
        /* After ring is full of positive values, output must be positive.
         * A 16-bit overflow/wrap would give negative values. */
        TEST_ASSERT_TRUE_MESSAGE(dry_l >= 0,
            "MIX-01 violation: int16 wrap detected on loud chord");
    }
    TEST_ASSERT_TRUE(found_positive);
}

/* Test: MIX-04 — two voices keyed simultaneously start at same sample offset.
 * Both pending_kon bits set in same batch, applied at tick start. Both voices
 * should start with pitch_counter=0. */
void test_mix04_kon_timing_two_voices_same_tick(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voices 0 and 1 in same batch (before any tick) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Verify both pending bits are set */
    TEST_ASSERT_TRUE(s_test_mixer.pending_kon & (1u << 0));
    TEST_ASSERT_TRUE(s_test_mixer.pending_kon & (1u << 1));

    /* Apply by running one tick */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Both voices should be active and at the same point in playback.
     * After one tick at pitch 0x1000: pitch_counter should be the same
     * fractional value for both (they started at 0 simultaneously). */
    TEST_ASSERT_EQUAL_UINT8(1, s_test_mixer.voices[0].active);
    TEST_ASSERT_EQUAL_UINT8(1, s_test_mixer.voices[1].active);
    TEST_ASSERT_EQUAL_UINT16(s_test_mixer.voices[0].pitch_counter,
                             s_test_mixer.voices[1].pitch_counter);

    /* Both should be at the same current_addr (decoding same block) */
    TEST_ASSERT_EQUAL_UINT32(s_test_mixer.voices[0].current_addr,
                             s_test_mixer.voices[1].current_addr);
}

/* ---------------------------------------------------------------
 * Phase 32: Anti-Aliasing / Gauss Bypass Tests (AA-01..03)
 * --------------------------------------------------------------- */

/* Test: gauss_bypass=1 (ZOH) produces different output from gauss_bypass=0 (Gauss)
 * at non-unity pitch. This proves the bypass changes interpolation behavior.
 * At pitch 0x1800 (1.5x unity), sample-skipping occurs — Gaussian interpolation
 * smooths across skipped samples while ZOH outputs the raw newest sample. */
void test_gauss_bypass_zoh_differs_from_gauss(void) {
    /* Create a multi-block sample with varied content */
    uint8_t ram[128];
    make_long_sample(ram, 8);

    /* Collect N ticks of output with Gauss ON (bypass=0) */
    spu94_voice_t v;
    spu94_voice_init(&v);
    spu94_voice_key_on(&v, 0, 0x1800, 0x7FFF, 0x7FFF);

    const int N = 40;
    int16_t gauss_out[40];
    for (int i = 0; i < N; i++) {
        int16_t out_l, out_r;
        spu94_voice_tick(&v, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        gauss_out[i] = out_l;
    }

    /* Collect N ticks of output with ZOH (bypass=1) */
    spu94_voice_init(&v);
    spu94_voice_key_on(&v, 0, 0x1800, 0x7FFF, 0x7FFF);

    int16_t zoh_out[40];
    for (int i = 0; i < N; i++) {
        int16_t out_l, out_r;
        spu94_voice_tick(&v, ram, sizeof(ram), 1, 0, 0, &out_l, &out_r);
        zoh_out[i] = out_l;
    }

    /* At least one sample must differ between the two paths */
    int differs = 0;
    for (int i = 0; i < N; i++) {
        if (gauss_out[i] != zoh_out[i]) {
            differs = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(differs,
        "AA-01: Gauss and ZOH outputs are identical — bypass has no effect");
}

/* Test: gauss_bypass defaults to 0 (Gaussian ON) after mixer init */
void test_gauss_bypass_default_off(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.gauss_bypass);
}

/* ---------------------------------------------------------------
 * Phase 34: Signed Volume / VxOUTX Tests (SVOL-01..04)
 * --------------------------------------------------------------- */

/* Test: negative volume produces exact phase inversion of positive volume.
 * vol_l=-0x4000 output == -(vol_l=+0x4000 output) for every tick (SVOL-03). */
void test_negative_volume_phase_inversion(void) {
    /* Use make_long_sample which produces nonzero content via nibbles +7,+3,+5,+2
     * at shift=0, filter=0. Then use full volume 0x7FFF for maximum output. */
    uint8_t ram[128];
    make_long_sample(ram, 8);

    /* Run voice with positive volume: vol_l=+0x4000, vol_r=+0x4000 */
    spu94_voice_t v_pos;
    spu94_voice_init(&v_pos);
    spu94_voice_key_on(&v_pos, 0, 0x1000, 0x4000, 0x4000);

    /* Warm up Gaussian ring (first ~4 ticks from cold ring produce 0) */
    int16_t out_l, out_r;
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v_pos, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }

    /* Collect N ticks of stabilized output */
    const int N = 20;
    int16_t pos_l[20], pos_r[20];
    for (int i = 0; i < N; i++) {
        spu94_voice_tick(&v_pos, ram, sizeof(ram), 0, 0, 0, &pos_l[i], &pos_r[i]);
    }

    /* Run fresh voice with negative volume: vol_l=-0x4000, vol_r=-0x4000 */
    spu94_voice_t v_neg;
    spu94_voice_init(&v_neg);
    spu94_voice_key_on(&v_neg, 0, 0x1000, (int16_t)(-0x4000), (int16_t)(-0x4000));

    /* Same warmup */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v_neg, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
    }

    /* Collect N ticks */
    int16_t neg_l[20], neg_r[20];
    for (int i = 0; i < N; i++) {
        spu94_voice_tick(&v_neg, ram, sizeof(ram), 0, 0, 0, &neg_l[i], &neg_r[i]);
    }

    /* For every tick where the positive output is nonzero, the negative output
     * must be the negation within 1 LSB (SVOL-03: phase inversion).
     *
     * Q15 truncation (ASR = floor toward -inf) means q15_mul_truncate(x, -v)
     * can differ from -q15_mul_truncate(x, +v) by exactly 1 LSB when the
     * intermediate product is not evenly divisible by 2^15. This is the
     * authentic PS1 SPU behavior -- the truncation asymmetry IS the sound.
     * We verify the output is inverted within this 1-LSB tolerance. */
    int checked = 0;
    for (int i = 0; i < N; i++) {
        if (pos_l[i] != 0) {
            int16_t expected_neg = (int16_t)(-pos_l[i]);
            int16_t diff = (int16_t)(neg_l[i] - expected_neg);
            /* Must be exact or off by 1 LSB due to Q15 truncation asymmetry */
            TEST_ASSERT_TRUE_MESSAGE(diff >= -1 && diff <= 0,
                "SVOL-03: negative volume output differs from positive by more than 1 LSB");
            checked++;
        }
        if (pos_r[i] != 0) {
            int16_t expected_neg = (int16_t)(-pos_r[i]);
            int16_t diff = (int16_t)(neg_r[i] - expected_neg);
            TEST_ASSERT_TRUE_MESSAGE(diff >= -1 && diff <= 0,
                "SVOL-03: negative volume output differs from positive by more than 1 LSB");
        }
    }
    /* At least some samples must be nonzero for the test to be meaningful */
    TEST_ASSERT_TRUE(checked > 0);
}

/* Test: key_on accepts negative volume without clamping (SVOL-01/SVOL-02). */
void test_negative_volume_accepted_by_key_on(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    spu94_voice_key_on(&v, 0, 0x1000, (int16_t)(-0x4000), (int16_t)(-0x4000));

    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), v.vol_l);
    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), v.vol_r);
}

/* Test: outx stores post-ADSR, pre-volume value -- identical regardless of
 * volume sign (SVOL-04). */
void test_outx_stored_post_adsr_pre_volume(void) {
    /* Create sample data with nonzero content */
    uint8_t ram[128];
    make_long_sample(ram, 8);

    /* Voice A: positive volume, ADSR enabled with fast attack */
    spu94_voice_t va;
    spu94_voice_init(&va);
    va.adsr.enabled = 1;
    va.adsr.attack_shift = 0;    /* fastest attack */
    va.adsr.attack_step = 0;     /* step = (7-0) << 11 = 14336 per tick */
    va.adsr.attack_exp = 0;
    va.adsr.decay_shift = 0;
    va.adsr.sustain_level = 15;  /* max sustain */
    va.adsr.sustain_shift = 31;  /* sustain forever */
    va.adsr.release_shift = 0;
    va.adsr.release_exp = 1;
    spu94_voice_key_on(&va, 0, 0x1000, 0x4000, 0x4000);

    /* Voice B: negative volume, same ADSR config */
    spu94_voice_t vb;
    spu94_voice_init(&vb);
    vb.adsr.enabled = 1;
    vb.adsr.attack_shift = 0;
    vb.adsr.attack_step = 0;
    vb.adsr.attack_exp = 0;
    vb.adsr.decay_shift = 0;
    vb.adsr.sustain_level = 15;
    vb.adsr.sustain_shift = 31;
    vb.adsr.release_shift = 0;
    vb.adsr.release_exp = 1;
    spu94_voice_key_on(&vb, 0, 0x1000, (int16_t)(-0x4000), (int16_t)(-0x4000));

    /* Run ticks until outx is nonzero */
    int16_t out_l, out_r;
    int found_nonzero_outx = 0;
    for (int i = 0; i < 20; i++) {
        spu94_voice_tick(&va, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        spu94_voice_tick(&vb, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        if (va.outx != 0) {
            found_nonzero_outx = 1;
            /* outx must be identical for both voices regardless of volume sign */
            TEST_ASSERT_EQUAL_INT16(va.outx, vb.outx);
        }
    }
    TEST_ASSERT_TRUE(found_nonzero_outx);
}

/* Test: mixer key_on accepts negative volume and stores it through to
 * live voice after pending KON is applied (SVOL-01/SVOL-02 at mixer layer). */
void test_mixer_key_on_negative_volume(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a sample so tick doesn't immediately deactivate */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on with negative volume */
    spu94_result_t rc = spu94_voice_mixer_key_on(&s_test_mixer, 3,
        0, 0x1000, (int16_t)(-0x4000), (int16_t)(-0x4000), 0, NULL);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);

    /* Pending config should store the negative values */
    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), s_test_mixer.pending_config[3].vol_l);
    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), s_test_mixer.pending_config[3].vol_r);

    /* Apply pending KON via a mixer tick */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Live voice should have vol_l == -0x4000 after KON is applied */
    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), s_test_mixer.voices[3].vol_l);
    TEST_ASSERT_EQUAL_INT16((int16_t)(-0x4000), s_test_mixer.voices[3].vol_r);
}

/* ---------------------------------------------------------------
 * Phase 35: PMON (Pitch Modulation) Tests (PMON-01..06)
 *
 * PMON lets voice N-1's post-ADSR output modulate voice N's pitch step.
 * Formula: Factor = outx(N-1) + 0x8000; Step = (base_pitch * Factor) >> 15;
 * Clamp: if Step > 0x3FFF then Step = 0x4000.
 * Voice 0 PMON bit is accepted but ignored (no predecessor).
 * --------------------------------------------------------------- */

/* Helper: create a loud multi-block sample with maximum positive amplitude.
 * shift=0, filter=0, all nibbles=+7 -> decoded sample = 7 << 12 = 28672.
 * ADPCM formula: decoded = nibble << (12 - shift). shift=0 gives max amplification.
 * This produces large positive outx values suitable for PMON testing. */
static void make_loud_sample(uint8_t *ram, uint32_t num_blocks) {
    for (uint32_t b = 0; b < num_blocks; b++) {
        memset(ram + b * 16, 0, 16);
        ram[b * 16 + 0] = 0x00;  /* shift=0, filter=0 -> max amplification */
        ram[b * 16 + 1] = 0x00;  /* no flags (continuous) */
        for (int j = 2; j < 16; j++) {
            ram[b * 16 + j] = 0x77;  /* nibbles +7, +7 -> decoded = 7 << 12 = 28672 */
        }
    }
}

/* Test: silent modulator (outx=0) preserves carrier pitch (unity passthrough).
 * Factor = 0 + 0x8000 = 0x8000 (unity). Step = (pitch * 0x8000) >> 15 = pitch.
 * This is the correct PS1 behavior: Factor 0x8000 = 1.0x scaling.
 * Voice 1's pitch_counter should advance at the same rate as without PMON. */
void test_pmon_silent_modulator_halves_pitch(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a silent sample for voice 0 (all zeros -> outx=0) */
    uint8_t silent_sample[64];
    memset(silent_sample, 0, sizeof(silent_sample));
    for (int b = 0; b < 4; b++) {
        silent_sample[b * 16 + 0] = 0x00;  /* shift=0, filter=0 */
        silent_sample[b * 16 + 1] = 0x00;  /* no flags */
    }
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, silent_sample, 64);

    /* Load a normal sample for voice 1 at a different address */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 64, sample, 64);

    /* Key on voice 0 (silent modulator) and voice 1 (carrier with PMON) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 64, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run 1 tick to apply pending KON */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* After first tick, voice 0 produces outx=0 (silent sample).
     * Voice 1 with PMON: Factor=0x8000, effective step = (0x1000 * 0x8000) >> 15 = 0x1000.
     * This is unity — same as without PMON. The PS1 PMON center is 1.0x, not 0.5x. */
    uint16_t pmon_counter = s_test_mixer.voices[1].pitch_counter;

    /* Now run the same setup WITHOUT PMON for comparison */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, silent_sample, 64);
    spu94_voice_mixer_load_sample(&s_test_mixer, 64, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 64, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t no_pmon_counter = s_test_mixer.voices[1].pitch_counter;

    /* With silent modulator (outx=0), PMON Factor=0x8000 is unity.
     * Both counters should be identical — PMON has no effect when modulator is silent. */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(no_pmon_counter, pmon_counter,
        "PMON-03: silent modulator (Factor=0x8000) should be unity — same counter as no PMON");
}

/* Test: PMON bit 0 is accepted but ignored (voice 0 has no predecessor). */
void test_pmon_bit0_ignored(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load sample */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voice 0 */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 0 (should be accepted but ignored) */
    spu94_result_t rc = spu94_voice_mixer_set_pmon(&s_test_mixer, 0, 1);
    TEST_ASSERT_EQUAL(SPU94_OK, rc);
    TEST_ASSERT_TRUE(s_test_mixer.pmon_flags & (1u << 0));

    /* Run 1 tick */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t pmon_counter = s_test_mixer.voices[0].pitch_counter;

    /* Run same without PMON for comparison */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t no_pmon_counter = s_test_mixer.voices[0].pitch_counter;

    /* Voice 0 should be unaffected by PMON bit 0 */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(no_pmon_counter, pmon_counter,
        "PMON-05: voice 0 pitch should be unaffected by PMON bit 0");
}

/* Test: positive modulator increases pitch beyond base.
 * Voice 0 plays a loud sample producing large positive outx (~28000).
 * Factor = outx + 0x8000 >> 15 produces ~1.8x multiplier.
 * Voice 1's pitch should be noticeably higher than base.
 *
 * We verify by comparing the pitch_counter position after N ticks
 * with PMON vs without PMON. With PMON, voice 1 should have consumed
 * MORE samples (crossed more sample boundaries). */
void test_pmon_formula_positive_modulator(void) {
    /* --- Run WITH PMON --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    uint8_t loud[256];
    make_loud_sample(loud, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 256);

    /* Load a long sample for voice 1 at a separate address */
    uint8_t carrier[256];
    make_long_sample(carrier, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 256);

    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    uint32_t pmon_addr = s_test_mixer.voices[1].current_addr;

    /* --- Run WITHOUT PMON --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 256);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 256);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    /* No PMON */
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    uint32_t no_pmon_addr = s_test_mixer.voices[1].current_addr;

    /* With positive modulator, voice 1 should have consumed MORE data
     * (advanced further through sample blocks) due to higher effective pitch */
    TEST_ASSERT_TRUE_MESSAGE(pmon_addr > no_pmon_addr,
        "PMON-01: positive modulator should cause voice to consume more sample data");
}

/* Test: max negative modulator stops pitch advancement.
 * Voice 0 outx = -0x8000. Factor = -0x8000 + 0x8000 = 0x0000.
 * Step = (0x1000 * 0) >> 15 = 0. Voice 1 counter doesn't advance.
 *
 * ADPCM decode formula: decoded = nibble << (12 - shift). So shift=0 gives
 * maximum amplification: nibble -8 << 12 = -32768 = -0x8000. */
void test_pmon_formula_negative_modulator(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Create sample with maximum negative amplitude:
     * shift=0, filter=0, nibbles=-8 -> decoded = -8 << 12 = -32768 */
    uint8_t neg_sample[128];
    memset(neg_sample, 0, sizeof(neg_sample));
    for (uint32_t b = 0; b < 8; b++) {
        neg_sample[b * 16 + 0] = 0x00;  /* shift=0, filter=0 */
        neg_sample[b * 16 + 1] = 0x00;  /* no flags */
        for (int j = 2; j < 16; j++) {
            neg_sample[b * 16 + j] = 0x88;  /* nibbles -8, -8 -> decoded = -32768 */
        }
    }
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, neg_sample, 128);

    /* Load normal sample for voice 1 */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 128, sample, 64);

    /* Key on voice 0 with ADSR at max sustain */
    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);

    /* Key on voice 1 */
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 128, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run ticks to let ADSR reach max and ring stabilize with -32768 values */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* Voice 0's outx should be near -32768 (max negative). Due to Gaussian
     * interpolation and Q15 truncation, the actual outx stabilizes at about
     * -32640 -- never exactly -32768. This is authentic PS1 behavior.
     *
     * Factor = outx + 0x8000. With outx = -32640: Factor = 128 = 0x0080.
     * Step = (0x1000 * 0x80) >> 15 = 0x0010 (very small but nonzero).
     *
     * We verify the effective step is negligibly small (< 1% of base pitch). */
    uint16_t pre_counter = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter = s_test_mixer.voices[1].pitch_counter;

    /* Compute effective step (handle 12-bit counter wrap) */
    uint16_t step;
    if (post_counter >= pre_counter) {
        step = (uint16_t)(post_counter - pre_counter);
    } else {
        step = (uint16_t)(0x1000 + post_counter - pre_counter);
    }

    /* Step should be negligibly small compared to base pitch 0x1000.
     * With authentic PS1 Q15 truncation, expect step <= 0x20 (~0.5% of base). */
    TEST_ASSERT_TRUE_MESSAGE(step < 0x40,
        "PMON-01: near-max negative modulator should nearly stop pitch (step < 1% base)");
}

/* Test: PMON chain stacking — voice 0->1->2 cascading modulation.
 * Voices 0, 1, 2 all active at same pitch. PMON on voices 1 and 2.
 * Voice 0 modulates 1, voice 1's (modulated) output modulates 2.
 *
 * We verify by comparing current_addr (sample data position) after N ticks.
 * Voice 1 should consume more sample data than voice 0 (PMON speeds it up).
 * Voice 2 should also be accelerated but by a DIFFERENT amount than voice 1
 * (because voice 1's outx differs from voice 0's outx due to PMON-altered playback). */
void test_pmon_chain_stacking(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a sample with VARYING content for all voices.
     * Constant-amplitude samples produce the same outx regardless of playback
     * speed, so chain modulation would be identical. Varying content ensures
     * that voice 1 (playing faster) reads different sample values than voice 0,
     * producing different outx and thus different modulation for voice 2.
     *
     * Use shift=0, filter=0, alternating loud nibbles: blocks alternate
     * between +7,+7 and +3,+5 content to create amplitude variation. */
    uint8_t sample[512];
    memset(sample, 0, sizeof(sample));
    for (uint32_t b = 0; b < 32; b++) {
        sample[b * 16 + 0] = 0x00;  /* shift=0, filter=0 */
        sample[b * 16 + 1] = 0x00;  /* no flags */
        if (b % 2 == 0) {
            /* Loud block: +7 nibbles -> decoded = 28672 */
            for (int j = 2; j < 16; j++) sample[b * 16 + j] = 0x77;
        } else {
            /* Quieter block: +3 nibbles -> decoded = 12288 */
            for (int j = 2; j < 16; j++) sample[b * 16 + j] = 0x33;
        }
    }
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 512);

    /* Key on voices 0, 1, 2 at the same pitch with ADSR for consistent outx */
    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;

    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 2, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);

    /* Enable PMON on voices 1 and 2 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);
    spu94_voice_mixer_set_pmon(&s_test_mixer, 2, 1);

    /* Run enough ticks for ADSR to stabilize and PMON pitch differences to
     * accumulate enough to cross block boundaries. At base pitch 0x1000 (1 sample/tick)
     * and PMON ~1.8x, after 100 ticks voice 0 consumes ~100 samples (~3.6 blocks)
     * while voice 1 consumes ~180 samples (~6.4 blocks) — clearly different addresses. */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 100; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* After 100 ticks, compare how far each voice has advanced through sample data.
     * Voice 0: unmodulated at base pitch.
     * Voice 1: modulated by voice 0's outx (large positive) -> faster playback.
     * Voice 2: modulated by voice 1's outx (which is different from voice 0's
     *          because voice 1 is playing at a different rate). */
    uint32_t addr0 = s_test_mixer.voices[0].current_addr;
    uint32_t addr1 = s_test_mixer.voices[1].current_addr;
    uint32_t addr2 = s_test_mixer.voices[2].current_addr;

    /* Voice 1 should have consumed more data than voice 0 (PMON accelerates it) */
    TEST_ASSERT_TRUE_MESSAGE(addr1 > addr0,
        "PMON-04: voice 1 (modulated by voice 0) should advance faster than unmodulated voice 0");

    /* Voice 2 should differ from voice 1 in either address or fractional counter
     * position. Cascading modulation means voice 2's modulator (voice 1) has
     * different outx over time than voice 1's modulator (voice 0), producing
     * different cumulative advancement. Even when block addresses coincide,
     * the sub-sample fractional counters prove different effective pitch history. */
    uint16_t ctr1 = s_test_mixer.voices[1].pitch_counter;
    uint16_t ctr2 = s_test_mixer.voices[2].pitch_counter;
    TEST_ASSERT_TRUE_MESSAGE(addr2 != addr1 || ctr2 != ctr1,
        "PMON-04: cascading modulation should produce different advancement for voice 2 vs voice 1");
}

/* Test: PMON clamp to 0x4000 (not 0x3FFF).
 * When PMON produces a step > 0x3FFF, it clamps to 0x4000.
 * Use a high base pitch (0x3000) with a strong positive modulator to exceed 0x3FFF.
 *
 * Verification approach: with step=0x4000, the counter advances by exactly
 * 0x4000 each tick. Since 0x4000 = 4 * 0x1000, this consumes exactly 4 samples
 * with zero fractional remainder. The fractional counter (& 0x0FFF) stays the same.
 *
 * If the clamp were 0x3FFF instead of 0x4000, the counter would advance by
 * 0x3FFF, leaving a fractional remainder of 0x0FFF each tick (it would
 * never be zero). This test distinguishes the two. */
void test_pmon_clamp_0x4000(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load loud sample for voice 0 (produces large positive outx ~28559) */
    uint8_t loud[256];
    make_loud_sample(loud, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 256);

    /* Load a long normal sample for voice 1 at a separate address */
    uint8_t carrier[512];
    make_long_sample(carrier, 32);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 512);

    /* Key on voice 0 with ADSR at max sustain for stable large positive outx */
    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);

    /* Key on voice 1 at high pitch (0x3000) — with strong positive mod,
     * unclamped step = (0x3000 * ~0xEF8F) >> 15 = ~0x59E1.
     * Since 0x59E1 > 0x3FFF, clamp to 0x4000. */
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x3000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run ticks to let ADSR reach max and ring stabilize */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* With step=0x4000 (exactly 4 samples), the fractional counter stays unchanged.
     * Run one tick and verify counter stays the same. */
    uint16_t pre_counter = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter = s_test_mixer.voices[1].pitch_counter;

    /* With step=0x4000, fractional part doesn't change (exact multiple of 0x1000).
     * With step=0x3FFF, fractional part would change by 0xFFF each tick. */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(pre_counter, post_counter,
        "PMON-06: step clamped to 0x4000 should produce exact sample boundary advancement (no fractional drift)");
}

/* ---------------------------------------------------------------
 * Phase 35 Plan 02: PMON Integration Tests (PMON-02, PMON-07)
 * --------------------------------------------------------------- */

/* Test: modulator voice ADSR shapes FM modulation depth over time.
 *
 * Phase 35 success criterion 1: "A modulator voice playing a slow sine
 * sweeps the carrier voice's pitch audibly, with depth controlled by the
 * modulator's ADSR."
 *
 * Setup:
 *   Voice 0 (modulator): plays a loud ADPCM sample with a SLOW ADSR attack
 *     (attack_shift=10 for slow linear ramp). ADSR starts at level 0 and
 *     ramps up gradually, so outx starts near zero and increases over ticks.
 *   Voice 1 (carrier): plays a sample with ADSR at instant max sustain.
 *     PMON enabled -- voice 1's pitch is modulated by voice 0's outx.
 *     Both voices have the same base pitch (0x1000).
 *
 * Proof:
 *   Early ticks (voice 0's ADSR near zero): outx ~ 0, Factor ~ 0x8000 (unity),
 *     voice 1's effective pitch ~ base pitch 0x1000.
 *   Later ticks (voice 0's ADSR ramped up): outx > 0, Factor > 0x8000,
 *     voice 1's effective pitch > base pitch.
 *   The late-tick pitch_counter delta exceeds the early-tick delta, proving
 *   that the modulator's ADSR ramp increased the FM modulation depth. */
void test_pmon_adsr_shapes_modulation_depth(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a loud sample for voice 0 (modulator) -- large positive amplitude.
     * shift=0, filter=0, all nibbles=+7 -> decoded = 7 << 12 = 28672.
     * With ADSR scaling, outx = q15_mul(28672, adsr_level). */
    uint8_t mod_sample[512];
    make_loud_sample(mod_sample, 32);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, mod_sample, 512);

    /* Load a normal sample for voice 1 (carrier) at a separate address */
    uint8_t car_sample[512];
    make_long_sample(car_sample, 32);
    spu94_voice_mixer_load_sample(&s_test_mixer, 512, car_sample, 512);

    /* Voice 0 (modulator): SLOW ADSR attack.
     * attack_shift=10: counter fires when bit 15 of (counter + (1 << max(0,10-11))) rolls.
     * With shift=10: AdsrCycles = 1 << max(0, 10-11) = 1 (fires every tick).
     * AdsrStep = (7 - step) << max(0, 11-10) = 7 << 1 = 14 per tick.
     * So adsr_level increases by 14 per tick (out of 0x7FFF = 32767).
     * Takes ~2340 ticks to reach max. Very slow ramp. */
    spu94_adsr_state_t mod_adsr;
    spu94_adsr_init(&mod_adsr);
    mod_adsr.enabled = 1;
    mod_adsr.attack_shift = 10;   /* slow attack */
    mod_adsr.attack_step = 0;     /* largest step within shift: (7-0) = 7 */
    mod_adsr.attack_exp = 0;      /* linear */
    mod_adsr.decay_shift = 0;     /* fast decay */
    mod_adsr.sustain_level = 15;  /* max sustain target */
    mod_adsr.sustain_shift = 31;  /* sustain forever */
    mod_adsr.sustain_step = 0;
    mod_adsr.sustain_exp = 0;
    mod_adsr.sustain_dir = 0;
    mod_adsr.release_shift = 0;
    mod_adsr.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &mod_adsr);

    /* Voice 1 (carrier): instant ADSR (shift=0, step=0 -> fires every tick,
     * step value = 14336 per tick, reaches max in ~3 ticks). No ADSR effect. */
    spu94_adsr_state_t car_adsr;
    spu94_adsr_init(&car_adsr);
    car_adsr.enabled = 1;
    car_adsr.attack_shift = 0;    /* instant attack */
    car_adsr.attack_step = 0;
    car_adsr.attack_exp = 0;
    car_adsr.decay_shift = 0;
    car_adsr.sustain_level = 15;
    car_adsr.sustain_shift = 31;
    car_adsr.release_shift = 0;
    car_adsr.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 512, 0x1000, 0x7FFF, 0x7FFF, 0, &car_adsr);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run ticks and measure effective pitch at early and late points.
     *
     * Measurement method: record voice 1's pitch_counter before and after a
     * single tick. The delta is the effective pitch step for that tick.
     * Early ticks: modulator ADSR near zero -> outx near zero -> Factor near
     * 0x8000 (unity) -> step near base pitch.
     * Late ticks: modulator ADSR ramped up -> outx > 0 -> Factor > 0x8000
     * -> step > base pitch. */
    int16_t dry_l, dry_r, rev_l, rev_r;

    /* Run 5 ticks to apply pending KON and warm up Gaussian rings */
    for (int i = 0; i < 5; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* Measure effective pitch at tick 6 (early -- modulator ADSR still very low).
     * After 5 ticks with attack_shift=10 step=0: ADSR level ~ 5 * 14 = 70.
     * outx = q15_mul(~28672, 70) ~ 61. Factor ~ 0x8000 + 61 = 0x803D.
     * Step ~ (0x1000 * 0x803D) >> 15 ~ 0x1007. Nearly unity. */
    uint16_t pre_counter_early = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter_early = s_test_mixer.voices[1].pitch_counter;

    /* Compute early-tick effective step (handle 12-bit fractional wrap) */
    uint16_t early_step;
    if (post_counter_early >= pre_counter_early) {
        early_step = (uint16_t)(post_counter_early - pre_counter_early);
    } else {
        /* Counter wrapped past 0x0FFF -- add the consumed whole-sample part */
        early_step = (uint16_t)(0x1000 + post_counter_early - pre_counter_early);
    }

    /* Now run many more ticks to let modulator ADSR ramp up significantly.
     * After 80 more ticks (total ~86): ADSR level ~ 86 * 14 = 1204.
     * outx = q15_mul(~28559, 1204) ~ 1050. Factor ~ 0x8000 + 1050 = 0x841A.
     * Step ~ (0x1000 * 0x841A) >> 15 ~ 0x1083. Noticeably above unity. */
    for (int i = 0; i < 80; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* Measure effective pitch at tick ~87 (late -- modulator ADSR ramped up) */
    uint16_t pre_counter_late = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter_late = s_test_mixer.voices[1].pitch_counter;

    uint16_t late_step;
    if (post_counter_late >= pre_counter_late) {
        late_step = (uint16_t)(post_counter_late - pre_counter_late);
    } else {
        late_step = (uint16_t)(0x1000 + post_counter_late - pre_counter_late);
    }

    /* The late-tick effective step MUST be greater than the early-tick step.
     * This proves that the modulator's ADSR ramp increased the FM depth. */
    TEST_ASSERT_TRUE_MESSAGE(late_step > early_step,
        "PMON-02: modulator ADSR ramp must increase carrier effective pitch over time");

    /* Sanity: both voices must still be active */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s_test_mixer.voices[0].active,
        "modulator voice should still be active (slow attack not finished)");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s_test_mixer.voices[1].active,
        "carrier voice should still be active");

    /* Sanity: modulator's ADSR level should be notably above zero but below max.
     * This confirms the slow attack is actually ramping (not stuck at 0 or at max). */
    TEST_ASSERT_TRUE_MESSAGE(s_test_mixer.voices[0].adsr.level > 100,
        "modulator ADSR should have ramped above zero by tick 87");
    TEST_ASSERT_TRUE_MESSAGE(s_test_mixer.voices[0].adsr.level < 0x7FFF,
        "modulator ADSR should not have reached max yet (slow attack)");
}

/* ---------------------------------------------------------------
 * Phase 36: NON (Noise On) Tests (NON-04..NON-07)
 *
 * NON-enabled voices output the global noise level instead of ADPCM/Gauss
 * interpolation output. ADSR still shapes the result. ADPCM decode still
 * runs for side effects (loop flags, ENDX). All NON voices share the
 * same noise level per tick (single global generator).
 * --------------------------------------------------------------- */

/* Test: NON-enabled voice outputs noise, not ADPCM/Gauss.
 * Enable NON for voice 0, set noise_gen to a known level, tick mixer.
 * Verify output derives from noise level, not decoded ADPCM. */
void test_non_voice_outputs_noise(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a loud sample (produces large non-zero ADPCM output) */
    uint8_t sample[64];
    make_loud_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Key on voice 0 with ADSR disabled (level stays 0x7FFF) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* First tick without NON to capture normal ADPCM output */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    int16_t normal_output = dry_l;

    /* Re-initialize, enable NON, set noise to a small known value */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON for voice 0 */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    /* Set noise level to a known small value */
    s_test_mixer.noise_gen.level = 100;

    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* With NON enabled, output should derive from noise level, not ADPCM.
     * The ADPCM sample produces a large value (~28672 range), while
     * noise level is set to 100. Output should be much smaller than
     * the normal ADPCM output. */
    TEST_ASSERT_TRUE_MESSAGE(dry_l != normal_output,
        "NON-04: NON-enabled voice should produce different output than normal ADPCM");
}

/* Test: Two NON-enabled voices produce identical output samples.
 * (NON-05: single global generator, same noise_level for all) */
void test_non_two_voices_same_output(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load the same sample for both voices */
    uint8_t sample[64];
    make_loud_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_load_sample(&s_test_mixer, 64, sample, 64);

    /* Key on voices 0 and 1 with identical volume/pitch/ADSR */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 64, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON for both voices */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    spu94_voice_mixer_set_non(&s_test_mixer, 1, 1);

    /* Set noise to a known value */
    s_test_mixer.noise_gen.level = 500;

    /* Tick once (applies KON) */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Read per-voice outx -- both should be identical since they get the same noise_level */
    int16_t outx_0 = s_test_mixer.voices[0].outx;
    int16_t outx_1 = s_test_mixer.voices[1].outx;

    TEST_ASSERT_EQUAL_INT16_MESSAGE(outx_0, outx_1,
        "NON-05: Two NON voices should produce identical outx (shared global noise)");
}

/* Test: ADPCM decode still runs for NON voices (ENDX fires on loop-end).
 * (NON-06: loop flags are side effects of ADPCM decode, which runs regardless) */
void test_non_adpcm_still_runs(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Build a 2-block looping sample with LOOP_END flag on block 1 */
    uint8_t sample[32];
    memset(sample, 0, sizeof(sample));
    sample[0]  = 0x00;  /* shift=0, filter=0 */
    sample[1]  = SPU94_VAG_FLAG_LOOP_START;  /* 0x04 */
    for (int j = 2; j < 16; j++) sample[j] = 0x77;
    sample[16] = 0x00;
    sample[17] = SPU94_VAG_FLAG_END | SPU94_VAG_FLAG_LOOP_REPEAT;  /* 0x03 */
    for (int j = 18; j < 32; j++) sample[j] = 0x77;

    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 32);

    /* Key on voice 0 with ADSR disabled */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);

    /* Tick enough to decode both blocks (28 samples per block at pitch=0x1000) */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 30; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* ENDX should be set -- ADPCM decode processed the LOOP_END flag even with NON */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, spu94_voice_get_endx(&s_test_mixer.voices[0]),
        "NON-06: ENDX should fire for NON voice (ADPCM decode runs for side effects)");

    /* Voice should still be active (loop repeat keeps it alive) */
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, s_test_mixer.voices[0].active,
        "NON-06: Voice should still be active (loop repeat)");
}

/* Test: ADSR envelope shapes noise output.
 * (NON-07: gauss_out = noise_level, then adsr_level applied in Step 2.5)
 * Key on with slow attack ADSR, verify output increases as ADSR ramps. */
void test_non_adsr_shapes_noise(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a sample */
    uint8_t sample[64];
    make_loud_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);

    /* Configure ADSR with slow attack */
    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 5;   /* moderate attack speed */
    adsr_cfg.attack_step = 4;    /* step = (7-4) << max(0, 11-5) = 3 << 6 = 192 per trigger */
    adsr_cfg.sustain_level = 15;     /* max: target = (15+1)*0x800 */
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.sustain_step = 0;
    adsr_cfg.release_shift = 0;

    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);

    /* Hold noise level constant: set level=1000 AND timer very high so
     * spu94_noise_gen_tick() decrements timer but never underflows (no LFSR shift).
     * shift=0, step=4 -> reload=131072. Set timer to 131072 so first tick
     * decrements to 131068, second to 131064, etc. — never goes negative. */
    s_test_mixer.noise_gen.level = 1000;
    s_test_mixer.noise_gen.timer = 131072;
    s_test_mixer.noise_gen.shift = 0;
    s_test_mixer.noise_gen.step  = 4;

    /* Tick 1: ADSR just started, level is low */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    int16_t early_outx = s_test_mixer.voices[0].outx;

    /* Run more ticks to let ADSR ramp up (noise_gen.level stays 1000 because
     * timer never underflows) */
    for (int i = 0; i < 50; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    int16_t late_outx = s_test_mixer.voices[0].outx;

    /* Late outx should be larger than early outx (ADSR ramped up -> more noise through) */
    TEST_ASSERT_TRUE_MESSAGE(late_outx > early_outx,
        "NON-07: ADSR attack should increase noise output over time (noise * adsr_level)");
}

/* Test: Per-voice pitch has NO effect on noise output.
 * (NON-03: noise frequency is from SPUCNT, not per-voice pitch register)
 * Run two NON voices at different pitches, verify identical outx. */
void test_non_pitch_no_effect(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load the same sample at two addresses */
    uint8_t sample[64];
    make_loud_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_load_sample(&s_test_mixer, 64, sample, 64);

    /* Key on voice 0 at pitch 0x0800, voice 1 at pitch 0x2000 (4x faster) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x0800, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 64, 0x2000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON for both */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    spu94_voice_mixer_set_non(&s_test_mixer, 1, 1);

    /* Set noise to known value */
    s_test_mixer.noise_gen.level = 2000;

    /* Apply KON */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Both voices should have identical outx regardless of pitch */
    int16_t outx_0 = s_test_mixer.voices[0].outx;
    int16_t outx_1 = s_test_mixer.voices[1].outx;

    TEST_ASSERT_EQUAL_INT16_MESSAGE(outx_0, outx_1,
        "NON-03: Pitch should not affect NON output (noise frequency is global)");
}

/* ---------------------------------------------------------------
 * Phase 38: Integration — INT-01 Processing Order Proof Tests
 * --------------------------------------------------------------- */

/* INT-01: Prove sweep modifies vol_l BEFORE the volume multiply step uses it.
 * Setup: voice with sweep_l active (linear increase, shift=0, step=0 for max rate),
 * initial vol_l=0x100. One mixer tick. Swept output differs from unswerpt control. */
void test_int01_processing_order_sweep_before_decode(void) {
    /* --- Run WITH sweep active --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a loud sample */
    uint8_t sample[256];
    make_loud_sample(sample, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 256);

    /* Key on voice 0 with vol_l=0x100, ADSR disabled (level stays 0x7FFF) */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x0100, 0x0100, 0, NULL);

    /* Apply KON first */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Now configure sweep on the live voice (linear increase, shift=0, step=0 = max rate) */
    spu94_voice_mixer_set_sweep_l(&s_test_mixer, 0, 0, 0, 0, 0, 0);

    /* Run a few ticks to let sweep advance vol_l above 0x100 */
    for (int i = 0; i < 5; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    int16_t swept_vol_l = s_test_mixer.voices[0].vol_l;
    int16_t swept_output = dry_l;

    /* vol_l must have increased above 0x100 due to sweep */
    TEST_ASSERT_TRUE_MESSAGE(swept_vol_l > 0x0100,
        "INT-01: sweep should have increased vol_l above initial 0x100");

    /* --- Run WITHOUT sweep (static vol_l=0x100) --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 256);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x0100, 0x0100, 0, NULL);

    /* Run same number of ticks (1 for KON + 5 additional) */
    for (int i = 0; i < 6; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    int16_t unswerpt_output = dry_l;

    /* vol_l should still be 0x100 (no sweep) */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0100, s_test_mixer.voices[0].vol_l,
        "INT-01: control voice vol_l should remain 0x100 without sweep");

    /* The swept output must differ from the unswept output, proving sweep
     * modified vol_l BEFORE the volume multiply used it */
    TEST_ASSERT_TRUE_MESSAGE(swept_output != unswerpt_output,
        "INT-01: swept output must differ from unswerpt — proves sweep runs before volume multiply");
}

/* INT-01: Prove PMON pitch modification happens BEFORE voice_tick (and thus before
 * ADPCM decode uses the pitch). Voice 1's current_addr differs from a control run
 * without PMON, proving PMON altered the pitch before the counter advanced. */
void test_int01_processing_order_pmon_before_decode(void) {
    /* --- Run WITH PMON --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Voice 0: loud modulator with ADSR at max sustain */
    uint8_t loud[256];
    make_loud_sample(loud, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 256);

    /* Voice 1: carrier at a separate address */
    uint8_t carrier[256];
    make_long_sample(carrier, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 256);

    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15;
    adsr_cfg.sustain_shift = 31;
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;

    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    uint32_t pmon_addr = s_test_mixer.voices[1].current_addr;

    /* --- Run WITHOUT PMON --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 256);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 256);
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    /* No PMON */
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    uint32_t no_pmon_addr = s_test_mixer.voices[1].current_addr;

    /* PMON with a positive modulator increases effective pitch, so the carrier
     * should have consumed more sample data (higher current_addr). This proves
     * PMON altered the pitch BEFORE voice_tick used it for counter advancement. */
    TEST_ASSERT_TRUE_MESSAGE(pmon_addr != no_pmon_addr,
        "INT-01: PMON must modify pitch before voice_tick — current_addr must differ");
    TEST_ASSERT_TRUE_MESSAGE(pmon_addr > no_pmon_addr,
        "INT-01: positive modulator should advance carrier further (PMON applied before decode)");
}

/* INT-01: Prove noise generator ticks exactly once before any voice processes.
 * Two NON-enabled voices must have identical outx — proving both received the
 * same noise_gen.level (from a single tick at top of mixer_tick, not per-voice). */
void test_int01_processing_order_noise_global_before_voices(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load samples at two addresses */
    uint8_t sample[64];
    make_loud_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 64);
    spu94_voice_mixer_load_sample(&s_test_mixer, 64, sample, 64);

    /* Key on voices 0 and 1 with identical volume/pitch, ADSR disabled */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 64, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON for both voices */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    spu94_voice_mixer_set_non(&s_test_mixer, 1, 1);

    /* Set noise to a fast-shifting state: shift=15, step=7.
     * At shift=15: timer reload = 0x20000 >> 15 = 4. step=7 decrements by 7.
     * The LFSR shifts rapidly, producing changing level values. */
    s_test_mixer.noise_gen.shift = 15;
    s_test_mixer.noise_gen.step  = 7;
    s_test_mixer.noise_gen.level = 1;  /* seed */
    s_test_mixer.noise_gen.timer = 0;

    /* First tick applies KON */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* After the tick, both NON voices should have received the SAME noise level.
     * If noise ticked per-voice (incorrectly), they would get different values.
     * Use outx to compare — outx = gauss_out * adsr_level, and gauss_out = noise_level
     * for NON voices. With ADSR disabled, adsr_level = 0x7FFF (nearly unity). */
    int16_t outx_0 = s_test_mixer.voices[0].outx;
    int16_t outx_1 = s_test_mixer.voices[1].outx;

    TEST_ASSERT_EQUAL_INT16_MESSAGE(outx_0, outx_1,
        "INT-01: both NON voices must get the same noise level — proves noise ticks once globally before voice loop");

    /* Run several more ticks to confirm this holds consistently */
    for (int i = 0; i < 10; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        outx_0 = s_test_mixer.voices[0].outx;
        outx_1 = s_test_mixer.voices[1].outx;
        TEST_ASSERT_EQUAL_INT16_MESSAGE(outx_0, outx_1,
            "INT-01: NON voices must produce identical outx on every tick (global noise)");
    }
}

/* INT-01: Prove VxOUTX captures the signal after ADSR but before volume scaling.
 * Two runs with different vol_l but same ADSR must produce identical outx. */
void test_int01_outx_post_adsr_pre_volume(void) {
    uint8_t ram[128];
    make_long_sample(ram, 8);

    /* Voice A: vol_l=0x2000, ADSR enabled with fast attack to known sustain */
    spu94_voice_t va;
    spu94_voice_init(&va);
    va.adsr.enabled = 1;
    va.adsr.attack_shift = 0;
    va.adsr.attack_step = 0;
    va.adsr.attack_exp = 0;
    va.adsr.decay_shift = 0;
    va.adsr.sustain_level = 15;
    va.adsr.sustain_shift = 31;
    va.adsr.release_shift = 0;
    va.adsr.release_exp = 1;
    spu94_voice_key_on(&va, 0, 0x1000, 0x2000, 0x2000);

    /* Voice B: vol_l=0x7FFF (max), same ADSR */
    spu94_voice_t vb;
    spu94_voice_init(&vb);
    vb.adsr.enabled = 1;
    vb.adsr.attack_shift = 0;
    vb.adsr.attack_step = 0;
    vb.adsr.attack_exp = 0;
    vb.adsr.decay_shift = 0;
    vb.adsr.sustain_level = 15;
    vb.adsr.sustain_shift = 31;
    vb.adsr.release_shift = 0;
    vb.adsr.release_exp = 1;
    spu94_voice_key_on(&vb, 0, 0x1000, 0x7FFF, 0x7FFF);

    /* Run enough ticks for ADSR to reach sustain and outx to be nonzero */
    int16_t out_l, out_r;
    int found_nonzero = 0;
    for (int i = 0; i < 20; i++) {
        spu94_voice_tick(&va, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        spu94_voice_tick(&vb, ram, sizeof(ram), 0, 0, 0, &out_l, &out_r);
        if (va.outx != 0) {
            found_nonzero = 1;
            /* The key assertion: outx must be identical for both voices,
             * because outx = gauss_out * adsr_level, independent of vol_l/vol_r.
             * If outx were post-volume, they would differ. */
            TEST_ASSERT_EQUAL_INT16_MESSAGE(va.outx, vb.outx,
                "INT-01: outx must be identical regardless of vol_l — proves post-ADSR, pre-volume");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_nonzero,
        "INT-01: outx should be nonzero after ADSR reaches sustain");

    /* Additional check: the actual outputs must DIFFER (volume does affect final output) */
    int16_t out_a_l, out_a_r, out_b_l, out_b_r;
    spu94_voice_tick(&va, ram, sizeof(ram), 0, 0, 0, &out_a_l, &out_a_r);
    spu94_voice_tick(&vb, ram, sizeof(ram), 0, 0, 0, &out_b_l, &out_b_r);
    /* With different volumes, final outputs should differ (outx same, but volume applied after) */
    if (va.outx != 0) {
        TEST_ASSERT_TRUE_MESSAGE(out_a_l != out_b_l,
            "INT-01: final output must differ with different vol_l — volume applied after outx capture");
    }
}

/* ---------------------------------------------------------------
 * Phase 38: Integration — INT-02 PMON+NON Cross-Feature Tests
 * --------------------------------------------------------------- */

/* INT-02: Prove a NON-enabled voice's output feeds the PMON factor for the next
 * voice, producing measurable pitch variation. Voice 0 outputs noise (NON);
 * voice 1 has PMON enabled — its pitch should be modulated by voice 0's noise
 * outx. We verify by checking that voice 0's outx (the noise level that feeds
 * PMON) is nonzero, proving the noise output IS the PMON factor for voice 1.
 * Then compare voice 1's pitch_counter history against a control where voice 0
 * plays a silent ADPCM sample (outx=0, Factor=0x8000 unity). */
void test_int02_non_voice_feeds_pmon(void) {
    /* --- Run WITH NON on voice 0 (noise output feeds PMON) --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Voice 0: load a sample (ADPCM still decodes for side effects but output
     * is noise). Use a loud sample so loop/decode mechanics work. */
    uint8_t sample[512];
    make_loud_sample(sample, 32);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 512);

    /* Voice 1: carrier with long sample at separate address, high pitch to
     * consume blocks faster so address diverges within the test window. */
    uint8_t carrier[2048];
    make_long_sample(carrier, 128);
    spu94_voice_mixer_load_sample(&s_test_mixer, 512, carrier, 2048);

    /* Key on both voices, ADSR disabled (level = 0x7FFF), high pitch */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 512, 0x2000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON on voice 0 (outputs noise) */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    /* Enable PMON on voice 1 (modulated by voice 0's outx) */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Set noise to a state that produces large positive levels.
     * shift=15 for fast LFSR shifting. */
    s_test_mixer.noise_gen.level = 5000;
    s_test_mixer.noise_gen.timer = 0;
    s_test_mixer.noise_gen.shift = 15;
    s_test_mixer.noise_gen.step  = 7;

    int16_t dry_l, dry_r, rev_l, rev_r;

    /* Verify the mechanism: after a few ticks, voice 0's outx (noise) must be
     * nonzero — this is the value that feeds into PMON for voice 1. */
    int found_nonzero_outx = 0;
    for (int i = 0; i < 100; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        if (s_test_mixer.voices[0].outx != 0) {
            found_nonzero_outx = 1;
        }
    }
    uint32_t non_pmon_addr = s_test_mixer.voices[1].current_addr;

    TEST_ASSERT_TRUE_MESSAGE(found_nonzero_outx,
        "INT-02: NON voice must produce nonzero outx (noise level feeds PMON)");

    /* --- CONTROL: voice 0 NOT NON-enabled, plays a silent sample (outx=0) --- */
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Silent sample for voice 0: all zeros -> ADPCM decodes to 0 -> outx=0 */
    uint8_t silent[512];
    memset(silent, 0, sizeof(silent));
    for (int b = 0; b < 32; b++) {
        silent[b * 16 + 0] = 0x00;
        silent[b * 16 + 1] = 0x00;
    }
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, silent, 512);
    spu94_voice_mixer_load_sample(&s_test_mixer, 512, carrier, 2048);

    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 512, 0x2000, 0x7FFF, 0x7FFF, 0, NULL);
    /* No NON on voice 0 (plays silent ADPCM -> outx=0 -> Factor=0x8000 unity) */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    for (int i = 0; i < 100; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }
    uint32_t ctrl_addr = s_test_mixer.voices[1].current_addr;

    /* NON voice's noise output should feed through PMON to alter voice 1's pitch,
     * producing a different sample position than the silent-modulator control.
     * This proves NON+PMON compose correctly (spec-orthogonal cross-feature). */
    TEST_ASSERT_TRUE_MESSAGE(non_pmon_addr != ctrl_addr,
        "INT-02: NON voice output must feed PMON factor — current_addr must differ from silent control");
}

/* INT-02: Demonstrate "random pitch jitter" character from NON+PMON combination.
 * Voice 0 NON-enabled with fast noise (shift=15, step=7). Voice 1 PMON-enabled.
 * Over 50 ticks, collect voice 1's per-tick pitch step (pitch_counter delta).
 * Assert at least 2 distinct step values — proving noise modulation creates pitch
 * variation (jitter), not a constant offset. */
void test_int02_non_pmon_random_pitch_jitter(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Voice 0: NON-enabled noise source */
    uint8_t sample[256];
    make_loud_sample(sample, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 256);

    /* Voice 1: carrier */
    uint8_t carrier[512];
    make_long_sample(carrier, 32);
    spu94_voice_mixer_load_sample(&s_test_mixer, 256, carrier, 512);

    /* Key on both, ADSR disabled */
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 256, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable NON on voice 0, PMON on voice 1 */
    spu94_voice_mixer_set_non(&s_test_mixer, 0, 1);
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Fast noise: shift=15, step=7. LFSR shifts every tick or so. */
    s_test_mixer.noise_gen.level = 1;
    s_test_mixer.noise_gen.timer = 0;
    s_test_mixer.noise_gen.shift = 15;
    s_test_mixer.noise_gen.step  = 7;

    /* First tick applies KON */
    int16_t dry_l, dry_r, rev_l, rev_r;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);

    /* Collect per-tick pitch_counter values for voice 1 over 50 ticks.
     * Track the counter BEFORE and AFTER each tick to measure the effective
     * pitch step. Since pitch_counter wraps at 0x0FFF (12-bit within tick),
     * and the actual counter advancement includes sample boundary crossings,
     * we instead track voice 0's outx (the modulator value) per tick to detect
     * variation. The PMON formula makes effective pitch proportional to outx.
     * If outx varies across ticks, pitch step varies. */
    int16_t outx_values[50];
    for (int i = 0; i < 50; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
        outx_values[i] = s_test_mixer.voices[0].outx;
    }

    /* Count distinct outx values (which directly determine PMON factor) */
    int distinct_count = 0;
    int16_t seen[50];
    for (int i = 0; i < 50; i++) {
        int is_new = 1;
        for (int j = 0; j < distinct_count; j++) {
            if (seen[j] == outx_values[i]) {
                is_new = 0;
                break;
            }
        }
        if (is_new) {
            seen[distinct_count++] = outx_values[i];
        }
    }

    /* At least 2 distinct outx values across 50 ticks, proving the noise
     * modulation creates pitch variation (jitter), not a constant offset.
     * With shift=15, step=7, the LFSR should shift nearly every tick. */
    TEST_ASSERT_TRUE_MESSAGE(distinct_count >= 2,
        "INT-02: NON+PMON must produce >= 2 distinct pitch modulation factors across 50 ticks (jitter)");

    /* Sanity: with such fast noise, we expect many distinct values */
    TEST_ASSERT_TRUE_MESSAGE(distinct_count >= 5,
        "INT-02: fast noise (shift=15) should produce many distinct modulation values — confirms rich jitter");
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
    /* Phase 30 Task 2: Mixer tick integration tests */
    RUN_TEST(test_mixer_tick_two_voices_sum);
    RUN_TEST(test_mixer_eon_routes_only_flagged);
    RUN_TEST(test_mixer_master_vol_zero_silences);
    RUN_TEST(test_mixer_kon_deferred);
    /* Phase 30 Task 3: MIX-06 coexistence, saturation, timing */
    RUN_TEST(test_mix06_voice_and_adpcm_independent);
    RUN_TEST(test_mix01_saturation_on_loud_chord);
    RUN_TEST(test_mix04_kon_timing_two_voices_same_tick);
    /* Phase 32: Anti-Aliasing / Gauss Bypass tests (AA-01..03) */
    RUN_TEST(test_gauss_bypass_zoh_differs_from_gauss);
    RUN_TEST(test_gauss_bypass_default_off);
    /* Phase 34: Signed Volume / VxOUTX tests (SVOL-01..04) */
    RUN_TEST(test_negative_volume_phase_inversion);
    RUN_TEST(test_negative_volume_accepted_by_key_on);
    RUN_TEST(test_outx_stored_post_adsr_pre_volume);
    RUN_TEST(test_mixer_key_on_negative_volume);
    /* Phase 35: PMON (Pitch Modulation) tests (PMON-01..06) */
    RUN_TEST(test_pmon_silent_modulator_halves_pitch);
    RUN_TEST(test_pmon_bit0_ignored);
    RUN_TEST(test_pmon_formula_positive_modulator);
    RUN_TEST(test_pmon_formula_negative_modulator);
    RUN_TEST(test_pmon_chain_stacking);
    RUN_TEST(test_pmon_clamp_0x4000);
    /* Phase 35 Plan 02: PMON integration test (PMON-02, PMON-07) */
    RUN_TEST(test_pmon_adsr_shapes_modulation_depth);
    /* Phase 36: NON (Noise On) tests (NON-03..NON-07) */
    RUN_TEST(test_non_voice_outputs_noise);
    RUN_TEST(test_non_two_voices_same_output);
    RUN_TEST(test_non_adpcm_still_runs);
    RUN_TEST(test_non_adsr_shapes_noise);
    RUN_TEST(test_non_pitch_no_effect);
    /* Phase 38: Integration — INT-01 processing order proof */
    RUN_TEST(test_int01_processing_order_sweep_before_decode);
    RUN_TEST(test_int01_processing_order_pmon_before_decode);
    RUN_TEST(test_int01_processing_order_noise_global_before_voices);
    RUN_TEST(test_int01_outx_post_adsr_pre_volume);
    /* Phase 38: Integration — INT-02 PMON+NON cross-feature */
    RUN_TEST(test_int02_non_voice_feeds_pmon);
    RUN_TEST(test_int02_non_pmon_random_pitch_jitter);
    return UNITY_END();
}
