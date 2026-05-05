---
phase: 21-macro-controls
plan: 03
subsystem: dsp-core
tags: [macro-controls, decay-coupling, buffer-crush, preset-derivation, derive-all, c99]

# Dependency graph
requires:
  - phase: 21-macro-controls
    plan: 02
    provides: 10 group definitions, registration function, link/constrain toggles, composite apply (Room Size, Tap Position, Diff Position)
provides:
  - spu94_macro_apply_decay with Reflectivity re-derive coupling
  - spu94_macro_apply_buffer with inverted mapping and m-prefix re-clamp cascade
  - spu94_macro_derive_all for read-only preset-to-macro derivation
  - Complete Phase 21 control surface (10 macro groups, all coupling/safety behaviors)
affects: [22-snap-integration (echo speed snap), 23-gui-overlay (macro panel controls)]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Cross-macro coupling: Decay apply triggers Reflectivity re-derive (only cross-dependency)"
    - "Buffer cascade: mBASE write -> effective ceiling recalc -> m-prefix re-clamp -> group re-derive"
    - "Derive-all is provably read-only: 35-register snapshot before/after asserts equality"
    - "Inverted mapping: knob up = register down (user thinks in 'more space' not 'lower address')"

key-files:
  created:
    - tests/unit/macro/test_macro_coupling.c
  modified:
    - src/spu94/spu94_macro_controls.c
    - include/spu94/spu94_macro.h
    - tests/unit/macro/CMakeLists.txt

key-decisions:
  - "Buffer effective ceiling = (work_buf_size / 2) - 1 - mBASE, simulating PS1 relative addressing"
  - "Re-clamp writes m-prefix directly via spu94_set_reg_u16 (not safe setter) since the effective ceiling is tighter than SAFE-02's work_buf_size-based check"
  - "Derive-all routes single-bipolar groups to derive_bipolar, multi-register to derive_spread_sweep, single-unipolar to derive"
  - "After derive_all, spread is forced to 0.5 because references were just set from current state (spacing IS the reference)"

patterns-established:
  - "Post-apply coupling hook: apply function calls derive on dependent group after write"
  - "Buffer cascade pattern: mBASE change -> iterate m-prefix -> clamp to effective ceiling -> tick -> re-derive dependent groups"
  - "Register-invariant derivation: snapshot all registers before, call derive functions, assert unchanged after"

requirements-completed: [CTRL-02, BUF-01, BUF-02, SS-02, PRESET-01, SAFE-03, SAFE-04]

# Metrics
duration: 29min
completed: 2026-05-04
---

# Phase 21 Plan 03: Coupling, Buffer, and Derive-All Summary

**Decay-to-Reflectivity stability coupling, Buffer crush with m-prefix re-clamp cascade, and read-only derive-all completing the full Phase 21 macro control surface**

## Performance

- **Duration:** 29 min
- **Started:** 2026-05-04T21:12:46Z
- **Completed:** 2026-05-04T21:41:25Z
- **Tasks:** 2
- **Files modified:** 4 (1 source, 1 header, 1 test file created, 1 CMakeLists)

## Accomplishments

- Implemented Decay-to-Reflectivity coupling: vIIR write through stability ceiling may clamp vWALL, Reflectivity knob immediately re-derives from truth. Only cross-macro dependency in the system.
- Implemented Buffer control with inverted mapping (knob up = mBASE=0 = max space) and m-prefix re-clamp cascade. After mBASE increases, effective ceiling for all 16 m-prefix registers shrinks to (work_buf_size/2 - 1 - mBASE), values are clamped, and Room Size/Tap Position/Diff Position groups re-derive.
- Implemented derive-all: read-only derivation after preset load sets references for all groups, derives positions, initializes spread to 0.5 center. Proven register-invariant by 35-register snapshot assertion.
- Phase 21 control surface is COMPLETE: 10 macro groups, Spread+Sweep engine, bipolar knobs, link/constrain toggles, Decay/Reflectivity coupling, Buffer crush with safety re-clamp, preset derive-all. All 26 Phase 21 requirements proven by passing tests.

