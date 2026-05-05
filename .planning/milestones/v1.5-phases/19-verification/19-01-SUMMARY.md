---
phase: 19-verification
plan: 01
subsystem: tempo-verification
tags: [testing, tempo, preset, integration, known-vectors]
dependency_graph:
  requires: [16-01, 16-02, 16-03, 17-01, 17-02]
  provides: [TEMPO-10]
  affects: []
tech_stack:
  added: []
  patterns: [known-vector sweep, dual-state preset round-trip, live-resnap proof]
key_files:
  created:
    - tests/unit/tempo/test_tempo_sweep.c
    - tests/unit/preset/test_preset_tempo_full_roundtrip.c
  modified:
    - tests/unit/tempo/CMakeLists.txt
    - tests/unit/preset/CMakeLists.txt
decisions: []
metrics:
  duration: 261s
  completed: "2026-05-04T01:20:08Z"
  tasks: 2/2
  files_created: 2
  files_modified: 2
---

# Phase 19 Plan 01: Tempo Verification Summary

75 known-vector BPM x subdivision sweep + 10-register preset round-trip with mixed binding states and live auto-resnap proof.

## Tasks Completed

### Task 1: BPM x subdivision sweep test (75 known-vector assertions)
**Commit:** b898f13

Created `tests/unit/tempo/test_tempo_sweep.c` with two tests:
- `test_sweep_all_bpm_subdivisions`: 5 BPMs (60, 90, 120, 140, 180) x 15 subdivisions = 75 assertions. Each verifies the integer formula `(60*22050*num)/(bpm*den)` against the C core output via `spu94_set_subdivision` + `spu94_tick` + `spu94_get_reg_u16`. All 75 also confirm `spu94_subdivision_valid == 1` and `spu94_get_binding_state == SPU94_BIND_GRID`.
- `test_sweep_binding_state_reset`: cycles all 15 subdivisions through GRID -> FIXED via `spu94_set_binding_fixed`.

Spot-checked reference values from the plan:
- BPM 60, 1/1 = 22050
- BPM 60, 1/16t = 918
- BPM 120, 1/4 = 2756
- BPM 120, 1/8 = 1378
- BPM 140, 1/4 = 2362
- BPM 140, 1/16t = 393
- BPM 180, 1/16 = 459

### Task 2: Preset round-trip with tempo+subdivision across all 10 delay registers
**Commit:** 21ade9c

Created `tests/unit/preset/test_preset_tempo_full_roundtrip.c` with three tests:
- `test_full_roundtrip_all_registers`: Hall preset at 120 BPM, both sync toggles enabled, all 6 d-prefix registers bound to distinct subdivisions (1/4, 1/8, 1/16, 1/8., 1/4t, 1/2) and all 4 virtual comb registers bound (1/1, 1/2., 1/4., 1/8t). Save/load cycle preserves BPM, sync toggles, binding states, subdivisions, and register values for all d-prefix registers. Virtual combs verified for binding state and subdivision only (register values depend on comb geometry).
- `test_roundtrip_mixed_bindings`: dAPF1=GRID(1/4), dAPF2=FIXED, dLSAME=GRID(1/8), dRSAME=PROPORTIONAL(ref_bpm=140, raw=3000). All states survive save/load correctly.
- `test_roundtrip_different_bpm`: Proves loaded grid binding is live -- changing BPM from 120 to 140 on loaded state auto-resnaps dAPF1 from 2756 to 2362.

## Verification Results

All tempo-labeled tests (17 assertions across 3 test binaries): PASS
All preset-labeled tests (61 assertions across 9 test binaries): PASS
Zero regressions in existing test suites.

## Deviations from Plan

None -- plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

All created files exist on disk. Both task commits verified in git log.
