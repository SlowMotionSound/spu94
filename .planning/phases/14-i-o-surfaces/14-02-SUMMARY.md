---
phase: 14-i-o-surfaces
plan: 02
subsystem: gui
tags: [preset-io, juce, file-dialog, modified-indicator, combobox]

requires:
  - phase: 13-core-preset-api
    provides: "spu94_preset_save/spu94_preset_load C API, SPU94_PRESET_BUF_SIZE constant"
provides:
  - "JUCE Save button with name prompt + file dialog + .spu94 file write"
  - "JUCE Load button with file dialog + .spu94 file read + full GUI sync"
  - "Custom preset dropdown entry with diamond prefix after file load (D-09)"
  - "Modified-state asterisk indicator tracking all 46 engine fields (D-11)"
  - "Thread-safe file preset handoff via acquire/release atomics"
affects: []

tech-stack:
  added: []
  patterns: ["AlertWindow modal with ModalCallbackFunction::create for name prompt", "Pending preset buffer with atomic acquire/release for message->audio thread handoff", "PresetSnapshot struct with memcmp-based float comparison for dirty tracking", "Custom ComboBox entry management with dynamic add/remove"]

key-files:
  created: []
  modified:
    - "src/standalone/PluginProcessor.h"
    - "src/standalone/PluginProcessor.cpp"
    - "src/standalone/PluginEditor.h"
    - "src/standalone/PluginEditor.cpp"

key-decisions:
  - "Used memcmp for float equality in checkModified to avoid -Wfloat-equal while preserving exact bitwise semantics"
  - "File preset handoff uses same acquire/release atomic pattern as existing PresetCommandQueue"
  - "Diamond bullet (U+25C6) as custom preset visual distinguisher per D-09"
  - "30Hz timer-based dirty check polls all 46 fields (35 registers + 11 mixer/DAC values)"

patterns-established:
  - "showPresetNamePrompt: two-step save flow with AlertWindow then FileChooser"
  - "syncMixerKnobsFromProcessor: full GUI knob/toggle sync from processor atomics"
  - "captureBaseline/checkModified/updatePresetDisplayName: snapshot-based modified tracking"

requirements-completed: [PRE-08, PRE-09]

duration: 10min
completed: 2026-05-02
---

# Phase 14 Plan 02: JUCE Preset Save/Load UI Summary

**Save/Load buttons with name prompt and file dialogs, custom preset dropdown entry with diamond marker, and 46-field modified-state asterisk indicator using memcmp-based float comparison**

## Performance

- **Duration:** 10 min
- **Started:** 2026-05-02T16:36:42Z
- **Completed:** 2026-05-02T16:46:25Z
- **Tasks:** 2 complete, 1 pending (human verification checkpoint)
- **Files modified:** 4

## Accomplishments
- Added savePresetToString/loadPresetFromString/getFilePresetAppliedCount to PluginProcessor
- Implemented audio-thread file-preset drain with acquire/release atomics matching existing PresetQueue pattern
- Full mixer/DAC atomic sync after file preset load (11 values: input gain, 5 faders, latency comp, 4 DAC toggles)
- Save button opens AlertWindow name prompt (pre-filled from current preset), then native file dialog with suggested filename
- Load button opens native file dialog filtered to .spu94, parses name= field for dropdown display
- Custom preset entry with diamond bullet prefix appears in ComboBox after file load
- Factory preset selection clears custom entry and rebuilds dropdown
- PresetSnapshot captures all 46 engine fields as baseline on every preset load
- 30Hz timerCallback polls checkModified() and appends/removes asterisk from dropdown text
- Updated toolbar layout to accommodate Save/Load buttons between Stop and Preset label
- Zero compiler warnings (fixed -Wfloat-equal via memcmp, fixed -Wshadow via variable rename)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add Save/Load buttons with name prompt, file dialogs, and processor file preset support** - `1230a86` (feat)
2. **Task 2: Add custom preset dropdown entry and modified-state asterisk indicator** - `e3cf09b` (feat)

## Files Modified
- `src/standalone/PluginProcessor.h` - Added savePresetToString, loadPresetFromString, getFilePresetAppliedCount public methods; pendingPresetBuf, pendingPresetLen, filePresetReady, filePresetAppliedCount private members
- `src/standalone/PluginProcessor.cpp` - Implemented 3 file preset methods; added file-preset drain block in processBlock with full mixer/DAC atomic sync
- `src/standalone/PluginEditor.h` - Added savePresetButton, loadPresetButton, showPresetNamePrompt, customPresetName, lastFilePresetCount, syncMixerKnobsFromProcessor, kCustomPresetId, PresetSnapshot, baseline, modifiedState, captureBaseline, checkModified, updatePresetDisplayName
- `src/standalone/PluginEditor.cpp` - Implemented Save/Load button handlers, showPresetNamePrompt with AlertWindow, syncMixerKnobsFromProcessor, captureBaseline, checkModified (memcmp-based), updatePresetDisplayName; updated presetSelector.onChange for D-10; updated timerCallback for file-preset detection and modified tracking; updated resized() layout

## Decisions Made
- Used memcmp for bitwise float equality in checkModified -- semantically correct for slider-sourced values stored/loaded without arithmetic, avoids -Wfloat-equal warning
- Renamed inner lambda variable from `result` to `buttonResult`/`chosen` to avoid -Wshadow
- Diamond bullet (U+25C6) chosen as custom entry prefix -- if fonts don't render it, fallback to ">>" prefix is documented in plan
- File preset load path uses same single-writer/single-reader atomic pattern as existing factory PresetQueue

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed -Wfloat-equal warnings in checkModified**
- **Found during:** Task 2 (build verification)
- **Issue:** Direct `!=` comparison of float values triggers -Wfloat-equal warning
- **Fix:** Used memcmp-based lambda `feq()` for bitwise float comparison, preserving exact-match semantics
- **Files modified:** src/standalone/PluginEditor.cpp
- **Commit:** e3cf09b

**2. [Rule 1 - Bug] Fixed -Wshadow warning in showPresetNamePrompt**
- **Found during:** Task 2 (build verification)
- **Issue:** Inner lambda `auto result = fc.getResult()` shadows outer lambda parameter `int result`
- **Fix:** Renamed outer to `buttonResult`, inner to `chosen`
- **Files modified:** src/standalone/PluginEditor.cpp
- **Commit:** e3cf09b

## Known Stubs

None -- all functionality is fully wired.

## Self-Check: PASSED

- All 4 modified source files exist
- Both task commits verified (1230a86, e3cf09b)
- SUMMARY.md created at expected path

---
*Phase: 14-i-o-surfaces*
*Completed: 2026-05-02 (pending verification)*
