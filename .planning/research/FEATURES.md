# Feature Landscape: True 8x Oversampled DAC (v1.3)

**Domain:** Replacing the 44.1kHz FIR approximation with genuine multirate 8x oversampling in SPU-94's AK4309 DAC model
**Researched:** 2026-04-30
**Confidence:** HIGH on DSP fundamentals of multirate vs single-rate. HIGH on what the current v1.2 code does (read from source). MEDIUM on audible magnitude of differences (theoretical + measurement-informed, no direct A/B test yet).

---

## Context: What v1.2 Does and Why It Is an Approximation

The v1.2 DAC FIR (`spu94_dac_fir.c`) runs all three cascaded half-band stages at **44.1 kHz on every call**. The source comment (line 8-10) is explicit: "All three stages operate at 44.1 kHz on every call (Pitfall 5 -- NOT at increasing rates). The cascade reproduces the passband ripple character at the audio rate; the upsampling is already accounted for in the coefficient design (Phase 5)."

This means v1.2 applies the correct *frequency response envelope* (passband ripple, stopband attenuation) but does NOT perform the actual multirate signal processing that the AK4309 performs internally. The coefficients were designed for each stage's respective operating rate (88.2, 176.4, 352.8 kHz) using scipy.signal.remez, then the resulting frequency response is reproduced at 44.1 kHz via a single-rate cascade.

### What the Approximation Gets Right
- Passband ripple magnitude and distribution (the gentle wobble above 3 kHz)
- Overall frequency response shape within the audio band (0-22 kHz)
- Computational cost (22 multiplies per sample at 44.1 kHz)

### What the Approximation Gets Wrong
- **No inter-sample behavior**: True oversampling generates 7 interpolated samples between each input sample. These intermediate values interact with the noise model and any downstream nonlinearity. The v1.2 approach skips this entirely.
- **Noise model operates at the wrong rate**: The LFSR + HP-shaped noise in `spu94_dac_noise.c` runs at 44.1 kHz. In the real AK4309, the delta-sigma modulator and its noise shaping operate at 352.8 kHz (or higher). The spectral shape of the noise at 44.1 kHz after decimation differs from noise generated directly at 44.1 kHz.
- **No image rejection characterization**: The real cascade produces and suppresses spectral images at multiples of 44.1 kHz during interpolation. These images interact with the noise shaping. In v1.2, no images exist because no upsampling happens.
- **Decimation filter behavior absent**: After processing at 352.8 kHz, the signal must be decimated back to 44.1 kHz. The decimation anti-alias filter adds its own time-domain characteristics (settling behavior on transients).

---

## Table Stakes

Features that MUST be present for the "true oversampled DAC" claim to be honest. Missing any of these means the milestone is just a v1.2 filter rewrite.

