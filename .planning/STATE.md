---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: executing
last_updated: "2026-04-19T19:49:51.360Z"
progress:
  total_phases: 8
  completed_phases: 1
  total_plans: 9
  completed_plans: 5
  percent: 56
---

# STATE: SPU-94

**Last updated:** 2026-04-19

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 02 — buffer-register-infrastructure

## Current Position

Phase: 02 (buffer-register-infrastructure) — EXECUTING
Plan: 2 of 5

- **Milestone:** 1 (v1.0)
- **Phase:** 2
- **Plan:** 02-01 complete; ready for 02-02
- **Status:** Executing Phase 02
- **Progress:** [██████░░░░] 56%

```
[█.......] 1/8 phases complete (Plan 02-01 of Phase 02 done; 4 plans remain in Phase 02)
```

## Performance Metrics

- Phases completed: 1
- Plans completed: 5 (Phase 1: 4, Phase 2: 1)
- Requirements validated: 17 / 49 (Phase 1: 13, Plan 02-01: API-01, API-02, API-07, API-09)

| Plan | Duration | Tasks | Files | Notes |
|------|----------|-------|-------|-------|
| 02-01 | ~5m 24s | 3 (1 TDD) | 13 | spu94_state chassis + verify-no-heap CI + API-07 consumer tests |

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

- Continue Phase 2: execute Plan 02-02 (register identity table).
- Plan 04 Task 2: write ADR-0006 entry in docs/DECISIONS.md (snap-on-write).

## Session Continuity

### Last Session (2026-04-19)

- Executed Phase 2 Plan 01: spu94_state chassis (opaque handle, lifecycle API, result enum, size/align bounds with `_Static_assert`).
- Added linker-symbol heap-free proof script + dedicated `verify-no-heap` CI job (Phase 2 SC 1).
- Added C99 + C++ extern-C consumer compile tests (Phase 2 SC 6 / API-07).
- Auto-fixed C++ static_assert keyword mismatch in spu94_q15.h (API-07 surface forced by Plan 01).
- 4 commits land Plan 01 atomically (RED + GREEN + Task 2 + Task 3); all CI gates green; ctest 4/4.

### Next Session

- `/gsd-execute-phase` (continue) — execute Plan 02-02 (register identity table).

---
*State initialized: 2026-04-18 at roadmap completion*
