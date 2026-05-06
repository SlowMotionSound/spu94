---
phase: 16-interpolation-engine
reviewed: 2026-05-05T18:30:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - src/spu94/spu94_interp.c
  - tests/unit/interp/test_interp.c
  - tests/unit/interp/CMakeLists.txt
  - include/spu94/spu94.h
  - src/spu94/CMakeLists.txt
  - tests/unit/CMakeLists.txt
findings:
  critical: 1
  warning: 2
  info: 0
  total: 3
status: issues_found
---

# Phase 16: Code Review Report

**Reviewed:** 2026-05-05T18:30:00Z
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

The interpolation engine is well-structured with clear intent documentation, proper NULL guards, and correct signed/unsigned dispatch logic. However, the NaN threat mitigation (T-16-02) is fatally flawed -- the code comment claims NaN produces safe behavior but the actual C semantics produce undefined behavior on x86, leading to an out-of-bounds array read. The test suite covers the main happy paths thoroughly but lacks NaN/Inf edge case coverage and uses a midpoint formula that does not precisely match the implementation's rounding behavior (masked by the specific preset data having even differences).

## Critical Issues

### CR-01: NaN input causes out-of-bounds array access (undefined behavior)

**File:** `src/spu94/spu94_interp.c:63-84`
**Issue:** The comment at line 63-65 claims NaN "falls through" safely because "NaN cast to int is implementation-defined but typically 0 on all target platforms." This is incorrect. On x86 (the primary target), `cvttss2si` of NaN produces `INT_MIN` (-2147483648). The subsequent check at line 77 (`seg >= SPU94_INTERP_WAYPOINT_COUNT - 1`) evaluates false for `INT_MIN`, so the negative seg value passes through unchecked. Lines 83-84 then access `spu94_interp_waypoints[INT_MIN]` -- a catastrophic out-of-bounds read from `.rodata`, likely followed by dereferencing garbage pointers into `spu94_presets[garbage].regs`. This is a crash/memory-corruption vector. If position is ever NaN (e.g., from uninitialized float, division 0.0/0.0, or user-supplied parameter), the program has undefined behavior.

**Fix:**
```c
/* T-16-01 + T-16-02: Clamp and reject non-finite values.
 * NaN fails both ordered comparisons, so detect it explicitly. */
if (!(position >= 0.0f)) position = 0.0f;  /* catches NaN and negatives */
if (position > 1.0f) position = 1.0f;
```

Or equivalently, add a lower-bound check on `seg` after the cast:

```c
int seg = (int)scaled;
float frac = scaled - (float)seg;

/* Clamp segment to valid range. */
if (seg < 0) {
    seg = 0;
    frac = 0.0f;
}
if (seg >= SPU94_INTERP_WAYPOINT_COUNT - 1) {
    seg = SPU94_INTERP_WAYPOINT_COUNT - 2;
    frac = 1.0f;
}
```

The first approach (replacing `position < 0.0f` with `!(position >= 0.0f)`) is preferred because it eliminates NaN at the source, making all downstream arithmetic well-defined. The negated ordered-comparison idiom exploits the fact that NaN comparisons always return false.

## Warnings

### WR-01: Test midpoint formula does not match implementation rounding for odd differences

**File:** `tests/unit/interp/test_interp.c:257-269`
**Issue:** The test computes expected midpoint as `((int32_t)va + (int32_t)vb) / 2` (line 260), while the implementation computes `va + (int32_t)((float)(vb - va) * 0.5f)` (spu94_interp.c:130). These produce different results when `(vb - va)` is odd and the sum `(va + vb)` is also odd. Example: va=-3, vb=2 produces implementation=-1 but test expects 0. The test currently passes only because all Studio B to Studio C signed register differences happen to be even. If the waypoint order is ever changed or new presets are added with odd differences, this test will report false failures or (worse) mask real bugs by not matching the actual implementation formula.

**Fix:** The test should mirror the implementation formula exactly:
```c
if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
    int16_t va = studio_b->regs[r];
    int16_t vb = studio_c->regs[r];
    int32_t expected = (int32_t)va + (int32_t)((float)(vb - va) * 0.5f);
    TEST_ASSERT_EQUAL_INT16_MESSAGE((int16_t)expected,
        spu94_get_reg_i16(state, (spu94_reg_t)r), msg);
} else {
    uint16_t va = (uint16_t)studio_b->regs[r];
    uint16_t vb = (uint16_t)studio_c->regs[r];
    int32_t expected = (int32_t)va + (int32_t)((float)((int32_t)vb - (int32_t)va) * 0.5f);
    TEST_ASSERT_EQUAL_UINT16_MESSAGE((uint16_t)expected,
        spu94_get_reg_u16(state, (spu94_reg_t)r), msg);
}
```

### WR-02: No test coverage for NaN and Infinity inputs

**File:** `tests/unit/interp/test_interp.c`
**Issue:** The test suite exercises NULL state, negative position (-1.0), and over-range position (2.0), but never passes NaN or positive/negative infinity. Given that T-16-02 is listed as a threat mitigation in the implementation, the threat should have a corresponding regression test. The absence of this test means CR-01 above was not caught during development.

**Fix:** Add a test case:
```c
#include <math.h>

static void test_interp_nan_input(void) {
    /* NaN must not crash -- should behave as morph 0.0 (Half Echo). */
    spu94_interp_set_morph(state, NAN);
    spu94_tick(state);

    const spu94_preset_t *half_echo = &spu94_presets[SPU94_PRESET_HALF_ECHO];
    /* Spot-check one non-fixed register. */
    TEST_ASSERT_EQUAL_INT16(half_echo->regs[SPU94_REG_vIIR],
        spu94_get_reg_i16(state, SPU94_REG_vIIR));
}

static void test_interp_infinity_input(void) {
    /* +Inf should clamp to morph 1.0 (Delay). */
    spu94_interp_set_morph(state, INFINITY);
    spu94_tick(state);

    const spu94_preset_t *delay = &spu94_presets[SPU94_PRESET_DELAY];
    TEST_ASSERT_EQUAL_INT16(delay->regs[SPU94_REG_vIIR],
        spu94_get_reg_i16(state, SPU94_REG_vIIR));
}
```

---

_Reviewed: 2026-05-05T18:30:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