| Feature | Why Expected | Complexity | Dependencies | Notes |
|---------|--------------|------------|--------------|-------|
| Zero-stuff to 352.8 kHz | This IS oversampling -- insert 7 zeros between each input sample. Without it, the milestone name is false. | LOW | New internal buffer at 8x rate in state struct | 7 zeros + 1 original sample per input. Creates spectral images that the interpolation filter must suppress. |
| Three-stage interpolation at true operating rates | Stage 1 at 88.2 kHz, Stage 2 at 176.4 kHz, Stage 3 at 352.8 kHz. Each stage doubles the rate and removes the image from the previous upsample. | MEDIUM | Zero-stuff buffer; per-stage delay lines at correct dimensions | Same coefficients from v1.2 reused (they were designed for these rates). The change is running them at their designed rates instead of all at 44.1 kHz. |
| Decimation back to 44.1 kHz | SPU-94's output contract is 44.1 kHz int16 stereo. The 352.8 kHz internal signal must be downsampled. | MEDIUM | Decimation anti-alias filter (or reuse of interpolation filters in reverse) | Correct approach: the interpolation filters already suppress all images above 22.05 kHz. Pick every 8th sample after the full cascade. No separate decimation filter needed if interpolation is done right. |
| Noise model at 352.8 kHz | Delta-sigma noise shaping should run at the elevated rate so decimation produces the correct spectral shape in the audio band. | MEDIUM | Noise generator ticking 8x per output sample; same LFSR + HP shaping | Currently runs at 44.1 kHz. At 352.8 kHz, each output sample integrates 8 noise ticks through the decimation process. The spectral balance after decimation differs from single-rate noise. |
| Identical frequency response to v1.2 in the audio band | Passband ripple, stopband rejection, response at 20 kHz must match v1.2 (same coefficients, same datasheet specs). True oversampling should not change what the filter does to audio-band content. | LOW (by design) | Same coefficient tables from `spu94_dac_fir_coef.c` | Regression test: v1.3 frequency response at 0-22 kHz matches v1.2 within 0.01 dB. |
| DAC on/off toggle and sub-toggles unchanged | `spu94_set_dac_enabled()`, `spu94_set_dac_fir_enabled()`, `spu94_set_dac_noise_enabled()` work identically. | LOW | API compatibility -- no signature changes | Internal implementation changes only. |
| Latency reporting updated | True oversampling changes the DAC stage's group delay. `spu94_get_total_latency_samples` must report the correct value. | LOW | Compute new group delay from multirate cascade | v1.2 single-rate group delay = (55-1)/2 + (11-1)/2 + (7-1)/2 = 35 samples at 44.1 kHz. True multirate: each stage's delay is measured at its own rate and converted to 44.1 kHz equivalent. The equivalent delay will be different. |
| Golden file regression | New golden files for true-oversampled DAC output. v1.2 DAC goldens archived as reference. | LOW | Existing test infrastructure | Output WILL differ from v1.2. That is the point. New goldens needed. |
| Real-time safety preserved | No heap, no locks, no syscalls. The 8x internal buffer must be stack or struct-embedded. | LOW (constraint) | State struct expansion | 8x buffer is tiny: 2 channels x 8 samples x 2 bytes = 32 bytes per call. Trivially struct-embedded. |
| CLI/Python/JUCE surfaces unchanged | `--dac`, `set_dac_enabled()`, JUCE toggle all work identically from user's perspective. | LOW | No API change | The change is invisible to users. Output sounds different (more faithful), but controls are identical. |

---

## Differentiators

Features that make v1.3 more than a correctness fix -- things that add genuine value.

| Feature | Value Proposition | Complexity | Dependencies | Notes |
|---------|-------------------|------------|--------------|-------|
| A/B comparison mode (v1.2 approx vs v1.3 true) | Lets Anthony hear the difference. Educational. Validates whether the engineering effort is audibly worthwhile. | MEDIUM | Keep v1.2 single-rate path as a selectable mode | Two modes: `SPU94_DAC_MODE_APPROX` (v1.2 behavior) and `SPU94_DAC_MODE_TRUE` (v1.3 oversampled). Default to TRUE. |
| Characterization script (v1.2 vs v1.3) | Python script processing test signals through both modes. Produces frequency response, impulse response, noise floor, and time-domain comparison plots. Quantifies the actual difference. | LOW | Both DAC modes accessible from Python | Builds on existing `tools/dac_measure.py`. Produces evidence for the "does it matter?" ADR. |
| ADR: "Does true oversampling matter?" | Honest assessment with measurements of whether v1.3 is audibly different from v1.2, and in what way. The intellectually honest capstone. | LOW | Characterization script results | If the answer is "no audible difference," that validates v1.2 and is a valuable documented finding. If "yes," it justifies v1.3. Either outcome is good. |
| Noise spectrum verification at 352.8 kHz | Verify that noise at 352.8 kHz, after decimation, produces correct +12 dB/octave slope with correct level (~-90 dB RMS) in the audio band. | MEDIUM | Noise at elevated rate + decimation + spectral analysis | The noise spectrum after decimation may differ from v1.2's direct 44.1 kHz noise. This is the most audibly interesting part. |
| Polyphase efficient implementation | Instead of literally zero-stuffing and running 8x, use polyphase decomposition to compute only needed output samples. Identical output at ~1x cost. | HIGH | Polyphase restructuring of all three stages | Defer unless naive 8x approach is too expensive for real-time. Measure first. |

