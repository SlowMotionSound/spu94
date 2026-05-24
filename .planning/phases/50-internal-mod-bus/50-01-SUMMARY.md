---
phase: 50-internal-mod-bus
plan: 01
subsystem: voice-engine
tags: [mod-bus, noise-modulation, per-voice, c-core, host-wiring]
dependency_graph:
  requires: [Phase 36 NON noise generator, Phase 37 sweep, Phase 27 voice tick]
  provides: [spu94_voice_mixer_set_mod_bus API, per-voice noise modulation at sample rate]
  affects: [spu94_voice_tick pipeline, PluginProcessor processBlock]
tech_stack:
  added: []
  patterns: [per-tick ephemeral modulation (no writeback to struct), int32 intermediates with clamping]
key_files:
  created: []
  modified:
    - include/spu94/spu94_voice.h
    - src/spu94/spu94_voice.c
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/unit/voice/test_voice_tick.c
decisions:
  - "Mod bus injection order: vol+pan before Step 3 (volume multiply), pitch before Step 4 (counter advance)"
  - "All modulation is ephemeral per-tick — never writes back to v->vol_l, v->vol_r, or v->pitch"
  - "Early-out bitwise OR check skips entire mod bus block when all three depths are 0"
  - "Mod bus coexists with sweep effects — NOT mutually exclusive (operates on different axis)"
metrics:
  duration_seconds: 462
  completed: "2026-05-24T23:46:37Z"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 5
  tests_added: 6
  tests_total: 63
---

# Phase 50 Plan 01: Internal Mod Bus C Core Summary

Per-voice noise-to-pitch/vol/pan modulation at sample rate inside C core voice tick, with host-layer atomic wiring for GUI control.

## Commits

| Task | Name | Commit | Key Changes |
|------|------|--------|-------------|
| 1 | Add mod bus fields and voice tick injection | 8febad0 | 3 depth fields in struct, noise modulation in tick Steps 2.9 and 4, mixer setter API |
| 2 | Unit tests and host-layer wiring | 4b6a09a | 6 unit tests, 3 atomic<float> in PluginProcessor, processBlock wiring |

## Implementation Details

### C Core (spu94_voice_t / spu94_voice_tick)

Three new `int16_t` fields in `spu94_voice_t` between `sweep_r` and `loop_addr`:
- `noise_mod_pitch_depth` — bipolar (-0x7FFF..+0x7FFF): noise offsets effective pitch
- `noise_mod_vol_depth` — unipolar (0..0x7FFF): noise offsets volume symmetrically
- `noise_mod_pan_depth` — unipolar (0..0x7FFF): noise creates L/R divergence

Voice tick injection points:
- **Step 2.9 (new)**: After ADSR, before volume multiply — computes `mod_vol_l`/`mod_vol_r` with vol and pan offsets
- **Step 4**: After base pitch clamp, before counter advance — computes modulated effective pitch

All arithmetic uses `int32_t` intermediates with explicit clamping to PS1 register ranges. When all three depths are 0, the block is skipped entirely (bitwise OR early-out), ensuring bit-identical output for unmodulated voices.

### Mixer API

```c
spu94_result_t spu94_voice_mixer_set_mod_bus(spu94_voice_mixer_t *m, int voice_idx,
    int16_t pitch_depth, int16_t vol_depth, int16_t pan_depth);
```

Validates voice_idx 0..23, stores depths directly on the voice struct. RT-safe (no allocation, no locks).

### Host Layer (PluginProcessor)

Three `std::atomic<float>` fields (0.0..1.0) with accessor methods. ProcessBlock reads atomics at control rate, clamps to [0.0, 1.0] (T-50-01 threat mitigation), converts to int16_t, and calls `spu94_voice_mixer_set_mod_bus` on voice 0. Placed after the stereo widener block — mod bus is NOT mutually exclusive with any sweep-based effect.

## Deviations from Plan

None — plan executed exactly as written.

## Verification Results

1. All 57 existing voice engine tests pass (zero regression from struct field addition)
2. Six new mod bus tests prove each destination works independently and simultaneously
3. Zero-depth produces bit-identical output (verified by test_mod_bus_pitch_depth_zero_no_change)
4. `cmake --build` succeeds with no errors on all modified files
5. RT-safety: no heap/locks/syscalls in the new code path (multiplies, shifts, and int32 clamps only)

## Self-Check: PASSED

All files found, all commits verified, all key patterns present in target files.
