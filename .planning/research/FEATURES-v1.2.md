# Feature Research — DAC Modeling (v1.2 Milestone)

**Domain:** Digital model of PS1 DAC conversion artifacts for libspu94
**Researched:** 2026-04-28
**Confidence:** HIGH on DAC chip identity and topology (multiple independent sources confirm AK4309AVM sigma-delta). HIGH on ZOH sinc droop physics (textbook DSP). MEDIUM on PS1-specific artifact magnitudes (Stereophile/Archimago measurements exist but cover CD playback path, not isolated SPU output). MEDIUM on what matters musically (informed by existing retro DAC plugins but no direct PS1-DAC-model precedent exists). LOW on sigma-delta idle tone audibility in game audio context (theoretical understanding, no PS1-specific measurement).

---

## Context Before The Table

1. **DAC modeling is a post-processing stage.** It sits after the reverb output (and after FIR interpolation back to 44.1 kHz). The existing architecture already anticipated this: `spu94_io_chain.c` produces int16 stereo at 44.1 kHz, and ARCHITECTURE.md section 7.2 documented DAC as "an int16-in / int16-out filter plugged in after `spu94_process`."

2. **The PS1 DAC is identified.** The AK4309AVM (Asahi Kasei Microsystems) is a **1-bit sigma-delta DAC** with an 8x FIR interpolation digital filter, 2nd-order SCF (switched-capacitor filter) analog post-filter, and continuous-time output filter. It is NOT an R2R ladder DAC. This fundamentally constrains which artifacts are relevant: R2R-specific artifacts (monotonicity errors, major-carry glitches, resistor-mismatch DNL) do not apply.

3. **The goal is coloration, not perfection.** Like the ADPCM stage (v1.1), DAC modeling is a toggleable coloration feature. It adds the "sound of the PS1 output stage" on top of the bit-faithful reverb. It does not need to be a cycle-accurate AK4309 emulation -- it needs to reproduce the audible character.

4. **"Digital-only" is the scope boundary.** Everything after the DAC's analog output pin -- op-amps (NJM2100), coupling capacitors, output impedance, cable effects, TV input stage -- is explicitly out of scope per PROJECT.md.

---

## PS1 DAC Hardware Reference

| Fact | Value | Source | Confidence |
|------|-------|--------|------------|
| DAC chip | AK4309AVM (early models), AK4309BM (SCPH-700x) | dogbreath.de PS1 DAC page, multiple teardown sources | HIGH |
| Topology | 1-bit sigma-delta | AK4309B datasheet (compatible family) | HIGH |
| Bit depth (input) | 16-bit stereo | AK4309B datasheet | HIGH |
| Digital filter | 8x FIR interpolation | AK4309B datasheet ("8 times FIR Interpolator") | HIGH |
| Analog post-filter | 2nd-order SCF + continuous-time filter | AK4309B datasheet | HIGH |
| Dynamic range (rated) | 90 dB | AK4309B datasheet | HIGH |
| THD+N (rated) | -84 dB | AK4309B datasheet | HIGH |
| Measured dynamic range | ~15-bit effective (~90 dB) | Archimago SCPH-5501 measurements | MEDIUM |
| Measured jitter concern | Negligible (sidebands below -100 dB) | Archimago measurements | MEDIUM |
| Output sample rate | 44.1 kHz | nocash psx-spx ("Mixer and DAC supports 44.1kHz output rate") | HIGH |
| Master clock | 256fs or 384fs | AK4309B datasheet | HIGH |
| Newer board integration | DAC integrated into 208-pin SPU chip | nocash psx-spx pinouts section | MEDIUM |

---

## What Artifacts Does a Mid-90s Sigma-Delta DAC Introduce?

### 1. Zero-Order Hold (ZOH) Sinc Droop

**What it is:** The DAC holds each sample value constant for one sample period, creating a staircase waveform. This is equivalent to convolving the ideal impulse train with a rectangular pulse of width T (the sample period). The frequency response of this rectangular hold is a sinc function: `|H(f)| = |sin(pi*f/fs) / (pi*f/fs)|`.

**Magnitude:** 3.92 dB attenuation at Nyquist (fs/2 = 22.05 kHz). At 20 kHz this is approximately 3.5 dB of droop. At 10 kHz it is about 0.9 dB.

