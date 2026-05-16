---
phase: 28-adsr-envelope
plan: 01
type: execute
wave: 1
depends_on: [27-01]
files_modified:
  - include/spu94/spu94_adsr.h
  - include/spu94/spu94_voice.h
  - src/spu94/spu94_adsr.c
  - src/spu94/spu94_voice.c
  - src/spu94/CMakeLists.txt
  - tests/unit/voice/test_voice_tick.c
autonomous: true
requirements: [ADSR-01, ADSR-02, ADSR-03, ADSR-04, ADSR-05, ADSR-06]

must_haves:
  truths:
    - "A voice keyed on rises from 0 to 0x7FFF during Attack, using counter-accumulate steps not a fixed ramp"
    - "Attack steps halve in size once the envelope level crosses 0x6000 (fake exponential knee)"
    - "After Attack, the level decays exponentially toward (N+1)*0x800 — each step is proportional to current level"
    - "The voice holds at the sustain plateau until KOFF is received"
    - "KOFF triggers Release: exponential decay toward zero; when level hits 0 the voice goes silent"
    - "With ADSR registers zeroed, voice output is constant-amplitude — envelope module is additive and isolated"
  artifacts:
    - path: "include/spu94/spu94_adsr.h"
      provides: "spu94_adsr_state_t struct + spu94_adsr_tick() declaration"
      exports: [spu94_adsr_state_t, spu94_adsr_tick, spu94_adsr_init, spu94_adsr_key_on, spu94_adsr_key_off]
    - path: "src/spu94/spu94_adsr.c"
      provides: "Counter-accumulate ADSR engine implementation"
      min_lines: 120
    - path: "include/spu94/spu94_voice.h"
      provides: "spu94_voice_t extended with adsr field"
      contains: "spu94_adsr_state_t adsr"
    - path: "src/spu94/spu94_voice.c"
      provides: "spu94_voice_tick calls spu94_adsr_tick; spu94_voice_key_off enters Release"
  key_links:
    - from: "src/spu94/spu94_voice.c:spu94_voice_tick"
      to: "src/spu94/spu94_adsr.c:spu94_adsr_tick"
      via: "called after Gaussian interpolation, before volume scale"
      pattern: "spu94_adsr_tick"
    - from: "src/spu94/spu94_adsr.c"
      to: "adsr_level"
      via: "output sample *= adsr_level / 0x7FFF (Q15 multiply)"
      pattern: "q15_mul_truncate.*adsr"
    - from: "src/spu94/spu94_voice.c:spu94_voice_key_off"
      to: "adsr.phase = ADSR_RELEASE"
      via: "spu94_adsr_key_off()"
      pattern: "spu94_adsr_key_off"
---

<objective>
Add the PS1-faithful ADSR envelope generator to each voice. After this phase,
keying on a voice produces the characteristic PS1 attack-decay-sustain shape,
and KOFF initiates release to silence rather than the current immediate cutoff.

Purpose: ADSR is the sonic identity of the SPU. Without it, every voice plays
at constant full amplitude — useful for debugging, wrong as a sampler. The PS1
envelope is also architecturally unusual (counter-accumulate, not fixed-rate
ramp), and the fake-exponential attack and true-exponential decay are the
quirks that give the hardware its character.

Output:
  - New module: spu94_adsr.h / spu94_adsr.c
  - spu94_voice_t gains an embedded spu94_adsr_state_t field
  - spu94_voice_tick applies the envelope between interpolation and volume
  - spu94_voice_key_off now enters ADSR Release instead of immediate silence
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/ROADMAP.md
@.planning/REQUIREMENTS.md
@.planning/phases/27-single-voice-playback/27-PLAN-SUMMARY.md

<!-- Key interfaces Phase 28 builds against -->
<interfaces>
From include/spu94/spu94_voice.h (current — Phase 27 output):

```c
typedef struct {
    uint32_t  current_addr;
    uint32_t  sample_start_addr;
    uint16_t  pitch;
    uint16_t  pitch_counter;
    spu94_adpcm_state adpcm_state;
    int16_t   decode_buf[28];
    uint8_t   decode_buf_pos;
    uint8_t   has_block;
    int16_t   gauss_ring[4];
    uint8_t   gauss_ring_pos;
    int16_t   vol_l;
    int16_t   vol_r;
    uint8_t   active;
} spu94_voice_t;

void spu94_voice_init(spu94_voice_t *v);
void spu94_voice_key_on(spu94_voice_t *v, uint32_t start_addr,
                        uint16_t pitch, int16_t vol_l, int16_t vol_r);
void spu94_voice_key_off(spu94_voice_t *v);   /* Phase 28 replaces body */
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      int16_t *out_l, int16_t *out_r);
```

