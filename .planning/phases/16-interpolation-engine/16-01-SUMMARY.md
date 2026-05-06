---
phase: 16-interpolation-engine
plan: 01
subsystem: core-dsp
tags: [interpolation, morph, presets, tdd]
dependency_graph:
  requires: [spu94_presets, spu94_set_reg_i16, spu94_set_reg_u16, spu94_reg_type]
  provides: [spu94_interp_set_morph, SPU94_INTERP_WAYPOINT_COUNT]
  affects: [include/spu94/spu94.h, src/spu94/CMakeLists.txt]
tech_stack:
  added: []
  patterns: [waypoint-table-interpolation, perceptual-ordering, fixed-register-exclusion]
key_files:
  created:
    - src/spu94/spu94_interp.c
    - tests/unit/interp/test_interp.c
    - tests/unit/interp/CMakeLists.txt
  modified:
    - include/spu94/spu94.h
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - "Waypoint positions use frac==0/1 direct-write bypass for bit-identical output (INTERP-04)"
  - "NaN position falls through clamp logic to seg=0 frac=0 producing valid Half Echo state (T-16-02)"
metrics:
  duration: "50m 19s"
  completed: "2026-05-06T00:31:32Z"
  tasks: 2
  files_created: 3
  files_modified: 3
  loc_added: 505
---

# Phase 16 Plan 01: Interpolation Engine Summary

TDD implementation of the preset interpolation engine: float morph position [0.0, 1.0] produces linearly interpolated SPU register values along a 9-preset perceptual continuum with signed-aware math and bit-identical waypoint output.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Tests for interpolation engine (RED) | 208e93d | tests/unit/interp/test_interp.c, tests/unit/interp/CMakeLists.txt |
| 2 | Implement interpolation engine (GREEN) | 0299cb9 | src/spu94/spu94_interp.c, include/spu94/spu94.h |

## Implementation Details

### Architecture

The interpolation engine is a single function (`spu94_interp_set_morph`) that:

1. Clamps position to [0.0, 1.0]
2. Maps position to a segment index (0-7) and fractional distance within that segment
3. Looks up the two adjacent presets from a static waypoint table
4. For each of 35 registers:
   - Fixed registers (vLOUT/vROUT/vLIN/vRIN/mBASE): write constant values
   - At exact waypoints (frac == 0.0 or 1.0): write preset value directly (no math)
   - Between waypoints: linearly interpolate using signed or unsigned arithmetic per register type
5. Writes through existing engine-layer setters (spu94_set_reg_i16/u16) so ADR-0005 write-timing policy is honored automatically

### Waypoint Order (perceptual, confirmed by ear)

```
0: Half Echo (0.000) -> 1: Room (0.125) -> 2: Studio A (0.250) ->
3: Studio B (0.375) -> 4: Studio C (0.500) -> 5: Hall (0.625) ->
6: Space Echo (0.750) -> 7: Echo (0.875) -> 8: Delay (1.000)
```

### Test Coverage

7 sub-tests covering all 5 INTERP requirements:
- `test_interp_null_state` -- T-16-03 NULL guard
- `test_interp_clamp_below_zero` -- position < 0.0 produces Half Echo
- `test_interp_clamp_above_one` -- position > 1.0 produces Delay
- `test_interp_waypoint_identity` -- 9 waypoints x 35 registers bit-identical (INTERP-04)
- `test_interp_fixed_registers` -- 5 positions x 5 fixed registers constant (INTERP-03)
- `test_interp_midpoint_linear` -- Studio B/C midpoint matches integer average (INTERP-02)
- `test_interp_signed_no_wraparound` -- vCOMB2 and vWALL negative interpolation (INTERP-05)

## Deviations from Plan

None -- plan executed exactly as written.

## TDD Gate Compliance

- RED gate: commit 208e93d (`test(16-01): add failing tests...`) -- compilation fails on undefined `spu94_interp_set_morph`
- GREEN gate: commit 0299cb9 (`feat(16-01): implement preset interpolation engine...`) -- all 7 tests pass
- REFACTOR gate: not needed -- implementation is clean and minimal (133 LOC)

## Verification Results

- `ctest -R test_interp --output-on-failure`: 1/1 passed (7 sub-tests)
- All C unit tests pass (0 regressions in preset, process, registers, buffer, reverb, fir, adpcm, vag, dac suites)
- `nm -D libspu94.so | grep interp`: `spu94_interp_set_morph` exported as T symbol
- Pre-existing environment-dependent failures (Python packaging, CLI pytest, fuzz timeout) unrelated to this change

## Known Stubs

None -- all code paths are fully wired to production data sources (spu94_presets[] table).

## Requirements Fulfilled

| REQ-ID | Evidence |
|--------|----------|
| INTERP-01 | Position maps to segment+fraction; tested at 0.0, 0.0625, 0.125, boundaries |
| INTERP-02 | Linear interpolation verified at morph 0.4375 (Studio B/C midpoint) |
| INTERP-03 | Fixed registers tested at 5 positions across full range |
| INTERP-04 | All 9 waypoints x 35 registers bit-identical to Sony presets |
| INTERP-05 | vCOMB2 (-17184 to -16688) and vWALL (-31488 to -17792) midpoints verified |

## Self-Check: PASSED

- FOUND: src/spu94/spu94_interp.c
- FOUND: tests/unit/interp/test_interp.c
- FOUND: tests/unit/interp/CMakeLists.txt
- FOUND: commit 208e93d (RED phase)
- FOUND: commit 0299cb9 (GREEN phase)