---

## Anti-Features

Features to explicitly NOT build. These are scope traps.

| Anti-Feature | Why It Seems Related | Why It Is Wrong for v1.3 | What to Do Instead |
|--------------|---------------------|--------------------------|-------------------|
| Analog post-filter modeling (SCF + CTF) | "While redoing the DAC, also add the analog filter" | v1.3 is about fixing the digital interpolation. The analog filter is a separate problem requiring hardware measurements. | Defer to a future milestone. Keep the boundary clean: digital interpolation only. |
| Variable oversampling rate (2x/4x/8x) | Creative flexibility | The AK4309 is fixed at 8x. Variable rates turn SPU-94 into a generic resampler, not a PS1 model. | Fixed 8x. Period. |
| Higher-quality interpolation (more taps) | "Better" oversampling | The point is to match the AK4309, not exceed it. Over-designing destroys the coloration character. | Reuse exact 55+11+7 coefficients from v1.2. |
| Sigma-delta 1-bit modulator simulation | The AK4309 internally converts to 1-bit PDM at MCLK rate | Simulating the 1-bit modulator at 11.3 MHz is enormously complex with negligible audible benefit over the noise model approximation. | LFSR + HP shaping noise model at 352.8 kHz is the right abstraction level. |
| Output at 352.8 kHz (skip decimation) | "Let the host DAC handle it" | SPU-94's contract is 44.1 kHz int16 stereo. Changing the output rate breaks every consumer. | Always decimate back to 44.1 kHz. |
| Redesigning the filter coefficients | "New oversampling mode deserves new filters" | The v1.2 coefficients were designed FOR their correct operating rates. They are correct as-is. Redesigning wastes time and risks regressions. | Reuse v1.2 coefficients verbatim from `spu94_dac_fir_coef.c`. |
| DAC sub-stage independent rate selection | "Run FIR at 8x but noise at 44.1 kHz" | Combinatorial testing surface with minimal user value. | Both FIR and noise run at 352.8 kHz when DAC is enabled. Unified rate. |
| Coefficient redesign for "optimal decimation" | Separate decimation filter with different specs | The interpolation filter already suppresses images. A separate decimation filter adds complexity for no benefit when using pick-every-8th. | Use the interpolation filter's output directly. Pick every 8th sample. |

---

## Feature Dependencies

```
[Zero-stuff to 352.8 kHz]
    enables -> [Three-stage interpolation at true rates]
    enables -> [Noise model at elevated rate]

[Three-stage interpolation at true rates]
    requires -> [Zero-stuff to 352.8 kHz]
    reuses  -> [v1.2 FIR coefficients (spu94_dac_fir_coef.c)]
    enables -> [Decimation back to 44.1 kHz]

[Noise model at elevated rate]
    requires -> [8x processing loop]
    reuses  -> [v1.2 noise model (spu94_dac_noise.c)]
    produces -> [different noise spectrum after decimation]

[Decimation back to 44.1 kHz]
    requires -> [Interpolation complete at 352.8 kHz]
    requires -> [Noise added at 352.8 kHz]
    produces -> [44.1 kHz output for downstream consumers]

[Golden file regression]
    requires -> [Decimation complete]
    invalidates -> [v1.2 DAC golden files]

[Frequency response regression]
    requires -> [Decimation complete]
    validates -> [Audio-band response matches v1.2]

[A/B comparison mode]
    requires -> [v1.2 single-rate path preserved]
    requires -> [v1.3 multirate path complete]

[Characterization script]
    requires -> [A/B comparison mode]
    produces -> [Evidence for "does it matter?" ADR]

[Latency reporting]
    requires -> [True multirate group delay computed]
    updates -> [spu94_get_total_latency_samples]
```

### Critical Path

