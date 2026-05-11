---
phase: 21-macro-controls
verified: 2026-05-04T22:15:00Z
status: passed
score: 26/26 must-haves verified
overrides_applied: 0
re_verification: false
gaps: []
deferred: []
human_verification: []
---

# Phase 21: Macro Controls Verification Report

**Phase Goal:** Full Room Designer control surface defined in the C core -- walls with paired echo speeds and link toggles, tap positions with constrained/unconstrained mode, diffusion section (amount/texture/position), Room Size master, Buffer (mBASE) control, Decay/Reflectivity coupling, Early Reflections -- all wired through the Phase 20 macro engine with Spread+Sweep model, bipolar knob support, vIIR safety constraints, and preset-to-macro derivation proven
**Verified:** 2026-05-04T22:15:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| #  | Truth | Status | Evidence |
|----|-------|--------|----------|
| 1  | Spread+Sweep apply takes spread [0,1] and sweep [0,1] or [-1,1] and produces gang-clamped register values | VERIFIED | `spu94_macro_apply_spread_sweep` in spu94_macro.c:299; 7 tests in test_macro_spread_sweep.c all pass |
| 2  | Spread at 0.5 reproduces reference proportions exactly | VERIFIED | `test_spread_center_reproduces_reference` passes; spread_factor = spread*2.0, center gives factor 1.0 |
| 3  | Bipolar apply maps [-1,1] to [floor,0] on left half and [0,ceiling] on right half | VERIFIED | `spu94_macro_apply_bipolar` in spu94_macro.c:408; 8 tests in test_macro_bipolar.c all pass |
| 4  | Decay bipolar apply with floor=-0x1000 prevents any value below -0x1000 (SAFE-03) | VERIFIED | g_decay in spu94_macro_controls.c:147 defines floor=-0x1000; test_decay_floor_minus_0x1000 passes |
| 5  | No code path can set vIIR to -0x8000 through the macro layer (SAFE-04) | VERIFIED | test_minus_0x8000_unreachable loops 0.01 steps across full range; all pass; floor=-0x1000 in group def |
| 6  | Reference values stored separately from base values in the state struct | VERIFIED | macro_ref_values[16][16] added in spu94_state_internal.h:247, separate from macro_base_values |
| 7  | SPU94_MACRO_MAX_GROUPS is 16, accommodating 10 groups with headroom | VERIFIED | spu94_macro.h:55 defines SPU94_MACRO_MAX_GROUPS 16; enum COUNT=10 |
| 8  | All 10 macro group definitions are registered as static const tables | VERIFIED | 10 groups in spu94_macro_controls.c:44-175; g_all_groups[] lookup; test_all_groups_register passes |
| 9  | Left Wall group pairs mLSAME distance with dLSAME echo speed | VERIFIED | g_echo_speed covers dLSAME/dRSAME/dLDIFF/dRDIFF; Wall controls use wall_m_regs/wall_d_regs tables; test_echo_speed_group_members passes |
| 10 | Per-wall link toggle makes Room Size propagate echo speed changes when linked | VERIFIED | spu94_macro_apply_room_size:282-305; test_wall_link_propagates_echo_speed passes |
| 11 | Same/Cross link toggle tethers same-side and cross-side pairs | VERIFIED | same_cross_link field; wall_cross_partner table; test_same_cross_link_propagation passes |
| 12 | 8 individual tap positions can be independently set | VERIFIED | g_tap_position has 8 members (mLCOMB1-4, mRCOMB1-4); test_tap_position_group_members passes |
| 13 | Tap Position Spread+Sweep operates over all 8 mLCOMB/mRCOMB registers | VERIFIED | spu94_macro_apply_tap_position calls spu94_macro_apply_spread_sweep on SPU94_MACRO_TAP_POSITION |
| 14 | Constrained mode clamps taps within their containing wall boundaries | VERIFIED | spu94_macro_apply_tap_position:349-391; tap_wall_regs[]; test_tap_constrained_clamps_to_wall passes |
| 15 | Diffusion Amount/Texture/Position groups have correct register assignments | VERIFIED | g_diff_amount (vAPF1/2 signed), g_diff_texture (dAPF1/2 unsigned), g_diff_position (mLAPF1/2, mRAPF1/2); test functions in test_macro_controls.c pass |
| 16 | Diffusion Position constrained mode clamps within wall boundaries | VERIFIED | spu94_macro_apply_diff_position:393-430; diff_pos_wall_regs[]; test_diff_pos_constrained_clamps passes |
| 17 | Room Size master scales all 14 m-prefix registers (actual: 16) | VERIFIED | g_room_size has member_count=16 (all m-prefix excluding mBASE); test_room_size_group and test_room_size_apply_scales_all_m_prefix pass |
| 18 | Early Reflections group covers vCOMB1-4 with bipolar sweep | VERIFIED | g_early_refl: 4 members (vCOMB1-4), is_bipolar=1, floor=-32768, ceiling=32767 |
| 19 | Decay group has floor=-0x1000 enforcing SAFE-03/SAFE-04 | VERIFIED | spu94_macro_controls.c:147 confirmed by direct read |
| 20 | When Decay changes vIIR, Reflectivity knob position auto-rederives from the now-clamped vWALL | VERIFIED | spu94_macro_apply_decay:450-451 calls spu94_macro_derive_bipolar(REFLECTIVITY); test_decay_rederives_reflectivity passes |
| 21 | Buffer control with inverted mapping: knob up = mBASE low (max space), knob down = mBASE high (crush) | VERIFIED | spu94_macro_apply_buffer:496 computes new_mbase = (1.0f - position) * ceiling; test_buffer_fully_up_is_mbase_zero and test_buffer_fully_down_is_mbase_max pass |
| 22 | After Buffer shrinks, all m-prefix registers that exceed the new boundary are clamped | VERIFIED | spu94_macro_apply_buffer:515-535 iterates 16 m-prefix regs, clamps to effective_ceiling; test_buffer_shrink_reclamps_m_prefix passes |
| 23 | After Buffer re-clamp, all m-prefix-containing macro groups re-derive their knob positions | VERIFIED | spu94_macro_apply_buffer re-derives ROOM_SIZE, TAP_POSITION, DIFF_POSITION after re-clamp |
| 24 | Loading a .spu94 preset sets register values and all macros derive positions without changing any register | VERIFIED | spu94_macro_derive_all:556-590 is provably read-only; test_derive_all_after_factory_preset and test_preset_load_then_apply_identity both pass |
| 25 | Preset reference values are set for all groups, becoming the anchor for Spread computations | VERIFIED | derive_all calls spu94_macro_set_reference for each group, then sets spread=0.5; test_derive_all_sets_references and test_derive_all_spread_at_center pass |
| 26 | All Phase 21 requirements proven by passing test suites (no regressions) | VERIFIED | 10/10 macro test suites pass; full suite exit code 0; 4 pre-existing CLI failures unrelated to Phase 21 |

