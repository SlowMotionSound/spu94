---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
last_updated: "2026-04-19T20:02:37.088Z"
progress:
  total_phases: 8
  completed_phases: 1
  total_plans: 9
  completed_plans: 6
  percent: 67
---

# STATE: SPU-94

**Last updated:** 2026-04-19

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 02 — buffer-register-infrastructure

## Current Position

Phase: 02 (buffer-register-infrastructure) — EXECUTING
Plan: 3 of 5

- **Milestone:** 1 (v1.0)
- **Phase:** 2
- **Plan:** 02-02 complete; ready for 02-03
- **Status:** Executing Phase 02
- **Progress:** [███████░░░] 67%

```
[█.......] 1/8 phases complete (Plans 02-01, 02-02 of Phase 02 done; 3 plans remain in Phase 02)
```

## Performance Metrics

- Phases completed: 1
- Plans completed: 6 (Phase 1: 4, Phase 2: 2)
- Requirements validated: 20 / 49 (Phase 1: 13, Plan 02-01: API-01/02/07/09, Plan 02-02: CORE-04, API-04, API-07 reaffirmed)

| Plan | Duration | Tasks | Files | Notes |
|------|----------|-------|-------|-------|
| 02-01 | ~5m 24s | 3 (1 TDD) | 13 | spu94_state chassis + verify-no-heap CI + API-07 consumer tests |
| 02-02 | ~7m 7s | 3 (2 TDD) | 11 | register identity (35-enum + tables) + q15 error tap + spu94_tick stub + ADR-0004 |

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

### Gray-Area Decisions Pending (to be logged in DECISIONS.md)

- Phase 1: Q15 multiply semantics (`>> 15` direction); vIIR = -0x8000 policy.
- Phase 2: per-register mid-stream write policy (Plan 03); mBASE-write side effect (Plan 04, ADR-0006 := snap-on-write resolved by research, ADR text written in Plan 04 Task 2).
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

- Continue Phase 2: execute Plan 02-03 (per-register read/write API + write-timing policy + ADR-0005).
- Plan 03: wire `spu94_snapshot_registers` body to read `state->reg_values[]` (Plan 01 reserved the storage; Plan 02 declared the function with a zero-fill stub).
- Plan 03: route `apply_pending_writes` through `spu94_tick(state)` at tick-start.
- Plan 04: route `spu94_buffer_advance` through `spu94_tick(state)`.
- Plan 04 Task 2: write ADR-0006 entry in docs/DECISIONS.md (snap-on-write).

## Session Continuity

### Last Session (2026-04-19)

- Executed Phase 2 Plan 02: register identity surface (35-entry enum + parallel hw_offset/name tables + atomic snapshot stub) + q15 error-observation tap + spu94_tick public stub + ADR-0004.
- Refactored `q15_mul_truncate` to a thin wrapper around the new `_with_err`; Phase 1 reference table still bit-identical green.
- Auto-fixed circular include (spu94.h <-> spu94_registers.h) by forward-declaring spu94_state in the registers header; auto-fixed C99-pedantic duplicate-typedef issue by removing the duplicate from spu94.h.
- Removed `src/spu94/spu94_placeholder.c` (Phase 1 scaffold superseded by 3 real TUs).
- 5 commits land Plan 02 atomically (Task 1 RED+GREEN, Task 2 RED+GREEN, Task 3 ADR); all CI gates green; ctest 5/5.

### Next Session

- `/gsd-execute-phase` (continue) — execute Plan 02-03 (per-register read/write API + write-timing policy table + ADR-0005).

---
*State initialized: 2026-04-18 at roadmap completion*
