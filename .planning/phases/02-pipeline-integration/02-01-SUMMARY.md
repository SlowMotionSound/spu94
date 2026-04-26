---
phase: 02-pipeline-integration
plan: 01
subsystem: dsp
tags: [adpcm, pipeline, double-buffer, latency, c99, real-time]

# Dependency graph
requires:
  - phase: 01-core-codec
    provides: spu94_adpcm_encode_block + spu94_adpcm_decode_block standalone codec API
provides:
  - ADPCM encode+decode wired into spu94_process as toggleable upstream stage
  - Public API: spu94_set_adpcm_enabled, spu94_get_adpcm_enabled, spu94_get_total_latency_samples
  - Double-buffer state in spu94_state with 28-sample block latency when enabled
affects: [02-pipeline-integration, 03-io-surface, 04-verification]

# Tech tracking
tech-stack:
  added: []
  patterns: [double-buffer block accumulation in sample-at-a-time pipeline, state-dependent latency reporting, partial-buffer discard on toggle]

key-files:
  created: []
  modified:
    - include/spu94/spu94.h
    - src/spu94/spu94_state_internal.h
    - src/spu94/spu94_process.c
    - src/spu94/spu94_io_chain.c

key-decisions:
  - "Single adpcm_state per channel shared between encode and decode (ADPCM-05 correctness guarantee)"
  - "ADPCM stage before FIR chain_step, not inside it -- preserves 44.1 kHz operation upstream of decimator"
  - "Output buffer + codec state zeroed on disable for clean re-enable (T-02-03 stale buffer mitigation)"
  - "New spu94_get_total_latency_samples(state) instead of modifying existing spu94_get_latency_samples(void) -- no ABI break"

patterns-established:
  - "Double-buffer pattern: accumulate 28 input samples, emit from previous decoded block, encode+decode on block boundary"
  - "Toggle API pattern: spu94_set_*/get_* with NULL-safe no-op and enabled normalization to 0/1"

requirements-completed: [ADPCM-INT-01, ADPCM-INT-02, ADPCM-INT-03, ADPCM-INT-04, ADPCM-INT-05, ADPCM-INT-06]

# Metrics
duration: 31min
completed: 2026-04-26
---

# Phase 2 Plan 01: ADPCM Pipeline Integration Summary

**ADPCM encode+decode wired into spu94_process as toggleable upstream double-buffer stage with 28-sample latency, public toggle/query API, and state-dependent latency reporting**

## Performance

- **Duration:** 31 min
- **Started:** 2026-04-26T22:10:28Z
- **Completed:** 2026-04-26T22:41:43Z
- **Tasks:** 2
- **Files modified:** 4

## Accomplishments
- ADPCM encode+decode conditionally routes all samples through PS1-characteristic quantization before FIR decimator
- Three new public API functions (set/get ADPCM enabled, total latency query) exported from libspu94.so
- ADPCM off by default -- all 82 non-packaging tests pass unchanged with zero modifications
- spu94_state struct grew by ~236 bytes (560 -> ~796), well under 16384 cap
- All rt_safety gates pass with ADPCM wired into the process loop

## Task Commits

Each task was committed atomically:

1. **Task 1: Add ADPCM fields to spu94_state and public API declarations** - `a9e3143` (feat)
2. **Task 2: Implement ADPCM process-loop integration, toggle API, and latency accessor** - `e242188` (feat)

## Files Created/Modified
- `include/spu94/spu94.h` - Added spu94_set_adpcm_enabled, spu94_get_adpcm_enabled, spu94_get_total_latency_samples declarations
- `src/spu94/spu94_state_internal.h` - Added ADPCM double-buffer fields (adpcm_enabled, buf_pos, in/out buffers, codec states) + spu94_adpcm.h include
- `src/spu94/spu94_process.c` - Inserted ADPCM coloration stage between input substitution and FIR chain_step; changed l/r from const to mutable
- `src/spu94/spu94_io_chain.c` - Implemented toggle API with partial-buffer discard, codec state reset, and state-dependent latency accessor

## Decisions Made
- Single adpcm_state per channel shared between encode and decode -- ADPCM-05 guarantees encoder's internal decoder tracks identically to standalone decoder, so encode then decode on same state produces correct cross-block continuity
- ADPCM fields placed before oob_tap_count (tail appendable section) to preserve append-only offset stability for existing fuzz harnesses
- Output buffer AND codec state zeroed on disable (not just buf_pos) -- prevents stale audio AND ensures re-enable starts with clean prediction state
- enabled parameter normalized to 0/1 via ternary (T-02-01 tampering mitigation)

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered
- Build directory required fresh cmake configure with JUCE FetchContent -- resolved by pointing FETCHCONTENT_SOURCE_DIR_JUCE to cached JUCE source in main repo build
- /tmp ran out of space during initial JUCE compilation -- cleared pytest cache to free 7.5GB
- 2 packaging tests timeout (test_packaging_editable_install, test_packaging_wheel_tag) -- pre-existing, unrelated to ADPCM changes

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness
- ADPCM integration is complete and testable via the public API
- Ready for Plan 02 (integration tests covering INT-01..06 behavioral verification)
- Ready for Plan 03 (Python bindings / CLI surface for ADPCM toggle)

## Self-Check: PASSED

All 4 modified files exist. Both task commits (a9e3143, e242188) verified in git log. Key patterns confirmed in each file.

---
*Phase: 02-pipeline-integration*
*Completed: 2026-04-26*
