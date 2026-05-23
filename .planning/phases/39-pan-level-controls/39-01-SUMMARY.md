---
phase: 39-pan-level-controls
plan: 01
status: complete
started: 2026-05-23
completed: 2026-05-23
duration: 25min
---

# Plan 39-01 Summary

## What Was Built

Replaced the raw Volume L/R rotary knobs in the sampler window with musician-intuitive controls:

- **Pan rotary encoder** — -100 (hard left) to +100 (hard right), center detent at 0 (double-click returns to center)
- **Level vertical fader** — 0% (silent) to 100% (max), channel-fader style (LinearVertical)
- **INV toggle** — flips polarity on both channels, teal "INV" indicator appears when active

Pan-to-register math uses linear pan law: `gain_l = min(1, 1-pan)`, `gain_r = min(1, 1+pan)`, then `vol = level * gain * 0x3FFF`.

## Layout Changes

- ADSR section moved back up directly below marker knobs (undoes Phase 34's awkward insertion)
- Pan/Level/INV section placed below ADSR with proper spacing
- Sampler window grown from 560px to 710px to accommodate the new layout
- Pan encoder on top, Level fader below, INV toggle to the right

## Deviations

1. Initial layout crammed controls into the same 560px window between markers and ADSR — user rejected this as too cramped. Fixed by moving ADSR up and putting Pan/Level/INV below with window resize.
2. Initial Pan and Level were both rotary and too small (80x54). Increased to 100x70, then switched Level to vertical fader per user request.
3. Added center detent on Pan (double-click return to 0) per user request.

## Commits

| Commit | Description |
|--------|-------------|
| e819e3f | feat(39-01): replace Vol L/R with Pan + Level + INV controls |
| 949a5a2 | feat(39-01): pan encoder + vertical level fader + window resize |

## Self-Check

- [x] Pan at center: vol_l = vol_r = 0x3FFF ✓
- [x] Pan hard left: vol_l = 0x3FFF, vol_r = 0 ✓
- [x] Pan hard right: vol_l = 0, vol_r = 0x3FFF ✓
- [x] Level at 0%: silence ✓
- [x] INV toggle negates both channels ✓
- [x] Teal INV indicator visible when toggled ✓
- [x] ADSR section unaffected ✓
- [x] Waveform display unaffected ✓
- [x] Build succeeds ✓
- [x] User visual approval ✓

## Self-Check: PASSED

## Key Files

### Created
(none — modifications only)

### Modified
- `src/plugin/PluginEditor.h` — replaced Vol L/R members with Pan/Level/INV members
- `src/plugin/PluginEditor.cpp` — new control setup, pan-to-register math, updated layout
- `src/plugin/SamplerWindow.h` — window resized to 710px
