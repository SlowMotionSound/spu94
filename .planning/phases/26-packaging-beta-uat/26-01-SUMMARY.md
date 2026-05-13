---
phase: 26-packaging-beta-uat
plan: 01
subsystem: plugin-packaging
tags: [plugin-window, beta-readme, cache-reset, unsigned-binary, PLUG-46, PLUG-47, PLUG-48]
dependency_graph:
  requires: []
  provides: [fixed-size-plugin-window, beta-tester-documentation]
  affects: [src/plugin/PluginEditor.cpp, BETA-README.md]
tech_stack:
  added: []
  patterns: [setResizable-fixed-window]
key_files:
  created: [BETA-README.md]
  modified: [src/plugin/PluginEditor.cpp]
decisions:
  - "Plugin window fixed at 900x1100 via setResizable(false, false); setResizeLimits removed as meaningless when resize is disabled"
metrics:
  duration: "4m 59s"
  completed: "2026-05-13T18:57:19Z"
---

# Phase 26 Plan 01: Fixed-Size Window & Beta README Summary

Fixed plugin window to 900x1100 and wrote Beta README covering per-DAW cache reset for all 6 UAT matrix DAWs plus macOS/Windows unsigned-binary workarounds.

## What Was Done

### Task 1: Lock plugin window to fixed size (PLUG-48)

Replaced `setResizeLimits(900, 800, 1600, 1400)` with `setResizable(false, false)` in the `SPU94AudioProcessorEditor` constructor. The plugin window is now non-resizable at 900x1100 in every host format, preventing layout bugs from user drag-resize. The existing `setSize(900, 1100)` call is unchanged.

**Commit:** `75328b9`
**Files modified:** `src/plugin/PluginEditor.cpp`

### Task 2: Write Beta README (PLUG-46, PLUG-47)

Created `BETA-README.md` at the repository root with polished, confident tone. Covers:

- Supported formats table (VST3/AU/LV2/CLAP across 3 OSes)
- Per-OS installation paths (macOS system paths, Windows Program Files, Linux user paths)
- macOS Gatekeeper bypass: right-click Open, System Settings Open Anyway, xattr -cr commands for drag-install bundles
- Windows SmartScreen bypass: More info then Run anyway
- Per-DAW cache reset instructions for all 6 DAWs on the test matrix (Reaper, Ardour, Logic Pro, Ableton Live, FL Studio, Bitwig)
- Pro Tools note (VST3 via Blue Cat PatchWork)
- Known issues (standalone volume bug, LV2/Ardour GUI state)
- Feedback template

**Commit:** `b9f53b8`
**Files created:** `BETA-README.md` (155 lines)

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

### Task 1
- `setResizable(false, false)` present: 1 occurrence
- `setResizeLimits` absent: confirmed (0 occurrences)
- `setSize(900, 1100)` retained: confirmed
- Build succeeds: `cmake --build build --config Release --target spu94_plugin` completed with zero errors

### Task 2
- BETA-README.md exists at repo root: confirmed
- Contains `xattr -cr`: 3 occurrences
- Contains `Run anyway`: 1 occurrence
- References all 6 DAWs: confirmed (6 non-heading references)
- Per-DAW cache reset instructions: all 6 covered with specific paths/menus
- macOS install paths present: all 3 paths confirmed
- Windows install paths present: both paths confirmed
- Linux install paths present: all 3 paths confirmed
- Does not contain "signing" or "notarize": confirmed
- File length: 155 lines (exceeds 80-line minimum)

## Commits

| Task | Commit  | Type | Description                                |
|------|---------|------|--------------------------------------------|
| 1    | 75328b9 | feat | Lock plugin window to fixed 900x1100 size  |
| 2    | b9f53b8 | docs | Beta README with cache-reset and unsigned-binary instructions |

## Self-Check: PASSED

All files found, all commits verified.
