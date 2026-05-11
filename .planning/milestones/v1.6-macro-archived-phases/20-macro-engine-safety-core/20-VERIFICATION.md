---
phase: 20-macro-engine-safety-core
verified: 2026-05-03T23:15:00Z
status: passed
score: 11/11 must-haves verified
overrides_applied: 0
gaps: []
deferred: []
human_verification: []
---

# Phase 20: Macro Engine + Safety Core Verification Report

**Phase Goal:** The C core has a macro engine that scales register groups proportionally from their current state, gang-clamps when any member hits a ceiling/floor, recalculates knob range dynamically, re-derives macro positions from raw state, and enforces the vIIR x vWALL stability ceiling and address bounds checking at the engine level
**Verified:** 2026-05-03T23:15:00Z
**Status:** passed
**Re-verification:** No -- initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | A macro knob set to 50% on a group produces proportionally scaled values -- not absolute lookup table values | VERIFIED | `spu94_macro_apply` computes `scale_factor = position * max_scale`, then `new_value = floor + base_offset * scale_factor`. No lookup table. `test_apply_proportional` asserts concrete numeric values at position=0.6. |
| 2 | When the most-constrained register hits its ceiling, sweeping further does not change any register -- all stop together | VERIFIED | `compute_max_scale` returns `min(headroom/offset)` across all members, so at position=1.0 the most-constrained member exactly reaches its ceiling. `test_gang_clamp_ceiling` and `test_no_change_above_clamp` prove this. |
| 3 | After a raw register edit, switching to macro mode shows a knob position reflecting current register state | VERIFIED | `spu94_macro_derive` snapshots current register values as new base and returns `1.0/max_scale`. `test_derive_after_raw_edit` and `test_apply_at_derived_position_is_identity` prove round-trip identity. |
| 4 | Setting vIIR and vWALL to values whose product exceeds the stability ceiling results in clamping | VERIFIED | `spu94_safe_set_reg_i16` computes `int64_t product = abs(vIIR) * abs(vWALL)` and clamps when `product > SPU94_STABILITY_LIMIT`. Test suite uses `SPU94_STABILITY_LIMIT=536870912` (half default) to exercise clamp path with int16 values. `test_viir_clamped_above_limit` and `test_vwall_clamped_above_limit` pass with asserted clamped values. |
| 5 | Setting an m-prefix address register beyond the work buffer results in clamping | VERIFIED | `spu94_safe_set_reg_u16` computes `max_halfword = (work_buf_size/2) - 1` and returns `SPU94_ADDRESS_CLAMPED` when exceeded. `test_m_prefix_exceeds_clamped` passes using 2048-byte work buffer (max_halfword=1023, value=1024 clamped). |
| 6 | Safe setters accept values within limits and return SPU94_OK, passing through unchanged | VERIFIED | Both safe setters fall through to `spu94_set_reg_i16`/`spu94_set_reg_u16` on no-clamp path. `test_hall_preset_passthrough`, `test_echo_preset_boundary`, `test_m_prefix_within_bounds`, `test_m_prefix_at_boundary` all assert SPU94_OK. |
| 7 | INT16_MIN (-0x8000) for vIIR does not cause undefined behavior in the stability product computation | VERIFIED | `abs_i32_safe(int16_t v)` widens to int32 before negation: `(v < 0) ? -(int32_t)v : (int32_t)v`. `test_int16_min_no_ub` passes with vIIR=-0x8000. |
| 8 | A group where all base values are zero can be swept without division by zero or crash | VERIFIED | `compute_max_scale` skips members where `offset <= 0` (base == floor). `spu94_macro_apply` guard: `if (max_scale <= 0.0f || max_scale > 1e20f) max_scale = 1.0f`. `test_zero_base_stays_zero` passes. |
| 9 | After sweeping a macro knob, the ratios between registers match the ratios of their base values | VERIFIED | Offset-then-scale model: all members multiply the same `scale_factor`, so `(value[i]-floor[i])/(value[j]-floor[j])` is constant. `test_ratio_preserved` checks A:B:C = 1:2:3 at positions 0.1, 0.3, 0.5, 0.7, 1.0. `test_ratios_at_clamp` checks at gang-clamp boundary. |
| 10 | The macro knob range recalculates based on the most-constrained register | VERIFIED | `spu94_macro_derive` returns `1.0/max_scale`. Low max_scale (member near ceiling) gives high derive position (little headroom). `test_derive_position_reflects_headroom` confirms this in test_macro_range.c. |
| 11 | All macro engine register writes go through the safety layer | VERIFIED | `write_member_value` in spu94_macro.c calls `spu94_safe_set_reg_u16` for U16 members and `spu94_safe_set_reg_i16` for I16 members -- no direct calls to raw setters for writes. `grep -c "spu94_safe_set_reg" src/spu94/spu94_macro.c` returns 2. |

