---
phase: 09-verification-documentation
plan: 01
subsystem: testing
tags: [golden-files, sha256, dac, regression, conformance]

# Dependency graph
requires:
  - phase: 06-dac-core-implementation
    provides: DAC FIR + noise shaping C implementation
  - phase: 08-i-o-surface
    provides: CLI --dac flag, mixer integration
provides:
  - 55 DAC golden WAV files with SHA-256 sidecars (50 full-pipeline + 5 isolated)
  - Extended regeneration script with --dac and --dac-isolated modes
  - Extended conformance test validating 135-golden corpus
affects: [09-02, 09-03, future-oversampling]

# Tech tracking
tech-stack:
  added: []
  patterns: [dac golden file generation mirroring adpcm pattern, isolated golden for pure-DAC fingerprint]

key-files:
  created:
    - tests/golden/*/dac/*.wav (50 full-pipeline DAC goldens)
    - tests/golden/*/dac/*.wav.sha256 (50 sidecars)
    - tests/golden/dac_isolated/*.wav (5 isolated DAC-only goldens)
    - tests/golden/dac_isolated/*.wav.sha256 (5 sidecars)
  modified:
    - scripts/regenerate_goldens.py
    - tests/conformance/test_goldens_present.py

key-decisions:
  - "DAC isolated goldens use --preset off (CLI requires preset flag; Off = no reverb + DAC = pure DAC fingerprint)"

patterns-established:
  - "DAC goldens follow the adpcm/ subdirectory pattern: tests/golden/<preset>/dac/<input>.wav"
  - "Isolated goldens in tests/golden/dac_isolated/ for pure-model fingerprinting before oversampling rework"

requirements-completed: [DAC-TEST-01]

# Metrics
duration: 9min
completed: 2026-04-30
---

# Phase 9 Plan 01: DAC Golden Files Summary

**55 DAC golden WAVs with SHA-256 sidecars locking down the DAC model's bit-level fingerprint before any oversampling rework**

## Performance

- **Duration:** 9 min
- **Started:** 2026-04-30T17:32:00Z
- **Completed:** 2026-04-30T17:41:00Z
- **Tasks:** 2
- **Files modified:** 112

## Accomplishments
- Generated 50 full-pipeline DAC goldens (10 presets x 5 inputs) in tests/golden/<preset>/dac/
- Generated 5 isolated DAC-only goldens in tests/golden/dac_isolated/ capturing pure DAC model fingerprint
- Extended conformance test from 80 to 135 golden corpus, 282 parametrized tests all passing
- Regeneration produces bit-identical output (--check-dac and --check-dac-isolated both pass)

## Task Commits

Each task was committed atomically:

1. **Task 1: Extend regenerate_goldens.py with --dac and --dac-isolated modes** - `211e864` (feat)
2. **Task 2: Extend test_goldens_present.py for DAC corpus validation** - `2bd358c` (feat)

## Files Created/Modified
- `scripts/regenerate_goldens.py` - Added render_dac_golden(), render_dac_isolated(), DAC_INPUTS constant, --dac/--check-dac/--dac-isolated/--check-dac-isolated argparse flags
- `tests/conformance/test_goldens_present.py` - Added DAC full-pipeline tests (50 existence + 50 format + 3 spot-check), DAC isolated tests (5 existence + 5 format + 2 spot-check), updated count gate to 135
- `tests/golden/*/dac/*.wav` - 50 full-pipeline DAC golden WAVs
- `tests/golden/*/dac/*.wav.sha256` - 50 SHA-256 sidecars
- `tests/golden/dac_isolated/*.wav` - 5 isolated DAC-only golden WAVs
- `tests/golden/dac_isolated/*.wav.sha256` - 5 SHA-256 sidecars

## Decisions Made
- DAC isolated goldens use `--preset off` explicitly because the CLI requires a preset flag. Off preset with DAC enabled produces the pure DAC model fingerprint (no reverb processing), which is the intended behavior for isolated goldens.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] CLI requires --preset flag for isolated DAC goldens**
- **Found during:** Task 1 (render_dac_isolated implementation)
- **Issue:** Plan specified "No preset flag" for isolated goldens, but the spu94 CLI requires `--preset` or `--config`
- **Fix:** Used `--preset off` which gives the Off preset (no reverb) with DAC enabled -- identical to the plan's intent of capturing pure DAC model fingerprint
- **Files modified:** scripts/regenerate_goldens.py
- **Verification:** 5 isolated goldens generated and --check-dac-isolated passes
- **Committed in:** 211e864 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking)
**Impact on plan:** CLI constraint required explicit --preset off; output is functionally identical to plan intent. No scope creep.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- 135-golden corpus complete and validated
- DAC fingerprint locked down for future oversampling rework comparison
- Ready for 09-02 (frequency response measurement) and 09-03 (C unit tests + coverage map)

## Self-Check: PASSED

- All key files verified present
- Both commit hashes found in git log
- Golden counts: 135 WAV, 135 SHA-256

---
*Phase: 09-verification-documentation*
*Completed: 2026-04-30*
