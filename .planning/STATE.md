---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
last_updated: "2026-04-19T20:33:18.963Z"
progress:
  total_phases: 8
  completed_phases: 1
  total_plans: 9
  completed_plans: 8
  percent: 89
---

# STATE: SPU-94

**Last updated:** 2026-04-19

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 02 — buffer-register-infrastructure

## Current Position

Phase: 02 (buffer-register-infrastructure) — EXECUTING
Plan: 5 of 5

- **Milestone:** 1 (v1.0)
- **Phase:** 2
- **Plan:** 02-04 complete; ready for 02-05
- **Status:** Executing Phase 02
- **Progress:** [█████████░] 89%

```
[█.......] 1/8 phases complete (Plans 02-01, 02-02, 02-03, 02-04 of Phase 02 done; 1 plan remains in Phase 02)
```

## Performance Metrics

- Phases completed: 1
- Plans completed: 8 (Phase 1: 4, Phase 2: 4)
- Requirements validated: 25 / 49 (Phase 1: 13, Plan 02-01: API-01/02/07/09, Plan 02-02: CORE-04, API-04, API-07 reaffirmed, Plan 02-03: CORE-04 reaffirmed, CORE-10 partial, API-04 reaffirmed, Plan 02-04: CORE-03, CORE-10 complete)

| Plan | Duration | Tasks | Files | Notes |
|------|----------|-------|-------|-------|
| 02-01 | ~5m 24s | 3 (1 TDD) | 13 | spu94_state chassis + verify-no-heap CI + API-07 consumer tests |
| 02-02 | ~7m 7s | 3 (2 TDD) | 11 | register identity (35-enum + tables) + q15 error tap + spu94_tick stub + ADR-0004 |
| 02-03 | ~11m 44s | 4 (2 TDD) | 15 | engine register I/O + 35-entry policy table + facade (105 wrappers) + ADR-0005 |
| 02-04 | ~6m 18s | 2 (1 TDD) | 9 | buffer arithmetic + mBASE snap-on-write + ADR-0006 + spu94_get_buffer_address |

## Accumulated Context

### Key Decisions (locked)

- Build from spec (nocash psx-spx), not by porting GPL emulators.
- Plain C99/C11 core, hand-rolled fixed-point, caller-allocated state, zero heap in hot path.
- ctypes Python bindings; scikit-build-core + cibuildwheel; dr_wav vendored for CLI WAV I/O.
- Linux primary; Cortex-M7 cross-compile smoke test as MCU-portability proof inside M1.
- 22.05 kHz internal reverb tick with 39-tap half-band FIR at both I/O boundaries (bit-faithful at the boundary, closing the lv2-psx-reverb gap).
- All 33 SPU reverb-affecting registers implemented (not 24; corrected during research).
- DECISIONS.md is a first-class M1 deliverable.
- LEVERS-CATALOG.md catalogued during M1, used by M4 (lever abstraction deferred).
- License pick (MIT vs Apache-2.0) deferred to end of M1.

### Phase 2 Plan 01 Decisions (locked)

- SPU94_STATE_SIZE_MAX = 16384u; actual sizeof(struct spu94_state) at end of Plan 01 = 168 bytes (97× headroom; `_Static_assert` pins it).
- SPU94_STATE_ALIGN_MAX = 16u (covers int64_t + future SIMD on every M1 target).
- spu94_init contract: NULL/undersized/misaligned state_buf → NULL; NULL+0 work_buf legal; NULL+nonzero work_buf → NULL.
- spu94_reset contract: zero work buffer + zero state, preserve work_buf pointer + size.
- No `<string.h>` in core; hand-rolled byte-loop zero-fill keeps libspu94.so heap-import-free (verified by both `nm -u` and `readelf -r`).
- `_Static_assert -> static_assert` aliased under `__cplusplus` in spu94_q15.h to satisfy API-07 C++ consumer compile.
- verify-no-heap-symbols.sh wired as its own CI job (matches grep-guard / clang-tidy / cppcheck / ubsan one-concern-per-job style).
- `pending_mask` is uint64 (35 bits used, 29 reserved) so `__builtin_ctzll` works on the full mask.

