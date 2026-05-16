---
phase: 27-single-voice-playback
plan: 01
subsystem: voice-engine
tags: [voice, adpcm, gaussian, pitch, spu-ram]
dependency_graph:
  requires: [spu94_adpcm_decode_block, spu94_gauss_table, spu94_process]
  provides: [spu94_voice_t, spu94_voice_tick, spu94_voice_key_on, spu94_voice_key_off, spu94_sample_encode_to_ram, spu94_voice_load_sample_raw, spu94_voice0_key_on]
  affects: [spu94_process.c]
tech_stack:
  added: []
  patterns: [per-voice isolated state, decode-before-interpolate ordering, single-counter architecture, patina-bus injection scaffolding]
key_files:
  created:
    - include/spu94/spu94_spu_ram.h
    - include/spu94/spu94_voice.h
    - include/spu94/spu94_sample_loader.h
    - src/spu94/spu94_voice.c
    - src/spu94/spu94_sample_loader.c
    - tests/unit/voice/test_voice_tick.c
    - tests/unit/voice/test_sample_loader.c
    - tests/unit/voice/CMakeLists.txt
  modified:
    - include/spu94/spu94_vag.h
    - src/spu94/spu94_process.c
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - "Voice state lives OUTSIDE spu94_state (file-scope statics in spu94_process.c) to avoid growing the struct past SPU94_STATE_SIZE_MAX. Phase 30 migrates to caller-allocated spu94_voice_mixer_t."
  - "Patina bus injection: voice 0 output overwrites patina_l/patina_r before the coloration path. When adpcm_enabled=1 AND voice engine active, coloration overwrites voice output. Only one should be active at a time in Phase 27."
  - "vol_l/vol_r declared as int16_t allowing phase inversion (S2) but Phase 27 documents unsigned semantics (0-32767) per VOICE-04."
metrics:
  duration_seconds: 3313
  completed: "2026-05-16T19:24:13Z"
  tasks_completed: 3
  tasks_total: 3
  files_created: 8
  files_modified: 4
  lines_added: 895
  test_count_before: 114
  test_count_after: 116
---

# Phase 27 Plan 01: Single Voice Playback Summary

**One-liner:** Per-voice ADPCM decode from dedicated 512 KB RAM with single-counter Gaussian interpolation, pitch clamp to 0x3FFF, and Q15 volume scaling — wired into spu94_process as voice 0 scaffolding.

## Per-Task Summary

### Task 1: Define spu94_voice_t struct and SPU RAM contract header

| Item | Detail |
|------|--------|
| Commit | `91e28c0` |
| Files created | `include/spu94/spu94_spu_ram.h` (27 lines), `include/spu94/spu94_voice.h` (86 lines) |
| Files modified | `include/spu94/spu94_vag.h` (added LOOP_REPEAT, LOOP_START constants) |
| Verification | All headers compile clean under `-std=c99 -Wall -Wextra` |

The spu94_voice_t struct contains exactly the fields specified in the plan: current_addr, sample_start_addr, pitch, pitch_counter, adpcm_state, decode_buf[28], decode_buf_pos, has_block, gauss_ring[4], gauss_ring_pos, vol_l, vol_r, active.

### Task 2: Implement spu94_voice.c and spu94_sample_loader.c

| Item | Detail |
|------|--------|
| Commit | `c6c546a` |
| Files created | `src/spu94/spu94_voice.c` (169 lines), `include/spu94/spu94_sample_loader.h` (45 lines), `src/spu94/spu94_sample_loader.c` (67 lines), `tests/unit/voice/test_voice_tick.c` (278 lines, 10 tests), `tests/unit/voice/test_sample_loader.c` (223 lines, 11 tests), `tests/unit/voice/CMakeLists.txt` |
| Tests | 21 tests, all pass |
| RT-safety | `nm -u` confirms no malloc/free/fopen/printf references in either .o file |

### Task 3: Wire voice 0 into spu94_process

