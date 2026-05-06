---
phase: 17-morph-knob-gui
plan: 02
subsystem: standalone-gui
tags: [morph-knob, toggle, editor-wiring, build-integration]
dependency_graph:
  requires: [17-01]
  provides: [morph-panel-in-editor, macro-advanced-toggle]
  affects: [PluginEditor, CMakeLists]
tech_stack:
  added: []
  patterns: [visibility-swap-toggle, timer-driven-knob-sync, shared-zone-bounds]
key_files:
  created: []
  modified:
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
    - src/standalone/CMakeLists.txt
decisions:
  - "advancedToggle placed at w-100, 45 in toolbar row (right-aligned)"
  - "registerPanel.updateFromShadows() called on switch to Advanced to sync sliders"
metrics:
  duration: "1m 28s"
  completed: "PENDING - checkpoint:human-verify"
  tasks_completed: 1
  tasks_total: 2
  files_created: 0
  files_modified: 3
---

# Phase 17 Plan 02: Wire MorphPanel into Editor Summary

**MorphPanel wired as default Zone 2 view with Macro/Advanced toggle; MorphPanel.cpp added to build; timer-driven knob sync active when morph panel visible**

## Status: CHECKPOINT PENDING

Task 2 (checkpoint:human-verify) awaiting user verification of the visual GUI.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Wire MorphPanel into PluginEditor with Macro/Advanced toggle, add to build | 3e3434b | PluginEditor.h, PluginEditor.cpp, CMakeLists.txt |

## What Was Built

### Task 1: MorphPanel Wiring + Build Integration

- **CMakeLists.txt**: Added `MorphPanel.cpp` to `target_sources` after `RegisterPanel.cpp`
- **PluginEditor.h**: Added `#include "MorphPanel.h"`, `MorphPanel morphPanel` member, `juce::TextButton advancedToggle{"Advanced"}` member
- **PluginEditor.cpp**:
  - `morphPanel(p)` added to constructor initializer list
  - `addAndMakeVisible(morphPanel)` in constructor body
  - `registerViewport.setVisible(false)` sets macro as default view (D-02)
  - `advancedToggle.onClick` handler swaps visibility between morphPanel and registerViewport, updates button text between "Macro" and "Advanced", syncs register sliders on switch to Advanced
  - `morphPanel.setBounds(10, registerTop, viewportW, viewportH)` in `resized()` -- same Zone 2 bounds as registerViewport
  - `advancedToggle.setBounds(w - 100, 45, 90, 25)` in `resized()` -- toolbar row, right-aligned
  - `morphPanel.updateKnobPosition()` called in `timerCallback()` when morphPanel is visible

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

- Build: cmake configure + build succeeded with 0 errors
- All 13 acceptance criteria verified via grep (MorphPanel.cpp in CMakeLists, include/member in .h, initializer/addAndMakeVisible/setVisible/onClick/setBounds/timerCallback in .cpp)

## Self-Check: PASSED

All files exist, commit 3e3434b found.
