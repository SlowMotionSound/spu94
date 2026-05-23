# Technology Stack: v1.9 Complete Voice

**Project:** SPU-94 v1.9
**Researched:** 2026-05-21

## Recommended Stack

No new technology. v1.9 uses the existing stack with zero additions.

### Core (unchanged)
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| C99/C11 | gcc 13+ / clang 18+ | DSP core | RT-safe, no heap, portable to MCU/FPGA |
| Q15 fixed-point | -- | All audio math | Bit-faithful to PS1 SPU (truncation, not rounding) |
| Unity test framework | -- | C unit tests | Already in use since v1.0 |

### Build (unchanged)
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| CMake | 3.22+ | Build system | Already in use |
| ctest | -- | Test runner | Already in use |
| JUCE | 7.x | Standalone + plugin GUI | Already in use since v1.4 |

### Testing (unchanged)
| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| pytest | -- | Integration tests | Already in use |
| Golden WAV files | -- | Regression gate | Already in use |

## Why No New Dependencies

All four v1.9 features are pure integer arithmetic operating on the existing voice pipeline:
- Noise LFSR: bit shifts, XOR, addition (standard C operators)
- Volume Sweep: same counter-accumulate as existing ADSR (same math functions)
- PMON: one multiply + shift per voice per tick (existing `q15_mul_truncate` pattern)
- Signed Volume: already supported by `int16_t` declarations and `q15_mul_truncate`

No external libraries, no new data formats, no new I/O pathways.

## New Source Files (Estimated)

| File | Purpose | LOC (est) |
|------|---------|-----------|
| `include/spu94/spu94_noise.h` | Noise generator type + API declarations | ~40 |
| `src/spu94/spu94_noise.c` | Noise LFSR + timer implementation | ~80 |
| `include/spu94/spu94_sweep.h` | Sweep envelope type + API declarations | ~30 |
| `src/spu94/spu94_sweep.c` | Sweep stepping (uses shared envelope helper) | ~120 |
| `src/spu94/spu94_envelope_step.h` | Shared counter-accumulate core (refactored from ADSR) | ~40 |
| `tests/unit/noise/` | Noise LFSR correctness tests | ~200 |
| `tests/unit/sweep/` | Sweep envelope tests | ~200 |
| `tests/unit/pmon/` | Pitch modulation tests | ~150 |
| **Total new** | | **~860 LOC** |

## Modified Source Files

| File | Change | Risk |
|------|--------|------|
| `spu94_voice.h` | Add sweep_t fields, update vol_l/vol_r docs | LOW |
| `spu94_voice.c` | Accept effective_step, noise_level, capture outx | MEDIUM |
| `spu94_adsr.c` | Extract shared envelope_step core | MEDIUM (golden regression risk) |
| `spu94_process.c` | No changes needed (mixer tick already called) | NONE |

## Sources

- Existing codebase analysis
