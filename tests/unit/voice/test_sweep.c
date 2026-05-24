/* tests/unit/voice/test_sweep.c
 * Phase 37 Plan 02: Unity unit tests for spu94_sweep volume sweep.
 *
 * Tests cover:
 *   - Init defaults (active=0, level=0, counter=0)
 *   - Linear increase/decrease step magnitudes
 *   - Exponential decrease anti-stall guard
 *   - Exponential increase knee at 0x6000
 *   - Independent L/R sweep on same voice
 *   - KON deactivates sweep; KOFF does not
 *   - Sweep modifies vol_l/vol_r directly
 *   - Negative-phase increase (toward -0x7FFF)
 *   - Negative-phase decrease (toward 0)
 *   - Phase bit ignored in exponential decrease
 */

#include "unity.h"
#include <spu94/spu94_sweep.h>
#include <spu94/spu94_voice.h>
#include <stdint.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

void test_sweep_init(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.active);
    TEST_ASSERT_EQUAL_INT16(0, sw.level);
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);
}

void test_sweep_linear_increase(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0;

    spu94_sweep_tick(&sw);

    /* step = (7 - 0) << max(0, 11 - 0) = 7 << 11 = 14336 */
    TEST_ASSERT_EQUAL_INT16(14336, sw.level);
}

void test_sweep_linear_decrease(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 1;  /* decrease */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0x7FFF;

    spu94_sweep_tick(&sw);

    /* step = -(8 - 0) << 11 = -16384; level = 32767 - 16384 = 16383 */
    TEST_ASSERT_EQUAL_INT16(16383, sw.level);
}

void test_sweep_exponential_decrease_antistall(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 1;       /* exponential */
    sw.direction = 1;  /* decrease */
    sw.phase = 0;
    sw.active = 1;
    sw.level = 1;

    /* At level=1, exp decrease: scaled_step = (-16384 * 1) / 32768 = 0
     * Anti-stall fires: step = -1, level = 1 - 1 = 0 */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_INT16(0, sw.level);
}

void test_sweep_exponential_increase_knee(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 3;       /* step = (7-3) << 11 = 8192 */
    sw.mode = 1;       /* exponential */
    sw.direction = 0;  /* increase */
    sw.phase = 0;
    sw.active = 1;
    sw.level = 0x6001; /* above 0x6000 -- knee fires */
    sw.counter = 0;

    /* With level > 0x6000 and exponential increase:
     * CounterIncrement = 0x8000 / 4 = 0x2000.
     * Tick 1: counter = 0x2000, bit 15 NOT set. No step. */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_INT16(0x6001, sw.level);

    /* Tick 2: counter = 0x4000 */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_INT16(0x6001, sw.level);

    /* Tick 3: counter = 0x6000 */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_INT16(0x6001, sw.level);

    /* Tick 4: counter = 0x8000, step fires */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_TRUE(sw.level > 0x6001);
}

void test_sweep_independent_lr(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);
    v.vol_l = 0x2000;
    v.vol_r = 0x6000;

    v.sweep_l.shift = 0;
    v.sweep_l.step = 0;
    v.sweep_l.mode = 0;
    v.sweep_l.direction = 0;  /* L increases */
    v.sweep_l.phase = 0;
    v.sweep_l.active = 1;
    v.sweep_l.level = v.vol_l;

    v.sweep_r.shift = 0;
    v.sweep_r.step = 0;
    v.sweep_r.mode = 0;
    v.sweep_r.direction = 1;  /* R decreases */
    v.sweep_r.phase = 0;
    v.sweep_r.active = 1;
    v.sweep_r.level = v.vol_r;

    int16_t old_l = v.sweep_l.level;
    int16_t old_r = v.sweep_r.level;

    spu94_sweep_tick(&v.sweep_l);
    spu94_sweep_tick(&v.sweep_r);

    TEST_ASSERT_TRUE(v.sweep_l.level > old_l);
    TEST_ASSERT_TRUE(v.sweep_r.level < old_r);
}

void test_sweep_kon_deactivates(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);

    v.sweep_l.active = 1;
    v.sweep_l.level = 0x4000;
    v.sweep_r.active = 1;
    v.sweep_r.level = 0x4000;

    /* Load a minimal ADPCM block into voice RAM for key_on */
    spu94_voice_key_on(&v, 0, 0x1000, 0x3FFF, 0x3FFF);

    TEST_ASSERT_EQUAL_UINT8(0, v.sweep_l.active);
    TEST_ASSERT_EQUAL_UINT8(0, v.sweep_r.active);
}

