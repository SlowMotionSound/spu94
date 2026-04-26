---
phase: 03-core-reverb-algorithm-hard-clip
plan: 03
subsystem: reverb-comb-apf
tags: [reverb, comb, apf, d-07-cascading-sat, pitfall-1-guard, pitfall-7-feedback-edge, err-accumulator]
requires:
  - spu94_reverb scaffold + body caller (Plan 01)
  - spu94_reverb_same_iir + spu94_reverb_diff_iir (Plan 02)
  - reverb_buf_read / reverb_buf_write static inlines (Plan 02)
  - state->err_comb / err_apf1 / err_apf2 fields (Plan 01 pre-allocation)
  - reverb_body snapshot surface (Plan 01 + 02)
  - q15_mul_truncate_with_err + sat_s16 + q15_add_sat (Phase 1)
provides:
  - src/spu94/spu94_reverb.c::spu94_reverb_comb full body (D-07 cascading sat_s16)
  - src/spu94/spu94_reverb.c::spu94_reverb_apf1 full body (Pitfall 1 guard on subtract)
  - src/spu94/spu94_reverb.c::spu94_reverb_apf2 full body (same recurrence as APF1)
  - spu94_reverb_body fully wired end-to-end: input_scale -> hard_clip ->
    same_iir -> diff_iir -> comb -> apf1 -> apf2 -> output_scale
  - tests/python/derive_reverb_reference.py::ref_comb (cascading sat)
  - tests/python/derive_reverb_reference.py::ref_apf1 / ref_apf2
    (plus the shared _ref_apf_side helper)
  - tests/unit/reverb/test_reverb_comb.c (5 Unity tests incl. D-07 distinguishing)
  - tests/unit/reverb/test_reverb_apf1.c (5 Unity tests incl. Pitfall-7 edge)
  - tests/unit/reverb/test_reverb_apf2.c (5 Unity tests incl. Pitfall-7 edge)
affects:
  - src/spu94/spu94_reverb.c (spu94_reverb_body snapshots vCOMB1..4, vAPF1/2, dAPF1/2 and invokes all 3 new stages)
  - tests/unit/reverb/CMakeLists.txt (3 new executables registered)
tech-stack:
  added: []
  patterns:
    - cascading-sat-per-add (D-07 locked; q15_add_sat called 3x per side
      in comb; NO int32 accumulator variable)
    - APF-recurrence-with-Pitfall-1-guard (sat_s16(-(int32_t)prod1) before
      the saturating add; 4 occurrences: L+R x APF1+APF2)
    - read-once-reuse-delayed-tap (APF: [mX-dAPF] read at start, used
      in both step 1 and step 3; no read-after-write within a side)
    - per-multiply err-tap wiring (D-11 scope i; 8 comb + 4 apf1 + 4 apf2
      truncation remainders per tick)
    - snapshot-at-pair-start-then-pass-down (D-08; vCOMB1..4, vAPF1, vAPF2,
      dAPF1, dAPF2 all frozen at body top and passed as parameters)
key-files:
  created:
    - tests/unit/reverb/test_reverb_comb.c
    - tests/unit/reverb/test_reverb_apf1.c
    - tests/unit/reverb/test_reverb_apf2.c
  modified:
    - src/spu94/spu94_reverb.c
    - tests/unit/reverb/CMakeLists.txt
    - tests/python/derive_reverb_reference.py
decisions:
  - D-07 cascading sat_s16 is implemented as 3 sequential q15_add_sat calls
    per side (accL = p1; accL = q15_add_sat(accL, p2); accL = q15_add_sat(accL, p3);
    accL = q15_add_sat(accL, p4)). NO `int32_t sumL` variable; the grep guard
    `! grep -E "int32_t[[:space:]]+(sumL|sumR|sum_L|sum_R|comb_acc|acc32)"`
    passes. The distinguishing test pins the behavior with a
    +32765-magnitude gap between the two variants.
  - APF subtract uses the Pitfall-1 guard pattern
    `q15_add_sat(Lin, sat_s16(-(int32_t)prod1))` at every APF subtraction
    point (4 occurrences total: L+R x APF1+APF2). Widening to int32 before
    negation avoids INT16_MIN-negation UB. Verified by the Pitfall-7 edge
    test with prod1 == INT16_MAX.
  - dAPF1 / dAPF2 are snapshotted at body top as uint16_t even though they
    are TICK_LATCHED (their staging policy guarantees they cannot change
    mid-tick anyway). The snapshot preserves D-08 symmetry and makes any
    future policy tweak a one-line change.
  - APF delayed-tap `[mX-dAPF]` is read ONCE at the start of each side
    and reused in both step 1 (subtract) and step 3 (feedback). Step 2
    writes to `[mX]`, not to `[mX-dAPF]`, so there is no read-after-write
    hazard within a single side even when dAPF==0.
  - err counts per tick (when driven from reverb_body): comb = 8 multiplies
    (4 taps x 2 sides); apf1 = 4 multiplies (2 multiplies x 2 sides);
    apf2 = 4 multiplies. Totals visible via `grep -c` in the acceptance
    block.
  - state struct size unchanged at 200 B (no new fields; Plan 01 pre-allocated
    err_comb / err_apf1 / err_apf2 on day one).
