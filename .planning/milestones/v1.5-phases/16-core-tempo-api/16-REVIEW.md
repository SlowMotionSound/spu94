---
phase: 16-core-tempo-api
reviewed: 2026-05-03T12:15:00Z
depth: deep
files_reviewed: 8
files_reviewed_list:
  - src/spu94/spu94_tempo.c
  - include/spu94/spu94.h
  - include/spu94/spu94_registers.h
  - tests/unit/tempo/test_tempo_basic.c
  - tests/unit/tempo/test_tempo_snap.c
  - tests/unit/tempo/test_tempo_binding.c
  - tests/unit/tempo/test_tempo_comb.c
  - tests/unit/tempo/CMakeLists.txt
findings:
  critical: 1
  warning: 4
  info: 2
  total: 7
status: issues_found
---

# Phase 16: Code Review Report

**Reviewed:** 2026-05-03T12:15:00Z
**Depth:** deep
**Files Reviewed:** 8
**Status:** issues_found

## Summary

Phase 16 adds a well-structured tempo-synced delay tap system: BPM state, a 15-entry subdivision ratio table, binding state tracking (fixed/grid/proportional), auto-resnap on BPM change, virtual comb delay geometry, and a write-interception hook for binding transitions. The integer arithmetic is correctly bounded (all intermediates fit uint32) and the subdivision ratios are musically correct (dotted = 1.5x, triplet = 2/3x confirmed for all 5 base values). Known test vectors match hand-computed results.

One critical issue found: the re-entrancy guard is a process-global static variable, which corrupts binding state when multiple spu94_state instances operate concurrently (even on the same thread with interleaved calls). Four warnings address a zero-sample-delay edge case, missing register-value assertions in the snap tests, hardcoded array sizes that will desync from the enum if it grows, and comb resnap reading stale reference values before tick commit.

## Critical Issues

### CR-01: Process-global re-entrancy guard breaks multi-instance correctness

**File:** `src/spu94/spu94_tempo.c:69`
**Issue:** `spu94_tempo_writing` is declared as `static int` at file scope, making it a single process-global flag shared across all spu94_state instances. When instance A calls `spu94_set_tempo()` or `spu94_set_subdivision()`, it sets the guard to 1. If instance B has `spu94_set_reg_u16()` called during this window (even on the same thread via interleaved operations), the `spu94_tempo_on_reg_write()` hook sees `spu94_tempo_writing == 1` and silently skips the GRID-to-PROPORTIONAL binding state transition for instance B. This corrupts B's binding state without any error signal.

The public API header (`spu94.h` line 24) states "A spu94_state is NOT thread-safe. Concurrent access from multiple threads requires external synchronization." This implies that two *separate* states used from two *separate* threads (or even sequentially interleaved on one thread) should be independent. The process-global guard violates that contract.

A DAW plugin host instantiating two SPU-94 reverb instances on separate mixer channels would hit this. Even without threads, a single-threaded host alternating operations between two states can trigger the bug.

**Fix:** Move the guard into `struct spu94_state` as a per-instance field:

```c
/* In spu94_state_internal.h, add to struct spu94_state: */
uint8_t        tempo_writing;  /* re-entrancy guard for tempo snap writes */

/* In spu94_tempo.c, replace all references to the static: */
/* Before: spu94_tempo_writing = 1; */
/* After:  state->tempo_writing = 1; */

/* In spu94_tempo_on_reg_write: */
/* Before: if (spu94_tempo_writing) return; */
/* After:  if (state->tempo_writing) return; */
```

Delete the `static int spu94_tempo_writing = 0;` line entirely.

## Warnings

### WR-01: Zero-sample delay accepted as valid at extreme BPM values

**File:** `src/spu94/spu94_tempo.c:122-129`
**Issue:** `spu94_compute_delay_samples()` can return 0 when `bpm * denominator > 60 * 22050 * numerator` due to integer truncation. This occurs for the 1/16 triplet subdivision (num=1, den=24) at BPM >= 55126. The `spu94_subdivision_valid()` function checks only `samples <= UINT16_MAX`, so it reports 0-sample delays as valid. `spu94_set_subdivision()` would then write 0 to a d-prefix delay register, producing a zero-length delay tap (no effect). For virtual combs, `ref - 0 = ref`, so the comb tap collapses to the reference position (degenerate).

While not a crash or corruption, accepting a 0-sample delay as "valid" is musically meaningless and could confuse host applications that trust `spu94_subdivision_valid()` to filter out degenerate combinations.

**Fix:** Add a `samples == 0` check to both `spu94_subdivision_valid` and `spu94_set_subdivision`:

```c
/* In spu94_subdivision_valid, line 228: */
return (samples > 0 && samples <= UINT16_MAX) ? 1 : 0;

/* In spu94_set_subdivision, after line 253: */
if (samples == 0 || samples > UINT16_MAX) return SPU94_INVALID_ARG;
```

### WR-02: Snap tests verify API success but never assert actual register values

**File:** `tests/unit/tempo/test_tempo_snap.c:73-110`
**Issue:** The four "known-vector" tests (`test_known_vector_120_quarter`, `test_known_vector_60_whole`, `test_known_vector_90_eighth`, `test_known_vector_140_16t`) each verify that `spu94_set_subdivision()` returns `SPU94_OK`, but none of them read back the hardware register to assert the computed sample count. For example, `test_known_vector_120_quarter` computes the expected value of 2756 in a comment but never calls `spu94_get_reg_u16(s, SPU94_REG_dAPF1)` to verify the register actually contains 2756.

This means the formula could produce the wrong sample count (e.g., if the table ratios were swapped or the formula had an off-by-one) and all four tests would still pass. The test names and comments promise "known-vector formula tests" but deliver only success-code checks.

