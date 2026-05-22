---
phase: 35-pitch-modulation-pmon
plan: 01
subsystem: voice-engine
tags: [pmon, pitch-modulation, fm-synthesis, voice-chain, outx]

# Dependency graph
requires:
  - phase: 34-signed-volume
    provides: int16_t outx field on spu94_voice_t (post-ADSR, pre-volume capture)
  - phase: 30-voice-mixer
    provides: spu94_voice_mixer_tick sequential 0..23 voice loop
provides:
  - pmon_flags field on spu94_voice_mixer_t (uint32_t bitmask)
  - spu94_voice_mixer_set_pmon API function
  - PMON formula applied per-tick in mixer voice loop
  - 6 regression tests covering all PMON behaviors
affects: [36-noise-generator, 37-volume-sweep]

# Tech tracking
tech-stack:
  added: []
  patterns: [save-restore pitch for per-tick PMON override, Factor=outx+0x8000 unsigned multiply]

key-files:
  created: []
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_voice_tick.c

key-decisions:
  - "PMON Factor 0x8000 is unity (1.0x), not 0.5x -- (step * 0x8000) >> 15 = step"
  - "PMON clamp is 0x4000 per spec, relaxed voice_tick pitch re-clamp from 0x3FFF to 0x4000"
  - "Near-max-negative modulator produces nonzero step (~0x10) due to authentic Gauss/ADSR Q15 truncation"

patterns-established:
  - "PMON save/restore: save base pitch before PMON override, restore after voice_tick"
  - "PMON formula: Factor = (int32_t)outx + 0x8000 as uint32_t; Step = (base * factor) >> 15"

requirements-completed: [PMON-01, PMON-03, PMON-04, PMON-05, PMON-06]

# Metrics
duration: 15min
completed: 2026-05-22
---

# Phase 35 Plan 01: PMON Pitch Modulation Summary

**PS1-faithful PMON pitch modulation via Factor = outx(N-1) + 0x8000 with 0x4000 clamp, save/restore pitch pattern, 6 regression tests**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-22T18:50:58Z
- **Completed:** 2026-05-22T19:06:13Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Added `pmon_flags` (uint32_t) to `spu94_voice_mixer_t` for per-voice PMON enable bitmask
- Implemented `spu94_voice_mixer_set_pmon` API function with voice_idx 0..23 validation
- Wired PMON formula in `spu94_voice_mixer_tick` voice loop: `Factor = outx(N-1) + 0x8000; Step = (base_pitch * Factor) >> 15; clamp to 0x4000`
- Save/restore pattern preserves base pitch register across ticks (PMON is per-tick, not persistent)
- Voice 0 PMON bit accepted but silently ignored (no predecessor to read)
- Relaxed voice_tick Step 4 pitch clamp from 0x3FFF to 0x4000 to accommodate PMON spec
- All 45 voice_tick unit tests pass (39 existing + 6 new PMON), zero regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: RED -- Failing tests for PMON pitch modulation** - `92b8b76` (test)
2. **Task 2: GREEN -- Implement PMON formula in mixer tick** - `5e5e16c` (feat)

## Files Created/Modified
- `include/spu94/spu94_voice.h` - Added pmon_flags field, declared spu94_voice_mixer_set_pmon
- `src/spu94/spu94_voice.c` - PMON formula in mixer_tick, set_pmon implementation, relaxed pitch re-clamp to 0x4000
- `tests/unit/voice/test_voice_tick.c` - 6 new PMON tests: silent modulator (unity), bit0 ignored, positive modulator (increased pitch), negative modulator (near-zero pitch), chain stacking (cascading modulation), 0x4000 clamp

