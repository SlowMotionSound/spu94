---
phase: 22-echo-speed-diffusion-snap
verified: 2026-05-05T03:30:00Z
status: passed
score: 7/7
overrides_applied: 0
---

# Phase 22: Echo Speed + Diffusion Snap Verification Report

**Phase Goal:** Echo Speed and Diffusion Texture controls integrate with the v1.5 tempo system via a Sync/Free modal toggle -- Free mode uses continuous Phase 21 Spread+Sweep, Sync mode quantizes all 4 echo speed registers to subdivision table positions with discrete Sweep (shift pattern faster/slower), Spread (unison to polyrhythmic), and Rotate (Euclidean permutation) transforms operating on per-register dropdown selections as the reference anchor
**Verified:** 2026-05-05T03:30:00Z
**Status:** PASSED
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Binary Sync/Free toggle switches the entire echo speed section between continuous and discrete subdivision-quantized operation -- all 4 registers toggle together | VERIFIED | `spu94_snap_set_echo_speed_sync` at spu94_snap.c:95-125 normalizes sync arg (T-22-05), handles Sync->Free (unbinds all 4 via `spu94_set_binding_fixed`, re-anchors Phase 21 reference) and Free->Sync (calls `spu94_set_subdivision` per register with valid dropdowns). Composite apply at line 526-541 routes Free->macro, Sync->discrete. 11 toggle tests all pass (test_snap_toggle). |
| 2 | In Sync mode, each register has a per-register subdivision dropdown selection that becomes the reference state for Sweep/Spread/Rotate transforms | VERIFIED | `snap_echo_sub[4]` in state struct (spu94_state_internal.h:282). Set/get API at spu94_snap.c:174-192 with bounds checking. `compute_spread_indices` at line 272 reads `ref_sub` (which is `state->snap_echo_sub`) as transform input. 0xFF sentinel for unset. Init/reset sets 0xFF (spu94_state.c:99-100, 142-143). |
| 3 | Sweep shifts all 4 registers up/down the duration-sorted subdivision table together maintaining relative spacing; dynamic range mapping ensures no dead zones | VERIFIED | `compute_sweep_offset` at spu94_snap.c:324-351 computes headroom from min/max spread positions, maps sweep [0,1] to offset within available range (D-09). `apply_sweep_to_indices` at line 360-368 adds uniform offset with clamping. 6 sweep tests pass including `test_sweep_dynamic_range_from_spread` and `test_sweep_full_spread_no_range` (D-10). |
| 4 | Spread controls unison (all same subdivision) to polyrhythmic (maximum separation); Spread=0.5 reproduces dropdown selections | VERIFIED | `compute_spread_indices` at spu94_snap.c:272-306: converts to sorted positions, computes centroid (mean), applies factor=spread*2.0 (0.5 maps to factor 1.0 = identity). Clamped to [0,14]. 5 spread tests pass including `test_spread_zero_produces_unison`, `test_spread_half_reproduces_reference`, `test_spread_full_doubles_spacing`. |
| 5 | Rotate performs Euclidean circular permutation of subdivision assignments across the 4 echo paths | VERIFIED | `compute_rotate_indices` at spu94_snap.c:386-399: maps float [0,1] to int [0,3] at 0.25 thresholds, applies `out[i] = in[(i+rot) % 4]`. 6 rotate tests pass including `test_rotate_circular_permutation` (verifies multiset preservation) and `test_rotate_at_unison_noop` (Pitfall 2). |
| 6 | dAPF1 and dAPF2 have independent Sync/Free toggle with per-register dropdown -- no Sweep/Spread/Rotate macros | VERIFIED | `spu94_snap_set_diff_texture_sync` at spu94_snap.c:133-168 handles dAPF1/dAPF2 toggle independently from echo speed. `snap_diff_sub[2]` in state struct. `spu94_snap_apply_diff_texture` at line 505-519 applies dropdown subdivisions only (no transforms). `spu94_snap_diff_texture_apply` composite at line 543-558 routes Free->Phase21 macro, Sync->apply dropdown. 6 diffusion tests pass. |
| 7 | BPM changes auto-update all Sync-mode registers to their current effective subdivision's new sample count | VERIFIED | `spu94_snap_apply_echo_speed` calls `spu94_set_subdivision` at line 457 which writes TRANSFORMED subdivision to `tempo_bind_sub[]`. Existing auto-resnap loop in spu94_tempo.c reads `tempo_bind_sub[]` on BPM change, re-computing sample counts. Test `test_bpm_resnaps_transformed_not_reference` explicitly verifies Pitfall 3 prevention. Test `test_bpm_change_resnaps_diff_sync` verifies diffusion resnap. 5 BPM tests pass. |

