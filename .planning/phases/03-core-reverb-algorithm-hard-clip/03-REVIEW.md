---
phase: 03-core-reverb-algorithm-hard-clip
reviewed: 2026-04-19T00:00:00Z
depth: deep
files_reviewed: 14
files_reviewed_list:
  - src/spu94/spu94_reverb.c
  - src/spu94/spu94_reverb_internal.h
  - src/spu94/spu94_state_internal.h
  - src/spu94/spu94_tick.c
  - tests/unit/reverb/test_reverb_hard_clip.c
  - tests/unit/reverb/test_reverb_input_scale.c
  - tests/unit/reverb/test_reverb_output_scale.c
  - tests/unit/reverb/test_reverb_same_iir.c
  - tests/unit/reverb/test_reverb_diff_iir.c
  - tests/unit/reverb/test_reverb_comb.c
  - tests/unit/reverb/test_reverb_apf1.c
  - tests/unit/reverb/test_reverb_apf2.c
  - tests/unit/reverb/test_reverb_edges.c
  - tests/unit/reverb/test_reverb_body.c
  - tests/python/derive_reverb_reference.py
  - tests/python/fuzz_reverb.py
findings:
  critical: 0
  high: 1
  medium: 1
  low: 1
  info: 2
  total: 5
status: issues_found
---

# Phase 3: Code Review Report

**Reviewed:** 2026-04-19
**Depth:** deep (cross-file arithmetic verification + Python/C divergence analysis)
**Files Reviewed:** 16
**Status:** issues_found

## Summary

Phase 3 implements the full nocash SPU reverb network in C with exemplary
discipline. The architecture matches every locked decision (D-01 through
D-11, ADR-0007 through ADR-0011). The D-07 cascading-sat guard is confirmed
present and the D-10 anomaly branch count is exactly 4 (verified by grep).
Every UB-prone edge — INT16_MIN negation, INT16_MIN squared, tap subtraction
— has explicit int32 widening guards. The Python reference script is
semantically equivalent to the C implementation for all int16 inputs.

Five findings follow. The high-severity finding is a wrong comment that
claims an int64-to-int32 cast is always safe when it is not; the cast
itself is well-defined on C23 / two's-complement targets (all mainstream
platforms) and UBSan does not flag it, but the comment misleads future
maintainers and the associated test silently validates a negative result
as correct for INT32 extreme inputs. The medium finding is a gap in the
Python reference script. The remaining three are low/info.

---

## High Issues

### HR-01: Wrong safety comment in `hard_clip` — int64→int32 cast overflows for INT32 extreme inputs

**File:** `src/spu94/spu94_reverb.c:113-115`
**Violates locked decision:** No (D-09 / D-11 overflow-tap logic is correct; only the comment is wrong)

**Issue:**
The comment inside `spu94_reverb_hard_clip` states:

```c
/* Sum is bounded by 2 * (INT32_MAX - INT16_MAX) < INT32_MAX;
 * cast to int32 is safe. */
*overflow_out = (int32_t)sum;
```

The bound `2 * (INT32_MAX - INT16_MAX)` evaluates to `4,294,901,760`, which
exceeds `INT32_MAX` (`2,147,483,647`) by more than 2 billion. When both
inputs are at INT32 extremes — `INT32_MAX` on one side and `INT32_MIN` on
the other — the int64 `sum` is `0xFFFE0001` (`4,294,836,225`), which is
greater than `INT32_MAX`. The cast `(int32_t)sum` is implementation-defined
(not undefined) in C17/C23 on two's-complement hardware — all mainstream
targets wrap modularly, giving `-131071` — so this does not trigger UBSan
and does not crash in practice.

