---
phase: 03-io-layer
plan: 03
subsystem: python-adpcm-bindings
tags: [python, ctypes, adpcm, vag, bindings]
dependency_graph:
  requires: [Phase 1 ADPCM codec, Phase 2 pipeline integration, Plan 01 VAG module]
  provides: [Python ctypes bindings for ADPCM + VAG functions]
  affects: [python/spu94/_binding.py, tests/python/binding/]
tech_stack:
  added: []
  patterns: [ctypes Structure for C struct mirroring, POINTER-based array passing]
key_files:
  created:
    - tests/python/binding/test_binding_adpcm.py
  modified:
    - python/spu94/_binding.py
    - tests/python/binding/CMakeLists.txt
decisions:
  - "D-09 raw ctypes wrappers for ADPCM functions (no high-level Python API)"
  - "D-10 VAG header read/write exposed via ctypes matching C API"
metrics:
  duration_minutes: 3
  completed: "2026-04-27T18:35:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 1
  files_modified: 2
---

# Phase 3 Plan 03: Python ADPCM + VAG Bindings Summary

ctypes prototypes for 7 ADPCM/VAG C functions added to _binding.py with struct definitions for spu94_adpcm_state (4 bytes) and spu94_vag_header; 12 pytest tests verify decode, encode roundtrip, toggle, and VAG header operations.

## Task Results

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Python ctypes bindings for ADPCM + VAG | 7a3cd64 | python/spu94/_binding.py |
| 2 | Python binding tests for ADPCM + VAG | 885a915 | tests/python/binding/test_binding_adpcm.py, tests/python/binding/CMakeLists.txt |

## What Was Built

### Task 1: ctypes Bindings
- `_Spu94AdpcmState` ctypes Structure: two int16 fields (old, older), validated at 4 bytes
- `_Spu94VagHeader` ctypes Structure: version, data_size, sample_rate, name[16]
- 4 ADPCM function prototypes: `spu94_adpcm_decode_block` (returns flag byte), `spu94_adpcm_encode_block`, `spu94_set_adpcm_enabled`, `spu94_get_adpcm_enabled`
- 1 latency function: `spu94_get_total_latency_samples` (58 without ADPCM, 86 with)
- 2 VAG function prototypes: `spu94_vag_read_header` (returns 0/-1), `spu94_vag_write_header`
- 3 constants: `SPU94_ADPCM_BLOCK_SAMPLES=28`, `SPU94_ADPCM_BLOCK_BYTES=16`, `SPU94_VAG_HEADER_BYTES=48`

### Task 2: Binding Tests
- `TestAdpcmStateStruct`: sizeof validation (T-03-10 mitigation), zero-init
- `TestAdpcmDecodeBlock`: all-zero block produces silence, flag byte returned correctly
- `TestAdpcmEncodeBlock`: silence roundtrip, non-zero ramp signal deterministic roundtrip
- `TestAdpcmToggle`: default off, enable/disable cycle, latency 58->86 when enabled
- `TestVagHeader`: write-read roundtrip (v2, data_size, sample_rate, name), bad magic rejection, NULL name safety

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

- `python3 -c "from spu94._binding import _Spu94AdpcmState; import ctypes; assert ctypes.sizeof(_Spu94AdpcmState) == 4"`: PASSED
- `pytest tests/python/binding/test_binding_adpcm.py -x -v`: 12/12 PASSED
- `pytest tests/python/binding/ -x`: 69/69 PASSED (all binding tests including pre-existing)
- `python3 -c "from spu94._binding import _lib; print('OK')"`: PASSED

## Known Stubs

None -- all functions are wired to the live libspu94.so and tested end-to-end.

## Self-Check: PASSED

All created files verified on disk. Both task commits (7a3cd64, 885a915) verified in git log.