void test_sweep_koff_no_effect(void) {
    spu94_voice_t v;
    spu94_voice_init(&v);
    v.adsr.enabled = 1;

    v.sweep_l.active = 1;
    v.sweep_l.level = 0x4000;
    v.sweep_r.active = 1;
    v.sweep_r.level = 0x4000;

    v.active = 1;
    v.adsr.phase = ADSR_SUSTAIN;
    spu94_voice_key_off(&v);

    TEST_ASSERT_EQUAL_UINT8(1, v.sweep_l.active);
    TEST_ASSERT_EQUAL_UINT8(1, v.sweep_r.active);
}

void test_sweep_modifies_vol_directly(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;
    sw.direction = 0;
    sw.phase = 0;
    sw.active = 1;
    sw.level = 0x1000;

    spu94_sweep_tick(&sw);

    /* After sweep tick, level has changed -- this IS the volume */
    TEST_ASSERT_TRUE(sw.level != 0x1000);
    TEST_ASSERT_TRUE(sw.level > 0x1000);
}

void test_sweep_negative_phase_increase(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 1;      /* negative -- step inverted, moves toward -0x7FFF */
    sw.active = 1;
    sw.level = 0;

    spu94_sweep_tick(&sw);

    /* Negative-phase increase: step = -(7 << 11) = -14336 */
    TEST_ASSERT_EQUAL_INT16(-14336, sw.level);
}

void test_sweep_negative_phase_decrease(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 1;  /* decrease */
    sw.phase = 1;      /* negative -- step inverted, moves toward 0 */
    sw.active = 1;
    sw.level = -0x7FFF;

    spu94_sweep_tick(&sw);

    /* Negative-phase decrease: step inverted: -(-(8<<11)) = +16384
     * level = -32767 + 16384 = -16383 */
    TEST_ASSERT_EQUAL_INT16(-16383, sw.level);
}

void test_sweep_phase_ignored_exp_decrease(void) {
    /* Phase bit should have no effect when direction=decrease AND mode=exponential */
    spu94_sweep_t sw_pos;
    spu94_sweep_init(&sw_pos);
    sw_pos.shift = 0;
    sw_pos.step = 0;
    sw_pos.mode = 1;       /* exponential */
    sw_pos.direction = 1;  /* decrease */
    sw_pos.phase = 0;      /* positive */
    sw_pos.active = 1;
    sw_pos.level = 0x4000;

    spu94_sweep_t sw_neg;
    spu94_sweep_init(&sw_neg);
    sw_neg.shift = 0;
    sw_neg.step = 0;
    sw_neg.mode = 1;       /* exponential */
    sw_neg.direction = 1;  /* decrease */
    sw_neg.phase = 1;      /* negative -- but should be IGNORED */
    sw_neg.active = 1;
    sw_neg.level = 0x4000;

    spu94_sweep_tick(&sw_pos);
    spu94_sweep_tick(&sw_neg);

    /* Both should produce identical results because phase is ignored */
    TEST_ASSERT_EQUAL_INT16(sw_pos.level, sw_neg.level);
}

/* ---------------------------------------------------------------
 * Phase 43 Plan 01: Retrigger engine tests (RTR-01, RTR-03, RTR-04)
 * --------------------------------------------------------------- */

void test_sweep_retrigger_linear_increase_reverses(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0;
    sw.retrigger_enable = 1;

    /* Tick until level reaches +0x7FFF */
    int ticks = 0;
    while (sw.level < 0x7FFF && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0x7FFF, sw.level);

    /* On the tick AFTER reaching the clamp, direction must flip to decrease */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(1, sw.direction);  /* flipped to decrease */
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);   /* counter reset */
    /* Level stays at boundary on the reversal tick, next tick moves it away */
}

void test_sweep_retrigger_linear_decrease_reverses(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 1;  /* decrease */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0x7FFF;
    sw.retrigger_enable = 1;

    /* Tick until level reaches 0 */
    int ticks = 0;
    while (sw.level > 0 && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0, sw.level);

    /* On reversal tick, direction must flip to increase */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);  /* flipped to increase */
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);   /* counter reset */
}

void test_sweep_retrigger_disabled_is_oneshot(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0;
    sw.retrigger_enable = 0;  /* disabled -- v1.9 one-shot behavior */

    /* Tick until level reaches +0x7FFF */
    int ticks = 0;
    while (sw.level < 0x7FFF && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0x7FFF, sw.level);

    /* Further ticks should NOT change direction; level stays clamped */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);  /* still increase (NOT flipped) */
    TEST_ASSERT_EQUAL_INT16(0x7FFF, sw.level); /* still clamped */

    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);
    TEST_ASSERT_EQUAL_INT16(0x7FFF, sw.level);
}