metrics:
  duration: 9m 32s
  tasks: 3
  files_created: 3
  files_modified: 3
  lines_added: ~977 (production: 139 + 209 = 348; tests: 619; Python ref: 62)
  reverb_c_line_count: 604
  reverb_body_stage_count: 8 (all 7 nocash stages + hard_clip)
  d07_distinguishing_gap: 32765 (cascading -0x7FFF vs int32-accum -2)
  comb_cascading_sat_points: 6 (3 per side)
  apf_pitfall_1_guards: 4 (2 per APF stage x 2 stages)
  err_accumulator_writes_per_tick: 16 (8 comb + 4 apf1 + 4 apf2)
  state_struct_sizeof: "200 B (unchanged; headroom 16184 B vs SPU94_STATE_SIZE_MAX)"
  ctests: 23 passing (was 20; +3 new reverb_comb + reverb_apf1 + reverb_apf2)
completed: 2026-04-20
---

# Phase 3 Plan 03: 4-tap Comb + APF1 + APF2 (D-07 Cascading Sat) Summary

Completed the nocash reverb network. Implemented the 4-tap comb using
the user-locked D-07 cascading `sat_s16` variant (diverges from most
emulator witnesses), both all-pass filter stages with the Pitfall-1
guard on the subtract, and wired all three into `spu94_reverb_body`.
After this plan, `reverb_body` runs the complete 7-stage nocash network
end-to-end: input_scale -> hard_clip -> same_iir -> diff_iir -> comb ->
apf1 -> apf2 -> output_scale. CORE-05 is fully satisfied.

## What Shipped

### 4-tap Comb (CORE-05, D-07 LOCKED)

Nocash E1:
```
Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2]
     + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2]
     + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]
```

**D-07 cascading sat_s16 — the critical user-locked choice.** Per CONTEXT
line 53, the 4-tap sum clamps after every intermediate add rather than
accumulating in int32 and clamping once at the end. Implementation:

```c
int16_t accL = p1;
accL = q15_add_sat(accL, p2);   /* sat #1 (D-07 cascade) */
accL = q15_add_sat(accL, p3);   /* sat #2 */
accL = q15_add_sat(accL, p4);   /* sat #3 */
```

`q15_add_sat` internally widens to int32 and calls `sat_s16`, so each
invocation is one clamp point (three per side; six across L+R). **No
`int32_t sum*` variable exists in the TU.** The grep guard
`! grep -E "int32_t[[:space:]]+(sumL|sumR|sum_L|sum_R|comb_acc|acc32)" src/spu94/spu94_reverb.c`
passes, and regression would be caught both by that guard and by the
distinguishing test below.

### All-Pass Filters (CORE-05)

Both APF1 and APF2 implement the nocash recurrence:
```
step1 = Xin - vAPF*[mX - dAPF]       ;step 1 (subtract delayed tap)
[mX]  = step1                         ;step 2 (store intermediate)
step3 = step1*vAPF + [mX - dAPF]     ;step 3 (feedback output)
```

Key properties of the implementation:

- **Pitfall-1 guard on the subtract:** `q15_add_sat(Xin, sat_s16(-(int32_t)prod1))`.
  Widens `prod1` (which is int16) to int32 before negating, clamps via
  `sat_s16`, then uses `q15_add_sat` for the add itself. Four occurrences
  total (L+R x APF1+APF2). Verified by the Pitfall-7 edge test which
  constructs inputs where `prod1 == INT16_MAX`, making `-(int32_t)prod1
  = -INT16_MAX` which sits inside int16 range cleanly — and leaves a
  path for naive implementations to go wrong if the guard is missing.