**Score:** 7/7 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94_snap.h` | Public API for toggle, assignment, transforms, composite apply | VERIFIED | 137 lines, declares all 12 functions, 2 extern tables, 2 count macros. Wired: included by spu94_snap.c and all 6 test files. |
| `src/spu94/spu94_snap.c` | Toggle implementation, diffusion apply, sorted tables, transform engine | VERIFIED | 559 lines, complete Spread->Sweep->Rotate chain, composite apply routing, diffusion snap. Contains `compute_spread_indices`, `compute_sweep_offset`, `apply_sweep_to_indices`, `compute_rotate_indices`. Wired: compiled via CMakeLists.txt line 30. |
| `src/spu94/spu94_state_internal.h` | State fields for echo_speed_sync, diff_texture_sync, snap_echo_sub, snap_diff_sub, knob positions | VERIFIED | Phase 22 block at lines 265-288 with all 7 planned fields (2 toggles, 6 dropdown bytes, 12 bytes knob positions = 20 bytes). |
| `tests/unit/snap/test_snap_toggle.c` | Tests for Sync/Free mode switching and state preservation | VERIFIED | 283 lines, 11 test functions including `test_sync_to_free_preserves_values` and `test_free_to_sync_quantizes_to_dropdown`. All pass. |
| `tests/unit/snap/test_snap_diffusion.c` | Tests for dAPF1/dAPF2 Sync/Free + dropdown assignment | VERIFIED | 206 lines, 6 test functions including `test_diff_sync_sets_subdivision` and `test_diff_composite_free_delegates`. All pass. |
| `tests/unit/snap/test_snap_spread.c` | Tests for Spread from unison to polyrhythmic | VERIFIED | 234 lines, 5 test functions including `test_spread_zero_produces_unison` and `test_spread_half_reproduces_reference`. All pass. |
| `tests/unit/snap/test_snap_sweep.c` | Tests for discrete Sweep transform with dynamic range | VERIFIED | 309 lines, 6 test functions including `test_sweep_shifts_all_registers` and `test_sweep_dynamic_range_from_spread`. All pass. |
| `tests/unit/snap/test_snap_rotate.c` | Tests for Euclidean rotation of assignments | VERIFIED | 287 lines, 6 test functions including `test_rotate_circular_permutation` and `test_rotate_at_unison_noop`. All pass. |
| `tests/unit/snap/test_snap_bpm.c` | Tests for auto-resnap in Sync mode on BPM change | VERIFIED | 304 lines, 5 test functions including `test_bpm_change_resnaps_sync_registers` and `test_bpm_resnaps_transformed_not_reference`. All pass. |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| spu94_snap.c | spu94_set_subdivision | Calls existing tempo API for grid binding | WIRED | Called at lines 116, 153, 457, 514. Writes to registers and updates tempo_bind_sub for auto-resnap. |
| spu94_snap.c | spu94_set_binding_fixed | Unbinds registers when switching to Free mode | WIRED | Called at lines 106 (echo) and 144 (diffusion) during Sync->Free transition. |
| spu94_snap.c | spu94_macro_set_reference | Re-anchors Spread+Sweep after Sync->Free transition | WIRED | Called at lines 108 (ECHO_SPEED) and 146 (DIFF_TEXTURE). |
| spu94_snap.c (compute_spread_indices) | spu94_subdivision_sorted_by_duration | Operates on sorted positions for musical sweep order | WIRED | Line 283: converts via `spu94_subdivision_to_sorted_pos`. Line 456: converts back via `spu94_subdivision_sorted_by_duration`. |
| spu94_snap.c (spu94_snap_apply_echo_speed) | spu94_set_subdivision | Writes final transformed indices as GRID bindings | WIRED | Lines 452-459: sets tempo_writing=1, calls set_subdivision for each register, sets tempo_writing=0. |
| spu94_snap.c | tempo_bind_sub | Updates binding subdivisions for auto-resnap (Pitfall 3) | WIRED | spu94_set_subdivision internally writes to tempo_bind_sub[reg], so transformed values flow through. Test `test_bpm_resnaps_transformed_not_reference` confirms. |
| spu94_snap.c | spu94_macro_apply_spread_sweep | Free mode delegation to Phase 21 | WIRED | Lines 535 (ECHO_SPEED) and 551 (DIFF_TEXTURE). |
| CMakeLists.txt (src) | spu94_snap.c | Added to spu94_obj sources | WIRED | Line 30 of src/spu94/CMakeLists.txt. |
| CMakeLists.txt (tests) | snap/ | add_subdirectory(snap) | WIRED | Line 28 of tests/unit/CMakeLists.txt. |

### Data-Flow Trace (Level 4)

Not applicable -- this is a C core DSP module with no UI rendering. Data flows are verified through tests exercising actual register writes and reads (not rendering dynamic data).

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 6 snap test suites pass | `ctest -R snap --output-on-failure` | 7/7 passed (includes pre-existing test_tempo_snap), 0 failures | PASS |
| Macro/tempo/safety tests show no regressions | `ctest -R "macro\|tempo\|safety\|preset\|state\|register"` | 38/38 passed, 0 failures | PASS |
| Module compiles and exports | `spu94_snap.c` compiled into `spu94_static` library | Build succeeds | PASS |
| All commits exist in git | `git log --oneline` for 6 commit hashes | All 6 verified | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-----------|-------------|--------|----------|
| SNAP-01 | 22-01, 22-02 | Per-register magnetic snap -- each echo speed register has independent snap behavior (Free / Global / specific subdivision) | SATISFIED | Sync/Free toggle + per-register dropdown + Sweep/Spread/Rotate transforms all operating on per-register subdivision assignments. 39 tests cover all behaviors. |
| SNAP-02 | 22-01, 22-02 | Smooth knob travel between snap points -- registers scale freely, snapping only within a pull zone near each subdivision grid value | SATISFIED | Implemented via binary Sync/Free toggle per D-01: Free mode = continuous Phase 21 Spread+Sweep, Sync mode = discrete subdivision table positions. CONTEXT.md D-01 clarifies this is a toggle, not magnetic pull. Composite apply routes correctly. |
| SNAP-03 | 22-01 | dAPF1 and dAPF2 have independent snap behavior grouped with Diffusion controls | SATISFIED | Separate `diff_texture_sync` toggle, `snap_diff_sub[2]` array, `spu94_snap_apply_diff_texture`, and composite `spu94_snap_diff_texture_apply`. 6 diffusion tests verify. |

No orphaned requirements. REQUIREMENTS.md traceability table maps SNAP-01, SNAP-02, SNAP-03 to Phase 22, all marked Complete.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| include/spu94/spu94_snap.h | 95, 102 | Comments say "Stub in Plan 01 -- implemented in Plan 02" on fully-implemented functions | INFO | Stale documentation. Functions are fully implemented in spu94_snap.c. No functional impact. |

### Human Verification Required

None. All behaviors verified programmatically through 39 unit tests covering toggle transitions, transform math, BPM resnap, and mode-aware composite routing.

### Gaps Summary

No gaps found. All 7 ROADMAP success criteria verified against codebase evidence. All 3 requirements (SNAP-01, SNAP-02, SNAP-03) satisfied. 39 tests across 6 suites all passing. Zero regressions in the broader test suite. 6 commits verified. All key links wired. No stubs remain in functional code.

---

_Verified: 2026-05-05T03:30:00Z_
_Verifier: Claude (gsd-verifier)_
