---
phase: 18-i-o-surfaces
plan: 02
subsystem: standalone-gui
tags: [tempo-sync, juce, gui, atomic-bridge]
dependency_graph:
  requires: [Phase 16 tempo API, Phase 17 preset tempo format]
  provides: [JUCE tempo GUI controls, processBlock tempo state push, tempo preset round-trip]
  affects: [PluginProcessor, PluginEditor]
tech_stack:
  added: []
  patterns: [atomic tempo bridge, per-register subdivision dropdowns, timer-based binding state sync]
key_files:
  created: []
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
decisions:
  - "SyncMode enum declared as enum class in PluginProcessor.h (not C-style) for type safety"
  - "Per-register subdivision sentinel 0xFF for Global, SPU94_SUBDIVISION__COUNT for Free -- matches C core convention"
  - "Tempo zone placed as 80px light gray bar between toolbar and register panel"
  - "BPM field uses juce::Slider with IncDecButtons style for mouse-drag + type-in + increment buttons"
  - "Per-register dropdowns arranged as 2 rows of 5 below the mode/BPM/grid controls"
  - "acceptsMidi changed to true to prepare for Plan 03 MIDI clock (harmless when no MIDI is connected)"
  - "processBlock midiMessages parameter name uncommented to prepare for Plan 03 MIDI iteration"
metrics:
  duration: 12m
  completed: "2026-05-03T21:30:00Z"
---

# Phase 18 Plan 02: JUCE Tempo GUI Controls Summary

Tempo sync controls added to the JUCE standalone GUI with atomic parameter bridge and processBlock state push to the C core tempo API.

## One-liner

FREE/INT/EXT mode selector with BPM field, global subdivision dropdown, and 10 per-register subdivision dropdowns driving the Phase 16 C core tempo API through lock-free atomics.

## What Was Done

### Task 1: Add tempo atomics to PluginProcessor and processBlock tempo push (1dc3bd5)

**PluginProcessor.h:**
- Added `enum class SyncMode : uint8_t { FREE, INT, EXT }` before the class
- Added tempo atomic declarations: `tempoBpm`, `syncMode`, `globalSubdivision`, `perRegSub[10]`
- Added binding state shadow arrays: `bindingStateShadow[10]`, `bindingSubShadow[10]`
- Added audio-thread-only tracking: `lastPushedBpm`, `lastPushedMode`, `lastPushedSub[10]`, `lastPushedGlobalSub`
- Added public getters: `getTempoBpm()`, `getSyncMode()`, `getGlobalSubdivision()`, `getPerRegSub(reg)`, `getBindingStateShadow(reg)`, `getBindingSubShadow(reg)`
- Changed `acceptsMidi()` from `return false` to `return true`

**PluginProcessor.cpp:**
- Constructor initializes perRegSub to 0xFF (Global), bindingStateShadow to 0 (FIXED), bindingSubShadow to 15 (sentinel)
- processBlock tempo push block: handles FREE-to-INT/EXT mode transition (enable sync groups, apply all subdivisions, set tempo), steady-state BPM/subdivision change detection, and INT/EXT-to-FREE transition
- processBlock updates binding state shadows every block for GUI consumption
- File preset drain syncs tempo atomics from loaded SPU state (BPM, syncMode, perRegSub, binding shadows)
- Factory preset drain resets all tempo atomics to FREE mode defaults
- savePresetToString pushes current tempo state to SPU before serialization
- processBlock midiMessages parameter name uncommented for Plan 03

### Task 2: Add tempo GUI controls to PluginEditor (00d73b9)

**PluginEditor.h:**
- Added tempo widget declarations: `syncModeSelector`, `bpmField`, `globalSubSelector`, `perRegDropdowns[10]`, `perRegLabels[10]`
- Added sentinel ID constants: `kPerRegFreeId`, `kPerRegGlobalId`, `kPerRegSubBase`
- Added PresetSnapshot tempo fields: `tempoBpm`, `syncModeVal`, `globalSub`, `perRegSubVals[10]`
- Added `syncTempoControlsFromProcessor()` helper method

**PluginEditor.cpp:**
- Constructor builds all tempo controls with onChange callbacks that store to processor atomics
- Sync mode selector (FREE/INT/EXT) enables/disables BPM field and subdivision controls
- BPM field uses IncDecButtons style with range 1-999
- Global subdivision dropdown populated from `spu94_subdivision_to_string` (15 entries)
- 10 per-register dropdowns with Free/Global/separator/15 individual subdivisions, labeled from `spu94_tempo_reg_name`
- resized() places tempo zone at y=75, per-register dropdowns in 2 rows of 5
- paint() draws light gray tempo zone bar with "Tempo Sync" label
- timerCallback syncs EXT mode BPM display and detects C core write-interception FIXED transitions
- Factory preset load resets all tempo controls to FREE/disabled
- File preset load calls syncTempoControlsFromProcessor
- captureBaseline and checkModified include tempo state
- Window size increased from 900x1100 to 900x1180, resize limits adjusted

## Deviations from Plan

None -- plan executed exactly as written.

## Decisions Made

1. **SyncMode as enum class** -- declared in PluginProcessor.h as `enum class SyncMode : uint8_t` for C++ type safety (plan left this to discretion)
2. **acceptsMidi and midiMessages** -- both changed now (plan only specified acceptsMidi); uncommenting the midiMessages parameter name prevents compiler warnings when Plan 03 adds MIDI iteration

## Commits

| Task | Hash | Description |
|------|------|-------------|
| 1 | 1dc3bd5 | feat(18-02): add tempo atomics to PluginProcessor and processBlock tempo push |
| 2 | 00d73b9 | feat(18-02): add tempo GUI controls to PluginEditor |

## Checkpoint: Task 3 (human-verify)

Task 3 is a blocking human-verify checkpoint. The build succeeds and all code changes are committed, but visual and functional verification requires launching the GUI and exercising the controls interactively.

## Known Stubs

None -- all tempo controls are fully wired to processor atomics and the C core tempo API.

## Self-Check: PASSED

- All 4 modified files exist on disk
- Both task commits (1dc3bd5, 00d73b9) found in git log
- Build completes with zero errors (warnings only: sign-conversion on array index)