### Phase 2 Plan 02 Decisions (locked)

- `spu94_reg_t` enum order: vLOUT, vROUT, mBASE, then reverb block 0x1DC0..0x1DFE in ascending hardware-offset order (vLIN/vRIN at indices 33/34). `SPU94_REG__COUNT = 35` pinned by `_Static_assert` in spu94_registers.c AND in test_register_identity.c.
- `spu94_reg_name` returns the BARE name (e.g., "vIIR", not "SPU94_REG_vIIR") per CONTEXT D-17.
- Out-of-range `spu94_reg_name` returns NULL (not "" empty string); out-of-range `spu94_reg_hw_offset` returns 0xFFFF.
- `q15_mul_truncate_with_err` remainder is PRE-saturation: `INT16_MIN^2` returns result=INT16_MAX, err=0 (the saturation discard is recoverable separately). Documented in header AND in ADR-0004 with a revision-path note.
- `q15_mul_truncate` is now a one-line wrapper passing err_out=NULL — bit-identical to Phase 1; reference test table unchanged.
- `spu94_tick(spu94_state*)` lives in src/spu94/spu94_tick.c with an empty body. NULL-safe. Plans 03/04/Phase 3 fill it in.
- `spu94_state` typedef has a SINGLE home: forward-declared in spu94_registers.h. spu94.h does NOT re-typedef it (would break -std=c99 -pedantic / API-07).
- `spu94_registers.h` does NOT include `spu94/spu94.h` — only forward-declares the opaque type. Sub-headers must never include the umbrella header (one-way include rule).
- `spu94_placeholder.c` removed in this plan; src/spu94 now contains spu94_state.c + spu94_registers.c + spu94_tick.c (three real TUs).
- ADR-0004 added at line 33 of docs/DECISIONS.md (prepended above ADR-0001) — documents q15 error tap + spu94_tick as intentional public seams per D-22/D-23/D-24.

### Phase 2 Plan 03 Decisions (locked)

- Engine layer (6 typed accessors) lives in `src/spu94/spu94_register_io.c`; signedness validated at runtime via a 64-bit packed mask (23 bits set for u16 family). TYPE_MISMATCH is a pure no-op on data — does NOT clear a previously staged pending value.
- 35-entry `spu94_write_policy_table[]` in `src/spu94/spu94_write_policy.c` is the D-05 swappable seam — 13 IMMEDIATE (12 v* + mBASE) + 22 TICK_LATCHED. Pinned for SPU-94; Controllers re-points it via re-link.
- `spu94_apply_pending_writes` in `src/spu94/spu94_pending.c` is called from EXACTLY one place (first line of `spu94_tick`). Sequential 35-bit scan, branch-predictable, portable (no `__builtin_ctzll`).
- IMMEDIATE writes mirror the new value into `pending_values[]` AND clear the mask bit, so `spu94_get_reg_*_pending` always returns a meaningful value for IMMEDIATE-policy registers (D-06 contract).
- `spu94_mbase_on_write` Plan-03 stub lives in `spu94_write_policy.c` (not a public header — internal symbol). Plan 04 lifts the body to `state->buffer_address = (uint32_t)new_mbase;` per ADR-0006.
- Internal-only header `src/spu94/spu94_state_internal.h` holds the single ODR home for `struct spu94_state` + the sizeof `_Static_assert`. NEVER under `include/spu94/`. The alignof guard stays in `spu94_state.c`.
- `spu94_result_t` enum reordered above the `<spu94/spu94_registers.h>` include in `spu94.h` so engine setter signatures (which return `spu94_result_t`) declared in the registers sub-header can refer to it without a circular umbrella include.
- Facade header `include/spu94/spu94_register_facade.h` ships 105 hand-written `static inline` wrappers (35 setters + 35 active getters + 35 pending getters). Zero linker surface (verified via `nm`).
- `spu94_snapshot_registers` now reads `state->reg_values[]` (Plan 02 zero-fill stub replaced).
- ADR-0005 added at line 33 of `docs/DECISIONS.md` (prepended above ADR-0004) — documents the split write-timing policy + per-register assignments + seam structure + Pitfall 4 protection.
- `sizeof(struct spu94_state) == 168 bytes` unchanged from end of Plan 01 (Plan 03 used reserved fields, did not add new ones; 16216 bytes headroom remaining vs `SPU94_STATE_SIZE_MAX`).

