---
phase: 20-user-slot-persistence
plan: 01
subsystem: core-io
tags: [preset, persistence, back-compat, user-slots]
dependency_graph:
  requires: [spu94_preset_save, spu94_preset_load, spu94_interp_set_user_slot, spu94_interp_user_slot_is_filled]
  provides: []
  affects: [src/spu94/spu94_preset.c, include/spu94/spu94.h]
tech_stack:
  added: []
  patterns: [omit-empty-for-back-compat, scratch-buffer-commit-on-section-transition]
key_files:
  modified:
    - src/spu94/spu94_preset.c
    - include/spu94/spu94.h
    - tests/unit/preset/test_preset.c
decisions:
  - "Empty slots omitted entirely from serialization — back-compat with pre-feature presets is byte-identical"
  - "SECTION_USER_SLOT scratch buffer commits at section-transition or end-of-buffer — leverages existing parser dispatch"
  - "SPU94_PRESET_BUF_SIZE bumped 4096 → 8192 for worst-case (8 filled slots ≈ 5900 bytes)"
commits:
  - hash: 80fd56e
    title: "feat: persist user slots in .spu94 preset format (milestone 4/4)"
metrics:
  completed: "2026-05-10"
  tasks: 4
---

# Phase 20 Plan 01: User Slot Persistence Summary

Filled user slots persist in the `.spu94` preset format as `[user_slot N]` sections. Empty slots are omitted entirely so pre-feature presets stay byte-identical and old files load cleanly.

## What Shipped

### Saver

For each `i` in `0..7` where `spu94_interp_user_slot_is_filled(state, i)`:

```
[user_slot N]
mPMIX_L=0x...
mPMIX_R=0x...
... (35 register key=value pairs)
```

Empty slots produce no output. A preset with no filled slots is byte-identical to its pre-feature serialization.

### Loader

Section dispatch grows a `SECTION_USER_SLOT` case. Register lines within the section accumulate into a 35-entry scratch array; on transition to any other section header (or end of buffer) the scratch is committed via `spu94_interp_set_user_slot`, which marks the slot filled atomically.

Missing `[user_slot N]` sections leave all slots empty — backward compatibility for presets saved before this change.

### Buffer Size

`SPU94_PRESET_BUF_SIZE` bumped 4096 → 8192. Worst case: 8 filled slots × ~560 bytes/slot + registers/mixer/DAC + headers ≈ 5900 bytes.

## Tasks Completed

| Task | Name | Commit |
|------|------|--------|
| 1 | Saver: emit `[user_slot N]` sections for filled slots only | 80fd56e |
| 2 | Loader: `SECTION_USER_SLOT` case + scratch buffer commit | 80fd56e |
| 3 | Buffer size bump 4096 → 8192 | 80fd56e |
| 4 | 2 new test sub-cases (round-trip with 3 filled slots, back-compat with pre-feature file) | 80fd56e |

## Files Touched

- `src/spu94/spu94_preset.c` — saver + loader changes
- `include/spu94/spu94.h` — `SPU94_PRESET_BUF_SIZE` bump
- `tests/unit/preset/test_preset.c` — 2 sub-cases added (7-test suite continues to pass)
