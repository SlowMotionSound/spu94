---
phase: 22-echo-speed-diffusion-snap
reviewed: 2026-05-04T19:45:00Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - include/spu94/spu94_snap.h
  - src/spu94/spu94_snap.c
  - src/spu94/spu94_state_internal.h
  - src/spu94/spu94_state.c
  - src/spu94/CMakeLists.txt
  - tests/unit/snap/test_snap_toggle.c
  - tests/unit/snap/test_snap_diffusion.c
  - tests/unit/snap/test_snap_spread.c
  - tests/unit/snap/test_snap_sweep.c
  - tests/unit/snap/test_snap_rotate.c
  - tests/unit/snap/test_snap_bpm.c
findings:
  critical: 0
  warning: 3
  info: 2
  total: 5
status: issues_found
---

# Phase 22: Code Review Report

**Reviewed:** 2026-05-04T19:45:00Z
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

## Summary

The Phase 22 snap module implements Sync/Free toggle, duration-sorted subdivision tables, and discrete Sweep/Spread/Rotate transforms for tempo-locked echo rhythms. The code is well-structured with thorough input validation on NULL state pointers and register index bounds. The lookup tables are correct (verified via manual trace of all 15 enum-to-sorted-position round-trips). All 39 tests pass. The Spread/Sweep/Rotate transform chain is mathematically sound with proper clamping at table bounds.

Three warnings were found: uninitialized Sync-mode knob positions create an inconsistency after Free-to-Sync toggle, the subdivision setter accepts arbitrary values without range validation, and NaN float inputs pass through clampf unfiltered (matching codebase-wide convention but still a robustness gap). Two minor info-level items relate to magic numbers and an unused math.h include.

## Warnings

### WR-01: Sync-mode knob positions not initialized on Free-to-Sync toggle

**File:** `src/spu94/spu94_snap.c:109-121`
**Issue:** When `spu94_snap_set_echo_speed_sync` transitions Free-to-Sync, it calls `spu94_set_subdivision` for each register but does not set `snap_spread_pos`, `snap_sweep_pos`, or `snap_rotate_pos` to their identity values (0.5, 0.5, 0.0). These fields are zero-initialized at `spu94_init` (all 0.0f). If a GUI calls `spu94_snap_derive_echo_speed` after toggling to Sync mode but before the first `spu94_snap_apply_echo_speed`, it receives (0.0, 0.0, 0.0) -- spread=0 means "unison" and sweep=0 means "shift all to longest", which is inconsistent with the register state that actually holds the dropdown reference values (identity = spread 0.5, sweep 0.5). The same issue applies to `spu94_reset` which zero-fills the struct without setting these to identity.

**Fix:** Set identity positions in the Free-to-Sync transition:
```c
} else if (!was_sync && new_sync) {
    /* Free -> Sync: snap each register to its dropdown subdivision. */
    state->tempo_writing = 1;
    for (int i = 0; i < SPU94_SNAP_ECHO_COUNT; i++) {
        uint8_t sub = state->snap_echo_sub[i];
        if (sub < SPU94_SUBDIVISION__COUNT) {
            spu94_set_subdivision(state, echo_tempo_regs[i],
                                 (spu94_subdivision_t)sub);
        }
    }
    state->tempo_writing = 0;
    /* Initialize knob positions to identity (T-22-derive) */
    state->snap_spread_pos = 0.5f;
    state->snap_sweep_pos  = 0.5f;
    state->snap_rotate_pos = 0.0f;
}
```

### WR-02: Missing input validation on subdivision parameter in setters

**File:** `src/spu94/spu94_snap.c:174-183` and `src/spu94/spu94_snap.c:194-203`
**Issue:** `spu94_snap_set_echo_subdivision` and `spu94_snap_set_diff_subdivision` validate `reg_index` bounds but do not validate the `sub` parameter against `SPU94_SUBDIVISION__COUNT`. Callers can store arbitrary uint8_t values (e.g., 200) that are neither a valid subdivision (0-14) nor the sentinel (0xFF). Downstream code treats any value >= 15 as "unset", so there is no out-of-bounds read, but the API silently accepts invalid input. A caller passing an erroneous value (like 16 when they meant 14) would get silent no-op behavior instead of an error return that would help them diagnose the mistake.

**Fix:** Add range validation:
```c
spu94_result_t spu94_snap_set_echo_subdivision(spu94_state *state,
                                                int reg_index,
                                                spu94_subdivision_t sub)
{
    if (state == NULL) return SPU94_INVALID_STATE;
    if (reg_index < 0 || reg_index >= SPU94_SNAP_ECHO_COUNT)
        return SPU94_INVALID_ARG;
    if ((uint8_t)sub >= SPU94_SUBDIVISION__COUNT)
        return SPU94_INVALID_ARG;

    state->snap_echo_sub[reg_index] = (uint8_t)sub;
    return SPU94_OK;
}
```
Apply the same change to `spu94_snap_set_diff_subdivision`.

### WR-03: NaN float inputs produce undefined behavior in roundf_to_int

**File:** `src/spu94/spu94_snap.c:248-252`
**Issue:** The `clampf` helper does not filter NaN (IEEE 754 NaN comparisons always return false, so `NaN < 0.0f` and `NaN > 1.0f` are both false, and `clampf` returns NaN unchanged). NaN then propagates to `roundf_to_int` where `(int)(NaN + 0.5f)` is undefined behavior per the C standard (6.3.1.4: if the floating value cannot be represented in the integer type, the behavior is undefined). The same issue affects `compute_rotate_indices` where `(int)(rotate * 4.0f)` with NaN rotate is also UB. This matches the codebase-wide convention (Phase 20/21 macro engine also does not check for NaN), but the snap module adds a new float-to-int cast pathway that is vulnerable.

**Fix:** Guard `clampf` against NaN, or add a NaN check at the function entry:
```c
static float clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;  /* catches NaN: !(NaN >= lo) is true */
    if (v > hi) return hi;
    return v;
}
```

## Info

### IN-01: Magic numbers for snap array sizes in spu94_state.c and spu94_state_internal.h

**File:** `src/spu94/spu94_state.c:99-100` and `src/spu94/spu94_state_internal.h:282-283`
**Issue:** The state struct declares `snap_echo_sub[4]` and `snap_diff_sub[2]` with literal array sizes, and `spu94_init`/`spu94_reset` use literal loop bounds `i < 4` and `i < 2` instead of referencing `SPU94_SNAP_ECHO_COUNT` and `SPU94_SNAP_DIFF_COUNT`. If the count defines change, these would silently go out of sync. The header defines are not available because `spu94_state.c` does not include `spu94_snap.h`, and the internal header does not include it either.

**Fix:** Either include `spu94_snap.h` in `spu94_state_internal.h` and use the defines for array sizes, or add a `_Static_assert` in `spu94_snap.c` to bind the sizes:
```c
_Static_assert(SPU94_SNAP_ECHO_COUNT == 4, "update snap_echo_sub array size");
_Static_assert(SPU94_SNAP_DIFF_COUNT == 2, "update snap_diff_sub array size");
```

### IN-02: Unused math.h include in test_snap_spread.c

**File:** `tests/unit/snap/test_snap_spread.c:22`
**Issue:** `#include <math.h>` is included but no math.h functions are used in the test file. All floating-point operations use inline arithmetic.

**Fix:** Remove the include:
```c
// Delete line 22: #include <math.h>
```

---

_Reviewed: 2026-05-04T19:45:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