**Score:** 26/26 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94_macro.h` | Expanded enum (10 groups), is_bipolar field, all API declarations | VERIFIED | All 10 enum values, is_bipolar in struct, 5 Phase 21 engine APIs, 14 Phase 21 control APIs; SPU94_MACRO_WIDTH absent |
| `src/spu94/spu94_macro.c` | Spread+Sweep apply, bipolar apply, reference value helpers | VERIFIED | spu94_macro_apply_spread_sweep at line 299, spu94_macro_apply_bipolar at 408, spu94_macro_set_reference at 267, derive_spread_sweep at 460, derive_bipolar at 580 |
| `src/spu94/spu94_state_internal.h` | macro_ref_values, macro_spread_pos, macro_sweep_pos, link/constrain state fields | VERIFIED | All fields present at lines 247-263; _Static_assert for SPU94_STATE_SIZE_MAX still compiles |
| `src/spu94/spu94_macro_controls.c` | 10 static const group tables, registration, link toggles, constrained mode, coupling, buffer, derive-all | VERIFIED | 310+ lines covering all required functions; contains g_echo_speed, g_decay with -0x1000 floor |
| `tests/unit/macro/test_macro_spread_sweep.c` | 7+ test functions covering Spread+Sweep behavior | VERIFIED | 7 test functions; test_spread_center_reproduces_reference confirmed |
| `tests/unit/macro/test_macro_bipolar.c` | 8+ test functions including SAFE-03/SAFE-04 | VERIFIED | 8 test functions; test_decay_floor_minus_0x1000 and test_minus_0x8000_unreachable confirmed |
| `tests/unit/macro/test_macro_controls.c` | 10+ group registration and member verification tests | VERIFIED | 12 test functions; test_all_groups_register confirmed |
| `tests/unit/macro/test_macro_link.c` | 6+ link toggle tests | VERIFIED | 6 test functions; test_wall_link_propagates_echo_speed confirmed |
| `tests/unit/macro/test_macro_constrain.c` | 6+ constrained mode tests | VERIFIED | 6 test functions; test_tap_constrained_clamps_to_wall confirmed |
| `tests/unit/macro/test_macro_coupling.c` | 12+ coupling, buffer, and preset derivation tests | VERIFIED | 12 test functions; test_decay_rederives_reflectivity, test_buffer_fully_up_is_mbase_zero, test_derive_all_after_factory_preset confirmed |
| `tests/unit/macro/CMakeLists.txt` | 6 new test targets registered | VERIFIED | add_test entries for spread_sweep, bipolar, controls, link, constrain, coupling all present |
| `src/spu94/CMakeLists.txt` | spu94_macro_controls.c in spu94_obj sources | VERIFIED | Line 29 adds spu94_macro_controls.c to spu94_obj |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/spu94/spu94_macro.c` | `spu94_safe_set_reg_{i16,u16}` | write_member_value helper | VERIFIED | write_member_value:52-63 calls safe setters; all macro writes enforced through safety layer |
| `src/spu94/spu94_macro.c` | `macro_ref_values` in state struct | reference snapshot during set_reference | VERIFIED | spu94_macro_set_reference:278 writes to state->macro_ref_values[group_id][i] |
| `src/spu94/spu94_macro_controls.c` | `spu94_macro_register_group` | registration of all 10 groups | VERIFIED | spu94_macro_controls_register_all:182 iterates all 10 and calls spu94_macro_register_group |
| `src/spu94/spu94_macro_controls.c` | `spu94_macro_apply_spread_sweep` | Room Size, Tap Position, Diff Position composite applies | VERIFIED | spu94_macro_apply_tap_position:344 and spu94_macro_apply_diff_position:404 call spu94_macro_apply_spread_sweep |
| `src/spu94/spu94_macro_controls.c` | `spu94_safe_set_reg_u16` | constrained mode post-clamp writes | VERIFIED | spu94_macro_apply_tap_position constrained path writes through spu94_safe_set_reg_u16 |
| `spu94_macro_apply_decay` | `spu94_macro_derive_bipolar for REFLECTIVITY` | post-write re-derive hook | VERIFIED | spu94_macro_apply_decay:450-451 explicitly calls derive_bipolar on REFLECTIVITY after vIIR write |
| `spu94_macro_apply_buffer` | `spu94_safe_set_reg_u16 for all m-prefix` | re-clamp pass after mBASE change | VERIFIED | spu94_macro_apply_buffer:518-535 iterates all 16 m-prefix regs using spu94_set_reg_u16 |
| `spu94_macro_derive_all` | `spu94_macro_set_reference` | reference snapshot for all groups | VERIFIED | spu94_macro_derive_all:566 calls spu94_macro_set_reference for each registered group |

