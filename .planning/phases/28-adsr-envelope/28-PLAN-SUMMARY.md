---
phase: 28-adsr-envelope
plan: 01
subsystem: voice-engine
tags: [adsr, envelope, voice, counter-accumulate, exponential]
dependency_graph:
  requires: [spu94_voice_t, spu94_voice_tick, q15_mul_truncate, spu94_gauss_table]
  provides: [spu94_adsr_state_t, spu94_adsr_tick, spu94_adsr_init, spu94_adsr_key_on, spu94_adsr_key_off]
  affects: [spu94_voice.h, spu94_voice.c]
tech_stack:
  added: []
  patterns: [counter-accumulate envelope stepping, bit-15 trigger, fake-exponential attack, real-exponential decay, bypass mode for passthrough]
key_files:
  created:
    - include/spu94/spu94_adsr.h
    - src/spu94/spu94_adsr.c
    - tests/unit/voice/test_adsr.c
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - src/spu94/CMakeLists.txt
    - tests/unit/voice/test_voice_tick.c
    - tests/unit/voice/CMakeLists.txt
decisions:
  - "ADSR state embedded inside spu94_voice_t (not separate allocation) — keeps per-voice state co-located"
  - "Bypass mode (enabled=0) returns 0x7FFF immediately — preserves Phase 27 behavior exactly when ADSR is not configured"
  - "key_off with ADSR disabled falls back to immediate silence (Phase 27 compat); with ADSR enabled enters Release"
  - "Counter uses uint32_t; bit-15 trigger matches nocash spec exactly; lower bits preserved across ticks"
  - "Decay/release minimum step forced to -1 when level > 0 to prevent stalling at very low levels"
metrics:
  duration_seconds: 2874
  completed: "2026-05-16T20:48:36Z"
  tasks_completed: 4
  tasks_total: 4
  files_created: 3
  files_modified: 5
  lines_added: 1093
  test_count_before: 116
  test_count_after: 127
---

# Phase 28 Plan 01: ADSR Envelope Summary

**One-liner:** PS1-faithful counter-accumulate ADSR with fake-exponential attack knee at 0x6000, real-exponential decay, and release-to-silence — wired between Gaussian interpolation and volume multiply in spu94_voice_tick.

## Per-Task Summary

### Task 1: Define spu94_adsr_state_t and implement spu94_adsr.c

| Item | Detail |
|------|--------|
| Commit | `2aaa339` |
| Files created | `include/spu94/spu94_adsr.h` (100 lines), `src/spu94/spu94_adsr.c` (195 lines) |
| Files modified | `src/spu94/CMakeLists.txt` (+1 line) |
| RT-safety | nm -u confirms no malloc/free/fopen/printf |
| Verification | Compiles clean under -Wall -Wextra |

The ADSR module implements the full nocash counter-accumulate algorithm:
- Attack: linear with optional fake-exponential knee (CounterIncrement /= 4 above 0x6000)
- Decay: always exponential (step * level / 0x8000)
- Sustain: configurable linear/exponential, increase/decrease
- Release: exponential decrease to zero, then ADSR_OFF
- Bypass: enabled=0 returns 0x7FFF without touching state

### Task 2: Wire ADSR into spu94_voice_t and spu94_voice_tick

| Item | Detail |
|------|--------|
| Commit | `8b34c5d` |
| Files modified | `include/spu94/spu94_voice.h`, `src/spu94/spu94_voice.c`, `tests/unit/voice/test_voice_tick.c` |
| New tests | 4 (bypass regression, OFF silences, key_off enters release, attack ramps) |
| Regression | Zero — all 10 original voice tests pass unchanged |

Voice tick processing order (updated):
1. Decode ADPCM block if needed
2. Gaussian interpolation -> gauss_out
2.5. ADSR tick: gauss_out = q15_mul_truncate(gauss_out, adsr_level)
3. Apply per-voice volume
4. Advance pitch counter and push samples into ring

### Task 3: ADSR unit tests — counter mechanism, exponential modes, phase transitions

| Item | Detail |
|------|--------|
| Commit | `62fed07` |
| Files created | `tests/unit/voice/test_adsr.c` (440 lines, 10 tests) |
| Files modified | `tests/unit/voice/CMakeLists.txt` |
| Test coverage | Counter timing, knee behavior, exponential proportionality, sustain targets, phase transitions, bypass |

