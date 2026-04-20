---
phase: 03-core-reverb-algorithm-hard-clip
fixed_at: 2026-04-20T14:40:50Z
review_path: .planning/phases/03-core-reverb-algorithm-hard-clip/03-REVIEW.md
iteration: 1
findings_in_scope: 2
fixed: 2
skipped: 0
status: all_fixed
---

# Phase 3: Code Review Fix Report

**Fixed at:** 2026-04-20T14:40:50Z
**Source review:** `.planning/phases/03-core-reverb-algorithm-hard-clip/03-REVIEW.md`
**Iteration:** 1

**Summary:**
- Findings in scope: 2 (HR-01, WR-01; fix_scope = critical_warning)
- Fixed: 2
- Skipped: 0

## Fixed Issues

### HR-01: Wrong safety comment in `hard_clip` — int64->int32 cast overflows for INT32 extreme inputs

**Files modified:** `src/spu94/spu94_reverb.c`
**Commit:** 101d067
**Applied fix:** Replaced the two-line comment claiming "2 * (INT32_MAX - INT16_MAX) < INT32_MAX; cast to int32 is safe" with a correct seven-line comment. The new comment explains that the stated bound can actually exceed INT32_MAX for arbitrary int32 inputs, producing a wrapped negative cast on extreme values, and then clarifies that in practice `hard_clip` is only called from `input_scale` with a 16-bit x 16-bit product (max |product| = INT16_MIN^2 = 0x40000000), so the sum is always well within int32 range for all reverb-internal inputs. No numeric behavior changed; all 11 reverb tests pass.

### WR-01: Python `_buf_write` applies an extra `sat_s16` that the C code does not

**Files modified:** `tests/python/derive_reverb_reference.py`
**Commit:** 65556b6
**Applied fix:** Removed `sat_s16(value)` from the `_buf_write` store, replacing it with `value` directly (with a comment noting it matches `reverb_buf_write` in C). All existing callers (`ref_same_iir`, `ref_diff_iir`, etc.) already pass a saturated value to `_buf_write`, so the behavior change is zero for all current call sites. The Python oracle now accurately paraphrases the C store semantics, preventing silent divergence if a future caller passes a non-saturated intermediate. All 11 reverb tests pass.

---

_Fixed: 2026-04-20T14:40:50Z_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 1_
