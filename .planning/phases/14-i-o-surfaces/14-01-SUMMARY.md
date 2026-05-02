---
phase: 14-i-o-surfaces
plan: 01
subsystem: cli
tags: [cli, preset-dump, load-preset, serialization, i-o-surfaces]

requires:
  - phase: 13-core-preset-api
    plan: 01
    provides: "spu94_preset_save function, SPU94_PRESET_BUF_SIZE constant"
  - phase: 13-core-preset-api
    plan: 02
    provides: "spu94_preset_load parser"
provides:
  - "preset-dump subcommand -- exports factory presets as .spu94 text files"
  - "--load-preset flag on reverb -- reads .spu94 files before processing"
  - "Three-way mutual exclusion: --preset, --config, --load-preset"
  - "CLI edit-process loop: dump -> hand-edit -> --load-preset workflow"
affects: [14-02-PLAN]

tech-stack:
  added: []
  patterns: ["getopt_long subcommand with factory preset lookup and spu94_preset_save serialization", "fread with SPU94_PRESET_BUF_SIZE cap and NUL termination for safe .spu94 file loading"]

key-files:
  created:
    - "src/cli/cmd_preset_dump.c"
  modified:
    - "src/cli/cmd_reverb.c"
    - "src/cli/main.c"
    - "src/cli/CMakeLists.txt"

key-decisions:
  - "Default faders guard changed from loaded_pid!=0 to loaded_pid>0 so --load-preset and JSON configs preserve their own fader values"
  - "preset-dump uses static state_buf + heap work_buf matching cmd_reverb.c pattern"
  - "fread capped to SPU94_PRESET_BUF_SIZE-1 bytes with NUL termination for T-14-01/T-14-04 threat mitigations"

requirements-completed: [PRE-06, PRE-07]

duration: 6min
completed: 2026-05-02
---

# Phase 14 Plan 01: CLI Preset I/O Summary

**preset-dump subcommand exports factory presets as .spu94 text; reverb --load-preset reads them back with three-way mutual exclusion and fader layering**

## Performance

- **Duration:** 6 min
- **Started:** 2026-05-02T16:35:30Z
- **Completed:** 2026-05-02T16:41:19Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Created cmd_preset_dump.c: standalone subcommand exporting any of the 11 factory presets as .spu94 INI-style text
- Wired preset-dump into main.c dispatch (extern + strcmp) and global help text
- Added --load-preset flag to reverb subcommand with fread/spu94_preset_load pipeline
- Expanded mutual exclusion from two-way (--preset/--config) to three-way (--preset/--config/--load-preset)
- Changed default-faders guard to loaded_pid>0 so .spu94 files and JSON configs preserve their own mixer fader values
- Verified round-trip: preset-dump --preset hall -o file.spu94 then reverb --load-preset file.spu94 processes audio correctly

## Task Commits

Each task was committed atomically:

1. **Task 1: Create cmd_preset_dump.c subcommand and wire into main.c + CMake** - `027c5d3` (feat)
2. **Task 2: Add --load-preset flag to reverb subcommand with mutual exclusion and layering** - `462ab07` (feat)

## Files Created/Modified
- `src/cli/cmd_preset_dump.c` - New file: preset-dump subcommand with --preset, --name, -o, --list-presets flags (148 LOC)
- `src/cli/cmd_reverb.c` - Added --load-preset flag, three-way mutual exclusion, fader guard fix (+44/-10 lines)
- `src/cli/main.c` - Added cmd_preset_dump extern, dispatch entry, and help text (+4 lines)
- `src/cli/CMakeLists.txt` - Added cmd_preset_dump.c to spu94_cli source list (+1 line)

## Decisions Made
- Changed default-faders guard from `loaded_pid != 0` to `loaded_pid > 0` -- factory presets (pid 1-10) get unity faders applied, Off (pid 0) stays silent, --load-preset and JSON configs (pid -1) preserve their own fader values from the file
- Used short-code 1012 for --load-preset long_opt continuing the existing numeric sequence (1001-1011)
- Error paths in --load-preset branch return 1 (runtime failure) not 2 (user error), matching the plan's threat model for file I/O failures

## Deviations from Plan

None - plan executed exactly as written.

## Threat Mitigations Verified
- T-14-01: fread capped to SPU94_PRESET_BUF_SIZE-1 (4095 bytes); spu94_preset_load validates keys
- T-14-02: fopen failure in preset-dump returns error code 1 immediately; no retry loop
- T-14-04: preset_buf is stack-allocated with fixed 4096-byte size, NUL-terminated after fread

## Self-Check: PASSED

All 4 created/modified files verified present. Both task commits (027c5d3, 462ab07) verified in git log.

---
*Phase: 14-i-o-surfaces*
*Completed: 2026-05-02*
