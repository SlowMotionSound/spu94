---
phase: 17-preset-format-extension
plan: 01
subsystem: preset-io
tags: [preset, tempo, serialization, backward-compat]
dependency_graph:
  requires: [16-01, 16-02]
  provides: [tempo-preset-save, tempo-preset-load, tempo-string-conversion]
  affects: [spu94_preset_io.c, spu94_tempo.c, spu94.h]
tech_stack:
  added: []
  patterns: [two-pass-section-parser, sequential-field-accumulation, enum-string-table]
key_files:
  created: []
  modified:
    - include/spu94/spu94.h
    - src/spu94/spu94_tempo.c
    - src/spu94/spu94_preset_io.c
decisions:
  - "Proportional bindings restore metadata only; raw register hex from [registers] is the truth"
  - "Grid bindings auto-snap on load via spu94_set_subdivision (D-07)"
  - "Two-pass field tracking per register: _bind sets intent, _sub/_ref_bpm finalize"
  - "FIXED registers emit only _bind=fixed (no _sub or _ref_bpm per D-02)"
metrics:
  duration_seconds: 1676
  completed: "2026-05-03T18:14:00Z"
  tasks_completed: 3
  tasks_total: 3
  files_modified: 3
---

# Phase 17 Plan 01: Preset Format Tempo Extension Summary

Tempo subsystem state fully serialized in .spu94 preset files via [tempo] INI section with compact musical notation, three-state binding model, and auto-snap-on-load for grid-bound registers.

## Commits

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Add public tempo state getters and string conversion | 52256ae | 7 new API functions in spu94.h + implementations in spu94_tempo.c |
| 2 | Implement [tempo] section in spu94_preset_save | cded161 | [tempo] section emitted after [dac] when BPM > 0 |
| 3 | Implement [tempo] section parsing in spu94_preset_load | 981e1f5 | SECTION_TEMPO parser with two-pass binding restoration |

## What Was Built

**Task 1 -- Public Getters + String Conversion (spu94.h, spu94_tempo.c):**
- `spu94_get_binding_subdivision`: returns subdivision enum per tempo register
- `spu94_get_binding_ref_bpm`: returns reference BPM for proportional bindings
- `spu94_subdivision_to_string` / `spu94_subdivision_from_string`: 15-entry enum-string table
- `spu94_tempo_reg_name`: 10-entry register name table for preset keys
- `spu94_restore_binding_proportional`: preset loader helper (sets metadata without register write)
- `spu94_restore_binding_grid`: preset loader helper (delegates to spu94_set_subdivision)

**Task 2 -- Save Path (spu94_preset_io.c):**
- [tempo] section emitted only when BPM > 0
- Header fields: tempo=, reflection_sync=, comb_sync=
- Per-register fields: _bind=fixed|grid|proportional, _sub=<compact notation>, _ref_bpm=<decimal>
- FIXED registers omit _sub and _ref_bpm (per D-02)

**Task 3 -- Load Path (spu94_preset_io.c):**
- `SECTION_TEMPO` added to section state machine
- `parse_uint16`: safe decimal parser (0-65535 with overflow protection)
- `find_tempo_reg`: prefix-match against tempo register name table
- Two-pass binding restoration using local `tempo_parse_bind[]`/`tempo_parse_sub[]` arrays
- Grid bindings: `_sub=` triggers `spu94_set_subdivision` for auto-snap (D-07)
- Proportional bindings: `_ref_bpm=` triggers `spu94_restore_binding_proportional` (no register overwrite)
- v1.4 presets (no [tempo] section) load without error; tempo state stays at defaults

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed -Wshadow on `name` variable in save path**
- **Found during:** Task 2
- **Issue:** Local variable `name` shadowed the function parameter `name` in `spu94_preset_save`
- **Fix:** Renamed loop variable to `reg_name`
- **Files modified:** src/spu94/spu94_preset_io.c
- **Commit:** cded161

## Verification

- Build: `cmake --build build` exits 0 with zero warnings
- Preset tests: 9/9 pass (backward compatibility confirmed)
- Tempo tests: 4/4 pass (no regression)
- Binding tests: 7/7 pass
- C99/C++ consumer tests: 2/2 pass (header correctness)

## Self-Check: PASSED