From src/spu94/spu94_voice.c (spu94_voice_tick processing order):
  Step 1 — decode block if needed
  Step 2 — Gaussian interpolation → gauss_out (int16_t)
  Step 3 — apply per-voice volume:  *out_l = q15_mul_truncate(gauss_out, v->vol_l)
  Step 4 — advance pitch counter and push samples into ring

Phase 28 inserts between Steps 2 and 3:
  Step 2.5 — ADSR tick: gauss_out = q15_mul_truncate(gauss_out, adsr_level)

From include/spu94/spu94_q15.h:
```c
int16_t q15_mul_truncate(int16_t a, int16_t b);  /* (a*b)>>15 with sign */
int16_t sat_s16(int32_t v);
```
</interfaces>
</context>

<tasks>

<!-- ================================================================
     TASK 1: spu94_adsr.h — define the ADSR state struct and API
     ================================================================ -->
<task type="auto" tdd="true">
  <name>Task 1: Define spu94_adsr_state_t and implement spu94_adsr.c</name>
  <files>include/spu94/spu94_adsr.h, src/spu94/spu94_adsr.c, src/spu94/CMakeLists.txt</files>
  <behavior>
    - spu94_adsr_tick with all registers zero returns adsr_level == 0x7FFF every call (bypass / passthrough test)
    - After key_on with attack_rate=0 (fastest), adsr_level reaches 0x7FFF within 1 tick (ShiftValue=0 means one step per tick)
    - Attack level crosses 0x6000: next step size halves (fake exponential knee fires once)
    - After attack completes, phase transitions to ADSR_DECAY
    - Decay step is proportional to current level: at level 0x7FFF the step is full; at level 0x1000 the step is small
    - Decay stops when level drops to or below (sustain_level_reg+1)*0x800; phase transitions to ADSR_SUSTAIN
    - sustain_level_reg=0 → sustain target is 0x800 (not zero — M2 prevention)
    - spu94_adsr_key_off() on any phase transitions immediately to ADSR_RELEASE
    - Release decays level toward zero exponentially; when level reaches 0, returns phase ADSR_OFF
    - With adsr_enabled=0 flag on the state, tick always returns adsr_level == 0x7FFF (bypass mode)
  </behavior>
  <action>
Create include/spu94/spu94_adsr.h with:

  typedef enum {
      ADSR_OFF = 0,      /* voice is silent; skip further ticking */
      ADSR_ATTACK,
      ADSR_DECAY,
      ADSR_SUSTAIN,
      ADSR_RELEASE
  } spu94_adsr_phase_t;

  typedef struct {
      /* Register fields — loaded from voice register values at key_on */
      uint8_t  attack_shift;        /* 0..31 */
      uint8_t  attack_step;         /* 0..3 (maps to +7, +6, +5, +4) */
      uint8_t  attack_exp;          /* 1 = fake exponential above 0x6000 */
      uint8_t  decay_shift;         /* 0..15 */
      uint8_t  sustain_level;       /* 0..15; target = (val+1)*0x800 */
      uint8_t  sustain_shift;       /* 0..31 */
      uint8_t  sustain_step;        /* 0..3 */
      uint8_t  sustain_exp;         /* 1 = exponential */
      uint8_t  sustain_dir;         /* 0 = increase, 1 = decrease */
      uint8_t  release_shift;       /* 0..31 */
      uint8_t  release_exp;         /* 1 = exponential */

      /* Runtime state */
      spu94_adsr_phase_t phase;
      int16_t  level;               /* current envelope level 0..0x7FFF */
      uint32_t counter;             /* accumulator; step fires when bit 15 set */

      /* Control */
      uint8_t  enabled;             /* 0 = bypass (level always 0x7FFF) */
  } spu94_adsr_state_t;

  void    spu94_adsr_init(spu94_adsr_state_t *a);
  void    spu94_adsr_key_on(spu94_adsr_state_t *a);   /* reset to ATTACK, level=0 */
  void    spu94_adsr_key_off(spu94_adsr_state_t *a);  /* transition to RELEASE */
  int16_t spu94_adsr_tick(spu94_adsr_state_t *a);     /* return current level */

