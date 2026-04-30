# Phase 9: Verification + Documentation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-30
**Phase:** 09-verification-documentation
**Areas discussed:** Golden file corpus design, Frequency response measurement, Unit test scope, Coverage map structure

---

## Golden File Corpus Design

| Option | Description | Selected |
|--------|-------------|----------|
| dac/ subdirectory per preset | tests/golden/hall/dac/impulse.wav — mirrors adpcm/ pattern | ✓ |
| Flat with dac_ prefix | All files in one directory with dac_ prefix | |
| Separate top-level dac/ tree | Completely separate corpus root | |

**User's choice:** dac/ subdirectory per preset
**Notes:** Mirrors the established adpcm/ pattern exactly.

| Option | Description | Selected |
|--------|-------------|----------|
| Same 5 as reverb | impulse, white_noise, sine_1khz, silence, sweep — 50 WAVs | ✓ |
| Same 3 as ADPCM | impulse, sine_1khz, chirp — 30 WAVs | |
| Custom set | Different inputs tuned for DAC analysis | |

**User's choice:** Same 5 as reverb

| Option | Description | Selected |
|--------|-------------|----------|
| Full pipeline with DAC on | Run spu94 process --dac, captures real end-to-end output | |
| Isolated DAC-only processing | Feed clean signals through just DAC module | |
| Both | Full pipeline goldens + DAC-isolated reference files | ✓ |

**User's choice:** Both — initially selected full pipeline, then re-read and chose both.
**Notes:** User wanted to re-read the question before deciding. Isolated set serves as "before" snapshot for future oversampling rework.

| Option | Description | Selected |
|--------|-------------|----------|
| Same 5 inputs, no presets | 5 isolated WAVs — covers full frequency behavior | ✓ |
| Just impulse + sweep | 2 minimal WAVs | |
| 5 inputs × 3 sub-toggle combos | 15 WAVs capturing each sub-component | |

**User's choice:** Same 5 inputs, no presets — 5 isolated DAC reference WAVs.

---

## Frequency Response Measurement

| Option | Description | Selected |
|--------|-------------|----------|
| Process sweep through C engine | Measures real end-to-end behavior including quantization | |
| Analytical from coefficients | Computes freqz() from Q15 table — faster but design-only | |
| Both overlaid | Both curves on same axes — shows implementation vs design | ✓ |

**User's choice:** Both overlaid

| Option | Description | Selected |
|--------|-------------|----------|
| Passband ripple within tolerance | Check top-octave ripple stays within ±0.078dB | |
| Max deviation from design curve | Check max deviation at any frequency | |
| Both checks | Belt and suspenders | ✓ |

**User's choice:** Both checks

| Option | Description | Selected |
|--------|-------------|----------|
| Include noise in same script | Second subplot for noise floor spectrum + slope verification | ✓ |
| Keep noise in C unit tests only | Already verified in test_dac_noise_spectral.c | |

**User's choice:** Include noise — one script = one complete DAC characterization report.

---

## Unit Test Scope

| Option | Description | Selected |
|--------|-------------|----------|
| Toggle state transitions | DAC on→off→on, sub-toggles, state reset verification | ✓ |
| DAC + reverb interaction | Verify output differs with DAC enabled vs disabled | ✓ |
| DAC + ADPCM combination | Both coloration stages enabled simultaneously | ✓ |
| Bit-identity vs Python reference | C output matches scipy reference for identical inputs | ✓ |

**User's choice:** All four (multi-select)

---

## Coverage Map Structure

| Option | Description | Selected |
|--------|-------------|----------|
| New 'DAC Model Coverage' section | Third table in COVERAGE.md, same test: format | ✓ |
| Extend Per-Behavior table | Add DAC rows to existing table | |
| Separate COVERAGE-DAC.md file | New file cross-referenced from main | |

**User's choice:** New dedicated section

| Option | Description | Selected |
|--------|-------------|----------|
| Add ADPCM section too | Backfill ADPCM coverage while adding DAC | ✓ |
| DAC only for now | Scope to just DAC coverage | |

**User's choice:** Add ADPCM section too — makes COVERAGE.md the single source of truth for all coloration stages.

---

## Claude's Discretion

- Exact tolerance threshold for max deviation from analytical design curve
- Python measurement script location
- Test file naming and ctest registration details

## Deferred Ideas

- Real oversampling engine — zero-stuff and run cascade at elevated rate, variable 1x–128x knob (captured in STATE.md)
- Hardware calibration — DAC-HW-01 through DAC-HW-03 deferred to M5
