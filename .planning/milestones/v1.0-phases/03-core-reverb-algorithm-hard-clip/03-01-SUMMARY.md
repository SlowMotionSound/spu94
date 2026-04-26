---
phase: 03-core-reverb-algorithm-hard-clip
plan: 01
subsystem: reverb-scaffold
tags: [reverb, hard-clip, internal-header, err-accumulator, overflow-magnitude]
requires:
  - spu94_state (Phase 2 Plan 01)
  - spu94_tick hooks (Phase 2 Plan 02/04)
  - spu94_get_reg_i16/u16 engine layer (Phase 2 Plan 03)
  - q15_mul_truncate_with_err + sat_s16 (Phase 1 + Plan 02-02)
provides:
  - src/spu94/spu94_reverb_internal.h (internal header with 9 declarations)
  - src/spu94/spu94_reverb.c (8 stage functions + reverb body, 3 real + 5 stubs)
  - struct spu94_state err accumulators (7) + overflow_magnitude
  - tests/unit/reverb/ harness + 3 Plan-01 stage tests
  - tests/python/derive_reverb_reference.py (nocash-only reference model)
affects:
  - src/spu94/spu94_tick.c (reverb_body inserted as third statement)
  - src/spu94/spu94_state_internal.h (struct grew 168 -> 200 B)
  - tests/unit/CMakeLists.txt (add_subdirectory reverb)
tech-stack:
  added: []
  patterns:
    - internal-header-scaffold (D-01; mirrors spu94_state_internal.h)
    - stub-bodies-for-link-safety (Plan 02/03 stages declared and stubbed)
    - snapshot-at-pair-start (D-08; v* read once per tick in reverb_body)
    - q15-oracle-cross-check (output_scale tests use q15_mul_truncate_with_err as oracle)
key-files:
  created:
    - src/spu94/spu94_reverb_internal.h
    - src/spu94/spu94_reverb.c
    - tests/unit/reverb/CMakeLists.txt
    - tests/unit/reverb/test_reverb_hard_clip.c
    - tests/unit/reverb/test_reverb_input_scale.c
    - tests/unit/reverb/test_reverb_output_scale.c
    - tests/python/derive_reverb_reference.py
  modified:
    - src/spu94/spu94_state_internal.h
    - src/spu94/spu94_tick.c
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - Plan-01 stubs Plan-02/03 stages as empty no-ops so libspu94 links
    and later plans replace bodies (not create them).
  - test_reverb_output_scale uses the Phase-1 q15 helper as its oracle
    rather than hand-derived hex math — strictly stronger + decouples
    the table from ADR-0001 remainder-sign subtleties.
  - Hard-clip overflow_magnitude fires symmetrically around zero:
    |INT16_MIN|=0x8000 exceeds INT16_MAX=0x7FFF by 1, so the
    (INT16_MIN, INT16_MIN) case correctly contributes overflow=2.
metrics:
  duration: 6m 13s
  tasks: 3
  files_created: 7
  files_modified: 4
  lines_added: ~840
  state_struct_sizeof_delta: "+32 B (168 -> 200)"
  ctests: 18 passing (3 new reverb + 15 pre-existing)
completed: 2026-04-20
---

# Phase 3 Plan 01: Reverb Scaffold + Hard-Clip + Scale Stages Summary

Scaffolded the Phase 3 reverb body architecture (internal header, single
TU, insertion into `spu94_tick` between `buffer_advance` and tick exit)
and landed the three non-network stages that can be implemented without
yet reading tap-buffer state: `spu94_reverb_input_scale`,
`spu94_reverb_hard_clip` (CORE-02, independently testable per SC-2), and
`spu94_reverb_output_scale`. Extended `struct spu94_state` with the
D-11 error-tap + overflow-magnitude observability surface. Plans 02/03
now have a link-safe foundation with no-op stubs waiting to be replaced.

## What Shipped

### Stage function signatures (locked for Plans 02/03 consumption)

