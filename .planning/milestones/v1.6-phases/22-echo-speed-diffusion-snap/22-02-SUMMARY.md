---
phase: 22-echo-speed-diffusion-snap
plan: 02
subsystem: dsp-controls
tags: [sweep, spread, rotate, euclidean-permutation, tempo-snap, echo-speed, sync-mode, discrete-transforms]

requires:
  - phase: 22-echo-speed-diffusion-snap/01
    provides: "spu94_snap.h API, toggle implementation, sorted table, state struct fields, transform stubs"
  - phase: 16-core-tempo-api
    provides: "spu94_set_subdivision, auto-resnap loop, binding state tracking"
  - phase: 21-macro-controls
    provides: "Spread+Sweep dual-axis apply, macro group definitions, reference value management"
provides:
  - "compute_spread_indices: centroid-based scaling with factor=spread*2.0 (0.5=identity)"
  - "compute_sweep_offset: dynamic range mapping gang-clamped by Spread extremes (D-09, D-10)"
  - "compute_rotate_indices: 4-position Euclidean circular permutation via modular arithmetic"
  - "spu94_snap_apply_echo_speed: complete Spread->Sweep->Rotate transform chain"
  - "spu94_snap_derive_echo_speed: stored knob position recovery in Sync mode"
  - "BPM auto-resnap integration: TRANSFORMED subdivisions used for resnap (Pitfall 3 prevention)"
affects: [phase-23-gui, preset-system]

tech-stack:
  added: []
  patterns:
    - "Spread factor = spread * 2.0: identity at 0.5 (factor 1.0), unison at 0.0 (factor 0.0), doubled spacing at 1.0 (factor 2.0)"
    - "Sweep dynamic range: headroom_down = min_pos, headroom_up = 14 - max_pos, offset mapped from sweep [0,1]"
    - "Rotate quantization: float [0,1] -> int [0,3] at 0.25 thresholds, circular permutation via (i+rot)%4"
    - "Pitfall 3 prevention: spu94_set_subdivision writes TRANSFORMED subdivision to tempo_bind_sub, so auto-resnap uses correct value"

key-files:
  created:
    - tests/unit/snap/test_snap_spread.c
    - tests/unit/snap/test_snap_sweep.c
    - tests/unit/snap/test_snap_rotate.c
    - tests/unit/snap/test_snap_bpm.c
  modified:
    - src/spu94/spu94_snap.c
    - tests/unit/snap/CMakeLists.txt

key-decisions:
  - "Spread uses centroid-based offset scaling: centroid = mean of sorted positions, each register's offset from centroid scaled by factor. This gives symmetric expansion/contraction around the rhythmic center."
  - "Sweep direction convention: sweep=0 shifts toward sorted position 0 (longest/slowest), sweep=1 shifts toward position 14 (shortest/fastest). Matches 'turning up the speed' spatial metaphor."
  - "Rotate follows the action section formula: out[i] = in[(i + rot) % 4]. Rotation by 1 shifts elements forward (reg 0 gets what was in reg 1, etc.)"
  - "Derive returns stored knob positions rather than computing from register state, since the quantized transform chain is not invertible."
  - "BPM integration requires no new code: spu94_set_subdivision already writes to tempo_bind_sub, so the existing auto-resnap loop in spu94_set_tempo uses the TRANSFORMED subdivision automatically."

patterns-established:
  - "Spread->Sweep->Rotate chain: three-stage transform pipeline on sorted table positions, final positions converted back to subdivision enums via spu94_subdivision_sorted_by_duration[]"
  - "Dynamic range gang-clamping: sweep offset limited by min/max of spread output positions, ensuring no overshoot and no dead zones (D-09)"
  - "Input clamping at function entry: spread/sweep/rotate clamped to [0.0, 1.0] before any computation (T-22-11)"
  - "0xFF dropout: registers with unset dropdowns (0xFF) are skipped in transform chain (T-22-09)"

requirements-completed: [SNAP-01, SNAP-02]

duration: 32min
completed: 2026-05-05
---

# Phase 22 Plan 02: Discrete Transform Engine Summary

**Spread/Sweep/Rotate transform chain for Sync-mode echo speed: centroid-based spread scaling, dynamic-range sweep with gang-clamped headroom, 4-position Euclidean rotation, and verified BPM auto-resnap through existing tempo infrastructure**

## Performance

- **Duration:** 32 min (1941s)
- **Started:** 2026-05-05T02:43:58Z
- **Completed:** 2026-05-05T03:16:19Z
- **Tasks:** 2
- **Files modified:** 6

## Accomplishments

- Three discrete macro transforms implemented in spu94_snap.c:
  - **Spread**: converts dropdown subdivisions to duration-sorted positions, computes centroid (mean), scales each register's offset from centroid by factor=spread*2.0. Spread=0 collapses all to centroid (unison), spread=0.5 reproduces dropdown selections (identity), spread=1.0 doubles spacing (max polyrhythm). All positions clamped to [0,14].
  - **Sweep**: computes available headroom in each direction from Spread output extremes, maps sweep [0,1] to integer offset within that headroom. Dynamic range gang-clamping ensures no dead zones (D-09) and naturally zero range when Spread spans full table (D-10).
  - **Rotate**: maps float [0,1] to 4 discrete rotation positions at 0.25 thresholds, performs circular permutation via out[i] = in[(i+rot)%4]. Same multiset of subdivisions, different assignment to physical echo paths.
