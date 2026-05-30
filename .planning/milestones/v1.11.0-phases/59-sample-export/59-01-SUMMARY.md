---
phase: 59-sample-export
plan: 01
subsystem: sampler-gui
tags: [export, wav, sampler, gui]
dependency_graph:
  requires: [voice-engine, adpcm-codec, waveform-display]
  provides: [sample-export-wav]
  affects: [sampler-workflow]
tech_stack:
  added: []
  patterns: [manual-wav-header, async-filechooser]
key_files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
decisions: []
metrics:
  duration: 8m
  completed: 2026-05-29
---

# Phase 59 Plan 01: Sample Export Summary

Manual 16-bit mono WAV writer with trim-region extraction and encode-rate sample rate header, plus async Export button in the sampler top row.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | WAV export method in PluginProcessor | 67a8016 | PluginProcessor.h, PluginProcessor.cpp |
| 2 | Export button and FileChooser in sampler GUI | dbdeb1e | PluginEditor.h, PluginEditor.cpp |

## What Was Built

### exportSampleToWav (PluginProcessor)

Public method `bool exportSampleToWav(const juce::File& destFile)` that:

1. Guards against no-sample-loaded and empty-trim-region cases
2. Reads normalized start/end positions and computes frame range via floor/ceil
3. Writes a complete 44-byte RIFF WAV header manually (no JUCE AudioFormatWriter dependency)
4. Encodes all multi-byte fields as little-endian via `juce::ByteOrder::swapIfBigEndian`
5. Sets the WAV sample rate to `encodeRate` (the rate the sample was recorded/loaded at), not 44100

### Export Button (PluginEditor)

- `exportSampleButton` placed at (325, 10, 70, 30) in the sampler top row, between Stop Voice and the right-column controls
- Async FileChooser in saveMode with warnAboutOverwriting, defaulting to `{sampleName}.wav` in the user's Music directory
- Touch-file pattern for filename pre-fill (same as exportSingleSlot)
- Button is disabled by default and enabled/disabled in the timer callback based on `voiceSampleLoaded`

## Deviations from Plan

None -- plan executed exactly as written.

## Verification

- Build succeeds with zero errors across all targets (Standalone, VST3, LV2, CLAP)
- `grep -c exportSampleButton PluginEditor.cpp` returns 5 (addAndMakeVisible, initial setEnabled, onClick, timer setEnabled, setBounds)
- 118 lines added across 4 files, 0 deleted

## Self-Check: PASSED

- All 4 modified files exist on disk
- Both commit hashes (67a8016, dbdeb1e) found in git log
- exportSampleToWav method present in PluginProcessor.cpp (1 definition)
- exportSampleButton present in PluginEditor.cpp (5 references)