```c
void spu94_reverb_body         (spu94_state *state);

void spu94_reverb_input_scale  (spu94_state *state,
                                int16_t left_in, int16_t right_in,
                                int16_t vLIN_snap, int16_t vRIN_snap,
                                int32_t *Lin_out, int32_t *Rin_out);
void spu94_reverb_hard_clip    (int32_t Lin_wide, int32_t Rin_wide,
                                int16_t *Lin_out, int16_t *Rin_out,
                                int32_t *overflow_out);
void spu94_reverb_same_iir     (spu94_state *state,
                                int16_t Lin, int16_t Rin,
                                int16_t vIIR_snap, int16_t vWALL_snap);
void spu94_reverb_diff_iir     (spu94_state *state,
                                int16_t Lin, int16_t Rin,
                                int16_t vIIR_snap, int16_t vWALL_snap);
void spu94_reverb_comb         (spu94_state *state,
                                int16_t vCOMB1_snap, int16_t vCOMB2_snap,
                                int16_t vCOMB3_snap, int16_t vCOMB4_snap,
                                int16_t *Lout_out, int16_t *Rout_out);
void spu94_reverb_apf1         (spu94_state *state,
                                int16_t vAPF1_snap, uint16_t dAPF1_snap,
                                int16_t *Lout_inout, int16_t *Rout_inout);
void spu94_reverb_apf2         (spu94_state *state,
                                int16_t vAPF2_snap, uint16_t dAPF2_snap,
                                int16_t *Lout_inout, int16_t *Rout_inout);
void spu94_reverb_output_scale (spu94_state *state,
                                int16_t Lout, int16_t Rout,
                                int16_t vLOUT_snap, int16_t vROUT_snap,
                                int32_t *LeftOutput_out,
                                int32_t *RightOutput_out);
```

Plans 02 and 03 replace the empty stub bodies without touching the
header or any caller. The `body` caller already marks insertion points
in comments.

### `struct spu94_state` layout delta

| Field                        | Type      | Purpose                                       |
|------------------------------|-----------|-----------------------------------------------|
| `err_input_scale`            | `int32_t` | Truncation remainder accumulator (stays zero — no `>>15` in this stage) |
| `err_same_iir`               | `int32_t` | Plan 02 consumer                              |
| `err_diff_iir`               | `int32_t` | Plan 02 consumer                              |
| `err_comb`                   | `int32_t` | Plan 03 consumer                              |
| `err_apf1`                   | `int32_t` | Plan 03 consumer                              |
| `err_apf2`                   | `int32_t` | Plan 03 consumer                              |
| `err_output_scale`           | `int32_t` | Written by Plan-01 output_scale stage         |
| `overflow_magnitude`         | `int32_t` | Accumulated by Plan-01 reverb_body from hard_clip |

Struct grew from **168 B → 200 B** (+32 B, 8 × `int32_t`). The
`_Static_assert(sizeof <= SPU94_STATE_SIZE_MAX=16384)` guard still
passes with **16184 B of headroom**.

### `spu94_tick` body (now final for Phase 3 foundations)

```
1. spu94_apply_pending_writes(state);   // Phase 2 Plan 03
2. spu94_buffer_advance(state);         // Phase 2 Plan 04
3. spu94_reverb_body(state);            // Phase 3 Plan 01 (NEW)
```

Pitfall 4 discipline preserved — `grep -c "spu94_reverb_body(state);"
src/spu94/spu94_tick.c` returns **1**.

### Commits

| Task | Commit    | Scope                                                             |
|------|-----------|-------------------------------------------------------------------|
| 1    | `b2c4452` | Internal header + state struct err/overflow fields                |
| 2    | `3eaecec` | spu94_reverb.c (3 real stages + 5 stubs + body) + tick wiring     |
| 3    | `2a81f5d` | 3 unit tests (hard_clip, input_scale, output_scale) + Python ref  |

## Verification

- `ctest --test-dir build` → **18/18 green** (3 new reverb + 15 existing Phase 1/2)
- `bash scripts/ci/grep-guard.sh` → clean (no float/double/malloc/unqualified long)
- `bash scripts/ci/verify-no-heap-symbols.sh` → clean
- `nm build/src/spu94/libspu94.so | grep " T spu94_reverb_"` → all 9 symbols exported
  (body, input_scale, hard_clip, same_iir, diff_iir, comb, apf1, apf2, output_scale)
- `test ! -e include/spu94/spu94_reverb_internal.h` → internal header NOT on public path (D-01)
- `python3 tests/python/derive_reverb_reference.py --dump` → prints tables without error
- `grep -E "Mednafen|DuckStation|lv2-psx-reverb|UPSE|PCSX|MiSTer" tests/python/derive_reverb_reference.py` → no matches (Pitfall 9 GPL-provenance clean)

## Deviations from Plan

### 1. [Rule 3 — Blocking] Internal header include ordering

**Found during:** Task 2 build
**Issue:** `spu94_reverb_internal.h` originally included only
`<spu94/spu94_registers.h>`, but that sub-header's engine-layer setter
declarations return `spu94_result_t` (defined in `<spu94/spu94.h>`).
Compiling `spu94_reverb.c` failed with `unknown type name
'spu94_result_t'`.
**Fix:** Added `#include <spu94/spu94.h>` to the internal reverb
header before the registers include. Matches the ordering convention
in the umbrella header itself (result-code enum declared before the
registers sub-header is pulled in).
**Files modified:** `src/spu94/spu94_reverb_internal.h`
**Commit:** folded into `3eaecec` (same Task-2 commit that added the
include; the initial attempt failed to compile and was corrected
before the commit landed).

