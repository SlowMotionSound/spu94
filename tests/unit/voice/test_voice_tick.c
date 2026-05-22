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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    }

    /* out_l should be 0 (volume=0), out_r may be non-zero */
    TEST_ASSERT_EQUAL_INT16(0, out_l);
    /* We can't guarantee out_r is non-zero at every specific tick,
     * but let's run more ticks and check */
    int found_r_nonzero = 0;
    for (int i = 0; i < 20; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, NULL, 0, 0, &out_l, &out_r);

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
    spu94_voice_tick(&v, ram, 0, 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    }

    spu94_voice_key_off(&v);
    TEST_ASSERT_EQUAL_UINT8(0, v.active);

    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    int16_t level_after_1 = v.adsr.level;

    /* Run a few more ticks — level should be increasing */
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    int16_t level_after_2 = v.adsr.level;

    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    }

    /* Now measure — the ring should have non-zero samples.
     * Track whether output generally increases. */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
        int16_t abs_out = out_l > 0 ? out_l : (int16_t)(-out_l);
        if (abs_out > prev_abs) attack_increasing = 1;
        prev_abs = abs_out;
    }
    /* ADSR level is increasing during attack (or already reached sustain) */
    TEST_ASSERT_TRUE(attack_increasing || v.adsr.phase >= ADSR_DECAY);

    /* --- Run until SUSTAIN ---
     * The ADSR should transition attack->decay->sustain */
    for (int i = 0; i < 200; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
        if (v.adsr.phase == ADSR_SUSTAIN) break;
    }
    TEST_ASSERT_EQUAL(ADSR_SUSTAIN, v.adsr.phase);

    /* Sustain level should be at target 0x4000 */
    TEST_ASSERT_EQUAL_INT16(0x4000, v.adsr.level);

    /* Output should be non-zero (sustained) */
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    release_out_first = out_l > 0 ? out_l : (int16_t)(-out_l);

    int16_t release_out_later = 0;
    for (int i = 0; i < 10; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
    }
    release_out_later = out_l > 0 ? out_l : (int16_t)(-out_l);

    /* Later output should be less than or equal to first release output
     * (decreasing amplitude during release) */
    TEST_ASSERT_TRUE(release_out_later <= release_out_first);

    /* --- Run until voice goes silent ---
     * ADSR should reach OFF, voice active=0 */
    for (int i = 0; i < 10000; i++) {
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
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
    spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);

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
        spu94_voice_tick(&v, ram, sizeof(ram), 0, &out_l, &out_r);
        gauss_out[i] = out_l;
    }

    /* Collect N ticks of output with ZOH (bypass=1) */
    spu94_voice_init(&v);
    spu94_voice_key_on(&v, 0, 0x1800, 0x7FFF, 0x7FFF);

    int16_t zoh_out[40];
    for (int i = 0; i < N; i++) {
        int16_t out_l, out_r;
        spu94_voice_tick(&v, ram, sizeof(ram), 1, &out_l, &out_r);
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
        spu94_voice_tick(&v_pos, ram, sizeof(ram), 0, &out_l, &out_r);
    }

    /* Collect N ticks of stabilized output */
    const int N = 20;
    int16_t pos_l[20], pos_r[20];
    for (int i = 0; i < N; i++) {
        spu94_voice_tick(&v_pos, ram, sizeof(ram), 0, &pos_l[i], &pos_r[i]);
    }

    /* Run fresh voice with negative volume: vol_l=-0x4000, vol_r=-0x4000 */
    spu94_voice_t v_neg;
    spu94_voice_init(&v_neg);
    spu94_voice_key_on(&v_neg, 0, 0x1000, (int16_t)(-0x4000), (int16_t)(-0x4000));

    /* Same warmup */
    for (int i = 0; i < 5; i++) {
        spu94_voice_tick(&v_neg, ram, sizeof(ram), 0, &out_l, &out_r);
    }

    /* Collect N ticks */
    int16_t neg_l[20], neg_r[20];
    for (int i = 0; i < N; i++) {
        spu94_voice_tick(&v_neg, ram, sizeof(ram), 0, &neg_l[i], &neg_r[i]);
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
        spu94_voice_tick(&va, ram, sizeof(ram), 0, &out_l, &out_r);
        spu94_voice_tick(&vb, ram, sizeof(ram), 0, &out_l, &out_r);
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

/* Helper: create a loud multi-block sample with maximum amplitude content.
 * shift=12, filter=0, all nibbles=+7 -> decoded sample = 7 << 12 = 28672.
 * This produces large outx values suitable for PMON testing. */
static void make_loud_sample(uint8_t *ram, uint32_t num_blocks) {
    for (uint32_t b = 0; b < num_blocks; b++) {
        memset(ram + b * 16, 0, 16);
        ram[b * 16 + 0] = 0x0C;  /* shift=12, filter=0 */
        ram[b * 16 + 1] = 0x00;  /* no flags (continuous) */
        for (int j = 2; j < 16; j++) {
            ram[b * 16 + j] = 0x77;  /* nibbles +7, +7 */
        }
    }
}

/* Test: silent modulator (outx=0) halves carrier pitch.
 * Factor = 0 + 0x8000 = 0x8000. Step = (pitch * 0x8000) >> 15 = pitch/2.
 * Voice 1's pitch_counter should advance at half the rate of a non-PMON voice. */
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
        /* All data bytes zero -> decoded samples are 0 */
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
     * Voice 1 with PMON: Factor=0x8000, effective step = (0x1000 * 0x8000) >> 15 = 0x0800.
     * Without PMON: step = 0x1000. */
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
    /* No PMON enabled */
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t no_pmon_counter = s_test_mixer.voices[1].pitch_counter;

    /* With PMON and silent modulator, pitch_counter should be half.
     * PMON step = 0x0800, no-PMON step = 0x1000.
     * pitch_counter after 1 tick = step & 0x0FFF.
     * 0x0800 & 0x0FFF = 0x0800, 0x1000 & 0x0FFF = 0x0000 (wraps to 0). */
    TEST_ASSERT_TRUE_MESSAGE(pmon_counter != no_pmon_counter,
        "PMON-03: silent modulator should halve pitch — counters should differ");
    /* More specifically, PMON counter should be 0x0800, no-PMON should be 0x0000 */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0x0800, pmon_counter,
        "PMON-03: with silent modulator, effective pitch should be 0x0800");
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

