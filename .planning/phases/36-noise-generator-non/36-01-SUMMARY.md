---
phase: 36-noise-generator-non
plan: 01
subsystem: voice-engine
tags: [noise, lfsr, non, voice-pipeline, tdd]
dependency_graph:
  requires: [phase-35-pmon, phase-28-adsr, phase-29-loop]
  provides: [noise-generator, non-voice-routing, noise-freq-api]
  affects: [spu94_voice.h, spu94_voice.c, spu94_noise.h, spu94_noise.c]
tech_stack:
  added: [spu94_noise module]
  patterns: [lfsr-fibonacci, timer-driven-frequency, bitmask-flag-api]
key_files:
  created:
    - include/spu94/spu94_noise.h
    - src/spu94/spu94_noise.c
    - tests/unit/voice/test_noise_gen.c
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - src/spu94/CMakeLists.txt
    - tests/unit/voice/test_voice_tick.c
    - tests/unit/voice/CMakeLists.txt
decisions:
  - "LFSR parity computed from pre-shift level (nocash line 3 before line 4)"
  - "voice_tick signature gains noise_level + non_enabled parameters (not struct fields)"
  - "Noise gen ticked once in mixer_tick before voice loop (NON-08)"
metrics:
  duration: "15min"
  completed: "2026-05-22T21:22:12Z"
  tasks: 2
  tests_added: 11
  tests_total: 57
  files_changed: 8
---

# Phase 36 Plan 01: SPU Noise Generator (LFSR + Voice Pipeline Integration) Summary

PS1 SPU global 16-bit LFSR noise generator with timer-driven frequency, per-voice NON bitmask routing, and nocash-faithful double-reload mechanism

## What Was Built

### Noise Generator Module (spu94_noise.h / spu94_noise.c)

New standalone module implementing the PS1 SPU's single global noise generator:

- `spu94_noise_gen_t` struct: int16_t level (LFSR state), int32_t timer (signed, critical for underflow detection), uint8_t shift (0-15), uint8_t step (4-7)
- `spu94_noise_gen_init`: seed=1 (not 0 -- zero is absorbing), timer=0, step=4
- `spu94_noise_gen_tick`: exact 5-step nocash algorithm -- decrement timer, compute parity from PRE-shift level (taps at bits 15,12,11,10 XNOR), shift LFSR on underflow, reload timer, double-reload without re-shifting

### Voice Pipeline Integration (spu94_voice.h / spu94_voice.c)

- `non_flags` (uint32_t) and `noise_gen` (spu94_noise_gen_t) added to mixer struct after pmon_flags
- `spu94_voice_tick` signature extended with `int16_t noise_level` and `uint8_t non_enabled` parameters
- NON branch in voice_tick STEP 2: `if (non_enabled) gauss_out = noise_level` else existing Gaussian/ZOH interpolation unchanged
- ADPCM decode (STEP 1) runs unconditionally -- loop flags and ENDX fire for NON voices
- `spu94_voice_mixer_set_non`: bitmask API matching set_pmon/set_eon pattern
- `spu94_voice_mixer_set_noise_freq`: validates shift (0-15) and step_raw (0-3), converts step = step_raw + 4
- Noise generator ticked once in mixer_tick before voice loop (NON-08)
- noise_gen initialized in mixer_init (seed=1 via spu94_noise_gen_init)

### Test Suite (11 new tests)

6 standalone noise_gen unit tests:
1. `test_lfsr_seed_one` -- verifies init state (NON-01)
2. `test_lfsr_deterministic_sequence` -- 4-step sequence from seed=1 at shift=15 (NON-01)
3. `test_timer_decrement_and_reload` -- shift=0 reload=131072, no double-reload (NON-02)
4. `test_double_reload` -- shift=15 step=7: only ONE LFSR shift despite two reloads (NON-02)
5. `test_frequency_varies_with_shift` -- shift=15 vs shift=14 over 100 ticks (NON-03)
6. `test_parity_from_pre_shift_level` -- 11-tick sequence verifies bit10 flips parity (NON-01)

5 NON integration tests (in test_voice_tick.c):
1. `test_non_voice_outputs_noise` -- NON voice output differs from ADPCM (NON-04)
2. `test_non_two_voices_same_output` -- two NON voices produce identical outx (NON-05)
3. `test_non_adpcm_still_runs` -- ENDX fires on loop-end for NON voice (NON-06)
4. `test_non_adsr_shapes_noise` -- ADSR attack ramps noise output over time (NON-07)
5. `test_non_pitch_no_effect` -- different pitches produce identical NON output (NON-03)

## TDD Gate Compliance

- RED gate: `test(36-01)` commit f4ad508 -- 5/6 noise_gen tests and 2/5 NON tests fail as expected
- GREEN gate: `feat(36-01)` commit b5143c3 -- all 57 tests pass (46 existing + 11 new)
- REFACTOR gate: not needed -- implementation is compact (~30 LOC tick function)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed ADSR attack config in test_non_adsr_shapes_noise**
- **Found during:** Task 2 (GREEN phase)
- **Issue:** Test used attack_step=7 which produces step=(7-7)<<shift=0 -- ADSR level never increments. The nocash ADSR formula is inverse: step=0 gives maximum increment (7<<shift), step=7 gives zero.
- **Fix:** Changed to attack_shift=5, attack_step=4 giving step=(7-4)<<6=192 per trigger tick
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Commit:** b5143c3

**2. [Rule 1 - Bug] Fixed sustain_level overflow warning**
- **Found during:** Task 1 (RED phase, build)
- **Issue:** Test set sustain_level=0x7FFF but the field is uint8_t (0-15). Compiler warning about truncation.
- **Fix:** Changed to sustain_level=15 (maximum valid value)
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Commit:** f4ad508

## Commits

| # | Hash | Type | Message |
|---|------|------|---------|
| 1 | f4ad508 | test | RED -- failing tests for noise generator and NON integration |
| 2 | b5143c3 | feat | GREEN -- implement noise generator LFSR and NON voice branch |

## Requirements Coverage

| Req ID | Status | Test(s) |
|--------|--------|---------|
| NON-01 | Covered | test_lfsr_seed_one, test_lfsr_deterministic_sequence, test_parity_from_pre_shift_level |
| NON-02 | Covered | test_timer_decrement_and_reload, test_double_reload |
| NON-03 | Covered | test_frequency_varies_with_shift, test_non_pitch_no_effect |
| NON-04 | Covered | test_non_voice_outputs_noise |
| NON-05 | Covered | test_non_two_voices_same_output |
| NON-06 | Covered | test_non_adpcm_still_runs |
| NON-07 | Covered | test_non_adsr_shapes_noise |
| NON-08 | Covered | architectural (noise_gen_tick called once before voice loop in mixer_tick) |

## Self-Check: PASSED

- [x] include/spu94/spu94_noise.h exists
- [x] src/spu94/spu94_noise.c exists
- [x] tests/unit/voice/test_noise_gen.c exists
- [x] Commit f4ad508 exists (test/RED)
- [x] Commit b5143c3 exists (feat/GREEN)
- [x] All 57 tests pass (ctest noise_unit + voice_tick_unit)
- [x] No compiler warnings
- [x] No stubs in created/modified files
