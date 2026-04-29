# Roadmap: SPU-94

**Updated:** 2026-04-28
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- :construction: **v1.2 DAC Modeling** — Phases 5-9 (in progress)
- :white_check_mark: **v1.1 ADPCM** — Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- :white_check_mark: **v1.0 Product** — 8 phases (shipped 2026-04-26, standalone GUI)
- :white_check_mark: **M1 Reverb Core** — 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Phases

**Phase Numbering:**
- Integer phases (1, 2, 3...): Planned milestone work
- Decimal phases (5.1, 5.2): Urgent insertions (marked with INSERTED)

- [x] **Phase 1: Core Codec** - ADPCM decode/encode from spec (v1.1, shipped)
- [x] **Phase 2: Pipeline Integration** - ADPCM wired into reverb chain (v1.1, shipped)
- [x] **Phase 3: I/O Layer** - CLI, Python, JUCE for ADPCM (v1.1, shipped)
- [x] **Phase 4: Verification + Documentation** - ADPCM goldens, coverage, ADRs (v1.1, shipped)
- [x] **Phase 5: Interpolation Filter Design** - Scipy prototype of AK4309 8x cascaded half-band FIR, verified against datasheet specs (completed 2026-04-28)
- [ ] **Phase 6: DAC Core Implementation** - Q15 fixed-point interpolation filter and shaped noise model in C
- [ ] **Phase 7: Pipeline Integration** - DAC model wired into spu94_process as toggleable post-FIR stage
- [ ] **Phase 8: I/O Surface** - CLI --dac flag, Python ctypes toggle, JUCE DAC checkbox
- [ ] **Phase 9: Verification + Documentation** - Golden files, frequency response plots, unit tests, coverage map

## Phase Details

### Phase 5: Interpolation Filter Design
**Goal**: The AK4309's digital interpolation filter is characterized as a scipy prototype with automated pass/fail verification against AK4309B datasheet specs
**Depends on**: Nothing (v1.2 entry point; builds on shipped v1.1 codebase)
**Requirements**: DAC-FILT-01, DAC-FILT-03
**Success Criteria** (what must be TRUE):
  1. A Python/scipy script produces FIR coefficients matching the AK4309's 8x cascaded half-band interpolator spec (+/-0.05dB passband ripple, 41dB stopband attenuation) with automated pass/fail assertions
  2. The script plots the designed filter's frequency response with datasheet spec limits as reference lines
  3. An ADR documents the passband ripple gray area (datasheet spec vs Stereophile composite-chain measurements) with a reasoned resolution and confidence assessment
**Plans:** 1 plan
Plans:
- [x] 05-01-PLAN.md — Filter design script + verification + plot + ADR (completed 2026-04-28)

### Phase 6: DAC Core Implementation
**Goal**: The interpolation filter and delta-sigma noise model exist as tested C modules operating at 44.1kHz in Q15 fixed-point
**Depends on**: Phase 5 (filter coefficients designed in scipy)
**Requirements**: DAC-FILT-02, DAC-NOISE-01
**Success Criteria** (what must be TRUE):
  1. The interpolation filter is implemented in C as Q15 fixed-point (FIR or biquad) at 44.1kHz, reproducing the top-octave passband ripple character from the Phase 5 design
  2. A 2nd-order shaped noise model produces +12dB/octave highpass spectral slope from an LFSR source, calibrated to ~90dB dynamic range at the AK4309's operating point
  3. Both modules compile clean under the existing -Werror/-pedantic flags and pass standalone unit tests before integration
**Plans:** 2 plans
Plans:
- [x] 06-01-PLAN.md — DAC interpolation filter (3-stage cascaded half-band FIR in Q15) -- completed 2026-04-29
- [x] 06-02-PLAN.md — DAC noise model (LFSR + 2nd-order HP shaping) -- completed 2026-04-29

### Phase 7: Pipeline Integration
**Goal**: The DAC model is a toggleable coloration stage in the spu94_process signal chain, following the ADPCM precedent exactly
**Depends on**: Phase 6 (filter and noise modules exist)
**Requirements**: DAC-INT-01, DAC-INT-02, DAC-INT-03
**Success Criteria** (what must be TRUE):
  1. `spu94_set_dac_enabled()`/`spu94_get_dac_enabled()` toggle the DAC model on/off, default-off, with disable resetting filter/noise state cleanly
  2. The DAC model processes samples at 44.1kHz after `spu94_fir_chain_step` output, matching the hardware signal path (SPU serial output feeds DAC)
  3. All existing tests pass with DAC disabled (zero regression), and DAC state fits within the existing `spu94_state` budget
  4. All four rt_safety gates (rt_no_heap, rt_no_locks, rt_no_syscalls, rt_bench_latency) pass with DAC enabled
