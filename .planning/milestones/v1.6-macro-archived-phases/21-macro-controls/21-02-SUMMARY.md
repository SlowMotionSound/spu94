---
phase: 21-macro-controls
plan: 02
subsystem: dsp-core
tags: [macro-controls, group-definitions, link-toggles, constrained-mode, room-designer, c99]

# Dependency graph
requires:
  - phase: 21-macro-controls
    plan: 01
    provides: Spread+Sweep apply, bipolar apply, reference values, expanded 10-group enum, is_bipolar field
provides:
  - 10 static const macro group tables (Echo Speed, Tap Position, Diff Amount/Texture/Position, Early Refl, Room Size, Buffer, Decay, Reflectivity)
  - spu94_macro_controls_register_all function
  - spu94_macro_get_group_def accessor
  - Per-wall link toggles (4 booleans) with getter/setter API
  - Same/Cross link toggle with getter/setter API
  - Tap constrained mode with wall boundary clamping
  - Diffusion position constrained mode with wall boundary clamping
  - spu94_macro_apply_room_size with echo speed propagation
  - spu94_macro_apply_tap_position with optional wall clamping
  - spu94_macro_apply_diff_position with optional wall clamping
affects: [21-03-PLAN (coupling, buffer, preset derive-all), gui-phase (control surface mapping)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Static const group table pattern: each group is a named const struct with register array, floor/ceiling per member, is_bipolar flag"
    - "Composite apply: base engine apply + conditional post-apply coordination (link propagation, constrained clamping)"
    - "TICK_LATCHED flush pattern: composite apply functions tick internally to flush pending writes before reading back values"
    - "Wall containment mapping: L-taps bounded by mLSAME, R-taps by mRSAME, APF1 by same-side wall, APF2 by cross-side wall"

key-files:
  created:
    - src/spu94/spu94_macro_controls.c
    - tests/unit/macro/test_macro_controls.c
    - tests/unit/macro/test_macro_link.c
    - tests/unit/macro/test_macro_constrain.c
  modified:
    - include/spu94/spu94_macro.h
    - src/spu94/CMakeLists.txt
    - tests/unit/macro/CMakeLists.txt

key-decisions:
  - "Room Size includes all 16 m-prefix registers (not 14): mLSAME/mRSAME + 8 mCOMB + mLDIFF/mRDIFF + 4 mAPF, excluding mBASE"
  - "Link propagation is ratio-based: new_d = old_d * (new_m / old_m), preserving the echo speed / distance relationship"
  - "Constrained mode is a post-apply clamp (Pitfall 7): reads wall value at apply time, not cached at registration"
  - "Composite apply functions flush TICK_LATCHED writes internally via spu94_tick to ensure callers see final values"
  - "Diffusion position containment: mLAPF1->mLSAME, mRAPF1->mRSAME, mLAPF2->mLDIFF, mRAPF2->mRDIFF (matches reverb body topology)"

patterns-established:
  - "Static const group registration via lookup table: g_all_groups[SPU94_MACRO_GROUP__COUNT] indexed by enum"
  - "Wall-to-register mapping tables: wall_m_regs[4], wall_d_regs[4], tap_wall_regs[8], diff_pos_wall_regs[4]"
  - "Composite apply with re-entrancy awareness: Room Size apply delegates to spu94_macro_apply then does conditional propagation"

requirements-completed: [WALL-01, WALL-02, WALL-03, WALL-04, WALL-05, WALL-06, ECHO-SPD-01, TAP-01, TAP-02, TAP-03, DIFF-AMT-01, DIFF-TEX-01, DIFF-POS-01, DIFF-POS-02, ROOM-01, CTRL-03, SAFE-03, SAFE-04]

# Metrics
duration: 1006s
completed: 2026-05-04
---

# Phase 21 Plan 02: Macro Group Definitions + Link/Constrain Summary

**10 static const group tables defining the Room Designer's register-to-knob mapping, with per-wall link toggles for echo speed propagation and wall-boundary constrained mode for taps and diffusion positions**

## Performance

- **Duration:** 16 min
- **Started:** 2026-05-04T20:50:41Z
- **Completed:** 2026-05-04T21:07:27Z
- **Tasks:** 2
- **Files created:** 4 (1 source, 3 test files)
- **Files modified:** 3 (1 header, 2 CMakeLists)

## Accomplishments

- Defined all 10 macro group static const tables with correct register assignments, signedness types, floor/ceiling values, and is_bipolar flags
- Room Size group includes all 16 m-prefix address registers (mLSAME/mRSAME, 8 mCOMB, mLDIFF/mRDIFF, 4 mAPF) -- verified against register enum
- Decay group floor is -0x1000 enforcing SAFE-03/SAFE-04 (unreachable -0x8000)
- Registration function (spu94_macro_controls_register_all) wires all 10 groups via a static lookup table
- Per-wall link toggles (4 booleans): Room Size apply propagates echo speed changes proportionally when linked
- Same/Cross link toggle: echo speed ratio changes propagate across wall pairs (Left Wall <-> Left Cross, Right Wall <-> Right Cross)
- Tap constrained mode: post-apply clamp to wall boundaries (L-taps <= mLSAME, R-taps <= mRSAME)
- Diffusion position constrained mode: post-apply clamp to containing wall boundaries
- Constrained ceiling is dynamic -- tracks current wall value, not cached at registration time
- Discovered and fixed TICK_LATCHED flush issue: composite apply functions now tick internally after writing to latched registers, ensuring callers read final values

## Task Commits

Each task was committed atomically:

1. **Task 1 RED: Failing tests for group definitions** - `f89a534` (test)
2. **Task 1 GREEN: Group definitions, registration, link/constrain impl** - `579e381` (feat)
3. **Task 2 RED: Failing tests for link toggles and constrained mode** - `7396689` (test)
4. **Task 2 GREEN: Link propagation + constrain clamping + TICK_LATCHED fix** - `5f62681` (feat)

## Files Created/Modified

- `src/spu94/spu94_macro_controls.c` -- 10 group tables, registration, link toggles, constrained mode, composite apply functions (+310 lines)
- `include/spu94/spu94_macro.h` -- 14 new API declarations (register_all, get_group_def, link/constrain getters/setters, composite apply)
- `src/spu94/CMakeLists.txt` -- Added spu94_macro_controls.c to spu94_obj
- `tests/unit/macro/test_macro_controls.c` -- 12 test functions: registration, 10 group member verification, Room Size scaling
- `tests/unit/macro/test_macro_link.c` -- 6 test functions: wall link default, set/get, propagation, no-propagation, same/cross link
- `tests/unit/macro/test_macro_constrain.c` -- 6 test functions: constrained default, tap clamping, unconstrained, diff position, dynamic ceiling
- `tests/unit/macro/CMakeLists.txt` -- 3 new test targets

## Decisions Made

- **Room Size member count:** 16, not 14. The RESEARCH.md said "14 m-prefix regs" but the register enum has 16 m-prefix address registers (excluding mBASE). D-15 says "scales all m-prefix registers" so all 16 are included. SPU94_MACRO_MAX_MEMBERS (16) fits exactly.
- **Link propagation is ratio-based:** new_d = old_d * (new_m / old_m). This preserves the ratio between echo speed and wall distance, which is the physical relationship D-06 describes.
- **Internal tick for composite apply:** Room Size, Tap Position, and Diffusion Position composite apply functions call spu94_tick internally after writing to TICK_LATCHED registers. This means these functions advance the reverb buffer_address as a side effect. Necessary for correct behavior but worth documenting.
- **Diffusion position containment mapping:** mLAPF1 -> mLSAME, mRAPF1 -> mRSAME (same-side). mLAPF2 -> mLDIFF, mRAPF2 -> mRDIFF (cross-side). This matches the reverb body topology where APF1 operates on same-side IIR output and APF2 on cross-side.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] TICK_LATCHED flush in composite apply functions**
- **Found during:** Task 2 GREEN phase
- **Issue:** Composite apply functions (Room Size, Tap Position, Diffusion Position) wrote to TICK_LATCHED d-prefix/m-prefix registers but did not flush pending writes before returning. Callers reading register values saw stale pre-apply values.
- **Fix:** Added spu94_tick calls after each batch of TICK_LATCHED writes in composite apply functions. Room Size: tick after d-prefix echo speed propagation. Tap Position and Diff Position: tick after constrained-mode clamping writes.
- **Files modified:** src/spu94/spu94_macro_controls.c
- **Commit:** 5f62681

