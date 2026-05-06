---
phase: 17-morph-knob-gui
plan: 01
subsystem: standalone-gui
tags: [morph-knob, interpolation, gui, atomic, rotary]
dependency_graph:
  requires: [16-01]
  provides: [MorphPanel-component, morphPosition-atomic]
  affects: [PluginProcessor, PluginEditor]
tech_stack:
  added: []
  patterns: [detent-snap-slider, waypoint-dot-markers, timer-driven-knob-sync]
key_files:
  created:
    - src/standalone/MorphPanel.h
    - src/standalone/MorphPanel.cpp
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
decisions:
  - "280px knob diameter chosen (middle of 250-300px range per Claude's Discretion)"
  - "Snap threshold 0.01 (1% of range) -- tight enough to feel precise, loose enough to find"
  - "Dot colors cycle teal/mauve/coral/blue through 9 positions"
  - "Numerical display uses one decimal place (e.g., 62.5) between detents"
metrics:
  duration: "2m 13s"
  completed: "2026-05-06T16:15:44Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 2
  files_modified: 2
---

# Phase 17 Plan 01: Morph Position Atomic + MorphPanel Component Summary

**morphPosition atomic on the processor plus MorphPanel component: 280px rotary knob with 9 PS1-colored waypoint dots, detent snap at each preset position, dynamic label switching between preset names and numerical position**

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Add morphPosition atomic to PluginProcessor and wire into processBlock | b614bf5 | PluginProcessor.h, PluginProcessor.cpp |
| 2 | Create MorphPanel component with rotary knob, waypoint dots, detent snap, and dynamic label | f454a54 | MorphPanel.h, MorphPanel.cpp |

## What Was Built

### Task 1: morphPosition Atomic + processBlock Integration

- Added `std::atomic<float> morphPosition{0.625f}` to PluginProcessor (default = Hall preset, waypoint 5/8)
- Exposed `getMorphPosition()` getter following existing atomic pattern
- processBlock reads the atomic every buffer and calls `spu94_interp_set_morph(spu, value)` -- this drives the Phase 16 interpolation engine in real time
- prepareToPlay sets the initial morph position after loading the Hall preset, so the interpolation engine state is consistent from the first audio callback

### Task 2: MorphPanel Component

- **MorphPanel.h**: Component class with nested `MorphSlider` (overrides `snapValue` for detent behavior), `updateKnobPosition()` for timer-driven sync from the editor
- **MorphPanel.cpp** (146 LOC):
  - PS1 color palette constants (psxDarkGray, psxLightGray, psxTeal, psxMauve, psxCoral, psxBlue)
  - 9 waypoint names array matching Phase 16 perceptual order
  - 280px centered rotary knob with 270-degree arc (1.2pi to 2.8pi), range 0.0-1.0, step 0.0001
  - `snapValue`: snaps to i/8.0 positions within 0.01 threshold (9 detents)
  - `updateLabelText`: shows preset name at detent positions, numerical 0.0-100.0 (one decimal) between
  - `paint`: draws 9 dot markers at equal angular spacing around the knob arc, cycling through teal/mauve/coral/blue
  - `resized`: centers knob with label 8px below
  - `onValueChange`: stores to processorRef.getMorphPosition() with relaxed memory order, guarded by isUpdatingFromTimer flag
  - `updateKnobPosition`: loads from processor atomic and syncs knob + label (called from editor timer)

## Deviations from Plan

None -- plan executed exactly as written.

## Architecture Notes

- MorphPanel is created but NOT yet added to CMakeLists.txt or wired into PluginEditor -- that is Plan 02's responsibility
- The morph call in processBlock is placed after registerBridge.pushPendingRegisterWrites and after the DAC coloration section, as specified
- The onValueChange -> atomic store -> processBlock -> spu94_interp_set_morph path is the complete GUI-to-audio thread bridge for morph position

## Verification Results

All acceptance criteria verified via grep:
- getMorphPosition count in .h: 1
- spu94_interp_set_morph count in .cpp: 2 (prepareToPlay + processBlock)
- morphPosition{0.625f} declared in .h
- MorphPanel.h contains class MorphPanel, MorphSlider, snapValue, updateKnobPosition, forward-declare
- MorphPanel.cpp contains all PS1 colors, waypoint names, fillEllipse, snap logic, 280px sizing
- MorphPanel NOT in CMakeLists.txt (correct -- Plan 02 adds it)

## Self-Check: PASSED

All files exist, all commits found (b614bf5, f454a54).