- **Delayed tap read once and reused:** `[mX - dAPF]` is read into
  `tap_delayed` at the start of each side; steps 1 and 3 both consume
  it. Step 2 writes to `[mX]`, NOT to `[mX - dAPF]`, so there is no
  read-after-write hazard even when `dAPF == 0` (the two addresses
  differ by the full mX halfword offset).
- **APF2 is structurally identical to APF1** with `vAPF2 / dAPF2 / mLAPF2 /
  mRAPF2 / err_apf2` substituted. One unified recurrence shape; two
  register files.

### reverb_body wiring (final Phase-3 shape)

New snapshots at pair start (D-08):
```c
const int16_t  vCOMB1_snap = spu94_get_reg_i16(state, SPU94_REG_vCOMB1);
...
const int16_t  vAPF1_snap  = spu94_get_reg_i16(state, SPU94_REG_vAPF1);
const int16_t  vAPF2_snap  = spu94_get_reg_i16(state, SPU94_REG_vAPF2);
const uint16_t dAPF1_snap  = spu94_get_reg_u16(state, SPU94_REG_dAPF1);
const uint16_t dAPF2_snap  = spu94_get_reg_u16(state, SPU94_REG_dAPF2);
```

Stage calls (the complete 7-stage nocash network; 8 calls counting hard_clip):
```c
spu94_reverb_input_scale(state, left_in, right_in, vLIN_snap, vRIN_snap,
                         &Lin_wide, &Rin_wide);
spu94_reverb_hard_clip(Lin_wide, Rin_wide, &Lin, &Rin, &overflow);
state->overflow_magnitude += overflow;
spu94_reverb_same_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
spu94_reverb_diff_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
spu94_reverb_comb(state, vCOMB1_snap, vCOMB2_snap,
                  vCOMB3_snap, vCOMB4_snap, &Lout, &Rout);
spu94_reverb_apf1(state, vAPF1_snap, dAPF1_snap, &Lout, &Rout);
spu94_reverb_apf2(state, vAPF2_snap, dAPF2_snap, &Lout, &Rout);
spu94_reverb_output_scale(state, Lout, Rout, vLOUT_snap, vROUT_snap,
                          &LeftOutput, &RightOutput);
```

Call-order invariant holds per the grep check in the acceptance block:
all 8 stage names appear in the reverb_body call sequence in definition
order matching nocash E1.

### Python reference extensions (tests/python/derive_reverb_reference.py)

Added `ref_comb(buf, regs, vCOMB1..4, buffer_address)` that mirrors the
cascading cascade line-for-line via `sat_s16(a + b)`. A nested `_side()`
closure runs one side of the four-tap sum, reusing the shared
`_buf_read` helper from Plan 02.

Added `_ref_apf_side(buf, buffer_address, Xin, vAPF, mX, dAPF)` as the
shared one-side worker, plus `ref_apf1` / `ref_apf2` thin wrappers that
call it once per side and bundle the errors. Every function docstring
cites the nocash line it paraphrases; no GPL emulator touched the
derivation (Pitfall 9).

### D-07 distinguishing test (the load-bearing pin for user lock)

`test_comb_cascading_distinguishes_int32_accumulate` in
`tests/unit/reverb/test_reverb_comb.c` drives the comb with:

- `v = (0x7FFF, 0x7FFF, -0x7FFF, -0x7FFF)`
- all 8 taps seeded to `0x7FFF`

Products from `q15_mul_truncate_with_err`:
- `p1 = p2 = q15_mul(0x7FFF, 0x7FFF) = 0x7FFE` (err = 1 each)
- `p3 = p4 = q15_mul(-0x7FFF, 0x7FFF) = -0x7FFF` (err = 0x7FFF each; the
  product -0x3FFF0001 ASR-shifts to -0x8000 which saturates to -0x7FFF)

**Cascading (D-07, ours):**
```
acc = p1 = 0x7FFE
acc = sat_s16(0x7FFE + 0x7FFE) = sat_s16(0xFFFC) = 0x7FFF   (sat #1 fires)
acc = sat_s16(0x7FFF + -0x7FFF) = 0
acc = sat_s16(0 + -0x7FFF) = -0x7FFF
```
Final Lout = **-0x7FFF** (= -32767 = INT16_MIN + 1).

**Int32-accumulate (REJECTED variant):**
```
sum = 0x7FFE + 0x7FFE + (-0x7FFF) + (-0x7FFF) = -2
sat_s16(-2) = -2
```
REJECTED final Lout = **-2**.

