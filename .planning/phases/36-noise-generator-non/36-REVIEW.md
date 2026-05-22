---
phase: 36-noise-generator-non
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - include/spu94/spu94_noise.h
  - src/spu94/spu94_noise.c
  - include/spu94/spu94_voice.h
  - src/spu94/spu94_voice.c
  - src/spu94/CMakeLists.txt
  - tests/unit/voice/test_noise_gen.c
  - tests/unit/voice/test_voice_tick.c
  - tests/unit/voice/CMakeLists.txt
  - docs/DECISIONS.md
findings:
  critical: 0
  warning: 2
  info: 3
  total: 5
status: issues_found
---

# Phase 36: Code Review Report

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 9
**Status:** issues_found

## Summary

The Phase 36 noise generator implementation is functionally correct. The LFSR algorithm in `spu94_noise.c` faithfully translates the nocash pseudocode: three independent `if (timer < 0)` blocks, parity computed from the pre-shift level, signed `int32_t` timer, and `uint16_t` cast before the left shift to enforce 16-bit wrapping. The NON voice branch in `voice_tick` correctly substitutes `noise_level` for `gauss_out` while leaving ADPCM decode (STEP 1) unconditional — loop flags and ENDX fire regardless of NON state. The mixer plumbing (one noise tick before the voice loop, `non_flags` bitmask, `set_non`/`set_noise_freq` with validated bounds) is correct and complete.

Two warnings are raised: the `docs/DECISIONS.md` ADR-0058 contains a verifiably wrong LFSR sequence in its Consequences section (the code and tests are right; the documentation is wrong), and the `test_mixer_init_zeroes_all` test was not updated with Phase 36 fields, leaving `non_flags` and `noise_gen` initial state untested at the mixer layer. Three informational items cover a test comment misidentification, a missing negative-path test for `set_noise_freq`, and a stale comment in `spu94_voice_mixer_init`.

No critical issues. No spec violations. No security vulnerabilities. No undefined behavior. All eight NON requirements (NON-01 through NON-08) are correctly implemented.

---

## Warnings

### WR-01: ADR-0058 Consequences Section Contains Wrong LFSR Reference Sequence

**File:** `docs/DECISIONS.md:108`
**Issue:** The Consequences section of ADR-0058 states: *"the first four LFSR outputs at shift=15 (one-tick-per-shift) are 3, 6, 0xC, 0x19, verified against the nocash formula by unit test."*

This sequence is incorrect. With seed=1 and shift=15 (reload=4, step=4), taps at bits 15, 12, 11, 10 produce parity=1 for every level in the range 0x0001–0x03FF (all tapped bits are 0, so `0^0^0^0^1 = 1`). The actual sequence is 3, 7, 0xF, 0x1F. The claimed value of 6 for tick 2 would require parity=0, which only happens when bit 10 or higher tap bits are set — not the case until level reaches 0x0400.

The code (`spu94_noise.c`) and the unit tests (`test_lfsr_deterministic_sequence`, `test_parity_from_pre_shift_level`) are correct and consistent with each other. The ADR documentation is wrong and contradicts the passing tests.

**Fix:** In `docs/DECISIONS.md` ADR-0058 Consequences section, replace:
```
the first four LFSR outputs at shift=15 (one-tick-per-shift) are 3, 6, 0xC, 0x19
```
with:
```
the first four LFSR outputs at shift=15 (one-tick-per-shift) are 3, 7, 0xF, 0x1F
(parity=1 for all levels in 0x0001–0x03FF since no tap bits are set; first parity=0
occurs at level=0x07FF when bit10 becomes 1, producing 0x0FFE — verified by
test_parity_from_pre_shift_level and test_lfsr_deterministic_sequence)
```

---

### WR-02: `test_mixer_init_zeroes_all` Not Updated for Phase 36 Fields

**File:** `tests/unit/voice/test_voice_tick.c:640`
**Issue:** `test_mixer_init_zeroes_all` was not extended with Phase 36 assertions. It checks `pending_kon`, `pending_koff`, `eon_flags`, `master_vol_l/r`, and `enabled`, but omits:
- `non_flags == 0` (should be zero after init)
- `noise_gen.level == 1` (seed; critically NOT zero)
- `noise_gen.step == 4` (minimum step)
- `noise_gen.shift == 0`
- `noise_gen.timer == 0`

The seed assertion matters most: `spu94_voice_mixer_init` calls `spu94_noise_gen_init` after `memset` specifically to avoid the absorbing-state hazard (level=0 produces silence forever). Without a test asserting `noise_gen.level == 1` post-init, a future refactor that drops the `spu94_noise_gen_init` call would silently introduce the absorbing state with no test failure.

