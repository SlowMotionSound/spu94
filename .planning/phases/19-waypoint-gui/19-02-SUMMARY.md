---
phase: 19-waypoint-gui
plan: 02
subsystem: juce-gui
tags: [morph, edit-flow, save-revert, export-load, state-mirroring]
dependency_graph:
  requires: [spu94_interp_set_user_slot, spu94_interp_clear_user_slot, spu94_apply_pending_writes]
  provides:
    - spu94_export_user_slot
    - spu94_load_user_slot
    - SPU94AudioProcessor::saveUserSlot
    - SPU94AudioProcessor::clearUserSlot
    - SPU94AudioProcessor::exportUserSlot
    - SPU94AudioProcessor::loadUserSlot
  affects: [juce-plugin/Source, src/spu94/spu94_interp.c, include/spu94/spu94.h]
tech_stack:
  added: []
  patterns: [edit-flow, hidden-by-default-band, per-tick-action-stack, state-before-audio-gate]
key_files:
  modified:
    - juce-plugin/Source/MorphPanel.cpp
    - juce-plugin/Source/MorphPanel.h
    - juce-plugin/Source/PluginEditor.cpp
    - juce-plugin/Source/PluginEditor.h
    - juce-plugin/Source/PluginProcessor.cpp
    - juce-plugin/Source/PluginProcessor.h
    - src/spu94/spu94_interp.c
    - include/spu94/spu94.h
    - tests/unit/interp/test_user_slots.c
decisions:
  - "Three per-tick action buttons (EDIT/EXPORT/LOAD) stacked top-right of MorphPanel — replaces the Sony-preset dropdown after a long design loop"
  - "REVERT = clear slot entirely (not discard edits) — user-requested emergency clear"
  - "LOAD ignores the file's slot index — stamp onto currently-parked tick — enables drop-anywhere slot-file sharing"
  - "State management hoisted above the audio-I/O gate — sliders/ticks reflect engine state regardless of WAV/playback"
  - "Forced re-applies (LOAD, SAVE, REVERT) snap regardless of Morph Speed knob"
  - "Glide-path morph re-apply syncs shadows from engines[1] (target), not engines[0] (mid-slew)"
  - "Custom GritButtonLookAndFeel keeps Int/Fract toggle in high-saturation 'focused' coral regardless of focus state"
  - "Default Morph Speed lowered 1.0 → 0.5 — polished launch default sits between snap and full glide"
  - "saveUserSlot mirrors user_slots write to engines[1]; spu94_apply_pending_writes flushes TICK_LATCHED edits before snapshot"
commits:
  - hash: 4c4737c
    title: "feat: EDIT button + SAVE/REVERT slot edit flow (milestone 3/4)"
  - hash: 8c433ce
    title: "fix: SAVE/REVERT visibility + button placement + tick position"
  - hash: b648e43
    title: "feat: per-tick slot ops + always-on engine state mirroring"
  - hash: fdd1f71
    title: "fix: mirror user_slots to scratch engine + flush pending on SAVE"
metrics:
  completed: "2026-05-10"
  tasks: 8
---

# Phase 19 Plan 02: Waypoint GUI — Edit Flow + Per-Tick Actions Summary

Interactive waypoint authoring. EDIT enters Advanced view focused on the parked slot; SAVE captures; REVERT clears; EXPORT writes a single-slot file; LOAD stamps any single-slot file onto the parked tick. Engine state mirroring overhauled so sliders/ticks reflect engine state at all times.

## What Shipped

### Per-Tick Action Stack (b648e43)

Three buttons stacked top-right of MorphPanel:

