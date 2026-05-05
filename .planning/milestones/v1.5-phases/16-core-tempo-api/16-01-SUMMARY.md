---
phase: 16-core-tempo-api
plan: 01
subsystem: dsp
tags: [tempo, subdivision, bpm, integer-arithmetic, c99]

# Dependency graph
requires:
  - phase: 15-verification
    provides: "Stable v1.4 codebase with preset system"
provides:
  - "spu94_subdivision_t enum (15 musical subdivisions)"
  - "spu94_tempo_reg_t enum (10 tempo-tracked registers)"
  - "spu94_binding_state_t enum (fixed/grid/proportional)"
  - "spu94_set_tempo / spu94_get_tempo API"
  - "spu94_subdivision_valid query function"
  - "spu94_set_subdivision (partial -- register write deferred)"
  - "Integer delay formula at 22050 Hz with truncation"
  - "Sync group toggles (reflection_sync, comb_sync)"
  - "Per-register binding state storage in spu94_state"
affects: [16-core-tempo-api plan-02, 16-core-tempo-api plan-03, 17-io-surfaces]

# Tech tracking
tech-stack:
  added: []
  patterns: ["Compile-time subdivision ratio table (rational fractions)", "Tempo register binding state tracking"]

key-files:
  created:
    - "src/spu94/spu94_tempo.c"
    - "tests/unit/tempo/test_tempo_basic.c"
    - "tests/unit/tempo/test_tempo_snap.c"
    - "tests/unit/tempo/CMakeLists.txt"
  modified:
    - "include/spu94/spu94.h"
    - "src/spu94/spu94_state_internal.h"
    - "src/spu94/CMakeLists.txt"
    - "tests/unit/CMakeLists.txt"

key-decisions:
  - "SPU94_INVALID_ARG used for all tempo overflow/range rejections (no new error code)"
  - "Subdivision table uses uint8 numerator/denominator pairs (max 3/32)"
  - "Binding state stored as parallel arrays (state[10], sub[10], ref_bpm[10]) for 44 bytes total"

patterns-established:
  - "Tempo API: error-returning setter for BPM with NULL/zero guards"
  - "Subdivision validity query: stateless function taking bpm+subdivision, returns 0/1"
  - "Binding state: per-register enum tracked in spu94_state, indexed by spu94_tempo_reg_t"

requirements-completed: [TEMPO-01, TEMPO-03, TEMPO-04]

# Metrics
duration: 21min
completed: 2026-05-03
---

# Phase 16 Plan 01: Core Tempo API Summary

**Integer-only tempo computation engine with 15-entry subdivision table, BPM state storage, and 20 passing unit tests at 22050 Hz**

## Performance

- **Duration:** 21 min
- **Started:** 2026-05-03T05:18:36Z
- **Completed:** 2026-05-03T05:39:10Z
- **Tasks:** 3
- **Files modified:** 8

## Accomplishments
- Tempo type system established: 3 new enums (subdivision, tempo_reg, binding_state) + 10 API function prototypes in public header
- spu94_tempo.c implements the integer delay formula `(60*22050*num)/(bpm*den)` with compile-time ratio table and all safety guards
- 20 unit tests prove TEMPO-01 (BPM set/get roundtrip), TEMPO-03 (all 15 subdivisions valid at typical BPMs), and TEMPO-04 (known-vector formula correctness at 22050 Hz)
- No regressions -- entire existing test suite continues passing

## Task Commits

Each task was committed atomically:

1. **Task 1: Declare tempo types, enums, and API prototypes** - `3544503` (feat)
2. **Task 2: Implement spu94_tempo.c with subdivision table** - `e39f7d4` (feat)
3. **Task 3: Create tempo unit tests and verify correctness** - `dbb25e7` (test)

## Files Created/Modified
- `include/spu94/spu94.h` - Tempo API section: 3 enums, 10 function prototypes
- `src/spu94/spu94_state_internal.h` - 44 bytes of tempo fields (bpm, sync toggles, binding arrays)
- `src/spu94/spu94_tempo.c` - 195 LOC: subdivision table, compute function, API implementations
- `src/spu94/CMakeLists.txt` - Register spu94_tempo.c in OBJECT library
- `tests/unit/tempo/CMakeLists.txt` - Build config for 2 test executables
- `tests/unit/tempo/test_tempo_basic.c` - 106 LOC: 9 TEMPO-01 tests
- `tests/unit/tempo/test_tempo_snap.c` - 144 LOC: 11 TEMPO-03/04 tests
- `tests/unit/CMakeLists.txt` - Register tempo subdirectory

## Decisions Made
- Used SPU94_INVALID_ARG for all tempo rejections (overflow, range, bpm=0) rather than adding a new error code -- consistent with existing API style
- Subdivision table stored as static const struct array with designated initializers -- 30 bytes in .rodata, zero runtime cost
- Per-register binding state uses parallel uint8/uint16 arrays rather than a struct-of-arrays -- simpler indexing, fits naturally in the existing state struct layout
- set_subdivision stores binding state but defers actual register write to Plan 02 (partial implementation per plan spec)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Plan 02 can now implement auto-resnap loop (BPM change triggers grid-bound register recalculation)
- Plan 02 can wire spu94_set_subdivision to actual register writes via spu94_set_reg_u16
- Plan 03 can implement write-interception for binding state transitions (grid-bound -> proportional on manual write)
- All types, enums, and function signatures are stable -- downstream plans consume without header changes

## Self-Check: PASSED

All 4 created files exist on disk. All 3 task commit hashes found in git log.

---
*Phase: 16-core-tempo-api*
*Completed: 2026-05-03*