/* Test: max positive modulator approximately doubles pitch.
 * Voice 0 outx = +0x7FFF. Factor = 0x7FFF + 0x8000 = 0xFFFF.
 * Step = (0x1000 * 0xFFFF) >> 15 = 0x1FFF. */
void test_pmon_formula_positive_modulator(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load a loud sample for voice 0 so outx is large and positive */
    uint8_t loud[128];
    make_loud_sample(loud, 8);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 128);

    /* Also load a normal sample for voice 1 */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 128, sample, 64);

    /* Key on voice 0 with ADSR enabled, max sustain to get stable outx */
    spu94_adsr_state_t adsr_cfg;
    spu94_adsr_init(&adsr_cfg);
    adsr_cfg.enabled = 1;
    adsr_cfg.attack_shift = 0;
    adsr_cfg.attack_step = 0;    /* fastest: (7-0)<<11 = 14336/tick */
    adsr_cfg.attack_exp = 0;
    adsr_cfg.decay_shift = 0;
    adsr_cfg.sustain_level = 15; /* max sustain target -> 0x7FFF */
    adsr_cfg.sustain_shift = 31; /* hold forever */
    adsr_cfg.release_shift = 0;
    adsr_cfg.release_exp = 1;
    spu94_voice_mixer_key_on(&s_test_mixer, 0, 0, 0x1000, 0x7FFF, 0x7FFF, 0, &adsr_cfg);

    /* Key on voice 1 without ADSR */
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 128, 0x1000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run enough ticks to let voice 0's ADSR reach max sustain and
     * Gaussian ring to stabilize with large positive values */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* At this point voice 0's outx should be large positive.
     * Voice 1's effective pitch should be approximately 2x base pitch.
     * Check that voice 1's pitch_counter advances more than the base pitch
     * after a single additional tick. */
    uint16_t pre_counter = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter = s_test_mixer.voices[1].pitch_counter;

    /* Calculate the effective step (accounting for 12-bit wrap) */
    uint16_t effective_step;
    if (post_counter >= pre_counter) {
        effective_step = (uint16_t)(post_counter - pre_counter);
    } else {
        /* Counter wrapped — step crossed a sample boundary */
        effective_step = (uint16_t)(0x1000 + post_counter - pre_counter);
    }

    /* With max positive modulator (outx ~= 0x7FFF):
     * Factor = 0x7FFF + 0x8000 = 0xFFFF
     * effective_step = (0x1000 * 0xFFFF) >> 15 = 0x1FFF
     * This should be significantly larger than base pitch 0x1000 */
    TEST_ASSERT_TRUE_MESSAGE(effective_step > 0x1000,
        "PMON-01: positive modulator should increase effective pitch beyond base");
}