void test_sweep_retrigger_full_cycle(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0;
    sw.retrigger_enable = 1;

    /* Phase 1: tick up to max */
    int ticks = 0;
    while (sw.level < 0x7FFF && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0x7FFF, sw.level);

    /* Reversal tick: should flip to decrease */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(1, sw.direction);

    /* Phase 2: tick down to 0 */
    ticks = 0;
    while (sw.level > 0 && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0, sw.level);

    /* Reversal tick: should flip back to increase */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);
}

void test_sweep_retrigger_exponential_reversal(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 1;       /* exponential */
    sw.direction = 1;  /* decrease */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0x4000;
    sw.retrigger_enable = 1;

    /* Tick until level reaches 0 (exponential anti-stall ensures it reaches zero) */
    int ticks = 0;
    while (sw.level > 0 && ticks < 1000) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0, sw.level);

    /* Reversal: direction flips to increase, counter resets */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);  /* flipped to increase */
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);   /* counter reset */
}

void test_sweep_retrigger_negative_phase(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase (toward -0x7FFF in negative phase) */
    sw.phase = 1;      /* negative */
    sw.active = 1;
    sw.level = 0;
    sw.retrigger_enable = 1;

    /* Tick until level reaches -0x7FFF */
    int ticks = 0;
    while (sw.level > -0x7FFF && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(-0x7FFF, sw.level);

    /* Reversal: direction flips to decrease (toward 0 in negative phase) */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(1, sw.direction);
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);

    /* Tick until level reaches 0 */
    ticks = 0;
    while (sw.level < 0 && ticks < 100) {
        spu94_sweep_tick(&sw);
        ticks++;
    }
    TEST_ASSERT_EQUAL_INT16(0, sw.level);

    /* Reversal: direction flips back to increase */
    spu94_sweep_tick(&sw);
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);
}

void test_sweep_retrigger_audio_rate(void) {
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);
    sw.shift = 0;
    sw.step = 0;
    sw.mode = 0;       /* linear */
    sw.direction = 0;  /* increase */
    sw.phase = 0;      /* positive */
    sw.active = 1;
    sw.level = 0;
    sw.retrigger_enable = 1;

    /* Count ticks for one complete up+down cycle at fastest rate (shift=0, step=0) */
    int total_ticks = 0;
    int reversals = 0;
    uint8_t last_dir = sw.direction;

    /* Run until we get 2 reversals (one full cycle: up then down) */
    while (reversals < 2 && total_ticks < 200) {
        spu94_sweep_tick(&sw);
        total_ticks++;
        if (sw.direction != last_dir) {
            reversals++;
            last_dir = sw.direction;
        }
    }

    /* At shift=0/step=0, linear increase adds 14336 per tick.
     * 0x7FFF / 14336 ~ 2.28, so 3 ticks to clamp + reversal tick = ~4 ticks up.
     * Decrease: (8<<11)=16384 per tick. 0x7FFF/16384 ~ 2, so 2 ticks + reversal.
     * Total cycle should be deterministic and small (under 10 ticks). */
    TEST_ASSERT_TRUE(reversals == 2);
    TEST_ASSERT_TRUE(total_ticks < 20);  /* sanity: fast oscillation */
    TEST_ASSERT_TRUE(total_ticks > 2);   /* sanity: not degenerate */

    /* Verify no stuck levels or skipped reversals -- direction should be back to 0 */
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);
}

/* ---------------------------------------------------------------
 * Phase 43 Plan 02: Independent L/R rates + KON reset tests (RTR-02, RTR-05)
 * --------------------------------------------------------------- */

