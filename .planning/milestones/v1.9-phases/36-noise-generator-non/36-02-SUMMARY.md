---
phase: 36-noise-generator-non
plan: 02
subsystem: documentation
tags: [adr, noise, lfsr, non, decisions]
dependency_graph:
  requires:
    - phase: 36-01
      provides: noise generator implementation (LFSR + voice pipeline integration)
  provides:
    - ADR-0058 documenting noise LFSR polynomial, seed=1, ADPCM-fetch-during-NON
  affects: [docs/DECISIONS.md]
tech_stack:
  added: []
  patterns: []
key_files:
  created: []
  modified:
    - docs/DECISIONS.md
key_decisions:
  - "ADR-0058 documents three noise generator decisions: XNOR polynomial taps 15,12,11,10, seed=1 from emulator consensus, ADPCM decode always runs for NON voices"
patterns_established: []
requirements_completed: [NON-09]
metrics:
  duration: 2min
  completed: 2026-05-22
---

# Phase 36 Plan 02: Noise Generator ADR-0058 Summary

**ADR-0058 documenting LFSR polynomial (XNOR taps 15,12,11,10), initial seed=1, and ADPCM-fetch-during-NON architectural decision**

## Performance

- **Duration:** 2 min
- **Started:** 2026-05-22T21:28:49Z
- **Completed:** 2026-05-22T21:30:48Z
- **Tasks:** 1
- **Files modified:** 1

## Accomplishments

- ADR-0058 prepended to docs/DECISIONS.md documenting all three noise generator gray areas
- Context section covers LFSR polynomial formulation (XNOR vs XOR, DuckStation table equivalence), initial seed (nocash silent, emulator consensus seed=1, zero is absorbing), and ADPCM decode during NON (side effects preserved for loop flags/ENDX)
- Decision section states SPU-94's resolution for each: direct XNOR computation, seed=1, full ADPCM decode pipeline always runs

## Task Commits

Each task was committed atomically:

1. **Task 1: Write ADR-0058 for noise generator decisions** - `a4782cc` (docs)

## Files Created/Modified

- `docs/DECISIONS.md` - Prepended ADR-0058 (113 lines) before ADR-0057

## Decisions Made

None beyond what ADR-0058 itself documents -- this plan is purely documentation of decisions already made and implemented in 36-01.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Phase 36 (Noise Generator NON) is now complete -- both implementation (36-01) and documentation (36-02) done
- All 9 NON requirements (NON-01 through NON-09) covered
- Ready for next v1.9 feature phase

---
*Phase: 36-noise-generator-non*
*Completed: 2026-05-22*
