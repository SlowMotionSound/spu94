---
phase: 59-sample-export
reviewed: 2026-05-29T17:12:26Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/plugin/PluginProcessor.h
  - src/plugin/PluginProcessor.cpp
  - src/plugin/PluginEditor.h
  - src/plugin/PluginEditor.cpp
findings:
  critical: 2
  warning: 3
  info: 0
  total: 5
status: issues_found
---

# Phase 59: Code Review Report

**Reviewed:** 2026-05-29T17:12:26Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

Reviewed the sample export feature (Phase 59): the `exportSampleToWav` method in PluginProcessor.cpp and the Export button wiring in PluginEditor.cpp. The WAV writer is structurally sound (correct RIFF/fmt/data layout, endian-safe), but there is a sample-rate mismatch bug on the file-load path that produces wrong-speed WAV output, a potential out-of-bounds vector access, and several robustness issues with the file-chooser callback.

## Critical Issues

### CR-01: Wrong sample rate in exported WAV for file-loaded samples

**File:** `src/plugin/PluginProcessor.cpp:2320`
**Issue:** `exportSampleToWav` writes `encodeRate` as the WAV sample rate, but `waveformData` contains different data depending on the loading path:

- **File-loaded samples** (`loadVoiceSample`, line 2293): `waveformData = result->L` stores the ORIGINAL 44100 Hz PCM from WavLoader (which always outputs 44.1 kHz, per WavLoader.h line 11). The resampled data used for ADPCM encoding is discarded. So `waveformData` is always 44100 Hz, but the WAV header will claim whatever `encodeRate` is (e.g., 11025 Hz).
- **Recorded samples** (`encodeRecordedSample`, line 2199): `waveformData` is decoded from ADPCM (post-encode, at encode rate). Here `encodeRate` correctly matches the data.

When a user loads a WAV file at 22050 Hz encode rate and exports, the exported WAV will contain 44100 Hz sample data but claim to be 22050 Hz, causing playback at half speed / one octave down.

**Fix:** Either always write 44100 as the sample rate for file-loaded samples (detecting the source), or resample `waveformData` to match `encodeRate` before export, or store the actual sample rate of `waveformData` alongside it. The cleanest fix is to track which rate `waveformData` actually represents:

```cpp
// In the header, alongside waveformFrames:
uint32_t waveformSampleRate = 44100;

// In loadVoiceSample (line 2293):
waveformData = result->L;
waveformFrames = result->numFrames;
waveformSampleRate = 44100;  // WavLoader always outputs 44.1 kHz

// In encodeRecordedSample (line 2206):
waveformFrames = waveformData.size();
waveformSampleRate = static_cast<uint32_t>(encodeRate.load(std::memory_order_relaxed));

// In exportSampleToWav (line 2320):
uint32_t sr = waveformSampleRate;  // instead of encodeRate
```

### CR-02: Potential out-of-bounds read in sample write loop

**File:** `src/plugin/PluginProcessor.cpp:2362`
**Issue:** The loop `waveformData[startFrame + i]` bounds-checks against `waveformFrames` but not against `waveformData.size()`. If `waveformFrames` is ever larger than `waveformData.size()`, this is an out-of-bounds vector access (undefined behavior / crash).

In `loadVoiceSample` (line 2293-2294), `waveformData = result->L` and `waveformFrames = result->numFrames`. The `LoadedWav` struct documents `numFrames` as "number of stereo frames" while `L` is the left channel at the same frame count, so they should match. However, in `encodeRecordedSample` (line 2206), `waveformFrames = waveformData.size()` which is correct. The divergent patterns between the two paths make a future mismatch likely, and there is no guard at the point of access.

**Fix:** Add a bounds check before the write loop:
```cpp
// After computing numFrames (line 2319):
if (startFrame + numFrames > waveformData.size())
    return false;
```

## Warnings

### WR-01: Export return value silently discarded

**File:** `src/plugin/PluginEditor.cpp:173`
**Issue:** The call `processorRef.exportSampleToWav(chosen)` discards the bool return value. If the export fails (disk full, permissions error, empty trim region), the user receives no feedback. The file may have been deleted by `deleteFile()` on line 2323 but not successfully rewritten.

**Fix:** Check the return value and show an alert on failure:
```cpp
bool ok = processorRef.exportSampleToWav(chosen);
if (!ok)
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon,
        "Export Failed",
        "Could not write WAV file.");
```

### WR-02: Pre-created empty file left on disk if user Music directory is read-only

**File:** `src/plugin/PluginEditor.cpp:151-152`
**Issue:** Before opening the save dialog, the code calls `suggestedFile.create()` to touch-create an empty file in the user's Music directory. The cleanup logic at line 167-168 only deletes this file if `suggestedFile.getSize() == 0 && suggestedFile != chosen`. If the create() call fails silently (read-only directory, permissions), the FileChooser may still launch but with a non-existent suggested file, which is harmless. However, if create() succeeds but the async callback is never called (editor destroyed during dialog, JUCE edge case), the empty file remains as litter.

The same pattern exists in `exportSingleSlot` (line 2041) for .spu94 files.

**Fix:** Remove the `suggestedFile.create()` call. JUCE's `FileChooser` in save mode does not require the file to exist -- it uses the path as a suggestion. If the file does not exist, the dialog simply pre-fills the filename field:
```cpp
// Remove this block:
// if (!suggestedFile.exists())
//     suggestedFile.create();
```

### WR-03: Export during concurrent load could read inconsistent state

**File:** `src/plugin/PluginProcessor.cpp:2300-2303`
**Issue:** `exportSampleToWav` checks `voiceSampleLoaded` with acquire semantics, but `waveformData` and `waveformFrames` are plain (non-atomic) members. If `loadVoiceSample` is called on the message thread while `exportSampleToWav` is also running on the message thread (via the async FileChooser callback), the data is safe because JUCE message thread callbacks are serialized. However, the `voiceSampleLoaded` guard does not protect against the `encodeRecordedSample` path, where `waveformData` is mutated. If `recordingJustStopped` fires and the timer calls `encodeRecordedSample` while the FileChooser's async callback for export is pending, both could run in the same message-thread dispatch, with `encodeRecordedSample` finishing first and resizing `waveformData` before the export callback reads it.

This is a narrow window (both fire from timer vs. FileChooser callback in the same message dispatch), but the JUCE message loop does not guarantee ordering between timer callbacks and async completion handlers within the same dispatch cycle.

**Fix:** Snapshot the waveform data at export time (before the async dialog) rather than reading it in the callback:
```cpp
exportSampleButton.onClick = [this]()
{
    // Snapshot waveform data NOW, before async dialog
    auto exportData = processorRef.getWaveformData();
    auto exportFrames = processorRef.getWaveformFrames();
    // ... then use exportData/exportFrames in the callback
};
```
Or, add a guard in `exportSampleToWav` that checks `waveformData.size() >= endFrame` before the loop.

---

_Reviewed: 2026-05-29T17:12:26Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