Implement src/spu94/spu94_adsr.c. The core algorithm per tick (ADSR-02):

  CounterIncrement = 0x8000 >> max(0, ShiftValue - 11)
  AdsrStep = 7 - StepValue   (for attack/sustain; decay StepValue is always 3 giving AdsrStep=4, then negated to -8)

  Attack (ADSR-03 fake exponential):
    - base AdsrStep is positive: +(7 - attack_step) << max(0, 11 - attack_shift)
    - CounterIncrement = 0x8000 >> max(0, attack_shift - 11)
    - IF attack_exp AND level > 0x6000: CounterIncrement /= 4   (slows the rate above the knee)
    - Add CounterIncrement to counter. If bit 15 set after add: apply step, clear bit 15 (keep lower bits).
    - Clamp level to [0, 0x7FFF]. When level >= 0x7FFF: transition to ADSR_DECAY.

  Decay (ADSR-04 real exponential, always):
    - AdsrStep = -8 << max(0, 11 - decay_shift)   (always negative, always exponential)
    - CounterIncrement = 0x8000 >> max(0, decay_shift - 11)
    - Before applying step: AdsrStep = (int32_t)AdsrStep * level / 0x8000  (proportional to current level)
    - When level <= sustain_target: transition to ADSR_SUSTAIN. Do not go below sustain_target.

  Sustain:
    - Follows attack or decay formula depending on sustain_dir and sustain_exp.
    - Runs indefinitely until spu94_adsr_key_off() is called.

  Release:
    - AdsrStep = -(7 - 0) << max(0, 11 - release_shift) = -7 << max(0, 11 - release_shift)
      (nocash: release step value is fixed; direction is always decrease)
    - If release_exp: apply proportional formula AdsrStep = AdsrStep * level / 0x8000
    - When level <= 0: set level = 0, transition to ADSR_OFF.

  Bypass mode: if !a->enabled, return 0x7FFF immediately without touching any state.

  All multiplies use int32_t intermediates to avoid overflow (C3 prevention).
  Counter is uint32_t; bit-15 trigger means: after counter += CounterIncrement, if
  counter & 0x8000 != 0, fire step and clear bit 15 (counter &= ~0x8000).
  This matches nocash's "bit-15 set causes step" description exactly.

Add spu94_adsr.c to src/spu94/CMakeLists.txt target_sources.

RT-safety: no malloc, no locks, no syscalls. Verify: nm -u src/spu94/CMakeFiles/spu94.dir/spu94_adsr.c.o
must not list malloc, free, printf, fopen.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb/build" && cmake --build . --target spu94 -j4 2>&1 | tail -5 && echo BUILD_OK</automated>
  </verify>
  <done>
    spu94_adsr.h and spu94_adsr.c compile without warnings under -Wall -Wextra.
    All behavior cases listed above are testable via unit tests written before implementation.
    nm -u on the .o file shows no heap/IO symbols.
  </done>
</task>

<!-- ================================================================
     TASK 2: Wire ADSR into spu94_voice_t and spu94_voice_tick
     ================================================================ -->
<task type="auto" tdd="true">
  <name>Task 2: Extend spu94_voice_t and wire ADSR into voice tick</name>
  <files>include/spu94/spu94_voice.h, src/spu94/spu94_voice.c, tests/unit/voice/test_voice_tick.c</files>
  <behavior>
    - spu94_voice_init zeros the embedded adsr field and calls spu94_adsr_init
    - spu94_voice_key_on calls spu94_adsr_key_on (resets to ATTACK, level=0)
    - spu94_voice_key_off calls spu94_adsr_key_off (enters RELEASE, no longer immediate silence)
    - spu94_voice_tick with adsr.enabled=0 produces same output as Phase 27 (passthrough regression)
    - spu94_voice_tick with adsr.enabled=1, fast attack: output amplitude ramps up over ticks, not constant
    - spu94_voice_tick: when adsr.phase == ADSR_OFF, sets v->active=0 and outputs silence (ADSR-06)
    - spu94_voice_tick: gauss_out is scaled by adsr level between interpolation and volume multiply
  </behavior>
  <action>
