---
phase: 03-core-reverb-algorithm-hard-clip
plan: 02
subsystem: reverb-iir
tags: [reverb, iir, vIIR-anomaly, err-accumulator, cross-side-tap, pitfall-1-guard]
requires:
  - spu94_reverb scaffold (Plan 01)
  - spu94_reverb_internal.h signatures (Plan 01)
  - state->err_same_iir / err_diff_iir fields (Plan 01)
  - reverb_body snapshot-at-pair-start v* reads (Plan 01)
  - Phase 2 buffer_address + 0x7FFFE wrap arithmetic (Plan 02-04)
  - q15_mul_truncate_with_err + sat_s16 + q15_add_sat (Phase 1)
provides:
  - src/spu94/spu94_reverb.c::reverb_buf_read / reverb_buf_write
    (static inline halfword-index helpers)
  - src/spu94/spu94_reverb.c::spu94_reverb_same_iir full body
  - src/spu94/spu94_reverb.c::spu94_reverb_diff_iir full body
  - vIIR=INT16_MIN anomaly branch at each of 4 memory-write points
    (2 in SAME + 2 in DIFF; L and R sides of each stage)
  - tests/python/derive_reverb_reference.py::ref_same_iir +
    ref_diff_iir (pure-nocash derivation, Pitfall 9 clean)
  - tests/unit/reverb/test_reverb_same_iir.c (6 Unity tests)
  - tests/unit/reverb/test_reverb_diff_iir.c (7 Unity tests)
affects:
  - src/spu94/spu94_reverb.c (spu94_reverb_body now calls same_iir +
    diff_iir; snapshots vIIR + vWALL)
  - tests/unit/reverb/CMakeLists.txt (two new executables registered)
