# Phase 9: Verification + Documentation - Context

**Gathered:** 2026-04-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Lock down the current DAC model's fingerprint before any future oversampling rework. Golden WAV files with SHA-256 sidecars provide bit-level regression detection, a Python frequency response script verifies the C implementation matches the Phase 5 design, new C unit tests cover integration-level DAC behaviors not tested in Phase 6, and COVERAGE.md gets updated with complete DAC (and backfilled ADPCM) test mappings.

</domain>

<decisions>
## Implementation Decisions

### Golden File Corpus Design
- **D-01:** DAC goldens use a `dac/` subdirectory inside each preset folder, mirroring the existing `adpcm/` pattern. Path: `tests/golden/<preset>/dac/<input>.wav`.
- **D-02:** Same 5 test inputs as the reverb corpus: impulse, white_noise, sine_1khz, silence, sweep. 10 presets × 5 inputs = 50 full-pipeline DAC WAVs.
- **D-03:** Full pipeline goldens — each WAV is produced by running `spu94 process --dac` on the preset/input, capturing the real end-to-end output users hear.
- **D-04:** Additionally, 5 isolated DAC-only reference WAVs (same 5 inputs, no preset/reverb). These capture the pure DAC model fingerprint independent of the reverb interaction. Stored under `tests/golden/dac_isolated/`.
- **D-05:** Total DAC golden corpus: 55 WAVs (50 full-pipeline + 5 isolated), each with SHA-256 sidecar.

### Frequency Response Measurement
- **D-06:** Python measurement script processes a log sweep through the C engine with DAC enabled, FFTs the output, AND computes the analytical response from Q15 coefficients via freqz(). Both curves plotted on the same axes — shows how closely the C implementation tracks the design target.
- **D-07:** Two automated pass/fail checks: (1) passband ripple in the top octave (10–20kHz) stays within the Phase 5 documented tolerance (±0.078dB), (2) max deviation from the analytical design curve at any frequency stays below a defined threshold.
- **D-08:** Script also measures and plots the noise shaping spectral slope in a second subplot. Verifies +12dB/octave highpass slope. One script = one complete DAC characterization report.

### Unit Test Scope
- **D-09:** Phase 6 standalone module tests (FIR coefficients, DC gain, impulse, overflow proof, noise LFSR/amplitude/spectral) remain unchanged. Phase 9 adds integration-level tests.
- **D-10:** New C tests for Phase 9:
  1. Toggle state transitions — DAC on→off→on, FIR sub-toggle, noise sub-toggle, all combinations. Verify state resets cleanly and output is bit-identical after re-enable.
  2. DAC + reverb interaction — process through reverb with DAC enabled vs disabled, verify outputs differ (catches wiring bugs where DAC silently no-ops).
  3. DAC + ADPCM combination — both coloration stages enabled simultaneously, verify they compose correctly through the mixer.
  4. Bit-identity vs Python reference — Python script processes same input through scipy filter + noise model, C test verifies bit-identical output.

### Coverage Map Structure
- **D-11:** New "DAC Model Coverage" section in `docs/COVERAGE.md` after the existing Per-Behavior Coverage table. Rows for: FIR coefficient correctness, FIR frequency response, noise shaping slope, noise amplitude, toggle transitions, state reset, golden regression. Same `test:` column format (`source-path::ctest-name`).
- **D-12:** Also backfill an "ADPCM Coverage" section for v1.1 tests that are currently not mapped in COVERAGE.md. Makes the file the single source of truth for all coloration stages.

### Claude's Discretion
- Exact tolerance threshold for max deviation from analytical design curve (D-07 check 2) — choose a value that's tight enough to catch real drift but not so tight it false-alarms on quantization noise
- Python measurement script location (e.g., `tools/dac_measure.py` or `tests/python/` — follow existing patterns)
- Test file naming and ctest registration details for new Phase 9 C tests

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Golden File Pattern (Direct Template)
- `tests/conformance/test_goldens_present.py` — Structural conformance test for golden corpus; extend for DAC goldens
- `scripts/regenerate_goldens.py` — Golden WAV generation script; extend for `--dac` mode
- `tests/golden/hall/` — Example preset directory structure with `adpcm/` subdirectory pattern

