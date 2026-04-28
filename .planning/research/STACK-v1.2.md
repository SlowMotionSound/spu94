# Stack Research — SPU-94 v1.2 DAC Modeling

**Domain:** DAC conversion modeling as toggleable coloration stage in an existing C99 fixed-point DSP library.
**Researched:** 2026-04-28
**Confidence:** MEDIUM-HIGH — hardware identification is solid; modeling approach is well-grounded in DSP fundamentals; exact AK4309AVM internal filter coefficients are unavailable (datasheet lost to time).

**Scope note:** This document covers only the stack additions/changes needed for v1.2 DAC modeling. The existing v1.0/v1.1 stack (C11, CMake, pytest, ctypes, dr_wav, etc.) is validated and unchanged. See the original `STACK.md` commit history for those decisions.

---

## 1. PS1 DAC Hardware Identification

### The Chip: AKM AK4309AVM

**Confidence: HIGH** — multiple independent sources confirm.

| Attribute | Value | Source |
|-----------|-------|--------|
| Manufacturer | Asahi Kasei Microsystems (AKM) | psx-spx pinouts, dogbreath.de, Stereophile |
| Part number (early boards) | AK4309VM / AK4309AVM | psx-spx, emu-russia/psxrev |
| Part number (SCPH-55xx) | AK4309AVM | dogbreath.de |
| Part number (SCPH-700x) | AK4309BM (20-pin, not pin-compatible) | dogbreath.de |
| Part number (SCPH-75xx+) | Integrated into SPU SoC (CXD2938Q etc.) | psx-spx pinouts |
| Topology | 1-bit delta-sigma ("bitstream") DAC | AK4309B datasheet, multiple audiophile sources |
| Resolution | 16-bit input, 1-bit internal conversion | Datasheet, psx-spx |
| Dynamic range | 90 dB (rated) | AK4309B datasheet |
| THD+N | -84 dB | AK4309B datasheet |
| Digital filter | 8x FIR interpolator | AK4309B datasheet |
| Post-filter | 2nd-order SCF (switched-capacitor filter) + continuous-time filter (CTF) | AK4309B datasheet |
| Passband response | +/-0.5 dB at 20 kHz | AK4309B datasheet |
| Sampling rate range | 8 kHz - 50 kHz | AK4309B datasheet |
| Master clock | 256fs or 384fs | AK4309B datasheet |
| Output level | 3.4 Vpp (single-ended) | AK4309B datasheet |
| Power | 5V +/-10%, 80 mW | AK4309B datasheet |
| Package | AK4309AVM: 24-pin SSOP; AK4309BM: 20-pin SSOP | dogbreath.de, datasheet |

### Interface to SPU

The SPU sends audio to the DAC over a 3-wire serial interface:

| Signal | Pin | Purpose |
|--------|-----|---------|
| LRCK | 10 | Left/Right clock — 44.1 kHz word clock |
| BICK | 8 | Bit clock — serial data clock |
| SDATA | 9 | Serial data — 16-bit PCM, MSB first |
| MCLK | 6 | Master clock — 256fs or 384fs (11.2896 MHz or 16.9344 MHz at 44.1 kHz) |

The SPU outputs **16-bit signed PCM at 44.1 kHz** in serial form. The DAC receives this directly — there is no additional digital processing between the SPU's final mix bus and the DAC input.

### Datasheet Caveat

The AK4309AVM datasheet is no longer publicly available (AKM has discontinued the part and removed it from their website). The AK4309B datasheet IS available and provides the specs above, but dogbreath.de notes the two are "not exactly compatible" (different pin count, different package). The core converter topology (1-bit delta-sigma with 8x interpolation + SCF) is shared across the AK4309 family. The exact digital filter coefficients for the AK4309AVM are unknown.

**Confidence: HIGH** for topology and architecture. **MEDIUM** for exact filter coefficients and internal analog characteristics. **LOW** for AK4309AVM-specific deviations from AK4309B specs.

---

## 2. What the DAC Actually Does (Modeling Targets)

The AK4309 signal chain, from input to analog output, contains these stages:

```
16-bit PCM input (44.1 kHz)
    |
    v
[8x Digital Interpolation Filter] -- 8x FIR upsampling to 352.8 kHz
    |
    v
[Delta-Sigma Modulator] -- converts multibit to 1-bit PDM stream
    |
    v
[2nd-order SCF] -- switched-capacitor reconstruction filter
    |
    v
[Continuous-Time Filter (CTF)] -- analog smoothing
    |
    v
Analog output (AOUTL / AOUTR)
```

