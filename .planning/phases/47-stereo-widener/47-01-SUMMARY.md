---
phase: 47-stereo-widener
plan: 01
subsystem: voice-dynamics
tags: [stereo-width, volume-offset, mono-safety]
dependency_graph:
  requires: [46-01]
  provides: [stereo-width-atomic, width-offset-logic]
  affects: [processBlock-standalone-path, voice-0-vol-lr]
tech_stack:
  added: []
  patterns: [static-offset-after-sweep, int32-intermediate-clamp]
key_files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c
decisions:
  - kWidthMaxOffset = 0x2000 chosen for conservative mono safety (worst case -2.5 dB)
  - Width applied AFTER all sweep effects (tremolo/auto-pan/duck) and BEFORE output mixing
  - No mutual exclusion needed with other effects (additive composition)
metrics:
  duration: 179s
  completed: 2026-05-24T22:41:46Z
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 47 Plan 01: Stereo Widener DSP Summary

**Static L/R volume offset on voice 0 with 0x2000 cap for < 3 dB mono loss**

## What Was Built

1. **Stereo width atomic** (`stereoWidth`, 0.0-1.0) with `getStereoWidth()` public getter following existing tremolo/auto-pan pattern in PluginProcessor.h.

2. **Width offset logic in processBlock** applied after all sweep-based effects (tremolo depth scaling, auto-pan depth scaling, duck state machine) and before the output sample writing loop. The offset pushes vol_l up and vol_r down by `width * 0x2000`, with int32 intermediate arithmetic and clamping to the PS1 register range (-0x4000..+0x3FFF).

3. **Four unit tests** proving the offset formula:
   - Max width worst-case mono loss is -2.5 dB (within 3 dB cap)
   - Zero width produces no change
   - Half width within 3 dB cap
   - Low-volume case preserves mono sum exactly (no clipping = perfect mono)

## Commits

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Stereo width atomic and offset logic | beaa77b | PluginProcessor.h, PluginProcessor.cpp |
| 2 | Unit tests proving mono-safety cap | 19ee96f | tests/unit/voice/test_sweep.c |

## Deviations from Plan

None - plan executed exactly as written.

## Threat Mitigations Applied

| Threat ID | Mitigation |
|-----------|-----------|
| T-47-01 | Width clamped to 0.0-1.0 at point of use in processBlock (line: `if (width > 1.0f) width = 1.0f;`) |
| T-47-02 | int32 intermediate calculation prevents overflow before clamp to -0x4000..0x3FFF |

## Verification Results

- Build: `spu94_plugin_Standalone` compiles clean (warnings are pre-existing, unrelated to this phase)
- Tests: All 33 tests pass (29 existing + 4 new stereo widener tests)
- Logic: width=0 no-op confirmed, width=1.0 diverges by 0x2000, mono loss -2.5 dB < 3 dB cap

## Self-Check: PASSED