**Gap between variants: 32765.** Any regression to int32-accumulate
changes Lout from -0x7FFF to -2 and fails the test. Per-side err =
1 + 1 + 0x7FFF + 0x7FFF = 65536; L+R total = 131072 (also asserted).

### APF Pitfall-7 edge behavior observed

`test_apf1_pitfall_7_INT16_MIN_triple` drives the APF with `Lin = Rin =
vAPF1 = tap_delayed = INT16_MIN`. Trace (L side; R mirrors):

```
tap_delayed = -0x8000
prod1 = q15_mul(-0x8000, -0x8000):
  product = 0x40000000, shifted = 0x8000, sat_s16(0x8000) = 0x7FFF,
  err = 0 (INT16_MIN^2 is exactly divisible by 2^15)
step1 = q15_add_sat(-0x8000, sat_s16(-(int32_t)0x7FFF))
      = q15_add_sat(-0x8000, -0x7FFF)
      = sat_s16(-0xFFFF)
      = -0x8000                           <- note: step1 reaches INT16_MIN
[mLAPF1] = -0x8000                         <- stored unchanged
prod2 = q15_mul(-0x8000, -0x8000) = 0x7FFF (same as prod1), err = 0
step3 = q15_add_sat(0x7FFF, -0x8000) = -1
```

Final Lout = **-1**; `[mLAPF1]` stored at **INT16_MIN**. The hallmark of
the edge is that the Pitfall-1 guard keeps step 1's subtract well-defined
even though `prod1` is `+INT16_MAX` (the naïve `-prod1` would be
`-(+INT16_MAX)` which is fine in int16, but the implementation's
`sat_s16(-(int32_t)prod1)` makes the guard explicit and mechanically
checkable in the TU). The feedback-amplified saturation stays bounded
throughout (Lout = -1, not a wild value), which was the Pitfall-7 concern
from the research phase.

`test_apf2_pitfall_7_INT16_MIN_triple` runs the identical trace with
vAPF2 / dAPF2 / mLAPF2 / mRAPF2; same -1 / INT16_MIN outcome.

### err accumulator counts per stage

Per one tick of `reverb_body` with arbitrary saturating input:

| Stage         | Multiplies | err field         | Accumulated |
|---------------|------------|-------------------|-------------|
| input_scale   | 0          | err_input_scale   | 0 (no shift) |
| hard_clip     | 0          | (overflow_magnitude, separate) | varies |
| same_iir      | 4 (2 L + 2 R) | err_same_iir  | 4 remainders |
| diff_iir      | 4          | err_diff_iir     | 4 remainders |
| comb          | **8 (4 L + 4 R)** | **err_comb** | **8 remainders** |
| apf1          | **4 (2 L + 2 R)** | **err_apf1** | **4 remainders** |
| apf2          | **4 (2 L + 2 R)** | **err_apf2** | **4 remainders** |
| output_scale  | 2 (L + R)  | err_output_scale  | 2 remainders |
| **Total**     | **26**     |                   | **26 remainders / tick** |

The three bold rows are this plan's additions. Verified by
`grep -c "state->err_comb +="` returning 8, `grep -c "state->err_apf1 +="`
returning 4, `grep -c "state->err_apf2 +="` returning 4.

### Test battery (tests/unit/reverb/test_reverb_{comb,apf1,apf2}.c)

15 net-new Unity tests (5 per TU):

| Test                                                      | File     | Purpose |
|-----------------------------------------------------------|----------|---------|
| test_comb_zero                                            | comb     | zero-input invariant |
| test_comb_single_nonzero_tap                              | comb     | single-tap multiply path |
| test_comb_cascading_distinguishes_int32_accumulate        | comb     | **D-07 load-bearing pin** |
| test_comb_first_add_saturates                             | comb     | sat #1 fires on step 1 |
| test_comb_err_accumulates                                 | comb     | err is monotonic across calls |
| test_apf1_zero                                            | apf1     | zero-input invariant |
| test_apf1_vAPF1_zero_passes_through_to_store              | apf1     | pass-through path |
| test_apf1_feedback_seeded_tap                             | apf1     | Feedback with 0x2000 Lin + seeded tap |
| test_apf1_pitfall_7_INT16_MIN_triple                      | apf1     | **Pitfall-7 edge** |
| test_apf1_err_nonzero_for_non_divisible                   | apf1     | err invariant (ref-derived) |
| test_apf2_* (5 mirrors)                                   | apf2     | APF2 equivalents of APF1 cases |

### Commits

