# Requirements: SPU-94

**Defined:** 2026-04-30
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## v1.3 Requirements

Requirements for true oversampled DAC milestone. Each maps to roadmap phases.

### Core DSP (True Oversampling)

- [ ] **DSP-01**: Zero-stuff input to 352.8kHz (insert 7 zeros between each 44.1kHz sample)
- [ ] **DSP-02**: Run Stage 1 interpolation FIR at 88.2kHz using v1.2 coefficients verbatim
- [ ] **DSP-03**: Run Stage 2 interpolation FIR at 176.4kHz using v1.2 coefficients verbatim
- [ ] **DSP-04**: Run Stage 3 interpolation FIR at 352.8kHz using v1.2 coefficients verbatim
- [x] **DSP-05**: Run LFSR + HP noise model at 352.8kHz (8 ticks per output sample), retune DAC_NOISE_SHIFT for correct -90dB amplitude
- [ ] **DSP-06**: Decimate 352.8kHz output to 44.1kHz (pick every 8th sample after full cascade)
- [ ] **DSP-07**: Report correct group delay via spu94_get_total_latency_samples for true oversampled path
- [ ] **DSP-08**: Real-time safety preserved — no heap, no locks, no syscalls in 8x processing path

### Comparison + Characterization

- [x] **CMP-01**: A/B mode toggle — selectable v1.2 (approx) vs v1.3 (true) DAC processing path
- [x] **CMP-02**: Python characterization script comparing v1.2 vs v1.3 (frequency response, impulse response, noise floor, time-domain)
- [x] **CMP-03**: ADR documenting whether true oversampling produces audible differences, with measurement evidence

### Integration + Verification

- [ ] **INT-01**: CLI/Python/JUCE surfaces unchanged — --dac, set_dac_enabled(), JUCE toggle work identically
- [x] **INT-02**: v1.2 DAC golden files archived; new v1.3 golden files generated
- [ ] **INT-03**: DAC-off golden files are bit-identical before and after (zero blast radius on non-DAC paths)
- [ ] **INT-04**: Audio-band frequency response matches v1.2 within 0.05dB (Q15 truncation budget across 14 evaluations/sample)

## Future Requirements

Deferred to subsequent milestones.

### Preset System (v1.4)

- **PRES-01**: Save/load custom presets in .spu94 format
- **PRES-02**: C core preset save/load API
- **PRES-03**: CLI preset-dump/preset-load subcommands
- **PRES-04**: JUCE Save/Load buttons

### DAW Plugin (v1.5)

- **PLUG-01**: JUCE VST3 plugin build
- **PLUG-02**: JUCE AU plugin build
- **PLUG-03**: JUCE AAX plugin build (requires Avid developer program)

## Out of Scope

Explicitly excluded. Documented to prevent scope creep.

| Feature | Reason |
|---------|--------|
| Variable oversampling rate (2x/4x/8x knob) | AK4309 is fixed 8x — variable rates turn this into a generic resampler, not a PS1 model |
| Analog post-filter modeling (SCF + CTF) | Separate problem requiring hardware measurements — future milestone |
| Higher-quality interpolation (more taps) | Goal is matching AK4309, not exceeding it |
| Sigma-delta 1-bit modulator simulation at 11.3MHz | Enormous complexity, negligible audible benefit over noise model |
| Output at 352.8kHz (skip decimation) | SPU-94's contract is 44.1kHz int16 stereo |
| Coefficient redesign | v1.2 coefficients were designed for correct rates — reuse verbatim |
| Continuous oversampling sweep (luxury crossfade) | Deferred creative feature — build foundation first |

## Traceability

Which phases cover which requirements. Updated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| DSP-01 | Phase 10 | Pending |
| DSP-02 | Phase 10 | Pending |
| DSP-03 | Phase 10 | Pending |
| DSP-04 | Phase 10 | Pending |
| DSP-05 | Phase 11 | Complete (11-01) |
| DSP-06 | Phase 10 | Pending |
| DSP-07 | Phase 11 | Pending |
| DSP-08 | Phase 10 | Pending |
| CMP-01 | Phase 11 | Complete (11-01) |
| CMP-02 | Phase 12 | Complete |
| CMP-03 | Phase 12 | Complete |
| INT-01 | Phase 11 | Pending |
| INT-02 | Phase 12, Plan 01 | Complete |
| INT-03 | Phase 10 | Pending |
| INT-04 | Phase 10 | Pending |

**Coverage:**
- v1.3 requirements: 15 total
- Mapped to phases: 15
- Unmapped: 0

---
*Requirements defined: 2026-04-30*
*Last updated: 2026-05-01 -- DSP-05 and CMP-01 complete (Phase 11 Plan 01)*
