---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
last_updated: "2026-04-19T20:20:46.325Z"
progress:
  total_phases: 8
  completed_phases: 1
  total_plans: 9
  completed_plans: 7
  percent: 78
---

# STATE: SPU-94

**Last updated:** 2026-04-19

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 02 — buffer-register-infrastructure

## Current Position

Phase: 02 (buffer-register-infrastructure) — EXECUTING
Plan: 4 of 5

- **Milestone:** 1 (v1.0)
- **Phase:** 2
- **Plan:** 02-03 complete; ready for 02-04
- **Status:** Executing Phase 02
- **Progress:** [████████░░] 78%

```
[█.......] 1/8 phases complete (Plans 02-01, 02-02, 02-03 of Phase 02 done; 2 plans remain in Phase 02)
```

## Performance Metrics

- Phases completed: 1
- Plans completed: 7 (Phase 1: 4, Phase 2: 3)
- Requirements validated: 23 / 49 (Phase 1: 13, Plan 02-01: API-01/02/07/09, Plan 02-02: CORE-04, API-04, API-07 reaffirmed, Plan 02-03: CORE-04 reaffirmed, CORE-10, API-04 reaffirmed)

| Plan | Duration | Tasks | Files | Notes |
|------|----------|-------|-------|-------|
| 02-01 | ~5m 24s | 3 (1 TDD) | 13 | spu94_state chassis + verify-no-heap CI + API-07 consumer tests |
| 02-02 | ~7m 7s | 3 (2 TDD) | 11 | register identity (35-enum + tables) + q15 error tap + spu94_tick stub + ADR-0004 |
| 02-03 | ~11m 44s | 4 (2 TDD) | 15 | engine register I/O + 35-entry policy table + facade (105 wrappers) + ADR-0005 |

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

### Gray-Area Decisions Pending (to be logged in DECISIONS.md)

- Phase 1: Q15 multiply semantics (`>> 15` direction); vIIR = -0x8000 policy.
- Phase 2: per-register mid-stream write policy (RESOLVED Plan 03 → ADR-0005); mBASE-write side effect (Plan 04, ADR-0006 := snap-on-write resolved by research, ADR text written in Plan 04 Task 2).
- Phase 3: comb-sum intermediate accumulation precision; register-write timing between L-tick and R-tick.
- Phase 4: lv2-psx-reverb witness exclusion on frequency-response axis (documented).
- Phase 7: witness-diff tolerance calibration per preset.

### Open Questions

- Comb-sum intermediate precision — nocash silent; resolve in Phase 3 with witness check.
- mBASE-write buffer behavior — RESOLVED via Phase 2 research as snap-on-write (ADR-0006); Plan 04 lands the implementation through the D-11 seam.
- FIR integer accumulation width — verify 32-bit intermediate suffices for 39-tap Q15 × int16 sum in Phase 4.

### Blockers

None.

### Todos

- Continue Phase 2: execute Plan 02-04 (buffer arithmetic + mBASE snap-on-write + ADR-0006).
- Plan 04: replace the `spu94_mbase_on_write` Plan-03 stub in `src/spu94/spu94_write_policy.c` with the snap-on-write body (`state->buffer_address = (uint32_t)new_mbase;`); may relocate to `src/spu94/spu94_buffer.c` if Plan 04 prefers.
- Plan 04: route `spu94_buffer_advance` through `spu94_tick(state)` (Pitfall 4: keep apply-pending the only existing call site; buffer-advance lands as a separate call after apply).
- Plan 04 Task 2: write ADR-0006 entry in docs/DECISIONS.md (snap-on-write) — prepended above ADR-0005 at the top.
- Plan 05: per-register policy test battery (per ADR-0005 test obligation) and Python ctypes fuzz harness for the wrap formula across 10⁶ steps.

## Session Continuity

### Last Session (2026-04-19)

- Executed Phase 2 Plan 03: engine-layer typed register I/O (6 accessors, signedness-validated) + 35-entry write-policy table (D-05 seam) + pending-shadow flush wired into `spu94_tick` (Pitfall 4 enforced) + 35-register hand-written facade header (105 static-inline wrappers) + ADR-0005.
- Refactored `struct spu94_state` into `src/spu94/spu94_state_internal.h` so 5 internal TUs share one ODR-safe definition; alignof guard stays in `spu94_state.c`.
- Reordered `spu94_result_t` above `<spu94/spu94_registers.h>` in the umbrella header to satisfy the engine setter signatures without a circular include; preserves Plan 02's "sub-headers never include the umbrella" rule.
- Auto-fixed: C-comment-glob conflict from literal `d*/m*` in `/* ... */` comments (3 source files + 2 test files reworded to `d-prefix/m-prefix`); -Werror=missing-prototypes on `spu94_mbase_on_write` (added local forward decl); grep-guard hit on "double" in pending-flush comment (reworded to "twice").
- 5 commits land Plan 03 (Task 1 RED, Task 1+2 GREEN bundled because they share the internal-header refactor and would not link separately, Task 3 RED+GREEN, Task 4 ADR); ctest 7/7 green; grep-guard + verify-no-heap green.

### Next Session

- `/gsd-execute-phase` (continue) — execute Plan 02-04 (buffer arithmetic + mBASE snap-on-write + ADR-0006).

---
*State initialized: 2026-04-18 at roadmap completion*
