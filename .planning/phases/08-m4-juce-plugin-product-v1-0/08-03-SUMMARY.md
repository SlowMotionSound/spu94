---
phase: "08"
plan: "03"
subsystem: standalone-gui
tags: [gui, parameter-bridge, register-sliders, presets, lock-free, juce]
dependency_graph:
  requires: ["08-02"]
  provides: ["register-slider-panel", "preset-dropdown", "parameter-bridge"]
  affects: ["src/standalone/"]
tech_stack:
  added: ["std::atomic<int16_t> lock-free bridge"]
  patterns: ["SPSC command queue", "atomic shadow registers", "Timer-based GUI sync"]
key_files:
  created:
    - src/standalone/ParameterBridge.h
    - src/standalone/ParameterBridge.cpp
    - src/standalone/RegisterPanel.h
    - src/standalone/RegisterPanel.cpp
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
    - src/standalone/CMakeLists.txt
decisions:
  - "kSliderRegisters constexpr array is the single source of truth for which 18 registers get sliders"
  - "RegisterBridge uses std::atomic<int16_t> shadows with acquire/release ordering for lock-free GUI-to-audio handoff"
  - "PresetCommandQueue uses atomic bool flag + atomic int for SPSC preset switch without mutex"
  - "30Hz Timer polls appliedCount to detect preset switches and sync slider positions"
metrics:
  duration: "~54m"
  completed: "2026-04-25T23:19:26Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 4
  files_modified: 5
requirements:
  - STANDALONE-04
  - STANDALONE-05
  - STANDALONE-07
---

# Phase 08 Plan 03: Register Sliders + Preset Dropdown + Parameter Bridge Summary

Lock-free parameter bridge (atomic int16 shadows + SPSC preset queue) wiring 18 raw register sliders and a 10-preset dropdown between GUI and audio threads.

## What Was Done

### Task 1: ParameterBridge (lock-free register shadows + SPSC preset queue)
**Commit:** `06cd6ba`

Created `ParameterBridge.h` with:
- `constexpr std::array<spu94_reg_t, 18> kSliderRegisters` as the single source of truth for exposed registers (12 free-class v-prefix + 6 sample-quantized d-prefix)
- `static_assert(kSliderRegisters.size() == 18)` compile-time guard
- `RegisterBridge` class with `std::atomic<int16_t>` shadows and `lastApplied` audio-thread-only tracking
- `PresetCommandQueue` class with atomic request/drain handoff

Created `ParameterBridge.cpp` implementing:
- `pushPendingRegisterWrites` using `memory_order_acquire` reads, dispatching to `spu94_set_reg_i16` or `spu94_set_reg_u16` based on `spu94_reg_type`
- `syncShadowsFromSPU` using `memory_order_release` stores after reading back from `spu94_get_reg_i16`/`spu94_get_reg_u16`
- `PresetCommandQueue::drain` calling `spu94_load_preset` with `fetch_add` on appliedCount

Added `ParameterBridge.cpp` to CMakeLists.txt target_sources.

### Task 2: RegisterPanel, preset ComboBox, and editor wiring
**Commit:** `45d5a24`

Created `RegisterPanel.h/.cpp`:
- 18 sliders built dynamically by iterating `kSliderRegisters`
- Labels from `spu94_reg_name(reg)` -- raw register names per D-01
- Signed registers (v-prefix, I16): range -32768 to 32767, step 1
- Unsigned registers (d-prefix, U16): range 0 to 65535, step 1
- `LinearHorizontal` style with `TextBoxRight` showing numeric value
- Grouped by class with bold headers: Master I/O (4), IIR + Wall (2), Comb (4), All-Pass (2), Delay Offsets (6)
- `updateFromShadows()` for post-preset-switch slider sync

Updated `PluginProcessor.h/.cpp`:
- Added `RegisterBridge registerBridge` and `PresetCommandQueue presetQueue` members
- `prepareToPlay`: calls `registerBridge.syncShadowsFromSPU(spu)` after initial Hall preset load
- `processBlock`: drains preset queue (with bridge re-sync on switch), then pushes pending register writes, before existing spu94_process call

Updated `PluginEditor.h/.cpp`:
- Added `juce::Timer` inheritance for preset-switch detection
- Preset ComboBox populated from `spu94_presets[].name` for all 10 PS1 factory presets
- ComboBox onChange calls `requestPreset` through the SPSC queue
- `timerCallback` at 30Hz detects `appliedCount` changes and calls `updateFromShadows`
- Window resized to 800x750 to fit slider panel
- Removed the centered "SPU-94" text draw (panel now fills the space)

Added `RegisterPanel.cpp` to CMakeLists.txt target_sources.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification

- `cmake --build build --target spu94_standalone_Standalone` succeeds (100% built)
- 17/17 C unit tests pass (register, buffer, preset, process, FIR, API consumer tests)
- All pre-existing test failures in worktree environment (rt_safety symbol checks, CLI path resolution, Python packaging timeout, coverage validator) are environmental -- not caused by this plan's changes
- Standalone binary links and produces a working executable at `build/src/standalone/spu94_standalone_artefacts/Release/Standalone/SPU-94`

## Thread Safety Design

- GUI thread NEVER calls `spu94_set_reg_*` or `spu94_load_preset` directly
- All register writes go through `std::atomic<int16_t>` shadows; audio thread applies them via `pushPendingRegisterWrites`
- Preset switches go through `PresetCommandQueue`; audio thread calls `spu94_load_preset`
- `std::atomic<int16_t>` is lock-free on x86_64 (verifiable via `is_always_lock_free`)
- Acquire/release memory ordering ensures visibility across threads without mutex overhead

## Self-Check: PASSED

- All 4 created files exist on disk
- Both task commits (06cd6ba, 45d5a24) found in git log
- 08-03-SUMMARY.md exists at expected path
