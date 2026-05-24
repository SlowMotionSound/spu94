---
phase: 49-phase-modulator
plan: 01
subsystem: dsp
tags: [phase-modulator, sweep, zero-crossing, polarity-inversion, depth-formula, retrigger]

# Dependency graph
requires:
  - phase: 43-retrigger-engine
    plan: 02
    provides: "retrigger_enable in mixer API"
  - phase: 44-tremolo
    plan: 01
    provides: "hzToShift table and activation pattern"
  - phase: 48-am-synthesis
    plan: 01
    provides: "AM activation block as template for phase mod"
provides:
  - "Phase modulator activation logic in processBlock (phase=1, retrigger_enable=1)"
  - "Zero-crossing depth formula: vol = 0x7FFF + (sweep_level * 2 * depth)"
  - "Mutual exclusion with tremolo/auto-pan/AM/VCA ramp/sidechain duck"
  - "ADR-0060 documenting zero-crossing behavior and linear-mode-only decision"
  - "Integration test proving polarity cycling and depth formula correctness"
affects: [49-02, 51-gui-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: ["negative-phase sweep oscillation for polarity inversion", "zero-crossing depth formula mapping sweep domain to full volume domain"]

key-files:
  created:
    - docs/adr/ADR-0060-phase-modulator-zero-crossing.md
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c

key-decisions:
  - "Linear mode only (mode=0) -- exponential ignores phase bit per ADR-0059, making it unsuitable for polarity cycling"
  - "Depth formula uses sweep_level * 2 * depth (not raw mapping) to achieve full -0x7FFF..+0x7FFF range at depth=1.0"
  - "Phase mod has lowest priority in mutual exclusion hierarchy (tremolo > auto-pan > AM > phase mod)"
  - "Both L and R channels oscillate together (no ratio) for prototype -- stereo phase offset is a future enhancement"
  - "Zero-crossing is smooth (no click) because linear ramp passes through zero over many ticks"

patterns-established:
  - "Phase mod IS the sweep: phase=1 + retrigger=1 configuration from host, same pattern as tremolo/AM"
  - "Negative-phase sweep oscillates 0 to -0x7FFF; depth formula projects this into +0x7FFF to -0x7FFF"

requirements-completed: [PMOD-01, PMOD-02, PMOD-03, PMOD-04]

# Metrics
duration: 6min
completed: 2026-05-24
---

# Phase 49 Plan 01: Phase Modulator DSP Activation Summary

**Polarity-cycling sweep via phase=1 retrigger with zero-crossing depth formula (linear mode only, ADR-0060)**

## Performance

- **Duration:** 6 min
- **Started:** 2026-05-24T23:16:27Z
- **Completed:** 2026-05-24T23:22:25Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- Phase modulator atomics added to PluginProcessor.h (phaseModEnabled, phaseModSpeedHz default 4.0 Hz, phaseModDepth default 1.0)
- Activation block in processBlock configures sweep with phase=1, direction=0, mode=0, retrigger_enable=1
- Zero-crossing depth formula: vol = 0x7FFF + (sweep_level * 2 * depth) -- crosses zero at depth > 0.5
- Sweep level explicitly starts at 0 on enable (oscillates 0 to -0x7FFF)
- Mutual exclusion: phase mod disabled when tremolo, auto-pan, or AM is active (lowest priority)
- VCA ramp and sidechain duck exclusion updated to include phase mod
- Force-deactivate logic added in tremolo/auto-pan/AM enable paths
- T-49-01 mitigation: depth clamped 0.0-1.0, Hz clamped to table range at point of use
- T-49-02 mitigation: formula result clamped to int16_t range before assignment
- Integration test proves: sweep goes negative, reaches -0x7FFF, retriggers back to 0, depth formula crosses zero at depth=1.0, stays positive at depth=0.5
- ADR-0060 documents zero-crossing is smooth (continuous linear ramp), exponential unsuitable, no click expected
- All 35 sweep tests pass, full project builds clean

## Task Commits

Each task was committed atomically:

1. **Task 1: Phase modulator activation logic and zero-crossing depth formula** - `8611f80` (feat)
2. **Task 2: Phase modulator integration test and zero-crossing ADR** - `90acf25` (test)

## Files Created/Modified
- `src/plugin/PluginProcessor.h` - Added 3 phase mod atomics + 2 audio-thread-local state vars + getter methods
- `src/plugin/PluginProcessor.cpp` - Added phase mod activation block + depth formula + mutual exclusion updates in VCA ramp, tremolo, auto-pan, AM, and sidechain duck
- `tests/unit/voice/test_sweep.c` - 1 new test (35 total): phase_mod_polarity_cycling with 6-part verification
- `docs/adr/ADR-0060-phase-modulator-zero-crossing.md` - Zero-crossing behavior analysis and linear-mode-only decision

## Decisions Made
- Linear mode only: exponential mode's "phase bit ignored on decrease" exception (ADR-0059) makes it unsuitable
- Both channels oscillate in unison (no L/R ratio parameter for prototype)
- Zero-crossing is labeled ready/non-experimental given smooth continuous ramp behavior
- Default speed 4.0 Hz puts the control in the "hollow character" sweet spot on enable

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Plan 49-02 (GUI controls for phase modulator) can wire to getPhaseModEnabled/SpeedHz/Depth
- Phase 51 (GUI Integration) can connect host automation parameters to these atomics
- Manual verification: launch standalone, load sample, set phaseModEnabled=true -> hear hollow polarity-cycling

## Self-Check: PASSED