The PMON predecessor `test_mixer_init_zeroes_all` was not updated in Phase 35 for `pmon_flags` either, but the new `non_flags` + `noise_gen` fields are higher-risk to leave unverified because of the seed requirement.

**Fix:** Add to `test_mixer_init_zeroes_all` in `test_voice_tick.c`, after the existing `eon_flags` assertion:
```c
/* Phase 36: NON flags and noise generator initial state */
TEST_ASSERT_EQUAL_UINT32(0, s_test_mixer.non_flags);
TEST_ASSERT_EQUAL_INT16(1, s_test_mixer.noise_gen.level);   /* seed=1 (NOT 0, absorbing) */
TEST_ASSERT_EQUAL_UINT8(0, s_test_mixer.noise_gen.shift);
TEST_ASSERT_EQUAL_UINT8(4, s_test_mixer.noise_gen.step);
TEST_ASSERT_EQUAL_INT32(0, s_test_mixer.noise_gen.timer);
```

---

## Info

### IN-01: Test Comment Misidentifies Which NON Requirement is Covered

**File:** `tests/unit/voice/test_noise_gen.c:119`
**Issue:** The section comment for `test_timer_decrement_and_reload` says `NON-02` but the test body immediately below (lines 120–126 covering timer starting at 0, underflowing, LFSR shifting) also covers the initial tick behavior that falls under NON-01 (deterministic sequence) and NON-08 (single global tick). Similarly, `test_double_reload` is labelled only `NON-02` in the comment. The broader comment at the top of the file lists `NON-08` as "implicitly tested by shared state" — but there is no test that explicitly asserts the noise generator ticks exactly once per mixer tick (NON-08 should have a direct test).

The file-level comment (line 8) lists `NON-08` as covered, but no test verifies that `noise_gen` is ticked once and only once per `mixer_tick` call regardless of the number of active voices.

**Fix:** This is a documentation gap, not a functional bug. Add a comment clarification and consider adding an explicit `test_noise_gen_ticks_once_per_mixer_tick` test that sets a known `noise_gen` level, runs one `mixer_tick`, and verifies the level changed to the expected post-tick value (thereby confirming one tick fired, not zero or two).

---

### IN-02: `set_noise_freq` Bounds-Rejection Not Explicitly Tested

**File:** `tests/unit/voice/test_voice_tick.c`
**Issue:** `test_mixer_invalid_voice_idx` (line 743) tests bounds rejection for `key_on`, `key_off`, and `set_eon`, but no equivalent test covers `spu94_voice_mixer_set_non` or `spu94_voice_mixer_set_noise_freq`. Specifically, T-36-02 (`shift > 15` and `step_raw > 3` return `SPU94_INVALID_ARG`) is documented in the threat model but has no automated regression. If someone changes the bounds check accidentally, no test catches it.

**Fix:** Add to `test_mixer_invalid_voice_idx` or a new test:
```c
/* T-36-01/T-36-02: NON and noise_freq bounds checking */
TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
    spu94_voice_mixer_set_non(&s_test_mixer, 24, 1));
TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
    spu94_voice_mixer_set_non(&s_test_mixer, -1, 1));
TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
    spu94_voice_mixer_set_noise_freq(&s_test_mixer, 16, 0));  /* shift > 15 */
TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
    spu94_voice_mixer_set_noise_freq(&s_test_mixer, 0, 4));   /* step_raw > 3 */
TEST_ASSERT_EQUAL(SPU94_INVALID_ARG,
    spu94_voice_mixer_set_noise_freq(NULL, 0, 0));
```

---

### IN-03: `spu94_voice_mixer_init` Comment Omits Phase 36 Fields

**File:** `src/spu94/spu94_voice.c:331`
**Issue:** The comment at lines 331–333 lists fields that are "zero from memset — correct defaults" but does not mention `non_flags` (which is zero from memset, correctly) or the `noise_gen` initialization. This creates a documentation discrepancy: a reader auditing the init function would see the comment claiming completeness, then separately find the `spu94_noise_gen_init` call on line 336 without understanding why it's there or what fields it overwrites.

**Fix:** Update the comment to mention `non_flags` and why `noise_gen` requires explicit init:
```c
/* pending_kon, pending_koff, eon_flags, pmon_flags, non_flags, master_vol_l/r,
 * enabled, gauss_bypass are all zero from memset — correct defaults.
 * gauss_bypass=0 means Gaussian interpolation ON (PS1 faithful).
 * non_flags=0 means all voices default to ADPCM/Gauss output.
 * noise_gen is initialized separately below (memset alone would set level=0
 * which is an absorbing LFSR state; must override with seed=1). */
```

---

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
