---
phase: 53-sweep-shapes
plan: 01
subsystem: sweep-state-machine
tags: [dsp, sweep, sawtooth, waveform-shape, vca-ramp]
dependency_graph:
  requires: [52-01]
  provides: [sweep-shape-param, saw-down-behavior, saw-up-behavior]
  affects: [tremolo, auto-pan, am, ring-mod]
tech_stack:
  added: []
  patterns: [shape-aware-retrigger, atomic-boundary-reset]
key_files:
  created: []
  modified:
    - include/spu94/spu94_sweep.h
    - src/spu94/spu94_sweep.c
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_sweep.c
    - tests/unit/voice/test_voice_tick.c
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
decisions:
  - "Sawtooth reset is atomic within one tick (boundary hit + level reset in same spu94_sweep_tick call)"
  - "Shape applied via direct struct write on parameter change (no full reconfigure) to keep oscillation phase continuous"
  - "Single lastSweepShape variable shared across all effects (mutual exclusion means only one active at a time)"
metrics:
  duration_seconds: 800
  completed: "2026-05-25T22:42:43Z"
  tasks_completed: 2
  tasks_total: 2
  tests_added: 7
  tests_total: 47
---

# Phase 53 Plan 01: Sweep Shapes Summary

Selectable waveform shapes (Triangle / Saw Down / Saw Up) for the VCA ramp sweep state machine, tripling timbral variety from 2 waveforms to 6 (3 shapes x 2 curves).

## One-liner

Three sawtooth shapes via shape-aware retrigger boundary logic in C core, wired through all four VCA ramp effects.

## Task Results

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Add shape field + sawtooth boundary logic + tests | 371b9ce, 2d5aa05 | spu94_sweep.h, spu94_sweep.c, spu94_voice.h, spu94_voice.c, test_sweep.c |
| 2 | Wire shape through host-layer effect activation | 170ec27 | PluginProcessor.h, PluginProcessor.cpp |

## Implementation Details

### C Core (Task 1)

- Added `SPU94_SWEEP_SHAPE_TRIANGLE/SAW_DOWN/SAW_UP` enum constants
- Added `uint8_t shape` field to `spu94_sweep_t` struct
- Extended `spu94_sweep_configure()` and `spu94_voice_mixer_set_sweep_l/r()` with shape parameter
- Shape validation: returns `SPU94_INVALID_ARG` if shape > 2
- Retrigger boundary logic uses switch on shape:
  - **TRIANGLE (0):** Existing auto-reverse behavior (direction flip at boundary)
  - **SAW_DOWN (1):** Reset level to 0x7FFF at zero boundary; direction stays decrease
  - **SAW_UP (2):** Reset level to 0 at max boundary; direction stays increase
- Key insight: boundary hit + reset happen atomically within one tick (level never dwells at boundary for sawtooth shapes)

### Host Layer (Task 2)

- Added `std::atomic<int> sweepShape{0}` and `getSweepShape()` getter
- Added `int lastSweepShape{0}` for audio-thread change detection
- All four effect activation blocks (tremolo, auto-pan, AM, ring mod) read `sweepShape` and pass to `spu94_voice_mixer_set_sweep_l/r`
- Shape changes detected in parameter-update sub-blocks; applied via direct struct field write (no full reconfigure needed -- oscillation continues seamlessly)
- Sidechain duck always passes shape=0 (one-shot mode, shape irrelevant)
- Shape clamped to 0-2 at host layer (T-53-01 threat mitigation)

## TDD Gate Compliance

1. RED commit: `371b9ce` (test) -- 5 behavior tests fail, 2 API tests pass
2. GREEN commit: `2d5aa05` (feat) -- all 47 tests pass
3. No REFACTOR needed -- implementation is clean

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Test assertions corrected for atomic boundary behavior**
- **Found during:** Task 1 GREEN phase
- **Issue:** Original test design expected level to dwell at boundary (level==0 observable between ticks). But in the SPU tick model, boundary hit and retrigger action happen atomically within the same tick -- level is never observable at the boundary for sawtooth shapes.
- **Fix:** Rewrote tests to detect the reset event by watching for level to jump back to the start value after having moved away from it. This correctly models the real SPU behavior.
- **Files modified:** tests/unit/voice/test_sweep.c
- **Commit:** 2d5aa05

## Verification Results

- `./build/tests/unit/voice/test_sweep`: 47 Tests 0 Failures 0 Ignored
- `cmake --build build`: Clean compilation, no errors
- `./build/tests/unit/voice/test_voice_tick`: 63 Tests 0 Failures 0 Ignored

## Self-Check: PASSED

All files verified present, all commits verified in git log.
