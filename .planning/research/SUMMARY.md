# Project Research Summary — v1.3: True 8x Oversampled DAC

**Project:** SPU-94
**Domain:** True 8x oversampling of AK4309 DAC interpolation filter -- replacing the v1.2 44.1kHz passband-equivalent approximation with genuine multirate signal processing
**Researched:** 2026-04-30
**Confidence:** HIGH

## Executive Summary

SPU-94 v1.3 replaces the v1.2 DAC FIR approximation with the real operation: zero-stuff the 44.1kHz signal, run three cascaded half-band stages at their true operating rates (88.2 / 176.4 / 352.8 kHz), inject delta-sigma noise at 352.8kHz, and decimate back to 44.1kHz. The v1.2 approximation applied all three stages at 44.1kHz to reproduce the AK4309's passband ripple character cheaply. v1.3 captures what v1.2 misses: inter-sample interpolation behavior, correct noise spectral shaping through decimation, and proper image rejection -- the actual signal processing the AK4309 performs. The existing Q15 coefficients in `spu94_dac_fir_coef.c` are already designed for their true operating rates and require zero modification.

The recommended approach is a three-stage polyphase cascade reusing the existing folded-form `dac_fir_stage_apply` function for Phase 0 of each stage, with a trivial single-multiply Phase 1 (center tap passthrough). This follows the same polyphase half-band pattern already proven in `spu94_fir.c` for the reverb interpolator. The polyphase form costs approximately 42 multiplies per sample (versus 22 in v1.2), which is sub-microsecond on any modern CPU. No new dependencies, no new source files, approximately 200 lines of new/changed C code. State struct size actually decreases by 140 bytes because polyphase delay lines store only input-rate samples.

The key risks are: accumulator overflow in polyphase sub-filters (1.04dB headroom in v1.2 needs re-derivation for each polyphase branch -- analysis shows it improves to 5.4dB), golden file mass invalidation (55 DAC-specific goldens plus an unknown number of pipeline goldens will change -- requires archiving v1.2 goldens and a delta characterization test as the regression gate), and the noise model rate question (noise at 352.8kHz needs amplitude recalibration since the decimation changes in-band spectral balance). The central open question -- whether true oversampling is audibly different from the v1.2 approximation -- is itself a valuable deliverable. Either answer is good: "no" validates v1.2's shortcut; "yes" justifies the work.

## Key Findings

### Recommended Stack

No changes to the existing stack. Zero new dependencies.

**Core technologies (all unchanged from v1.2):**
- C99/C11 (gcc 14+): existing compiler and flags, Q15 fixed-point arithmetic
- scipy 1.17.1 + numpy 2.2.4: `--verify-8x` mode added to `dac_filter_design.py`, no new APIs needed
- pytest + Unity: new test cases in existing test files, same harness
- CMake: no new source files, only modifications to existing ones

### Expected Features