### DAC Model Implementation (What's Being Verified)
- `include/spu94/spu94_dac_fir.h` — FIR module public API
- `include/spu94/spu94_dac_noise.h` — Noise module public API
- `src/spu94/spu94_dac_fir.c` — Three-stage cascaded half-band FIR implementation
- `src/spu94/spu94_dac_noise.c` — LFSR + 2nd-order HP noise shaping
- `src/spu94/spu94_process.c` — Mixer + DAC section integration point

### Existing DAC Tests (Phase 6 — Don't Duplicate)
- `tests/unit/dac_fir/` — Coefficient table, DC gain, impulse response, overflow proof tests
- `tests/unit/dac_noise/` — Amplitude, LFSR, spectral slope tests

### Phase 5 Design Target (Reference for Frequency Response Script)
- `tools/dac_filter_design.py` — Scipy filter design script with `--export-c`; source of truth for design target curve
- `.planning/phases/05-interpolation-filter-design/05-01-SUMMARY.md` — Achieved specs: 0.078dB ripple, 49.3dB stopband, -0.016dB @20kHz

### Coverage Map
- `docs/COVERAGE.md` — Existing per-register and per-behavior coverage tables; add DAC and ADPCM sections
- `scripts/ci/check_coverage.py` — CI enforcement script that validates coverage entries have corresponding tests

### Requirements
- `.planning/REQUIREMENTS.md` — DAC-TEST-01 (golden WAVs), DAC-TEST-02 (frequency response), DAC-TEST-03 (C unit tests), DAC-TEST-04 (COVERAGE.md update)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `scripts/regenerate_goldens.py`: Existing golden generation script — extend with `--dac` flag to produce DAC-enabled goldens in the `dac/` subdirectories
- `tests/conformance/test_goldens_present.py`: Parametrized corpus check — extend PRESETS × INPUTS × MODES enumeration to include DAC goldens
- `tools/dac_filter_design.py`: Already computes and plots the filter frequency response — reuse the design target curve data for overlay in the measurement script
- `tests/python/derive_fir_reference.py`: Python FIR reference pattern for bit-identity testing — adapt for DAC FIR bit-identity verification

### Established Patterns
- Golden files: WAV + SHA-256 sidecar, sha256sum-compatible format, parametrized pytest conformance test
- C unit tests: standalone files under `tests/unit/<module>/`, registered via CMakeLists.txt, ctest names match `test_<module>_<behavior>`
- Coverage map: `test:` cells use backticked `<source-path>::<ctest-name>` format, CI-enforced by check_coverage.py
- Process integration tests: `tests/unit/process/test_process_dac_integration.c` already exists — Phase 9 adds toggle transition and combination tests alongside it

### Integration Points
- `scripts/regenerate_goldens.py` gets DAC mode support
- `tests/conformance/test_goldens_present.py` gets DAC corpus validation
- `docs/COVERAGE.md` gets two new sections (ADPCM + DAC)
- New Python measurement script (location at Claude's discretion)
- New C test files under `tests/unit/process/` or `tests/unit/dac_integration/`

</code_context>

<specifics>
## Specific Ideas

- The frequency response plot should be a single figure with two subplots: top = filter magnitude response (measured vs analytical, both on same axes), bottom = noise spectral slope. Save as PNG alongside the script for quick visual reference.
- The 5 isolated DAC-only reference WAVs serve as the "before" snapshot for the future oversampling rework — when the engine gets truly oversampled, compare against these to characterize what changed.
- The ADPCM coverage backfill should be thorough but doesn't need new tests — just map the existing `tests/unit/adpcm/` and `tests/python/binding/test_binding_adpcm.py` tests into COVERAGE.md rows.

</specifics>

<deferred>
## Deferred Ideas

- **Real oversampling engine** — current DAC FIR approximates at 44.1kHz; real implementation would zero-stuff and run cascade at elevated rate. Variable 1x–128x knob. Captured in STATE.md as a future feature.
- **Hardware calibration** — DAC-HW-01 through DAC-HW-03 deferred to M5. Noise amplitude and filter response refinement against Anthony's PS1 hardware captures.

</deferred>

---

*Phase: 09-Verification + Documentation*
*Context gathered: 2026-04-30*
