---
phase: 31-standalone-testbed-ux
plan: 02
subsystem: plugin-editor
tags: [voice-engine, gui, standalone, testbed]
dependency_graph:
  requires: [loadVoiceSample, triggerVoice, stopVoice, getVoiceSampleLoaded, getVoiceSampleName, getVoiceSampleBytes]
  provides: [voice-panel-gui, loadSampleButton, triggerVoiceButton, stopVoiceButton, voiceEnginePitchKnob, voiceSampleLabel]
  affects: [PluginEditor.h, PluginEditor.cpp]
tech_stack:
  added: []
  patterns: [JUCE standalone-gated GUI, async FileChooser, rotary knob, timerCallback polling]
key_files:
  created: []
  modified: [src/plugin/PluginEditor.h, src/plugin/PluginEditor.cpp]
decisions:
  - "Voice engine pitch knob is separate from existing voicePitchKnob (which controls ADPCM coloration bus rate)"
  - "All 6 new components gated inside wrapperType_Standalone block (TEST-04 preserved)"
  - "registerTop adjusted from 75 to 110 in standalone mode to accommodate voice panel second row"
  - "voiceEnginePitchKnob range 1-16383 (raw SPU pitch register) with 4096 default (unity/middle C)"
metrics:
  duration: "~12m"
  completed: "2026-05-17T01:42:38Z"
  tasks_completed: 1
  tasks_total: 2
  files_modified: 2
---

# Phase 31 Plan 02: Voice Engine GUI Panel Summary

Add standalone-only voice engine GUI panel with Load Sample, Trigger, Stop Voice buttons, pitch knob, and status label -- completing the testbed user experience for the voice engine.

## One-liner

Standalone-gated voice panel GUI: Load Sample (async FileChooser -> loadVoiceSample), Trigger/Stop buttons, raw SPU pitch knob (1-16383), status label polling via timerCallback.

## Completed Tasks

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Declare voice panel components in PluginEditor.h and wire in PluginEditor.cpp | ea24a98 | src/plugin/PluginEditor.h, src/plugin/PluginEditor.cpp |

## Pending Tasks (Checkpoint)

| Task | Name | Status |
|------|------|--------|
| 2 | Human verification of standalone testbed UX | Awaiting human-verify |

## Implementation Details

### Task 1: Voice panel GUI wiring

**Header (PluginEditor.h):**
- Added 6 member declarations after the ADPCM voice path controls section: `loadSampleButton`, `triggerVoiceButton`, `stopVoiceButton`, `voiceSampleLabel`, `voiceEnginePitchKnob`, `voiceEnginePitchLabel`

**Constructor (PluginEditor.cpp):**
- Extended the existing `if (wrapperType_Standalone)` block with all new components
- `loadSampleButton.onClick`: launches async FileChooser (*.wav;*.aiff;*.aif), calls `processorRef.loadVoiceSample(result)` on selection
- `triggerVoiceButton.onClick`: reads `voiceEnginePitchKnob.getValue()` cast to uint16_t, calls `processorRef.triggerVoice(pitch)`
- `stopVoiceButton.onClick`: calls `processorRef.stopVoice()`
- `voiceEnginePitchKnob`: Rotary style, TextBoxBelow, range 1.0-16383.0 step 1.0, default 4096.0 (unity), suffix " pitch"
- `voiceSampleLabel`: left-justified, initially empty

**Layout (resized):**
- Second row at y=45 inside standalone gate: loadSampleButton (10,45,110,30), triggerVoiceButton (125,45,70,30), stopVoiceButton (200,45,90,30), voiceEnginePitchLabel (295,37,80,16), voiceEnginePitchKnob (295,51,80,54), voiceSampleLabel (380,45,200,30)
- `registerTop` conditionally set to 110 (standalone) vs 75 (plugin) to prevent overlap

**Timer (timerCallback):**
- Polls `processorRef.getVoiceSampleLoaded()` with acquire ordering
- Updates `voiceSampleLabel` with filename + byte count (e.g., "kick.wav 1024B")

## Deviations from Plan

None - plan executed exactly as written.

## Verification Results

- Plugin builds cleanly: `cmake --build . --target spu94_plugin` (zero errors, zero warnings in modified files)
- Standalone builds cleanly: `cmake --build . --target spu94_plugin_Standalone` (links successfully)
- All 6 new components declared in PluginEditor.h (grep confirmed)
- All 6 addAndMakeVisible calls inside wrapperType_Standalone guard (lines 55-105 inside guard starting at line 15)
- loadSampleButton.onClick launches FileChooser and calls loadVoiceSample (line 70)
- triggerVoiceButton.onClick reads voiceEnginePitchKnob value and calls triggerVoice (lines 78-79)
- stopVoiceButton.onClick calls stopVoice (line 86)
- voiceEnginePitchKnob range is 1.0 to 16383.0 with default 4096.0 (lines 93-94)
- timerCallback updates voiceSampleLabel when voiceSampleLoaded is true (lines 469-474)
- Standalone resized() positions all new components (lines 507-512)

## Threat Mitigations Verified

| Threat | Mitigation | Status |
|--------|-----------|--------|
| T-31-05 (pitch knob value tampering) | Slider range constrained to 1..16383; cast to uint16_t before API call | Implemented |
| T-31-06 (large WAV file) | Accepted (testbed context) -- encode runs on message thread | Accepted |

## Known Stubs

None. All GUI components are fully wired to the processor API from Plan 01.

## Self-Check: PASSED
