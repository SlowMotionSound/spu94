---
phase: 09-verification-documentation
plan: 03
subsystem: testing
tags: [coverage-map, adpcm, dac, ci-enforcement, documentation]

# Dependency graph
requires:
  - phase: 09-01
    provides: DAC golden files (55 WAVs) and extended conformance test
  - phase: 09-02
    provides: DAC characterization script and integration C tests
provides:
  - Complete COVERAGE.md with ADPCM backfill (5 rows) and DAC Model (17 rows)
  - Updated check_coverage.py supporting new sections and tools/ paths
affects: [future-oversampling, future-phases]

# Tech tracking
tech-stack:
  added: []
  patterns: [coverage section per coloration stage, tools/ path validation in CI]

key-files:
  created: []
  modified:
    - docs/COVERAGE.md
    - scripts/ci/check_coverage.py

key-decisions:
  - "Widened check_coverage.py _TEST_REF_RE regex to accept tools/ paths alongside tests/ paths; tools/ entries validate file existence only (no ctest dispatch)"
  - "Passband ripple tolerance in DAC FIR frequency response row updated to 0.15 dB (matching 09-02 at-rate characterization result, not the 8x design spec of 0.078 dB)"

patterns-established:
  - "Each coloration stage (reverb, ADPCM, DAC) gets its own coverage section in COVERAGE.md"

requirements-completed: [DAC-TEST-04]

# Metrics
duration: 5min
completed: 2026-04-30
---

# Phase 9 Plan 03: ADPCM + DAC Coverage Map Summary

**COVERAGE.md updated with 22 new rows mapping ADPCM backfill (5 rows) and DAC Model (17 rows) to their tests, all passing CI enforcement**

## Performance

- **Duration:** 5 min
- **Started:** 2026-04-30T18:14:58Z
- **Completed:** 2026-04-30T18:19:49Z
- **Tasks:** 1
- **Files modified:** 2

## Accomplishments
- Added ## ADPCM Coverage section backfilling 5 existing v1.1 test mappings (decode, encode, binding, goldens, integration)
- Added ## DAC Model Coverage section with 17 behavior-to-test rows covering FIR, noise, toggles, integration, bindings, and goldens
- Updated check_coverage.py to recognize new sections and accept tools/ path references
- All 99 coverage rows pass CI validation (up from 77)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add DAC Model Coverage and ADPCM Coverage sections to COVERAGE.md** - `3c880e9` (feat)

## Files Created/Modified
- `docs/COVERAGE.md` - Added ## ADPCM Coverage (5 rows) and ## DAC Model Coverage (17 rows) between Per-Behavior and Per-Spec-Paragraph sections
- `scripts/ci/check_coverage.py` - Added new section names to recognized_sections set; widened _TEST_REF_RE to accept tools/ paths; skip ctest dispatch for tools/ entries

## Decisions Made
- **tools/ path support in CI validator**: The plan references `tools/dac_measure.py::dac_measure` for frequency response and noise spectral rows. This path is not a ctest target and doesn't live under `tests/`. Rather than excluding these rows or registering dac_measure as a ctest, check_coverage.py was widened to accept `tools/` prefixed paths with file-existence-only validation (no ctest dispatch). This keeps the coverage map complete while respecting that measurement scripts are standalone tools.
- **Passband ripple tolerance**: Plan listed 0.078 dB for FIR frequency response. 09-02 established that at-rate cascade tolerance is 0.15 dB (Phase 5's 0.078 dB spec applies to the 8x composite design, not the at-rate implementation). Coverage row uses 0.15 dB.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] check_coverage.py didn't recognize new section names**
- **Found during:** Task 1 (pre-implementation analysis)
- **Issue:** check_coverage.py's `recognized_sections` set only contained Per-Register, Per-Behavior, and Per-Spec-Paragraph. New ADPCM and DAC sections would be silently ignored by the validator.
- **Fix:** Added `"## ADPCM Coverage"` and `"## DAC Model Coverage"` to the recognized_sections set.
- **Files modified:** scripts/ci/check_coverage.py
- **Verification:** check_coverage.py now validates all 99 rows including the 22 new ones.
- **Committed in:** 3c880e9

**2. [Rule 3 - Blocking] check_coverage.py rejected tools/ paths**
- **Found during:** Task 1 (pre-implementation analysis)
- **Issue:** `_TEST_REF_RE` regex only matched paths starting with `tests/`. The plan's DAC FIR frequency response and noise spectral rows reference `tools/dac_measure.py` which would fail the regex match and be treated as malformed.
- **Fix:** Widened regex to `(?:tests|tools)/`. Added `tools/` path skip in ctest dispatch (file-existence-only validation for measurement scripts).
- **Files modified:** scripts/ci/check_coverage.py
- **Verification:** Both tools/dac_measure.py rows pass validation.
- **Committed in:** 3c880e9

---

**Total deviations:** 2 auto-fixed (2 blocking)
**Impact on plan:** Both fixes were necessary for the CI validator to actually enforce the new coverage rows. Without them, the rows would either be silently ignored or rejected. No scope creep.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- COVERAGE.md is now the single source of truth for all three coloration stages (reverb, ADPCM, DAC)
- 99 coverage rows all passing CI enforcement
- Phase 9 verification + documentation complete (all 3 plans executed)

## Self-Check: PASSED

- [x] docs/COVERAGE.md contains ## ADPCM Coverage
- [x] docs/COVERAGE.md contains ## DAC Model Coverage
- [x] ADPCM section has 5 data rows
- [x] DAC Model section has 17 data rows
- [x] check_coverage.py exits 0 (99 rows green)
- [x] Commit 3c880e9 exists in git log

---
*Phase: 09-verification-documentation*
*Completed: 2026-04-30*
