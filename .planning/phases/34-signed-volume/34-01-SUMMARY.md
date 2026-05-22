---
phase: 34-signed-volume
plan: 01
subsystem: voice-engine
tags: [q15, signed-volume, phase-inversion, outx, pmon-prep]

# Dependency graph
requires:
  - phase: 27-voice-engine
    provides: spu94_voice_t struct, voice tick pipeline, Gaussian interpolation
  - phase: 28-adsr-envelope
    provides: ADSR tick integration at Step 2.5
provides:
  - int16_t outx field on spu94_voice_t (post-ADSR, pre-volume capture point for PMON)
  - Signed volume documentation (vol_l/vol_r range -0x4000..+0x3FFF)
  - 4 regression tests proving phase inversion and outx capture
affects: [35-pmon, 37-volume-sweep]

# Tech tracking
tech-stack:
  added: []
  patterns: [VxOUTX capture between ADSR and volume multiply]

key-files:
  created: []
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_voice_tick.c

key-decisions:
  - "outx stored between Step 2.5 (ADSR multiply) and Step 3 (volume multiply) per DuckStation-confirmed capture point"
  - "Phase inversion test tolerates 1-LSB Q15 truncation asymmetry -- floor-toward-negative-infinity causes q15_mul(x,-v) to differ from -q15_mul(x,+v) by 1 LSB for odd products; this is authentic PS1 behavior"

patterns-established:
  - "VxOUTX capture: v->outx = gauss_out after ADSR multiply, before volume multiply"

requirements-completed: [SVOL-01, SVOL-02, SVOL-03, SVOL-04]

# Metrics
duration: 11min
completed: 2026-05-22
---

# Phase 34 Plan 01: Signed Volume Summary

**Signed volume range (-0x4000..+0x3FFF) exposed through C core API with phase-inversion regression tests and VxOUTX capture point for PMON**

## Performance

- **Duration:** 11 min
- **Started:** 2026-05-22T17:22:48Z
- **Completed:** 2026-05-22T17:34:28Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Added `int16_t outx` field to `spu94_voice_t` for post-ADSR, pre-volume value capture (Phase 35 PMON dependency)
- Updated vol_l/vol_r documentation from unsigned to signed semantics
- Proved negative volume produces exact phase inversion (within 1-LSB Q15 tolerance)
- Proved outx is identical regardless of volume sign (confirms correct capture point)
- All 39 voice_tick_unit tests pass (35 existing + 4 new, zero regressions)

## Task Commits

Each task was committed atomically:

1. **Task 1: RED -- Add failing tests for signed volume and outx capture** - `f570b12` (test)
2. **Task 2: GREEN -- Add outx field, store VxOUTX, update docs, make tests pass** - `2528d2c` (feat)

## Files Created/Modified
- `include/spu94/spu94_voice.h` - Added outx field, updated vol_l/vol_r comments to signed semantics
- `src/spu94/spu94_voice.c` - Store v->outx = gauss_out at Step 2.75 between ADSR and volume
- `tests/unit/voice/test_voice_tick.c` - 4 new tests: phase inversion, key_on acceptance, outx capture, mixer negative volume

## Decisions Made
- **outx capture point:** Stored between Step 2.5 (ADSR multiply) and Step 3 (volume multiply), matching DuckStation-confirmed VxOUTX behavior. This is the value PMON (Phase 35) will read.
- **Phase inversion tolerance:** Q15 ASR truncation causes 1-LSB asymmetry for `q15_mul(x, -v)` vs `-q15_mul(x, +v)` when the intermediate product is odd. This is authentic PS1 behavior, not a bug. Test validates inversion within this tolerance.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed phase inversion test sample energy and Q15 tolerance**
- **Found during:** Task 2 (GREEN phase)
- **Issue:** Original test used `make_long_sample` which produces small values (shift=0, nibbles +7,+3) that truncate to 0 at half-volume after Q15 multiply. Also, exact negation assertion failed by 1 LSB due to Q15 truncation asymmetry.
- **Fix:** Added 5-tick Gaussian ring warmup before collecting comparison samples. Changed assertion to tolerate 1-LSB Q15 truncation asymmetry (diff >= -1 && diff <= 0), which is the authentic PS1 behavior.
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Verification:** All 39 tests pass
- **Committed in:** 2528d2c (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Test fix was necessary to account for authentic Q15 arithmetic behavior. No scope creep.

## Issues Encountered
- CMake build required `SPU94_BUILD_GUI=OFF` in worktree context (JUCE FetchContent fails in parallel worktree). This is expected and does not affect test correctness -- the C core and unit tests build without JUCE.

## User Setup Required
None - no external service configuration required.

## TDD Gate Compliance

1. RED gate: `f570b12` -- test(34-01) commit with 4 failing tests (build error on missing outx field)
2. GREEN gate: `2528d2c` -- feat(34-01) commit with implementation making all 39 tests pass
3. No refactor step needed -- implementation was minimal.

## Next Phase Readiness
- outx field is ready for Phase 35 PMON to read as the pitch modulation source
- Signed volume API is fully documented and tested
- No blockers

## Self-Check: PASSED

All files exist, all commits verified, 39/39 tests pass.

---
*Phase: 34-signed-volume*
*Completed: 2026-05-22*
