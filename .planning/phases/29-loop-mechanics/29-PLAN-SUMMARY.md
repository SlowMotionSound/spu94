---
phase: 29-loop-mechanics
plan: 01
subsystem: voice-engine
tags: [loop, adpcm, endx, filter-state, one-shot]
dependency_graph:
  requires: [spu94_voice_t, spu94_voice_tick, spu94_adsr_state_t, spu94_adpcm_decode_block]
  provides: [spu94_voice_get_endx, loop_addr field, loop filter snapshot, one-shot termination, ENDX status]
  affects: [spu94_voice.h, spu94_voice.c]
tech_stack:
  added: []
  patterns: [flag-byte dispatch after decode, filter state snapshot/restore at loop boundaries, ENDX status bit with KON-clear semantics]
key_files:
  created: []
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_voice_tick.c
decisions:
  - "Used existing SPU94_VAG_FLAG_* constants from spu94_vag.h rather than creating new SPU94_VAG_LOOP_* aliases — same bit definitions, avoids redundancy"
  - "One-shot termination with ADSR enabled uses adsr.phase=ADSR_OFF + level=0 (same terminal state as completed release); without ADSR uses active=0 immediately"
  - "Filter state snapshot taken AFTER decode (captures state valid for next iteration), consistent with C5 pitfall prevention"
  - "Fixed pre-existing test helper make_long_sample to not set END flag — prevents unintended one-shot triggering in ADSR tests"
metrics:
  duration_seconds: 2016
  completed: "2026-05-16T21:34:17Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 3
  lines_added: 197
  test_count_before: 117
  test_count_after: 117
---

# Phase 29 Plan 01: Loop Mechanics Summary

**One-liner:** PS1 ADPCM block-header flag dispatch with loop-start address latching, filter-state snapshot/restore at loop boundaries, one-shot termination, and per-voice ENDX status bit.

## Per-Task Summary

### Task 1: Add loop fields to spu94_voice_t and update key_on / get_endx API

| Item | Detail |
|------|--------|
| Commit | `ebcbef2` |
| Files modified | `include/spu94/spu94_voice.h` (+7 lines), `src/spu94/spu94_voice.c` (+12 lines), `tests/unit/voice/test_voice_tick.c` (+131 lines) |
| New fields | loop_addr (uint32_t), loop_adpcm_old (int16_t), loop_adpcm_older (int16_t), endx (uint8_t) |
| New API | spu94_voice_get_endx(const spu94_voice_t *v) |
| Verification | Compiles clean, 4 loop test stubs registered |

### Task 2: Implement loop flag logic in spu94_voice_tick and validate tests

| Item | Detail |
|------|--------|
| Commit | `d09944b` |
| Files modified | `src/spu94/spu94_voice.c` (+30 lines), `tests/unit/voice/test_voice_tick.c` (helper fix) |
| New logic | flag_byte capture, Loop-Start latch, Loop-End+Repeat jump, one-shot termination, ENDX set |
| Tests | All 19 voice_tick tests pass (15 pre-existing + 4 new loop-mechanics) |
| Regression fix | make_long_sample helper no longer sets END flag (prevented false one-shot in ADSR tests) |

## Requirements Satisfied