**2. [Rule 1 - Bug] Test model mismatch in Room Size scaling test**
- **Found during:** Task 1 GREEN phase
- **Issue:** Original test assumed position=0.5 would produce half the Hall register values. This misunderstands the proportional scaling model where position maps to [0, max_scale * base_offset]. Fixed to apply at half the derived position instead.
- **Fix:** Changed test to derive position first, then apply at pos*0.5 to get half values.
- **Files modified:** tests/unit/macro/test_macro_controls.c
- **Commit:** 579e381

## Issues Encountered

None beyond the TICK_LATCHED flush issue (auto-fixed as deviation).

## User Setup Required

None -- no external service configuration required.

## TDD Gate Compliance

Task 1:
- RED gate: `f89a534` (test commit with link-time failures confirming functions not implemented)
- GREEN gate: `579e381` (feat commit with all 12 tests passing)

Task 2:
- RED gate: `7396689` (test commit with 5 test failures in link/constrain behavior)
- GREEN gate: `5f62681` (feat commit with all 24 tests passing, TICK_LATCHED fix)

## Threat Surface Check

| Threat | Status | Mitigation |
|--------|--------|------------|
| T-21-05: wall_index out of bounds | Mitigated | spu94_macro_set_wall_link validates [0,3], out of range is no-op |
| T-21-06: constrained clamp bypasses safety | Mitigated | Post-clamp writes go through spu94_safe_set_reg_u16 |
| T-21-08: Room Size 16 members exceeds MAX_MEMBERS | Mitigated | Exactly 16 = SPU94_MACRO_MAX_MEMBERS, fits within array bounds |
| Internal tick side effect | Documented | Composite apply functions advance buffer_address; acceptable tradeoff for correct TICK_LATCHED behavior |

## Next Phase Readiness

- All 10 macro group definitions registered and verified with 24 test functions
- Plan 03 can now implement: Decay/Reflectivity coupling (auto-rederive), Buffer apply (mBASE inverted mapping + m-prefix re-clamp), and derive-all-macros-after-preset-load
- 9 macro test suites pass (4 Phase 20 + 2 Plan 01 + 3 this plan), zero regressions
- 2 safety test suites still passing

## Self-Check: PASSED

All 4 created files exist. All 4 commit hashes verified in git log.

---
*Phase: 21-macro-controls*
*Completed: 2026-05-04*