**PS1 relevance:** The AK4309's internal 8x oversampling FIR upsamples to 352.8 kHz before the 1-bit modulator, so the ZOH droop of the final analog output applies at 352.8 kHz -- meaning the sinc droop within the 0-22 kHz audio band is negligible (about 0.005 dB at 20 kHz). **However**, the 8x digital interpolation filter itself has a non-flat passband: the AK4309B datasheet specifies "+/-0.5 dB at 20 kHz" total response. This passband ripple IS the audible artifact, not classical ZOH droop.

**Modeling approach:** A short FIR or biquad filter matching the AK4309's measured passband ripple. NOT a raw sinc-droop model (which would be wrong for an oversampling DAC).

### 2. Digital Interpolation Filter Characteristics

**What it is:** The AK4309's internal 8x FIR interpolation filter defines the frequency response the listener actually hears. Mid-90s sigma-delta DACs used relatively modest-order FIR filters compared to modern designs, resulting in:
- Passband ripple (measured: +/-0.5 dB by 20 kHz per datasheet)
- Transition band rolloff less steep than modern oversampling DACs
- Potential pre-ringing / post-ringing from the linear-phase FIR (though this is subtle at 8x oversampling)

**PS1 relevance:** Stereophile's measurements noted "ripple in the top three octaves indicates an underspecified digital filter, which will tend to smear time-domain information." Archimago confirmed "slight deviance from flat response above 3 kHz." This is THE primary frequency-domain coloration of the PS1 output stage.

**Modeling approach:** A matched FIR or IIR filter replicating the AK4309's measured passband response. If the AK4309B datasheet coefficients are not available (they likely are not public), use the Stereophile/Archimago measurement curves as the target. A 4th-6th order IIR or 15-31 tap FIR would suffice.

### 3. Quantization Noise Floor (Sigma-Delta Noise Shaping)

**What it is:** A 1-bit sigma-delta DAC achieves its effective bit depth through noise shaping -- pushing quantization noise energy above the audio band. Within the audio band, the residual noise floor is approximately -90 dB (matching the 90 dB dynamic range spec). This is about 3 dB worse than a perfect 16-bit system (-96 dB theoretical).

**PS1 relevance:** The ~6 dB gap between theoretical 16-bit and the AK4309's actual noise floor means very quiet passages have slightly more audible noise than a modern DAC. For game audio (which is rarely near the noise floor), this is nearly inaudible. For reverb tails decaying into silence, it is potentially character-defining.

**Modeling approach:** Additive shaped noise at the appropriate level. This is the simplest feature to implement: generate low-level noise shaped to approximate the sigma-delta residual spectrum.

### 4. Sigma-Delta Idle Tones

**What it is:** When the input to a sigma-delta modulator is near DC or very low-level, the modulator can produce periodic bitstream patterns that manifest as tonal artifacts ("idle tones") at frequencies related to the input level and the modulator order. For an exact mid-scale DC input, the modulator output can produce a spike at half the modulator clock frequency.

**PS1 relevance:** LOW confidence that this is audible in practice. The AK4309 likely uses dither internally to mitigate idle tones (standard practice even in the mid-90s). Game audio is rarely at the exact DC/near-DC levels where idle tones manifest. Reverb tail decay could theoretically expose them, but the noise floor of the rest of the system likely masks them.

**Modeling approach:** Likely NOT worth modeling. Flag as a differentiator candidate only if hardware measurements reveal audible idle tones on actual PS1 reverb output.

### 5. DNL/INL Nonlinearity

