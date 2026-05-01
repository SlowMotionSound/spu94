---
phase: 11-noise-recalibration-integration
plan: 01
subsystem: dsp
tags: [dac, noise, fir, oversampling, q15, lfsr, toggle]

# Dependency graph
requires:
  - phase: 10-core-polyphase-fir-cascade
    provides: spu94_dac_fir_step_8x (8x cascade at true rates)
provides:
  - dac_true_oversample toggle API (set/get pair, default=1)
  - spu94_dac_noise_step_8x for 352.8kHz raw white noise generation
  - spu94_dac_fir_step_8x_with_noise combined FIR+noise function
  - DAC_NOISE_SHIFT_8X=10 and DAC_NOISE_8X_ACC_SCALE=14 calibration constants
  - test_dac_noise_8x.c with 3 tests (RMS, non-silence, differs-from-noiseless)
affects: [11-02-PLAN, pipeline-integration, latency-reporting, surface-integration]

# Tech tracking
tech-stack:
  added: []
  patterns: [int32-precision-noise-injection, post-cascade-noise-scaling, accumulator-level-dithering]

key-files:
  created:
    - tests/unit/dac_noise/test_dac_noise_8x.c
  modified:
    - src/spu94/spu94_state_internal.h
    - src/spu94/spu94_state.c
    - src/spu94/spu94_io_chain.c
    - include/spu94/spu94.h
    - src/spu94/spu94_dac_noise.c
    - include/spu94/spu94_dac_noise.h
    - src/spu94/spu94_dac_fir.c
    - include/spu94/spu94_dac_fir.h
    - tests/unit/dac_noise/CMakeLists.txt

key-decisions:
  - "Raw white noise at 352.8kHz instead of HP-shaped: HP shaping pushes >99.9% of noise above audio at 8x rate, making it impractical at int16 precision"
  - "Post-cascade int32 noise injection to avoid -72dBFS quantization floor from int16 intermediate + <<3 gain"
  - "DAC_NOISE_SHIFT_8X=10 with DAC_NOISE_8X_ACC_SCALE=14 empirically calibrated to hit -90dB target"

patterns-established:
  - "Post-cascade noise injection: when int16 intermediates cause quantization floor above target noise level, inject at int32 precision after cascade decimation but before gain compensation"
  - "LFSR decorrelation at converter rate: advance LFSR 8 times per output sample even though only the last noise sample contributes, for L/R decorrelation"

requirements-completed: [DSP-05, CMP-01]

# Metrics
duration: 49min
completed: 2026-05-01
---

# Phase 11 Plan 01: Toggle API + Noise Injection Foundation Summary

**A/B mode toggle API, DAC_NOISE_SHIFT_8X calibration, and spu94_dac_fir_step_8x_with_noise with int32-precision noise injection hitting -90dB in-band RMS through empirical tuning**

## Performance

- **Duration:** 49 min
- **Started:** 2026-05-01T02:42:11Z
- **Completed:** 2026-05-01T03:31:00Z
- **Tasks:** 2 (Task 2 was TDD: RED + GREEN)
- **Files modified:** 9

## Accomplishments
- A/B toggle API (dac_true_oversample) with set/get pair, default=1 (v1.3 ON) in init and reset
- spu94_dac_fir_step_8x_with_noise combines FIR cascade with noise injection at 352.8kHz rate
- Noise amplitude empirically calibrated: in-band RMS between -100 and -80 dBFS validated by test
- spu94_dac_noise_step_8x produces raw white noise (no HP shaping) for 352.8kHz operation
- All 103 non-packaging tests continue to pass (2 pre-existing packaging failures unrelated)

## Task Commits

Each task was committed atomically:

1. **Task 1: Add A/B toggle API + noise shift constant + header declarations** - `54288e7` (feat)
2. **Task 2 RED: Add failing tests for 8x noise injection** - `c83b11c` (test)
3. **Task 2 GREEN: Implement spu94_dac_fir_step_8x_with_noise** - `5d693a3` (feat)

## Files Created/Modified
- `src/spu94/spu94_state_internal.h` - Added dac_true_oversample uint8_t field in DAC section
- `src/spu94/spu94_state.c` - Default dac_true_oversample=1 in spu94_init and spu94_reset
- `src/spu94/spu94_io_chain.c` - set/get toggle pair following existing pattern
- `include/spu94/spu94.h` - Public declarations for dac_true_oversample toggle
- `src/spu94/spu94_dac_noise.c` - DAC_NOISE_SHIFT_8X=10 constant, spu94_dac_noise_step_8x function
- `include/spu94/spu94_dac_noise.h` - Declaration for step_8x, updated header comment for Phase 11
- `src/spu94/spu94_dac_fir.c` - spu94_dac_fir_step_8x_with_noise implementation, DAC_NOISE_8X_ACC_SCALE
- `include/spu94/spu94_dac_fir.h` - Declaration for step_8x_with_noise, added spu94_dac_noise.h include
- `tests/unit/dac_noise/test_dac_noise_8x.c` - 3 noise amplitude/injection verification tests
- `tests/unit/dac_noise/CMakeLists.txt` - CMake entry for test_dac_noise_8x

## Decisions Made

