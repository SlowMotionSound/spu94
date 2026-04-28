---
phase: "04-verification-documentation"
plan: "02"
subsystem: "golden-files"
tags: [adpcm, golden, regression, conformance]
dependency_graph:
  requires: []
  provides: [adpcm-golden-corpus, adpcm-regression-gate]
  affects: [scripts/regenerate_goldens.py, tests/conformance/test_goldens_present.py, tests/conformance/CMakeLists.txt]
tech_stack:
  added: []
  patterns: [adpcm-golden-sidecar, chirp-test-signal]
key_files:
  created:
    - tests/golden/*/adpcm/*.wav (30 files)
    - tests/golden/*/adpcm/*.wav.sha256 (30 files)
  modified:
    - scripts/regenerate_goldens.py
    - tests/conformance/test_goldens_present.py
    - tests/conformance/CMakeLists.txt
decisions:
  - "Chirp input uses identical generation to existing sweep (20Hz-20kHz log chirp) per D-05; named separately for ADPCM corpus clarity"
  - "ADPCM goldens generated via 'spu94 reverb --adpcm --preset' CLI invocation with same determinism discipline as reverb goldens"
metrics:
  duration: "519s"
  completed: "2026-04-27T21:30:03Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 60
  files_modified: 3
---

# Phase 04 Plan 02: ADPCM Golden Files Summary

ADPCM golden corpus of 30 WAV + 30 SHA-256 sidecars across 10 presets x 3 inputs (impulse, sine_1khz, chirp), with regeneration script support (--adpcm/--check-adpcm) and ctest regression gate.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Extend regenerate_goldens.py with ADPCM golden generation | 8ab3388 | scripts/regenerate_goldens.py, tests/golden/*/adpcm/* |
| 2 | Update conformance test for ADPCM goldens + ctest gate | 7f57ae9 | tests/conformance/test_goldens_present.py, tests/conformance/CMakeLists.txt |

## What Was Built

### Task 1: ADPCM Golden Generation
- Added `ADPCM_INPUTS = ["impulse", "sine_1khz", "chirp"]` closed allowlist
- Added `chirp` case to `generate_input()` -- identical to existing `sweep` (20Hz-20kHz log chirp, same params)
- Added `render_adpcm_golden()` function using `spu94 reverb --adpcm --preset`
- Added `--adpcm` flag (regenerate 30 ADPCM goldens) and `--check-adpcm` flag (verify sidecars)
- Refactored generation/check logic into shared `_generate_loop()` and `_check_loop()` helpers
- Generated all 30 ADPCM golden WAV files + 30 SHA-256 sidecars
- Verified: `--check-adpcm` passes (30/30), `--check` still passes (50/50 reverb goldens unchanged)

### Task 2: Conformance Test + ctest Gate
- Added `ADPCM_INPUTS` list and 3 new parametrized test functions: `test_adpcm_wav_exists`, `test_adpcm_sidecar_exists_and_format`, `test_adpcm_sidecar_matches_wav`
- Updated `test_expected_count()` from 50 to 80 with `**/*.wav` glob pattern
- Added spot-check triples: hall/impulse, room/sine_1khz, studio_a/chirp
- Registered `adpcm_goldens_regression` ctest target with `golden;adpcm` labels and 300s timeout
- All 167 pytest cases pass; both ctest targets pass (goldens_present + adpcm_goldens_regression)

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

| Check | Result |
|-------|--------|
| `find tests/golden -path "*/adpcm/*.wav" \| wc -l` | 30 |
| `find tests/golden -path "*/adpcm/*.wav.sha256" \| wc -l` | 30 |
| `python3 scripts/regenerate_goldens.py --check-adpcm` | PASS 30/30 |
| `python3 scripts/regenerate_goldens.py --check` | PASS 50/50 (unchanged) |
| `ctest -R goldens_present` | 167 passed |
| `ctest -R adpcm_goldens_regression` | Passed (2.83s) |
| Each of 10 preset dirs has adpcm/ subdir | Yes (10/10) |

## Self-Check: PASSED

All key files verified present. Both task commits (8ab3388, 7f57ae9) confirmed in git log.
