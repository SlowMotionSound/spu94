---
phase: 17-morph-knob-gui
plan: 02
subsystem: standalone-gui
tags: [morph-knob, toggle, editor-wiring, build-integration]
dependency_graph:
  requires: [17-01]
  provides: [morph-panel-in-editor, macro-advanced-toggle]
  affects: [PluginEditor, CMakeLists, PluginProcessor]
tech_stack:
  added: []
  patterns: [visibility-swap-toggle, timer-driven-knob-sync, shared-zone-bounds, morph-active-gate]
key_files:
  created: []
  modified:
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
    - src/standalone/CMakeLists.txt
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
decisions:
  - "advancedToggle placed at w-100, 45 in toolbar row (right-aligned)"
  - "morphActive atomic gates morph engine vs register bridge in processBlock"
  - "write-on-change: morph engine only writes registers when position changes"
  - "requestShadowSync flag for audio-thread-safe shadow sync on mode switch"
  - "defaults: DAC on, ADPCM send full, dry send off, dry fader off, reverb full"
metrics:
  duration: "~25m (including checkpoint debugging)"
  completed: "2026-05-06"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 5
---

# Phase 17 Plan 02: Wire MorphPanel into Editor Summary

**MorphPanel wired as default Zone 2 view with Macro/Advanced toggle; morph-vs-register gate prevents write conflicts; write-on-change optimization prevents delay-line disruption**

## Status: COMPLETE (approved with known issues)

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Wire MorphPanel into PluginEditor with Macro/Advanced toggle, add to build | 3e3434b | PluginEditor.h/.cpp, CMakeLists.txt |
| 2 | Visual checkpoint: human-verify | e39dbcd (fixes) | PluginProcessor.h/.cpp, PluginEditor.cpp |

## What Was Built

### Task 1: MorphPanel Wiring + Build Integration

- **CMakeLists.txt**: Added `MorphPanel.cpp` to `target_sources` after `RegisterPanel.cpp`
- **PluginEditor.h**: Added `#include "MorphPanel.h"`, `MorphPanel morphPanel` member, `juce::TextButton advancedToggle{"Advanced"}` member
- **PluginEditor.cpp**:
  - `morphPanel(p)` added to constructor initializer list
  - `addAndMakeVisible(morphPanel)` in constructor body
  - `registerViewport.setVisible(false)` sets macro as default view (D-02)
  - `advancedToggle.onClick` handler swaps visibility, updates button text, gates morphActive, requests shadow sync on Advanced switch
  - `morphPanel.setBounds(10, registerTop, viewportW, viewportH)` in `resized()` -- same Zone 2 bounds
  - `advancedToggle.setBounds(w - 100, 45, 90, 25)` in `resized()` -- toolbar row, right-aligned
  - `morphPanel.updateKnobPosition()` called in `timerCallback()` when morphPanel is visible

### Task 2: Checkpoint Fixes

Three bugs found during visual verification:

1. **Morph engine vs register bridge fight** — both wrote all 30 reverb registers every processBlock. Added `morphActive` atomic gate: morph engine runs in Macro mode, register bridge runs in Advanced mode.

2. **Write-on-change optimization** — `spu94_interp_set_morph` was called every buffer even when position unchanged, continuously disrupting delay-line pending mechanism. Added `lastMorphPosition` tracker; only writes on change.

3. **Default settings wrong** — processor atomics and editor slider initial values corrected: DAC on, ADPCM send full, dry send off, dry fader off, reverb full.

## Known Issues (approved, out of Phase 17 scope)

1. **Transition artifacting** — digital clicks when d/m delay-line address registers change during knob movement. Fix: morph position slew filter (~20-50ms lowpass on audio thread).
2. **Unstable feedback spots** — certain interpolated register combinations between waypoints produce feedback. Fix: gain clamping or waypoint reordering after mapping specific positions.

## Deviations from Plan

- Additional files modified (PluginProcessor.h/.cpp) for morphActive gate, write-on-change, and default fixes
- Shadow sync uses audio-thread flag pattern instead of direct GUI-thread call (thread safety)

## Self-Check: PASSED

All files exist, commits 3e3434b and e39dbcd confirmed. Build succeeds. User approved visual checkpoint.
