---
phase: 41-vca-ramp-controls
plan: 01
status: complete
started: 2026-05-23
completed: 2026-05-23
duration: 20min
---

# Plan 41-01 Summary

## What Was Built

VCA ramp controls added to the sampler GUI:

- **Direction button** — toggles Up (fade in, teal) / Down (fade out, coral)
- **Speed knob** — 0.03s to 7.0s, rotary with skewed midpoint at 0.85s, displays seconds
- **Curve toggle** — Linear / Exponential
- **ARM button** — one-shot trigger (mauve accent), activates sweep on both L+R channels

Speed-to-shift mapping uses nearest-match lookup across SPU shift values 9-17.

## Bug Fixes During Verification

- Pitch knob default corrected from 0x1000 to 0x800 to match 22050 Hz encode rate default
- Processor pitch atomic default synced to match knob (was 0x1000, now 0x800)
- Encode rate change auto-adjusts pitch knob
- Noise generator shift/step set to audible values (shift=10/step=5) when NON enabled
- Voice mixer enabled at startup so noise works without sample loaded
- Pan/Level applied continuously (was only on key-on)
- NON voice trigger works without sample loaded (permissive end_addr)
- Noise Color knob added (0-15, controls LFSR shift rate in real time)
- Noise Gauss toggle added then reverted — experiment showed darkening but nothing a filter couldn't do; keeping PS1-faithful raw noise

## Commits

| Commit | Description |
|--------|-------------|
| 843fb87 | feat(41-01): add VCA ramp controls |
| 2ef9e4a | fix: correct double-speed playback and NON noise ticks |
| c3a328d | fix: match processor pitch default to knob default |
| 4e34b07 | fix: apply Pan/Level continuously |
| 716a296 | feat: add Noise Color knob |
| b2539d3 | fix: enable voice mixer at startup |
| 646182f | feat: add Noise Gauss toggle (reverted) |
| 049b462 | revert: noise gauss toggle |
| f807c17 | fix: rename curve label to Exponential |
| ec904d4 | fix: allow NON voice trigger without sample loaded |

## Self-Check: PASSED
