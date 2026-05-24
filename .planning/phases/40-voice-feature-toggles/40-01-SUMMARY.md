---
phase: 40-voice-feature-toggles
plan: 01
status: complete
started: 2026-05-23
completed: 2026-05-23
duration: 12min
---

# Plan 40-01 Summary

## What Was Built

Added NON (noise) and PMON (pitch modulation) toggle buttons to the sampler GUI window:

- **NON toggle** — enables/disables noise generator output for voice 0. When on, voice outputs global LFSR noise instead of ADPCM sample. ADPCM still decodes for flag side effects.
- **PMON toggle** — enables/disables pitch modulation from previous voice. Voice 0 PMON has no effect (no predecessor).
- **Processor wiring** — two `std::atomic<bool>` members (`guiVoiceNon`, `guiVoicePmon`) added to PluginProcessor, read in processBlock and applied via `spu94_voice_mixer_set_non` / `spu94_voice_mixer_set_pmon`.

## Layout

NON and PMON toggles positioned below the INV toggle in the sampler window voice controls section.

## Commits

| Commit | Description |
|--------|-------------|
| 6816ed9 | feat(40-01): add NON/PMON toggle buttons with audio-thread wiring |

## Self-Check

- [x] NON toggle visible in sampler window ✓
- [x] PMON toggle visible in sampler window ✓
- [x] Processor atomics wired to audio thread ✓
- [x] Build succeeds ✓
- [x] Human verification: deferred to batch check (user away)

## Self-Check: PASSED