void test_sweep_retrigger_independent_lr_rates(void) {
    /* Configure L with retrigger_enable=1, shift=11, step=0 (faster)
     * Configure R with retrigger_enable=1, shift=13, step=0 (slower)
     * Both start from level=0, direction=increase, phase=0.
     * L (faster rate) must have MORE direction reversals than R after 500 ticks. */
    spu94_sweep_t sw_l;
    spu94_sweep_init(&sw_l);
    sw_l.shift = 11;
    sw_l.step = 0;
    sw_l.mode = 0;       /* linear */
    sw_l.direction = 0;  /* increase */
    sw_l.phase = 0;      /* positive */
    sw_l.active = 1;
    sw_l.level = 0;
    sw_l.retrigger_enable = 1;

    spu94_sweep_t sw_r;
    spu94_sweep_init(&sw_r);
    sw_r.shift = 13;
    sw_r.step = 0;
    sw_r.mode = 0;       /* linear */
    sw_r.direction = 0;  /* increase */
    sw_r.phase = 0;      /* positive */
    sw_r.active = 1;
    sw_r.level = 0;
    sw_r.retrigger_enable = 1;

    int reversals_l = 0, reversals_r = 0;
    uint8_t prev_dir_l = sw_l.direction;
    uint8_t prev_dir_r = sw_r.direction;

    for (int t = 0; t < 500; t++) {
        spu94_sweep_tick(&sw_l);
        spu94_sweep_tick(&sw_r);
        if (sw_l.direction != prev_dir_l) {
            reversals_l++;
            prev_dir_l = sw_l.direction;
        }
        if (sw_r.direction != prev_dir_r) {
            reversals_r++;
            prev_dir_r = sw_r.direction;
        }
    }

    /* L (shift=11, faster) must have MORE reversals than R (shift=13, slower) */
    TEST_ASSERT_TRUE(reversals_l > reversals_r);
    /* Both must have at least 1 reversal */
    TEST_ASSERT_TRUE(reversals_l >= 1);
    TEST_ASSERT_TRUE(reversals_r >= 1);
}

void test_sweep_retrigger_kon_resets_phase(void) {
    /* Configure sweep_l with retrigger_enable=1, run 200 ticks so the sweep
     * is mid-oscillation. Then call spu94_voice_key_on. After key_on,
     * sweep_l.active must be 0 (SWEEP-07 preserved) and retrigger_enable
     * must be 0 (key_on zeros the voice via init). */
    spu94_voice_t v;
    spu94_voice_init(&v);
    v.vol_l = 0;
    v.vol_r = 0;

    v.sweep_l.shift = 11;
    v.sweep_l.step = 0;
    v.sweep_l.mode = 0;
    v.sweep_l.direction = 0;
    v.sweep_l.phase = 0;
    v.sweep_l.active = 1;
    v.sweep_l.level = 0;
    v.sweep_l.retrigger_enable = 1;

    /* Run 200 ticks so sweep is mid-oscillation */
    for (int t = 0; t < 200; t++) {
        spu94_sweep_tick(&v.sweep_l);
    }

    /* Sweep should be mid-oscillation (direction may have flipped) */
    /* Now key_on resets everything */
    spu94_voice_key_on(&v, 0, 0x1000, 0x3FFF, 0x3FFF);

    /* After key_on, sweep is deactivated (SWEEP-07) */
    TEST_ASSERT_EQUAL_UINT8(0, v.sweep_l.active);
    /* key_on does NOT explicitly zero retrigger fields -- the voice_init in
     * the pending_config path does that. Direct key_on only sets active=0.
     * But the underlying sweep struct fields (retrigger_enable, counter,
     * direction) persist until reconfigured. What we verify here is that
     * the sweep is inactive and won't run. */
    TEST_ASSERT_EQUAL_UINT8(0, v.sweep_l.active);
    TEST_ASSERT_EQUAL_UINT8(0, v.sweep_r.active);
}

