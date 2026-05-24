---
phase: 35-pitch-modulation-pmon
plan: 02
subsystem: voice-engine
tags: [pmon, pitch-modulation, adr, outx, integration-test]

# Dependency graph
requires:
  - phase: 35-pitch-modulation-pmon
    plan: 01
    provides: PMON formula in mixer tick, pmon_flags field, outx capture at Step 2.75
  - phase: 34-signed-volume
    provides: int16_t outx field on spu94_voice_t (post-ADSR, pre-volume)
provides:
  - ADR-0057 documenting VxOUTX capture point decision for PMON
  - ADR-0057 documenting PMON pitch clamp 0x4000 behavior
  - Integration test proving modulator ADSR shapes FM depth over time
affects: [36-noise-generator, 37-volume-sweep]

# Tech tracking
tech-stack:
  added: []
  patterns: [slow-ADSR-ramp as PMON depth proof, per-tick pitch_counter delta measurement]

key-files:
  created: []
  modified:
    - docs/DECISIONS.md
    - tests/unit/voice/test_voice_tick.c

key-decisions:
  - "VxOUTX captured post-ADSR pre-volume (Step 2.75), confirmed by DuckStation behavioral witness"
  - "PMON pitch clamp is 0x4000 (not 0x3FFF), per spec: FM modulation can exceed base pitch maximum"

requirements-completed: [PMON-02, PMON-07]

# Metrics
duration: 3min
completed: 2026-05-22
---

# Phase 35 Plan 02: PMON Documentation and Integration Test Summary

**ADR-0057 documents VxOUTX post-ADSR capture point and 0x4000 pitch clamp; integration test proves modulator ADSR ramp controls FM depth**

## Performance

- **Duration:** 3 min
- **Started:** 2026-05-22T19:09:28Z
- **Completed:** 2026-05-22T19:12:19Z
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- ADR-0057 written in docs/DECISIONS.md: documents VxOUTX capture at post-ADSR pre-volume (Step 2.75), the three alternative capture points considered, DuckStation as behavioral witness, and the 0x4000 pitch clamp (distinct from 0x3FFF base-pitch cap)
- Integration test `test_pmon_adsr_shapes_modulation_depth` added: voice 0 (modulator) with slow ADSR attack ramp, voice 1 (carrier) with PMON enabled, proves late-tick effective pitch exceeds early-tick effective pitch as modulator ADSR ramps up
- All 46 voice_tick unit tests pass (39 original + 6 PMON from Plan 01 + 1 integration from Plan 02), zero regressions

## Task Commits

Each task was committed atomically:

1. **Task 1: ADR-0057 for VxOUTX capture point and PMON pitch clamp** - `fd741ed` (docs)
2. **Task 2: Integration test -- modulator ADSR shapes FM depth** - `b39548e` (test)

## Files Created/Modified
- `docs/DECISIONS.md` - ADR-0057 prepended: VxOUTX capture point decision (post-ADSR pre-volume), PMON 0x4000 clamp documentation, three alternative capture points, DuckStation citation
- `tests/unit/voice/test_voice_tick.c` - 1 new integration test: test_pmon_adsr_shapes_modulation_depth (slow ADSR modulator ramp proving FM depth increases over time)

## Decisions Made
- **VxOUTX capture point formalized in ADR-0057:** Post-ADSR, pre-volume (Step 2.75 in voice_tick). DuckStation's `voice.last_volume = ApplyVolume(sample, voice.regs.adsr_volume)` is the behavioral witness. Confidence: HIGH.
- **PMON pitch clamp documented in ADR-0057:** 0x4000 (not 0x3FFF). The nocash spec explicitly states `if Step > 3FFFh then Step = 4000h`. FM modulation can push pitch one increment beyond the normal register maximum.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- CMake build required `SPU94_BUILD_GUI=OFF` in worktree context (JUCE FetchContent fails in parallel worktree). This is expected and does not affect test correctness.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Phase 35 PMON is fully documented and tested: ADR-0057 + 7 regression tests + 1 integration test
- Ready for Phase 36 (Noise Generator): noise voice outx can feed PMON factor (spec-orthogonal interaction)
- Ready for Phase 37 (Volume Sweep): PMON and sweep are independent systems, no interaction
- No blockers

## Self-Check: PASSED

All files exist, all commits verified, 46/46 tests pass.

---
*Phase: 35-pitch-modulation-pmon*
*Completed: 2026-05-22*