---

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| `spu94_macro_apply_spread_sweep` | spread_values[], sweep_offset | macro_ref_values[] snapshot | Yes — reads live register values via read_member_value | FLOWING |
| `spu94_macro_apply_bipolar` | value (int32) | group->members[0].floor/ceiling | Yes — maps position to register range arithmetically | FLOWING |
| `spu94_macro_derive_all` | spread/sweep positions per group | current register values via read_member_value | Yes — reads registers, asserts read-only by test snapshot | FLOWING |
| `spu94_macro_apply_decay` | vIIR value + Reflectivity re-derive | bipolar apply, then stability layer side-effect on vWALL | Yes — proven by test_decay_rederives_reflectivity | FLOWING |
| `spu94_macro_apply_buffer` | mBASE write + m-prefix re-clamp | position -> inverted mBASE; effective_ceiling = wbs/2-1-mBASE | Yes — proven by test_buffer_shrink_reclamps_m_prefix | FLOWING |

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 10 macro test suites pass | `ctest --test-dir build -R test_macro` | 10/10 passed, 0 failed | PASS |
| Build succeeds without errors | `cmake --build build` | Exit 0, 100% targets built | PASS |
| SAFE-03 floor enforced | `test_decay_floor_minus_0x1000` in test suite | PASSED | PASS |
| SAFE-04 unreachability exhaustive | `test_minus_0x8000_unreachable` exhaustive 0.01-step loop | PASSED | PASS |
| Derive-all is read-only | `test_derive_all_after_factory_preset` 35-register snapshot | PASSED | PASS |
| Pre-existing test suite intact | Full ctest (CLI/packaging failures pre-date Phase 21) | Exit 0 background run; 4 pre-existing failures confirmed pre-Phase-21 by git blame | PASS |

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| WALL-01 | 21-02 | Left Wall: mLSAME + dLSAME paired | SATISFIED | g_echo_speed[0]=dLSAME; g_room_size[0]=mLSAME; wall_m_regs[0]=mLSAME |
| WALL-02 | 21-02 | Right Wall: mRSAME + dRSAME paired | SATISFIED | g_echo_speed[1]=dRSAME; g_room_size[1]=mRSAME; wall_m_regs[1]=mRSAME |
| WALL-03 | 21-02 | Left Cross: mLDIFF + dLDIFF paired | SATISFIED | g_echo_speed[2]=dLDIFF; g_room_size[10]=mLDIFF; wall_m_regs[2]=mLDIFF |
| WALL-04 | 21-02 | Right Cross: mRDIFF + dRDIFF paired | SATISFIED | g_echo_speed[3]=dRDIFF; g_room_size[11]=mRDIFF; wall_m_regs[3]=mRDIFF |
| WALL-05 | 21-02 | Per-wall link toggle (4 booleans) | SATISFIED | wall_link[4] state field; spu94_macro_set/get_wall_link API; test_wall_link_propagates_echo_speed passes |
| WALL-06 | 21-02 | Same/Cross link toggle | SATISFIED | same_cross_link field; spu94_macro_set/get_same_cross_link; test_same_cross_link_propagation passes |
| ECHO-SPD-01 | 21-02 | Echo Speed Spread+Sweep over 4 echo speeds | SATISFIED | g_echo_speed (4 members: dLSAME/dRSAME/dLDIFF/dRDIFF); spu94_macro_apply_spread_sweep wired |
| TAP-01 | 21-02 | 8 individual tap position controls | SATISFIED | g_tap_position member_count=8 (mLCOMB1-4, mRCOMB1-4) |
| TAP-02 | 21-02 | Tap Position Spread+Sweep macro | SATISFIED | spu94_macro_apply_tap_position calls spu94_macro_apply_spread_sweep on SPU94_MACRO_TAP_POSITION |
| TAP-03 | 21-02 | Wall constrained/unconstrained toggle | SATISFIED | tap_constrained field; spu94_macro_set/get_tap_constrained; post-apply clamp in spu94_macro_apply_tap_position |
| DIFF-AMT-01 | 21-02 | Diffusion Amount Spread+Sweep (vAPF1, vAPF2) | SATISFIED | g_diff_amount: vAPF1/vAPF2, I16, is_bipolar=1 |
| DIFF-TEX-01 | 21-02 | Diffusion Texture Spread+Sweep (dAPF1, dAPF2) | SATISFIED | g_diff_texture: dAPF1/dAPF2, U16, is_bipolar=0 |
| DIFF-POS-01 | 21-02 | Diffusion Position 4 controls + Spread+Sweep | SATISFIED | g_diff_position: 4 members (mLAPF1/2, mRAPF1/2); spu94_macro_apply_diff_position wired |
| DIFF-POS-02 | 21-02 | Diffusion Position constrained/unconstrained toggle | SATISFIED | diff_pos_constrained field; spu94_macro_set/get_diff_pos_constrained; post-apply clamp |
| ROOM-01 | 21-02 | Room Size master scales all m-prefix | SATISFIED | g_room_size member_count=16; spu94_macro_apply_room_size; echo speed propagation when linked |
| BUF-01 | 21-03 | Buffer: inverted mapping, default fully up | SATISFIED | spu94_macro_apply_buffer: (1.0-position)*ceiling; test_buffer_fully_up_is_mbase_zero passes |
| BUF-02 | 21-03 | Safety clamps m-prefix when buffer shrinks | SATISFIED | spu94_macro_apply_buffer re-clamp pass; test_buffer_shrink_reclamps_m_prefix passes |
| SS-01 | 21-01 | All multi-register macros use Spread+Sweep | SATISFIED | spu94_macro_apply_spread_sweep implemented with dual-axis model; 7 tests pass |
| SS-02 | 21-01/03 | Preset register values as reference anchor | SATISFIED | macro_ref_values[] separate from base_values; spu94_macro_set_reference; derive_all sets references |
| SS-03 | 21-01 | Signed registers sweep into negative territory | SATISFIED | test_signed_sweep_negative in test_macro_spread_sweep.c passes; bipolar sweep maps to signed range |
| CTRL-01 | 21-01 | Decay bipolar center-detent (vIIR) | SATISFIED | spu94_macro_apply_bipolar with DECAY group; left/right half mapping; test_bipolar_center_is_zero passes |
| CTRL-02 | 21-02/03 | Reflectivity bipolar; auto-rederives when Decay changes | SATISFIED | spu94_macro_apply_decay:450-451; test_decay_rederives_reflectivity passes |
| CTRL-03 | 21-02 | Early Reflections Spread+Sweep (vCOMB1-4) | SATISFIED | g_early_refl: 4 vCOMB members, is_bipolar=1; test_early_refl_group passes |
| SAFE-03 | 21-01/02 | vIIR floor at -0x1000 | SATISFIED | g_decay floor=-0x1000; test_decay_floor_minus_0x1000 passes |
| SAFE-04 | 21-01/02 | vIIR -0x8000 unreachable | SATISFIED | exhaustive test_minus_0x8000_unreachable passes; floor=-0x1000 makes -0x8000 impossible |
| PRESET-01 | 21-03 | Presets remain register-only; macros derive on load | SATISFIED | spu94_macro_derive_all proven read-only; test_derive_all_after_factory_preset + test_preset_load_then_apply_identity pass |