Zero-stuff -> interpolation at true rates -> noise at 352.8 kHz -> decimation -> golden files -> characterization -> ADR. This is a linear dependency chain with no parallelism in the core DSP work. The A/B mode is a branch off the main path that requires preserving the v1.2 code path.

---

## Expected Audible Differences

The central question of this milestone. Based on DSP theory, the v1.2 source code, and what the AK4309 actually does:

### Likely Audible (MEDIUM confidence)

1. **Noise floor texture in reverb tails**: Noise at 352.8 kHz decimated to 44.1 kHz produces different spectral balance than noise generated at 44.1 kHz. The decimation filter low-passes the noise, changing its high-frequency content. For reverb tails decaying into the noise floor, this is the most likely audible difference. Magnitude: subtle, probably measurable, possibly inaudible in context.

2. **Transient response on sharp attacks**: True multirate interpolation generates intermediate sample values that settle differently through the cascade than the single-rate approximation. For impulsive signals (percussion, transient attacks), the interpolated path's settling behavior differs. Magnitude: likely only perceptible on isolated transients, not on mixed material.

### Likely Inaudible (HIGH confidence)

3. **Steady-state frequency response**: Identical coefficients produce identical passband ripple and stopband rejection. A swept sine through both modes should be nearly indistinguishable in the 0-20 kHz band.

4. **Phase response in audio band**: Same linear-phase FIR coefficients, same phase. The multirate path does not alter the phase response within the audio band.

### Unknown Until Measured

5. **Signal-noise interaction at different rates**: Noise added at 352.8 kHz to an interpolated signal, then decimated, may differ subtly from noise added at 44.1 kHz to a filtered signal. The interaction between signal-dependent content and noise at different rates is a second-order effect whose magnitude needs empirical measurement.

### The Honest Assessment

The primary value of v1.3 is **correctness of process**, not necessarily audible improvement. The v1.2 approximation produces the right frequency response but skips the actual multirate signal processing. Whether this matters audibly is an empirical question the characterization script will answer.

Either outcome is valuable:
- "No audible difference" validates the v1.2 shortcut and documents why it was acceptable
- "Audible difference" means v1.3 is the correct model going forward

The milestone is worth doing regardless because it replaces a known approximation with a known-correct implementation, produces a documented ADR answering the question, and exercises the codebase's ability to handle internal rate changes.

---

## MVP Recommendation

### Must Ship (core milestone)

1. Zero-stuff + three-stage interpolation at true rates
2. Noise model at 352.8 kHz
3. Decimation to 44.1 kHz
4. Golden file regression (new goldens)
5. Frequency response regression (audio-band equivalence to v1.2)
6. Latency reporting update

### Should Ship (completes the story)

7. A/B comparison mode (v1.2 approx vs v1.3 true)
8. Characterization script (quantifies the difference)
9. ADR: "Does true oversampling matter?"

### Defer

10. Polyphase efficient implementation -- optimize AFTER correctness is verified. The naive 8x approach costs 8x22 = 176 multiplies per output sample at 44.1 kHz. At Q15 int16 arithmetic, this is well under 1 microsecond on any modern CPU. Polyphase is unnecessary unless targeting MCU. Measure first, optimize only if needed.

---

## Complexity Budget

| Component | Estimated C LOC | Rationale |
|-----------|----------------|-----------|
| Oversampled DAC step (new function or refactor of `spu94_dac_fir_step`) | 60-100 | Zero-stuff + 3-stage cascade at true rates + pick-every-8th decimation. Reuses coefficient tables and delay line helpers. |
| Noise at elevated rate (modify/wrap `spu94_dac_noise_step`) | 20-40 | Run existing noise step 8x per output sample, add to interpolated signal before decimation. |
| State struct changes | 10-20 | Intermediate 8x buffer (8 samples per channel, 32 bytes total). Delay line dimensions unchanged (coefficients unchanged). |
| Integration in `spu94_process.c` | 10-20 | Replace `spu94_dac_fir_step` + `spu94_dac_noise_step` with oversampled equivalent. |
| A/B mode flag + API | 15-25 | `spu94_set_dac_oversampled()` or mode enum. Conditional dispatch in process loop. |
| Unit + integration tests | 100-200 | Oversampled FIR correctness, noise at elevated rate, decimation, frequency response regression, new golden files. |
| Characterization script (Python) | 80-150 | Extends `tools/dac_measure.py` with v1.2-vs-v1.3 comparison plots. |
| **Total C** | **~215-400** | Modest. The hard work (coefficient design, noise model design) was done in v1.2. |
| **Total Python** | **~80-150** | Characterization and measurement tooling. |

