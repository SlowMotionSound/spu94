---
phase: 03-core-reverb-algorithm-hard-clip
fixed_at: 2026-04-19T00:00:00Z
review_path: .planning/phases/03-core-reverb-algorithm-hard-clip/03-REVIEW.md
iteration: 2
findings_in_scope: 5
fixed: 5
skipped: 0
status: all_fixed
---

# Phase 3: Code Review Fix Report

**Fixed at:** 2026-04-20T14:40:50Z (iter 1) / 2026-04-19 (iter 2)
**Source review:** `.planning/phases/03-core-reverb-algorithm-hard-clip/03-REVIEW.md`
**Iteration:** 2

**Summary (cumulative):**
- Findings in scope: 5 (HR-01, WR-01, LW-01, IN-01, IN-02)
- Fixed: 5
- Skipped: 0

---

## Iteration 1 — fix_scope: critical_warning (HR-01, WR-01)

**Summary:**
- Findings in scope: 2 (HR-01, WR-01; fix_scope = critical_warning)
- Fixed: 2
- Skipped: 0

### HR-01: Wrong safety comment in `hard_clip` — int64->int32 cast overflows for INT32 extreme inputs

**Files modified:** `src/spu94/spu94_reverb.c`
**Commit:** 101d067
**Applied fix:** Replaced the two-line comment claiming "2 * (INT32_MAX - INT16_MAX) < INT32_MAX; cast to int32 is safe" with a correct seven-line comment. The new comment explains that the stated bound can actually exceed INT32_MAX for arbitrary int32 inputs, producing a wrapped negative cast on extreme values, and then clarifies that in practice `hard_clip` is only called from `input_scale` with a 16-bit x 16-bit product (max |product| = INT16_MIN^2 = 0x40000000), so the sum is always well within int32 range for all reverb-internal inputs. No numeric behavior changed; all 11 reverb tests pass.

### WR-01: Python `_buf_write` applies an extra `sat_s16` that the C code does not

**Files modified:** `tests/python/derive_reverb_reference.py`
**Commit:** 65556b6
**Applied fix:** Removed `sat_s16(value)` from the `_buf_write` store, replacing it with `value` directly (with a comment noting it matches `reverb_buf_write` in C). All existing callers (`ref_same_iir`, `ref_diff_iir`, etc.) already pass a saturated value to `_buf_write`, so the behavior change is zero for all current call sites. The Python oracle now accurately paraphrases the C store semantics, preventing silent divergence if a future caller passes a non-saturated intermediate. All 11 reverb tests pass.

---

## Iteration 2 — fix_scope: all (LW-01, IN-01, IN-02)

**Summary:**
- Findings in scope: 3 (LW-01, IN-01, IN-02)
- Fixed: 3
- Skipped: 0

### LW-01: `reverb_buf_write` bounds check — silent-drop contract not documented

**Files modified:** `src/spu94/spu94_reverb.c`
**Commit:** 5aeaf22
**Applied fix:** Added a two-line inline comment immediately before the `if ((size_t)byte_off + 1u >= s->work_buf_size) return;` guard in `reverb_buf_write` explaining that out-of-range writes are silently discarded (defensive; buffer too small for address; stage output is lost but state is not corrupted). Comment-only — no control flow changed. All 11 reverb tests pass.

### IN-01: `spu94_reverb_body` — `err_input_scale` body-level coverage vacuous until Phase 5

**Files modified:** `src/spu94/spu94_reverb.c`
**Commit:** 96a0fe1
**Applied fix:** Extended the existing placeholder comment on `left_in`/`right_in` with a note explaining that because both are zero, `err_input_scale` is permanently zero in all body-level tests (including fuzz), that the per-stage test (`test_reverb_input_scale.c`) covers `err_input_scale` properly, and that body-level coverage of that field is intentionally vacuous until Phase 5 wires non-zero inputs. This prevents a future maintainer from misreading the always-zero body assertion as meaningful coverage. All 11 reverb tests pass.

### IN-02: `test_reverb_output_scale.c` — abandoned comment with wrong arithmetic + dead `exp_err_delta` table column

**Files modified:** `tests/unit/reverb/test_reverb_output_scale.c`
**Commit:** c258b88
**Applied fix:** Two changes in one commit:
1. Deleted the 6-line abandoned derivation comment for the `0x1234 * 0x7FFF` case (lines 46-51 in the original), which contained incorrect arithmetic (`4660*32767 = 152,695,220` — actual value is `152,688,380`). The surviving comment for the `0x7FFF*0x7FFF` case is retained.
2. Removed the `exp_err_delta` field from the `output_scale_case_t` struct and its corresponding initializer from every row in `g_cases`. Confirmed via grep that no test assertion ever read `g_cases[i].exp_err_delta` — the test loop exclusively uses the `ref_output_scale` oracle for the error delta, making the table column dead code. The oracle function's `exp_err_delta` output parameter is a local variable in the function and was not affected. All 11 reverb tests pass.

---

_Fixed: 2026-04-19_
_Fixer: Claude (gsd-code-fixer)_
_Iteration: 2_