### 2. [Rule 1 — Bug in test table] hard_clip (INT16_MIN, INT16_MIN) overflow expectation

**Found during:** Task 3 first ctest run
**Issue:** The plan's suggested test row for
`(INT16_MIN, INT16_MIN)` expected `overflow_magnitude = 0` on the
reasoning that "INT16_MIN is in range for `sat_s16`." However, the
overflow-magnitude formula is `|x| - INT16_MAX` applied to the
raw int32 input, and `|INT16_MIN| = 0x8000 > INT16_MAX = 0x7FFF`, so
each side contributes `1`, sum `= 2`.
**Fix:** Updated the test row to expect `overflow = 2` with an
inline comment explaining the symmetric-around-zero semantic. The
Python reference script already computed the same value — confirming
the production code is correct; only the test expectation was wrong.
**Files modified:** `tests/unit/reverb/test_reverb_hard_clip.c`
**Commit:** folded into `2a81f5d` (corrected before commit landed).

## Pitfall Encounters

- **Pitfall 1 (INT16_MIN / INT32_MIN negation UB):** avoided in
  `spu94_reverb_hard_clip` by widening `Lin_wide`/`Rin_wide` to `int64_t`
  before taking absolute value. Verified by the `INT32 extremes` test
  row with `Lin_wide = INT32_MAX`, `Rin_wide = INT32_MIN`.
- **Pitfall 4 (single call site):** preserved — `spu94_reverb_body` is
  called exactly once from `spu94_tick`. Confirmed by `grep -c`.
- **Pitfall 9 (GPL provenance):** `tests/python/derive_reverb_reference.py`
  is pure-Python integer math citing only psx-spx. grep confirms no
  Mednafen / DuckStation / lv2-psx-reverb / UPSE / PCSX / MiSTer strings.

No IIR / comb / APF pitfalls encountered in Plan 01 — those stages are
empty stubs awaiting Plans 02 and 03.

## Forward Dependencies Sealed for Plan 02

- **`struct spu94_state`** has the `err_same_iir` and `err_diff_iir`
  fields pre-allocated — Plan 02 writes to them without bumping any
  `SPU94_STATE_SIZE_MAX` macro.
- **`spu94_reverb_same_iir` / `spu94_reverb_diff_iir` signatures** are
  frozen (see Stage function signatures above). Plan 02 replaces the
  empty stub bodies; `spu94_reverb_body` already snapshots
  `vLIN/vRIN/vLOUT/vROUT` but leaves `vIIR/vWALL` snapshotting as an
  explicit comment for Plan 02 to uncomment alongside the same_iir +
  diff_iir calls.
- **Test harness** (`tests/unit/reverb/CMakeLists.txt`,
  `add_subdirectory(reverb)` in `tests/unit/CMakeLists.txt`) is in
  place — Plan 02 adds `test_reverb_same_iir.c` / `test_reverb_diff_iir.c`
  with one-line CMake additions.
- **`tests/python/derive_reverb_reference.py`** has a docstring comment
  listing the stages Plans 02/03 will add. The module's `sat_s16` and
  `q15_mul_truncate_with_err` helpers are the Plan-02 reference
  implementation's basis.

## Forward Dependencies Sealed for Plan 03

- Same as above for comb/apf1/apf2: err fields pre-allocated, stub
  bodies in place, signatures frozen.
- `spu94_reverb_body` already propagates the `Lin`/`Rin` int16 pair
  from hard_clip to the insertion-point comment, and passes a zeroed
  `Lout`/`Rout` into `output_scale`. Plan 03 rewires this to receive
  APF2's outputs.

## Self-Check: PASSED

All files claimed as created exist:
- `src/spu94/spu94_reverb_internal.h` — FOUND
- `src/spu94/spu94_reverb.c` — FOUND
- `tests/unit/reverb/CMakeLists.txt` — FOUND
- `tests/unit/reverb/test_reverb_hard_clip.c` — FOUND
- `tests/unit/reverb/test_reverb_input_scale.c` — FOUND
- `tests/unit/reverb/test_reverb_output_scale.c` — FOUND
- `tests/python/derive_reverb_reference.py` — FOUND

All commits claimed exist:
- `b2c4452` — FOUND (Task 1)
- `3eaecec` — FOUND (Task 2)
- `2a81f5d` — FOUND (Task 3)
