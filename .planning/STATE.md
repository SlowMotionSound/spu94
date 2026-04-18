# STATE: SPU-94

**Last updated:** 2026-04-18

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Milestone 1 — reverb network + hard clip. C library + Python bindings + CLI. Linux primary, MCU cross-compile smoke test.

## Current Position

- **Milestone:** 1 (v1.0)
- **Phase:** — (not yet started)
- **Plan:** —
- **Status:** Roadmap complete; ready to plan Phase 1
- **Progress:** Phase 0/8

```
[........] 0/8 phases complete
```

## Performance Metrics

- Phases completed: 0
- Plans completed: 0
- Requirements validated: 0 / 49

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

### Gray-Area Decisions Pending (to be logged in DECISIONS.md)

- Phase 1: Q15 multiply semantics (`>> 15` direction); vIIR = -0x8000 policy.
- Phase 2: per-register mid-stream write policy; mBASE-write side effect on work buffer.
- Phase 3: comb-sum intermediate accumulation precision; register-write timing between L-tick and R-tick.
- Phase 4: lv2-psx-reverb witness exclusion on frequency-response axis (documented).
- Phase 7: witness-diff tolerance calibration per preset.

### Open Questions

- Comb-sum intermediate precision — nocash silent; resolve in Phase 3 with witness check.
- mBASE-write buffer behavior — ipatix zeroes, hardware may not; resolve in Phase 2.
- FIR integer accumulation width — verify 32-bit intermediate suffices for 39-tap Q15 × int16 sum in Phase 4.

### Blockers

None.

### Todos

- Start Phase 1: `/gsd-plan-phase 1`.

## Session Continuity

### Last Session (2026-04-18)

- Initialized PROJECT.md; ran research (ARCHITECTURE, FEATURES, PITFALLS, STACK, SUMMARY).
- Incorporated research corrections into PROJECT.md (register count 24→33, 39-tap FIR, nocash paraphrase discipline, lv2-psx-reverb frequency-response exclusion).
- Defined REQUIREMENTS.md with 49 v1 requirements across 7 categories (CORE, API, PYBIND, CLI, TEST, BUILD, DOCS).
- Created ROADMAP.md: 8 phases, 100% requirement coverage, success criteria derived goal-backward.
- Updated REQUIREMENTS.md traceability table with phase assignments.

### Next Session

- `/gsd-plan-phase 1` — plan Phase 1 (Foundation: Fixed-Point Math + Build Infrastructure).

---
*State initialized: 2026-04-18 at roadmap completion*
