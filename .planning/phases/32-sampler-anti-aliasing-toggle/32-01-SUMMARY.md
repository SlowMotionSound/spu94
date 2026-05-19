---
phase: 32-sampler-anti-aliasing-toggle
plan: 01
subsystem: dsp, ui
tags: [gaussian-interpolation, zero-order-hold, anti-aliasing, sampler, voice-engine, juce]

# Dependency graph
requires:
  - phase: 30-24-voice-polyphony-mixer
    provides: spu94_voice_mixer_t with 24-voice tick, Gaussian interpolation in spu94_voice_tick
provides:
  - gauss_bypass field on spu94_voice_mixer_t (global toggle for all 24 voices)
  - Zero-order hold path in spu94_voice_tick STEP 2 (AA-01)
  - Anti-Alias toggle in Sampler window (AA-03)
  - Two unit tests proving behavioral difference and correct default
affects: [sampler-features, voice-engine-extensions, creative-effects]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "gauss_bypass parameter passed through spu94_voice_tick for per-tick interpolation mode selection"
    - "Inverted boolean bridge: AA ON = gauss_bypass 0 (Gauss), AA OFF = gauss_bypass 1 (ZOH)"

key-files:
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - tests/unit/voice/test_voice_tick.c
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp

key-decisions:
  - "gauss_bypass as a parameter to spu94_voice_tick rather than reading from mixer directly -- cleaner function signature, explicit dependency"
  - "ZOH outputs s3 (newest sample in ring) -- this is the correct zero-order hold behavior matching what the SPU would produce without its Gaussian smoothing"
  - "Toggle placed below Loop toggle at (300, 42) in sampler window -- avoids overlap with voiceSampleLabel and groups with other mode controls"

patterns-established:
  - "Interpolation mode bypass via function parameter: gauss_bypass=0 for faithful, gauss_bypass=1 for creative"

requirements-completed: [AA-01, AA-02, AA-03]

# Metrics
duration: 9m 21s
completed: 2026-05-19
---

# Phase 32 Plan 01: Anti-Aliasing Toggle Summary

**Global Gaussian interpolation bypass toggle for the 24-voice sampler engine -- switches between PS1-faithful 4-tap Gauss smoothing (default ON) and raw zero-order-hold aliasing (OFF)**

## Performance

- **Duration:** 9m 21s
- **Started:** 2026-05-19T17:50:11Z
- **Completed:** 2026-05-19T17:59:32Z
- **Tasks:** 2
- **Files modified:** 7

## Accomplishments
- Added `gauss_bypass` field to `spu94_voice_mixer_t` and `gauss_bypass` parameter to `spu94_voice_tick` -- zero-order hold path outputs newest ring buffer sample directly, skipping the 4-tap Gaussian table lookup
- Two new unit tests: `test_gauss_bypass_zoh_differs_from_gauss` (proves output differs at pitch 0x1800 between Gauss and ZOH) and `test_gauss_bypass_default_off` (confirms gauss_bypass=0 after mixer init)
- Anti-Alias toggle in Sampler window wired through `samplerAAEnabled` atomic to `gauss_bypass` via inverted mapping in processBlock
- All 35 unit tests pass (33 existing + 2 new), Release standalone builds cleanly

## Task Commits

Each task was committed atomically:

1. **Task 1: C core -- gauss_bypass field, ZOH path in voice tick, unit test** - `e52aed9` (feat)
2. **Task 2: GUI -- Anti-Aliasing toggle in Sampler window, wired through processBlock** - `18c5956` (feat)

## Files Created/Modified
- `include/spu94/spu94_voice.h` - Added `gauss_bypass` field to mixer struct, `gauss_bypass` param to voice tick declaration
- `src/spu94/spu94_voice.c` - Conditional ZOH/Gauss branch in STEP 2, mixer tick passes `m->gauss_bypass` to each voice
- `tests/unit/voice/test_voice_tick.c` - Two new gauss bypass tests, updated all 33 existing call sites with `gauss_bypass=0`
- `src/plugin/PluginProcessor.h` - `samplerAAEnabled` atomic + `getSamplerAAEnabled()` accessor
- `src/plugin/PluginProcessor.cpp` - processBlock pushes inverted atomic to `gauss_bypass` on voice mixer
- `src/plugin/PluginEditor.h` - `samplerAAToggle` button declaration
- `src/plugin/PluginEditor.cpp` - Toggle setup (default ON), onClick wiring, layout at (300, 42)

## Decisions Made
None beyond plan specification -- followed plan exactly as written.

## Deviations from Plan
None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Anti-aliasing toggle is complete and functional
- All voice engine features (ADPCM decode, Gaussian interp, ADSR, loop, 24-voice mixer, and now AA bypass) are in place
- Ready for Phase 31 (Standalone Testbed UX) or further v1.8 work

## Self-Check: PASSED

All files exist, both commits verified, all key content present in modified files.

---
*Phase: 32-sampler-anti-aliasing-toggle*
*Completed: 2026-05-19*