Extend include/spu94/spu94_voice.h:
  - Add #include of spu94_adsr.h
  - Add field to spu94_voice_t: spu94_adsr_state_t adsr;
  - Update spu94_voice_key_on signature to accept adsr register values so callers can configure
    envelope. Add a convenience parameter struct or accept the adsr registers directly. The
    simplest approach: callers configure voice->adsr fields before calling key_on, and key_on
    calls spu94_adsr_key_on(). Document this in the header comment.
  - Update spu94_voice_key_off comment: now enters ADSR Release, not immediate silence.

Modify src/spu94/spu94_voice.c:

  spu94_voice_init: add spu94_adsr_init(&v->adsr) call.

  spu94_voice_key_on: add spu94_adsr_key_on(&v->adsr) at the end of the function
  (after all other resets). This sets phase=ADSR_ATTACK, level=0.

  spu94_voice_key_off: replace v->active=0 with:
    spu94_adsr_key_off(&v->adsr);
    /* voice remains active; ADSR_OFF transition in tick will clear active */

  spu94_voice_tick — insert between Step 2 (Gaussian interpolation) and Step 3 (volume):

    /* STEP 2.5 — ADSR envelope (ADSR-01..06)
     * spu94_adsr_tick returns current level 0..0x7FFF.
     * Level is applied as a Q15 multiply against the Gaussian output.
     * When ADSR transitions to OFF, voice is silenced immediately. */
    {
        int16_t adsr_level = spu94_adsr_tick(&v->adsr);
        if (v->adsr.phase == ADSR_OFF) {
            v->active = 0;
            *out_l = 0;
            *out_r = 0;
            return;  /* skip Steps 3 and 4 — voice is done */
        }
        gauss_out = q15_mul_truncate(gauss_out, adsr_level);
    }

  Note: the gauss_out variable must be declared mutable (not const) to allow the
  in-place ADSR scale. It is already a local int16_t in the existing code block.
  Move it out of the inner block scope if needed to make it accessible at Step 2.5.

Extend tests/unit/voice/test_voice_tick.c:
  Add tests covering the behavior cases listed above:
    - test_adsr_bypass_matches_phase27: adsr.enabled=0; tick output matches pre-Phase-28 behavior
    - test_adsr_off_silences_voice: set adsr.phase=ADSR_OFF externally; next tick sets active=0
    - test_key_off_enters_release: key_off sets phase=ADSR_RELEASE, not immediate active=0
    - test_adsr_attack_ramps_output: fast attack; output amplitude increases over successive ticks

All existing 21 voice tests must still pass.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb/build" && cmake --build . -j4 2>&1 | tail -5 && ctest --test-dir . -R voice_tick_unit --output-on-failure 2>&1 | tail -20</automated>
  </verify>
  <done>
    spu94_voice_t compiles with the new adsr field.
    All pre-existing 21 voice tick tests still pass.
    New ADSR integration tests pass.
    spu94_voice_key_off no longer sets active=0 directly — it enters ADSR Release.
    When ADSR phase reaches OFF, spu94_voice_tick clears active and returns silence.
  </done>
</task>

<!-- ================================================================
     TASK 3: Unit tests for ADSR correctness (C3 pitfall prevention)
     ================================================================ -->
<task type="auto" tdd="true">
  <name>Task 3: ADSR unit tests — counter mechanism, exponential modes, phase transitions</name>
  <files>tests/unit/voice/test_adsr.c, tests/unit/voice/CMakeLists.txt</files>
  <behavior>
    - Counter accumulates each tick; step fires when bit 15 is set (not every tick at ShiftValue > 0)
    - At ShiftValue=0: CounterIncrement=0x8000, step fires every tick
    - At ShiftValue=11: CounterIncrement=1, step fires every 32768 ticks
    - Fake exponential knee: attack step halves when level crosses 0x6000 (strictly greater than, per nocash)
    - Real exponential decay: at level=0x7FFF step is full; at level=0x3FFF step is approximately half
    - Sustain target with sustain_level_reg=0 is 0x800 (not zero — M2)
    - Sustain target with sustain_level_reg=15 is 0x8000 (clamped by level ceiling)
    - spu94_adsr_key_off transitions to RELEASE from any phase (ATTACK, DECAY, SUSTAIN)
    - Release reaches level=0 then ADSR_OFF; does not produce negative levels
    - Bypass mode (enabled=0): tick always returns 0x7FFF, no state change
  </behavior>
  <action>
Create tests/unit/voice/test_adsr.c using the Unity test framework (same pattern as
test_voice_tick.c — include unity.h, implement setUp/tearDown, add test functions,
implement main calling RUN_TEST for each).

