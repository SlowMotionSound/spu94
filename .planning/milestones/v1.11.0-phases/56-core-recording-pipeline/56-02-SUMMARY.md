---
phase: 56-core-recording-pipeline
plan: 02
subsystem: gui
tags: [recording, gui, sampler-window, input-meter, ram-display]

requires:
  - phase: 56-core-recording-pipeline
    plan: 01
    provides: recording state machine, startRecording/stopRecording/encodeRecordedSample API, atomics for GUI polling
provides:
  - Record button in sampler window with coral visual feedback during recording
  - Input peak meter label showing live dB level (always active)
  - Recording stats label with seconds recorded and time remaining
  - Encode-on-stop triggering from timerCallback
  - Load Sample disabled during recording
affects: [57 sample rate selection, 58 threshold trigger]

tech-stack:
  added: []
  patterns: [timer-poll GUI update, atomic-to-label display, encode-on-stop from timer]

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp

key-decisions:
  - "Record button placed leftmost in sampler toolbar row, shifting other buttons right"
  - "Input peak meter uses text bar (pipe characters) plus numeric dB readout"
  - "Coral clip warning at peak > 0.95 matches PS1 color scheme"
  - "Recording stats use ADPCM block math (bytesUsed/16*28) for accurate time display"
  - "Layout shifted marker knobs and ADSR section up 14px to accommodate new labels"

requirements-completed: [REC-01, REC-04, REC-05, REC-06]

duration: 3min
completed: 2026-05-28
---

# Phase 56 Plan 02: GUI Integration Summary

**Record button, input peak meter, and RAM stats display wired to recording engine in sampler window with coral visual feedback and encode-on-stop triggering**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-28T21:12:56Z
- **Completed:** 2026-05-28T21:15:34Z
- **Tasks:** 1 auto + 1 checkpoint
- **Files modified:** 2

## Accomplishments
- Record button added to sampler window toolbar (leftmost position), toggles startRecording/stopRecording on click
- Button turns coral (0xFFE06060) during recording with "Stop" label, reverts to default "Record" when idle
- Input peak meter label shows live dB level with 12-character pipe-bar visualization, clips to coral at >0.95 peak
- Recording stats label displays seconds recorded and time remaining during active recording, clears when idle
- timerCallback polls recordingJustStopped atomic and calls encodeRecordedSample on message thread
- loadSampleButton disabled during recording to prevent concurrent mutation (T-56-07 mitigation)
- RAM meter updates live during recording from recordBytesUsed atomic
- Button state enforced every tick from recordingState atomic (T-56-06 mitigation)

## Task Commits

Each task was committed atomically:

1. **Task 1: Record button, input meter, and RAM display in sampler window** - `8f554f2` (feat)

## Files Created/Modified
- `src/plugin/PluginEditor.h` - Added recordButton, inputPeakMeterLabel, recordStatsLabel members
- `src/plugin/PluginEditor.cpp` - Constructor adds 3 new controls to sampler panel; resized() positions Record button first in toolbar row with input/stats labels at y=130; timerCallback adds encode-on-stop trigger, button state sync, peak meter update, recording stats, and live RAM display

## Decisions Made
- Record button placed leftmost in the toolbar row to give it visual priority as the primary recording action
- Peak meter uses text-based pipe-bar display (12 chars max) with numeric dB readout -- lightweight, no custom component needed
- Layout shifts existing controls (marker knobs, ADSR) up 14px to accommodate the new input/stats row at y=130 without increasing panel height
- Recording time math uses ADPCM block math (bytesUsed / 16 * 28 samples per block) for accurate time-to-ADPCM-capacity mapping

## Deviations from Plan

None - plan executed exactly as written.

## Threat Mitigations Applied
- T-56-06: Button text/colour enforced from recordingState atomic every tick (not just on click)
- T-56-07: loadSampleButton disabled during recording, re-enabled on stop

## Issues Encountered
None

## User Setup Required
None - no external service configuration required.

## Verification Checklist
1. Build and launch: `cd build && cmake --build . --target spu94_plugin --config Release && ./spu94_plugin_artefacts/Release/Standalone/SPU-94`
2. Open sampler window, verify Record button appears leftmost in toolbar
3. Verify input level meter shows activity with audio input present
4. Press Record -- button turns coral, shows "Stop"
5. While recording: verify stats show seconds counting up, time remaining counting down, RAM meter updates
6. Press Stop -- waveform should populate after brief encode pause
7. Press Trigger to play back recorded sample
8. Verify Load Sample was disabled during recording, re-enabled after
9. Test auto-stop: let recording run until RAM fills (~20 seconds at 44.1kHz)

## Self-Check: PASSED

- All source files exist
- Commit 8f554f2 verified in git log
- SUMMARY.md written

## Next Phase Readiness
- Recording pipeline complete end-to-end (backend + GUI)
- Ready for Phase 57 (sample rate selection) and Phase 58 (threshold trigger)

---
*Phase: 56-core-recording-pipeline*
*Completed: 2026-05-28*