### Which stages produce audible artifacts worth modeling?

| Stage | Artifact | Audibility | Model in v1.2? |
|-------|----------|------------|-----------------|
| 8x digital interpolation | Passband ripple (+/-0.5 dB), stopband rejection, transition-band rolloff | Subtle high-frequency coloration | YES — this is the primary digital artifact |
| ZOH sinc droop | sinc(pi*f/fs) rolloff (-3.9 dB at Nyquist) | Compensated by the interpolation filter; residual droop is part of the filter's target response | NO — subsumed by the interpolation filter model |
| Delta-sigma quantization noise | Shaped to ultrasonic frequencies by noise-shaping loop | Inaudible in-band at 90 dB dynamic range; noise is above 22 kHz | NO — modeling sigma-delta at 1-bit resolution requires massive oversampling for negligible audible effect |
| SCF clock feedthrough | Artifacts at multiples of the switched-capacitor clock frequency | Above 100 kHz; completely inaudible | NO |
| CTF analog rolloff | Gentle anti-aliasing above audio band | Above 22 kHz; inaudible in digital domain | NO |
| DNL/INL nonlinearity | Low-level harmonic distortion, especially at low signal levels | Measurable but very small (-84 dB THD+N); characteristic of the specific DAC silicon | MAYBE — defer to post-measurement |
| 15-bit effective dynamic range | Noise floor ~6 dB higher than ideal 16-bit | Audible as slightly elevated noise floor vs. modern DACs | YES — simple to model as additive shaped noise if desired |

### Recommendation: Model the Digital Interpolation Filter

**Confidence: HIGH** that this is the right scope.

The 8x digital interpolation filter is the **dominant audible coloration** introduced by the AK4309. It defines:
- The passband ripple character (+/-0.5 dB)
- The transition band rolloff (how steeply high frequencies roll off above ~18 kHz)
- Image rejection in the oversampled domain

This is also the only stage that operates entirely in the digital domain and can be modeled deterministically in fixed-point C without requiring analog circuit simulation.

**The delta-sigma modulator, SCF, and CTF operate in the 1-bit/analog domain.** Modeling them faithfully would require running the signal chain at 352.8 kHz (8x oversampling) or higher, which is:
1. Computationally expensive (8x the sample count per block)
2. Unnecessary — the in-band artifacts are below the DAC's rated noise floor
3. Not reproducible without the actual AK4309AVM silicon measurements

---

## 3. Stack Additions for v1.2

### No new external dependencies needed.

The DAC model fits entirely within the existing stack. Here is what changes:

### New C Source Files

| File | Purpose | Estimated LOC |
|------|---------|---------------|
| `spu94_dac.c` | DAC model: interpolation filter + optional noise floor | 150-250 |
| `spu94_dac.h` | Public API for DAC stage enable/disable/configure | 30-50 |
| `spu94_dac_filter_coef.c` | Interpolation filter coefficients (like `spu94_fir_coef.c` pattern) | 20-40 |

### Mathematical Approach: FIR Interpolation Filter in Fixed-Point

The 8x interpolation filter is a polyphase FIR — a standard DSP structure. Implementation approach:

**Step 1: Design the interpolation filter.**

Since the exact AK4309AVM coefficients are lost, design a period-appropriate 8x interpolation FIR that meets the datasheet specs:
- Passband: 0 to 20 kHz, +/-0.5 dB ripple
- Stopband: begins at 24.1 kHz (44.1 kHz - 20 kHz), rejection > 70 dB
- Operating at 352.8 kHz (8 * 44.1 kHz)
- Filter length: likely 64-128 taps (typical for mid-90s delta-sigma DAC digital filters)

Use `scipy.signal.remez` (Parks-McClellan equiripple) or `scipy.signal.firwin` to design the prototype, then quantize coefficients to fixed-point.

**Step 2: Implement as polyphase decomposition.**

An 8x interpolation FIR with N taps decomposes into 8 sub-filters of N/8 taps each. At the input rate (44.1 kHz), each input sample produces 8 output samples, but each sub-filter only computes N/8 multiply-accumulates. This is the efficient form.

