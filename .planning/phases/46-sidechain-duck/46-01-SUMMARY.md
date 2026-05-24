---
phase: 46-sidechain-duck
plan: 01
subsystem: voice-engine
tags: [sidechain, duck, sweep, kon-detection, state-machine]
dependency_graph:
  requires: [sweep-engine, mixer-tick, kon-pending]
  provides: [duck-source-atomics, duck-state-machine, duck-recovery]
  affects: [processBlock, voice-sweep-ownership]
tech_stack:
  added: []
  patterns: [block-level-kon-snapshot, depth-floor-clamp, sweep-reuse-for-duck]
key_files:
  created: []
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_sweep.c
decisions:
  - "Block-level KON detection (not per-sample) -- processBlock reads pending_kon before spu94_process, sufficient for musical duck timing"
  - "One-shot sweep stays active=1 at boundary -- recovery completion checks level >= original, not active==0"
  - "Duck attack hardcoded shift=10 (~0.05s exponential slam) -- fast attack is inherent to sidechain compression"
metrics:
  duration: "~8 minutes"
  completed: "2026-05-24T22:18:11Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 46 Plan 01: Sidechain Duck DSP Summary

Event-triggered per-voice VCA duck using existing one-shot sweep engine, with exponential attack and configurable recovery speed.

## What Was Built

### Per-Voice Duck Infrastructure (PluginProcessor.h)
- `duckSource[24]` atomic array: which voice's KON triggers duck (-1 = none)
- `duckRelease[24]` atomic array: recovery speed in seconds (0.03-6.8)
- `duckDepth[24]` atomic array: how far volume drops (0.0-1.0)
- `DuckPhase` enum: IDLE / DECREASING / RECOVERING state machine
- `duckState[24]` + `duckOrigLevel_l/r[24]`: audio-thread-only state
- Public getters: `getDuckSource(voice)`, `getDuckRelease(voice)`, `getDuckDepth(voice)`

### Duck State Machine (PluginProcessor.cpp)
- **KON snapshot**: reads `mx->pending_kon` after GUI/MIDI triggers, before `spu94_process` clears it
- **Trigger**: when source voice's KON detected, stores original volume, configures exponential decrease (shift=10, one-shot)
- **Depth floor**: each block checks if decrease reached `(1-depth) * original`; clamps there and transitions to recovery
- **Recovery**: configures exponential increase from floor to original, speed via `speedToShift(duckRelease[v])`
- **Completion**: when recovery level >= original, deactivates sweep, restores exact original volume, returns to IDLE

### Threat Mitigations
- T-46-01: duckSource clamped to -1..23 (out-of-range treated as no-duck)
- T-46-02: self-duck blocked (voice cannot duck itself)
- Mutual exclusion: duck disabled on voice 0 when tremolo/auto-pan active

### Integration Test (test_sweep.c)
- `test_sidechain_duck_trigger_and_recovery`: proves full decrease->recovery cycle at sweep level
- Exponential decrease from 0x7FFF reaches <0x0100 in 12000 ticks (shift=10)
- Exponential increase from near-zero reaches >0x7F00 in 36000 ticks (shift=13)
- Test count: 29 (was 28)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Recovery completion check used wrong condition**
- **Found during:** Task 1 verification
- **Issue:** One-shot sweeps (retrigger_enable=0) never set `active=0` -- they stay active but clamped at the boundary. Checking `active==0` for recovery completion would never fire.
- **Fix:** Changed recovery completion to check `sweep_l.level >= duckOrigLevel_l[v]` (level reached target) instead of `active==0`. Also deactivates sweep explicitly on completion.
- **Files modified:** src/plugin/PluginProcessor.cpp
- **Commit:** f89089a

**2. [Rule 1 - Bug] Test tick counts underestimated for exponential decay**
- **Found during:** Task 2
- **Issue:** Exponential decrease at shift=10 decays by factor (1 - 16/32768) per tick, requiring ~10000 ticks to reach <256 from 0x7FFF (not 3000 as estimated). Recovery at shift=13 needs ~33000 ticks.
- **Fix:** Updated test to use 12000 ticks for decrease, 36000 for recovery.
- **Files modified:** tests/unit/voice/test_sweep.c
- **Commit:** 359ecb9

## Commits

| Hash | Type | Description |
|------|------|-------------|
| 3fe92a9 | feat | Sidechain duck DSP -- KON detection, state machine, depth floor |
| 359ecb9 | test | Integration test proving duck trigger -> decrease -> recovery |
| f89089a | fix | Recovery completion checks level, not active flag |

## Self-Check: PASSED
