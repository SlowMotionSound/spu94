---
phase: 03-io-layer
plan: 02
subsystem: standalone-gui
tags: [juce, adpcm, toggle, gui]
dependency_graph:
  requires: [spu94_set_adpcm_enabled, spu94_get_adpcm_enabled]
  provides: [adpcm-toggle-gui]
  affects: [PluginEditor, PluginProcessor]
tech_stack:
  added: []
  patterns: [atomic-bool-toggle, juce-togglebutton]
key_files:
  created: []
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
decisions:
  - "D-05: ADPCM toggle placed in toolbar between preset selector and Input knob"
  - "D-06: Amber tick colour (0xFFD4A017) on active state, click-free via atomic"
metrics:
  duration: 104s
  completed: "2026-04-27T17:09:00Z"
  status: checkpoint-blocked
---

# Phase 3 Plan 02: JUCE ADPCM Toggle Summary

ADPCM toggle button wired into JUCE standalone toolbar with atomic<bool> handoff to audio thread calling spu94_set_adpcm_enabled per processBlock.

## Tasks

| # | Task | Status | Commit | Key Change |
|---|------|--------|--------|------------|
| 1 | ADPCM toggle in JUCE standalone | Done | 9be132a | Toggle button + atomic + processBlock wiring |
| 2 | Visual + Functional Verification | BLOCKED | -- | Checkpoint: human-verify required |

## Completed: 1/2

## Deviations from Plan

None - plan executed exactly as written.

## Key Changes

1. **PluginProcessor.h**: Added `std::atomic<bool> adpcmEnabled{false}` and `getAdpcmEnabled()` accessor
2. **PluginProcessor.cpp**: Added `spu94_set_adpcm_enabled(spu, ...)` call reading the atomic in processBlock, placed after register bridge push and before buffer fill
3. **PluginEditor.h**: Added `juce::ToggleButton adpcmToggle{"ADPCM"}` member
4. **PluginEditor.cpp**: Constructor sets up toggle with amber tick colour (0xFFD4A017), onClick stores to atomic. Layout shifts Input and Wet/Dry knobs right by 70px. Window widened from 800x750 to 850x750.

## Checkpoint: Human Verification Required

Task 2 requires visual and functional verification of the ADPCM toggle. See checkpoint details in the executor's return message.