However — **we do not need the 8 interpolated samples.** The DAC model's job is to reproduce the *audible coloration* of the interpolation filter as seen at the output sample rate. Since the SPU already outputs at 44.1 kHz and our output is 44.1 kHz, the modeling question becomes: **what does the AK4309's interpolation filter do to the signal that is visible at 44.1 kHz?**

**Answer: passband droop/ripple.** The interpolation filter's passband response is not perfectly flat — it has the +/-0.5 dB ripple specified in the datasheet. This is the coloration.

**Step 3: Implement as a passband-coloration filter at 44.1 kHz.**

Rather than upsampling 8x and downsampling back (wasteful), implement the *equivalent passband effect* as a short FIR or IIR at 44.1 kHz. This is a 5-15 tap filter that reproduces the AK4309's passband ripple character.

```c
// Conceptual — the DAC coloration filter at native rate
static inline int16_t spu94_dac_filter(spu94_dac_state_t *s, int16_t sample) {
    // Short FIR implementing the passband ripple of the AK4309 interpolation filter
    // Coefficients derived from datasheet specs via scipy.signal design + quantization
    int32_t acc = 0;
    for (int i = 0; i < SPU94_DAC_FIR_TAPS; i++) {
        acc += (int32_t)s->delay[i] * (int32_t)dac_fir_coefs[i];
    }
    // Shift state
    memmove(&s->delay[1], &s->delay[0], (SPU94_DAC_FIR_TAPS - 1) * sizeof(int16_t));
    s->delay[0] = sample;
    return sat_s16(acc >> 15);  // Q15 truncation, matching project convention
}
```

### Fixed-Point Coefficient Quantization

Use the same Q15 representation as the existing half-band FIR (`spu94_fir_coef.c`). The project already has infrastructure for this:
- Q15 multiply with truncation (`q15_mul_truncate`)
- Saturation (`sat_s16`)
- Golden-file regression for filter outputs

The new filter coefficients will be designed in Python (scipy), quantized to Q15 in Python, verified against the float reference, then hardcoded in C.

### Optional: Noise Floor Model

The AK4309 has approximately 15-bit effective dynamic range (vs. theoretical 16-bit). This manifests as a slightly elevated noise floor. Modeling options:

1. **Simple dither:** Add 1-bit TPDF dither to the output. This is technically "wrong" (the real noise is shaped, not white) but captures the subjective warmth.
2. **Shaped noise injection:** Use a simple first-order noise-shaping loop to push added noise toward higher frequencies, mimicking the delta-sigma's noise shape. Still very cheap computationally.
3. **Skip it.** The noise difference between 15-bit and 16-bit dynamic range is 6 dB — measurable but potentially not worth the modeling complexity for a creative effect.

**Recommendation:** Implement option 1 (TPDF dither) as a toggle, default off. It is 2 lines of code and costs nothing. Defer shaped noise to post-measurement (when Anthony can A/B against his real PS1).

---

## 4. What NOT to Add

| Avoid | Why | Impact if Added |
|-------|-----|-----------------|
| Full delta-sigma modulator simulation | Requires 8x oversampling (352.8 kHz); all artifacts are ultrasonic; computationally expensive | 8x CPU cost, zero audible benefit, breaks real-time budget on MCU |
| Analog output stage modeling (op-amps, coupling caps) | Requires actual circuit measurements from PS1 hardware; varies by board revision; explicitly out of scope in PROJECT.md | Scope creep into analog domain; no reference data to validate against |
| SCF / switched-capacitor filter emulation | Operates in analog domain; artifacts are above audio band | Adds complexity for inaudible results |
| Generic "tube warmth" or "analog warmth" processing | Not faithful to the actual hardware; no PS1 justification | Breaks the project's "bit-faithful from spec" philosophy |
| Resampling to 352.8 kHz for "accuracy" | The coloration is fully capturable at 44.1 kHz via equivalent filter | Wastes CPU, memory, and dev time |
| libsamplerate / libsoxr for upsampling | External dependency for a feature we should not be implementing | Dependency bloat for no benefit |
| Lookup tables for DNL/INL curves | Would need real measurements from specific AK4309 silicon; no published data exists | Fabricating data ≠ faithfulness |

---

## 5. Test and Measurement Tools for Verification

### Python-side (already in stack)

