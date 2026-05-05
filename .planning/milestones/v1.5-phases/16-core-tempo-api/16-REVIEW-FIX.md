---
phase: 16-core-tempo-api
fixed_at: 2026-05-03T13:00:00Z
review_path: .planning/phases/16-core-tempo-api/16-REVIEW.md
iteration: 1
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 16: Code Review Fix Report

**Fixed at:** 2026-05-03T13:00:00Z
**Source review:** .planning/phases/16-core-tempo-api/16-REVIEW.md
**Iteration:** 1

**Summary:**
- Findings in scope: 5
- Fixed: 5
- Skipped: 0

## Fixed Issues

### CR-01: Process-global re-entrancy guard breaks multi-instance correctness

**Files modified:** `src/spu94/spu94_state_internal.h`, `src/spu94/spu94_tempo.c`
**Commit:** 75c96e6
**Applied fix:** Moved `spu94_tempo_writing` from a file-scope `static int` to a per-instance `uint8_t tempo_writing` field in `struct spu94_state`. All 5 references in spu94_tempo.c (2 set-to-1, 2 set-to-0, 1 guard-check in the hook) now use `state->tempo_writing`. The static variable and its declaration were removed. The field is zero-initialized by spu94_init's byte-loop, matching the old static's default of 0.

### WR-01: Zero-sample delay accepted as valid at extreme BPM values

**Files modified:** `src/spu94/spu94_tempo.c`
**Commit:** 7de8115
**Applied fix:** Added `samples > 0` check to `spu94_subdivision_valid` (returns 0 for zero-sample results), `samples == 0` early-return in `spu94_set_subdivision` (returns SPU94_INVALID_ARG), and `samples == 0` guard in the auto-resnap loop of `spu94_set_tempo` (transitions to FIXED, same as overflow). This prevents writing musically-meaningless zero-length delay taps at extreme BPMs.

### WR-02: Snap tests verify API success but never assert actual register values

**Files modified:** `tests/unit/tempo/test_tempo_snap.c`
**Commit:** 9dc3f06
**Applied fix:** Added `spu94_tick(s)` + `TEST_ASSERT_EQUAL_UINT16` readback assertions to all four known-vector tests. Each test now commits the TICK_LATCHED write and verifies the hardware register contains the expected value: 120 BPM 1/4 -> dAPF1 = 2756, 60 BPM 1/1 -> dLSAME = 22050, 90 BPM 1/8 -> dRDIFF = 1837, 140 BPM 1/16t -> dAPF2 = 393. Also added `TEST_ASSERT_NOT_NULL(s)` to the three tests that were missing it.

### WR-03: Hardcoded array size 10 in struct will silently desync from enum growth

**Files modified:** `src/spu94/spu94_state_internal.h`
**Commit:** 34ebfa3
**Applied fix:** Replaced literal `[10]` with `[SPU94_TEMPO_REG__COUNT]` in all three parallel arrays: `tempo_bind_state`, `tempo_bind_sub`, `tempo_bind_ref_bpm`. If a future phase adds a tempo register and increments the enum sentinel, the arrays grow automatically and for-loops using `SPU94_TEMPO_REG__COUNT` stay in bounds.

### WR-04: Virtual comb resnap reads TICK_LATCHED registers that may hold stale values

**Files modified:** `src/spu94/spu94_tempo.c`
**Commit:** c7ea9ba
**Applied fix:** Added `get_latest_u16()` static helper that checks `pending_mask` and returns the pending value if dirty, active value otherwise. Replaced all 8 reference-register reads (4 in `spu94_set_tempo` resnap loop, 4 in `spu94_set_subdivision` comb path) from `spu94_get_reg_u16()` to `get_latest_u16()`. This ensures comb geometry checks see freshly-loaded preset values even before the first `spu94_tick()` commit.

---

_Fixed: 2026-05-03T13:00:00Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
