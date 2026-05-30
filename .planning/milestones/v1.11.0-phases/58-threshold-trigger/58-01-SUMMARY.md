---
phase: 58-threshold-trigger
plan: 01
subsystem: sampler
tags: [recording, threshold, state-machine, audio-capture, juce]

# Dependency graph
requires:
  - phase: 56-live-input-recording
    provides: "RecState enum, startRecording/stopRecording, processBlock capture, staging buffer"
provides:
  - "REC_ARMED state in 4-state recording machine"
  - "armRecording() method with buffer pre-allocation"
  - "Threshold-triggered auto-start from exact transient sample"
  - "Threshold knob (-60 to 0 dB) with dB-to-linear conversion"
  - "Record button tri-state display (default/amber/coral)"
affects: [sampler, recording, voice-engine]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Threshold detection in audio callback with per-sample trigger offset"
    - "Tri-state button display driven by atomic enum in timerCallback"

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp

key-decisions:
  - "Threshold stored as linear amplitude in atomic, GUI converts dB-to-linear"
  - "Transient preserved by capturing from the exact trigger sample within the block"
  - "Disarming from ARMED returns to IDLE (not STOPPED) since nothing was recorded"

patterns-established:
  - "Per-sample trigger offset: when threshold crossed mid-block, capture starts at that sample index"
  - "Tri-state button cycle: IDLE->ARMED->RECORDING->STOP with distinct colors"

requirements-completed: [TRIG-01, TRIG-02, TRIG-03, TRIG-04]

# Metrics
duration: 9min
completed: 2026-05-29
---

# Phase 58 Plan 01: Threshold Trigger Summary

**4-state recording machine with threshold-triggered auto-start capturing from exact transient sample, adjustable -60..0 dB threshold knob, tri-state record button (default/amber/coral)**

## Performance

- **Duration:** 9 min
- **Started:** 2026-05-29T02:26:43Z
- **Completed:** 2026-05-29T02:35:37Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Extended recording state machine from 3 states to 4 (IDLE/RECORDING/STOPPED/ARMED) with full transition coverage
- Threshold-triggered auto-start preserves attack transient by capturing from the exact sample that crossed the threshold within the audio block
- Threshold knob provides -60 to 0 dB range with real-time dB-to-linear conversion stored in processor atomic
- Record button displays three distinct visual states with PS1-palette colors (default, amber armed, coral recording)
- All sampling controls (load, encode rate, threshold) lock during armed and recording states

## Task Commits

Each task was committed atomically:

1. **Task 1: Backend -- ARMED state and threshold-triggered capture** - `156d406` (feat)
2. **Task 2: GUI -- threshold knob, record button tri-state, armed display** - `15572a2` (feat)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - RecState enum extended to 4 values, armRecording/isArmed/getRecordingThreshold added, recordingThreshold atomic
- `src/plugin/PluginProcessor.cpp` - armRecording() implementation, startRecording() handles ARMED->RECORDING, stopRecording() handles ARMED->IDLE disarm with buffer free, processBlock REC_ARMED branch with per-sample threshold detection and transient-preserving capture
- `src/plugin/PluginEditor.h` - thresholdKnob and thresholdLabel member declarations
- `src/plugin/PluginEditor.cpp` - Threshold knob setup with dB range/conversion, record button tri-state onClick cycle, timerCallback tri-state display with amber/coral colors, control locking during armed/recording, threshold knob layout at x=330

## Decisions Made
- Threshold stored as linear amplitude in the processor atomic (0.01f default, approx -40 dB). The GUI knob operates in dB and converts on value change. This keeps the audio thread comparison branchless (no log/pow per sample).
- When threshold is crossed mid-block, capture starts from that exact sample index (triggerSample), not the next block. This preserves the attack transient that crossed the threshold.
- Disarming from REC_ARMED transitions to REC_IDLE (not REC_STOPPED) because nothing was recorded. The staging buffer is freed and standalone input is re-muted.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- Threshold-triggered recording is fully functional and ready for UAT
- The existing auto-stop at buffer capacity (REC-02) works correctly with threshold-triggered recording
- The encode-on-stop path (recordingJustStopped flag) works for both threshold-triggered and manual recording flows

## Self-Check: PASSED

All files exist, both commits verified, key content present in all modified files.

---
*Phase: 58-threshold-trigger*
*Completed: 2026-05-29*