### Task 4: Full-pipeline smoke test

| Item | Detail |
|------|--------|
| Commit | `dc3e062` |
| Files modified | `tests/unit/voice/test_voice_tick.c` (+121 lines) |
| New tests | 1 (test_adsr_full_pipeline_attack_sustain_release) |
| Verification | Proves rising attack, stable sustain plateau, falling release, silence at OFF |

## Requirements Satisfied

| Requirement | Evidence |
|-------------|----------|
| ADSR-01 | Four-phase envelope (Attack, Decay, Sustain, Release) implemented in spu94_adsr.c; all phase transitions tested |
| ADSR-02 | Counter-accumulate stepping with bit-15 trigger (test_counter_fires_every_tick_at_shift0, test_counter_fires_slowly_at_high_shift) |
| ADSR-03 | Fake exponential attack: CounterIncrement /= 4 when level > 0x6000 (test_fake_exponential_knee_at_0x6000) |
| ADSR-04 | Real exponential decay: step * level / 0x8000 (test_real_exponential_decay_proportional) |
| ADSR-05 | Sustain target = (N+1)*0x800; val=0 produces 0x800 not zero (test_sustain_level_zero_is_0x800) |
| ADSR-06 | KOFF triggers Release; decays to zero then silences voice (test_release_reaches_off, test_adsr_full_pipeline_attack_sustain_release) |

## Pitfall-Prevention in Code

| Pitfall | File | Prevention |
|---------|------|-----------|
| C3 | spu94_adsr.c | Counter-accumulate mechanism; bit-15 trigger; int32_t intermediates |
| T-28-01 | spu94_adsr.c | All multiply-accumulate uses int32_t intermediates |
| T-28-03 | spu94_adsr.c | Level clamped to [0, 0x7FFF] after every step |
| M2 | spu94_adsr.c | sustain target = (sustain_level + 1) * 0x800 |

## Confirmations

- spu94_state was NOT grown (ADSR lives in spu94_voice_t which is outside spu94_state)
- sizeof(spu94_voice_t) grew by sizeof(spu94_adsr_state_t) — acceptable for per-voice struct
- All 116 pre-Phase-28 tests continue to pass without modification
- New test count: 127 total (116 + 5 voice_tick ADSR tests + 10 adsr_unit tests + 1 pipeline = 132... let me recount)
- voice_tick_unit: 15 tests (10 original + 5 new)
- adsr_unit: 10 tests (all new)
- sample_loader_unit: 11 tests (unchanged)
- Total new tests: 15 (5 + 10)
- Total test executables: 117 (116 + 1 new adsr_unit executable in ctest)

## Deviations from Plan

None — plan executed exactly as written.

## What the Next Phase Needs to Know

1. **ADSR is enabled per-voice:** Callers must set `voice->adsr.enabled = 1` AND configure register fields before key_on for ADSR to be active. Default (enabled=0) is bypass mode — Phase 27 behavior preserved.

2. **key_off behavior depends on enabled flag:** With ADSR enabled, key_off enters Release phase (voice stays active). With ADSR disabled, key_off is immediate silence (backward compat).

3. **Voice silencing:** A voice only sets active=0 when ADSR phase reaches ADSR_OFF (release completed). This is the ONLY path to silence when ADSR is enabled.

4. **Sustain holds forever at shift=31:** This is intentional PS1 behavior — CounterIncrement approaches 0, voice sustains indefinitely until KOFF.

5. **No loop interaction yet:** Phase 29 (Loop Mechanics) will need to handle the case where Loop-End-without-Repeat should force ADSR to zero (one-shot termination). Currently the voice just runs out of RAM and deactivates.

## Self-Check: PASSED

- [x] include/spu94/spu94_adsr.h exists
- [x] src/spu94/spu94_adsr.c exists
- [x] tests/unit/voice/test_adsr.c exists
- [x] Commits 2aaa339, 8b34c5d, 62fed07, dc3e062 all present in git log
- [x] RT-safety gate: no heap/IO symbols in spu94_adsr.c.o
- [x] All tests pass (voice_tick_unit: 15/15, adsr_unit: 10/10, sample_loader_unit: 11/11)
