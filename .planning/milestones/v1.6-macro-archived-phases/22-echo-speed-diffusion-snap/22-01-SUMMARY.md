---
phase: 22-echo-speed-diffusion-snap
plan: 01
subsystem: dsp-controls
tags: [sync-free-toggle, subdivision-table, tempo-snap, echo-speed, diffusion-texture, modal-routing]

requires:
  - phase: 21-macro-controls
    provides: "Spread+Sweep dual-axis apply, macro group definitions (ECHO_SPEED, DIFF_TEXTURE), reference value management"
  - phase: 16-core-tempo-api
    provides: "spu94_set_subdivision, binding state tracking, auto-resnap infrastructure"
provides:
  - "spu94_snap.h public API (toggle, assignment, transforms, composite apply)"
  - "Duration-sorted subdivision table with reverse lookup (15 entries)"
  - "Sync/Free toggle for echo speed (4 regs) and diffusion texture (2 regs)"
  - "Mode-aware composite apply routing (Free->Phase21, Sync->discrete)"
  - "State struct Phase 22 fields (echo_speed_sync, diff_texture_sync, snap_echo/diff_sub, knob positions)"
affects: [22-02-PLAN, phase-23-gui]

tech-stack:
  added: []
  patterns:
    - "Modal apply routing: composite function checks Sync/Free toggle, dispatches to continuous or discrete path"
    - "Duration-sorted table: Sweep operates on musically-ordered positions, not raw enum indices"
    - "0xFF sentinel for unset dropdown selections (distinct from 0 = SPU94_SUB_1_1)"

key-files:
  created:
    - include/spu94/spu94_snap.h
    - src/spu94/spu94_snap.c
    - tests/unit/snap/CMakeLists.txt
    - tests/unit/snap/test_snap_toggle.c
    - tests/unit/snap/test_snap_diffusion.c
  modified:
    - src/spu94/spu94_state_internal.h
    - src/spu94/spu94_state.c
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt

key-decisions:
  - "Init/reset set snap_echo_sub and snap_diff_sub to 0xFF (unset) rather than 0 (which would be SPU94_SUB_1_1, a valid subdivision)"
  - "Toggle implementation fully in Plan 01 snap.c (not deferred to Plan 02) since diffusion snap is complete in this plan"
  - "Transform functions (Sweep/Spread/Rotate) remain stubs returning SPU94_OK for Plan 02 to implement"

patterns-established:
  - "Modal apply routing: spu94_snap_echo_speed_apply checks echo_speed_sync, routes to continuous (Phase 21) or discrete (Phase 22) path"
  - "Duration-sorted subdivision table: spu94_subdivision_sorted_by_duration[15] + spu94_subdivision_to_sorted_pos[15] enable musically-ordered Sweep in Plan 02"
  - "Separate toggle for echo speed (4 regs) and diffusion texture (2 regs) per D-02 and D-12"

requirements-completed: [SNAP-01, SNAP-02, SNAP-03]

duration: 21min
completed: 2026-05-05
---

# Phase 22 Plan 01: Snap Module Contracts + Toggle Summary

**Sync/Free modal toggle for echo speed (dLSAME/dRSAME/dLDIFF/dRDIFF) and diffusion texture (dAPF1/dAPF2) with duration-sorted subdivision table, mode transition state preservation, and 13-test TDD suite**

## Performance

- **Duration:** 21 min (1265s)
- **Started:** 2026-05-05T02:18:12Z
- **Completed:** 2026-05-05T02:39:17Z
- **Tasks:** 2
- **Files modified:** 9

## Accomplishments

- Full Phase 22 snap module API declared in spu94_snap.h (toggle, assignment, transforms, composite apply)
- Duration-sorted subdivision table (15 entries, longest to shortest) with bidirectional reverse lookup verified by exhaustive round-trip test
- Sync/Free toggle implementation for both echo speed (4 registers) and diffusion texture (2 registers) with correct mode transition behavior: Sync->Free preserves values and unbinds from GRID; Free->Sync quantizes to dropdown selections; unset dropdowns (0xFF) leave registers unchanged
- Diffusion snap complete: toggle + dropdown + apply_diff_texture + composite routing
- State struct extended with 20 bytes of Phase 22 fields (echo_speed_sync, diff_texture_sync, snap_echo_sub[4], snap_diff_sub[2], snap_sweep/spread/rotate_pos)
- 13 tests across 2 test suites all green, 0 regressions in full tempo/macro/snap test matrix (17 tests)

