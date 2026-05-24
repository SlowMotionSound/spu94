---
phase: 50-internal-mod-bus
plan: 02
subsystem: sampler-gui
tags: [mod-bus, gui, knobs, noise-modulation]
dependency_graph:
  requires: [50-01 C core mod bus atomics]
  provides: [Mod Bus GUI section with three depth knobs]
  affects: [SamplerWindow height, PluginEditor layout]
tech_stack:
  added: []
  patterns: [rotary knob section with teal fill, onValueChange -> atomic store normalized]
key_files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h
decisions:
  - "No enable toggle for Mod Bus -- depth at 0% is functionally disabled, simpler UX"
  - "No mutual exclusion -- mod bus coexists freely with all sweep-based effects"
  - "Panel height 1470->1550 (80px for section label + three horizontal knobs)"
metrics:
  duration_seconds: 187
  completed: "2026-05-24T23:52:17Z"
  tasks_completed: 1
  tasks_total: 1
  files_modified: 3
---

# Phase 50 Plan 02: Mod Bus GUI Summary

Three teal-filled depth knobs (Pitch/Volume/Pan, 0-100%) in a dedicated "Mod Bus" section, wired to processor atomics for per-voice noise modulation control.

## Commits

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Add Mod Bus section GUI components and layout | 199e945 | Section label, 3 rotary knobs, layout, window resize |

## Implementation Details

### GUI Components (PluginEditor.h)

Seven new members after the Phase Mod section:
- `modBusSectionLabel` -- bold 11pt "Mod Bus" header
- `modBusPitchKnob` + `modBusPitchLabel` -- "Pitch" depth 0-100%
- `modBusVolKnob` + `modBusVolLabel` -- "Volume" depth 0-100%
- `modBusPanKnob` + `modBusPanLabel` -- "Pan" depth 0-100%

### Knob Wiring (PluginEditor.cpp)

Each knob's `onValueChange` divides by 100.0 and stores to the corresponding processor atomic:
- `modBusPitchKnob` -> `processorRef.getModBusPitchDepth()` (0.0-1.0)
- `modBusVolKnob` -> `processorRef.getModBusVolDepth()` (0.0-1.0)
- `modBusPanKnob` -> `processorRef.getModBusPanDepth()` (0.0-1.0)

All knobs default to 0 (no modulation). Double-click returns to 0. Teal fill (0xFF3CBBB1) matches existing effect sections.

### Layout

Positioned after Phase Mod section at `phasy + 110` in the sampler panel. Three knobs arranged horizontally (x=20, 110, 200). Section label above at y offset 0, knob labels at +20, knobs at +36.

### Window Resize (SamplerWindow.h)

Panel height increased from 1470 to 1550 to accommodate the new 80px section without cramping. Updated in `setSize`, `centreWithSize`, and `ensureMinimumSize`.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

1. `cmake --build` succeeds with no errors across all targets (Standalone, VST3, CLAP, LV2)
2. Three rotary knobs visible in Mod Bus section below Phase Mod in sampler panel
3. All knobs default to 0%, range 0-100%, teal fill accent
4. No mutual exclusion logic -- mod bus operates independently of sweep effects
5. Sampler window height accommodates new section without overflow

## Self-Check: PASSED

All files found, commit 199e945 verified, key patterns present in target files.