| Tool | Purpose | How to Use |
|------|---------|------------|
| `scipy.signal.freqz` | Plot frequency response of the designed FIR | Verify passband ripple matches +/-0.5 dB spec |
| `scipy.signal.remez` or `scipy.signal.firwin` | Design the interpolation filter prototype | Generate float coefficients, then quantize to Q15 |
| `numpy.fft.rfft` | Spectral analysis of DAC model output | Compare spectrum of processed vs. unprocessed signal |
| `matplotlib` | All visualization | Frequency response plots, spectrograms, A/B comparisons |
| `pytest` + golden files | Regression testing | Same pattern as v1.0 FIR and v1.1 ADPCM goldens |

### New Python verification scripts (to write)

| Script | Purpose |
|--------|---------|
| `design_dac_filter.py` | Design interpolation filter, quantize to Q15, emit C coefficient array |
| `verify_dac_response.py` | Measure frequency response of the C DAC model via ctypes, compare to design target |
| `compare_dac_coloration.py` | A/B spectral comparison: reverb output with and without DAC model enabled |

### Hardware measurement (future, with Anthony's PS1)

| Tool | Purpose | When |
|------|---------|------|
| REW (Room EQ Wizard) | Measure real PS1 frequency response, THD, noise floor via loopback | When hardware validation begins (M5 timeframe) |
| Audio interface with known flat response | Capture PS1 analog output digitally | Requires interface with good enough ADC (> 100 dB dynamic range) |
| `scipy` analysis of captured audio | Compare measured PS1 response to DAC model response | Post-capture |

### Reference measurements (published)

| Source | What It Provides | Confidence |
|--------|-----------------|------------|
| Stereophile SCPH-1001 measurements | Jitter (737 ps p-p), linearity error, noise floor ~15 dB above good CD players | MEDIUM — measured a specific unit, not the DAC in isolation |
| Archimago SCPH-5501 measurements | ~15-bit dynamic range, frequency response "slight deviance above 3 kHz", THD "respectable" | MEDIUM — blog measurements, not lab-grade |