**Fix:** Add register readback assertions to each known-vector test. Example for the quarter-note test:

```c
void test_known_vector_120_quarter(void) {
    spu94_state *s = fresh_state();
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_INT(SPU94_OK, spu94_set_tempo(s, 120));
    TEST_ASSERT_EQUAL_INT(SPU94_OK,
        spu94_set_subdivision(s, SPU94_TEMPO_REG_dAPF1, SPU94_SUB_1_4));
    spu94_tick(s);  /* commit TICK_LATCHED */
    TEST_ASSERT_EQUAL_UINT16(2756,
        spu94_get_reg_u16(s, SPU94_REG_dAPF1));
}
```

Repeat for the other three with their expected values (22050, 1837, 393).

### WR-03: Hardcoded array size 10 in struct will silently desync from enum growth

**File:** `src/spu94/spu94_state_internal.h:212-214`
**Issue:** The three parallel arrays (`tempo_bind_state[10]`, `tempo_bind_sub[10]`, `tempo_bind_ref_bpm[10]`) use a hardcoded literal `10` instead of `SPU94_TEMPO_REG__COUNT`. If a future phase adds a tempo register (incrementing the enum sentinel to 11), the code in `spu94_tempo.c` that loops `for (int i = 0; i < SPU94_TEMPO_REG__COUNT; i++)` would access index 10 -- out of bounds of the `[10]` arrays. This would be a silent buffer overread/overwrite with no compile-time protection.

The existing pattern in this codebase uses `SPU94_REG__COUNT` for the register arrays -- these tempo arrays should follow the same convention.

**Fix:**

```c
/* In spu94_state_internal.h: */
uint8_t        tempo_bind_state[SPU94_TEMPO_REG__COUNT];
uint8_t        tempo_bind_sub[SPU94_TEMPO_REG__COUNT];
uint16_t       tempo_bind_ref_bpm[SPU94_TEMPO_REG__COUNT];
```

### WR-04: Virtual comb resnap reads TICK_LATCHED reference registers that may hold stale values

**File:** `src/spu94/spu94_tempo.c:181-189` and `src/spu94/spu94_tempo.c:271-279`
**Issue:** Both `spu94_set_subdivision()` (for virtual combs, line 271) and the auto-resnap loop in `spu94_set_tempo()` (line 181) read reference registers (mLSAME, mRSAME, mLDIFF, mRDIFF) via `spu94_get_reg_u16()`, which returns the ACTIVE value from `reg_values[]`. These registers are TICK_LATCHED -- their active value only updates at the next `spu94_tick()`. If a caller loads a preset and immediately sets a comb subdivision without an intervening tick, the reference values are stale (typically 0 from init). This causes the geometry check `(uint16_t)samples > ref_L` to reject valid subdivisions (if ref_L is 0, any nonzero delay is rejected), or -- if ref_L happens to be 0 and samples is also 0 -- to write 0 to the comb registers (degenerate).

The test suite avoids this by always calling `spu94_tick(s)` before tempo operations (see `fresh_state_with_hall` in test_tempo_comb.c:36-38), but no API documentation warns callers that a tick is required between preset load and comb subdivision binding.

**Fix:** Either document the precondition explicitly in the API doc-comment for `spu94_set_subdivision`, or read from `pending_values[]` instead of `reg_values[]` when fetching reference addresses for virtual comb geometry. A dedicated internal helper that reads the "most-recent-write" value (pending if dirty, active otherwise) would be the cleanest approach:

```c
static uint16_t get_latest_u16(const spu94_state *state, spu94_reg_t reg) {
    if (state->pending_mask & (UINT64_C(1) << reg))
        return (uint16_t)state->pending_values[reg];
    return (uint16_t)state->reg_values[reg];
}
```

## Info

### IN-01: Hardcoded magic number 6 used as d-prefix/comb boundary throughout

**File:** `src/spu94/spu94_tempo.c:78, 153, 173, 260, 267`
**Issue:** The literal `6` appears five times as the boundary between d-prefix tempo registers (indices 0-5) and virtual comb registers (indices 6-9). This magic number derives from the layout of `spu94_tempo_reg_t` but is not named. If the enum ever reorders or a register is inserted, each `6` must be found and updated.

**Fix:** Define a named constant:

```c
#define SPU94_TEMPO_COMB_START 6  /* first virtual comb index in spu94_tempo_reg_t */
```

### IN-02: Comment describes 1/1 as "whole note" but formula gives one-beat period

**File:** `src/spu94/spu94_tempo.c:43`
**Issue:** The comment says `[SPU94_SUB_1_1] = {1, 1}, /* whole note */` but at 120 BPM the formula yields 11025 samples = 0.5 seconds = one beat (quarter note), not a whole note (4 beats = 2 seconds). This is standard delay-effect convention (1/1 = one beat period), but the "whole note" label is misleading for anyone thinking in standard music notation. The same issue applies to "half note", "quarter note", etc. -- they are all one subdivision tier shorter than the musical names suggest.

**Fix:** Update comments to use delay-effect convention naming:

```c
[SPU94_SUB_1_1]          = {1,  1},   /* 1 beat (delay convention) */
[SPU94_SUB_1_2]          = {1,  2},   /* 1/2 beat */
[SPU94_SUB_1_4]          = {1,  4},   /* 1/4 beat */
```

Or add a header comment explaining the convention:

```c
/* Note: subdivision names follow delay-effect convention, not standard notation.
 * "1/1" = one beat period at the given BPM (= quarter note in 4/4 time). */
```

---

_Reviewed: 2026-05-03T12:15:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: deep_
