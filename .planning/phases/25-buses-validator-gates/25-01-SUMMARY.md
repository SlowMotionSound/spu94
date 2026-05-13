---
phase: 25-buses-validator-gates
plan: 01
subsystem: plugin-processor
tags: [bus-layout, mono, CLAP, unit-test, processBlock]
dependency_graph:
  requires: [phase-24-state-automation-surface]
  provides: [isBusesLayoutSupported, mono-processBlock-path, CLAP-mono-tag, bus-layout-tests]
  affects: [src/plugin/PluginProcessor.h, src/plugin/PluginProcessor.cpp, src/plugin/CMakeLists.txt, tests/plugin/CMakeLists.txt]
tech_stack:
  added: []
  patterns: [stack-scratch-buffer, bus-layout-whitelist, mono-summing]
key_files:
  created:
    - tests/plugin/test_bus_layout.cpp
    - tests/plugin/test_mono_sum.cpp
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/CMakeLists.txt
    - tests/plugin/CMakeLists.txt
decisions: []
metrics:
  duration: 27m 9s
  completed: 2026-05-13
  tasks: 2
  files: 6
---

# Phase 25 Plan 01: Bus Layout + Mono ProcessBlock Summary

isBusesLayoutSupported override accepting mono-mono/mono-stereo/stereo-stereo with stack-scratch mono summing via (L+R)*0.5f and CLAP mono feature tag

## Commits

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Declare isBusesLayoutSupported + mono processBlock path + CLAP mono tag | 0632d28 | PluginProcessor.h, PluginProcessor.cpp, CMakeLists.txt |
| 2 | Unit tests for bus layout whitelist and mono summing accuracy | 7e916c5 | test_bus_layout.cpp, test_mono_sum.cpp, tests/plugin/CMakeLists.txt |

## What Was Done

### Task 1: isBusesLayoutSupported + mono processBlock path + CLAP mono tag

- Added `bool isBusesLayoutSupported(const BusesLayout&) const override` declaration to PluginProcessor.h
- Implemented the override in PluginProcessor.cpp as a strict whitelist: mono-mono, mono-stereo, stereo-stereo accepted; all else rejected (disabled, surround, Atmos, sidechain, multi-bus)
- BusesProperties constructor unchanged (stereo/stereo default stays -- override handles negotiation)
- Added `float monoRScratch[kMaxBlock]` stack scratch in the plugin processBlock path to capture R channel output from SrcChain::processOut when the host negotiated mono output
- Changed `hostOutPtrs[1]` from nullptr to monoRScratch when `monoOutput` is true, so R data is captured instead of lost
- Added mono summing loop: `out[i] = (out[i] + monoRScratch[i]) * 0.5f` after processOut + under-produce padding
- Guarded side-channel limiter with `if (!monoOutput)` -- for mono, side==0 by definition after L+R summing, so the limiter is an identity transform; skipping saves CPU
- Updated CLAP_FEATURES from `audio-effect stereo reverb` to `audio-effect mono stereo reverb`

### Task 2: Unit tests for bus layout whitelist and mono summing accuracy

- Created `test_bus_layout.cpp` with 6 test functions: accepts stereo-stereo, accepts mono-stereo, accepts mono-mono, rejects stereo-mono, rejects disabled (3 sub-cases), rejects 5.1 surround
- Created `test_mono_sum.cpp` with 3 test functions: mono processBlock no crash/NaN/Inf (10 blocks), mono output nonzero after settling (20 blocks), mono-stereo layout processBlock no crash
- Both tests use the `test_state_roundtrip` full-processor CMake target pattern (all plugin sources linked)
- Registered as CTest targets: `add_test(NAME bus_layout ...)` and `add_test(NAME mono_sum ...)`

## Verification Results

- Plugin target builds clean (no warnings)
- `ctest -R bus_layout` passes (6/6 tests)
- `ctest -R mono_sum` passes (3/3 tests)
- CLAP_FEATURES grep confirms `mono stereo reverb`

## Deviations from Plan

None - plan executed exactly as written.

## Known Stubs

None.

## Self-Check: PASSED

- All 6 files exist on disk
- Both commits (0632d28, 7e916c5) present in git log
- PluginProcessor.h contains isBusesLayoutSupported declaration
- PluginProcessor.cpp contains isBusesLayoutSupported implementation
- CMakeLists.txt contains "mono stereo reverb"
- PluginProcessor.cpp contains monoRScratch (5 references: declaration, assignment, padding, summing loop)
