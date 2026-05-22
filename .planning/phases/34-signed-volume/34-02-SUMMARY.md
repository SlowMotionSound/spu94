---
phase: 34-signed-volume
plan: 02
subsystem: plugin-gui
tags: [signed-volume, phase-inversion, sampler-gui, juce-slider, volume-knob]

# Dependency graph
requires:
  - phase: 34-signed-volume (plan 01)
    provides: int16_t vol_l/vol_r in spu94_voice_mixer_key_on, signed volume C core API
  - phase: 31-voice-engine-gui
    provides: SamplerWindow, voiceEnginePitchKnob pattern, GUI trigger path
provides:
  - guiVoiceVolL/guiVoiceVolR atomic fields on PluginProcessor
  - Volume L/R rotary knobs in sampler panel with full signed range
  - Teal phase-flip "INV" indicator visible at negative volume
  - GUI trigger path sends knob values instead of hardcoded 0x7FFF
affects: [37-volume-sweep]

# Tech tracking
tech-stack:
  added: []
  patterns: [signed-range slider with visual phase-flip indicator]

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp
    - src/plugin/SamplerWindow.h

key-decisions:
  - "Default volume 0x3FFF (16383) instead of plan-suggested 0x7FFF -- 0x3FFF is the PS1-correct max positive volume per spu94_voice.h range -0x4000..+0x3FFF"
  - "Volume knobs placed in new section between marker row and ADSR display, window expanded 510->560px"
  - "Phase-flip indicator uses separate juce::Label components with show/hide rather than inline label text modification"

patterns-established:
  - "Signed volume knob pattern: juce::Slider range -16384..+16383, int16_t atomic, teal INV indicator"

requirements-completed: [SVOL-05, SVOL-02]

# Metrics
duration: 15min
completed: 2026-05-22
---

# Phase 34 Plan 02: Signed Volume GUI Summary

**Per-voice Volume L/R knobs with signed PS1 range and teal phase-flip indicator wired into sampler GUI trigger path**

## Performance

- **Duration:** 15 min
- **Started:** 2026-05-22T17:53:00Z
- **Completed:** 2026-05-22T18:08:21Z
- **Tasks:** 2 auto + 1 checkpoint (pending human verification)
- **Files modified:** 5

## Accomplishments
- Added guiVoiceVolL/guiVoiceVolR int16_t atomics to PluginProcessor with setters/getters
- Replaced hardcoded 0x7FFF in GUI trigger path with volume atomic loads
- Added Volume L/R rotary knobs to sampler panel with full signed range -16384..+16383
- Added teal "INV" phase-flip indicators that appear when volume knob is negative
- Expanded sampler window from 510px to 560px to accommodate new volume section
- MIDI dispatch path unchanged (velocity-derived positive volume preserved)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add volume atomics to processor and wire into GUI trigger path** - `09ba14e` (feat)
2. **Task 2: Add Volume L/R knobs with phase-flip indicator to sampler panel** - `65b4686` (feat)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - Added guiVoiceVolL/guiVoiceVolR atomics, setters, getters
- `src/plugin/PluginProcessor.cpp` - Replaced hardcoded 0x7FFF with guiVoiceVolL/R.load() in GUI trigger
- `src/plugin/PluginEditor.h` - Added voiceVolLKnob, voiceVolRKnob, phaseFlipLIndicator, phaseFlipRIndicator declarations
- `src/plugin/PluginEditor.cpp` - Volume knob setup, phase-flip indicator setup, layout in sampler window
- `src/plugin/SamplerWindow.h` - Window size expanded from 510 to 560px

## Decisions Made
- **Default volume 0x3FFF not 0x7FFF:** The plan suggested 0x7FFF as the atomic default, but the PS1 SPU volume range is -0x4000..+0x3FFF per the C core API. 0x7FFF is outside the valid PS1 range. Used 0x3FFF (16383) as the correct max positive value.
- **Volume section placement:** Placed between marker knobs (y=252-318) and ADSR display, expanding the sampler window height by 50px. This keeps related controls grouped by function.
- **Phase-flip indicator approach:** Used separate juce::Label components with show/hide visibility rather than modifying the knob label text. Cleaner separation of concerns and easier to style independently with the teal color.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Corrected default volume from 0x7FFF to 0x3FFF**
- **Found during:** Task 1
- **Issue:** Plan frontmatter specified `guiVoiceVolL{0x7FFF}` and `guiVoiceVolR{0x7FFF}`, but the PS1 SPU volume range is -0x4000..+0x3FFF. 0x7FFF exceeds the valid range.
- **Fix:** Used 0x3FFF (16383) as the default, matching the PS1-correct maximum positive volume.
- **Files modified:** src/plugin/PluginProcessor.h
- **Verification:** Compiles cleanly; value matches spu94_voice.h documentation
- **Committed in:** 09ba14e (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 bug in plan spec)
**Impact on plan:** Default value corrected to match PS1 hardware range. No scope creep.

## Issues Encountered
- Worktree required its own CMake build directory (build_wt) since build_test doesn't exist in the worktree. JUCE FetchContent succeeded in this worktree context.
- Font constructor deprecation warnings exist in pre-existing code (lines 127, 133, 176, 274). New code uses FontOptions to avoid additional warnings.

## User Setup Required
None - no external service configuration required.

## Checkpoint Status
Task 3 (human-verify) is pending. Visual and functional verification required before plan completion.

## Next Phase Readiness
- Signed volume GUI is ready for human verification
- Volume sweep (Phase 37) can build on this signed volume infrastructure
- No blockers

## Self-Check: PASSED

All files exist, all commits verified, build succeeds.

---
*Phase: 34-signed-volume*
*Completed: 2026-05-22 (pending Task 3 human verification)*
