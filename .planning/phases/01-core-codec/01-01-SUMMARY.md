---
phase: 01-core-codec
plan: 01
subsystem: codec
tags: [adpcm, decoder, fixed-point, ps1-spu, c99]

# Dependency graph
requires: []
provides:
  - "spu94_adpcm_decode_block() -- standalone 16-byte ADPCM block decoder"
  - "spu94_adpcm_state -- 4-byte caller-allocated decoder state"
  - "Filter coefficient tables spu94_adpcm_f0[5] / spu94_adpcm_f1[5]"
  - "19 known-vector unit tests covering all 5 filters and edge cases"
affects: [01-02 encoder, 02-integration, 04-verification]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "ADPCM module follows reverb-core pattern: header in include/spu94/, impl in src/spu94/, tests in tests/unit/"
    - "Decoder state is caller-allocated 4-byte struct (no spu94_state dependency)"
    - "ASR >>6 with +32 bias for prediction rounding (consistent with ADR-0001 discipline)"

key-files:
  created:
    - include/spu94/spu94_adpcm.h
    - src/spu94/spu94_adpcm.c
    - tests/unit/adpcm/test_adpcm_decode.c
    - tests/unit/adpcm/CMakeLists.txt
  modified:
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Filter 1 prediction for old=1000: (60000+32)>>6 = 938 (not 937 as plan stated; 60032 is exactly divisible by 64)"

patterns-established:
  - "ADPCM test helper build_block() for constructing 16-byte blocks from shift/filter/nibble arrays"
  - "Known-vector testing with hand-computed expected values for each filter coefficient pair"

requirements-completed: [ADPCM-01, ADPCM-02, ADPCM-03, ADPCM-07]

# Metrics
duration: 32min
completed: 2026-04-26
---

# Phase 1 Plan 01: ADPCM Decoder Summary

**Bit-faithful PS1 SPU ADPCM decoder with 5-filter prediction, shift/filter clamping, and 19 known-vector tests**

## Performance

- **Duration:** 32 min
- **Started:** 2026-04-26T20:30:25Z
- **Completed:** 2026-04-26T21:02:52Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments
- Standalone ADPCM decoder: 16-byte block to 28 int16 samples, all 5 Sony filter pairs
- Hardware-faithful edge cases: shift 13-15 mapped to 9, filter 5-7 clamped to 4, low-nibble-first ordering
- 19 unit tests covering every filter, shift extreme, saturation boundary, nibble order, state carry, and flag return
- Zero heap, integer-only, no float, no spu94_state dependency -- decoder is a peer module

## Task Commits

Each task was committed atomically:

1. **Task 1: Create ADPCM public header and decoder implementation** - `8bdd664` (feat)
2. **Task 2: Create ADPCM decoder unit tests with known vectors** - `078090e` (test)

## Files Created/Modified
- `include/spu94/spu94_adpcm.h` - Public API: state struct, decode_block signature, filter table externs, block constants
- `src/spu94/spu94_adpcm.c` - Decoder implementation with filter coefficient tables and ASR prediction
- `src/spu94/CMakeLists.txt` - Added spu94_adpcm.c to spu94_obj OBJECT library
- `tests/unit/adpcm/test_adpcm_decode.c` - 19 known-vector unit tests with build_block helper
- `tests/unit/adpcm/CMakeLists.txt` - Test target adpcm_decode_unit linking unity + spu94_static
- `tests/unit/CMakeLists.txt` - Added add_subdirectory(adpcm)

## Decisions Made
- Filter 1 prediction hand-computation corrected: (1000*60+32)>>6 = 60032>>6 = 938, not 937 as plan stated. 60032 is exactly divisible by 64, so there is no ASR rounding ambiguity here -- the plan's arithmetic was simply wrong.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected filter 1 expected value in test**
- **Found during:** Task 2 (unit test creation)
- **Issue:** Plan hand-computed (1000*60+0+32)>>6 = 937, but 60032>>6 = 938 exactly (60032/64 = 938.0)
- **Fix:** Updated test expected value from 937 to 938
- **Files modified:** tests/unit/adpcm/test_adpcm_decode.c
- **Verification:** All 19 tests pass
- **Committed in:** 078090e (Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 arithmetic correction in test vector)
**Impact on plan:** Trivial arithmetic fix. No scope change.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Decoder API is stable and ready for encoder (Plan 02) to embed internally
- Filter coefficient tables are exposed for encoder's optimal-filter search
- Test infrastructure (build_block helper, known-vector pattern) ready for encoder tests
- 83 total ctests (82 existing + 1 new ADPCM suite) all green

## Self-Check: PASSED

- All 4 created files exist on disk
- Both task commits (8bdd664, 078090e) found in git log
- 19/19 ADPCM tests pass, all C unit tests pass with 0 failures
- No /64, no float/double, no malloc/calloc/free in spu94_adpcm.c

---
*Phase: 01-core-codec*
*Completed: 2026-04-26*
