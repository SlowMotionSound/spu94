---
phase: 31-standalone-testbed-ux
plan: 01
subsystem: plugin-processor
tags: [voice-engine, midi, standalone, testbed]
dependency_graph:
  requires: [spu94_voice.h, spu94_sample_loader.h, spu94_voice_mixer_t, WavLoader]
  provides: [loadVoiceSample, triggerVoice, stopVoice, MIDI-dispatch, round-robin-allocator]
  affects: [PluginProcessor.h, PluginProcessor.cpp]
tech_stack:
  added: []
  patterns: [JUCE MidiBuffer iteration, round-robin voice allocation, message-thread sample encode]
key_files:
  created: []
  modified: [src/plugin/PluginProcessor.h, src/plugin/PluginProcessor.cpp]
decisions:
  - "acceptsMidi gated on wrapperType_Standalone -- plugin format path unchanged (TEST-04)"
  - "MIDI dispatch before WAV-active check -- voices always respond when sample loaded"
  - "Round-robin across all 24 voices -- no reserved voice slots"
  - "midiNoteToPitch clamped to 0x3FFF -- matches hardware pitch register max (T-31-03)"
  - "Master volume set to 0x7FFF on sample load -- full volume by default"
metrics:
  duration: "49m 8s"
  completed: "2026-05-17T00:60:54Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 2
---

# Phase 31 Plan 01: Voice Engine Processor Wiring Summary

Wire the voice engine C core APIs into the JUCE processor layer with sample loading, GUI-triggered playback, and standalone-only MIDI polyphonic dispatch via round-robin voice allocation.

## One-liner

24-voice MIDI-driven polyphonic voice engine wired into standalone processBlock with WAV-to-ADPCM sample loading via spu94_sample_encode_to_ram.

## Completed Tasks

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Add voice engine API and state to PluginProcessor.h | 6f7b10e | src/plugin/PluginProcessor.h |
| 2 | Implement voice engine methods and MIDI dispatch in PluginProcessor.cpp | 136791b | src/plugin/PluginProcessor.cpp |

## Implementation Details

### Task 1: Header declarations
- Added `#include <spu94/spu94_voice.h>` and `#include <spu94/spu94_sample_loader.h>` inside the extern "C" block
- Changed `acceptsMidi()` from hardcoded `false` to `wrapperType == wrapperType_Standalone`
- Declared public API: `loadVoiceSample`, `triggerVoice`, `stopVoice`
- Declared public accessors: `getVoiceSampleLoaded`, `getVoiceSampleName`, `getVoiceSampleBytes`
- Added private state: `voiceSampleLoaded`, `voiceSampleName`, `voiceSampleBytes`, `noteForVoice[24]`, `nextVoice`
- Declared private helpers: `midiNoteToPitch`, `allocateVoice`, `findVoiceForNote`

### Task 2: Implementation
- Enabled `midiMessages` parameter in processBlock (removed comment-out)
- MIDI dispatch loop processes note-on/off when `voiceSampleLoaded` is true, inside standalone branch
- `loadVoiceSample`: WavLoader::load -> spu94_sample_encode_to_ram -> enable mixer with full master volume
- `triggerVoice`: direct key_on on voice 0 at given pitch
- `stopVoice`: direct key_off on voice 0
- `midiNoteToPitch`: pow(2, semitones/12) * 0x1000, clamped 1..0x3FFF
- `allocateVoice`: round-robin index across 24 voices, stores note for later lookup
- `findVoiceForNote`: linear scan, clears entry on match, returns -1 if no match

## Deviations from Plan

None - plan executed exactly as written.

## Verification Results

- Plugin builds cleanly (`cmake --build . --target spu94_plugin` -- zero errors)
- All 23 C unit tests pass (including mixer tests from Phase 30)
- All 4 plugin-specific tests pass (state_serializer, state_roundtrip, bus_layout, mono_sum)
- `acceptsMidi` contains `wrapperType_Standalone` (grep confirmed)
- processBlock MIDI parameter is not commented out (grep confirmed)
- `loadVoiceSample`, `triggerVoice`, `stopVoice` methods exist (grep confirmed: 3 matches in .cpp)

## Threat Mitigations Verified

| Threat | Mitigation | Status |
|--------|-----------|--------|
| T-31-01 (WAV overflow) | loadVoiceSample checks `bytes > 0` before enabling mixer | Implemented |
| T-31-03 (MIDI pitch OOB) | midiNoteToPitch clamps to 0x0001..0x3FFF | Implemented |

## Known Stubs

None. All methods are fully implemented with working data paths.
