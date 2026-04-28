# Requirements: SPU-94 v1.2 DAC Modeling

**Defined:** 2026-04-28
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.2 Requirements

Requirements for the DAC modeling milestone. The PS1 uses an AKM AK4309AVM 1-bit delta-sigma DAC with 8x digital interpolation, 2nd-order noise shaping, and on-chip reconstruction filtering. This milestone models the converter's digital-domain artifacts as a toggleable coloration stage.

### DAC Interpolation Filter (DAC-FILT)

- [ ] **DAC-FILT-01**: Scipy script designs interpolation filter matching AK4309 8x FIR specs (cascaded half-band stages, +/-0.05dB passband ripple per datasheet, 41dB stopband attenuation), plots frequency response, and overlays against Stereophile PS1 measurements as sanity check
- [ ] **DAC-FILT-02**: Interpolation filter implemented in C as Q15 fixed-point FIR or biquad approximation operating at 44.1kHz after the output FIR, faithfully reproducing the top-octave passband ripple character of the AK4309's cascaded half-band interpolator
- [ ] **DAC-FILT-03**: ADR documenting passband ripple gray area — datasheet spec (+/-0.05dB) vs Stereophile measured ripple (described as audible), with reasoned resolution and confidence assessment

### DAC Noise Shaping (DAC-NOISE)

- [ ] **DAC-NOISE-01**: 2nd-order shaped noise model matching the AK4309's delta-sigma modulator characteristics — LFSR source with 2nd-order highpass shaping producing +12dB/octave spectral slope, calibrated to ~90dB dynamic range at 384x OSR

### Pipeline Integration (DAC-INT)

- [ ] **DAC-INT-01**: DAC model inserted at 44.1kHz after spu94_fir_chain_step output, toggleable via spu94_set_dac_enabled(), default-off, following the ADPCM toggle pattern
- [ ] **DAC-INT-02**: DAC state contained within spu94_state budget, disable resets filter/noise state cleanly, zero regression on all existing tests with DAC disabled
- [ ] **DAC-INT-03**: All rt_safety gates (rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency) pass with DAC enabled

### I/O Surface (DAC-IO)

- [ ] **DAC-IO-01**: CLI --dac flag enables DAC model on spu94 process command (matches --adpcm pattern)
- [ ] **DAC-IO-02**: Python ctypes bindings expose DAC toggle (matches ADPCM binding pattern)
- [ ] **DAC-IO-03**: JUCE standalone GUI includes DAC toggle checkbox (matches ADPCM toggle)

### Verification (DAC-TEST)

- [ ] **DAC-TEST-01**: DAC-enabled golden WAV files with SHA-256 sidecar regression gate (separate corpus from reverb-only and ADPCM goldens)
- [ ] **DAC-TEST-02**: Python frequency response script measures DAC model output, plots against design target curve, and verifies passband ripple magnitude falls within documented tolerance
- [ ] **DAC-TEST-03**: C unit tests verify filter coefficient correctness, noise shaping spectral slope (+12dB/octave), toggle on/off state transitions, and filter state reset on disable
- [ ] **DAC-TEST-04**: docs/COVERAGE.md updated with DAC model test mappings

## Future Requirements

### Hardware Calibration (deferred to M5)

- **DAC-HW-01**: Noise amplitude calibrated against captures from Anthony's PS1 hardware
- **DAC-HW-02**: Filter frequency response refined against hardware measurements (isolated DAC output vs full analog chain)
- **DAC-HW-03**: A/B perceptual comparison between DAC model output and real PS1 hardware output

### Late-Revision DAC (deferred indefinitely)

- **DAC-LATE-01**: Model the CXD2938Q integrated DAC behavior (PU-22+ boards) as an alternative DAC profile

## Out of Scope

| Feature | Reason |
|---------|--------|
| Analog output stage (op-amps, coupling caps, output impedance) | Requires real hardware measurement; separate future milestone |
| SCF/CTF reconstruction filter modeling | Artifacts are ultrasonic (>50kHz), inaudible in audio band |
| ZOH sinc droop compensation | At 384x OSR, droop at 20kHz is 0.000009dB — completely negligible |
| Idle tone modeling | At -70 to -80dB, masked by program material; theoretical concern only |
| Delta-sigma modulator simulation at true 384x rate | Would require 16.9MHz processing for zero audible benefit; we model the artifacts, not the mechanism |
| R2R ladder / DNL/INL nonlinearity modeling | Wrong topology — the AK4309 is 1-bit delta-sigma with perfect inherent linearity |
| Late-revision integrated DAC (PU-22+ / PSone) | Different silicon, undocumented; target the classic AK4309 sound |

## Traceability

| Requirement | Phase | Status |
|-------------|-------|--------|
| DAC-FILT-01 | — | Pending |
| DAC-FILT-02 | — | Pending |
| DAC-FILT-03 | — | Pending |
| DAC-NOISE-01 | — | Pending |
| DAC-INT-01 | — | Pending |
| DAC-INT-02 | — | Pending |
| DAC-INT-03 | — | Pending |
| DAC-IO-01 | — | Pending |
| DAC-IO-02 | — | Pending |
| DAC-IO-03 | — | Pending |
| DAC-TEST-01 | — | Pending |
| DAC-TEST-02 | — | Pending |
| DAC-TEST-03 | — | Pending |
| DAC-TEST-04 | — | Pending |

**Coverage:**
- v1.2 requirements: 14 total
- Mapped to phases: 0
- Unmapped: 14 (awaiting roadmap)

---
*Requirements defined: 2026-04-28*
*Last updated: 2026-04-28 after initial definition*
