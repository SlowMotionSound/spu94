---
phase: 45-auto-pan
plan: 01
subsystem: dsp
tags: [auto-pan, sweep, retrigger, opposition-phase, stereo-movement, linear-crossfade]

# Dependency graph
requires:
  - phase: 44-tremolo
    plan: 01
    provides: "hzToShift table, depth scaling pattern, mutual exclusion guard"
provides:
  - "Auto-pan activation logic in processBlock (opposition-phase L/R sweep configuration)"
  - "Auto-pan atomics: enabled, speedHz, depth, ratio"
  - "Three-way mutual exclusion: VCA ramp, tremolo, auto-pan"
  - "2 integration tests proving opposition-phase and linear crossfade dip"
affects: [45-02, 51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["opposition-phase L/R sweep (direction=1 vs direction=0) for stereo panning", "reuse tremolo hzToShift table and depth scaling formula"]

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c

key-decisions:
  - "No curve control for auto-pan -- PAN-04 mandates linear-only (mode=0 always)"
  - "Default speed 2.0 Hz (slower than tremolo's 5.0 Hz since panning is more noticeable)"
  - "Default depth 1.0 (full L-to-R excursion by default)"
  - "Tremolo has priority when both enabled simultaneously (defensive)"
  - "Reuses existing hzToShift table -- no code duplication"

patterns-established:
  - "Opposition-phase = same mechanism as tremolo but L direction=1, R direction=0"
  - "Three-way mutual exclusion via cascading atomic checks in processBlock"

requirements-completed: [PAN-01, PAN-02, PAN-03, PAN-04, PAN-05]

# Metrics
duration: 9min
completed: 2026-05-24
---

# Phase 45 Plan 01: Auto-Pan DSP Activation Summary

**Opposition-phase L/R retrigger sweep producing stereo movement with linear crossfade dip and three-way mutual exclusion**

## Performance

- **Duration:** 9 min
- **Started:** 2026-05-24T20:38:20Z
- **Completed:** 2026-05-24T20:47:17Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Auto-pan activation block configures L sweep direction=1 (decrease) and R sweep direction=0 (increase) for opposition-phase stereo movement
- Mode is always 0 (linear) per PAN-04 -- no curve selection parameter, PS1-faithful linear crossfade
- Depth scaling limits pan excursion: at depth=0.5, sound stays closer to center; at depth=1.0, full L-to-R
- L/R ratio provides asymmetric rates via shift offset (same octave-based logic as tremolo)
- Three-way mutual exclusion: VCA ramp ARM gated by both tremolo AND auto-pan; tremolo block runs at priority; auto-pan skipped when tremolo active
- Parameter change detection re-configures sweeps mid-pan when speed/ratio change
- Two C-core integration tests prove opposition behavior and linear crossfade center dip
- All 28 sweep tests pass, 57 voice_tick tests pass, full project builds clean

## Task Commits

Each task was committed atomically:

1. **Task 1: Auto-pan atomics, activation logic, and mutual exclusion** - `6c68f29` (feat)
2. **Task 2: Integration test proving opposition-phase and linear crossfade dip** - `691bc78` (test)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - Added 4 auto-pan atomics + 3 audio-thread-local state vars + getter methods
- `src/plugin/PluginProcessor.cpp` - Added auto-pan activation block (opposition-phase config, parameter change detection, deactivation) + depth scaling + three-way mutual exclusion guard on VCA ramp
- `tests/unit/voice/test_sweep.c` - 2 new auto-pan tests (28 total): opposition + linear_crossfade_dip

## Decisions Made
- Auto-pan defaults to 2.0 Hz speed (slower than tremolo's 5.0 Hz) because panning movement is more perceptually obvious than amplitude modulation
- No tremoloCurve equivalent for auto-pan -- the linear crossfade dip IS the PS1 character; equal-power would be un-faithful
- Tremolo wins when both effects somehow enabled simultaneously (defensive priority) rather than auto-pan overriding

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 45-02 (GUI controls for auto-pan) can wire to getAutoPanEnabled/SpeedHz/Depth/Ratio
- Phase 51 (GUI Integration) can connect host automation parameters to these atomics
- Manual verification: launch standalone, load sample, set autoPanEnabled=true -> hear stereo sweep

## Self-Check: PASSED

---
*Phase: 45-auto-pan*
*Completed: 2026-05-24*
