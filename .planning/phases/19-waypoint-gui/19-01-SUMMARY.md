---
phase: 19-waypoint-gui
plan: 01
subsystem: juce-gui
tags: [morph, knob, ticks, gui]
dependency_graph:
  requires: [spu94_interp_user_slot_is_filled, MorphPanel]
  provides: [SPU94AudioProcessor::isUserSlotFilled]
  affects: [juce-plugin/Source/MorphPanel.cpp, juce-plugin/Source/PluginProcessor.cpp]
tech_stack:
  added: []
  patterns: [17-detent-snap, filled-vs-empty-tint, sony-anchor-primacy]
key_files:
  modified:
    - juce-plugin/Source/MorphPanel.cpp
    - juce-plugin/Source/MorphPanel.h
    - juce-plugin/Source/PluginProcessor.cpp
    - juce-plugin/Source/PluginProcessor.h
decisions:
  - "User-slot ticks INSIDE the Sony dot ring — Sony anchors stay the outer visual landmark"
  - "PS1 blue (filled) vs dim grey (empty) — high contrast, fits the PS1 face-button palette"
  - "Visual-only at this milestone — no click target, no hover state. Interaction added in 19-02"
commits:
  - hash: ddfd39d
    title: "feat: render user-slot ticks + 17-position snap (milestone 2/4)"
metrics:
  completed: "2026-05-09"
  tasks: 4
---

# Phase 19 Plan 01: Waypoint GUI — Ticks + 17-Position Snap Summary

Surface the 8 user-slot midpoints as visible detents on the morph knob. Ticks render inside the Sony dot ring; filled = PS1 blue, empty = dim grey.

## What Shipped

- Knob snaps to all 17 detents (9 Sony + 8 user)
- User-slot ticks render between the knob's outer edge and the Sony dot ring
- Filled/empty tint via `SPU94AudioProcessor::isUserSlotFilled(idx)` → `spu94_interp_user_slot_is_filled`
- Knob label table extended: midpoint detents read "User 1" .. "User 8"

## Tasks Completed

| Task | Name | Commit |
|------|------|--------|
| 1 | 17-entry detent table + snap math | ddfd39d |
| 2 | Tick rendering (radial line + colour by fill state) | ddfd39d |
| 3 | Label table extension | ddfd39d |
| 4 | Processor accessor | ddfd39d |

## Files Touched

- `juce-plugin/Source/MorphPanel.{h,cpp}` — detent table, tick render, label table
- `juce-plugin/Source/PluginProcessor.{h,cpp}` — `isUserSlotFilled` accessor

## Carried Forward to 19-02

- Click-to-edit affordance — ticks become interactive (EDIT button)
- SAVE/REVERT flow — commits the slot or clears it
- Per-tick action buttons (EXPORT, LOAD)
- Engine state mirroring overhaul (sliders always reflect engine state)
