---
phase: 07-pipeline-integration
plan: 01
subsystem: spu94-state
tags: [mixer, dac, api, init, wr-02]
dependency_graph:
  requires: []
  provides: [mixer-fields, dac-fields, mixer-api-declarations, dac-toggle-api-declarations, latency-comp-api, dac-noise-seed-param]
  affects: [spu94_state_internal.h, spu94.h, spu94_state.c, spu94_dac_noise.h, spu94_dac_noise.c]
tech_stack:
  added: []
  patterns: [per-channel-lfsr-seed, latency-comp-default-on, mixer-console-zero-init]
key_files:
  created: []
  modified:
    - include/spu94/spu94_dac_noise.h
    - src/spu94/spu94_dac_noise.c
    - src/spu94/spu94_state_internal.h
    - include/spu94/spu94.h
    - src/spu94/spu94_state.c
    - tests/unit/dac_noise/test_dac_noise_lfsr.c
    - tests/unit/dac_noise/test_dac_noise_amplitude.c
    - tests/unit/dac_noise/test_dac_noise_spectral.c
decisions:
  - "WR-02 fix: spu94_dac_noise_init now accepts uint32_t seed parameter with zero-guard fallback to 0xACE1u"
  - "Per-channel LFSR seeds: L=0xACE1u, R=0x1ECAu to decorrelate L/R DAC noise"
  - "latency_comp defaults to 1 (ON) per D-07, set explicitly in init/reset after zero-fill"
metrics:
  duration: 19min
  completed: "2026-04-29T21:10:00Z"
  tasks: 2/2
  files_modified: 8
---

# Phase 7 Plan 01: State Expansion + API Declarations Summary

**One-liner:** Mixer/DAC struct fields, 22 public API declarations, init/reset fixup with per-channel noise seeds and latency_comp=1 default.

## What Was Done

### Task 1: DAC noise init seed parameter (WR-02 fix)
- Changed `spu94_dac_noise_init` signature from `(state)` to `(state, uint32_t seed)`
- Added zero-guard: `seed ? seed : DAC_NOISE_LFSR_SEED` prevents absorbing state
- Updated all 7 call sites across 3 test files to pass `0xACE1u`
- All 3 dac_noise tests pass

### Task 2: Struct expansion + API declarations + init/reset fixup
- Added 2 includes to `spu94_state_internal.h`: `spu94_dac_fir.h`, `spu94_dac_noise.h`
- Added ~443 bytes of new fields: 6 Q15 faders/sends, latency comp delay buffer (28 samples x 2ch), 3 DAC toggles, 2 DAC FIR states, 2 DAC noise states
- Declared 22 new public API functions in `spu94.h` (6 fader set/get pairs + latency comp pair + 3 DAC toggle pairs)
- Fixed `spu94_init` and `spu94_reset` to plant `latency_comp=1` and per-channel noise seeds after zero-fill
- `_Static_assert` confirms struct remains under 16384 cap
- All 94 non-packaging tests pass (packaging timeouts are pre-existing)

## Commits

| Task | Commit | Description |
|------|--------|-------------|
| 1 | 5284bf3 | feat(07-01): add seed parameter to spu94_dac_noise_init (WR-02 fix) |
| 2 | 55227b0 | feat(07-01): expand spu94_state with mixer/DAC fields, declare public API, fix init/reset |

## Deviations from Plan

None - plan executed exactly as written.

## Verification Results

- Build: cmake --build exits 0 with -Werror/-pedantic
- dac_noise tests: 3/3 pass
- Full suite: 94/94 pass (excluding 2 pre-existing packaging timeouts)
- _Static_assert: no compile error (struct under 16384)
- New API symbols: `grep -c` confirms all 22 declarations present

## Decisions Made

1. **Per-channel LFSR seeds** (L=0xACE1u, R=0x1ECAu): Decorrelates L/R DAC noise per WR-02 finding. 0x1ECA is the byte-reversed form of 0xACE1, chosen for easy auditing.
2. **latency_comp=1 in init/reset**: D-07 mandates default ON, but zero-init gives 0. Explicit set after zero-fill is the same pattern used for DAC noise seeds.

## Self-Check: PASSED

All files verified present, both commits exist in git log.
