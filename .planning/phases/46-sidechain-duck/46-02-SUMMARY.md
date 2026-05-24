---
phase: 46-sidechain-duck
plan: 02
subsystem: gui
tags: [sidechain, duck, gui, sampler-window, mutual-exclusion, combo-box, knobs]
dependency_graph:
  requires:
    - phase: 46-sidechain-duck
      plan: 01
      provides: "Duck atomics and getters (getDuckSource/getDuckRelease/getDuckDepth)"
    - phase: 45-auto-pan
      plan: 02
      provides: "Auto-pan GUI pattern (section label, toggle, knobs, mutual exclusion)"
  provides:
    - "Duck GUI section in sampler panel (Source dropdown, Release knob, Depth knob)"
    - "Three-way mutual exclusion: duck vs tremolo vs auto-pan (same sweep hardware)"
    - "Timer-based GUI sync from processor duck atomics"
  affects: [voice-dynamics-gui]
tech_stack:
  added: []
  patterns: ["ComboBox for voice source selection", "onChange lambda with threat-model clamping", "three-way mutual exclusion via enable/disable"]
key_files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h
decisions:
  - "Duck controls live in PluginEditor (same as tremolo/auto-pan) not separate SamplerPanel files -- project has no SamplerPanel.h/cpp"
  - "ComboBox selectedId scheme: 1=None, 2-25=Voice 0-23 (JUCE ComboBox requires IDs >= 1)"
  - "Duck source clamped to valid range before conversion (T-46-04 threat mitigation)"
  - "Panel height increased from 1030px to 1140px (110px for duck section)"
  - "Three-way rampArmButton guard: only re-enables when tremolo OFF and auto-pan OFF and duck source=None"
metrics:
  duration: "~4 minutes"
  completed: "2026-05-24T22:30:00Z"
  tasks_completed: 1
  tasks_total: 1
  files_modified: 3
---

# Phase 46 Plan 02: Sidechain Duck GUI Controls Summary

Duck source dropdown, release speed knob, and depth knob in sampler panel wired to per-voice duck atomics from Plan 01.

## What Was Built

### Duck GUI Section (PluginEditor.h / PluginEditor.cpp)
- **Section label**: "Sidechain Duck" with matching style (11pt bold, light gray)
- **Source ComboBox**: "None" + "Voice 0" through "Voice 23" (25 items, IDs 1-25)
- **Release knob**: Rotary, range 0.03-6.8s, default 0.4s, skew mid=1.0, suffix " s"
- **Depth knob**: Rotary, range 0-100%, default 100%, suffix "%"
- All controls `addAndMakeVisible` on sampler window content panel

### Wiring to Processor Atomics
- `duckSourceBox.onChange` -> `processorRef.getDuckSource(0).store(selectedId - 2)`
- `duckReleaseKnob.onValueChange` -> `processorRef.getDuckRelease(0).store(value)`
- `duckDepthKnob.onValueChange` -> `processorRef.getDuckDepth(0).store(value / 100.0)`

### Timer Callback Sync
- Reads `getDuckSource(0)`, `getDuckRelease(0)`, `getDuckDepth(0)` each tick
- Updates GUI controls via `dontSendNotification` to avoid feedback loops
- Epsilon guards on float comparisons (0.005 for release, 0.5 for depth %)

### Mutual Exclusion (Three-Way Sweep Ownership Guard)
- **Duck active -> disables**: tremolo enable toggle, auto-pan enable toggle, VCA ramp ARM
- **Tremolo active -> disables**: auto-pan enable toggle, duck source box, VCA ramp ARM
- **Auto-pan active -> disables**: tremolo enable toggle, duck source box, VCA ramp ARM
- **rampArmButton re-enable**: only when ALL three effects are inactive

### Layout (resized)
- Duck section positioned at `pany + 110` (below auto-pan)
- Source combo: 120x24 at left; Release knob: 80x70 center; Depth knob: 80x70 right
- Labels above each control

### Sampler Window Resize (SamplerWindow.h)
- Panel height: 1030px -> 1140px (+110px for duck section)
- `ensureMinimumSize()` threshold updated to match

### Threat Mitigations Applied
- T-46-04: ComboBox selectedId clamped to [1, 25] before voice index conversion

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Plan references non-existent SamplerPanel.h/cpp**
- **Found during:** Task 1 setup
- **Issue:** Plan specifies `src/plugin/sampler/SamplerPanel.h` and `SamplerPanel.cpp` but these files do not exist. All sampler GUI controls live in `PluginEditor.h/cpp`.
- **Fix:** Implemented in `PluginEditor.h`, `PluginEditor.cpp`, and `SamplerWindow.h` following the exact same pattern as Phase 44 (tremolo) and Phase 45 (auto-pan).
- **Files modified:** src/plugin/PluginEditor.h, src/plugin/PluginEditor.cpp, src/plugin/SamplerWindow.h
- **Commit:** 1d53276

## Commits

| Hash | Type | Description |
|------|------|-------------|
| 1d53276 | feat | Sidechain duck GUI controls -- source dropdown, release/depth knobs, mutual exclusion |

## Self-Check: PASSED
