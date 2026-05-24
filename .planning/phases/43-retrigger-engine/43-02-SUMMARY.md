---
phase: 43-retrigger-engine
plan: 02
subsystem: dsp
tags: [sweep, retrigger, polyrhythmic, kon-reset, mixer-api, rt-safe]

# Dependency graph
requires:
  - phase: 43-retrigger-engine
    plan: 01
    provides: "retrigger_enable field and auto-reverse logic in spu94_sweep_tick"
provides:
  - "start_direction field in spu94_sweep_t for deterministic KON reset"
  - "Independent L/R retrigger rate configuration via mixer API"
  - "Polyrhythmic volume modulation capability (different shift per channel)"
  - "5 new retrigger unit tests proving L/R independence and KON behavior"
affects: [44-tremolo, 45-auto-pan, 48-am-synthesis, 49-phase-modulator]

# Tech tracking
tech-stack:
  added: []
  patterns: ["start_direction as configure-time snapshot for reset reference", "retrigger_enable forwarded through mixer API to per-channel sweep configure"]

key-files:
  created: []
  modified:
    - include/spu94/spu94_sweep.h
    - src/spu94/spu94_sweep.c
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c
    - tests/unit/voice/test_voice_tick.c

key-decisions:
  - "start_direction stored at configure time (same value as direction parameter) for deterministic reset"
  - "Mixer API extended with retrigger_enable as final param; all v1.9 callers pass 0"
  - "KON reset verified clean via existing voice_init path (memset zeros all sweep fields including retrigger_enable and start_direction)"
  - "Test shift values use 2 and 4 (not 11/13) to produce measurable reversals within 500 ticks"

patterns-established:
  - "API extension pattern: new params appended to function signatures; existing callers pass safe defaults"
  - "Polyrhythmic via independent shift per channel: no new DSP, just separate configuration"

requirements-completed: [RTR-02, RTR-05]

# Metrics
duration: 18min
completed: 2026-05-24
---

# Phase 43 Plan 02: Independent L/R Rates + KON Reset Summary

**Polyrhythmic volume modulation via independent retrigger_enable per channel in mixer API, with start_direction field for deterministic KON reset**

## Performance

- **Duration:** 18 min
- **Started:** 2026-05-24T17:15:12Z
- **Completed:** 2026-05-24T17:34:10Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Added start_direction field to spu94_sweep_t -- records initial direction at configure time for reset reference
- Updated spu94_voice_mixer_set_sweep_l/r signatures to accept retrigger_enable parameter, enabling per-channel oscillation control
- All existing callers (plugin, test_voice_tick) updated to pass retrigger_enable=0 preserving v1.9 one-shot behavior
- 5 new tests prove: independent L/R reversal rates, KON deactivation, start_direction storage, mixer API forwarding, polyrhythmic divergence
- All 24 sweep tests pass, full project builds with zero warnings

## TDD Gate Compliance

- RED gate: `bfb7de4` (test commit -- 5 tests fail on missing start_direction and wrong mixer API param count)
- GREEN gate: `882aabe` (feat commit -- all 24 tests pass, full build clean)
- REFACTOR gate: not needed (changes are 5 added lines of struct/logic, already minimal)

## Task Commits

Each task was committed atomically:

1. **Task 1: Write failing tests for independent L/R rates and KON reset** - `bfb7de4` (test)
2. **Task 2: Implement start_direction, mixer retrigger API, and KON reset** - `882aabe` (feat)

## Files Created/Modified
- `include/spu94/spu94_sweep.h` - Added start_direction field to spu94_sweep_t
- `src/spu94/spu94_sweep.c` - spu94_sweep_configure stores direction as start_direction
- `include/spu94/spu94_voice.h` - Updated set_sweep_l/r declarations with retrigger_enable param
- `src/spu94/spu94_voice.c` - Updated set_sweep_l/r implementations to forward retrigger_enable
- `src/plugin/PluginProcessor.cpp` - Updated callsite to pass retrigger_enable=0
- `tests/unit/voice/test_sweep.c` - 5 new retrigger tests (24 total)
- `tests/unit/voice/test_voice_tick.c` - Updated callsite to pass retrigger_enable=0

## Decisions Made
- start_direction is the direction value captured at configure time -- simple uint8_t, zero cost
- KON path already provides clean sweep state through existing voice_init (memset zeros everything) -- no additional code needed in key_on
- Test values chosen as shift=2/shift=4 to produce measurable divergence in 500 ticks (shift=11/13 are too slow for the counter-accumulate math at those rates)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Test shift values too slow for 500-tick window**
- **Found during:** Task 2 verification
- **Issue:** Plan specified shift=11/step=0 and shift=13/step=0 for the independent L/R test. At these rates, the counter-accumulate math produces a step of only 7 per tick (shift_amt for shift=11 means step_shift=0). A half-cycle takes ~4681 ticks -- far more than the 500-tick test window. Neither channel produced any reversals.
- **Fix:** Changed test to use shift=2 (step 3584/tick) and shift=4 (step 896/tick), which produce multiple reversals within 500 ticks and clearly demonstrate rate independence.
- **Files modified:** tests/unit/voice/test_sweep.c
- **Commit:** 882aabe

**2. [Rule 3 - Blocking] Additional callsite in test_voice_tick.c**
- **Found during:** Task 2 build
- **Issue:** test_voice_tick.c line 2103 called spu94_voice_mixer_set_sweep_l with old 7-arg signature, causing compile error.
- **Fix:** Added retrigger_enable=0 as 8th argument to preserve existing behavior.
- **Files modified:** tests/unit/voice/test_voice_tick.c
- **Commit:** 882aabe

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 43 (Retrigger Engine) is now COMPLETE (plans 01 + 02)
- Phase 44 (Tremolo) can call spu94_voice_mixer_set_sweep_l/r with retrigger_enable=1 and independent L/R shift values
- Phase 45 (Auto-Pan) can configure L=increase + R=decrease with retrigger for stereo ping-pong
- Phase 48 (AM Synthesis) can use audio-rate shift values for amplitude modulation via retrigger

## Self-Check: PASSED

---
*Phase: 43-retrigger-engine*
*Completed: 2026-05-24*