tech-stack:
  added: []
  patterns:
    - halfword-index-buf-taps (reverb_buf_read/write take a u16
      halfword index; multiply × 2 inside to get the byte offset with
      Phase 2's 0x7FFFE wrap mask)
    - int32-widen-before-negate (Pitfall 1 guard applied at both the
      tap_prev subtract and the vIIR=INT16_MIN anomaly branch)
    - per-stage err accumulator via local int16 remainder +
      int32 accumulation (matches Plan 01's output_scale pattern)
    - snapshot-at-pair-start-then-pass-down (D-08; stages receive
      vIIR/vWALL as parameters, never re-read from state)
key-files:
  created:
    - tests/unit/reverb/test_reverb_same_iir.c
    - tests/unit/reverb/test_reverb_diff_iir.c
  modified:
    - src/spu94/spu94_reverb.c
    - tests/unit/reverb/CMakeLists.txt
    - tests/python/derive_reverb_reference.py
decisions:
  - vIIR=INT16_MIN anomaly branch is present at each of the FOUR
    memory-write points in spu94_reverb.c (2 SAME + 2 DIFF, both L
    and R per stage). Expression is the literal
    `if (vIIR_snap == INT16_MIN) { result = sat_s16(-(int32_t)result); }`
    per D-10 and Pitfall 1. grep count is exactly 4 per the plan's
    verification block.
  - tap_prev subtraction uses the same Pitfall-1 guard pattern
    `q15_add_sat(acc, sat_s16(-(int32_t)tap_prev))` — widening to int32
    before negation avoids the INT16_MIN case where `-tap_prev` would
    otherwise be UB. Applied at all 4 sides (2 stages × L,R).
  - reverb_buf_read/write take halfword indexes (u16) directly and
    multiply by 2 inside the helper. Callers pass `(mLSAME - 2u)` as
    a u16 and the helper handles byte offset + 0x7FFFE wrap. Matches
    Research Pattern 1 verbatim.
  - err_same_iir / err_diff_iir each accumulate TWO remainders per
    call per side (wall + iir) => 4 remainders per stage call total.
    Local int16 err variable + `state->err_... += (int32_t)err;`
    mirrors the Plan-01 output_scale wiring convention.
  - DIFF IIR cross-side register pairing is verified by a dedicated
    Unity test: only seeding dRDIFF's tap slot (and leaving dLDIFF's
    tap slot zero) yields asymmetric L vs R outputs. A same-side
    pairing regression would produce symmetric L=R output and the
    test would fail.
  - state struct size unchanged at 200 B (no new fields added — Plan
    01 pre-allocated err_same_iir / err_diff_iir).
metrics:
  duration: 10m 29s
  tasks: 3
  files_created: 2
  files_modified: 3
  lines_added: ~640
  anomaly_branch_count: 4
  same_iir_q15_multiplies: 4
  diff_iir_q15_multiplies: 4
  ctests: 20 passing (was 18; +2 new reverb TUs)
  state_struct_sizeof: "200 B (unchanged from Plan 01; headroom 16184 B)"
completed: 2026-04-20
---

# Phase 3 Plan 02: SAME IIR + DIFF IIR + vIIR=INT16_MIN Anomaly Summary

Implemented the two IIR reverb stages that replace the Plan-01 empty
stubs. Landed the `reverb_buf_read` / `reverb_buf_write` static-inline
buffer tap helpers that every later stage (Plan 03's comb + APF) will
reuse. Added the D-10 anomaly branch at each of the four memory-write
points (SAME L, SAME R, DIFF L, DIFF R) with the int32-widening Pitfall-1
guard protecting against INT16_MIN-negation UB. Wired the `vIIR` /
`vWALL` snapshots in `spu94_reverb_body` (D-08 pair-start freeze) and
extended `tests/python/derive_reverb_reference.py` with the pure-nocash
reference implementations the C tests cross-check against.

## What Shipped

### Buffer tap helpers (src/spu94/spu94_reverb.c, static inline)

```c
static inline int16_t reverb_buf_read(const spu94_state *s,
                                      uint16_t halfword_offset);
static inline void    reverb_buf_write(spu94_state *s,
                                       uint16_t halfword_offset,
                                       int16_t value);
```

Design choices:
- Parameter is a u16 **halfword index**; helper multiplies by 2 inside
  to get the byte offset. Matches Research Pattern 1 verbatim and
  allows callers to write `reverb_buf_read(s, mLSAME - 2)` naturally
  (the u16 subtract wraps mod 2^16).
- Byte offset formula: `(buffer_address + halfword_offset * 2) & 0x7FFFE`
  — same wrap rule as Phase 2 Plan 04's `spu94_buffer_advance`.
- Defensive `work_buf_size` check: if the caller supplied a sub-0x80000
  buffer, out-of-range reads return 0 and writes are discarded. The
  0x7FFFE mask alone already bounds byte_off to `< 0x80000`; the extra
  check handles smaller caller buffers (legal per Phase 2 init contract).
- Little-endian halfword serialization matches the Phase 2 buffer TU
  convention (host-endian-agnostic ring).

### SAME IIR (CORE-05, CORE-08)

Full L + R body matching nocash E1:
```
[mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]
[mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]
```

Each side: 2 Q15 multiplies via `q15_mul_truncate_with_err` feeding
`state->err_same_iir`. Four multiplies per call (L+R × wall+iir). The
tap_prev subtract uses `q15_add_sat(acc, sat_s16(-(int32_t)tap_prev))`
to guard the INT16_MIN edge. The anomaly branch runs **after** the
final `q15_add_sat` and **before** the `reverb_buf_write` (Pitfall 5).

### DIFF IIR (CORE-05, CORE-08)

Identical structure with the cross-side wall tap pairing per nocash:

```
[mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]   ;R-to-L
[mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]   ;L-to-R
```

L reads its wall tap through **dRDIFF** (R's delay register), R reads
through **dLDIFF**. This is the algorithmic difference from SAME IIR.
Four Q15 multiplies per call, all feeding `state->err_diff_iir`.

### D-10 anomaly branch — exactly 4 occurrences

`grep -c "if (vIIR_snap == INT16_MIN)" src/spu94/spu94_reverb.c` returns
**4** — one per memory-write point (SAME L, SAME R, DIFF L, DIFF R).
Every branch body is the literal:

```c
if (vIIR_snap == INT16_MIN) {
    result = sat_s16(-(int32_t)result);
}
```

The int32 widening is the Pitfall-1 guard: when `result == INT16_MIN`,
naïve `-result` is UB; widening + saturation clamps to INT16_MAX
instead. Verified by the dedicated
`test_{same,diff}_iir_anomaly_INT16_MIN_result_clamps_to_MAX` tests.

### reverb_body wiring (src/spu94/spu94_reverb.c)

Two new snapshots added at pair start (D-08):
```c
const int16_t vIIR_snap  = spu94_get_reg_i16(state, SPU94_REG_vIIR);
const int16_t vWALL_snap = spu94_get_reg_i16(state, SPU94_REG_vWALL);
```

Stage calls inserted after hard_clip, before the Plan 03 comb/APF insertion point:
```c
spu94_reverb_same_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
spu94_reverb_diff_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
```

### Python reference extensions (tests/python/derive_reverb_reference.py)

Added `_buf_read` / `_buf_write` shared helpers (keyed by byte offset)
plus:
- `ref_same_iir(buf, Lin, Rin, vIIR, vWALL, regs, buffer_address)` →
  returns `(new_buf_dict, err_total)`
- `ref_diff_iir(buf, Lin, Rin, vIIR, vWALL, regs, buffer_address)` →
  same shape, cross-side pairing

Each function docstring cites nocash E1. Pure Python integer math —
Python's `>>` on negative int is floor-toward-−∞ matching ADR-0001 ASR.
No GPL emulator touched the derivation.

### Test battery (tests/unit/reverb/test_reverb_{same,diff}_iir.c)

| Test | File | Purpose |
|------|------|---------|
| `test_same_iir_anomaly_vIIR_INT16_MIN_negates` | same_iir | CORE-08 anomaly path |
| `test_same_iir_control_vIIR_INT16_MIN_plus_1_no_negate` | same_iir | TEST-06 control |
| `test_same_iir_control_vIIR_INT16_MAX_no_negate` | same_iir | Second control (opposite edge) |
| `test_same_iir_anomaly_INT16_MIN_result_clamps_to_MAX` | same_iir | Pitfall-1 edge |
| `test_same_iir_err_zero_for_non_saturating` | same_iir | err invariant (divisible) |
| `test_same_iir_err_nonzero_for_saturating` | same_iir | err invariant (non-divisible) |
| `test_diff_iir_anomaly_vIIR_INT16_MIN_negates` | diff_iir | CORE-08 anomaly (DIFF side) |
| `test_diff_iir_control_vIIR_INT16_MIN_plus_1_no_negate` | diff_iir | TEST-06 control |
| `test_diff_iir_control_vIIR_INT16_MAX_no_negate` | diff_iir | Second control |
| `test_diff_iir_anomaly_INT16_MIN_result_clamps_to_MAX` | diff_iir | Pitfall-1 edge |
| `test_diff_iir_err_zero_for_non_saturating` | diff_iir | err invariant (divisible) |
| `test_diff_iir_err_nonzero_for_saturating` | diff_iir | err invariant (non-divisible) |
| `test_diff_iir_cross_side_pairing_dRDIFF_feeds_L` | diff_iir | **Distinguishing test** — proves L reads via dRDIFF, not dLDIFF |

Every expected value has a derivation comment tracing the nocash
arithmetic. Python reference cross-checked all rows before commit.

### Commits

| Task | Commit    | Scope                                                        |
|------|-----------|--------------------------------------------------------------|
| 1    | `4a9e300` | reverb_buf helpers + same_iir body + vIIR anomaly + Python   |
| 2    | `d6f1f07` | diff_iir body with cross-side wall taps + reverb_body wiring |
| 3    | `c48c66f` | 13 Unity tests across same_iir + diff_iir TUs                |

## Verification

- `cmake --build build --target spu94_shared` — clean, no warnings.
- `ctest --test-dir build --output-on-failure` — **20/20 green**
  (18 pre-existing + 2 new reverb TUs).
- `bash scripts/ci/grep-guard.sh` — clean (no `float`/`double`/`malloc`/
  unqualified `long`).
- `bash scripts/ci/verify-no-heap-symbols.sh` — clean
  (`libspu94.so` still has zero malloc/calloc/realloc/free imports).
- `grep -c "if (vIIR_snap == INT16_MIN)" src/spu94/spu94_reverb.c` → **4**.
- `sizeof(struct spu94_state)` → **200 B** (unchanged; headroom 16184 B
  vs `SPU94_STATE_SIZE_MAX = 16384`).
- GPL-provenance scan: no Mednafen / DuckStation / lv2-psx-reverb /
  UPSE / PCSX / MiSTer strings in either test TU (Pitfall 9).

## Deviations from Plan

### 1. [Rule 3 — Blocking] Comment syntax broke the build on first attempt

**Found during:** Task 3 first build
**Issue:** The doc comment `/* Stage d*/m* registers ... */` contained
the literal `*/` sequence in the middle of the comment (the shorthand
"d/m registers" was written as `d*/m*` for visual compactness). The
compiler terminated the comment early at the nested `*/`, leaving the
rest of the doc text as uncompilable C code.
**Fix:** Rewrote the comment to "Stage the d/m delay/memory registers
via public API (TICK_LATCHED policy)". Functional behavior unchanged.
**Files modified:** `tests/unit/reverb/test_reverb_same_iir.c`
**Commit:** folded into `c48c66f` (the initial Task 3 attempt failed to
compile and was corrected before the commit landed).

### 2. [Rule 1 — Bug in my derivation comment] INT16_MAX control remainder miscomputed on first draft

**Found during:** Task 3 test-value review
**Issue:** My first-draft derivation comment for
`test_same_iir_control_vIIR_INT16_MAX_no_negate` said the per-side
truncation remainder was 4096 (treating the MIN+1 case as the same
shape as MAX). The actual remainder for `0x1000 * 0x7FFF` is
`0x7FFF000 - (0xFFF << 15) = 0x7000 = 28672` per side. L+R = 57344.
**Fix:** Updated the comment to show the correct derivation and changed
the assertion from `TEST_ASSERT_EQUAL_INT32_MESSAGE(8192, ...)` to
`TEST_ASSERT_EQUAL_INT32_MESSAGE(57344, ...)`. Caught by cross-checking
against the Python `ref_same_iir` output before the commit landed.
**Files modified:** `tests/unit/reverb/test_reverb_same_iir.c`
**Commit:** folded into `c48c66f` (corrected before commit).

No auth gates, no architectural escalations.

## Pitfall Encounters

- **Pitfall 1 (INT16_MIN negation UB):** explicitly guarded in FOUR
  places — once per memory-write anomaly branch
  (`sat_s16(-(int32_t)result)`) and once per tap_prev subtract
  (`sat_s16(-(int32_t)tap_prev)`). The Pitfall-1 edge test
  (`test_{same,diff}_iir_anomaly_INT16_MIN_result_clamps_to_MAX`)
  constructs inputs that force the pre-anomaly result to INT16_MIN
  and asserts the anomaly branch produces INT16_MAX, not UB.
- **Pitfall 4 (mid-tick v* re-read):** avoided — `vIIR_snap` and
  `vWALL_snap` are read once at the top of `spu94_reverb_body` and
  passed down by value; neither stage function re-reads them from
  state.
- **Pitfall 5 (anomaly branch ordering):** verified — the branch runs
  **after** the final `q15_add_sat(iir_prod, tap_prev)` and **before**
  `reverb_buf_write`. Both the anomaly test and the non-anomaly
  controls verify the sign flip lands exactly at the memory write.
- **Pitfall 9 (GPL provenance):** `tests/python/derive_reverb_reference.py`
  extensions are pure nocash arithmetic; the two new C test TUs have
  zero GPL emulator names. `grep -rE "Mednafen|DuckStation|..."` clean.

## Forward Dependencies Sealed for Plan 03

- **`reverb_buf_read` / `reverb_buf_write` static inlines** in
  `spu94_reverb.c` are immediately reusable by `spu94_reverb_comb`,
  `spu94_reverb_apf1`, and `spu94_reverb_apf2` in Plan 03. The
  halfword-index convention and 0x7FFFE wrap semantic are the load-
  bearing contract — Plan 03 should pass u16 register values directly
  without re-wrapping.
- **`reverb_body` snapshot surface:** Plan 03 adds `vAPF1_snap /
  vAPF2_snap / vCOMB1..4_snap / dAPF1_snap / dAPF2_snap` at the existing
  "Plan 03 snapshot" comment in `reverb_body`. vIIR/vWALL are already
  there.
- **Python reference extension points:** `tests/python/derive_reverb_reference.py`
  now has two new functions plus shared `_buf_read` / `_buf_write`
  helpers that `ref_comb`, `ref_apf1`, `ref_apf2` will call directly.
- **err accumulator wiring pattern:** Plan 03 should follow the same
  `int16_t err = 0; ...with_err(a, b, &err); state->err_<stage> +=
  (int32_t)err;` pattern per multiply. Existing `err_comb`, `err_apf1`,
  `err_apf2` fields in `spu94_state` are ready.
- **Anomaly branch pattern:** Plan 03 does NOT need to repeat the
  vIIR=INT16_MIN branch — the anomaly is documented per nocash as
  applying specifically to the IIR stages. Comb / APF stages have
  different coefficients (vCOMB1..4, vAPF1/2) and no documented
  anomaly.

## Deferred Items

None. Plan 02 completed cleanly; no out-of-scope issues were discovered
that needed deferring.

## Self-Check: PASSED

Files claimed as created:
- `tests/unit/reverb/test_reverb_same_iir.c` — FOUND
- `tests/unit/reverb/test_reverb_diff_iir.c` — FOUND

Files claimed as modified:
- `src/spu94/spu94_reverb.c` — FOUND (modified in commits 4a9e300 + d6f1f07)
- `tests/unit/reverb/CMakeLists.txt` — FOUND (modified in c48c66f)
- `tests/python/derive_reverb_reference.py` — FOUND (modified in 4a9e300)

Commits claimed exist:
- `4a9e300` — FOUND (Task 1: reverb_buf helpers + same_iir + Python)
- `d6f1f07` — FOUND (Task 2: diff_iir + reverb_body wiring)
- `c48c66f` — FOUND (Task 3: 13 Unity tests)

Acceptance criteria (plan <success_criteria>):
- SC-1 (bit-exact against hand-derived reference) — advanced for
  SAME IIR + DIFF IIR
- SC-3 (vIIR = INT16_MIN negation tested + control) — **satisfied**
  (dedicated anomaly + two control tests per stage = 6 tests total)
- SC-4 (Q15 saturation edges) — advanced (IIR stages exercised; full
  battery in Plan 04)
- SC-5 (ADR-0010 content ready) — content drafted in commit messages +
  this Summary; ADR landing deferred to Plan 04 as the plan specifies

Plan 02 verification block:
- `cmake --build build --target spu94_shared` → green
- `ctest --output-on-failure --test-dir build` → 20/20 green
- `bash scripts/ci/grep-guard.sh` + `verify-no-heap-symbols.sh` → clean
- CORE-05 partial (SAME + DIFF IIR done; comb/APF Plan 03), CORE-08
  satisfied, TEST-06 satisfied
- Anomaly branch count = 4 (matches plan)
- `sizeof(struct spu94_state) <= 16384` — 200 B (headroom 16184 B)
