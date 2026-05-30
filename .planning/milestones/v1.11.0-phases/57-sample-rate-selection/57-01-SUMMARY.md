---
phase: 57-sample-rate-selection
plan: 01
subsystem: ui
tags: [juce, sample-rate, pitch-register, recording, bidirectional-sync]

requires:
  - phase: 56-core-recording-pipeline
    provides: recording state machine, staging buffer, encodeRate atomic, timerCallback stats display

provides:
  - Bidirectional pitch-knob-to-encodeRate wiring (continuous sample rate control)
  - Rate-aware time display in all states (idle and recording)
  - Hz display on pitch knob instead of raw register value

affects: [recording-pipeline, sampler-gui, sample-export]

tech-stack:
  added: []
  patterns:
    - "textFromValueFunction/valueFromTextFunction for human-readable slider display"
    - "Bidirectional knob/dropdown sync with dontSendNotification to prevent loops"

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.cpp

key-decisions:
  - "Pitch knob label changed from 'Voice Pitch' to 'Sample Rate' -- knob now primarily serves as recording rate control"
  - "Text box widened from 50px to 80px to accommodate Hz/kHz display"
  - "Dropdown uses dontSendNotification when setting pitch knob to prevent notification loop, calls setGuiVoicePitch explicitly instead"
  - "Rate clamped to minimum 1000 Hz (T-57-01) -- below that is subsonic/impractical"
  - "Division-by-zero guard (T-57-02) via std::max(encodeRate, 1)"

patterns-established:
  - "Bidirectional control sync: when A changes B, use dontSendNotification and manually sync dependent state"

requirements-completed: [RATE-01, RATE-02, RATE-03, RATE-04]

duration: 3min
completed: 2026-05-29
---

# Phase 57 Plan 01: Sample Rate Selection Summary

**Pitch knob wired as continuous sample rate control with Hz display, bidirectional dropdown sync, and rate-aware recording time visible before pressing Record**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-29T00:34:09Z
- **Completed:** 2026-05-29T00:37:13Z
- **Tasks:** 2
- **Files modified:** 1

## Accomplishments
- Pitch knob displays computed Hz (e.g., "22.1 kHz", "5512 Hz") instead of raw SPU pitch register value
- Turning the knob calls setEncodeRate with the derived rate, so any arbitrary sample rate is available across the full PS1 pitch register range (1..16383)
- Dropdown auto-highlights when knob lands on a preset rate (44100/22050/11025/5512), clears to no selection at custom rates
- Idle time display shows max recording time at current rate ("Max: 41.6s at 22.1 kHz") before recording starts
- During recording, time remaining uses actual encode rate for capacity calculation instead of hardcoded 44100

## Task Commits

Each task was committed atomically:

1. **Task 1: Wire pitch knob as continuous rate control with bidirectional sync** - `734edcf` (feat)
2. **Task 2: Rate-aware time display in all states** - `99c0f2c` (feat)

## Files Created/Modified
- `src/plugin/PluginEditor.cpp` - Pitch knob textFromValueFunction/valueFromTextFunction for Hz display, onValueChange sets encodeRate with 1000 Hz floor, syncs dropdown selection; timerCallback uses encodeRate for time math, shows max time when idle

## Decisions Made
- Changed encodeRateBox.onChange to use `dontSendNotification` when setting pitch knob value, with explicit `setGuiVoicePitch` call. This prevents a notification loop where dropdown->knob->dropdown would cycle, while preserving correct voice pitch register sync.
- Label changed from "Voice Pitch" to "Sample Rate" since the knob now primarily controls recording sample rate. The voice pitch register is still set as a side effect.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- All four RATE requirements satisfied (RATE-01 through RATE-04)
- Ready for UAT: launch standalone, open sampler window, verify rate knob shows Hz, dropdown syncs, idle time display updates, recording time uses correct rate

---
*Phase: 57-sample-rate-selection*
*Completed: 2026-05-29*
