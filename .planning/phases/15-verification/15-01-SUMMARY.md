---
phase: 15-verification
plan: 01
subsystem: preset-verification
tags: [test, integration, golden-roundtrip, preset, verification]
dependency_graph:
  requires: [13-01, 13-02]
  provides: [PRE-10]
  affects: []
tech_stack:
  added: []
  patterns: [golden-roundtrip-pattern, dual-engine-comparison]
key_files:
  created:
    - tests/unit/preset/test_preset_golden_roundtrip.c
  modified:
    - tests/unit/preset/CMakeLists.txt
decisions: []
metrics:
  duration: "4m 36s"
  completed: "2026-05-02T18:01:47Z"
  tasks: 2
  files_changed: 2
---

# Phase 15 Plan 01: Preset Golden Round-Trip Verification Summary

Integration-level golden round-trip test proving bit-identical audio output after preset save/load through spu94_process -- two configurations (factory Hall + custom Delay with non-default mixer/DAC) exercise all serialized field types.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Create integration-level golden round-trip test | 60d9f8e | tests/unit/preset/test_preset_golden_roundtrip.c |
| 2 | Register ctest target and verify tests pass | 361116d | tests/unit/preset/CMakeLists.txt |

## Implementation Details

### Test Architecture

Two independent engine state sets (A and B), each with 16KB state buffer and 256KB work buffer. Deterministic LCG noise generator (seed 0x00C0FFEE, matching test_preset_nonzero_tail.c) ensures identical input sequences across both engines.

`drive_and_collect` helper: reseeds LCG, feeds 2048 samples through spu94_process one at a time, then flushes 1024 samples via spu94_flush. Total 3072 output samples per channel compared bit-for-bit.

### Test 1: Factory Hall Round-Trip

- Configure engine A: Hall preset + vLOUT/vROUT=0x7FFF + mixer faders at unity
- Drive noise, save state, load into fresh engine B, drive same noise
- Sanity check confirms non-zero output (catches silent-output regression)
- Sample-by-sample comparison: PASS (bit-identical)

### Test 2: Custom State Round-Trip

- Configure engine A: Delay preset + non-default mixer faders (input_gain=0x4000, dry_fader=0x2000, patina_fader=0x1000, dry_send=0x3000, patina_send=0x0800, reverb_fader=0x6000, latency_comp=0) + flipped DAC toggles (dac_enabled=1, dac_fir_enabled=0, dac_noise_enabled=1, dac_true_oversample=0)
- Same drive/save/load/compare cycle
- Sanity check confirms non-zero output
- Sample-by-sample comparison: PASS (bit-identical)

### Regression Check

All 7 preset-labeled tests pass: preset_table_integrity, test_preset_load_all, test_preset_nonzero_tail, test_preset_roundtrip, test_preset_parse, test_preset_golden_roundtrip, verify_preset_sources.

## Verification Results

| Check | Result |
|-------|--------|
| Build with zero warnings under -Werror | PASS |
| 2 Unity tests in golden_roundtrip binary | PASS |
| ctest -R preset_golden_roundtrip | 1/1 PASS |
| ctest -L preset (regression) | 7/7 PASS |
| spu94_process in test file | 3 occurrences |

## Deviations from Plan

None -- plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED
