---
phase: 52-ring-mod
plan: 01
subsystem: voice-effects
tags: [ring-mod, bipolar-sweep, vca-ramp, phase-inversion]
dependency_graph:
  requires: [43-retrigger-engine, 48-am-synthesis, 49-phase-modulator]
  provides: [bipolar-sweep-mode, ring-mod-host-layer]
  affects: [spu94_sweep, spu94_voice, PluginProcessor, PluginEditor]
tech_stack:
  added: []
  patterns: [bipolar-retrigger, zero-crossing-phase-flip]
key_files:
  created: []
  modified:
    - include/spu94/spu94_sweep.h
    - src/spu94/spu94_sweep.c
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_sweep.c
    - tests/unit/voice/test_voice_tick.c
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/PluginEditor.h
decisions:
  - Ring mod uses same depth formula as tremolo/AM -- bipolar sweep naturally produces negative vol
  - GUI rate knob range updated to audio-rate (21.5-9647 Hz) matching kAmHzTable
metrics:
  duration_seconds: 698
  completed: "2026-05-25T21:21:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 10
---

# Phase 52 Plan 01: Ring Mod Bipolar Sweep Summary

Bipolar sweep mode in C core with native zero-crossing phase flip, wired as ring mod host-layer effect replacing Phase 49's phase mod workaround.

## What Was Done

### Task 1: Bipolar sweep in C core (TDD)

Added `uint8_t bipolar` field to `spu94_sweep_t` struct and modified the retrigger logic:
- **At zero boundary (level == 0):** bipolar=1 flips BOTH direction AND phase, making the sweep cross into the opposite quadrant (+0x7FFF -> 0 -> -0x7FFF and back)
- **At non-zero boundaries (+/-0x7FFF):** only direction flips (standard reversal)
- **bipolar=0:** identical to pre-change behavior (unipolar, single quadrant)

Updated `spu94_sweep_configure` and `spu94_voice_mixer_set_sweep_l/r` signatures to accept the bipolar parameter. All existing callers pass `bipolar=0` for regression safety.

5 new tests: crosses_zero, full_cycle, bipolar_off_is_unipolar, configure_api, mixer_api.

### Task 2: Ring mod host-layer activation

Replaced Phase 49's phase mod block entirely:
- **Old approach:** Negative-only sweep (phase=1, 0 to -0x7FFF) with host-layer formula `vol = 0x7FFF + (sweep_level * 2 * depth)` to remap into bipolar volume
- **New approach:** Native bipolar sweep (bipolar=1, +0x7FFF through 0 to -0x7FFF) with the standard tremolo/AM depth formula `vol = 0x7FFF - (0x7FFF - sweep_lvl) * depth`. When sweep_lvl goes negative, the formula naturally produces negative volume.

Fewer moving parts, same ring mod result, no host-layer remapping.

## Key Technical Details

- Bipolar retrigger adds ~5 lines to spu94_sweep_tick's retrigger block
- Ring mod activation follows the exact AM pattern with bipolar=1 added
- Phase mod atomics (phaseModEnabled, phaseModSpeedHz, phaseModDepth) fully removed
- Ring mod atomics added (ringModEnabled, ringModRateHz, ringModDepth, ringModCurve)
- GUI editor renamed Phase Mod -> Ring Mod, rate range updated to audio-rate

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Updated PluginEditor.cpp/h for compilation**
- **Found during:** Task 2
- **Issue:** Plan only specified PluginProcessor.cpp changes, but PluginEditor references the phase mod accessors (getPhaseModEnabled, etc.) which no longer exist
- **Fix:** Renamed all phaseModXxx GUI members to ringModXxx, updated accessor calls
- **Files modified:** src/plugin/PluginEditor.cpp, src/plugin/PluginEditor.h

## Verification Results

- Build: 0 errors, 0 warnings (full project including JUCE plugin)
- Tests: 40 Tests, 0 Failures, 0 Ignored
- Regression: All 35 pre-existing sweep tests pass unchanged
- RT safety: No malloc/free/printf/pthread in spu94_sweep.c
- Plugin builds: Standalone, VST3, CLAP, LV2 all compile clean

## TDD Gate Compliance

- RED gate: test(52-01) commit 3ba6e4a -- 5 failing tests (compile error, struct/signature mismatch)
- GREEN gate: feat(52-01) commit 38c9eaa -- all 40 tests pass
- No refactor needed (implementation is minimal and clean)

## Self-Check: PASSED