## Decisions Made
- **Factor 0x8000 is unity:** The plan's FEATURES.md incorrectly stated that silent modulator (outx=0, Factor=0x8000) halves pitch. The actual math: `(step * 0x8000) >> 15 = step * 1.0`. Factor 0x8000 is unity passthrough, not half. Factor 0x0000 (max negative modulator) stops pitch; Factor 0xFFFF (~max positive) approximately doubles pitch. Tests corrected accordingly.
- **PMON clamp 0x4000 vs voice_tick 0x3FFF:** The PS1 spec says PMON clamps to 0x4000 (exceeding the normal 0x3FFF pitch maximum). The voice_tick Step 4 pitch re-clamp was blocking this, so it was relaxed from 0x3FFF to 0x4000. Normal key_on still clamps to 0x3FFF; only PMON can produce 0x4000.
- **Authentic Q15 truncation in negative modulator test:** Voice 0's outx stabilizes at -32640 (not -32768) due to Gaussian interpolation coefficient sum and Q15 truncation. Factor = 128, Step = 0x10. The test tolerates this small nonzero step (< 1% of base pitch) as authentic PS1 behavior.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed plan's incorrect PMON Factor interpretation**
- **Found during:** Task 2 (GREEN phase)
- **Issue:** Plan stated silent modulator (outx=0) "halves carrier pitch" and expected Factor=0x8000 to produce step = pitch/2. The actual formula `(pitch * 0x8000) >> 15 = pitch * 1.0` gives unity, not half. The plan's arithmetic `(0x1000 * 0x8000) >> 15 = 0x0800` is wrong (correct answer: 0x1000).
- **Fix:** Updated test expectations to match correct spec math. Silent modulator test now asserts unity (same counter with and without PMON). Positive modulator test uses current_addr comparison (more robust than single-tick counter measurement).
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Committed in:** 5e5e16c (Task 2 commit)

**2. [Rule 1 - Bug] Fixed ADPCM shift direction in test sample construction**
- **Found during:** Task 2 (GREEN phase)
- **Issue:** Negative modulator test used shift=12 (nibble=-8), expecting decoded=-32768. But ADPCM formula is `nibble << (12 - shift)`, so shift=12 gives `nibble << 0 = -8`. Need shift=0 for max amplification: `-8 << 12 = -32768`.
- **Fix:** Changed negative sample from shift=12 to shift=0. Same fix applied to `make_loud_sample` helper (positive samples).
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Committed in:** 5e5e16c (Task 2 commit)

**3. [Rule 1 - Bug] Fixed voice_tick pitch re-clamp blocking PMON 0x4000**
- **Found during:** Task 2 (GREEN phase)
- **Issue:** voice_tick Step 4 had `effective_pitch = (v->pitch > 0x3FFF) ? 0x3FFF : v->pitch`, which clamped PMON's spec-correct 0x4000 back to 0x3FFF.
- **Fix:** Changed clamp threshold from 0x3FFF to 0x4000. Normal key_on still clamps to 0x3FFF; PMON's 0x4000 passes through correctly.
- **Files modified:** src/spu94/spu94_voice.c
- **Committed in:** 5e5e16c (Task 2 commit)

---

**Total deviations:** 3 auto-fixed (3 bugs in plan/test expectations)
**Impact on plan:** Test expectations corrected to match actual PS1 spec math. Implementation matches spec exactly.

## Issues Encountered
- CMake build required `SPU94_BUILD_GUI=OFF` in worktree context (JUCE FetchContent fails in parallel worktree). This is expected and does not affect test correctness.

## User Setup Required
None - no external service configuration required.

## TDD Gate Compliance

1. RED gate: `92b8b76` -- test(35-01) commit with 6 PMON tests (3 failing as expected)
2. GREEN gate: `5e5e16c` -- feat(35-01) commit making all 45 tests pass
3. No refactor step needed -- implementation was minimal and clean.

## Next Phase Readiness
- PMON formula is ready for NON (Phase 36) interaction: noise voice outx can feed PMON factor
- pmon_flags field accessible at same level as eon_flags for GUI/API integration
- No blockers

## Self-Check: PASSED

All files exist, all commits verified, 45/45 tests pass.

---
*Phase: 35-pitch-modulation-pmon*
*Completed: 2026-05-22*