---

## Key Technical Decisions the Milestone Must Make

1. **Naive 8x vs polyphase**: Run the literal zero-stuff-and-filter at 352.8 kHz, or restructure as polyphase to compute only needed outputs? Recommendation: start naive, measure cost, optimize only if needed.

2. **Noise injection point**: Add noise at 352.8 kHz before decimation (faithful to AK4309 signal flow), or keep at 44.1 kHz (simpler, less faithful)? Recommendation: at 352.8 kHz. This is the whole point.

3. **Decimation strategy**: Pick-every-8th (relies on interpolation filter's image rejection) vs separate decimation filter? Recommendation: pick-every-8th. The interpolation filter already achieves 53.6 dB stopband attenuation, sufficient for alias-free decimation.

4. **v1.2 compatibility path**: Keep the old single-rate code as a selectable mode, or delete it? Recommendation: keep it behind a mode flag for A/B comparison. Low maintenance cost (existing code, no changes needed).

5. **State struct layout**: Expand existing `spu94_dac_fir_state` to hold 8x buffers, or create a new struct? Recommendation: new wrapper struct that contains the existing per-stage states plus a small intermediate buffer. Minimizes changes to existing code.

---

## Sources

- `src/spu94/spu94_dac_fir.c` -- v1.2 single-rate implementation (lines 8-10 confirm 44.1 kHz operation). HIGH confidence.
- `tools/dac_filter_design.py` -- confirms coefficients designed for 88.2/176.4/352.8 kHz. HIGH confidence.
- `.planning/milestones/v1.2-phases/05-interpolation-filter-design/05-RESEARCH.md` -- design rationale, cascade exploration, D-01 through D-13. HIGH confidence.
- `.planning/research/FEATURES-v1.2.md` -- prior DAC feature landscape. HIGH confidence.
- [Analog Devices MT-017: Oversampling Interpolating DACs](https://www.analog.com/media/en/training-seminars/tutorials/mt-017.pdf) -- standard reference on oversampling DAC architecture. HIGH confidence for theory.
- [Archimago PS1 SCPH-5501 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) -- measured PS1 DAC: ~15-bit dynamic range, passband deviation above 3 kHz. MEDIUM confidence.
- [Rick Lyons: Optimizing Half-Band Filters in Multistage Decimation and Interpolation](https://www.dsprelated.com/showarticle/903.php) -- cascaded half-band architecture. HIGH confidence for theory.
- [MathWorks: Oversampling Interpolating DAC](https://www.mathworks.com/help/msblks/ug/oversampling-interpolating-dac.html) -- multirate DAC signal flow. HIGH confidence.
- [Oversampling - Wikipedia](https://en.wikipedia.org/wiki/Oversampling) -- general reference on zero-stuffing and image rejection. MEDIUM confidence.
- [diyAudio: Interpolation in oversampling DACs](https://www.diyaudio.com/community/threads/what-type-of-interpolation-is-mostly-used-in-oversampling-upsampling-dacs.101349/) -- practical discussion of cascaded half-band stages. MEDIUM confidence.
- [AK4309B datasheet via AllDatasheet](https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html) -- "8 times FIR Interpolator", +/-0.05dB ripple, 41dB stopband. MEDIUM confidence (compatible family part).

---
*Feature research for: SPU-94 v1.3 True Oversampled DAC milestone*
*Researched: 2026-04-30*