1. **Raw white noise at 352.8kHz (not HP-shaped):** The 2nd-order HP shaping (1-z^-1)^2 designed for 44.1kHz operation pushes >99.92% of noise power above 22.05kHz at 352.8kHz sample rate. With int16 arithmetic, this leaves effectively zero in-band noise regardless of shift value. Raw white noise is the correct physical model -- DAC quantization noise is broadband at the converter clock rate, and the reconstruction filter determines its spectral shape at the output.

2. **Post-cascade int32 noise injection:** The plan specified q15_add_sat noise injection at each Stage 3 evaluation. However, the int16 FIR output quantization combined with <<3 gain compensation creates a minimum non-zero output of 8 LSBs = -72 dBFS, above the -90 dBFS target. The solution: add noise at int32 precision after the cascade's decimation pick but before <<3 gain compensation, using the last of 8 noise samples (the other 7 advance the LFSR for L/R decorrelation).

3. **Empirical calibration of DAC_NOISE_SHIFT_8X=10 and DAC_NOISE_8X_ACC_SCALE=14:** The theoretical estimate of SHIFT_8X=9 from the plan's research was based on HP-shaped noise, which proved architecturally incompatible with int16 precision at 352.8kHz. The two-constant approach (SHIFT for raw amplitude, ACC_SCALE for post-cascade injection) was tuned empirically to hit the -90 dB target.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Int16 quantization floor prevents plan's q15_add_sat noise injection**
- **Found during:** Task 2 (GREEN phase, noise calibration)
- **Issue:** The plan specified injecting noise via q15_add_sat at each Stage 3 evaluation. The minimum non-zero int16 noise value after <<3 gain compensation is 8 LSBs = -72 dBFS, which is above the -90 dBFS target. No int16 noise amplitude can produce output below -72 dBFS through this path. Empirical testing confirmed: SHIFT values from 9 to 20 all produced output between -36.9 dB and -70.5 dB, never reaching the -80 to -100 dB target window.
- **Fix:** Changed to post-cascade int32 noise injection. The last of 8 raw white noise samples is scaled by DAC_NOISE_8X_ACC_SCALE and right-shifted, then added to the decimated signal at int32 precision before <<3 gain and saturation. This allows sub-LSB noise amplitudes.
- **Files modified:** src/spu94/spu94_dac_fir.c, src/spu94/spu94_dac_noise.c
- **Verification:** test_noise_8x_rms_in_band passes with RMS in -100 to -80 dB window
- **Committed in:** 5d693a3 (Task 2 GREEN commit)

**2. [Rule 1 - Bug] HP-shaped noise impractical at 352.8kHz int16 precision**
- **Found during:** Task 2 (GREEN phase, noise calibration)
- **Issue:** The plan's spu94_dac_noise_step_8x used HP shaping identical to spu94_dac_noise_step. At 352.8kHz, the HP filter (1-z^-1)^2 pushes >99.92% of noise power above audio. After right-shifting to int16, input values quantize to {-1, 0, 1} for any shift >= 15, creating a quantization floor at -70.5 dBFS regardless of shift value.
- **Fix:** Changed spu94_dac_noise_step_8x to produce raw white noise (no HP shaping). This is physically correct: DAC quantization noise is broadband at the converter clock rate. The x_prev/x_prev2 HP state is not updated by step_8x to avoid corrupting the 44.1kHz path's HP shaping state.
- **Files modified:** src/spu94/spu94_dac_noise.c
- **Verification:** All 4 dac_noise tests pass (original 3 + new 8x test)
- **Committed in:** 5d693a3 (Task 2 GREEN commit)

---

**Total deviations:** 2 auto-fixed (2 Rule 1 bugs)
**Impact on plan:** Both deviations were necessary to achieve the -90 dBFS noise amplitude target. The noise injection point (352.8kHz, LFSR-based) and the combined function API are exactly as planned. Only the internal implementation of noise generation (raw vs HP-shaped) and injection precision (int32 vs int16) changed. No scope creep.

## Issues Encountered
- Extensive empirical calibration cycle during Task 2: 8 iterations testing different SHIFT values (9, 10, 12, 13, 14, 15, 17, 20) and injection strategies before discovering the int16 quantization floor. This was anticipated by the plan ("tune empirically") but the magnitude of the issue (50+ dB gap between theoretical and achievable) required an architectural change rather than simple constant tuning.

## User Setup Required
None - no external service configuration required.

## TDD Gate Compliance

Verified in git log:
1. RED gate: `c83b11c` test(11-01) commit -- linker error confirms failing tests
2. GREEN gate: `5d693a3` feat(11-01) commit -- all 3 tests pass with implementation
3. No REFACTOR gate needed (implementation is clean)

## Next Phase Readiness
- Toggle API exists and is ready for Plan 02 to wire into spu94_process.c path selection
- spu94_dac_fir_step_8x_with_noise is ready for Plan 02 to call when dac_true_oversample=1 and both FIR+noise are enabled
- Latency calculation update needed in Plan 02
- Surface integration verification needed in Plan 02

## Self-Check: PASSED

All files verified present (10/10). All commits verified (3/3). SUMMARY.md exists.

---
*Phase: 11-noise-recalibration-integration*
*Completed: 2026-05-01*
