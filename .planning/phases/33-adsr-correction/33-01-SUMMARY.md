---
phase: 33-adsr-correction
plan: 01
subsystem: voice-engine/adsr
tags: [bugfix, adsr, hardware-fidelity, tdd]
dependency_graph:
  requires: []
  provides: [corrected-decrease-formula]
  affects: [phase-37-volume-sweep]
tech_stack:
  added: []
  patterns: [tdd-red-green]
key_files:
  created: []
  modified:
    - src/spu94/spu94_adsr.c
    - tests/unit/voice/test_adsr.c
    - docs/DECISIONS.md
decisions:
  - "ADR-0056: Decrease formulas use base 8 per nocash spec; increase formulas use base 7"
metrics:
  duration: 16m
  completed: "2026-05-22T16:46:00Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 33 Plan 01: ADSR Step Formula Correction Summary

Corrected sustain-decrease and release step formulas from base 7 to base 8, matching the nocash psx-spx specification and eliminating a 12.5-14% timing error versus real PS1 hardware.

## What Was Done

### Task 1: Fix sustain-decrease and release step formulas (TDD)

**RED phase** (commit 25de372): Added two failing test functions to `test_adsr.c`:
- `test_sustain_decrease_step_magnitudes` -- iterates step values 0..3 at shift=0, asserts each produces the spec-correct delta: -16384, -14336, -12288, -10240 (which are -8, -7, -6, -5 shifted left by 11)
- `test_release_step_base_is_8` -- asserts release at shift=0 linear mode produces delta -16384 (base 8, not base 7)

Both tests failed against the pre-fix code, confirming the off-by-one bug. All 10 existing tests continued to pass.

**GREEN phase** (commit 7ed7bb9): Two-line fix in `spu94_adsr.c`:
- ADSR_SUSTAIN decrease branch: `(7 - a->sustain_step)` changed to `(8 - a->sustain_step)`
- ADSR_RELEASE branch: `(int32_t)7` changed to `(int32_t)8`
- Updated header comment block to document the increase=base7 vs decrease=base8 distinction

All 12 ADSR tests pass. All 35 voice_tick tests pass. ctest voice targets green.

### Task 2: Write ADR-0056

Commit 3c6b4fd: Prepended ADR-0056 ("ADSR sustain-decrease and release off-by-one correction") to `docs/DECISIONS.md` before ADR-0055. Documents the base 7 vs base 8 distinction, the 12.5% / 14% timing impact, the nocash spec reference, and the Phase 37 propagation prevention rationale.

## Deviations from Plan

None -- plan executed exactly as written.

## Verification Results

- 12/12 ADSR unit tests pass (10 existing + 2 new regression)
- 35/35 voice_tick tests pass
- ctest `adsr_unit` and `voice_tick_unit` targets both green
- grep confirms `(8 - a->sustain_step)` in ADSR_SUSTAIN decrease branch
- grep confirms `(int32_t)8` in ADSR_RELEASE branch
- ADR-0056 prepended before ADR-0055 with correct format

## TDD Gate Compliance

- RED gate: commit 25de372 (`test(33-01):`) -- 2 new tests fail, 10 existing pass
- GREEN gate: commit 7ed7bb9 (`feat(33-01):`) -- all 12 tests pass
- REFACTOR gate: not needed (minimal two-line change, no cleanup required)

## Commits

| Task | Type | Hash | Description |
|------|------|------|-------------|
| 1 (RED) | test | 25de372 | Add failing tests for sustain-decrease and release step magnitudes |
| 1 (GREEN) | feat | 7ed7bb9 | Fix ADSR sustain-decrease and release step formulas to match PS1 spec |
| 2 | docs | 3c6b4fd | Add ADR-0056 documenting ADSR off-by-one correction |