However, the comment's stated bound is arithmetically wrong, and
`test_reverb_hard_clip.c:44-47` enshrines the negative wrapped result as
the expected value for the `(INT32_MAX, INT32_MIN)` test case. Any
maintainer who adds a range check on `overflow_magnitude` (e.g., "assert
overflow >= 0") will be surprised.

**Practical impact of the incorrect value:**
The `overflow_magnitude` field is described in ADR-0011 as "the high bits
lost to saturation." For a pair of INT32-extreme inputs, the logical
magnitude is approximately 4 billion, but the stored value wraps to
`-131071`. This only happens when `input_scale` produces a product near
INT32 extremes — i.e., when both the input sample *and* the `vLIN`/`vRIN`
coefficient are near their respective maxima simultaneously. In any
realistic reverb scenario (input samples are 16-bit; `vLIN`/`vRIN` are
16-bit coefficients) the widened product is a 32-bit value bounded at
`INT16_MAX * INT16_MAX = 0x3FFF0001`, which is well within int32 range
with a sum of at most `2 * (0x3FFF0001 - INT16_MAX) = 0x7FFD8002`. The
wrap is therefore unreachable in the actual reverb data path — the
`input_scale` stage multiplies two `int16_t` values and the maximum product
is `0x40000000` (INT16_MIN squared), far below INT32_MAX. The test case
with `(INT32_MAX, INT32_MIN)` exercises a path that the calling code in
`spu94_reverb_body` can never produce.

**Fix:**
Replace the incorrect comment with the correct bound for the realistic input
domain, and either remove the unreachable extreme test case or add a comment
explaining that the input domain is 16-bit and the extreme values are a
white-box stress test:

```c
/* Sum is bounded by 2 * (INT32_MAX - INT16_MAX) for arbitrary int32 inputs
 * — this can exceed INT32_MAX when |Lin_wide| or |Rin_wide| is near INT32_MAX.
 * In practice, hard_clip is only called from input_scale with a 16-bit x 16-bit
 * product: max = INT16_MIN^2 = 0x40000000, so sum <= 2*(0x40000000 - INT16_MAX)
 * which fits easily in int32. The cast is lossless for all reverb-internal inputs. */
*overflow_out = (int32_t)sum;
```

If future code changes ever route wider values through `hard_clip`, the
field's sign contract changes unexpectedly. Consider clamping the sum or
widening `overflow_magnitude` to int64 when that future need arises.

---

## Warnings

### WR-01: Python `ref_same_iir` / `ref_diff_iir` — `_buf_write` applies an extra `sat_s16` that the C code does not

**File:** `tests/python/derive_reverb_reference.py:107`
**Violates locked decision:** No — but affects the oracle's accuracy for
values that are already saturated and then written.

**Issue:**
The Python helper `_buf_write` wraps every stored value in `sat_s16`:

```python
def _buf_write(buf: dict, buffer_address: int, halfword_offset: int,
               value: int) -> None:
    byte_off = (buffer_address + (halfword_offset & 0xFFFF) * 2) & 0x7FFFE
    buf[byte_off] = sat_s16(value)          # <-- extra sat_s16 here
```

The C implementation `reverb_buf_write` does not apply saturation — it
stores whatever `int16_t` value it receives:

```c
static inline void reverb_buf_write(spu94_state *s,
                                    uint16_t halfword_offset,
                                    int16_t value)
{
    /* ... */
    uint16_t u = (uint16_t)value;           /* no sat_s16 */
    s->work_buf[byte_off]       = (unsigned char)(u & 0xFFu);
    s->work_buf[byte_off + 1u]  = (unsigned char)((u >> 8) & 0xFFu);
}
```

Because every caller in the current code already passes a value that has
been through `sat_s16` or `q15_add_sat` before reaching `reverb_buf_write`,
the extra `sat_s16` in `_buf_write` is a no-op in all existing test
scenarios. However:

1. It makes the Python reference not a clean paraphrase of the C
   implementation. A future refactor that passes a non-saturated intermediate
   to `reverb_buf_write` (e.g., during seam swapping under D-22) would
   silently diverge between Python and C, with the Python oracle masking
   the bug rather than catching it.

2. The fuzz harness (`fuzz_reverb.py`) does not exercise the Python
   reference functions directly — it only exercises the C library. So the
   oracle's extra saturation is not currently caught by any automated test.

**Fix:**
Remove the `sat_s16` wrapper from `_buf_write` so the Python reference
matches the C implementation's actual store semantics:

```python
def _buf_write(buf: dict, buffer_address: int, halfword_offset: int,
               value: int) -> None:
    byte_off = (buffer_address + (halfword_offset & 0xFFFF) * 2) & 0x7FFFE
    buf[byte_off] = value   # store as-is, matching reverb_buf_write in C
```

Callers that need saturation before storing already call `sat_s16` before
passing to `_buf_write` (e.g., `ref_same_iir` computes `result = sat_s16(...)` 
before calling `_buf_write`), so the behavior change is zero for all current
call sites.

---

## Low Issues

### LW-01: `reverb_buf_read` / `reverb_buf_write` — bounds check uses `>=` which silently rejects the last valid halfword slot

**File:** `src/spu94/spu94_reverb.c:54, 67`

**Issue:**
Both helpers use the same guard pattern:

```c
if ((size_t)byte_off + 1u >= s->work_buf_size) return 0; /* or discard */
```

This is semantically correct — a halfword at `byte_off` requires valid bytes
at `byte_off` and `byte_off + 1`. The condition correctly rejects any case
where `byte_off + 1` is at or beyond the buffer end. No bug exists in the
current logic.

However, the condition also rejects the case `byte_off + 1 == work_buf_size - 1`
because the check compares against `work_buf_size` (one past the end), not
`work_buf_size - 1`. A halfword at offset `work_buf_size - 2` is the last
valid halfword (bytes at `work_buf_size - 2` and `work_buf_size - 1`); the
check `byte_off + 1 >= work_buf_size` evaluates to `(work_buf_size - 1) >= work_buf_size`
which is false — so it is not rejected. This is correct.

Re-reading: if `byte_off = work_buf_size - 1`, then `byte_off + 1 = work_buf_size`,
and `work_buf_size >= work_buf_size` is true, so it is rejected. Correct.
If `byte_off = work_buf_size - 2`, then `byte_off + 1 = work_buf_size - 1 < work_buf_size`,
so it is NOT rejected. Correct.

The logic is sound. The concern is that the comment reads:

```c
* If work_buf_size is smaller than 0x80000 (caller supplies a smaller
* buffer — legal per Phase 2 init contract), out-of-range reads return
* 0 and writes are discarded; the 0x7FFFE mask alone already gives the
* hardware-correct address, and the extra bounds check is defensive.
```

The comment doesn't mention what happens when `byte_off` computed from the
mask exceeds `work_buf_size`. For a small work buffer (e.g., 0x1000 bytes as
used by many tests), addresses above 0x0FFE will always be rejected. This is
defensive-correct but silently drops writes, which means stage functions
produce no output when registers point outside the buffer. Tests use
`buffer_address = 0` and small offsets, so they never trigger this path.
The behaviour is acceptable and documented, but the defensive-drop is
invisible to the caller.

**This is not a bug.** Rating it low for documentation clarity.

**Suggestion:** Add a one-line note clarifying the out-of-range-means-silent-drop
contract for the write path, so a future debugging session doesn't mistake
dropped writes for a math error:

```c
/* out-of-range write: silent discard (defensive; buffer too small for
 * this address; stage output is lost but state is not corrupted). */
if ((size_t)byte_off + 1u >= s->work_buf_size) return;
```

---

## Info

### IN-01: `spu94_reverb_body` — `left_in` / `right_in` hardcoded to 0; no comment about impact on err_input_scale test coverage

**File:** `src/spu94/spu94_reverb.c:568-569`

**Issue:**
```c
const int16_t left_in = 0;
const int16_t right_in = 0;
```

The comment correctly states this is placeholder until Phase 5. However,
because `left_in = right_in = 0`, `err_input_scale` is permanently zero
in any test that exercises the full body path (including the fuzz harness
and `test_reverb_body.c`). The per-stage test `test_reverb_input_scale.c`
verifies that `err_input_scale` stays zero specifically because there is
no `>> 15` shift, so this field genuinely should always be zero by design —
but the zero-input body makes the `test_err_zero_for_non_saturating_all_stages`
test in `test_reverb_edges.c` partially vacuous for the `err_input_scale`
assertion (it asserts zero, and it will always be zero regardless of what
`left_in`/`right_in` are, for a different reason than the other stages).

No fix required. The field is zero by construction (D-11 documents this
explicitly in the production code comment). This is an observation about
the limited body-level coverage of `err_input_scale` until Phase 5.

### IN-02: `test_reverb_output_scale.c` — inline comment arithmetic contains an off-by-one note marker

**File:** `tests/unit/reverb/test_reverb_output_scale.c:49-56`

**Issue:**
The test file contains a comment block that begins a manual derivation for
the `0x1234 * 0x7FFF` case and then self-interrupts:

```c
/* Use INT16_MAX*INT16_MAX case — well-known from Phase 1 test table: ...*/
```

The comment starts to derive `0x1234 * 0x7FFF`, notes `4660*32767 = 152,695,220`,
then explicitly abandons the derivation with "Let's trust the Q15 helpers +
recompute below". This pattern is fine architecturally (the test then uses
`ref_output_scale` as the oracle, which is the right approach), but the
abandoned derivation with incorrect intermediate math (`4660*32767 = 152,695,220`
is incorrect; the actual value is `152,688,380 = 0x91AA3CC`) leaves a
misleading comment in the test file.