### Phase 2 Plan 04 Decisions (locked)

- BufferAddress wrap formula `MAX(mBASE, (buffer_address + 2) AND 0x7FFFE)` implemented in byte arithmetic in `src/spu94/spu94_buffer.c::spu94_buffer_advance` using an inline ternary (`(advanced > mbase) ? advanced : mbase`) — no `max()` macro to hide intent (acceptance criterion).
- mBASE snap-on-write per ADR-0006: `spu94_mbase_on_write(state, new_mbase)` assigns `state->buffer_address = (uint32_t)new_mbase` verbatim. No bit-0 mask (bit-faithful per T-02-18); no implicit work-buffer clear; audible discontinuity accepted as hardware-accurate.
- `spu94_mbase_on_write` definition relocated from `spu94_write_policy.c` (Plan 03 stub) to `spu94_buffer.c` (Plan 04 real body). ODR preserved — `nm` confirms exactly one `T spu94_mbase_on_write` symbol, in `spu94_buffer.o`. The forward declaration in `spu94_register_io.c` (sole caller) was unchanged; satisfied at link time by the new home.
- Public observability accessor `spu94_get_buffer_address(const spu94_state *)` added to `include/spu94/spu94.h` — returns `uint32_t`; NULL-safe (returns 0). D-23 read-only observability principle.
- `spu94_tick` body now in its final Phase-2 shape: `apply_pending_writes` → `buffer_advance`. Pitfall 4 still satisfied — each helper has exactly one call site. Phase 3 inserts the reverb-network computation as the third line.
- `spu94_buffer_advance` is INTERNAL (not on public header). Forward-declared at the top of `spu94_buffer.c` (satisfies `-Werror=missing-prototypes`) and at the call site in `spu94_tick.c` (only caller). Promotion to public symbol would require a new ADR.
- ADR-0006 added at line 33 of `docs/DECISIONS.md` (prepended above ADR-0005). Snap-on-write resolution + wrap formula + D-11 seam + bit-0 pass-through pin + audible-discontinuity acceptance + three revision paths. Paraphrase discipline upheld; psx-spx URL cited; verbatim sentence absent.
- ADR-0005 left intact per "accepted ADRs not edited in place" Discipline rule. Its reference to `spu94_write_policy.c` as home of `spu94_mbase_on_write` is now historical; ADR-0006 records the relocation explicitly in its Sources.
- `sizeof(struct spu94_state) == 168 bytes` unchanged from end of Plan 03 (Plan 04 added no new struct fields; `buffer_address` was already reserved by Plan 01).
- Tests directory `tests/unit/buffer/` created with Unity suite `buffer_basic_unit` (11 sub-tests). Plan 05 owns the formal `test_buffer_wrap.c`, `test_buffer_mbase.c` (with full sentinel sweep for work-buf-unchanged), and the Python ctypes 10⁶-step fuzz harness `tests/python/fuzz_buffer.py`.

### Gray-Area Decisions Pending (to be logged in DECISIONS.md)

- Phase 1: Q15 multiply semantics (`>> 15` direction); vIIR = -0x8000 policy.
- Phase 2: per-register mid-stream write policy (RESOLVED Plan 03 → ADR-0005); mBASE-write side effect (RESOLVED Plan 04 → ADR-0006 snap-on-write).
- Phase 3: comb-sum intermediate accumulation precision; register-write timing between L-tick and R-tick.
- Phase 4: lv2-psx-reverb witness exclusion on frequency-response axis (documented).
- Phase 7: witness-diff tolerance calibration per preset.