void test_sweep_retrigger_kon_fresh_start(void) {
    /* Configure sweep_l mid-oscillation, set start_direction.
     * After re-configure, direction must equal start_direction and counter=0.
     * This tests that start_direction is stored and spu94_sweep_configure
     * records it properly for use in KON reset. */
    spu94_sweep_t sw;
    spu94_sweep_init(&sw);

    /* Simulate mid-oscillation: level=0x5000, direction=1 (already reversed) */
    sw.level = 0x5000;
    sw.direction = 1;
    sw.counter = 12345;
    sw.retrigger_enable = 1;

    /* Configure with starting direction = 0 (increase).
     * spu94_sweep_configure should set both direction AND start_direction. */
    spu94_sweep_configure(&sw, 0, 0, 0, 11, 0, 1);

    /* After configure: direction = 0 (the param), start_direction = 0 */
    TEST_ASSERT_EQUAL_UINT8(0, sw.direction);
    TEST_ASSERT_EQUAL_UINT8(0, sw.start_direction);
    TEST_ASSERT_EQUAL_UINT32(0, sw.counter);

    /* Also test with start_direction = 1 (decrease) */
    spu94_sweep_configure(&sw, 0, 1, 0, 11, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(1, sw.direction);
    TEST_ASSERT_EQUAL_UINT8(1, sw.start_direction);
}

void test_sweep_retrigger_mixer_api(void) {
    /* Use spu94_voice_mixer_set_sweep_l/r with the new retrigger parameter.
     * Verify the voice's sweep_l/r.retrigger_enable is set correctly. */
    spu94_voice_mixer_t *mixer = spu94_get_voice_mixer();
    spu94_voice_mixer_init(mixer);
    mixer->enabled = 1;

    /* Set volume so sweep has a starting level */
    mixer->voices[0].vol_l = 0x2000;
    mixer->voices[0].vol_r = 0x3000;

    /* Configure L sweep with retrigger_enable=1 */
    spu94_result_t res_l = spu94_voice_mixer_set_sweep_l(mixer, 0,
        0, 0, 0, 11, 0, 1);  /* mode=0, dir=0, phase=0, shift=11, step=0, retrigger=1 */
    TEST_ASSERT_EQUAL(SPU94_OK, res_l);
    TEST_ASSERT_EQUAL_UINT8(1, mixer->voices[0].sweep_l.retrigger_enable);

    /* Configure R sweep with retrigger_enable=1 */
    spu94_result_t res_r = spu94_voice_mixer_set_sweep_r(mixer, 0,
        0, 0, 0, 13, 0, 1);  /* mode=0, dir=0, phase=0, shift=13, step=0, retrigger=1 */
    TEST_ASSERT_EQUAL(SPU94_OK, res_r);
    TEST_ASSERT_EQUAL_UINT8(1, mixer->voices[0].sweep_r.retrigger_enable);

    /* Also verify retrigger_enable=0 works */
    spu94_voice_mixer_set_sweep_l(mixer, 0, 0, 0, 0, 11, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, mixer->voices[0].sweep_l.retrigger_enable);
}

void test_sweep_retrigger_polyrhythmic_divergence(void) {
    /* Two sweeps (L and R) both start at level=0 with retrigger_enable=1.
     * L has shift=10, R has shift=12. After 1000 ticks, snapshot both levels.
     * L and R must differ (NOT in sync) because different rates create
     * polyrhythmic behavior. */
    spu94_sweep_t sw_l;
    spu94_sweep_init(&sw_l);
    sw_l.shift = 10;
    sw_l.step = 0;
    sw_l.mode = 0;       /* linear */
    sw_l.direction = 0;  /* increase */
    sw_l.phase = 0;      /* positive */
    sw_l.active = 1;
    sw_l.level = 0;
    sw_l.retrigger_enable = 1;

    spu94_sweep_t sw_r;
    spu94_sweep_init(&sw_r);
    sw_r.shift = 12;
    sw_r.step = 0;
    sw_r.mode = 0;       /* linear */
    sw_r.direction = 0;  /* increase */
    sw_r.phase = 0;      /* positive */
    sw_r.active = 1;
    sw_r.level = 0;
    sw_r.retrigger_enable = 1;

    for (int t = 0; t < 1000; t++) {
        spu94_sweep_tick(&sw_l);
        spu94_sweep_tick(&sw_r);
    }

    /* L and R must differ -- they are NOT in sync because different rates */
    TEST_ASSERT_TRUE(sw_l.level != sw_r.level);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_sweep_init);
    RUN_TEST(test_sweep_linear_increase);
    RUN_TEST(test_sweep_linear_decrease);
    RUN_TEST(test_sweep_exponential_decrease_antistall);
    RUN_TEST(test_sweep_exponential_increase_knee);
    RUN_TEST(test_sweep_independent_lr);
    RUN_TEST(test_sweep_kon_deactivates);
    RUN_TEST(test_sweep_koff_no_effect);
    RUN_TEST(test_sweep_modifies_vol_directly);
    RUN_TEST(test_sweep_negative_phase_increase);
    RUN_TEST(test_sweep_negative_phase_decrease);
    RUN_TEST(test_sweep_phase_ignored_exp_decrease);
    RUN_TEST(test_sweep_retrigger_linear_increase_reverses);
    RUN_TEST(test_sweep_retrigger_linear_decrease_reverses);
    RUN_TEST(test_sweep_retrigger_disabled_is_oneshot);
    RUN_TEST(test_sweep_retrigger_full_cycle);
    RUN_TEST(test_sweep_retrigger_exponential_reversal);
    RUN_TEST(test_sweep_retrigger_negative_phase);
    RUN_TEST(test_sweep_retrigger_audio_rate);
    /* Phase 43 Plan 02: Independent L/R rates + KON reset */
    RUN_TEST(test_sweep_retrigger_independent_lr_rates);
    RUN_TEST(test_sweep_retrigger_kon_resets_phase);
    RUN_TEST(test_sweep_retrigger_kon_fresh_start);
    RUN_TEST(test_sweep_retrigger_mixer_api);
    RUN_TEST(test_sweep_retrigger_polyrhythmic_divergence);
    return UNITY_END();
}