These published measurements are useful as **sanity checks** (is our model in the right ballpark?) but not as **design targets** (they include the entire analog chain, not just the DAC's digital filter).

---

## 6. Integration with Existing Architecture

### Signal chain placement

The DAC model slots into the same toggleable-stage architecture as ADPCM:

```
Input samples (44.1 kHz stereo)
    |
    v
[ADPCM encode/decode] (if enabled)  -- v1.1
    |
    v
[Half-band FIR downsample to 22.05 kHz]
    |
    v
[Reverb network at 22.05 kHz]
    |
    v
[Half-band FIR upsample to 44.1 kHz]
    |
    v
[DAC coloration filter] (if enabled)  -- v1.2 NEW
    |
    v
Output samples (44.1 kHz stereo)
```

The DAC model goes **after** the reverb output, because in the real hardware the SPU's final mix bus feeds the DAC. The DAC does not process individual voices — it processes the final stereo mix.

### State structure addition

```c
typedef struct {
    int16_t delay_l[SPU94_DAC_FIR_TAPS];  // Left channel FIR delay line
    int16_t delay_r[SPU94_DAC_FIR_TAPS];  // Right channel FIR delay line
    int     enabled;                        // Toggle (default off)
} spu94_dac_state_t;
```

Estimated memory: ~64 bytes for a 15-tap stereo FIR. Fits comfortably within the existing static allocation model.

### API additions

```c
void spu94_dac_enable(spu94_t *ctx, int enable);
int  spu94_dac_is_enabled(const spu94_t *ctx);
// No other configuration — the filter is fixed to match AK4309 behavior
```

Minimal API surface, matching the ADPCM toggle pattern.

---

## Consolidated Stack Delta for v1.2

### New (C core)

| Addition | Purpose | Why |
|----------|---------|-----|
| `spu94_dac.c` + `spu94_dac.h` | DAC coloration filter implementation | Models AK4309 interpolation filter passband ripple |
| `spu94_dac_filter_coef.c` | Quantized Q15 FIR coefficients | Follows `spu94_fir_coef.c` pattern for traceability |
| Optional: TPDF dither toggle | Models DAC noise floor elevation | 2 lines of code, toggleable, default off |

### New (Python tooling)

| Addition | Purpose | Why |
|----------|---------|-----|
| `design_dac_filter.py` | Filter design + coefficient generation | Reproducible design-to-C pipeline |
| `verify_dac_response.py` | Frequency response verification | Automated check that C implementation matches design |
| `compare_dac_coloration.py` | A/B spectral analysis | Qualitative verification of coloration character |

### Unchanged

Everything else. No new external C dependencies. No new Python dependencies (scipy already in stack). No build system changes beyond adding new source files to CMakeLists.txt.

---

## What NOT to Use (v1.2 specific)

| Avoid | Why | Use Instead |
|-------|-----|-------------|
| libsamplerate / libsoxr | No resampling needed; coloration captured at native rate | Short FIR at 44.1 kHz |
| FFTW | Overkill; filter is a short time-domain FIR | Direct convolution (< 15 taps) |
| Float arithmetic in the DAC filter | Breaks project convention; non-deterministic across platforms | Q15 fixed-point, same as reverb core |
| External DSP filter library | Adds dependency for a 15-tap FIR; hides the arithmetic | Hand-rolled, like the rest of the project |
| Analog circuit simulation (SPICE, etc.) | Wrong domain; we are modeling digital-domain effects only | scipy for filter design, C for implementation |

---

## Confidence Summary

| Area | Confidence | Notes |
|------|------------|-------|
| DAC chip identification (AK4309AVM) | HIGH | Multiple independent sources agree |
| Converter topology (1-bit delta-sigma) | HIGH | Datasheet, psx-spx, emu-russia all confirm |
| 8x interpolation filter architecture | HIGH | Standard for AKM delta-sigma DACs of this era; confirmed by AK4309B datasheet |
| Exact filter coefficients | LOW | AK4309AVM datasheet unavailable; design from specs is the only option |
| Passband ripple spec (+/-0.5 dB) | MEDIUM | From AK4309B datasheet; AK4309AVM may differ slightly |
| Modeling approach (passband-equivalent FIR) | HIGH | Standard DSP technique; avoids unnecessary oversampling |
| Signal chain placement | HIGH | Matches real hardware topology |
| What to exclude (sigma-delta, analog) | HIGH | Clear cost/benefit: inaudible artifacts, massive cost |

---

## Sources

### Primary (HIGH confidence)

- [psx-spx Pinouts](https://psx-spx.consoledev.net/pinouts/) — AK4309VM identification, serial interface signals, board revision chip mapping
- [psx-spx SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — SPU output format, mixer/DAC sample rate
- [emu-russia/psxrev SPU](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md) — SPU serial output pins (DATO, LRCO, BCKO), DAC integration in later chips
- [AK4309B datasheet (AllDatasheet)](https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html) — specifications for the AK4309 family (90 dB DR, -84 dB THD+N, 8x FIR, SCF, 256fs/384fs MCLK)

### Secondary (MEDIUM confidence)

- [dogbreath.de PS1 DAC](https://dogbreath.de/PS1/DAC/DAC.html) — board revision to DAC chip mapping; AK4309AVM vs AK4309BM differences; notes on datasheet unavailability
- [Archimago SCPH-5501 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) — ~15-bit dynamic range, frequency response characterization, jitter measurements
- [Stereophile PS1 measurements](https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements) — 737 ps jitter, linearity error data, noise floor characterization (SCPH-1001)
- [DSPRelated: DAC Zero-Order Hold Models](https://www.dsprelated.com/showarticle/1627.php) — ZOH modeling mathematics, sinc droop formula, discrete-time model implementations
- [DSPRelated: Design a DAC sinx/x Corrector](https://www.dsprelated.com/showarticle/1191.php) — Fixed-point sinc compensation filter design with quantized coefficients
- [beis.de Delta-Sigma Introduction](https://www.beis.de/Elektronik/DeltaSigma/DeltaSigma.html) — Sigma-delta modulator structure, integrator-feedback pseudocode

### Tertiary (LOW confidence — verify at implementation time)

- Exact AK4309AVM vs AK4309B spectral differences — unknown without original datasheet or silicon measurement
- Filter tap count for passband-equivalent model — will be determined during scipy design phase
- TPDF dither amplitude calibration — needs A/B testing with real PS1 output to tune

---

*Stack research for: PS1 DAC conversion modeling as coloration stage in libspu94.*
*Researched: 2026-04-28*