| Requirement | Evidence |
|-------------|----------|
| LOOP-01 | flag_byte from spu94_adpcm_decode_block is captured and dispatched on every block decode (line 120 of spu94_voice.c) |
| LOOP-02 | Loop-Start flag (bit 2) latches loop_addr = current_addr - 16 (the decoded block's address). Test: test_loop_start_latches_address |
| LOOP-03 | Loop-Start snapshots adpcm_state.old/older; Loop-End+Repeat restores them before next decode. Test: test_loop_end_repeat_jumps_to_loop_addr |
| LOOP-04 | Loop-End without Repeat forces ADSR to ADSR_OFF (or active=0 if ADSR disabled). Test: test_one_shot_silences_voice |
| LOOP-05 | endx set on any Loop-End, queried via spu94_voice_get_endx, cleared by KON only. Tests: test_one_shot_silences_voice, test_endx_cleared_by_key_on |

## Pitfall-Prevention in Code

| Pitfall | File | Prevention |
|---------|------|-----------|
| C4 | spu94_voice.c | Flag byte read from block header byte 1 via spu94_adpcm_decode_block return value — not from registers |
| C5 | spu94_voice.c | Filter state snapshot taken AFTER decode; restored on loop jump. Two dedicated fields: loop_adpcm_old, loop_adpcm_older |
| S3 | spu94_voice.c | Gaussian ring NOT cleared on loop jump — old samples pushed out naturally over 3 ticks |
| M3 | spu94_voice.c | ENDX cleared on KON (line 73), NOT on KOFF |

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Pre-existing test helper set END flag on continuous samples**
- **Found during:** Task 2
- **Issue:** `make_long_sample` helper set flag byte 0x01 on the last block, which triggered one-shot termination once loop-end logic was added. This broke `test_key_off_enters_release` which expected the voice to remain active during release.
- **Fix:** Changed `make_long_sample` to set flag=0x00 on all blocks. Samples without loop flags run until bounds-check deactivation (original Phase 27 behavior).
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Commit:** d09944b

**2. [Rule 3 - Blocking] Plan referenced non-existent SPU94_VAG_LOOP_* constants**
- **Found during:** Task 2
- **Issue:** Plan's interface section referenced `SPU94_VAG_LOOP_END`, `SPU94_VAG_LOOP_REPEAT`, `SPU94_VAG_LOOP_START` but actual codebase uses `SPU94_VAG_FLAG_END`, `SPU94_VAG_FLAG_LOOP_REPEAT`, `SPU94_VAG_FLAG_LOOP_START`.
- **Fix:** Used existing `SPU94_VAG_FLAG_*` constants from spu94_vag.h. Same bit definitions, no functional difference.
- **Files modified:** src/spu94/spu94_voice.c, tests/unit/voice/test_voice_tick.c
- **Commit:** d09944b

## Confirmations

- spu94_state was NOT grown (loop fields live in spu94_voice_t which is outside spu94_state)
- sizeof(spu94_voice_t) grew by 9 bytes (uint32_t + int16_t + int16_t + uint8_t)
- All 15 pre-Phase-29 voice_tick tests continue to pass without modification
- New test count: 117 ctest executables (same count — no new test executable added; 4 new test functions in existing voice_tick_unit)
- voice_tick_unit internal count: 19 tests (15 pre-existing + 4 new)
- RT-safety: no new heap/IO calls introduced (flag dispatch is pure arithmetic + field assignment)
- Build clean under -Wall -Wextra: zero errors, zero warnings

## What the Next Phase Needs to Know

1. **Loop address default:** loop_addr defaults to 0 (same as sample_start_addr in typical usage). If a sample has no LOOP_START-flagged block but does have LOOP_END+REPEAT, the voice jumps to address 0.

2. **ENDX set on ALL loop-ends:** Including loop-end+repeat. This matches real PS1 behavior per nocash spec. Game software polls ENDX to detect voice completion; clearing happens on KON only.

3. **Gaussian ring preservation:** The ring is NOT cleared at loop boundaries (S3). This means the first 3 samples after a loop jump use stale ring history mixed with newly-decoded samples. This is authentic PS1 behavior and prevents a pop at the loop point.

4. **test_voice_stops_at_ram_boundary still uses make_test_block:** That helper retains flag 0x01 (END). This now correctly triggers one-shot termination (ADSR disabled → active=0), which is the same observable behavior as the original bounds-check deactivation. The test remains valid.

5. **ADSR integration:** One-shot termination with ADSR enabled drives ADSR to the same terminal state as a completed release (phase=ADSR_OFF, level=0). The existing ADSR_OFF check in spu94_voice_tick handles deactivation on the next tick.

## Self-Check: PASSED

- [x] include/spu94/spu94_voice.h has loop_addr, loop_adpcm_old, loop_adpcm_older, endx fields
- [x] include/spu94/spu94_voice.h has spu94_voice_get_endx declaration
- [x] src/spu94/spu94_voice.c has flag_byte capture and dispatch logic
- [x] src/spu94/spu94_voice.c has spu94_voice_get_endx implementation
- [x] Commits ebcbef2, d09944b both present in git log
- [x] All 19 voice_tick tests pass (including 4 loop-mechanics tests)
- [x] All 10 adsr_unit tests pass
- [x] All 11 sample_loader_unit tests pass
