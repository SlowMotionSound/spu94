---
phase: 10-core-polyphase-fir-cascade
reviewed: 2026-04-30T20:30:00Z
depth: standard
files_reviewed: 6
files_reviewed_list:
  - include/spu94/spu94_dac_fir.h
  - src/spu94/spu94_dac_fir.c
  - src/spu94/spu94_process.c
  - tests/unit/dac_fir/CMakeLists.txt
  - tests/unit/dac_fir/test_dac_fir_8x.c
  - tools/dac_filter_design.py
findings:
  critical: 1
  warning: 3
  info: 1
  total: 5
status: issues_found
---

# Phase 10: Code Review Report

**Reviewed:** 2026-04-30T20:30:00Z
**Depth:** standard
**Files Reviewed:** 6
**Status:** issues_found

## Summary

Phase 10 adds `spu94_dac_fir_step_8x` -- a naive 8x zero-stuff FIR cascade
that replaces the v1.2 single-rate `spu94_dac_fir_step` on the production audio
path (`spu94_process`). The C implementation is structurally sound: delay-line
management, folded-form evaluation, and stage cascading all follow the
established Phase 6 patterns. The Python reference implementation in
`dac_filter_design.py` faithfully mirrors the C integer arithmetic for
cross-validation. The test file covers impulse response, DC gain, overflow
resilience, and v1.2 regression. CMake integration is correct.

One critical issue was found: the production path now outputs ~18 dB less signal
than v1.2 with no gain compensation and no API documentation of the change.
Three warnings address test effectiveness gaps and a documentation accuracy
concern.

## Critical Issues

### CR-01: Production path silently drops output level by ~18 dB (1/8 gain)

**File:** `src/spu94/spu94_process.c:117-118`
**Issue:** The diff replaces `spu94_dac_fir_step` with `spu94_dac_fir_step_8x`
on the production audio path. The naive 8x zero-stuff cascade inherently has 1/8
the DC gain of the single-rate path (~-18.06 dB). Any downstream consumer or
host that had calibrated levels with DAC FIR enabled will now hear an 18 dB drop
in output level with no warning, no version bump on the API, and no
documentation in the public header.

The test `test_8x_dc_gain` confirms the 1/8 factor explicitly (expected steady
state of 2044 vs. v1.2's 16333 for the same input). The existing integration
tests (`test_process_dac_integration.c`) only check "output differs from
baseline" -- they do not validate magnitude, so they mask this regression.

This is the intended DSP behavior of the naive zero-stuff approach (the comment
block in `spu94_dac_fir.c` lines 170-185 documents the gain relationship). But
deploying it as a silent drop-in replacement on the production path without
either (a) compensating the gain or (b) documenting the breaking change in the
public API header is a behavioral regression that could cause data-level
surprises for callers.

**Fix:** Either:
1. Add an 8x gain compensation multiply (`sat_s16((int32_t)result * 8)` or
   equivalent Q15 scale) inside `spu94_dac_fir_step_8x` so the function's DC
   gain matches `spu94_dac_fir_step`, OR
2. Document the 1/8 gain factor prominently in the public header
   (`include/spu94/spu94_dac_fir.h` line 40-46) and in a BREAKING CHANGE note,
   so callers know to adjust fader levels.

Option 1 is the cleaner path if the intent is behavioral compatibility. Option 2
is acceptable if the 1/8 gain is a deliberate "this is how the real AK4309
behaves at this stage" fidelity choice -- but it must be called out, not silent.

## Warnings

### WR-01: Overflow test assertions are tautological (no actual range verification)

**File:** `tests/unit/dac_fir/test_dac_fir_8x.c:131-132`
**Issue:** The assertions in `test_8x_overflow_adversarial`:
```c
TEST_ASSERT_GREATER_OR_EQUAL_INT16(INT16_MIN, out);
TEST_ASSERT_LESS_OR_EQUAL_INT16(INT16_MAX, out);
```
These can never fail. Any `int16_t` value is by definition `>= INT16_MIN` and
`<= INT16_MAX`. The test is effectively a "doesn't crash" smoke test, which is
valuable, but the assertions create a false sense of coverage -- they appear to
verify output range but add zero verification power.

**Fix:** Either remove the tautological assertions and add a comment that this is
a crash/UB smoke test, or replace them with meaningful checks such as verifying
the output matches the Python reference implementation for the same adversarial
input sequence:
```c
/* Smoke test: function completes without crashing or triggering UB.
 * No meaningful range assertion possible -- int16_t is always in range. */
(void)spu94_dac_fir_step_8x(&state, INT16_MIN);
```

### WR-02: No integration test validates 8x DC gain through spu94_process

**File:** `tests/unit/dac_fir/test_dac_fir_8x.c` (entire file) and
`tests/unit/process/test_process_dac_integration.c` (not in diff, but affected)
**Issue:** The unit test `test_8x_dc_gain` validates the standalone function's DC
gain (2044 for input 16384). However, there is no integration test that feeds DC
through `spu94_process` with DAC FIR enabled and verifies the output magnitude.
The existing `test_process_dac_integration.c` tests only check "differs from
baseline" -- they would pass even if the 8x function returned zero for all
inputs (provided it returned a different zero pattern from the noise-off
baseline, which it would due to filter transients).

This gap means a future regression that breaks the gain relationship (e.g., a
polyphase optimization that accidentally doubles or halves the gain) would not be
caught by CI.

**Fix:** Add an integration test in the `test_dac_fir_8x.c` or
`test_process_dac_integration.c` suite that feeds a known DC level through
`spu94_process` with DAC FIR enabled and asserts the output magnitude is within a
tolerance band of the expected 1/8 gain.

### WR-03: Header doc says "14 evaluations" but actual count differs per call

**File:** `include/spu94/spu94_dac_fir.h:43`
**Issue:** The comment states the function "runs 14 evaluations of
dac_fir_stage_apply (2+4+8)". The actual implementation runs 2+4+8 = 14
evaluations. However, only 13 of those produce meaningful results -- in
Stage 3, the first evaluation of each pair (lines 234-238) is explicitly
discarded via `(void)`. While the delay-line state advancement is necessary
(as the comment on line 227-229 correctly notes), describing all 14 as
"evaluations" could be misleading to a reader who expects 14 output samples.
This is a documentation clarity issue, not a logic bug.

**Fix:** Amend the header comment to clarify:
```c
/* ... runs 14 dac_fir_stage_apply calls (2+4+8) for delay-line
 * advancement; only the last Stage 3 output survives decimation. */
```

## Info

### IN-01: Python reference accumulator uses arbitrary-precision integers

**File:** `tools/dac_filter_design.py:203-213`
**Issue:** The Python function `dac_fir_stage_apply_py` documents that it "Uses
int32 accumulator matching C's int32_t acc" (line 197). However, Python integers
are arbitrary-precision -- the accumulator `acc` in Python will never overflow
regardless of input values, whereas the C code relies on proven accumulator
bounds to stay within int32 range. If someone used the Python reference to
validate a modified coefficient set with larger magnitudes, the Python code would
silently produce correct results while the C code would overflow.

**Fix:** Add a bounds check after the accumulation loop:
```python
assert -2**31 <= acc < 2**31, f"Accumulator overflow: {acc}"
```
This makes the Python reference fail-loud when the int32 assumption is violated,
providing an early warning before C code is tested.

---

_Reviewed: 2026-04-30T20:30:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
