---
status: partial
phase: 61-coherent-controls
source: [61-VERIFICATION.md]
started: 2026-05-31T03:30:00Z
updated: 2026-05-31T03:30:00Z
---

## Current Test

[awaiting human testing]

## Tests

### 1. Voice-0/Trigger audition A/B at Level=100%
expected: Load a sample, set Level=100%, press the on-screen Trigger; the audition sounds indistinguishable from a v1.11.0 build doing the same. Bit-identity is already auto-verified (base_vol = 0x3FFE via voice_controls_default24_regression) — this is the ear confirmation that no new artifact slipped in alongside the fan-out change.
result: [pending]

### 2. PMON-chain character audible across the active set
expected: Set voice count >= 3, enable PMON, play a chord of >= 3 notes; confirm the chained pitch-mod character (voice N bent by N-1) is audible across all active voices, not just voice 0. Flag-set is auto-verified (voice_controls_non_pmon_all_active asserts pmon_flags bits); this confirms the audible chaining.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