## Task Commits

Each task was committed atomically:

1. **Task 1: Header contracts + state struct extension + sorted table** - `3017178` (feat)
2. **Task 2: Sync/Free toggle + diffusion snap implementation with TDD** - `0be052f` (test)

## Files Created/Modified

- `include/spu94/spu94_snap.h` - Full Phase 22 public API: toggle, assignment, transforms, composite apply
- `src/spu94/spu94_snap.c` - Toggle implementation, diffusion apply, sorted table, transform stubs for Plan 02
- `src/spu94/spu94_state_internal.h` - Phase 22 state fields (20 bytes: 2 toggles + 6 dropdown selections + 12 bytes knob positions)
- `src/spu94/spu94_state.c` - Init/reset 0xFF initialization for snap dropdown arrays
- `src/spu94/CMakeLists.txt` - Added spu94_snap.c to spu94_obj sources
- `tests/unit/CMakeLists.txt` - Added add_subdirectory(snap)
- `tests/unit/snap/CMakeLists.txt` - Test targets for test_snap_toggle and test_snap_diffusion
- `tests/unit/snap/test_snap_toggle.c` - 11 tests: table verification + echo speed toggle
- `tests/unit/snap/test_snap_diffusion.c` - 6 tests: diffusion texture toggle + dropdown + composite routing

## Decisions Made

- **0xFF sentinel in init/reset (Rule 2 auto-add):** Zero-init would set snap_echo_sub/snap_diff_sub to 0 (SPU94_SUB_1_1, a valid subdivision). Added explicit 0xFF initialization in both spu94_init and spu94_reset to correctly represent "unset/no selection". This prevents Free->Sync toggle from silently snapping all registers to whole-note subdivisions on first toggle.
- **Toggle implemented in Plan 01:** The plan said "stub implementations" for all functions, but also said "the toggle and diffusion functions will be implemented in this task's GREEN phase". Implemented the full toggle + diffusion in Plan 01 since diffusion snap is complete in this plan per the objective.
- **Transform stubs return SPU94_OK with no side effects:** spu94_snap_apply_echo_speed and spu94_snap_derive_echo_speed are stubs returning SPU94_OK (derive returns 0.5/0.5/0.0 defaults). Plan 02 implements the real Sweep/Spread/Rotate transforms.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Added 0xFF initialization for snap dropdown arrays in init/reset**
- **Found during:** Task 1 (state struct extension)
- **Issue:** Zero-init via spu94_zero_bytes would set snap_echo_sub[4] and snap_diff_sub[2] to 0, which is SPU94_SUB_1_1 (a valid subdivision), not "unset"
- **Fix:** Added explicit 0xFF initialization loops in both spu94_init() and spu94_reset() after the zero-fill step
- **Files modified:** src/spu94/spu94_state.c
- **Verification:** test_free_to_sync_unset_dropdown_no_change passes (unset dropdowns do not change register values on toggle)
- **Committed in:** 3017178 (Task 1 commit)

---

**Total deviations:** 1 auto-fixed (1 missing critical)
**Impact on plan:** Essential for correctness -- without this fix, the first Free->Sync toggle would silently snap all registers to whole-note subdivisions. No scope creep.

## Known Stubs

| File | Line | Stub | Reason |
|------|------|------|--------|
| src/spu94/spu94_snap.c | 225 | spu94_snap_apply_echo_speed returns SPU94_OK without transforms | Plan 02 implements Sweep/Spread/Rotate |
| src/spu94/spu94_snap.c | 239 | spu94_snap_derive_echo_speed returns defaults (0.5/0.5/0.0) | Plan 02 implements derivation |

These stubs are intentional -- Plan 02 implements the discrete transform functions. The plan's goal is achieved: toggle works, diffusion snap is complete, foundation is ready for Plan 02 transforms.

## Issues Encountered

None.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- Plan 02 can build discrete Sweep/Spread/Rotate transforms on top of this foundation
- The spu94_snap_apply_echo_speed stub is the entry point Plan 02 will implement
- Duration-sorted table is ready for musically-ordered Sweep operations
- Test infrastructure (tests/unit/snap/) is ready for Plan 02 test files (test_snap_sweep.c, test_snap_spread.c, test_snap_rotate.c, test_snap_bpm.c)

---
*Phase: 22-echo-speed-diffusion-snap*
*Completed: 2026-05-05*