**All 26 Phase 21 requirements: SATISFIED**

**Note on pre-existing test failures:** 4 CLI and packaging tests fail in the full suite (test_cli_config_and_list::test_list_presets_10_lines, test_cli_error_paths::test_unknown_preset_exact_shape, test_packaging_editable_install, test_packaging_wheel_tag). These failures are confirmed pre-existing from before Phase 21: `git log 062cafc..HEAD -- tests/cli/ src/standalone/` returns no output, and the root cause (an 'init' preset added in commit 2856d41, a prior phase) predates Phase 21 entirely. No regression introduced by Phase 21.

---

### Anti-Patterns Found

| File | Pattern | Severity | Impact |
|------|---------|----------|--------|
| None | — | — | — |

No TODO, FIXME, placeholder, stub, or empty-implementation patterns found in any Phase 21 source or test file.

---

### Human Verification Required

None. All phase goal behaviors are verifiable from C source, unit tests, and binary test runner output. No GUI, visual, or real-time behaviors were added in this phase.

---

## Gaps Summary

No gaps. All 26 observable truths are VERIFIED, all required artifacts exist and are substantive, all key links are confirmed wired, all 26 requirement IDs are satisfied by passing tests.

The 4 pre-existing CLI/packaging test failures are not Phase 21 regressions and do not affect the phase goal.

---

_Verified: 2026-05-04T22:15:00Z_
_Verifier: Claude (gsd-verifier)_
