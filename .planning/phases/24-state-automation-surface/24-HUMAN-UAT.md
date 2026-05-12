---
status: partial
phase: 24-state-automation-surface
source: [24-VERIFICATION.md]
started: 2026-05-12T15:40:00Z
updated: 2026-05-12T15:40:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Single-instance state round-trip (PLUG-26)
expected: Save DAW project with SPU-94 on a track. Close DAW. Reopen project. All parameter values (input gain, mixer levels, sends, morph position/speed/grit) are restored identically.
result: [pending]

### 2. Multi-instance independence (PLUG-27)
expected: Two SPU-94 instances on separate tracks, each with different settings. Save project. Close DAW. Reopen. Each instance retains its own independent settings — they don't bleed into each other.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
