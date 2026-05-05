---
phase: 18-i-o-surfaces
plan: 01
subsystem: cli
tags: [tempo, cli, getopt, integration-tests]
dependency_graph:
  requires: [phase-16-tempo-api, phase-17-preset-tempo]
  provides: [cli-tempo-flag, cli-tempo-tests]
  affects: [cmd_reverb.c, tests/cli/CMakeLists.txt]
tech_stack:
  added: []
  patterns: [getopt_long-case-1013, strtol-validation, pytest-cli-integration]
key_files:
  created:
    - tests/cli/test_cli_tempo.py
  modified:
    - src/cli/cmd_reverb.c
    - tests/cli/CMakeLists.txt
decisions:
  - "--tempo uses strtol with full endptr + range validation (1-65535) per T-18-01 threat model"
  - "Factory presets get default 1/4 subdivision on all FIXED registers; file presets preserve existing bindings (D-05)"
  - "Tempo application placed after spu94_tick and factory fader setup, before user fader overrides (D-04)"
  - "Added test_cli_mixer_dac to ctest list (was previously missing from _cli_tests)"
metrics:
  duration: "6m 47s"
  completed: "2026-05-03T21:24:27Z"
  tasks: 2
  files_changed: 3
---

# Phase 18 Plan 01: CLI --tempo Flag Summary

CLI --tempo flag exposing Phase 16 C core tempo API through getopt_long, with strtol input validation and factory/file preset distinction for subdivision defaults.

## Tasks Completed

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Add --tempo flag to cmd_reverb.c | 10ad620 | getopt_long case 1013, tempo_bpm variable, tempo application block with sync group enable + factory preset 1/4 default + spu94_set_tempo call, help text |
| 2 | Create CLI tempo integration tests and register in ctest | da049e8 | 9 pytest tests (3 valid BPM, 4 invalid, 1 missing-preset, 1 help-text), ctest registration for test_cli_tempo and test_cli_mixer_dac |

## Verification Results

- `cmake --build build --target spu94_cli` exits 0 with zero warnings
- `python3 -m pytest tests/cli/test_cli_tempo.py -v` -- all 9 tests pass
- `ctest -L cli` -- 5/7 pass; 2 pre-existing failures (test_cli_config_and_list, test_cli_error_paths) due to 11th "init" preset not reflected in test expectations (NOT caused by this plan's changes)
- `spu94 reverb --preset hall --tempo 120 input.wav output.wav` exits 0 and produces valid WAV

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Registration] Added test_cli_mixer_dac to ctest**
- **Found during:** Task 2
- **Issue:** test_cli_mixer_dac.py existed but was not registered in the _cli_tests list in tests/cli/CMakeLists.txt (as noted in the plan)
- **Fix:** Added test_cli_mixer_dac to _cli_tests alongside test_cli_tempo
- **Files modified:** tests/cli/CMakeLists.txt
- **Commit:** da049e8

**2. [Rule 2 - Missing Test] Added test_tempo_without_preset_errors**
- **Found during:** Task 2
- **Issue:** Plan's must_haves truth #5 states "--tempo without a preset/config/load-preset still requires one of those flags" but no test was specified for this
- **Fix:** Added test_tempo_without_preset_errors to verify exit code 2 with "required" in stderr
- **Files modified:** tests/cli/test_cli_tempo.py
- **Commit:** da049e8

## Pre-existing Test Failures

Two CLI tests fail before and after this plan's changes:
- `test_cli_config_and_list::test_list_presets_10_lines` -- expects 10 presets, finds 11 (init preset added in a prior phase)
- `test_cli_error_paths::test_unknown_preset_exact_shape` -- expected error message lists 10 presets, actual lists 11

These are out of scope for this plan (pre-existing, unrelated to tempo).

## Self-Check: PASSED
