---
phase: 44-tremolo
plan: 01
subsystem: dsp
tags: [tremolo, sweep, retrigger, vca-oscillation, depth-scaling, hz-conversion]

# Dependency graph
requires:
  - phase: 43-retrigger-engine
    plan: 02
    provides: "retrigger_enable in mixer API, independent L/R rates"
provides:
  - "Tremolo activation logic in processBlock (retrigger_enable=1 sweep configuration)"
  - "Hz-to-shift conversion table for musically useful tremolo speeds (0.67-43 Hz)"
  - "Depth scaling mechanism (vol = 0x7FFF - (0x7FFF - sweep_level) * depth)"
  - "L/R ratio for independent channel speeds via shift offset"
  - "Tremolo/VCA ramp mutual exclusion guard"
  - "2 integration tests proving full-range oscillation and exponential asymmetry"
affects: [44-02, 51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["host-layer post-multiply depth scaling over C-core sweep oscillation", "log-distance Hz lookup for nearest shift/step pair"]

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c

key-decisions:
  - "Hz-to-shift table uses step=0..3 across shifts 7-13 to cover 0.67-43 Hz with musical granularity"
  - "Depth scaling is a host-layer post-multiply: sweep oscillates full 0-0x7FFF range, host reduces effective modulation"
  - "L/R ratio implemented as shift+floor(log2(ratio)) offset, giving octave-based rate differences"
  - "Tremolo starts in direction=1 (decrease from current vol) for immediate audible effect"
  - "Exponential asymmetry is slow-fade-out + snappy-attack (PS1 hardware characteristic)"

patterns-established:
  - "Tremolo IS the sweep: no new C modules, just retrigger_enable=1 configuration from host"
  - "Atomic enable + audio-thread-local was-active flag for edge detection"

requirements-completed: [TREM-01, TREM-02, TREM-03, TREM-04, TREM-05]

# Metrics
duration: 11min
completed: 2026-05-24
---

# Phase 44 Plan 01: Tremolo DSP Activation Summary

**Hz-to-shift conversion + depth-scaled retrigger oscillation configured from processBlock with L/R ratio and mutual exclusion guard**

## Performance

- **Duration:** 11 min
- **Started:** 2026-05-24T18:36:37Z
- **Completed:** 2026-05-24T18:47:56Z
- **Tasks:** 2
- **Files modified:** 3

## Accomplishments
- Implemented hzToShift lookup table covering 25 entries (shift 7-13, step 0-3) mapping 0.67-43 Hz to SPU shift/step pairs
- Tremolo activation in processBlock configures both L/R sweeps with retrigger_enable=1 for continuous oscillation
- Depth scaling limits modulation range proportionally: at depth=0.5, volume only oscillates over half the full range
- L/R ratio provides independent channel speeds via logarithmic shift offset (1.0=same, 2.0=R one octave slower)
- VCA ramp arm is suppressed when tremolo is active (mutual exclusion)
- Parameter change detection re-configures sweeps without restart when speed/curve/ratio change mid-tremolo
- Two C-core integration tests prove full-range oscillation and exponential asymmetry behavior
- All 26 sweep tests pass, 57 voice_tick tests pass, full project builds clean

## Task Commits

Each task was committed atomically:

1. **Task 1: Hz-to-shift conversion and tremolo activation logic** - `107e826` (feat)
2. **Task 2: Tremolo integration test** - `f37d295` (test)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - Added 5 tremolo atomics + 4 audio-thread-local state vars + getter methods
- `src/plugin/PluginProcessor.cpp` - Added hzToShift helper (25-entry table + log-distance lookup) + tremolo activation block + depth scaling + VCA ramp mutual exclusion guard
- `tests/unit/voice/test_sweep.c` - 2 new tremolo tests (26 total): full_oscillation + exponential_asymmetry

## Decisions Made
- Hz table derived from actual counter-accumulate math (verified against spu94_envelope_step.c) rather than plan's theoretical values which had an off-by-factor error
- Exponential asymmetry is "decrease slower, increase faster" (not the reverse as plan assumed) -- this matches real PS1 hardware: exponential decrease shrinks steps as level drops, producing a slow fade-out tail
- Table covers 25 entries spanning the full musically useful tremolo range with step variation for finer granularity between adjacent shift values

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Plan's Hz-to-shift expected values were incorrect**
- **Found during:** Task 1 implementation
- **Issue:** Plan stated hzToShift(0.5) returns shift~23, hzToShift(5.0) returns shift~17-18, hzToShift(19.0) returns shift~14-15. Actual counter-accumulate math gives shift=13 for 0.67 Hz, shift=10 for 5.4 Hz, shift=9 for 18.8 Hz. The plan's formula failed to account for step_magnitude scaling when shift < 11.
- **Fix:** Built table from verified math. The function behavior is correct (finds nearest Hz match); only the expected shift numbers in the plan were wrong.
- **Files modified:** src/plugin/PluginProcessor.cpp
- **Commit:** 107e826

**2. [Rule 1 - Bug] Plan's exponential asymmetry assertion was inverted**
- **Found during:** Task 2 test execution
- **Issue:** Plan expected "decrease half-cycle took FEWER ticks than increase." Reality: exponential decrease takes MORE ticks because step magnitude = step * level / 0x8000 shrinks toward zero as level drops (slow tail). Increase is linear-fast.
- **Fix:** Inverted the assertion to match actual PS1 hardware behavior: decrease_ticks > increase_ticks. This is the correct Uni-Vibe character (slow fade-out, snappy attack).
- **Files modified:** tests/unit/voice/test_sweep.c
- **Commit:** f37d295

## Issues Encountered
None beyond the auto-fixed deviations above.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 44-02 (GUI controls for tremolo) can wire to getTremoloEnabled/SpeedHz/Depth/Curve/Ratio
- Phase 51 (GUI Integration) can connect host automation parameters to these atomics
- Manual verification: launch standalone, load sample, set tremoloEnabled=true -> hear pulsing

## Self-Check: PASSED

---
*Phase: 44-tremolo*
*Completed: 2026-05-24*
