---
status: partial
phase: 62-voice-count-selector
source: [62-VERIFICATION.md]
started: 2026-05-31T00:00:00Z
updated: 2026-05-31T00:00:00Z
blocker: No working MIDI / note-trigger path on the Linux standalone (out of v1.12.0 milestone scope)
---

## Current Test

[awaiting a working note-input path on the Linux standalone]

## Tests

### 1. Control visible and populated at startup
expected: Dropdown labeled "Voice Count" is visible in the sampler voice panel, shows 24 as the default on startup, and its menu lists every integer from 1 to 24.
result: passed — confirmed visually 2026-05-31 (user verified: dropdown present in voice panel, reads 24, lists 1–24)

### 2. Count = 1 produces monophonic behavior
expected: With a sample triggering, setting Voice Count to 1 makes the sampler monophonic — each new note takes over the single active voice (last-note priority); no reload or restart.
result: [blocked — no Linux MIDI / trigger path]

### 3. Raising count adds polyphony immediately; controls follow
expected: Raising Voice Count (1 → 8 → 24) increases polyphony audibly and immediately with no reload; per-voice controls (Level/Pan/ADSR/NON/PMON) reach the newly-active voices (Phase 61 fan-out following the new count).
result: [blocked — no Linux MIDI / trigger path]

### 4. Displayed count matches actual under playback
expected: Changing the dropdown while notes are sounding keeps the displayed value equal to the number of voices actually allocated — display and engine never diverge.
result: [blocked — no Linux MIDI / trigger path]

## Summary

total: 4
passed: 1
issues: 0
pending: 0
skipped: 0
blocked: 3

## Gaps

No code gaps. Items 2–4 are blocked on a working note-input path for the Linux standalone (MIDI controller or trigger), which is out of scope for the v1.12.0 "Voice Count" milestone. The code is verified by source inspection + a clean Release build and routes through already-audibly-proven Phase 60 (allocation/ring-out) and Phase 61 (control fan-out) engine code, so behavioral risk is low. When a note source is available on Linux, run `/gsd:verify-work 62` to exercise items 2–4 and close the audible verification.