Additionally, the table entry for `0x7FFF * 0x7FFF` says `exp_err_delta` is
`(int32_t)(0x8001) + (int32_t)(0x8001)`. But `0x8001 = 32769` as unsigned,
and as `int32_t` it is `32769`, so `exp_err_delta = 65538`. However the
oracle call (`ref_output_scale`) does the right calculation, so the
hardcoded table entry for `exp_err_delta` is effectively unused (the test
asserts the oracle result, not the table's `exp_err_delta`). The table is
vestigial — it is declared but `exp_err_delta` from the table is never
asserted against in `test_output_scale_table_against_q15_oracle`. Not a
bug, but dead code in the test file.

No runtime impact. The oracle-based pattern (`ref_output_scale`) is the
correct approach and is what actually runs.

---

## Decision Compliance Verification

All locked decisions verified:

| Decision | Verified |
|----------|---------|
| D-01: Internal header `spu94_reverb_internal.h`, not on public path | PASS |
| D-02: One function per stage (7 functions), L+R internal | PASS |
| D-03: Output-scale is its own stage function | PASS |
| D-05: Single `spu94_reverb.c` TU | PASS |
| D-06: Inserted between `apply_pending_writes` and `buffer_advance` in `spu94_tick.c` | PASS |
| D-07: Comb sum uses cascading `q15_add_sat` — no `int32_t sumL/sumR/acc32` | PASS (grep confirmed zero matches) |
| D-08: v* registers snapshotted once at `spu94_reverb_body` top, passed by value | PASS |
| D-09: `spu94_reverb_hard_clip` is its own stage function, accepts int32, returns int16 + overflow | PASS |
| D-10: `vIIR_snap == INT16_MIN` explicit branch at each memory-write point; 4 occurrences | PASS (verified by grep) |
| D-11: Every Q15 multiply uses `q15_mul_truncate_with_err`; 7 per-stage err accumulators + overflow_magnitude | PASS |
| ADR-0001: `>> 15` ASR truncation toward −∞ | PASS |
| ADR-0003: No file-level or function-level UBSan disables observed | PASS |
| Pitfall 1 guard (INT16_MIN negation UB): `sat_s16(-(int32_t)x)` pattern used consistently | PASS |
| No heap, no float/double in core | PASS |
| Test expectations derived from Python oracle (no GPL emulator in chain) | PASS |

---

_Reviewed: 2026-04-19_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: deep_
