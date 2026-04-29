---
phase: 06-dac-core-implementation
plan: 02
subsystem: dac-noise
tags: [noise, dac, lfsr, delta-sigma, highpass-shaping]
dependency_graph:
  requires: [phase-6-plan-01]
  provides: [spu94_dac_noise_step, spu94_dac_noise_init, spu94_dac_noise_state]
  affects: [phase-7-pipeline-integration]
tech_stack:
  added: []
  patterns: [galois-lfsr, 2nd-order-highpass-shaping, goertzel-spectral-test]
key_files:
  created:
    - include/spu94/spu94_dac_noise.h
    - src/spu94/spu94_dac_noise.c
    - tests/unit/dac_noise/CMakeLists.txt
    - tests/unit/dac_noise/test_dac_noise_lfsr.c
    - tests/unit/dac_noise/test_dac_noise_spectral.c
    - tests/unit/dac_noise/test_dac_noise_amplitude.c
  modified:
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
decisions:
  - "DAC_NOISE_SHIFT tuned from 9 (research estimate) to 14 for correct -90 dB amplitude calibration"
  - "Unity double assertions replaced with TEST_ASSERT_TRUE for project compatibility (no UNITY_INCLUDE_DOUBLE)"
  - "Zero-LFSR silence test accounts for 2nd-order HP filter transient (settle 10 samples before asserting zero)"
metrics:
  duration: 18min
  completed: "2026-04-29T01:29:00Z"
  tasks: 2
  files_created: 6
  files_modified: 2
  test_count: 3
  test_assertions: 5+2+2=9 (plus 100+100 loop assertions in LFSR tests)
---

# Phase 6 Plan 02: DAC Noise Model Summary

LFSR white noise with 2nd-order highpass shaping producing +12 dB/octave spectral slope at -85 dB RMS, matching AK4309 delta-sigma noise character with DAC_NOISE_SHIFT=14 amplitude calibration.

## Commits

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Noise module (header + implementation + build) | df018cb | spu94_dac_noise.h, spu94_dac_noise.c, CMakeLists.txt |
| 2 | Unit tests (LFSR, spectral, amplitude) | 7a3af33 | 3 test files + test CMakeLists + NOISE_SHIFT tuning |

## What Was Built

- **Public API** (`include/spu94/spu94_dac_noise.h`): `spu94_dac_noise_state` struct with LFSR and HP filter history, `spu94_dac_noise_init()`, and `spu94_dac_noise_step()`. Standalone module, no spu94_state dependency. WARNING comment documents that memset(0) is invalid (LFSR absorbing state).

- **LFSR noise source** (`src/spu94/spu94_dac_noise.c`): 32-bit Galois LFSR with polynomial x^32 + x^22 + x^2 + x^1 + 1 (feedback mask 0x80200003). Period 2^32-1 (~27 hours at 44.1 kHz). Upper 16 bits extracted for better statistical properties.

- **2nd-order HP shaping**: y[n] = x[n] - 2*x[n-1] + x[n-2], equivalent to NTF(z) = (1-z^-1)^2. Produces +12 dB/octave spectral slope characteristic of delta-sigma quantization noise.

- **Amplitude calibration**: DAC_NOISE_SHIFT=14 produces ~-85 dB RMS in the audio band. Tunable compile-time constant per D-06. Measured: ~21 dB slope across 2 octaves (2kHz to 8kHz), 1.87 LSBs RMS.

- **Three unit tests**: LFSR behavioral verification (5 subtests: non-zero init, non-zero output, zero-absorbing proof, no short period, deterministic), Goertzel-based spectral slope verification, and RMS amplitude measurement.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] DAC_NOISE_SHIFT value incorrect**
- **Found during:** Task 2
- **Issue:** Plan specified NOISE_SHIFT=9 based on research estimate. Actual measurement showed -55 dB RMS (35 dB too loud). The research derivation used an in-band integration approximation that understated the full-band RMS by ~35 dB.
- **Fix:** Tuned DAC_NOISE_SHIFT from 9 to 14, producing -85 dB RMS (within the -100 to -80 dB acceptance window, close to -90 dB target per D-04).
- **Files modified:** src/spu94/spu94_dac_noise.c
- **Commit:** 7a3af33

**2. [Rule 1 - Bug] Zero-LFSR silence test failed due to HP filter transient**
- **Found during:** Task 2
- **Issue:** Test assumed zero LFSR produces immediate silence, but the 2nd-order HP filter has a step response transient. With LFSR=0, x[n] is constant (-64 after centering and shifting), so HP output is non-zero for the first few samples until x[n]=x[n-1]=x[n-2].
- **Fix:** Added 10-sample settling period before asserting silence. Documented the HP step response reason.
- **Files modified:** tests/unit/dac_noise/test_dac_noise_lfsr.c
- **Commit:** 7a3af33

**3. [Rule 3 - Blocking] Unity Double Precision not enabled**
- **Found during:** Task 2
- **Issue:** TEST_ASSERT_GREATER_OR_EQUAL_DOUBLE_MESSAGE requires UNITY_INCLUDE_DOUBLE compile definition, which is not configured in this project's vendored Unity.
- **Fix:** Replaced double-precision Unity assertions with TEST_ASSERT_TRUE_MESSAGE wrapping manual double comparisons. Functionally equivalent.
- **Files modified:** tests/unit/dac_noise/test_dac_noise_spectral.c, tests/unit/dac_noise/test_dac_noise_amplitude.c
- **Commit:** 7a3af33

## Verification Results

- `ctest -L dac_noise --output-on-failure`: 3/3 tests passing
- `cmake --build build --target spu94_static`: compiles clean under -Werror/-pedantic
- Core unit test suite: 38/38 labeled tests pass (zero regression)
- DAC FIR tests (Plan 01): 4/4 still passing

## Known Stubs

None -- all code paths are fully wired with production data sources.

## Self-Check: PASSED

- [x] include/spu94/spu94_dac_noise.h exists
- [x] src/spu94/spu94_dac_noise.c exists
- [x] tests/unit/dac_noise/ directory with 3 test files + CMakeLists.txt
- [x] Commit df018cb exists
- [x] Commit 7a3af33 exists