| Button | Enabled When | Action |
|--------|-------------|--------|
| EDIT | Knob parked on user-slot tick | Enter Advanced focused on this slot |
| EXPORT | Knob parked on user-slot tick AND slot filled | Write `.spu94` single-slot file (one `[user_slot N]` section + `type=user_slot` marker) |
| LOAD | Knob parked on user-slot tick | Read any single-slot file; stamp onto current tick (file's slot index ignored) |

Replaces the Sony-preset dropdown that previously occupied that space. The dropdown approach was the leading design for several rounds before being rejected — ticks are visual-only, dedicated buttons invite the curiosity-driven 'I wonder what these do?' moment.

### SAVE / REVERT Band (4c4737c, 8c433ce)

Centered in the band the old floating Advanced/Macro toggle previously occupied, anchored at the TOP of the Advanced viewport. Hidden via `addChildComponent` (not `addAndMakeVisible`) so they don't leak through other views.

- **SAVE** (PS1 teal) — `saveUserSlot()` flushes pending, snapshots engines[0] registers into `user_slots[]`, mirrors to engines[1], requests morph re-apply (forced snap)
- **REVERT** (PS1 coral) — `clearUserSlot()` marks slot empty, mirrors to engines[1], re-applies (slot stays empty, transparent v1.5 interp restores)

### Engine State Mirroring Overhaul (b648e43)

The Advanced sliders ALWAYS reflect what the morph engine would compute at the current knob position — regardless of WAV state, slew progress, or prior edit history.

| Fix | What |
|-----|------|
| Hoist state mgmt above audio-I/O gate | Preset drains, morph re-apply, mixer/DAC pushes, shadow syncs run every block, even with no WAV loaded |
| Forced re-applies snap regardless of Morph Speed | Action buttons need instantaneous feedback, not glide |
| Shadow sync reads engines[1] not engines[0] | Sliders show destination, not mid-slew state |
| Snap engines[0] to morph target on Advanced entry | Guarantees sliders show right values AND engines[0] is correct SAVE-snapshot baseline |
| `shadowSyncCompletedCount` atomic | Editor timer detects sync, refreshes register sliders + tick colors |

### Final Bug Pass (fdd1f71)

Three audible bugs, all rooted in stale or per-engine state:

1. **After SAVE, glide away and back went to generic slot, not saved waypoint.** Root cause: `user_slots` are per-engine state; `saveUserSlot` only wrote to engines[0], but the glide path uses engines[1] (scratch engine) for `spu94_interp_set_morph` — which reads its OWN slots. engines[1]'s slots stayed empty forever. **Fix:** mirror `saveUserSlot`, `clearUserSlot`, per-slot LOAD, full preset LOAD to engines[1].

2. **SAVE could miss freshly-edited m/d-prefix registers.** Root cause: TICK_LATCHED edits stage into `pending_values[]` until `spu94_tick` fires; a fast SAVE click could snapshot before tick. **Fix:** `spu94_apply_pending_writes(engines[0])` before snapshot.

3. **needShadowSync handler rewrote engines[0] = morph target on every Advanced entry → audible blip.** With #1 fixed engines[0] reaches the right state via slew. **Fix:** removed the rewrite; sliders sync from engines[0] which now reliably matches.

### Polish

- SAVE/REVERT use `addChildComponent` (start hidden, no leak-through bug)
- SAVE/REVERT anchored top of Advanced viewport
- User-slot ticks tucked between knob outer edge and Sony dot ring
- Custom `GritButtonLookAndFeel` keeps Int/Fract toggle in high-saturation focused-coral regardless of keyboard focus
- `SPU94_PRESET_BUF_SIZE` bumped to 8192 (worst case 8 filled slots ≈ 5900 bytes)
- Default Morph Speed lowered 1.0 → 0.5 (mid-glide launch default)
- Order fix in `exitAdvancedView`: `requestMorphReapply` BEFORE `morphActive=true` (release semantics) so the audio thread cannot see `morphActive=true` while `morphReapplyPending=false` and skip the re-apply

## Tasks Completed

| Task | Name | Commit |
|------|------|--------|
| 1 | EDIT button + SAVE/REVERT band | 4c4737c |
| 2 | `currentEditingSlot` + `saveUserSlot` + `requestMorphReapply` | 4c4737c |
| 3 | REVERT = clear slot entirely | 4c4737c |
| 4 | Layout polish (hidden-by-default, top anchor, tick placement) | 8c433ce |
| 5 | Per-tick EDIT/EXPORT/LOAD button stack | b648e43 |
| 6 | C-core `spu94_export_user_slot` / `spu94_load_user_slot` + 3 unit tests | b648e43 |
| 7 | Engine state mirroring overhaul | b648e43 |
| 8 | Final fixup (mirror to engines[1], flush pending, blip fix, speed default) | fdd1f71 |

## Anti-Patterns Captured

1. **Audio I/O gate above state management** (advisory). processBlock's loaded/playing early-return originally skipped morph re-apply, shadow sync, and preset drains when no WAV was loaded. State mgmt MUST run unconditionally so the GUI mirrors engine state at all times. Only the actual sample-read + `spu94_process` + output-mix loop is gated on loaded/playing.

2. **Synchronous spu94_apply_pending_writes from GUI thread mid-frame** (advisory). `spu94_apply_pending_writes` is documented to be called from EXACTLY ONE location (the first line of `spu94_tick`). Calling it from the GUI thread or from a different audio-thread point can cause audible blips when engines[0] differs from the just-written morph target. Prefer requesting a re-apply via `morphReapplyPending` and letting the audio thread handle it through its normal slew/snap path. SAVE flush to apply_pending_writes is the one acknowledged exception (without it, snapshot misses freshly-edited m/d-prefix values).

## Files Touched

- `juce-plugin/Source/MorphPanel.{h,cpp}` — per-tick action stack, tick layout
- `juce-plugin/Source/PluginEditor.{h,cpp}` — SAVE/REVERT band, layout
- `juce-plugin/Source/PluginProcessor.{h,cpp}` — `currentEditingSlot`, `saveUserSlot`, `clearUserSlot`, `exportUserSlot`, `loadUserSlot`, mirroring to engines[1], state mgmt above audio-I/O gate, GritButtonLookAndFeel
- `src/spu94/spu94_interp.c` + `include/spu94/spu94.h` — `spu94_export_user_slot`, `spu94_load_user_slot`
- `tests/unit/interp/test_user_slots.c` — 3 added sub-cases (round-trip, empty-slot error, preserves-other-slots)