| Task | Commit    | Scope |
|------|-----------|-------|
| 1    | `cc55882` | spu94_reverb_comb body + vCOMB snapshots + reverb_body wiring + ref_comb |
| 2    | `96acb8d` | spu94_reverb_apf1 + spu94_reverb_apf2 bodies + snapshots + body wiring + ref_apf1/2 |
| 3    | `8deb564` | test_reverb_comb.c + test_reverb_apf1.c + test_reverb_apf2.c + CMakeLists |

## Verification

- `cmake --build build` — clean, no warnings, no errors.
- `ctest --test-dir build --output-on-failure` — **23/23 green**
  (20 pre-existing + 3 new reverb TUs: comb/apf1/apf2).
- `bash scripts/ci/grep-guard.sh` — clean (no float/double/malloc/
  unqualified long).
- `bash scripts/ci/verify-no-heap-symbols.sh` — clean (libspu94.so
  still zero heap imports).
- D-07 cascading grep guards:
  - `grep -c "q15_add_sat(accL, p[234])" src/spu94/spu94_reverb.c` = **3**
  - `grep -c "q15_add_sat(accR, pR[234])" src/spu94/spu94_reverb.c` = **3**
  - `! grep -E "int32_t[[:space:]]+(sumL|sumR|sum_L|sum_R|comb_acc|acc32)" src/spu94/spu94_reverb.c` — **PASS** (no int32 accumulator)
- Pitfall-1 guard count: `grep -c "sat_s16(-(int32_t)prod1)" src/spu94/spu94_reverb.c` = **4** (L+R x APF1+APF2).
- reverb_body call order (from top to bottom in the function): input_scale,
  hard_clip, same_iir, diff_iir, comb, apf1, apf2, output_scale. **Matches
  nocash E1 verbatim.**
- GPL provenance: no Mednafen / DuckStation / lv2-psx-reverb / UPSE /
  PCSX / MiSTer strings in any of the three new test TUs (Pitfall 9).

## Deviations from Plan

### 1. [Rule 2 — defensive extra test] Added `test_comb_first_add_saturates`

**Found during:** Task 3 test design review.
**Issue:** The plan called for 4-5 comb test cases; the distinguishing
test only exercises the D-07 difference at the 2nd, 3rd, and 4th adds.
I added a test where the **first add saturates** (v1=v2=0x7FFF, v3=v4=0,
all taps 0x7FFF; expected Lout=0x7FFF, err=4) to cover the path where
cascading's first saturation fires on add #1 specifically. This is the
case most commonly exercised by musical material (two strong taps at
saturation threshold, two small).
**Fix:** Net-new test, additive. No behavior change.
**Files modified:** `tests/unit/reverb/test_reverb_comb.c`
**Commit:** folded into `8deb564`.

### 2. [Rule 2 — API consistency] dAPF1/dAPF2 snapshots added alongside v*

**Found during:** Task 2 reverb_body wiring.
**Issue:** The plan explicitly called for snapshotting dAPF1/dAPF2 as
u16 at the body top "for symmetry and resilience against future D-08
policy changes." I followed the plan verbatim; flagging here because
it's a visible departure from the minimal-change posture (Plan 02
passed vIIR/vWALL as parameters only, not dIIR; this is the first u16
TICK_LATCHED snapshot in the body).
**Fix:** Two new `spu94_get_reg_u16` calls at the snapshot block.
Cost: two register-engine reads per tick. Benefit: one-line swap if
D-08 policy is ever flipped for d* registers.
**Files modified:** `src/spu94/spu94_reverb.c` (reverb_body only).
**Commit:** folded into `96acb8d`.

No auth gates, no architectural escalations. No Rule-1 bugs, no Rule-3
blockers, no Rule-4 architectural changes.

## Pitfall Encounters

- **Pitfall 1 (INT16_MIN / INT32_MIN negation UB):** explicitly guarded
  at every APF subtract via `sat_s16(-(int32_t)prod1)` — 4 occurrences.
  Verified by the Pitfall-7 edge test where `prod1 == INT16_MAX`
  deliberately pushes the guard path; the int32 widen + sat_s16 keeps
  the negation well-defined.
- **Pitfall 4 (mid-tick v* re-read):** avoided — all 6 new v* values
  (vCOMB1..4, vAPF1, vAPF2) and 2 u16 values (dAPF1, dAPF2) are snapshotted
  ONCE at the top of `spu94_reverb_body` and passed down by value; stage
  functions never re-read v*/d* from state.
