---
phase: 17-preset-format-extension
plan: 02
subsystem: preset-io-tests
tags: [preset, tempo, testing, roundtrip, backward-compat]
dependency_graph:
  requires: [17-01]
  provides: [tempo-preset-test-suite, ref_bpm-parser-fix]
  affects: [spu94_preset_io.c, test_preset_tempo_roundtrip.c, CMakeLists.txt]
tech_stack:
  added: []
  patterns: [tick-before-read-pattern, hand-crafted-preset-strings, known-vector-assertions]
key_files:
  created:
    - tests/unit/preset/test_preset_tempo_roundtrip.c
  modified:
    - tests/unit/preset/CMakeLists.txt
    - src/spu94/spu94_preset_io.c
decisions:
  - "d-prefix registers require spu94_tick() before spu94_get_reg_u16 reads (tick-latched semantics from D-08)"
  - "Proportional binding round-trip proven: raw register hex preserved, ref_bpm/subdivision metadata restored"
  - "Auto-snap-on-load proven: grid bindings recalculate from BPM+subdivision regardless of stale [registers] hex"
metrics:
  duration_seconds: 900
  completed: "2026-05-03T18:32:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 17 Plan 02: Tempo Preset Round-Trip Test Suite Summary

12 tests proving [tempo] section serialization correctness: save format verification, round-trip fidelity for all three binding states, backward compatibility with v1.4 presets, auto-snap-on-load with known-vector validation, and subdivision string conversion round-trip.

## Commits

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Create tempo preset round-trip test suite | 80b72ce | 12 tests + CMake registration + ref_bpm parser bug fix |
| 2 | Register test in CMakeLists and run full suite | 80b72ce | (merged with Task 1 -- test registration required for compilation) |

## What Was Built

**Task 1 -- Test Suite + Parser Bug Fix (test_preset_tempo_roundtrip.c, CMakeLists.txt, spu94_preset_io.c):**

12 tests in `test_preset_tempo_roundtrip.c`:
1. `test_save_no_tempo_no_section` -- BPM=0 produces no [tempo] section
2. `test_save_tempo_section_present` -- BPM>0 produces [tempo] with tempo= line
3. `test_save_sync_toggles` -- reflection_sync/comb_sync appear in output
4. `test_save_grid_binding_format` -- grid binding emits _bind=grid + _sub=notation
5. `test_save_fixed_binding_no_sub` -- fixed binding emits only _bind=fixed (D-02)
6. `test_roundtrip_tempo_bpm` -- BPM value round-trips through save/load
7. `test_roundtrip_sync_toggles` -- sync toggles round-trip
8. `test_roundtrip_grid_binding` -- grid binding + register value (1378) round-trips
9. `test_roundtrip_proportional_binding` -- proportional state + raw value (3000) + ref_bpm (100) round-trips
10. `test_backward_compat_v14_preset` -- v1.4 preset loads with tempo inactive, all FIXED
11. `test_auto_snap_on_load_correctness` -- stale hex overwritten by auto-snap (2362, 1181 at 140 BPM)
12. `test_subdivision_string_roundtrip` -- all 15 subdivisions survive string conversion

**Task 2 -- CMake Registration:**
- `add_executable`, `target_link_libraries`, `target_include_directories`, `add_test`, `set_tests_properties` with LABELS "preset;tempo"
- Committed together with Task 1 (inseparable -- test file needs CMake registration to compile)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed strrchr suffix-split bug in tempo section parser**
- **Found during:** Task 1 (test_roundtrip_proportional_binding failing)
- **Issue:** `strrchr(key, '_')` on `"dAPF1_ref_bpm"` found the underscore between "ref" and "bpm", producing prefix="dAPF1_ref" and suffix="bpm". The register name "dAPF1_ref" doesn't match any tempo register, so `_ref_bpm` lines were silently discarded.
- **Fix:** Replaced strrchr approach with explicit suffix-first matching: check if key ends with `_bind` (5 chars), `_sub` (4 chars), or `_ref_bpm` (8 chars), then extract prefix from the remaining characters.
- **Files modified:** src/spu94/spu94_preset_io.c
- **Commit:** 80b72ce

**2. [Rule 3 - Blocking] Added spu94_tick() calls for d-prefix register readability**
- **Found during:** Task 1 (4 tests returning 0 for d-prefix register reads)
- **Issue:** d-prefix registers use TICK_LATCHED write policy (D-08). After `spu94_set_reg_u16` or `spu94_preset_load`, values are in `pending_values` only. `spu94_get_reg_u16` reads from `reg_values` (active slot), returning 0 until `spu94_tick` flushes pending to active.
- **Fix:** Added `spu94_tick(state)` calls before save (to capture register values in output) and after load (to make d-prefix values readable via `spu94_get_reg_u16`). This models real-world usage where the audio engine ticks between preset load and register reads.
- **Files modified:** tests/unit/preset/test_preset_tempo_roundtrip.c
- **Commit:** 80b72ce

## Verification

- Build: `cmake --build build` exits 0 with zero warnings
- Tempo test: 12/12 pass (all test functions green)
- Preset suite: 8/8 pass (no regression in existing preset tests)
- Full C unit suite: 75/75 pass (100%, zero failures)
- Integration tests (76-112): timeouts in rt_safety stress tests (pre-existing, >30s needed) and disk-space exhaustion (environmental) -- unrelated to this plan's changes

## Known Stubs

None -- all tests exercise real API behavior with concrete assertions.

## Self-Check: PASSED
