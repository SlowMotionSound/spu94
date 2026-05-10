---
phase: 18-user-slots-core
plan: 01
subsystem: core-dsp
tags: [interpolation, user-slots, waypoints, morph]
dependency_graph:
  requires: [spu94_interp_set_morph, spu94_presets, spu94_set_reg_i16, spu94_set_reg_u16]
  provides:
    - spu94_interp_set_user_slot
    - spu94_interp_clear_user_slot
    - spu94_interp_get_user_slot
    - spu94_interp_user_slot_is_filled
    - SPU94_INTERP_USER_SLOT_COUNT
  affects: [include/spu94/spu94.h, src/spu94/spu94_interp.c]
tech_stack:
  added: []
  patterns: [transparent-empty-slot, midpoint-segment-break, per-engine-mirror]
key_files:
  created:
    - tests/unit/interp/test_user_slots.c
  modified:
    - src/spu94/spu94_interp.c
    - include/spu94/spu94.h
    - tests/unit/interp/CMakeLists.txt
decisions:
  - "8 slots at midpoints (1/16, 3/16, ..., 15/16) — preserves the 9 Sony anchors as the visible coarse grid and adds fine detail between them"
  - "Empty = transparent: fresh project remains bit-identical to v1.5 — INTERP-04 guarantee unbroken"
  - "User slot state lives on spu94_state (per-engine). Multi-engine consumers MUST mirror writes — root cause of the 19-02 audible-glide bug"
  - "Pending-shadow flush before snapshot: m/d-prefix (TICK_LATCHED) edits stage into pending_values[] until spu94_tick fires; saveUserSlot flushes via spu94_apply_pending_writes(engines[0]) before snapshot or risks missing recent edits"
commits:
  - hash: ba22323
    title: "feat: user-programmable waypoint slots (C core, milestone 1/4)"
    summary: "API + 17-position interp + 7 unit tests"
  - hash: fdd1f71
    title: "fix: mirror user_slots to scratch engine + flush pending on SAVE"
    summary: "Follow-up after Phase 19 integration surfaced per-engine state semantics"
metrics:
  completed: "2026-05-10"
  tasks: 4
  files_created: 1
  files_modified: 4
  tests_added: 7
---

# Phase 18 Plan 01: User Slots — Core Summary

8 programmable waypoint slots between Sony's 9 anchors. Empty slots are transparent (audio bit-identical to v1.5). Filled slots act as new interpolation breakpoints. The morph dial now resolves a 17-anchor continuum instead of 9.

## What Shipped

| Surface | Symbol | Behaviour |
|---------|--------|-----------|
| Public API | `spu94_interp_set_user_slot(state, idx, regs)` | Copy 35-register array into slot idx (0..7), mark filled |
| Public API | `spu94_interp_clear_user_slot(state, idx)` | Mark slot empty; storage zeroed |
| Public API | `spu94_interp_get_user_slot(state, idx, regs_out)` | Read slot contents (regardless of filled state) |
| Public API | `spu94_interp_user_slot_is_filled(state, idx)` | Filled query |
| Engine | `spu94_interp_set_morph(state, pos)` | 17-anchor lookup; filled slot at slot centre = direct-write (INTERP-04 carve-out) |
| State | `int16_t user_slots[8][35]` + `bool user_slot_filled[8]` | Per-`spu94_state` storage |

## Tasks Completed

| Task | Name | Commit | Tests |
|------|------|--------|-------|
| 1 | User slot data + public API | ba22323 | API guards, round-trip |
| 2 | 17-position interpolation in set_morph | ba22323 | empty-slot transparency at Sony + midpoint, filled-slot bit-identity, segment break |
| 3 | Test suite — 7 sub-cases in `test_user_slots.c` | ba22323 | 7/7 pass |
| 4 | Engine state mirroring + pending-writes flush (follow-up) | fdd1f71 | (validated in 19-02 by real-world EDIT/SAVE flow) |

12/12 user_slot sub-tests green (7 initial + 3 from per-slot file I/O wired in 19-02 + 2 from preset persistence in 20-01).

## Anti-Patterns Captured

1. **Per-engine state without explicit mirroring** (blocking). `user_slots[][]` live on the `spu94_state` struct, so engines[0] and engines[1] each have their own copy. Any operation that mutates `user_slots` on engines[0] (saveUserSlot, clearUserSlot, per-slot LOAD, full preset LOAD) MUST mirror the change to engines[1] — otherwise the glide path's `spu94_interp_set_morph(engines[1], pos)` sees empty slots and produces transparent Sony interp instead of the user's saved values. Audible-but-silent-on-the-snap-path bug; hard to diagnose without knowing the engine architecture.

2. **TICK_LATCHED snapshot races** (advisory). `spu94_snapshot_registers` reads only active `reg_values[]`. m/d-prefix register edits are TICK_LATCHED — staged into `pending_values[]` until `spu94_tick` commits them. Snapshot taken before tick fires misses recent edits. saveUserSlot flushes pending before snapshot to address this.

## Files Touched

- `src/spu94/spu94_interp.c` — 17-anchor lookup, set/clear/get/is_filled implementations, INTERP-04 bypass extension
- `include/spu94/spu94.h` — API declarations + `SPU94_INTERP_USER_SLOT_COUNT` constant
- `tests/unit/interp/test_user_slots.c` — new file, 7 sub-cases (+ 3 added in 19-02 for per-slot file I/O, + 2 in 20-01 for preset persistence = 12 total)
- `tests/unit/interp/CMakeLists.txt` — new test TU