**Must have (table stakes):**
- Zero-stuff to 352.8kHz + three-stage interpolation at true operating rates
- Noise model at 352.8kHz (8 LFSR steps per output sample)
- Decimation back to 44.1kHz (pick every 8th sample, no separate decimation filter)
- Audio-band frequency response identical to v1.2 within 0.01dB (same coefficients)
- Golden file regression (new goldens for true-oversampled output)
- Latency reporting updated for true multirate group delay (~30 samples vs v1.2's 35)
- Real-time safety preserved (no heap, no locks, stack-local intermediates)
- All existing toggles and API surfaces unchanged

**Should have (differentiators):**
- A/B comparison mode (`SPU94_DAC_APPROX` vs `SPU94_DAC_TRUE_8X`) -- keep v1.2 path selectable
- Characterization script (v1.2 vs v1.3 frequency response, impulse response, noise floor comparison plots)
- ADR: "Does true oversampling matter?" -- honest assessment with measurements

**Defer:**
- Polyphase efficiency optimization (compute only the 1 needed output of 8) -- measure naive cost first, optimize only if needed
- Analog post-filter modeling (SCF + CTF) -- separate milestone, requires hardware measurements
- Variable oversampling rates (2x/4x/8x) -- AK4309 is fixed 8x, period
- Sigma-delta 1-bit modulator at MCLK rate -- enormously complex, negligible audible benefit over noise model

### Architecture Approach

The polyphase cascade replaces `spu94_dac_fir_step` with `spu94_dac_fir_upsample8x` (or `spu94_dac_fir_step_8x`), producing 8 samples at 352.8kHz per input sample. Integration in `spu94_process.c` changes the DAC section to: upsample -> inject noise at 352.8kHz -> decimate. Stack-local `int16_t os[8]` buffers are transient per call. The v1.2 path is preserved behind a mode toggle for A/B comparison. No new files are created; all changes are to existing files.

**Major components:**
1. `spu94_dac_fir.c` -- polyphase 8x interpolation cascade (major rewrite, ~30-40 new LOC)
2. `spu94_process.c` -- DAC section orchestrates upsample -> noise -> decimate (~15 LOC)
3. `spu94_dac_noise.c` -- unchanged logic, called 8x per sample; `DAC_NOISE_SHIFT` retuned
4. `spu94_state_internal.h` -- new polyphase delay line dimensions (net -140 bytes)
5. `tools/dac_filter_design.py` -- `--verify-8x` mode for validation (~60 LOC)

### Critical Pitfalls

1. **Accumulator overflow in polyphase sub-filters (C1)** -- v1.2 has only 1.04dB headroom. Polyphase decomposition changes which coefficients contribute to each branch. Re-derive proofs for each sub-filter. Analysis shows non-trivial branch is safer (5.4dB headroom) because center tap is separated. Write proofs as comment blocks; extend overflow test with adversarial polyphase inputs.

2. **Golden file mass invalidation (C3)** -- 55+ DAC goldens will produce different SHA-256s. Archive v1.2 goldens before any code changes. Write a delta characterization test (RMS/peak/spectral difference between v1.2 and v1.3 modes) as the regression gate during transition. Assert all DAC-off goldens are bit-identical to prove isolation.

3. **Cascade order and rate assignment (C4)** -- stages must run at their designed rates (88.2 / 176.4 / 352.8 kHz) in order. The v1.2 code obscures rate structure by running everything at 44.1kHz. Explicitly label each stage's operating rate. Verify with frequency-domain sweep test (no content above 22.05kHz exceeds -41dB).

4. **Noise aliasing through decimation (C5)** -- HP-shaped noise at 352.8kHz will have most energy above 22.05kHz; decimation folds it back. The `DAC_NOISE_SHIFT` constant needs recalibration (approximately 9dB adjustment). Measure in-band noise floor empirically; adjust amplitude to maintain -90dB target.

5. **Composite filter shortcut temptation (M6)** -- a single composite FIR is mathematically equivalent but loses inter-stage Q15 truncation behavior that is part of the AK4309's sonic character. Implement as true three-stage cascade with int16 truncation at each stage boundary.

## Implications for Roadmap

Based on research, suggested phase structure:

### Phase 1: Polyphase FIR Cascade

**Rationale:** The core DSP change. Everything else depends on having a working 8x interpolation cascade. Must happen first. Archive v1.2 goldens before any code changes.
**Delivers:** `spu94_dac_fir_upsample8x` (or `_step_8x`) producing 8 samples at 352.8kHz per input. Polyphase delay line state struct. Accumulator overflow proofs for each sub-filter branch. Per-stage unit tests (phase 0 matches v1.2 stage-apply, phase 1 center-tap correctness). Frequency response verification against `dac_filter_design.py --verify`.
**Addresses:** Zero-stuff + three-stage interpolation (table stakes), coefficient reuse validation.
**Avoids:** C1 (overflow -- proofs first), C2 (implementation rewrite -- keep v1.2 function alongside), C4 (cascade order -- explicit rate labeling), M6 (composite shortcut -- three-stage cascade enforced).

### Phase 2: Noise Recalibration + Integration

**Rationale:** Depends on Phase 1 (needs working cascade to integrate with). Wires the new cascade into `spu94_process.c` and recalibrates the noise model for 352.8kHz operation.
**Delivers:** DAC section in `spu94_process.c` rewritten (upsample -> noise -> decimate). `DAC_NOISE_SHIFT` retuned via scipy calibration. Noise spectral shape verified at 352.8kHz. Integration test (full chain: reverb + ADPCM + DAC). Mode toggle (`dac_oversampled` or mode enum) for v1.2/v1.3 selection.
**Addresses:** Noise at elevated rate (table stakes), A/B comparison mode (differentiator), latency reporting update.
**Avoids:** C5 (noise aliasing -- empirical calibration), M4 (latency -- measured via impulse response), M5 (v1.2 preservation -- mode toggle).

### Phase 3: Verification + Characterization

**Rationale:** Depends on Phase 2 (needs complete integrated pipeline). Proves correctness, generates new goldens, and answers the central question: does it matter?
**Delivers:** New DAC-enabled golden files. DAC-off identity assertion (bit-identical). Delta characterization script (v1.2 vs v1.3 comparison plots). `--verify-8x` mode in `dac_filter_design.py`. ADR: "Does true oversampling matter?" with measurements. CLI/Python/JUCE surface updates (expose mode toggle). Coverage map update.
**Addresses:** Golden file regression, characterization script, ADR (differentiators), I/O surface updates.
**Avoids:** C3 (golden transition -- delta test as regression gate, DAC-off identity proven).

### Phase Ordering Rationale

- Linear dependency chain: cascade -> integration -> verification. No parallelism in the core DSP work.
- Polyphase FIR first because it is the riskiest (accumulator proofs, phase tracking) and everything else consumes its output.
- Noise recalibration bundled with integration because the noise amplitude constant needs the full pipeline to measure against.
- Verification last because it requires all features present and is the capstone that documents whether the work was worth it.
- The v1.2 path preservation (M5) is addressed in Phase 2 via mode toggle, not deferred -- A/B comparison is essential for the characterization in Phase 3.

### Research Flags

Phases likely needing deeper research during planning:
- **Phase 1:** Polyphase delay line indexing and phase tracking across the three-stage cascade need implementation-level analysis. The PITFALLS research (M3) flags this as "genuinely complex." The recommended path: implement naive 8x first for correctness verification, then optimize to polyphase. Alternatively, resolve the polyphase phase-tracking design in a phase-specific research step.
- **Phase 2:** Noise amplitude recalibration is an empirical task (scipy simulation). The math is straightforward but the exact `DAC_NOISE_SHIFT` value needs measurement.

Phases with standard patterns (skip research):
- **Phase 3:** Golden file generation, characterization scripting, and ADR writing follow established project patterns. No research needed.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Zero new dependencies; all operations are existing Q15 MAC + LFSR |
| Features | HIGH | Well-bounded scope; clear table stakes vs differentiators vs anti-features |
| Architecture | HIGH | Polyphase half-band pattern already proven in codebase (`spu94_fir.c`); signal flow well-understood |
| Pitfalls | HIGH | All pitfalls are structural consequences of the existing code, verified by codebase analysis |

**Overall confidence:** HIGH

### Gaps to Address

- **Noise amplitude recalibration:** Exact `DAC_NOISE_SHIFT` value for 352.8kHz operation needs empirical measurement (scipy simulation during Phase 2). Theory predicts ~9dB adjustment; actual value depends on decimation interaction.
- **Polyphase phase tracking across three stages:** The "only compute what you need" optimization is complex (M3, M6). Recommendation: start with naive 8x (compute all 8 outputs), verify correctness against Python reference, then optimize. The naive approach costs 70 multiplies -- still 113x under real-time budget.
- **Audible difference magnitude:** Whether v1.3 sounds different from v1.2 is unknown until measured. The characterization script (Phase 3) answers this. Either outcome is valuable.
- **Decimation sample selection:** Which of the 8 output samples to keep (index 0 vs index 7) affects group delay by 2.8us. STACK.md recommends index 7 (last computed); ARCHITECTURE.md recommends index 0 (phase-aligned with input). Resolve with impulse response test in Phase 1.

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_dac_fir.c` -- v1.2 implementation, accumulator width proofs
- `src/spu94/spu94_dac_fir_coef.c` -- Q15 coefficients, DC gain, pair tables
- `src/spu94/spu94_process.c` -- DAC integration point, signal flow
- `src/spu94/spu94_state_internal.h` -- state layout, size ceiling
- `tools/dac_filter_design.py` -- coefficient design, composite verification at 352.8kHz
- `src/spu94/spu94_fir.c` -- existing polyphase half-band precedent

### Secondary (MEDIUM-HIGH confidence)
- `.planning/milestones/v1.2-phases/05-interpolation-filter-design/05-RESEARCH.md` -- v1.2 design rationale
- `.planning/research/DEEP-AK4309-FAMILY.md` -- AK4309 datasheet extraction
- `docs/DECISIONS.md` ADR-Phase-6 series -- v1.2 DAC design decisions

### Tertiary (MEDIUM confidence)
- AK4309B datasheet (AllDatasheet) -- 8x FIR interpolator specs
- Archimago PS1 SCPH-5501 measurements -- real hardware noise floor reference
- DSP textbooks: Vaidyanathan "Multirate Systems", Crochiere & Rabiner "Multirate DSP"

---
*Research completed: 2026-04-30*
*Ready for requirements: yes*