Test cases to implement:

  test_counter_fires_every_tick_at_shift0:
    Set attack_shift=0. Call spu94_adsr_tick() in a loop. Verify level increases
    on EVERY tick (because CounterIncrement=0x8000 means bit 15 is set immediately).

  test_counter_fires_slowly_at_shift11:
    Set attack_shift=11 (CounterIncrement=1). Call tick 32767 times. Verify level
    has NOT yet changed (counter has not yet hit bit 15). Call once more (tick 32768).
    Verify level increased by exactly one step.

  test_fake_exponential_knee_at_0x6000:
    Set attack_shift=0, attack_exp=1. Run ticks until level > 0x6000.
    Record the step increment applied below 0x6000.
    Record the step increment applied above 0x6000.
    Verify the above-0x6000 increment is exactly 1/4 of below (CounterIncrement /= 4
    means we need 4x more ticks per step, i.e. effective step rate drops 4x).
    Verify this is piecewise — the halving only applies when level > 0x6000, not at exactly 0x6000.

  test_real_exponential_decay_proportional:
    Set a fast decay (decay_shift=0). Record level at two points during decay.
    Verify that the step applied at higher level is proportionally larger than at lower level.
    Specifically: step_at_0x7FFF / step_at_0x3FFF ≈ 2.0 (within integer rounding).

  test_sustain_level_zero_is_0x800:
    Set sustain_level=0. Run attack to completion, run decay. Verify decay stops
    at level == 0x800 (not 0).

  test_sustain_level_15_is_0x8000:
    Set sustain_level=15. Verify sustain target = (15+1)*0x800 = 0x8000. Because
    max level is 0x7FFF, decay should reach 0x7FFF and stop immediately (attack
    already there; sustained at max). Or: verify the computed target is 0x8000 by
    running a decay from above — with sustain=15, decay should stop at 0x7FFF.

  test_key_off_from_attack_enters_release:
    Start in ATTACK. Call spu94_adsr_key_off(). Verify phase == ADSR_RELEASE.

  test_key_off_from_decay_enters_release:
    Set level partway through decay phase. Call key_off. Verify phase == ADSR_RELEASE.

  test_release_reaches_off:
    Set release_shift=0 (fastest). Call key_off from SUSTAIN. Tick until phase == ADSR_OFF.
    Verify level == 0 at that point.

  test_bypass_always_returns_7FFF:
    Set enabled=0. Verify every tick returns 0x7FFF regardless of phase.
    Verify phase does NOT change (still ADSR_OFF) — no state mutation in bypass.

Add the new test executable to tests/unit/voice/CMakeLists.txt, following the
pattern of the existing voice_tick_unit and sample_loader_unit targets.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb/build" && cmake --build . -j4 2>&1 | tail -5 && ctest --test-dir . -R adsr_unit --output-on-failure 2>&1 | tail -30</automated>
  </verify>
  <done>
    test_adsr executable exists and all test cases pass.
    Total test count increases from 116 (Phase 27 exit) by the number of new ADSR tests.
    All pre-existing tests continue to pass (ctest --output-on-failure shows 0 failures).
  </done>
</task>

<!-- ================================================================
     TASK 4: Full-pipeline smoke test — listen to ADSR shape via spu94_process
     ================================================================ -->
<task type="auto">
  <name>Task 4: Smoke test — ADSR shape through spu94_process with known parameters</name>
  <files>tests/unit/voice/test_voice_tick.c</files>
  <action>
Add one integration-level test to test_voice_tick.c that runs a full voice through
spu94_voice_tick for several hundred ticks with a known ADSR configuration, and
asserts the envelope shape at key points.

The test is self-contained (does not call spu94_process — it drives spu94_voice_tick
directly with a synthetic single-block sample):

  1. Allocate a 512-byte voice RAM buffer on the stack.
  2. Write one valid ADPCM block into it: full-amplitude DC (all nibbles 0x7, filter 0,
     shift 0 — produces the maximum positive decoded value for every sample in the block).
     This means every decoded sample from this block will be near 0x7FFF, so the
     Gaussian ring stabilizes quickly at a known constant value.
  3. Configure voice: pitch=0x1000, vol_l=0x7FFF, vol_r=0x7FFF.
  4. Configure ADSR: attack_shift=0, attack_exp=1, attack_step=0 (fastest attack),
     decay_shift=0, sustain_level=7 (target = 8*0x800 = 0x4000),
     sustain_shift=31 (sustain forever), release_shift=0.
  5. Call spu94_voice_key_on.
  6. Tick 10 times. Verify *out_l is increasing (ADSR in attack, level rising).
  7. Tick until stable (wait for ADSR_SUSTAIN). Verify *out_l is approximately
     0x4000/0x7FFF * expected_gauss_output (within ±256 for rounding).
  8. Call spu94_voice_key_off.
  9. Record *out_l immediately. Tick 10 more times. Verify *out_l is DECREASING
     (release in progress).
  10. Tick until voice.active == 0. Verify *out_l == 0 at that point.

