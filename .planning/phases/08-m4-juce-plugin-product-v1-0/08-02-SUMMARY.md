---
phase: 08-m4-juce-plugin-product-v1-0
plan: 02
subsystem: audio-pipeline
tags: [juce, wav-loader, resampler, spu94-process, audio-callback, standalone]

# Dependency graph
requires:
  - phase: 08-01
    provides: "JUCE standalone shell (PluginProcessor/Editor scaffolding, CMake build target)"
  - phase: 05 (M1)
    provides: "libspu94 C API (spu94_init, spu94_process, spu94_load_preset, spu94_destroy)"
provides:
  - "WavLoader I/O wrapper: any-SR/any-BD/mono-stereo WAV to 44.1 kHz int16 stereo"
  - "PluginProcessor audio path: spu94_process wired in processBlock with Hall preset"
  - "File picker (async), Play/Stop transport buttons in editor"
  - "Continuous-loop WAV playback through SPU reverb"
affects: [08-03, 08-04]

# Tech tracking
tech-stack:
  added: [juce::AudioFormatManager, juce::WindowedSincInterpolator, juce::FileChooser]
  patterns: [message-thread-load-audio-thread-swap, stack-allocated-int16-io-buffers, atomic-flag-handoff]

key-files:
  created:
    - src/standalone/WavLoader.h
    - src/standalone/WavLoader.cpp
  modified:
    - src/standalone/PluginProcessor.h
    - src/standalone/PluginProcessor.cpp
    - src/standalone/PluginEditor.h
    - src/standalone/PluginEditor.cpp
    - src/standalone/CMakeLists.txt

key-decisions:
  - "Renamed local variable 'blockSize' to 'samplesToProcess' to avoid shadowing juce::AudioProcessor::blockSize member"

patterns-established:
  - "WAV load pattern: AudioFormatManager.registerBasicFormats() + createReaderFor() + WindowedSincInterpolator resample + float-to-int16 with 32767 scaling"
  - "Audio-thread data handoff: message thread sets pending buffers + atomic flag; audio thread swaps in processBlock (no locks, no allocs)"
  - "processBlock I/O: stack int16 arrays for spu94_process input/output, float conversion at boundaries"

requirements-completed: [STANDALONE-02, STANDALONE-03]

# Metrics
duration: 85min
completed: 2026-04-25
---

# Phase 8 Plan 02: WAV Loader + Audio Pipeline Summary

**WAV I/O wrapper (any-SR/BD/channels to 44.1 kHz int16 stereo via WindowedSincInterpolator) + spu94_process wired in real-time audio callback with Hall preset and continuous loop playback**

## Performance

- **Duration:** ~85 min (dominated by JUCE fetch/compile and full ctest suite)
- **Started:** 2026-04-25T20:57:06Z
- **Completed:** 2026-04-25T22:22:23Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- WavLoader handles any WAV/AIFF format (any sample rate, any bit depth, mono or stereo) and normalizes to 44.1 kHz int16 stereo for the SPU
- WindowedSincInterpolator used for high-quality one-shot resample at file-load time (per-channel state, Pitfall 7 avoided)
- Float-to-int16 conversion uses correct 32767 scaling (not 32768), std::lround rounding, and std::clamp saturation (Pitfall 10 avoided)
- processBlock feeds int16 blocks to spu94_process and converts output back to float -- zero heap allocations in the audio thread
- Continuous-loop playback for slider-twisting workflow (playPos wraps modulo numFrames)
- File picker uses launchAsync (not modal browseForFileToOpen) per JUCE best practices

## Task Commits

Each task was committed atomically:

1. **Task 1: WAV I/O wrapper** - `adb5ef0` (feat)
2. **Task 2: Wire PluginProcessor + editor buttons** - `03e9043` (feat)

## Files Created/Modified
- `src/standalone/WavLoader.h` - LoadedWav struct + WavLoader::load() declaration
- `src/standalone/WavLoader.cpp` - AudioFormatManager + WindowedSincInterpolator + float-to-int16 conversion
- `src/standalone/PluginProcessor.h` - SPU state members, WavSource struct, playback control methods
- `src/standalone/PluginProcessor.cpp` - spu94_init/process/destroy wired, atomic WAV swap, loop playback
- `src/standalone/PluginEditor.h` - Load WAV / Play / Stop button members + FileChooser
- `src/standalone/PluginEditor.cpp` - Button setup with async file picker, button layout
- `src/standalone/CMakeLists.txt` - Added WavLoader.cpp to target_sources

## Decisions Made
- Renamed `blockSize` local variable to `samplesToProcess` in processBlock to avoid shadowing `juce::AudioProcessor::blockSize` (GCC -Wshadow warning)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed -Wshadow warning on blockSize variable**
- **Found during:** Task 2
- **Issue:** Local `blockSize` variable in processBlock shadowed `juce::AudioProcessor::blockSize` member, producing a compiler warning
- **Fix:** Renamed to `samplesToProcess`
- **Files modified:** src/standalone/PluginProcessor.cpp
- **Verification:** Clean rebuild with no warnings
- **Committed in:** 03e9043

---

**Total deviations:** 1 auto-fixed (1 bug)
**Impact on plan:** Trivial rename, no scope change.

## Issues Encountered
- CMake FetchContent for JUCE required ~77s configure time (JUCE source shared from sibling worktree via FETCHCONTENT_SOURCE_DIR_JUCE to avoid re-downloading)
- Packaging tests (test_packaging_editable_install, test_packaging_wheel_tag) timed out during ctest run -- pre-existing, unrelated to this plan's changes

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Audio path is end-to-end functional: load WAV, play through SPU reverb, hear output
- Plan 03 builds on this: register sliders + preset dropdown wire into the existing spu94_state
- Plan 04 adds Wet/Dry mix on top of the working SPU output path

## Self-Check: PASSED

- All 8 files verified present
- Both task commits (adb5ef0, 03e9043) verified in git log
- Build succeeds: `cmake --build build --target spu94_standalone_Standalone` clean
- 80/82 ctests pass (2 pre-existing packaging timeouts, unrelated)
- No changes to src/spu94/ or include/spu94/ (core untouched)

---
*Phase: 08-m4-juce-plugin-product-v1-0*
*Completed: 2026-04-25*