**Score:** 11/11 truths verified

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94.h` | SPU94_STABILITY_CLAMPED=7, SPU94_ADDRESS_CLAMPED=8, SPU94_STABILITY_LIMIT macro, safe setter declarations | VERIFIED | Lines 64-65 (enum), 656-657 (limit macro), 672-673 (function declarations) confirmed present |
| `src/spu94/spu94_safety.c` | Safety enforcement: abs_i32_safe, m_prefix_addr_mask, spu94_safe_set_reg_i16, spu94_safe_set_reg_u16 | VERIFIED | All four symbols present; implementation is substantive (166 lines, full constraint logic) |
| `include/spu94/spu94_macro.h` | spu94_macro_group_t, spu94_macro_member_t, SPU94_MACRO_MAX_MEMBERS, group_id enum, 5 API declarations | VERIFIED | All symbols confirmed; 138-line header with extern "C", full Doxygen comments |
| `src/spu94/spu94_macro.c` | spu94_macro_register_group, derive, apply, get_range, group_registered, compute_max_scale, macro_writing guard | VERIFIED | All symbols present; 251 lines, full implementation with re-entrancy guard |
| `src/spu94/spu94_state_internal.h` | macro_writing, macro_group_defs[], macro_base_values[][], macro_knob_pos[] | VERIFIED | Lines 230-233 confirmed; includes spu94_macro.h at line 21 |
| `src/spu94/CMakeLists.txt` | spu94_safety.c and spu94_macro.c in object library | VERIFIED | Lines 27-28 confirmed |
| `tests/unit/CMakeLists.txt` | add_subdirectory(safety) and add_subdirectory(macro) | VERIFIED | Lines 26-27 confirmed |
| `tests/unit/safety/CMakeLists.txt` | test_safety_stability with SPU94_STABILITY_LIMIT=536870912 override, test_safety_bounds | VERIFIED | Direct source compilation pattern with reduced limit; both targets present |
| `tests/unit/safety/test_safety_stability.c` | 8 tests including viir_clamped, vwall_clamped, hall_preset, int16_min_no_ub | VERIFIED | All 8 tests confirmed at lines 43, 62, 111, 134, 156, 181, 193 |
| `tests/unit/safety/test_safety_bounds.c` | 7 tests including m_prefix_exceeds_clamped, d_prefix_passthrough, mbase_not_checked | VERIFIED | All 7 tests confirmed at lines 47, 60, 73, 86, 101, 115, 137 |
| `tests/unit/macro/CMakeLists.txt` | 4 test executables: group, gang, derive, range | VERIFIED | All 4 targets present and linked to spu94_static |
| `tests/unit/macro/test_macro_group.c` | test_apply_proportional, test_zero_base_stays_zero, test_ratio_preserved | VERIFIED | All confirmed; 7 total tests |
| `tests/unit/macro/test_macro_gang.c` | test_gang_clamp_ceiling, test_ratios_at_clamp | VERIFIED | Both confirmed; 4 total tests |
| `tests/unit/macro/test_macro_derive.c` | test_apply_at_derived_position_is_identity, test_ratio_preserved_after_raw_edit | VERIFIED | Both confirmed; 6 total tests |
| `tests/unit/macro/test_macro_range.c` | test_range_default, test_derive_position_reflects_headroom | VERIFIED | Both confirmed; 5 total tests |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/spu94/spu94_safety.c` | `src/spu94/spu94_register_io.c` | calls `spu94_set_reg_i16` / `spu94_set_reg_u16` after clamping | VERIFIED | Non-clamp path: `return spu94_set_reg_i16(state, reg, value)` (line 135); clamp path: explicit call then `return SPU94_STABILITY_CLAMPED` (lines 107, 129) |
| `src/spu94/spu94_macro.c` | `src/spu94/spu94_safety.c` | calls `spu94_safe_set_reg_i16` / `spu94_safe_set_reg_u16` | VERIFIED | `write_member_value` calls both safe setters based on member type (lines 56-58 of spu94_macro.c); 2 grep hits on `spu94_safe_set_reg` |
| `src/spu94/spu94_macro.c` | `src/spu94/spu94_state_internal.h` | reads/writes `macro_base_values`, `macro_writing` guard | VERIFIED | `macro_base_values` read/written in register_group, derive, apply; `macro_writing` set/cleared in apply (lines 205, 225) |
| `src/spu94/spu94_state_internal.h` | `include/spu94/spu94_macro.h` | `#include <spu94/spu94_macro.h>` for group types in state struct | VERIFIED | Line 21 of spu94_state_internal.h |

---