Name this test: test_adsr_full_pipeline_attack_sustain_release.

This test does not replace golden-file regression (that is a Phase 31 / verification
concern). It proves the ADSR wiring through voice tick produces the right qualitative
shape with observable intermediate values.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb/build" && cmake --build . -j4 2>&1 | tail -5 && ctest --test-dir . -R voice_tick_unit --output-on-failure 2>&1 | tail -20</automated>
  </verify>
  <done>
    test_adsr_full_pipeline_attack_sustain_release passes.
    The test demonstrates: rising amplitude during attack, stable plateau during sustain,
    falling amplitude during release, silence when active=0.
    All other tests continue to pass.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| caller → spu94_adsr_tick | Caller supplies register values at key_on; malformed shift/step values must not produce UB |
| voice_ram → decoder | Voice RAM is caller-provided; bounds already checked in Phase 27 |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-28-01 | Tampering | adsr counter integer arithmetic | mitigate | Use int32_t intermediates for all multiply-accumulate; clamp level to [0, 0x7FFF] after every step to prevent signed overflow propagating into output |
| T-28-02 | Denial of Service | infinite sustain at shift=31 | accept | Intended hardware behavior — ShiftValue=31 means CounterIncrement≈0; voice holds sustain indefinitely until KOFF; no CPU pathology (counter still accumulates, just never trips bit 15) |
| T-28-03 | Elevation of Privilege | adsr_level used as Q15 multiplier without range check | mitigate | level is clamped to [0, 0x7FFF] before being passed to q15_mul_truncate; 0x7FFF is the legal Q15 maximum |
</threat_model>

<verification>
## Phase 28 Completion Checks

Run all three in sequence from the build directory:

```bash
cd "/home/ubuntu-studio/Desktop/PSX Reverb/build"
cmake --build . -j4
ctest --output-on-failure
```

Expected: zero failures. New test count >= 116 + 10 (new adsr_unit tests) + 4 (new voice_tick integration tests).

Specific gate: the following tests must be present and pass:
- adsr_unit: all counter/exponential/phase-transition tests
- voice_tick_unit: all Phase 27 tests + new ADSR wiring tests + full-pipeline test

Regression gate: all tests that passed at Phase 27 exit (116 total) must still pass.

RT-safety gate:
```bash
nm -u src/spu94/CMakeFiles/spu94.dir/spu94_adsr.c.o | grep -E 'malloc|free|printf|fopen'
```
Must produce zero output.
</verification>

<success_criteria>
1. A voice keyed on with fast attack settings produces output amplitude that rises from near-zero to near-full over successive ticks — not constant from tick 1. Verified by test_adsr_attack_ramps_output.

2. The fake exponential knee fires: when ADSR level crosses 0x6000 during attack, the effective step rate drops by 4x. Verified by test_fake_exponential_knee_at_0x6000.

3. Decay is genuinely proportional to current level (real exponential): a step applied at level 0x7FFF is approximately twice the step applied at level 0x3FFF. Verified by test_real_exponential_decay_proportional.

4. Sustain level register value 0 produces a sustain plateau at 0x800 (not zero). Verified by test_sustain_level_zero_is_0x800.

5. KOFF transitions to Release from any phase; Release decays to zero then the voice goes fully silent (active=0). Verified by test_release_reaches_off and test_adsr_full_pipeline_attack_sustain_release.

6. With adsr.enabled=0, spu94_voice_tick output is identical to Phase 27 behavior (bypass passthrough). Verified by test_adsr_bypass_matches_phase27.

7. All 116 pre-Phase-28 tests continue to pass without modification.
</success_criteria>

<output>
Create `.planning/phases/28-adsr-envelope/28-01-PLAN-SUMMARY.md` when done.
</output>
