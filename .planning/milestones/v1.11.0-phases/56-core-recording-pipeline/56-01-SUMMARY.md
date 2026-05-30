---
phase: 56-core-recording-pipeline
plan: 01
subsystem: audio-engine
tags: [recording, adpcm, state-machine, staging-buffer, real-time]

requires:
  - phase: 31-standalone-testbed
    provides: voice engine infrastructure, loadVoiceSample pattern, adpcmStateCache, waveformData
  - phase: 27-single-voice-playback
    provides: spu94_sample_encode_to_ram, voice_ram, ADPCM codec
provides:
  - Recording state machine (IDLE/RECORDING/STOPPED) with atomic GUI polling
  - startRecording/stopRecording/encodeRecordedSample public API
  - processBlock live input capture to staging buffer with peak and RAM tracking
  - Buffer-then-encode pipeline producing valid ADPCM in voice_ram
affects: [56-02 GUI integration, 57 sample rate selection, 58 threshold trigger, 59 sample export]

tech-stack:
  added: []
  patterns: [buffer-then-encode, message-thread batch encode, audio-thread staging capture]

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp

key-decisions:
  - "Staging buffer fixed at 917504 samples (32768 blocks x 28 samples) matching full 512KB voice RAM"
  - "Waveform display shows decoded ADPCM (authentic PS1-degraded view) not original PCM"
  - "Input peak always computed (even when not recording) so GUI meter shows signal at all times"

patterns-established:
  - "Buffer-then-encode: accumulate raw PCM on audio thread, batch encode on message thread after stop"
  - "Audio-thread staging: single-writer index (recordStagingCount), no atomic needed, bounds-checked"
  - "recordingJustStopped flag: audio thread sets, GUI timer polls and calls encodeRecordedSample"

requirements-completed: [REC-01, REC-02, REC-03, REC-04, REC-05, REC-06]

duration: 3min
completed: 2026-05-28
---

# Phase 56 Plan 01: Recording Engine Backend Summary

**Buffer-then-encode recording pipeline: 3-state machine, mono-summed staging capture in processBlock, batch ADPCM encode on stop with waveform/state cache population**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-28T21:06:35Z
- **Completed:** 2026-05-28T21:09:15Z
- **Tasks:** 1
- **Files modified:** 2

## Accomplishments
- Recording state machine (REC_IDLE -> REC_RECORDING -> REC_STOPPED) with atomic GUI polling
- processBlock captures mono-summed live input to pre-allocated staging buffer with int16 conversion, peak tracking, and estimated ADPCM byte count
- Auto-stop triggers when staging count reaches 917504 samples (512KB ADPCM capacity)
- encodeRecordedSample batch-encodes via spu94_sample_encode_to_ram, builds adpcmStateCache by decoding each block, populates waveformData with decoded PCM, sets voiceSampleLoaded
- Input peak level updates every processBlock even when idle (GUI meter always active)
- loadVoiceSample guarded during active recording to prevent concurrent mutation

## Task Commits

Each task was committed atomically:

1. **Task 1: Recording state machine, staging buffer, and public API** - `ee585d1` (feat)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - RecState enum, recording atomics, staging buffer members, public API (startRecording/stopRecording/encodeRecordedSample/isRecording + atomic getters)
- `src/plugin/PluginProcessor.cpp` - Recording methods implementation, processBlock input capture with peak/RAM tracking, auto-stop logic, loadVoiceSample guard

## Decisions Made
- Waveform display populated from decoded ADPCM blocks rather than original PCM -- shows authentic PS1-degraded waveform, consistent with what the playback engine will produce
- Input peak computed in all states (not just recording) so the GUI level meter shows signal strength even when idle, matching hardware sampler behavior
- Staging buffer sized to exact maximum (917504 = 32768 blocks x 28 samples) rather than over-allocating -- matches the 512KB voice RAM constraint precisely

## Deviations from Plan

None - plan executed exactly as written.

## Threat Mitigations Applied
- T-56-01: Bounds check (recordStagingCount < recordStagingCapacity) before every staging buffer write
- T-56-02: No allocation, no locks, no syscalls on audio thread -- only array writes and atomic stores
- T-56-03: loadVoiceSample returns early if recordingState != REC_IDLE
- T-56-04: Buffer cleared/overwritten on each startRecording (accepted risk)

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Recording backend complete, ready for Plan 02 (GUI integration: record button, input meter, RAM display, waveform wiring)
- All atomics and public methods in place for GUI timer polling
- recordingJustStopped flag ready for GUI timer to trigger encodeRecordedSample on message thread

---
*Phase: 56-core-recording-pipeline*
*Completed: 2026-05-28*
