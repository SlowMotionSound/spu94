---
phase: 24-state-automation-surface
plan: 02
subsystem: plugin-automation
tags: [host-automation, parameter-registration, gesture-api, daw-integration]
dependency_graph:
  requires: [StateSerializer-namespace, pendingPresetBuf-mechanism, atomic-scalar-bridge]
  provides: [9-AudioParameterFloat-instances, gesture-wired-GUI-controls, param-to-GUI-timer-sync]
  affects: [PluginProcessor.h, PluginProcessor.cpp, PluginEditor.cpp, MorphPanel.cpp]
tech_stack:
  added: []
  patterns: [AudioParameterFloat-raw-addParameter, onDragStart-onDragEnd-gesture-wiring, param-get-in-processBlock, setValue-from-audio-thread]
key_files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.cpp
    - src/plugin/MorphPanel.cpp
decisions:
  - "All 9 parameter IDs frozen as snake_case with versionHint=1 (PLUG-30); future params at END with versionHint=2+"
  - "processBlock reads paramXxx->get() for all 9 automated values; old atomics retained but not read for these 9"
  - "Gesture wiring uses JUCE 8 onDragStart/onDragEnd (available in 8.0.12) for proper single-gesture-per-drag recording"
  - "Preset-load sync uses setValue (audio-thread safe atomic write) for all 9 params; setStateInformation uses setValueNotifyingHost (message thread)"
  - "input_gain stringFromValue returns plain \"-inf dB\" (no Unicode infinity symbol) for portability across DAW automation lanes"
metrics:
  duration: "7m 54s"
  completed: "2026-05-12T15:24:26Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 0
  files_modified: 4
---

# Phase 24 Plan 02: Automation Surface (9 Host-Automatable Parameters) Summary

9 AudioParameterFloat instances registered with frozen IDs and correct display units; all GUI controls rewired through gesture API for bidirectional DAW automation.

## Tasks Completed

| # | Task | Commit | Key Changes |
|---|------|--------|-------------|
| 1 | Register 9 AudioParameterFloat instances and wire processBlock reads | 19afb98 | 9 addParameter calls in frozen order, processBlock reads param->get(), preset-load syncs params via setValue, setStateInformation syncs via setValueNotifyingHost |
| 2 | Rewire GUI controls through AudioParameterFloat gesture API and strip REVERT comment | fe398f4 | 6 editor knobs + 3 morph panel controls rewired with onDragStart/onDragEnd/setValueNotifyingHost, timer syncs params to GUI, REVERT comment removed |

## Implementation Details

### Parameter Registration (PluginProcessor.cpp constructor)

9 parameters registered via `addParameter` in the FROZEN order specified by PLUG-30. Registration order determines AU parameter index and must never change. All use `versionHint=1`. Future parameters go at the END with `versionHint=2+`.

| # | ID | Display Name | Range | Default | Display |
|---|---|---|---|---|---|
| 1 | `input_gain` | Input Gain | 0..16, skew@1.0 | 0.5 | dB (-inf to +24 dB) |
| 2 | `adpcm_send` | ADPCM Send | 0..1 | 1.0 | Percent 0-100% |
| 3 | `dry_send` | Dry Send | 0..1 | 0.0 | Percent 0-100% |
| 4 | `morph_position` | Morph Position | 0..1 | 0.625 | Percent 0-100% |
| 5 | `morph_speed` | Morph Speed | 0..1 | 0.5 | Percent 0-100% |
| 6 | `morph_grit` | Morph Grit | 0..1, step=1 | 0.0 | Int / Fract. |
| 7 | `dry_level` | Dry Level | 0..1 | 0.0 | Percent 0-100% |
| 8 | `adpcm_level` | ADPCM Level | 0..1 | 0.0 | Percent 0-100% |
| 9 | `reverb_level` | Reverb Level | 0..1 | 1.0 | Percent 0-100% |

### processBlock Wiring

All 9 params read via `paramXxx->get()` (denormalized real value) in processBlock. The old `atomic.load()` calls are replaced for these 9 values. DAC/latency-comp toggles remain on their atomics since they are not host-automated.

### Preset-Load Sync

When the audio thread applies a loaded preset (filePresetReady block), it calls `param->setValue(normalized)` on all 9 params to sync the host's view. `setValue` writes an internal atomic -- safe from any thread. In `setStateInformation` (message thread), the 4 float-appendix params additionally call `setValueNotifyingHost` to inform the host of restored values.

### GUI Gesture Wiring

All 9 controls use the JUCE 8 gesture API pattern:
- `onDragStart` -> `param->beginChangeGesture()`
- `onValueChange` -> `param->setValueNotifyingHost(normalized_value)`
- `onDragEnd` -> `param->endChangeGesture()`

This produces one automation gesture per drag, which hosts record correctly.

### Host -> GUI Sync (Timer Callback)

The editor's 30Hz timer reads each param's current value and updates the corresponding slider if the value differs by more than 0.001. Uses `dontSendNotification` to avoid feedback loops. MorphPanel's `updateKnobPosition` similarly syncs morphPosition, morphSpeed, and morphGrit from their params.

### REVERT Comment Removal

The 3-line Phase 23 UAT carryover comment ("REVERT after UAT approval") at PluginEditor.cpp:92-95 is removed. The 0..16 range with unity-at-midpoint skew is permanent per Phase 23 sign-off.

## Deviations from Plan

None -- plan executed exactly as written.

## Decisions Made

1. **onDragStart/onDragEnd confirmed available in JUCE 8.0.12** -- verified in the JUCE Slider header. Used the proper drag-lifecycle gesture pattern instead of the fallback begin+end-in-every-onValueChange approach.
2. **savePresetToString and getStateInformation updated to read from params** -- the plan specified processBlock wiring but the same source-of-truth principle applies to the save paths. Both now read `paramXxx->get()` instead of atomics for the 9 automated values.
3. **input_gain displays "-inf dB" at zero** (plain ASCII, no Unicode infinity) for maximum DAW automation lane compatibility.

## Verification

- [x] 9 addParameter calls in frozen order, all with versionHint=1
- [x] 9 parameter IDs match exactly: input_gain, adpcm_send, dry_send, morph_position, morph_speed, morph_grit, dry_level, adpcm_level, reverb_level
- [x] processBlock reads paramXxx->get() for all 9 automated params
- [x] No direct atomic .store() for 9 automated params in PluginEditor.cpp or MorphPanel.cpp
- [x] Every GUI control has gesture brackets (begin/end split across onDragStart/onDragEnd)
- [x] REVERT comment gone; setRange(0.0, 16.0) and setSkewFactorFromMidPoint(1.0) remain
- [x] No APVTS usage anywhere in plugin source
- [x] input_gain stringFromValue returns "-inf dB" when v < 0.0001f
- [x] morph_grit stringFromValue returns "Int" for v < 0.5f and "Fract." otherwise
- [x] VST3 + Standalone + CLAP + LV2 all build clean

## Self-Check: PASSED

All 4 modified files exist on disk. Both task commits verified in git log:
- 19afb98: feat(24-02): register 9 AudioParameterFloat instances and wire processBlock reads
- fe398f4: feat(24-02): rewire GUI controls through AudioParameterFloat gesture API
