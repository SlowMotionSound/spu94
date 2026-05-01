---
phase: 12-verification-characterization
plan: 01
subsystem: dsp, cli, testing
tags: [dac, golden-files, sha256, ctypes, cli-flag, regression-gate]

# Dependency graph
requires:
  - phase: 11-noise-recalibration-integration
    provides: v1.3 true 8x oversampled DAC processing (spu94_set_dac_true_oversample API)
provides:
  - "--no-dac-true-oversample CLI flag for v1.2/v1.3 DAC mode toggle"
  - "Python ctypes binding for set/get_dac_true_oversample"
  - "v1.3 DAC golden files (55 total) with SHA-256 sidecars"
  - "Passing regression gate for all DAC goldens"
affects: [12-02-characterization-script, phase-13-plugin]

# Tech tracking
tech-stack:
  added: []
  patterns: [cli-flag-dispatch-pattern, ctypes-binding-pattern, golden-regeneration-workflow]

key-files:
  created: []
  modified:
    - src/cli/cmd_reverb.c
    - python/spu94/_binding.py
    - tests/golden/*/dac/*.wav
    - tests/golden/*/dac/*.wav.sha256
    - tests/golden/dac_isolated/*.wav
    - tests/golden/dac_isolated/*.wav.sha256

key-decisions:
  - "Used opcode 1011 for --no-dac-true-oversample (next available after 1010 --reverb)"
  - "Negated flag convention (--no-dac-true-oversample) matches existing --no-dac-fir/--no-dac-noise pattern"

patterns-established:
  - "DAC sub-toggle CLI flags: --no-dac-* pattern with spu94_set_dac_* dispatch"

requirements-completed: [INT-02]

# Metrics
duration: 5min
completed: 2026-05-01
---

# Phase 12 Plan 01: Surface Access + Golden Regeneration Summary

**CLI --no-dac-true-oversample flag + Python dac_true_oversample binding, 55 v1.3 DAC golden files regenerated with SHA-256 sidecars**

## Performance

- **Duration:** 5 min
- **Started:** 2026-05-01T19:07:22Z
- **Completed:** 2026-05-01T19:12:00Z
- **Tasks:** 2
- **Files modified:** 74 (2 source + 72 golden files)

## Accomplishments
- CLI --no-dac-true-oversample flag wired end-to-end (help, long_opts, bool, case, DAC dispatch)
- Python ctypes binding declares set/get_dac_true_oversample with correct argtypes/restype
- 55 DAC golden files regenerated under v1.3 true 8x oversampled path (50 full-pipeline + 5 isolated)
- SHA-256 regression gate: PASS 50/50 DAC + PASS 5/5 isolated
- Conformance gate: 135 .wav + 135 .sha256 count unchanged
- v1.2 archive (tests/golden_v1.2/) preserved untouched

## Task Commits

Each task was committed atomically:

1. **Task 1: Add --no-dac-true-oversample CLI flag + Python binding** - `7fdaaa5` (feat)
2. **Task 2: Regenerate v1.3 DAC golden files + verify regression gate** - `b1cb642` (feat)

## Files Created/Modified
- `src/cli/cmd_reverb.c` - Added --no-dac-true-oversample flag (help, long_opts 1011, bool, case, DAC dispatch calling spu94_set_dac_true_oversample(state, 0))
- `python/spu94/_binding.py` - Added ctypes declarations for spu94_set_dac_true_oversample and spu94_get_dac_true_oversample
- `tests/golden/*/dac/*.wav` - 36 full-pipeline DAC goldens updated to v1.3 output (non-zero presets x non-silence inputs)
- `tests/golden/*/dac/*.wav.sha256` - 36 corresponding SHA-256 sidecars updated
- `tests/golden/dac_isolated/*.wav` - Unchanged (Off preset produces silence regardless of DAC mode)
- `tests/golden/dac_isolated/*.wav.sha256` - Unchanged (same reason)

## Decisions Made
- Used opcode 1011 for the new CLI flag (next available after 1010 used by --reverb), following the project's sequential opcode assignment pattern.
- Followed the negated flag convention (--no-dac-true-oversample) consistent with --no-dac-fir and --no-dac-noise, since the default is v1.3 enabled (dac_true_oversample=1).

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- CLI and Python surfaces ready for Plan 12-02 characterization script (CMP-02)
- v1.3 golden baseline established; regression gate operational
- v1.2 archive preserved for A/B comparison measurements

## Self-Check: PASSED

All key files verified present. All commit hashes verified in git log.

---
*Phase: 12-verification-characterization*
*Completed: 2026-05-01*