**Plans**: TBD

### Phase 8: I/O Surface
**Goal**: DAC coloration is accessible through all three I/O layers (CLI, Python, JUCE) matching the ADPCM toggle pattern
**Depends on**: Phase 7 (DAC toggle API exists)
**Requirements**: DAC-IO-01, DAC-IO-02, DAC-IO-03
**Success Criteria** (what must be TRUE):
  1. `spu94 process --dac` enables DAC coloration on CLI WAV processing (same pattern as `--adpcm`)
  2. Python ctypes bindings expose `spu94_set_dac_enabled`/`spu94_get_dac_enabled` with the same calling convention as the ADPCM toggle
  3. The JUCE standalone GUI includes a DAC toggle checkbox alongside the existing ADPCM toggle
**Plans**: TBD
**UI hint**: yes

### Phase 9: Verification + Documentation
**Goal**: DAC model correctness is locked by golden files, frequency response measurements, unit tests, and coverage mapping
**Depends on**: Phases 7-8 (full DAC integration available)
**Requirements**: DAC-TEST-01, DAC-TEST-02, DAC-TEST-03, DAC-TEST-04
**Success Criteria** (what must be TRUE):
  1. DAC-enabled golden WAV files with SHA-256 sidecars exist as a separate corpus, and a regression gate catches any bit-level drift
  2. A Python frequency response script measures the DAC model output, plots against the Phase 5 design target, and verifies passband ripple falls within documented tolerance
  3. C unit tests verify filter coefficient correctness, noise shaping spectral slope (+12dB/octave), toggle state transitions, and filter state reset on disable
  4. `docs/COVERAGE.md` is updated with DAC model test mappings showing every DAC requirement covered
**Plans**: TBD

## Progress

**Execution Order:**
Phases execute in numeric order: 5 -> 6 -> 7 -> 8 -> 9

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 5. Interpolation Filter Design | 1/1 | Complete | 2026-04-28 |
| 6. DAC Core Implementation | 2/2 | Complete | 2026-04-29 |
| 7. Pipeline Integration | 0/TBD | Not started | - |
| 8. I/O Surface | 0/TBD | Not started | - |
| 9. Verification + Documentation | 0/TBD | Not started | - |

## Completed Milestones

<details>
<summary>v1.1 ADPCM (Phases 1-4) -- SHIPPED 2026-04-27</summary>

Sony 4-bit ADPCM encode/decode: bit-faithful codec as toggleable reverb coloration stage. 23 requirements, 10 plans.

- [x] Phase 1: Core Codec (2/2 plans) -- completed 2026-04-26
- [x] Phase 2: Pipeline Integration (2/2 plans) -- completed 2026-04-27
- [x] Phase 3: I/O Layer (3/3 plans) -- completed 2026-04-27
- [x] Phase 4: Verification + Documentation (3/3 plans) -- completed 2026-04-27

</details>

<details>
<summary>v1.0 Product (8 phases / 37 plans) -- SHIPPED 2026-04-26</summary>

M1 reverb core + standalone JUCE GUI. Archived to `.planning/milestones/v1.0-product-ROADMAP.md`.

</details>

<details>
<summary>M1 Reverb Core (7 phases / 33 plans) -- SHIPPED 2026-04-25</summary>

49 requirements validated. Archived to `.planning/milestones/v1.0-ROADMAP.md`.

</details>

---

## Previous Milestone Archives

- `.planning/milestones/v1.1-ROADMAP.md` -- Full M2 ADPCM roadmap
- `.planning/milestones/v1.1-REQUIREMENTS.md` -- 23 requirements (all complete)
- `.planning/milestones/v1.0-product-ROADMAP.md` -- v1.0 product roadmap
- `.planning/milestones/v1.0-ROADMAP.md` -- M1 reverb core roadmap
- `.planning/milestones/v1.0-REQUIREMENTS.md` -- M1 requirements

---
*Last updated: 2026-04-29 -- Phase 6 complete (2/2 plans)*