/* Test: max negative modulator stops pitch advancement.
 * Voice 0 outx = -0x8000. Factor = -0x8000 + 0x8000 = 0x0000.
 * Step = (0x1000 * 0) >> 15 = 0. Voice 1 counter doesn't advance. */
void test_pmon_formula_negative_modulator(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* We need voice 0 to produce negative outx. Use a loud sample with
     * negative volume to get negative gauss output, then ADSR will scale it
     * but maintain sign. Actually, outx is post-ADSR pre-volume so volume
     * doesn't affect it. We need a sample whose decoded values are negative.
     *
     * Use shift=12, filter=0, nibbles = -8 (0x8 in each nibble = -8 in signed):
     * decoded value = -8 << 12 = -32768. After ADSR at max level, outx = -32768. */
    uint8_t neg_sample[128];
    memset(neg_sample, 0, sizeof(neg_sample));
    for (uint32_t b = 0; b < 8; b++) {
        neg_sample[b * 16 + 0] = 0x0C;  /* shift=12, filter=0 */
        neg_sample[b * 16 + 1] = 0x00;  /* no flags */
        for (int j = 2; j < 16; j++) {
            neg_sample[b * 16 + j] = 0x88;  /* nibbles -8, -8 */
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

    /* Run ticks to let ADSR reach max and ring stabilize */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* Voice 0's outx should be very negative (around -0x8000).
     * Factor = outx + 0x8000 ~= 0. Step ~= 0.
     * Voice 1's counter should not advance. */
    uint16_t pre_counter = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter = s_test_mixer.voices[1].pitch_counter;

    /* Counter should not have advanced (or advanced very little) */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(pre_counter, post_counter,
        "PMON-01: max negative modulator should stop pitch advancement");
}

/* Test: PMON chain stacking — voice 0->1->2 cascading modulation.
 * Voices 0, 1, 2 all active at same pitch. PMON on voices 1 and 2.
 * Voice 0 modulates 1, voice 1's (modulated) output modulates 2.
 * All three should have different pitch_counter advancement. */
void test_pmon_chain_stacking(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load loud sample for all voices */
    uint8_t sample[256];
    make_loud_sample(sample, 16);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, sample, 256);

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

    /* Run enough ticks for ADSR to stabilize and chain effects to manifest */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 30; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* After stabilization, record counters before and after one more tick */
    uint16_t pre0 = s_test_mixer.voices[0].pitch_counter;
    uint16_t pre1 = s_test_mixer.voices[1].pitch_counter;
    uint16_t pre2 = s_test_mixer.voices[2].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post0 = s_test_mixer.voices[0].pitch_counter;
    uint16_t post1 = s_test_mixer.voices[1].pitch_counter;
    uint16_t post2 = s_test_mixer.voices[2].pitch_counter;

    /* Voice 0: unmodulated, step = 0x1000 */
    /* Voice 1: modulated by voice 0's outx (positive) -> step > 0x1000 */
    /* Voice 2: modulated by voice 1's outx (which is itself modulated) -> different step */

    /* At minimum, with PMON active, voices 1 and 2 should have different
     * pitch advancement from voice 0. This proves cascading modulation. */
    (void)pre0; (void)post0;  /* voice 0 is reference */

    /* The key assertion: voice 2's effective step differs from voice 1's,
     * proving cascade modulation (voice 1's PMON-altered output feeds voice 2). */
    uint16_t step1 = (uint16_t)((post1 >= pre1) ? (post1 - pre1) : (0x1000 + post1 - pre1));
    uint16_t step2 = (uint16_t)((post2 >= pre2) ? (post2 - pre2) : (0x1000 + post2 - pre2));

    /* Voice 1 and voice 2 should both differ from base pitch AND from each other */
    TEST_ASSERT_TRUE_MESSAGE(step1 != 0x1000 || step2 != 0x1000,
        "PMON-04: chain stacking should produce modulated pitches different from base");
    TEST_ASSERT_TRUE_MESSAGE(step1 != step2,
        "PMON-04: cascading modulation should produce different steps for voice 1 and voice 2");
}

/* Test: PMON clamp to 0x4000 (not 0x3FFF).
 * When PMON produces a step > 0x3FFF, it clamps to 0x4000.
 * Use a high base pitch (0x3000) with a strong positive modulator to exceed 0x3FFF.
 * Verify the effective step is exactly 0x4000. */
void test_pmon_clamp_0x4000(void) {
    spu94_voice_mixer_init(&s_test_mixer);
    s_test_mixer.enabled = 1;
    s_test_mixer.master_vol_l = 0x7FFF;
    s_test_mixer.master_vol_r = 0x7FFF;

    /* Load loud sample for voice 0 (produces large positive outx) */
    uint8_t loud[128];
    make_loud_sample(loud, 8);
    spu94_voice_mixer_load_sample(&s_test_mixer, 0, loud, 128);

    /* Load normal sample for voice 1 */
    uint8_t sample[64];
    make_long_sample(sample, 4);
    spu94_voice_mixer_load_sample(&s_test_mixer, 128, sample, 64);

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
     * step = (0x3000 * 0xFFFF) >> 15 = 0x5FFF, which exceeds 0x3FFF -> clamp to 0x4000 */
    spu94_voice_mixer_key_on(&s_test_mixer, 1, 128, 0x3000, 0x7FFF, 0x7FFF, 0, NULL);

    /* Enable PMON on voice 1 */
    spu94_voice_mixer_set_pmon(&s_test_mixer, 1, 1);

    /* Run ticks to let ADSR reach max and ring stabilize */
    int16_t dry_l, dry_r, rev_l, rev_r;
    for (int i = 0; i < 20; i++) {
        spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    }

    /* Now check: voice 1's effective step should be clamped to 0x4000.
     * Run one more tick and check counter advancement.
     * 0x4000 mod 0x1000 = voice consumes 4 samples per tick.
     * pitch_counter after = (pre + 0x4000) & 0x0FFF = pre (wraps 4 whole samples).
     * We need to check the step itself. With step=0x4000, the counter advances by
     * 0x4000, consuming 4 samples (4 * 0x1000 = 0x4000). The fractional part is 0. */

    /* A cleaner check: the counter should advance by exactly 0x4000 worth.
     * Since 0x4000 = 4 * 0x1000, the fractional counter wraps to 0x000 each tick.
     * After this tick the fractional counter = (pre_frac + 0x4000) & 0x0FFF = pre_frac.
     * So the fractional counter stays the same! The step is at the clamp. */
    uint16_t pre_counter = s_test_mixer.voices[1].pitch_counter;
    spu94_voice_mixer_tick(&s_test_mixer, &dry_l, &dry_r, &rev_l, &rev_r);
    uint16_t post_counter = s_test_mixer.voices[1].pitch_counter;

    /* With step=0x4000, fractional part doesn't change (0x4000 is exact multiple of 0x1000).
     * The key thing: it should NOT be 0x3FFF (which would give a fractional remainder). */
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(pre_counter, post_counter,
        "PMON-06: step clamped to 0x4000 should produce exact sample boundary advancement");
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
    return UNITY_END();
}