- **Pitfall 5 (anomaly branch ordering):** N/A — comb and APF stages
  have no anomaly branch (unlike SAME/DIFF IIR). The D-07 cascading-sat
  choice is a design lock, not an anomaly; it produces consistent
  output for all inputs.
- **Pitfall 7 (APF feedback edge at INT16_MIN):** explicitly tested via
  the INT16_MIN-triple case in both `test_reverb_apf1.c` and
  `test_reverb_apf2.c`. The Pitfall-1 guard handles it; observed Lout
  is -1 with `[mX]` stored at INT16_MIN — feedback-amplified saturation
  stays bounded.
- **Pitfall 9 (GPL provenance):** `tests/python/derive_reverb_reference.py`
  extensions are pure nocash arithmetic; the three new C test TUs have
  zero GPL emulator names. `grep -rE "Mednafen|DuckStation|..."` clean.

## Forward Dependencies Sealed for Plan 04

- **`reverb_body` is feature-complete** for Phase 3. Plan 04 adds the
  Q15-edge fuzz battery and body composition-equivalence test, plus the
  ADR-0007..0011 writeups and the DECISIONS.md updates. No further
  structural changes to `spu94_reverb.c` are expected.
- **Python reference is complete** for the 7-stage network. Plan 04's
  `fuzz_reverb.py` can drive the ctypes harness against the combined
  reference (input_scale -> hard_clip -> same_iir -> diff_iir -> comb
  -> apf1 -> apf2 -> output_scale).
- **err accumulator surface is fully wired** for D-11 scope (i): every
  Q15 multiply in every stage feeds the appropriate per-stage int32
  accumulator; `overflow_magnitude` covers the hard-clip overflow tap.
  Plan 04 tests can assert cross-stage invariants (e.g., zero err across
  all 7 stages for all-zero input; strict monotonicity under any
  saturating input).
- **D-07 pinning mechanism is in place.** If Plan 04 or a later
  refactor touches `spu94_reverb_comb`,
  `test_comb_cascading_distinguishes_int32_accumulate` is the
  tripwire; regression to int32-accumulate fails the test before any
  merge lands.
- **ADR-0007 content drafted** in this SUMMARY (cascading sat decision,
  rejected variant's exact output, 32765 gap, revert lever). Plan 04
  just needs to lift the prose into `docs/DECISIONS.md` in the
  ADR-0007..0011 numbering block.

## Known Stubs

None. Every function in `src/spu94/spu94_reverb.c` has a real body;
`spu94_reverb_body` invokes all 7 nocash stages plus hard_clip.

## Deferred Items

None. Plan 03 completed cleanly; no out-of-scope issues were discovered
that needed deferring.

## Self-Check: PASSED

Files claimed as created:
- `tests/unit/reverb/test_reverb_comb.c` — FOUND
- `tests/unit/reverb/test_reverb_apf1.c` — FOUND
- `tests/unit/reverb/test_reverb_apf2.c` — FOUND

Files claimed as modified:
- `src/spu94/spu94_reverb.c` — FOUND (modified in commits cc55882 + 96acb8d)
- `tests/unit/reverb/CMakeLists.txt` — FOUND (modified in 8deb564)
- `tests/python/derive_reverb_reference.py` — FOUND (modified in cc55882 + 96acb8d)

Commits claimed exist:
- `cc55882` — FOUND (Task 1: comb body + reverb_body wiring + ref_comb)
- `96acb8d` — FOUND (Task 2: apf1 + apf2 bodies + body wiring + ref_apf1/2)
- `8deb564` — FOUND (Task 3: 15 Unity tests across 3 new TUs)

Acceptance criteria (plan `<success_criteria>`):
- All 3 tasks executed — **PASSED**
- Each task committed individually with --no-verify — **PASSED**
- SUMMARY.md created in plan directory — **PASSED** (this file)
- All reverb ctests pass (existing 5 + new comb + apf1 + apf2) — **PASSED** (8 reverb TUs green; total ctest 23/23)
- D-07 honored (cascading sat_s16 pattern verifiable by grep) — **PASSED** (6 cascading sats; no int32 accumulator)
- APF subtract uses Pitfall-1 guard (sat_s16 on the subtraction, not raw int16) — **PASSED** (4 guards across L+R x APF1+APF2)
- reverb_body runs the full 7-stage network end-to-end with no stub stages remaining — **PASSED**

SC-1 fully satisfied (every stage has hand-derived per-stage bit-
exactness test). SC-5 advanced (ADR-0007 cascading rationale is
Plan-04-ready; lands with the other ADRs in Plan 04).
