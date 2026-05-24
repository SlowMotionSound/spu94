---
phase: 48-am-synthesis
plan: 01
subsystem: dsp
tags: [am-synthesis, sweep, retrigger, audio-rate, sidebands, metallic]

# Dependency graph
requires:
  - phase: 44-tremolo
    plan: 01
    provides: "Tremolo activation pattern (retrigger_enable=1 sweep configuration)"
  - phase: 43-retrigger-engine
    plan: 02
    provides: "retrigger_enable in mixer API, independent L/R rates"
provides:
  - "AM synthesis activation logic in processBlock (audio-rate retrigger_enable=1 sweep configuration)"
  - "Audio-rate Hz-to-shift conversion table for metallic sidebands (21.5-9647 Hz across shifts 0-8)"
  - "Depth scaling mechanism (same formula as tremolo: vol = 0x7FFF - (0x7FFF - sweep_level) * depth)"
  - "Mutual exclusion: tremolo and auto-pan have priority over AM"
  - "1 integration test proving full-cycle audio-rate oscillation at ~639 Hz"
affects: [48-02, 51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["audio-rate sweep reuse for AM synthesis (same C-core mechanism, different shift range)"]

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c

key-decisions:
  - "AM Hz table uses shifts 0-8 (all 36 entries) covering full range the hardware supports at audio rates"
  - "No L/R ratio for AM -- both channels always run at the same rate (symmetric sidebands)"
  - "AM has lowest priority: tremolo wins, auto-pan wins, VCA ramp blocked, sidechain duck blocked on voice 0"
  - "AM starts in direction=1 (decrease from current vol) matching tremolo convention"

patterns-established:
  - "AM IS tremolo at higher rates: identical code pattern, different Hz table, same retrigger mechanism"
  - "Mutual exclusion hierarchy: tremolo > auto-pan > AM > VCA ramp > sidechain duck (on voice 0)"

requirements-completed: [AM-01, AM-02, AM-03, AM-04]

# Metrics
duration: 7min
completed: 2026-05-24
---

# Phase 48 Plan 01: AM Synthesis DSP Activation Summary

**Audio-rate Hz lookup table (shifts 0-8) with processBlock activation block, depth scaling, and mutual exclusion hierarchy**

## Performance

- **Duration:** 7 min
- **Started:** 2026-05-24T22:54:08Z
- **Completed:** 2026-05-24T23:01:23Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Implemented hzToShiftAm lookup table covering 36 entries (shift 0-8, step 0-3) mapping 21.5-9647 Hz to SPU shift/step pairs
- AM activation in processBlock configures both L/R sweeps identically with retrigger_enable=1 for audio-rate oscillation
- Depth scaling limits modulation range proportionally (at depth=0, no modulation; at depth=1.0, full metallic character)
- Mutual exclusion: tremolo and auto-pan both take priority over AM; AM also blocks VCA ramp and sidechain duck on voice 0
- Parameter change detection re-configures sweeps without restart when rate/curve change mid-AM
- One C-core integration test proves full-range audio-rate oscillation at shift=4 (~639 Hz)
- All 34 sweep tests pass, full project builds clean (all plugin targets + tests)

## Task Commits

Each task was committed atomically:

1. **Task 1: Audio-rate Hz table, AM activation block, and mutual exclusion** - `090591d` (feat)
2. **Task 2: AM audio-rate oscillation integration test** - `c2d35cc` (test)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - Added 4 AM atomics (amEnabled, amRateHz, amDepth, amCurve) + 3 audio-thread-local state vars + getter methods
- `src/plugin/PluginProcessor.cpp` - Added hzToShiftAm helper (36-entry table + log-distance lookup) + AM activation block + depth scaling + mutual exclusion in VCA ramp/tremolo/auto-pan/sidechain duck blocks
- `tests/unit/voice/test_sweep.c` - 1 new AM test (34 total): audio-rate oscillation at ~639 Hz

## Decisions Made
- AM Hz table derived from the same verified counter-accumulate math as Phase 44 tremolo table
- Table range (21.5-9647 Hz) is wider than plan's estimate (37-7350 Hz) because the actual math produces those values -- plan estimates were approximations
- Test asserts 69-tick cycle (not 72 as plan estimated) because decrease half (step=(8-0)<<7=1024) is faster than increase half (step=(7-0)<<7=896)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's Hz range estimates were approximate**
- **Found during:** Task 1 (table computation)
- **Issue:** Plan stated "approximately 37 Hz (shift 8, step 3) up to approximately 7350 Hz (shift 0, step 0)." Actual counter-accumulate math gives 21.5 Hz to 9647 Hz for shifts 0-8 steps 0-3.
- **Fix:** Used the verified math (same formula proven correct in Phase 44). The table covers the full range; hzToShiftAm clamps to boundaries.
- **Files modified:** src/plugin/PluginProcessor.cpp
- **Commit:** 090591d

**2. [Rule 1 - Bug] Plan's cycle length estimate was incorrect for test**
- **Found during:** Task 2 (test implementation)
- **Issue:** Plan estimated 72 ticks for shift=4/step=0 cycle (used symmetric 36+36). Actual cycle is 69 ticks because decrease uses (8-step) magnitude (1024/tick = 32 ticks) while increase uses (7-step) magnitude (896/tick = 37 ticks).
- **Fix:** Adjusted test tolerance to expect 69 ticks +/- 2 instead of 72 +/- 2.
- **Files modified:** tests/unit/voice/test_sweep.c
- **Commit:** c2d35cc

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 48-02 (GUI controls for AM) can wire to getAmEnabled/AmRateHz/AmDepth/AmCurve
- Phase 51 (GUI Integration) can connect host automation parameters to these atomics
- Manual verification: launch standalone, load sample, set amEnabled=true with amRateHz=440 -> hear metallic sidebands

## Self-Check: PASSED

---
*Phase: 48-am-synthesis*
*Completed: 2026-05-24*