**What it is:** Differential Non-Linearity (DNL) and Integral Non-Linearity (INL) describe how much each DAC output step deviates from its ideal value. In R2R ladder DACs, this is a major artifact source (the YM2612's famous "ladder effect" is a DNL problem). In sigma-delta DACs, 1-bit architectures are inherently monotonic -- there is only one comparator threshold, so DNL/INL in the traditional sense does not apply.

**PS1 relevance:** The AK4309 being a 1-bit sigma-delta DAC means it does NOT suffer from the ladder-effect-style nonlinearity that characterizes the Genesis YM2612 (which uses an R2R DAC). This is a critical distinction. The PS1's DAC coloration is fundamentally different from the Genesis's.

**Modeling approach:** No nonlinearity lookup table needed. This is an anti-feature for a sigma-delta DAC model.

---

## Which Artifacts Are Topology-Dependent?

| Artifact | R2R Ladder | Multibit Sigma-Delta | 1-bit Sigma-Delta (PS1) |
|----------|-----------|---------------------|------------------------|
| ZOH sinc droop | YES (NOS designs) | Negligible (oversampled) | Negligible (8x oversampled) |
| DNL/INL nonlinearity | MAJOR (resistor mismatch) | Moderate | Absent (1-bit = monotonic) |
| Major-carry glitch | YES (MSB transitions) | Absent | Absent |
| Idle tones | Absent | Possible | Possible (mitigated by dither) |
| Noise shaping residual | Absent | Present | Present |
| Digital filter passband ripple | Absent (NOS) | Present | Present (primary coloration) |
| Reconstruction filter coloration | Present (external) | Present (on-chip) | Present (on-chip SCF+CTF) |

**Key insight for SPU-94:** The PS1's DAC artifacts are COMPLETELY DIFFERENT from the Genesis's. Anyone expecting "ladder effect" style distortion from PS1 DAC modeling is confusing topologies. The PS1's coloration is subtle: passband ripple, gentle HF rolloff, slightly elevated noise floor.

---

## How Do Existing Emulators and Plugins Model DAC Behavior?

### Console Emulators

| Project | Console | DAC Topology | What They Model | Approach |
|---------|---------|-------------|-----------------|----------|
| Nuked-OPN2 (nukeykt) | Genesis | 9-bit R2R | Ladder effect (DNL nonlinearity at zero crossing), silence slot leakage, low-pass filter | Lookup table for nonlinearity, additive offset for silence slots, 1st-order LPF |
| Genesis Plus GX | Genesis | 9-bit R2R | Ladder effect, channel-sequential output timing | Same Nuked-OPN2 approach when enabled |
| bsnes/higan | SNES | 16-bit delta-sigma (S-DSP BRR output) | Bit-perfect to digital output; DAC not modeled | No analog modeling |
| Mednafen (PS1) | PS1 | Sigma-delta | Unknown (licensing prevents reading) | Unknown |
| DuckStation | PS1 | Sigma-delta | Unknown (licensing prevents reading) | Unknown |

**Takeaway:** The only console emulator community that has deeply modeled DAC artifacts is the Genesis community, because the YM2612's R2R ladder nonlinearity is **loud** and character-defining. SNES and PS1 emulators generally stop at the digital output. This means SPU-94 is exploring relatively uncharted territory for PS1-specific DAC modeling.

### Audio Plugins (DAC Emulation)

| Plugin | What It Models | Approach |
|--------|---------------|----------|
| Plogue chipcrusher | Multiple vintage DAC types | Oversampled encode/decode pipeline: resample -> encode -> decode -> ZOH/PWM -> RC filter -> downsample. 16 encoding formats. Bit-weighting for non-monotonic behavior. |
| TAL-DAC | Generic 80s sampler character | Bit-depth reduction + sample-rate reduction + upsampling. Character from the encode/decode artifacts. |
| HoRNet ADDA | Generic DAC/ADC chain | Adjustable bit depth (4-24), resampling (4k-44.1k), 4 interpolation algorithms, optional anti-alias filter. |
| Acustica Azero | Vintage 80s DAC | Bit depth, sample rate, jitter parameters. Convolution-based. |

**Takeaway:** Commercial DAC-emulation plugins focus on bit-crushing and sample-rate reduction because those are the LOUDEST artifacts. The subtle sigma-delta coloration (passband ripple, noise floor character) is territory none of these plugins address specifically. This is a potential differentiator.

---

## Feature Landscape

### Table Stakes (Users Expect These)

Features that anyone expecting "DAC modeling" in a PS1 reverb plugin would assume exist.

| Feature | Why Expected | Complexity | Dependencies | Notes |
|---------|--------------|------------|--------------|-------|
| Toggleable DAC stage (on/off) | Matches ADPCM pattern; users want A/B comparison | LOW | `spu94_io_chain.c` integration pattern from v1.1 | Follow exact ADPCM-INT-01 pattern: `spu94_set_dac_enabled()` / `spu94_get_dac_enabled()` |
| Passband frequency response coloration | THE primary audible artifact of the AK4309; this is the "sound" of the PS1 output stage | MEDIUM | FIR or IIR filter design targeting AK4309 measured response | Need measurement target: AK4309B datasheet curve (+/-0.5dB at 20kHz) or Stereophile/Archimago measurements |
| Reconstruction filter modeling (SCF+CTF analog post-filter) | The on-chip switched-capacitor + continuous-time filter shapes the final output | MEDIUM | Combined with passband response into single filter stage | May be inseparable from digital filter response in practice; model as one composite frequency response |
| Noise floor at correct level | 90 dB dynamic range (not 96 dB) means slightly elevated noise vs ideal 16-bit | LOW | Noise generator + level scaling | Simple additive noise. Key question: shaped or white? Sigma-delta residual is shaped (HF-biased). |
| Zero additional latency | DAC stage should not add processing latency (or declare it if it does) | LOW | Latency reporting API (`spu94_get_total_latency_samples`) | IIR filters add no latency; FIR filters add group delay. Minimum-phase IIR preferred for zero-latency. |
| Clean state on disable | Disabling DAC mid-stream must not leave artifacts | LOW | State reset pattern from ADPCM-INT-04 | Zero filter state on disable, clean re-enable |
| Default off | New coloration stages default to off for backward compatibility | LOW | Init code | Match ADPCM precedent |
| C99, no heap, real-time safe | Must meet all existing libspu94 constraints | LOW (design constraint, not implementation) | Existing rt_safety test infrastructure | Filter coefficients as const arrays in .rodata |
| Python binding exposure | `spu94.set_dac_enabled()` in Python ctypes binding | LOW | Existing binding pattern | Trivial addition to `_ffi.py` |
| CLI flag | `--dac` flag for CLI processing | LOW | Existing CLI flag pattern | Match `--adpcm` precedent |
| JUCE GUI toggle | DAC on/off toggle in standalone GUI | LOW | Existing JUCE ADPCM toggle pattern | Match ADPCM toggle precedent |

### Differentiators (Competitive Advantage)

Features that would set SPU-94's DAC model apart from generic bitcrusher/lo-fi plugins.

| Feature | Value Proposition | Complexity | Dependencies | Notes |
|---------|-------------------|------------|--------------|-------|
| PS1-specific frequency response curve | Not a generic filter -- matched to actual AK4309 measurements. No other plugin does this. | MEDIUM | Access to measurement data (Archimago blog, Stereophile) | THE differentiator. A filter that sounds like a PS1, not like a generic lo-fi effect. |
| Sigma-delta noise shaping character | Noise shaped to match actual sigma-delta residual spectrum (HF-biased), not flat white noise | MEDIUM | Noise shaping filter design | Subtle but correct. Adds authenticity to reverb tail decay. |
| Hardware-revision awareness | Different PS1 models used different DAC chips (AK4309AVM vs AK4309BM vs integrated) and analog output stages | HIGH | Multiple filter coefficient sets, UI for revision selection | Probably excessive for v1.2. Flag for future. Models vary in op-amp presence too (which is out of scope). |
| Documented ADR for every design choice | Following the DECISIONS.md pattern from M1/M2: every modeling decision explained | LOW | Discipline, not code | Matches project ethos. Valuable for the preservation/education audience. |

### Anti-Features (Explicitly NOT Building)

Features that seem related but would be wrong, misleading, or out of scope.

| Feature | Why Requested | Why Problematic | Alternative |
|---------|---------------|-----------------|-------------|
| R2R ladder nonlinearity / "ladder effect" | People associate "DAC artifacts" with Genesis-style distortion | PS1 uses sigma-delta, not R2R. Modeling ladder effect would be factually wrong. | Model the actual sigma-delta artifacts (passband ripple, noise character). |
| Bit-crushing / sample-rate reduction | Lo-fi plugins do this; people may expect it | The AK4309 receives full 16-bit/44.1kHz data. No bit-depth or rate reduction occurs at the DAC. Bit-crushing belongs to ADPCM (already implemented in v1.1) or to a separate creative effect. | ADPCM stage (v1.1) already provides the "crushed" coloration. DAC adds the "output stage" coloration. |
| Analog output stage (op-amps, coupling caps) | The "warm PS1 sound" includes analog components after the DAC | Requires hardware measurements, varies by board revision, involves analog circuit modeling that is fundamentally different from digital signal processing | Defer to a future milestone. Document the boundary: DAC model stops at the DAC output pin. |
| Power supply noise / ripple | PSU noise contributes to character on real hardware | Non-deterministic, board-revision-dependent, not reproducible without measurement | Out of scope entirely. If needed later, it is a separate "analog board model" stage. |
| Thermal drift / component aging | Real DACs drift with temperature and age | Unmeasurable without a lab. Non-deterministic. Adds complexity with zero audible payoff in a digital model. | Out of scope permanently. |
| Jitter modeling | DAC clock jitter adds artifacts | PS1 measurements show jitter below -100 dB -- negligible. Modeling it would add complexity for inaudible effect. | Out of scope unless hardware measurements reveal otherwise. |
| Variable bit depth control | Bitcrusher-style "reduce to 12-bit / 8-bit" knob | The PS1 DAC is fixed at 16-bit input. Exposing variable bit depth turns this into a generic effect, not a PS1 model. | Not applicable to faithful PS1 modeling. The ADPCM stage (4-bit) already provides bit-depth reduction if desired. |
| Idle tone modeling | Sigma-delta idle tones at near-DC levels | Likely inaudible in practice (dithered in hardware, masked by system noise, game audio rarely near DC). HIGH complexity, LOW payoff. | Skip. Revisit only if hardware captures reveal audible idle tones. |

---

## Feature Dependencies

```
[Passband frequency response filter]
    requires -> [Filter coefficient design from measurement data]
    requires -> [Toggleable stage infrastructure]

[Reconstruction filter model]
    merges-with -> [Passband frequency response filter]
    (modeled as single composite response, not two separate filters)

[Noise floor modeling]
    requires -> [Toggleable stage infrastructure]
    enhances -> [Passband frequency response filter]

[Toggleable stage infrastructure]
    requires -> [spu94_state DAC fields]
    requires -> [spu94_io_chain.c integration point]
    follows-pattern -> [ADPCM toggle from v1.1]

[Python/CLI/JUCE exposure]
    requires -> [Toggleable stage infrastructure]
    follows-pattern -> [ADPCM exposure from v1.1]

[ADR documentation]
    requires -> [All design decisions finalized]
    independent (can be written alongside implementation)
```

### Dependency Notes

- **Passband filter requires measurement target:** The single biggest research dependency. Need to extract a target frequency response curve from Archimago's measurements, Stereophile's measurements, or (ideally) direct AK4309B datasheet filter specification. Without this, the filter design is guesswork.
- **Reconstruction filter merges with passband filter:** In practice, the AK4309's digital interpolation filter and analog SCF+CTF post-filter are experienced as one composite frequency response. Modeling them as separate stages adds complexity without audible benefit. Model the composite measured response.
- **Toggle infrastructure is trivial:** The v1.1 ADPCM toggle established the exact pattern. DAC toggle is copy-paste with different state fields.

---

## MVP Definition

### Launch With (v1.2)

Minimum features for a credible "DAC modeling" milestone.

- [ ] Toggleable DAC coloration stage (on/off, default off) -- matches ADPCM precedent
- [ ] Composite frequency response filter matching AK4309 measured passband -- THE core feature; this is the "sound"
- [ ] Noise floor at correct level (~90 dB dynamic range) with appropriate spectral shape -- completes the model
- [ ] API exposure: `spu94_set_dac_enabled()` / `spu94_get_dac_enabled()` in C, Python, CLI, JUCE
- [ ] Latency reporting updated for DAC stage (likely zero if IIR; small if FIR)
- [ ] Golden files for DAC-on and DAC-off processing
- [ ] ADRs for all design choices (filter topology, noise approach, measurement target selection)
- [ ] Test coverage: unit tests for filter, integration tests for pipeline, golden regression

### Add After Validation (v1.2.x)

Features to add once the core DAC model is verified and shipped.

- [ ] Sigma-delta noise shaping (HF-biased noise instead of flat white) -- adds authenticity to tail decay
- [ ] Measurement comparison documentation (SPU-94 output vs published PS1 measurements)

### Future Consideration (v2+)

- [ ] Hardware-revision-specific DAC profiles (AK4309AVM vs AK4309BM vs integrated SPU DAC)
- [ ] Analog output stage modeling (op-amps, coupling caps) -- requires real hardware measurement campaign
- [ ] Wet/dry blend control for DAC coloration intensity

---

## Feature Prioritization Matrix

| Feature | User Value | Implementation Cost | Priority |
|---------|------------|---------------------|----------|
| Toggleable DAC stage | HIGH | LOW | P1 |
| Passband frequency response filter | HIGH | MEDIUM | P1 |
| Noise floor modeling | MEDIUM | LOW | P1 |
| Python/CLI/JUCE exposure | HIGH | LOW | P1 |
| Golden file regression tests | HIGH | LOW | P1 |
| ADR documentation | MEDIUM | LOW | P1 |
| Sigma-delta noise shaping | LOW | MEDIUM | P2 |
| Hardware-revision profiles | LOW | HIGH | P3 |
| Analog output stage | MEDIUM | HIGH | P3 (separate milestone) |

**Priority key:**
- P1: Must have for v1.2 launch
- P2: Should have, add when validation confirms value
- P3: Future milestone, not v1.2

---

## Competitor / Precedent Analysis

| Aspect | Genesis (Nuked-OPN2) | SNES (bsnes) | PS1 Emulators | Lo-fi Plugins | SPU-94 v1.2 |
|--------|----------------------|--------------|---------------|---------------|-------------|
| DAC topology modeled | R2R (9-bit) | None | None (digital output only) | Generic (bit-crush) | Sigma-delta (AK4309) |
| Nonlinearity table | Yes (ladder effect) | N/A | N/A | Bit-weighting (Plogue) | No (1-bit SD = monotonic) |
| Frequency response | 1st-order LPF (~3.4 kHz) | N/A | N/A | RC filter option | AK4309-matched composite response |
| Noise modeling | Implicit in ladder effect | N/A | N/A | Not typically | Sigma-delta residual noise |
| Documented design decisions | Source code only | Source code only | N/A | Marketing copy | ADRs in DECISIONS.md |
| PS1-specific | No | No | Conceptually yes, not implemented | No | Yes -- matched to actual PS1 DAC chip |

**SPU-94's position:** No existing project specifically models the PS1 DAC's sigma-delta characteristics. The Genesis community solved this for R2R; SPU-94 would be the first to do it for the PS1's sigma-delta output. The value is correctness and specificity, not loudness of effect.

---

## Sources

- [dogbreath.de — PS1 DAC page](https://dogbreath.de/PS1/DAC/DAC.html) — DAC chip identification across PS1 hardware revisions. **HIGH confidence.**
- [AK4309B datasheet via AllDatasheet](https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html) — 8x FIR interpolator, 2nd-order SCF+CTF, 90 dB DR, -84 dB THD+N. Note: AK4309B is 20-pin, PS1 uses 24-pin AK4309AVM; pinout differs but audio specs believed identical. **MEDIUM confidence** (compatible family, not identical part).
- [Archimago's Musings — PS1 SCPH-5501 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) — ~15-bit effective dynamic range, passband deviation above 3 kHz, jitter below -100 dB. **MEDIUM confidence** (measured CD playback, not isolated SPU output).
- [Stereophile — PS1 CD player measurements](https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements) — "Ripple in top three octaves indicates an underspecified digital filter." ~14-bit effective resolution. **MEDIUM confidence** (same caveat as Archimago).
- [nocash psx-spx — SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) — "Mixer and DAC supports 44.1kHz output rate"; older boards have separate DAC, newer boards have DAC in 208-pin SPU. **HIGH confidence.**
- [jsgroth — Emulating the YM2612 Part 5: Analog Output](https://jsgroth.dev/blog/posts/emulating-ym2612-part-5/) — Detailed documentation of Genesis R2R ladder effect modeling in Nuked-OPN2. Used as contrast reference (PS1 is NOT R2R). **HIGH confidence** for Genesis facts.
- [Neil Robertson — DAC Zero-Order Hold Models (DSPRelated)](https://www.dsprelated.com/showarticle/1627.php) — ZOH sinc droop formula, 3.92 dB at Nyquist, digital modeling approaches. **HIGH confidence** for ZOH theory.
- [Analog Devices — Equalizing Techniques Flatten DAC Frequency Response](https://www.analog.com/en/resources/technical-articles/equalizing-techniques-flatten-dac-frequency-response.html) — Sinc compensation theory and inverse-sinc filter design. **HIGH confidence** for DSP theory.
- [Plogue chipcrusher product page](https://www.plogue.com/products/chipcrusher.html) — Commercial DAC emulation reference: 16 encoding formats, ZOH/PWM oversampling, RC filter, bit-weighting for non-monotonic behavior. **HIGH confidence** for competitor feature set.
- [RetroGameTalk — PS1 Audio Quality](https://retrogametalk.com/threads/why-early-playstation-1-models-are-valued-in-the-audio-world.3598/) — Community discussion of PS1 model audio quality variations. **LOW confidence** (forum discussion).

---
*Feature research for: SPU-94 v1.2 DAC Modeling milestone*
*Researched: 2026-04-28*