### Open Questions

- Comb-sum intermediate precision — nocash silent; resolve in Phase 3 with witness check.
- mBASE-write buffer behavior — RESOLVED via Phase 2 research as snap-on-write (ADR-0006); Plan 04 landed the implementation in `src/spu94/spu94_buffer.c` through the D-11 seam.
- FIR integer accumulation width — verify 32-bit intermediate suffices for 39-tap Q15 × int16 sum in Phase 4.

### Blockers

None.

### Todos

- Continue Phase 2: execute Plan 02-05 (per-register policy test battery + buffer-corner tests + Python ctypes 10⁶-step fuzz harness).
- Plan 05: `tests/unit/buffer/test_buffer_wrap.c` (formula corners — wrap-to-zero, mBASE-floor, single-tick advance from arbitrary address).
- Plan 05: `tests/unit/buffer/test_buffer_mbase.c` (snap behavior + full sentinel sweep for the work-buffer-unchanged invariant).
- Plan 05: `tests/python/fuzz_buffer.py` — ctypes harness running 10⁶ random ops with per-step invariant `buffer_address >= mBASE && buffer_address <= 0x7FFFE && ((buffer_address & 1) == 0 OR last_op_was_odd_mbase_snap)` (T-02-28).
- Plan 05: per-register policy test battery (35 registers × {set, set-then-tick, set-wrong-type} × policy expectation) — completes ADR-0005's test obligation.

## Session Continuity

### Last Session (2026-04-19)

- Executed Phase 2 Plan 04: created `src/spu94/spu94_buffer.c` with three functions — `spu94_buffer_advance` (CORE-03 wrap formula, byte arithmetic, inline ternary), `spu94_mbase_on_write` (real snap-on-write body, lifted from Plan 03 stub), `spu94_get_buffer_address` (public observability accessor, D-23).
- Wired `spu94_buffer_advance` into `spu94_tick` after `apply_pending_writes` (final Phase-2 tick body shape).
- Removed the Plan-03 stub of `spu94_mbase_on_write` from `spu94_write_policy.c` (ODR preserved; nm confirms exactly one definition, now in `spu94_buffer.o`).
- Added ADR-0006 to `docs/DECISIONS.md` (line 33, prepended above ADR-0005) — snap-on-write resolution + wrap formula + D-11 seam + bit-0 pass-through pin + paraphrase-only discipline (psx-spx URL cited; verbatim sentence absent).
- New test suite `tests/unit/buffer/test_buffer_basic.c` with 11 Unity cases covering null-safety, init/reset zeroing, single/multi-tick advance, wrap-from-top, mBASE floor, snap-on-write, odd-mBASE pass-through, work-buffer-unchanged invariant, and tick-order observability. ctest 8/8 green.
- Auto-fixed: -Werror=missing-prototypes on the two new internal symbols (added forward decls at top of `spu94_buffer.c`); leftover `spu94_mbase_on_write` mentions in `spu94_write_policy.c` comments after stub removal (reworded to "the mBASE write-side-effect handler"); ADR-0006 wrap formula notation switched from lowercase `max(...)` to uppercase `MAX(...)` to satisfy the `grep -q 'MAX' docs/DECISIONS.md` acceptance criterion.
- 3 commits land Plan 04 (Task 1 RED `b6d03bc`, Task 1 GREEN `ecd17d4`, Task 2 ADR `4b55f86`); ctest 8/8 green; grep-guard + verify-no-heap green.

### Next Session

- `/gsd-execute-phase` (continue) — execute Plan 02-05 (per-register policy test battery + buffer-corner tests + Python ctypes 10⁶-step fuzz harness; closes ADR-0005 + ADR-0006 test obligations and ROADMAP Phase 2 SC 3).

---
*State initialized: 2026-04-18 at roadmap completion*
