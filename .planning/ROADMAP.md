# Roadmap: SPU-94

**Updated:** 2026-05-01
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestones

- 🚧 **v1.3 True Oversampled DAC** — Phases 10-12 (in progress)
- ✅ **v1.2 DAC Modeling** — Phases 5-9 (shipped 2026-04-30, tag `v1.2`)
- ✅ **v1.1 ADPCM** — Phases 1-4 (shipped 2026-04-27, tag `v1.1`)
- ✅ **v1.0 Product** — 8 phases (shipped 2026-04-26, standalone GUI)
- ✅ **M1 Reverb Core** — 7 phases (shipped 2026-04-25, tag `m1-reverb-core`)

## Phases

<details>
<summary>v1.2 DAC Modeling (Phases 5-9) -- SHIPPED 2026-04-30</summary>

AK4309 DAC interpolation filter + delta-sigma noise model as toggleable coloration stage. Send/return mixer architecture with 3 buses, 6 faders, latency compensation. 14 requirements, 12 plans.

- [x] Phase 5: Interpolation Filter Design (1/1 plans) -- completed 2026-04-28
- [x] Phase 6: DAC Core Implementation (2/2 plans) -- completed 2026-04-29
- [x] Phase 7: Pipeline Integration (3/3 plans) -- completed 2026-04-29
- [x] Phase 8: I/O Surface (3/3 plans) -- completed 2026-04-30
- [x] Phase 9: Verification + Documentation (3/3 plans) -- completed 2026-04-30

</details>

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

### v1.3 True Oversampled DAC (In Progress)

**Milestone Goal:** Replace the v1.2 44.1kHz FIR approximation with genuine 8x oversampling -- zero-stuff to 352.8kHz, run the AK4309 interpolation cascade at the real rate, decimate back to 44.1kHz.

- [x] **Phase 10: Core Polyphase FIR Cascade** - True 8x zero-stuff + interpolation at real operating rates, with decimation and DAC-off identity proof (2026-05-01)
- [ ] **Phase 11: Noise Recalibration + Integration** - Noise model at 352.8kHz, A/B mode toggle, latency update, surface compatibility
- [ ] **Phase 12: Verification + Characterization** - Characterization script, ADR documenting audible differences, new golden files

## Phase Details

### Phase 10: Core Polyphase FIR Cascade
**Goal**: The DAC interpolation filter runs at true operating rates (88.2 / 176.4 / 352.8 kHz) producing correct 8x oversampled output that decimates back to 44.1kHz
**Depends on**: Phase 9 (v1.2 DAC Modeling complete)
**Requirements**: DSP-01, DSP-02, DSP-03, DSP-04, DSP-06, DSP-08, INT-03, INT-04
**Success Criteria** (what must be TRUE):
  1. Processing a signal through the DAC-enabled path produces output with correct passband response (matches v1.2 within 0.05dB across 20Hz-20kHz; Q15 truncation budget)
  2. DAC-off golden files are bit-identical before and after the changes (zero blast radius on non-DAC paths)
  3. Each FIR stage runs at its designed rate (Stage 1 at 88.2kHz, Stage 2 at 176.4kHz, Stage 3 at 352.8kHz) with inter-stage Q15 truncation preserved
  4. Real-time safety gates pass -- no heap, no locks, no syscalls in the 8x processing path
  5. v1.2 DAC golden files are archived before any code changes
**Plans:** 4 plans

Plans:
- [x] 10-01-PLAN.md — Scipy 8x prototype + archive v1.2 DAC goldens
- [x] 10-02-PLAN.md — Implement spu94_dac_fir_step_8x in C + unit tests
- [x] 10-03-PLAN.md — Wire 8x into pipeline + prove zero blast radius (INT-03)
- [x] 10-04-PLAN.md — Regenerate DAC goldens + passband conformance (INT-04)

### Phase 11: Noise Recalibration + Integration
**Goal**: The complete DAC pipeline works end-to-end with noise at the correct rate and amplitude, with a mode toggle for v1.2/v1.3 comparison
**Depends on**: Phase 10
**Requirements**: DSP-05, DSP-07, CMP-01, INT-01
**Success Criteria** (what must be TRUE):
  1. LFSR noise model runs 8 ticks per output sample at 352.8kHz with in-band noise floor at -90dB target
  2. A/B mode toggle selects between v1.2 (approx) and v1.3 (true) DAC paths, accessible from C API
  3. `spu94_get_total_latency_samples` reports correct group delay for the true oversampled path
  4. CLI `--dac`, Python `set_dac_enabled()`, and JUCE DAC toggle work identically to v1.2 -- no surface breakage
**Plans:** 2 plans

Plans:
**Wave 1**
- [x] 11-01-PLAN.md — Toggle API + noise shift constant + combined FIR+noise function

**Wave 2** *(blocked on Wave 1 completion)*
- [ ] 11-02-PLAN.md — Pipeline dispatch + latency update + integration tests + listen gate

### Phase 12: Verification + Characterization
**Goal**: The true oversampled DAC is proven correct with new golden files, and the central question -- does it sound different from v1.2? -- is answered with measurements
**Depends on**: Phase 11
**Requirements**: CMP-02, CMP-03, INT-02
**Success Criteria** (what must be TRUE):
  1. New v1.3 DAC golden files are generated with SHA-256 sidecars and pass regression gate
  2. Python characterization script produces comparison plots (frequency response, impulse response, noise floor, time-domain) between v1.2 and v1.3 modes
  3. ADR documents whether true oversampling produces audible differences, with measurement evidence supporting the conclusion
**Plans:** 2 plans

Plans:
**Wave 1**
- [x] 12-01-PLAN.md — Surface toggle (CLI + Python) + regenerate v1.3 DAC golden files

**Wave 2** *(blocked on Wave 1 completion)*
- [ ] 12-02-PLAN.md — v1.2 vs v1.3 characterization script + ADR-0055

## Progress

**Execution Order:**
Phases execute in numeric order: 10 -> 11 -> 12

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 10. Core Polyphase FIR Cascade | v1.3 | 4/4 | Complete | 2026-05-01 |
| 11. Noise Recalibration + Integration | v1.3 | 1/2 | Executing | - |
| 12. Verification + Characterization | v1.3 | 1/2 | Executing | - |

---

## Previous Milestone Archives

- `.planning/milestones/v1.2-ROADMAP.md` -- Full v1.2 DAC Modeling roadmap
- `.planning/milestones/v1.2-REQUIREMENTS.md` -- 14 requirements (all complete)
- `.planning/milestones/v1.1-ROADMAP.md` -- Full M2 ADPCM roadmap
- `.planning/milestones/v1.1-REQUIREMENTS.md` -- 23 requirements (all complete)
- `.planning/milestones/v1.0-product-ROADMAP.md` -- v1.0 product roadmap
- `.planning/milestones/v1.0-ROADMAP.md` -- M1 reverb core roadmap
- `.planning/milestones/v1.0-REQUIREMENTS.md` -- M1 requirements

---
*Last updated: 2026-05-01 -- Phase 12 planned (2 plans)*