| Item | Detail |
|------|--------|
| Commit | `9adf287` |
| Files modified | `src/spu94/spu94_process.c` (+87 lines) |
| New symbols | `spu94_voice_load_sample_raw`, `spu94_voice0_key_on`, `spu94_voice0_key_off` |
| Regression | Zero — voice engine disabled by default (`s_voice_engine_enabled=0`) |

## Requirements Satisfied

| Requirement | Evidence |
|-------------|----------|
| VOICE-01 | spu94_voice_tick calls spu94_adpcm_decode_block on voice_ram; no encode in hot path |
| VOICE-02 | Single-counter Gaussian interpolation per voice (bits 12+ = sample, bits 4-11 = Gauss index) |
| VOICE-03 | Pitch clamped to 0x3FFF at key_on (test: test_pitch_clamp_to_0x3FFF) |
| VOICE-04 | Per-voice vol_l/vol_r applied via q15_mul_truncate (test: test_volume_scaling) |
| VOICE-05 | spu94_sample_encode_to_ram encodes WAV PCM to ADPCM at load time |
| VOICE-06 | 24 spu94_voice_t structs with isolated gauss_ring[4] (s_voices[24] in spu94_process.c) |
| RAM-01 | s_voice_ram[SPU94_SPU_RAM_BYTES] is a separate 512 KB buffer from state->work_buf |
| RAM-02 | state->work_buf (reverb) is unchanged and untouched by voice engine code |
| RAM-03 | spu94_sample_encode_to_ram returns -1 on bounds overflow; spu94_voice_load_sample_raw returns SPU94_INVALID_ARG |
| RAM-04 | All addressing uses PS1-style byte offsets within voice RAM |

## Pitfall-Prevention Comments in Code

| Code | File | Comment |
|------|------|---------|
| C1 | spu94_voice.c | "DECODE-ONLY from RAM — no encode call in this file" |
| C2 | spu94_voice.h | "gauss_ring[4]: ISOLATED per voice — never references state->gauss_ring_l/r" |
| C6 | spu94_process.c | "voice_ram is a SEPARATE 512 KB buffer from state->work_buf (C6 / RAM-01)" |
| C7 | spu94_voice.c | "Pitch clamped to 0x3FFF at key_on and re-checked in tick" |
| S5 | spu94_voice.c | "Processing order (S5 — decode before interpolate, then advance counter)" |
| M6 | spu94_voice.c | "M6: advance address by 16 bytes (one ADPCM block)" |

## Confirmations

- spu94_state was NOT grown: `sizeof(struct spu94_state) = 2816` vs `SPU94_STATE_SIZE_MAX = 16384`
- Voice mixer state lives OUTSIDE spu94_state (file-scope statics in spu94_process.c)
- Patina injection approach was kept (not replaced)
- ctest pass count: 114 before, 116 after (+2 new: voice_tick_unit, sample_loader_unit)
- All existing preset/golden/process tests pass unchanged

## Deviations from Plan

None — plan executed exactly as written.

## What the Next Phase Needs to Know

1. **Voice state location:** 24 voice structs are file-scope statics in `spu94_process.c`. Phase 30 must migrate these to a caller-allocated `spu94_voice_mixer_t` struct when expanding to full 24-voice polyphony.

2. **Patina bus injection:** Voice 0 output currently overwrites `patina_l/patina_r`. When `adpcm_enabled=1` AND voice engine active, the coloration path overwrites voice output. Phase 30 needs a proper mixer topology with separate voice dry/reverb buses.

3. **No loop support:** When a sample runs out of blocks (current_addr overflows voice_ram_size), the voice sets `active=0` and goes silent. Phase 29 adds loop flag handling.

4. **No ADSR:** key_off is immediate silence. Phase 28 adds the PS1 ADSR envelope.

5. **Forward declarations:** `spu94_voice_load_sample_raw`, `spu94_voice0_key_on`, `spu94_voice0_key_off` are forward-declared in the .c file only. Phase 31 will formalize these in spu94.h.

6. **Static voice RAM:** `s_voice_ram[SPU94_SPU_RAM_BYTES]` (512 KB) is a BSS static. Phase 30 should make this caller-allocated to match the existing work_buf pattern.
