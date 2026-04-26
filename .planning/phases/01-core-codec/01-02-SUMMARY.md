---
phase: 01-core-codec
plan: 02
subsystem: codec
tags: [adpcm, encoder, fixed-point, ps1-spu, c99, brute-force]

# Dependency graph
requires:
  - "01-01: spu94_adpcm_decode_block, filter tables, adpcm_state struct"
provides:
  - "spu94_adpcm_encode_block() -- brute-force optimal ADPCM encoder with internal decoder"
  - "12 encoder unit tests covering round-trip, invariants, and edge cases"
affects: [02-integration, 04-verification]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Encoder embeds internal decoder copy for reconstructed-sample prediction state"
    - "Brute-force search over 65 (filter, shift) combinations with int64 L2 error metric"
    - "Round-to-nearest nibble quantization with shift=12 UB guard (half_step=0 when shift_amount=0)"

key-files:
  created:
    - src/spu94/spu94_adpcm_encode.c
    - tests/unit/adpcm/test_adpcm_encode.c
  modified:
    - include/spu94/spu94_adpcm.h
    - src/spu94/CMakeLists.txt
    - tests/unit/adpcm/CMakeLists.txt

key-decisions:
  - "Tiebreak by iteration order (strict <): lower filter index wins first, then lower shift"

patterns-established:
  - "Encoder-decoder state consistency verification pattern: encode block, decode same block from matching initial state, assert states match"

requirements-completed: [ADPCM-04, ADPCM-05, ADPCM-06, ADPCM-07]

# Metrics
duration: 16min
completed: 2026-04-26
---

# Phase 1 Plan 02: ADPCM Encoder Summary

**Brute-force optimal PS1 SPU ADPCM encoder with int64 L2 error metric, reconstructed-sample prediction, and 12-test verification suite**

## Performance

- **Duration:** 16 min
- **Started:** 2026-04-26T21:14:26Z
- **Completed:** 2026-04-26T21:30:43Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- ADPCM encoder with brute-force search over 65 (filter, shift) combinations
- Internal decoder ensures prediction state uses reconstructed samples (not original PCM)
- int64 error accumulation prevents overflow (28 * 32767^2 exceeds int32)
- Round-to-nearest nibble quantization with shift=12 UB guard
- Deterministic tiebreak via strict < and iteration order (lower filter, lower shift)
- 12 unit tests: silence round-trip, deterministic round-trip, state consistency, valid shift/filter ranges, flag passthrough, reconstructed-state invariant, shift=12 guard, signal-adaptive shift selection, multi-block state carry, nibble range verification
- Zero heap, integer-only, no float/double, no spu94_state dependency

## Task Commits

Each task was committed atomically:

1. **Task 1: Implement ADPCM encoder with internal decoder and brute-force search** - `d91707a` (feat)
2. **Task 2: Create encoder unit tests -- round-trip, invariants, edge cases** - `057e242` (test)

## Files Created/Modified
- `src/spu94/spu94_adpcm_encode.c` - Encoder implementation: brute-force search, internal decoder, L2 error metric
- `tests/unit/adpcm/test_adpcm_encode.c` - 12 unit tests covering round-trip, invariants, edge cases
- `include/spu94/spu94_adpcm.h` - Added spu94_adpcm_encode_block declaration
- `src/spu94/CMakeLists.txt` - Added spu94_adpcm_encode.c to spu94_obj OBJECT library
- `tests/unit/adpcm/CMakeLists.txt` - Added test_adpcm_encode target and adpcm_encode_unit test

## Decisions Made
- Tiebreak strategy: strict less-than comparison with outer loop over filters, inner loop over shifts naturally selects lowest filter then lowest shift on equal error

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
None.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Codec pair complete: encode + decode are symmetric and state-consistent
- Ready for Phase 2 integration into reverb signal path
- All decoder tests from Plan 01 continue to pass (19/19)
- All encoder tests pass (12/12)

## Self-Check: PASSED

- All 2 created files exist on disk
- Both task commits (d91707a, 057e242) found in git log
- 12/12 encoder tests pass, 19/19 decoder tests pass
- No /64, no float/double, no malloc/calloc/free in spu94_adpcm_encode.c
- No trial.old = in[i] bug present

---
*Phase: 01-core-codec*
*Completed: 2026-04-26*
