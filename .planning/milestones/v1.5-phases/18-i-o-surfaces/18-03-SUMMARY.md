---
phase: 18-i-o-surfaces
plan: 03
status: complete
started: "2026-05-03"
completed: "2026-05-03"
---

## Summary

Enabled MIDI clock EXT mode: the standalone app receives MIDI clock messages (0xF8, 24 PPQN) and derives BPM via a 24-sample moving average with jitter smoothing. BPM is stored to the tempoBpm atomic, feeding the existing tempo sync pipeline from Plan 02.

## Key Changes

- `src/standalone/CMakeLists.txt`: `NEEDS_MIDI_INPUT TRUE` — JUCE standalone wrapper routes MIDI input to processBlock
- `src/standalone/PluginProcessor.h`: MIDI clock state variables (clockIntervals, clockIdx, clockCount, lastClockTime)
- `src/standalone/PluginProcessor.cpp`: processBlock iterates midiMessages in EXT mode, measures clock intervals, computes BPM via `60.0 / (avgInterval * 24.0)` with sanity guards (reject < 1ms, > 1s intervals; clamp BPM 1-999). Clock state resets on mode exit and factory preset load.

## Commits

- `9d03bb8` feat(18-03): enable MIDI clock EXT mode BPM derivation

## Self-Check: PASSED

- [x] CMakeLists.txt has NEEDS_MIDI_INPUT TRUE
- [x] processBlock signature uses midiMessages (not commented out)
- [x] isMidiClock() check in processBlock
- [x] Moving average window size = 24
- [x] Interval sanity guards (< 1ms, > 1s)
- [x] BPM clamp 1-999
- [x] Clock state reset on mode exit
- [x] Build succeeds with zero errors
- [x] EXT mode selectable without crash (structural verification)

## Verification Notes

Verified structurally (no MIDI hardware available). EXT mode selectable, BPM field read-only, Audio/MIDI Settings dialog accessible. Full MIDI clock verification deferred to Phase 19 or when hardware is available.
