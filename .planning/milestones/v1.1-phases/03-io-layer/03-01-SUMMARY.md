---
phase: 03-io-layer
plan: 01
subsystem: cli-vag
tags: [vag, cli, adpcm, subcommands]
dependency_graph:
  requires: [Phase 1 ADPCM codec, Phase 2 pipeline integration]
  provides: [VAG file I/O module, CLI subcommand dispatch, ADPCM CLI commands]
  affects: [src/cli/main.c, src/spu94/vag.c, include/spu94/spu94_vag.h]
tech_stack:
  added: []
  patterns: [git-style subcommand dispatch, dual-mono VAG stereo]
key_files:
  created:
    - include/spu94/spu94_vag.h
    - src/spu94/vag.c
    - src/cli/cmd_reverb.c
    - src/cli/cmd_adpcm.c
    - tests/unit/vag/test_vag.c
    - tests/unit/vag/CMakeLists.txt
    - tests/cli/test_cli_adpcm.py
  modified:
    - src/cli/main.c
    - src/cli/CMakeLists.txt
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/cli/test_cli_config_and_list.py
decisions:
  - "D-01 git-style subcommand dispatch with legacy fallback to reverb"
  - "D-02 --adpcm flag enables ADPCM coloration in reverb mode"
  - "D-03 global --help shows subcommand list; reverb --help shows reverb flags"
  - "D-04 stereo VAG uses dual-sequential mono streams"
  - "D-08 zero-heap VAG module with caller-allocated buffers"
metrics:
  duration_minutes: 46
  completed: "2026-04-27T17:52:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 7
  files_modified: 5
---

# Phase 3 Plan 01: VAG Module + CLI Subcommand Dispatch Summary

VAG file format reader/writer added to libspu94 as a zero-heap peer module; CLI restructured into git-style subcommands with adpcm-encode/decode/roundtrip commands, --adpcm reverb flag, and backward-compatible legacy mode.

## Task Results

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | VAG library module + unit tests | 81c4833 | spu94_vag.h, vag.c, test_vag.c |
| 2 | CLI subcommand dispatch + ADPCM commands | dc369f9 | main.c, cmd_reverb.c, cmd_adpcm.c, test_cli_adpcm.py |

## What Was Built

### Task 1: VAG Library Module
- `spu94_vag_read_header()`: validates VAGp magic, parses big-endian fields via shift-based conversion (no ntohl), accepts any version value (ADPCM-IO-03), force null-terminates name field (T-03-03 mitigation)
- `spu94_vag_write_header()`: produces v2 headers with zero-padded 48-byte structure, caller-allocated buffer, zero heap (D-08)
- 5 Unity unit tests: valid header parse, bad magic rejection, any-version acceptance, write-read roundtrip, NULL name safety

### Task 2: CLI Subcommand Dispatch
- `main.c` restructured: argv[1] dispatch to reverb/adpcm-encode/adpcm-decode/adpcm-roundtrip subcommands; bare `--help`/`-h` shows global help (D-03); no subcommand or leading `-` falls through to reverb (D-01 backward compat)
- `cmd_reverb.c`: extracted full reverb pipeline from old main.c with new `--adpcm` flag that calls `spu94_set_adpcm_enabled(state, 1)` before preset loading (ADPCM-IO-02)
- `cmd_adpcm.c`: three subcommand handlers:
  - `adpcm-encode`: WAV to VAG with dual-sequential stereo (D-04), end flag on last block (ADPCM-IO-04)
  - `adpcm-decode`: VAG to WAV with dual-mono stereo detection, stop on flag 0x01/0x07 (ADPCM-IO-03), capped allocation (T-03-02)
  - `adpcm-roundtrip`: in-memory encode+decode with no intermediate file (D-02)
- 7 new CLI integration tests, all 60 CLI tests pass

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Em-dash character in error message**
- **Found during:** Task 2
- **Issue:** Extracting the reverb pipeline to cmd_reverb.c used ASCII `--` instead of the UTF-8 em-dash that the original main.c used, breaking the `test_unknown_preset_exact_shape` test which checks the exact error message format.
- **Fix:** Used `\xE2\x80\x94` hex escape to produce the correct UTF-8 em-dash.
- **Files modified:** src/cli/cmd_reverb.c

**2. [Rule 1 - Bug] Global help test expectations**
- **Found during:** Task 2
- **Issue:** `test_help_exits_0` in test_cli_config_and_list.py expected `spu94 --help` to contain reverb-specific flags (--preset, --config, etc.), but the new global help shows subcommand names per D-03.
- **Fix:** Updated test to check for subcommand names in global help; added `test_reverb_help_shows_options` to verify reverb-specific flags are in `spu94 reverb --help`.
- **Files modified:** tests/cli/test_cli_config_and_list.py

**3. [Rule 1 - Bug] Unreachable --help check in dispatch**
- **Found during:** Task 2
- **Issue:** The plan's dispatch pattern placed `--help`/`-h` checks inside the `argv[1][0] != '-'` guard, making them unreachable since `--help` starts with `-`.
- **Fix:** Moved `--help`/`-h` check before the subcommand dispatch guard so `spu94 --help` correctly shows global help.
- **Files modified:** src/cli/main.c

## Verification Results

- `ctest -R vag_read_write --output-on-failure`: PASSED (5/5 tests)
- `ctest -R "adpcm_decode_unit|adpcm_encode_unit"`: PASSED (all existing ADPCM tests)
- `pytest tests/cli/ -x -v`: 60/60 PASSED
- `build/src/cli/spu94 --help`: shows all four subcommands
- Legacy mode (`spu94 --preset hall in.wav out.wav`): works identically to before

## Known Stubs

None -- all code paths are fully wired.

## Threat Flags

None -- all threat model mitigations (T-03-01 through T-03-03) implemented as specified.

## Self-Check: PASSED

All 7 created files verified on disk. Both task commits (81c4833, dc369f9) verified in git log.