- Complete Spread -> Sweep -> Rotate -> spu94_set_subdivision chain in spu94_snap_apply_echo_speed
- BPM auto-resnap integration verified: existing spu94_set_tempo loop correctly resnaps Sync-mode registers because spu94_set_subdivision writes the TRANSFORMED subdivision to tempo_bind_sub (Pitfall 3 prevention confirmed)
- spu94_snap_derive_echo_speed returns stored knob positions in Sync mode, neutral defaults in Free mode
- 22 new tests across 4 test suites, all green, 0 regressions in full project test matrix (70+ tests verified)
- Plan 01 stubs fully replaced with working implementations

## Task Commits

Each task was committed atomically following TDD (RED then GREEN):

1. **Task 1: Spread + Sweep transforms**
   - RED: `76be09f` (test) - 11 failing tests for Spread (5) + Sweep (6)
   - GREEN: `fd003e9` (feat) - compute_spread_indices, compute_sweep_offset, apply_sweep_to_indices
2. **Task 2: Rotate transform + BPM resnap integration**
   - RED: `1b056e7` (test) - 11 tests for Rotate (6) + BPM (5), 3 rotate failures
   - GREEN: `12487fb` (feat) - compute_rotate_indices, wired into apply chain

## Files Created/Modified

- `src/spu94/spu94_snap.c` - Complete Spread/Sweep/Rotate transform implementation replacing Plan 01 stubs (+266 lines net)
- `tests/unit/snap/test_snap_spread.c` - 5 tests: unison, identity, doubled spacing, same-dropdown no-op, table boundary clamping
- `tests/unit/snap/test_snap_sweep.c` - 6 tests: center no-shift, shift toward longest/shortest, dynamic range from spread, full-spread-no-range (D-10), all-registers shift with spacing preservation
- `tests/unit/snap/test_snap_rotate.c` - 6 tests: identity, rotation 1/2/3, circular permutation multiset preservation, unison no-op (Pitfall 2)
- `tests/unit/snap/test_snap_bpm.c` - 5 tests: Sync resnap, Free no-resnap, transformed-not-reference resnap (Pitfall 3), diffusion resnap, derive position recovery
- `tests/unit/snap/CMakeLists.txt` - Added 4 new test targets

## Decisions Made

- **Centroid-based Spread scaling**: chose mean of sorted positions as the centroid for symmetric expansion/contraction. Alternative (median) would bias toward musically common subdivisions but complicate the math without clear sonic benefit.
- **Sweep spatial convention**: sweep=0 = slowest (toward position 0), sweep=1 = fastest (toward position 14). This matches "turning up the echo speed" intuition rather than the research's initial suggestion of "sweep up = slower".
- **Rotate direction (out[i] = in[(i+rot)%4])**: follows the plan's action section formula. Rotation by 1 shifts element sources forward: register 0 gets what was in register 1's slot, etc. This is the natural Euclidean rotation direction.
- **No new BPM integration code needed**: the existing auto-resnap infrastructure handles transformed subdivisions correctly because spu94_set_subdivision updates tempo_bind_sub to the final (transformed) subdivision, not the dropdown reference.
- **Derive returns stored positions**: inverting the quantized Spread->Sweep->Rotate chain from register state is impractical (information loss at clamping/rounding boundaries). Stored knob positions are the truth.

## Deviations from Plan

None - plan executed exactly as written. The BPM integration prediction (no new code needed, Pitfall 3 handled by existing infrastructure) was confirmed by test_bpm_resnaps_transformed_not_reference passing without modification.

## TDD Gate Compliance

Verified in git log:
1. RED gate: `76be09f` (test) - Task 1 failing tests
2. GREEN gate: `fd003e9` (feat) - Task 1 implementation
3. RED gate: `1b056e7` (test) - Task 2 failing tests
4. GREEN gate: `12487fb` (feat) - Task 2 implementation

All TDD gates satisfied. No REFACTOR commits needed (implementations were clean from the start).

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Self-Check: PASSED

All files verified to exist:
- src/spu94/spu94_snap.c: FOUND
- tests/unit/snap/test_snap_spread.c: FOUND
- tests/unit/snap/test_snap_sweep.c: FOUND
- tests/unit/snap/test_snap_rotate.c: FOUND
- tests/unit/snap/test_snap_bpm.c: FOUND
- tests/unit/snap/CMakeLists.txt: FOUND

All commits verified in git log:
- 76be09f: FOUND
- fd003e9: FOUND
- 1b056e7: FOUND
- 12487fb: FOUND

---
*Phase: 22-echo-speed-diffusion-snap*
*Completed: 2026-05-05*