## Task Commits

Each task was committed atomically:

1. **Task 1 RED: Failing tests for coupling/buffer/derive-all** - `54e6d06` (test)
2. **Task 1 GREEN: Implement Decay coupling, Buffer apply, derive-all** - `a8683c7` (feat)

Task 2 was verification-only (no code changes needed -- all requirements covered).

## Files Created/Modified

- `tests/unit/macro/test_macro_coupling.c` - 12 test functions: 3 Decay-Reflectivity coupling, 5 Buffer apply, 4 preset derivation
- `src/spu94/spu94_macro_controls.c` - spu94_macro_apply_decay, spu94_macro_apply_buffer, spu94_macro_derive_all implementations (+157 lines)
- `include/spu94/spu94_macro.h` - 3 new API declarations (apply_decay, apply_buffer, derive_all)
- `tests/unit/macro/CMakeLists.txt` - test_macro_coupling target added

## Decisions Made

- **Buffer effective ceiling formula:** `(work_buf_size / 2) - 1 - mBASE`. This simulates PS1's relative addressing where reverb body offsets are relative to mBASE. When mBASE increases, the available space for m-prefix registers decreases.
- **Re-clamp uses spu94_set_reg_u16 directly:** The effective ceiling from mBASE is tighter than SAFE-02's (work_buf_size/2 - 1). The safe setter would pass values through unclamped because it doesn't know about mBASE. Direct writes with manual clamping are correct here.
- **Derive-all type routing:** Groups are classified by member_count and is_bipolar. Single-bipolar -> derive_bipolar (Decay, Reflectivity). Multi-register -> derive_spread_sweep (all S+S groups). Single-unipolar -> derive (Buffer). Room Size (16 members, unipolar) hits the multi-register path but gets derive_spread_sweep which sets references correctly.
- **Spread forced to 0.5 after derive_all:** Since references are set from the current register state in the same call, the current spacing IS the reference spacing by definition. Spread = 0.5 means factor 1.0 (identity), which is correct.

## Deviations from Plan

None - plan executed exactly as written.

## Issues Encountered

None.

## User Setup Required

None -- no external service configuration required.

## TDD Gate Compliance

- RED gate: `54e6d06` (test commit with implicit-declaration errors confirming functions not implemented)
- GREEN gate: `a8683c7` (feat commit with all 12 new tests passing + full macro suite green)
- REFACTOR gate: not needed (implementation is clean, follows established Plan 02 patterns)

## Threat Surface Check

| Threat | Status | Mitigation |
|--------|--------|------------|
| T-21-09: derive_all modifies registers | Mitigated | Test snapshots all 35 registers before/after, asserts equality |
| T-21-10: Buffer cascade unbounded recursion | Mitigated | Re-clamp iterates m-prefix registers once (16 iterations); no recursion |
| T-21-11: Reflectivity shows stale position | Mitigated | Decay apply always re-derives Reflectivity immediately after vIIR write |
| T-21-12: macro_ref_values information disclosure | Accepted | Internal state, not exposed to external callers |

## Phase 21 Completion Status

Phase 21 is COMPLETE. The full Room Designer control surface is defined in the C core:
- 10 macro groups with correct register assignments, signedness, floor/ceiling values
- Spread+Sweep dual-axis engine (6 groups)
- Bipolar center-detent knobs (Decay, Reflectivity)
- Per-wall link toggles with ratio-based echo speed propagation
- Same/Cross link toggle for physical room realism
- Wall-constrained mode for taps and diffusion positions
- Decay-to-Reflectivity stability coupling (auto-rederive)
- Buffer crush control with m-prefix re-clamp cascade
- Derive-all for preset integration (register-invariant)
- All 26 requirements proven by 10 passing test suites

Ready for Phase 22 (echo speed snap integration) and Phase 23 (GUI overlay).

## Self-Check: PASSED

---
*Phase: 21-macro-controls*
*Completed: 2026-05-04*