## Data-Flow Trace (Level 4)

Macro engine operates on register values, not rendered UI data -- no web/API data flow to trace. The relevant data flow is: `spu94_macro_apply(position)` -> `compute_max_scale(base_values)` -> `write_member_value` -> `spu94_safe_set_reg_{i16,u16}` -> `spu94_set_reg_{i16,u16}` -> `state->reg_values[]`. This chain is verified structurally by the unit tests (apply at known position, read back register values, assert correct values).

---

## Behavioral Spot-Checks

Tests are compiled C binaries that run natively. All 6 phase-20 tests verified by running `ctest -R "test_safety|test_macro" --output-on-failure` directly against the built library.

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Safety tests (2 suites, 15 tests) | `ctest -R test_safety --output-on-failure` | 2/2 passed | PASS |
| Macro tests (4 suites, 22 tests) | `ctest -R test_macro --output-on-failure` | 4/4 passed | PASS |
| Build (zero errors) | `cmake --build . -j4` | exit 0, `[100%] Built target spu94_standalone_Standalone` | PASS |
| No regression (tests 1-81) | `ctest` first 81 tests (pre-fuzz) | All 81 passed | PASS |
| TDD RED gate (Plan 01) | commit 499ff87 | Failing tests committed before implementation | PASS |
| TDD RED gate (Plan 02) | commit 0ebf628 | Failing tests committed before implementation | PASS |

---

## Requirements Coverage

All 7 requirement IDs declared across both plans are confirmed satisfied.

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| SAFE-01 | 20-01-PLAN.md | vIIR x vWALL product enforced below stability ceiling | SATISFIED | `spu94_safe_set_reg_i16` with int64 product check; 5 stability tests including clamp path exercised via compile-time limit override |
| SAFE-02 | 20-01-PLAN.md | m-prefix address registers bounds-checked against work buffer | SATISFIED | `spu94_safe_set_reg_u16` with `m_prefix_addr_mask` bitmask; 7 bounds tests; mBASE and d-prefix excluded as specified |
| MACRO-01 | 20-02-PLAN.md | Macro engine in C core, relative scaling from current state | SATISFIED | `spu94_macro.c` offset-then-scale model; `test_apply_proportional` and `test_register_and_derive` prove relative (not absolute) scaling |
| MACRO-02 | 20-02-PLAN.md | Gang clamping -- all registers stop when most-constrained hits ceiling | SATISFIED | `compute_max_scale` returns `min(headroom/offset)`; `test_gang_clamp_ceiling`, `test_no_change_above_clamp`, `test_ratios_at_clamp` |
| MACRO-03 | 20-02-PLAN.md | Dynamic knob range from most-constrained register | SATISFIED | `spu94_macro_derive` returns `1.0/max_scale`; `test_derive_position_reflects_headroom` confirms low headroom = high derive position |
| MACRO-04 | 20-02-PLAN.md | Switching to macro mode re-derives knob positions from register state | SATISFIED | `spu94_macro_derive` snapshots current values and computes position; `test_apply_at_derived_position_is_identity` proves round-trip |
| MACRO-05 | 20-02-PLAN.md | Macros preserve hand-sculpted register ratios | SATISFIED | Uniform `scale_factor` preserves `(value-floor)` ratios; `test_ratio_preserved` checks 1:2:3 at 5 positions; `test_ratio_preserved_after_raw_edit` confirms new ratios captured after raw edit |

**Orphaned requirements check:** REQUIREMENTS.md traceability table maps SAFE-01, SAFE-02, MACRO-01 through MACRO-05 to Phase 20 -- all 7 are covered above. No orphaned requirements.

---

## Anti-Patterns Found

| File | Pattern | Severity | Impact |
|------|---------|----------|--------|
| None | -- | -- | -- |

No TODO, FIXME, placeholder, stub, or hardcoded-empty-return patterns found in any phase-20 deliverable.

The two `return SPU94_OK` instances in `spu94_macro.c` are both legitimate: one in `spu94_macro_register_group` after completing initialization, one in `spu94_macro_get_range` after setting out-params and confirming the group is registered. Neither is a stub.

---

## Human Verification Required

None. All must-have truths are verifiable programmatically. The macro engine is a pure C library with no GUI surfaces -- audio output quality and tactile feel are deferred to Phase 21 (GUI wiring).

---

## Gaps Summary

No gaps. All 11 observable truths verified. All 7 requirement IDs covered. All 6 test suites (15 safety + 22 macro = 37 total unit tests) pass with zero failures. Build is clean. No regressions in the 81 non-fuzz unit tests preceding the new phase-20 tests.

---

_Verified: 2026-05-03T23:15:00Z_
_Verifier: Claude (gsd-verifier)_
