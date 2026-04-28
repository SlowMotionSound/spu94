# Deep Technical Research: Delta-Sigma DAC Conversion in the AK4309AVM

**Domain:** PS1 DAC internals for libspu94 modeling
**Researched:** 2026-04-28
**Overall confidence:** MEDIUM -- AK4309AVM-specific internals are not publicly documented (datasheet lost); findings are a combination of what the AK4309B datasheet *does* specify, general delta-sigma theory applied to the known specs, and PS1 hardware measurements. Confidence levels per-section below.

---

## 1. Delta-Sigma Modulator Architecture and Order

### What the AK4309 datasheet tells us

The AK4309B datasheet (the only publicly available family member datasheet) describes the part as a "1-bit stereo DAC" with "8 times FIR Interpolator" and "2nd order SCF and CTF." It specifies 90 dB dynamic range and -84 dB THD+N. It does NOT explicitly state the modulator order.

**Confidence: HIGH** that the datasheet does not disclose the modulator order.

### Deriving the modulator order from specifications

We can work backward from the published specs to determine what modulator order is needed.

**Given:**
- 1-bit quantizer (B = 1)
- Master clock: 384fs (confirmed by PS1 hardware -- see Section 3)
- 8x digital interpolation filter upsamples 44.1 kHz to 352.8 kHz
- The delta-sigma modulator operates at the master clock rate: 384 x 44.1 kHz = 16.9344 MHz
- Oversampling ratio (OSR) relative to the audio band: 16,934,400 / (2 x 22,050) = 384
- Dynamic range target: 90 dB

**SNR formula for an Lth-order delta-sigma modulator with 1-bit quantizer:**

```
SNR_dB = 6.02*B + 1.76 + (2L+1)*10*log10(OSR) - 10*log10(pi^(2L) / (2L+1))
```

For B=1, OSR=384:

| Order (L) | SNR formula result | Achieves 90 dB? |
|-----------|-------------------|------------------|
| 1 | 6.02 + 1.76 + 30*log10(384) - 10*log10(pi^2/3) = 7.78 + 77.5 - 5.17 = **80.1 dB** | NO |
| 2 | 6.02 + 1.76 + 50*log10(384) - 10*log10(pi^4/5) = 7.78 + 129.2 - 12.9 = **124.1 dB** | YES (excess) |
| 3 | 6.02 + 1.76 + 70*log10(384) - 10*log10(pi^6/7) = 7.78 + 180.8 - 21.4 = **167.2 dB** | YES (massive excess) |

**Wait -- these numbers seem high for L=2.** That is because OSR=384 is the ratio of the modulator clock to the Nyquist frequency. But the digital interpolation filter only upsamples 8x (to 352.8 kHz). The remaining factor of 384/8 = 48 comes from the modulator operating at the master clock rate, NOT the interpolated sample rate. The 8x interpolation filter defines the image-free bandwidth; the modulator then runs at a further 48x over that, giving the total OSR of 384 relative to the audio Nyquist.

Let me recalculate using the more conservative view where the effective modulator OSR relative to its input (352.8 kHz) is 16.9344 MHz / 352.8 kHz = 48:

But this is not the right way to think about it either. The modulator's noise shaping operates over its full bandwidth (0 to fs_mod/2 = 8.467 MHz). The audio band occupies 0 to 22.05 kHz. So the relevant OSR for the noise-shaping calculation IS 384. The 8x interpolation filter is just one stage in getting the signal up to the modulator rate; the SCF and CTF handle the analog reconstruction after the 1-bit stream.

However, many practical delta-sigma modulators do not achieve theoretical SNR due to stability constraints, non-ideal integrators, and thermal noise. The theoretical numbers above show that even a **2nd-order modulator at OSR=384 massively exceeds 90 dB** -- implying the AK4309 likely uses a relatively low-order modulator (2nd or 3rd order) and hits 90 dB comfortably.

Actually, let me reconsider. The 8x interpolation filter runs at 352.8 kHz, producing an image-free signal. The delta-sigma modulator takes THIS as input and converts it to 1-bit. The modulator itself must run at the master clock rate (16.9344 MHz = 384fs), which is 48x above the interpolation filter's output rate (352.8 kHz). So from the modulator's perspective, its OSR relative to the interpolation filter bandwidth is 48x. But the audio band it needs to keep clean is only 0-22.05 kHz within that 0-176.4 kHz bandwidth, so the effective OSR relative to audio Nyquist is still 384.

**Conclusion: A 2nd-order modulator is sufficient and likely.** A 3rd-order modulator would be overkill for 90 dB at OSR=384. A 1st-order is insufficient (80 dB theoretical, and practical implementations fall short of theory). Mid-1990s AKM consumer parts would use a 2nd- or 3rd-order modulator for cost and stability reasons.

**Confidence: MEDIUM.** The modulator order is inferred from specs, not documented. A 3rd-order MASH is also possible (unconditionally stable, common in the era). However, the 90 dB spec strongly suggests a 2nd-order single-loop or a 2nd-order MASH (1-1) architecture, because a 3rd-order design would easily exceed 100 dB and AKM would likely have advertised a higher dynamic range if they had one.

### Noise Transfer Function

For an Lth-order modulator, the noise transfer function (NTF) is:

```
NTF(z) = (1 - z^-1)^L
```

For a likely **2nd-order** modulator:

```
NTF(z) = (1 - z^-1)^2 = 1 - 2z^-1 + z^-2
```

In the frequency domain, this maps to:

```
|NTF(f)|^2 = [2 * sin(pi * f / fs_mod)]^(2L)
```

For L=2:

```
|NTF(f)| = [2 * sin(pi * f / fs_mod)]^2
```

This is a 2nd-order high-pass filter on the quantization noise. At low frequencies (audio band), the noise is heavily suppressed. At high frequencies (near fs_mod/2), the noise peaks.

**Noise spectrum shape:** For a 2nd-order modulator, quantization noise rises at **40 dB/decade** (12 dB/octave) from DC toward the modulator frequency. At 20 kHz, the noise is approximately:

```
|NTF(20kHz)|^2 = [2 * sin(pi * 20000 / 16934400)]^4
                = [2 * sin(0.00371)]^4
                = [2 * 0.00371]^4
                = [0.00742]^4
                = 3.03e-9
                = -85.2 dB relative to the full-band quantization noise
```

This means the in-band noise at 20 kHz is 85 dB below the total quantization noise power, which is consistent with achieving ~90 dB dynamic range in the audio band.

**Confidence: HIGH** for the NTF formula (textbook). **MEDIUM** for the 2nd-order assumption.

### What the noise spectrum means for modeling

The noise shaping pushes quantization noise energy strongly upward in frequency. Within the audio band (0-22.05 kHz), the noise spectrum rises at ~12 dB/octave (for a 2nd-order modulator). This means:
- **Below 1 kHz:** Noise is extremely low (well below -100 dB)
- **At 5 kHz:** Noise is rising but still very low
- **At 20 kHz:** Noise reaches approximately -90 dB (the rated dynamic range)
- **Above 22 kHz:** Noise rises dramatically, but is out of the audio band

For modeling at 44.1 kHz output rate, this noise spectrum should be approximated as a **2nd-order high-pass shaped noise** (if we believe the modulator is 2nd-order). The original ARCHITECTURE-v1.2.md research proposed a 1st-order HP-shaped noise; a 2nd-order HP shape would be more accurate.

---

## 2. The 8x Digital Interpolation Filter

### Known from the AK4309B datasheet

- 8x FIR interpolation (44.1 kHz input, 352.8 kHz output)
- Passband response: +/-0.5 dB at 20 kHz
- Sampling rate range: 8 kHz to 50 kHz

**Confidence: HIGH** for these specs.

### Single-stage 8x or cascaded?

The AK4309B datasheet does not specify whether the 8x interpolation is a single-stage FIR or cascaded stages. However, the standard practice in mid-1990s consumer audio DAC design -- well-documented in DSP literature -- was to use **cascaded half-band FIR filters** for interpolation.

**Typical cascaded 8x architecture:**

```
44.1 kHz --> [2x half-band FIR] --> 88.2 kHz --> [2x half-band FIR] --> 176.4 kHz --> [2x half-band FIR] --> 352.8 kHz
```

Three cascaded 2x half-band stages give 8x total interpolation. This is overwhelmingly preferred over a single 8x FIR because:

1. **Efficiency:** A half-band FIR exploits the property that every other coefficient is zero (except the center tap), halving the computation. Three short half-band filters require far fewer multiplies than one long 8x FIR.
2. **Silicon area:** In a mid-1990s process, gate count was expensive. Cascaded half-bands minimize the number of multipliers needed.
3. **Standard practice:** Virtually every oversampling DAC interpolation filter from this era used cascaded 2x stages. The Analog Devices MT-017 tutorial documents this as the standard approach.

**Confidence: MEDIUM-HIGH.** Not confirmed by datasheet but strongly supported by the engineering economics of the era and universal practice in comparable parts.

### Tap count estimates

For a half-band FIR designed to meet the AK4309's specs:

**First stage (44.1 kHz to 88.2 kHz):**
- Passband: 0-20 kHz, ripple +/-0.5 dB
- Transition band: 20 kHz to 24.1 kHz (88.2 - 2*20 = 48.2 kHz; image starts at 44.1 - 20 = 24.1 kHz)
- Stopband rejection: >70 dB (to keep images below the noise floor)
- Estimated taps: **~23-31 taps** (half-band, so ~12-16 non-zero coefficients)

This first stage is the most demanding because the transition band (20 kHz to 24.1 kHz) is narrow relative to the operating rate.

**Second stage (88.2 kHz to 176.4 kHz):**
- Transition band is wider relative to the operating rate
- Estimated taps: **~11-15 taps** (~6-8 non-zero coefficients)

**Third stage (176.4 kHz to 352.8 kHz):**
- Very relaxed specifications
- Estimated taps: **~7-11 taps** (~4-6 non-zero coefficients)

Total coefficient storage: approximately **40-57 unique coefficients** across all three stages.

**Confidence: LOW-MEDIUM.** These are educated estimates based on DSP filter design principles and the +/-0.5 dB spec. The actual AK4309 coefficients are unknown.

### Passband ripple mechanism

The +/-0.5 dB ripple at 20 kHz is a consequence of the digital interpolation filter design, NOT the analog reconstruction. In a cascaded half-band architecture:

- Each half-band stage contributes its own passband ripple
- The ripples of all stages **compound multiplicatively** in the frequency domain
- A half-band filter designed for +/-0.15 dB individual ripple would compound to approximately +/-0.45 dB through three stages (not simply additive due to the complex interaction of phase and magnitude)
- The +/-0.5 dB total spec is typical for a mid-1990s cost-optimized design with relatively short (low tap count) half-band filters

The "ripple in the top three octaves" noted by Stereophile is exactly this: the combined passband ripple of the cascaded interpolation stages, becoming more pronounced at higher frequencies where the filters approach their transition bands.

**Confidence: HIGH** for the mechanism (textbook DSP). **MEDIUM** for the specific ripple allocation per stage.

---

## 3. Oversampling Ratio and Clock Relationship

### PS1 clock tree (confirmed by hardware)

The PS1's audio clock tree is well-documented from multiple independent hardware analysis sources:

| Signal | Frequency | Derivation | Source |
|--------|-----------|------------|--------|
| System crystal | 67.7376 MHz | Primary oscillator | psx-spx, consolemods |
| CPU clock | 33.8688 MHz | 67.7376 / 2 | psx-spx |
| Audio master clock (MC) | **16.9344 MHz** | 67.7376 / 4 | firebrandx, gamingdoc |
| Bit clock (BC) | 2.1168 MHz | 16.9344 / 8 (= 48fs) | firebrandx, gamingdoc |
| Word clock (LRC) | 44.1 kHz | 16.9344 / 384 | firebrandx, gamingdoc |

**The master clock is 384fs.** This is confirmed by:
- The firebrandx PSX Digital Audio Mod page: "16.9344MHz, or 384 times the sample rate (384Fs for short)"
- The GamingDoc TOSLink mod page: identical numbers
- The AK4309 CKS pin: "CKS = H selects MCLK = 384fs"

The PS1's CKS pin on the AK4309AVM is pulled HIGH, selecting 384fs mode.

**Confidence: HIGH.** Multiple independent hardware measurements confirm 384fs.

### What 384fs means for the delta-sigma modulator

The delta-sigma modulator runs at the master clock rate: **16.9344 MHz**.

The 8x interpolation filter upsamples the 44.1 kHz input to 352.8 kHz (= 8fs). The modulator then further processes this at 384fs, meaning the modulator runs at **48x the interpolation filter output rate** (16.9344 MHz / 352.8 kHz = 48).

The total oversampling ratio relative to the audio Nyquist (22.05 kHz) is:
```
OSR = 16,934,400 / (2 * 22,050) = 384
```

This is a very high OSR by 1990s standards (typical was 64x or 128x for premium parts). The 384x OSR gives the modulator enormous headroom for noise shaping, which is why a relatively low-order modulator (2nd order) can achieve 90 dB dynamic range.

**Note:** The firebrandx page specifically mentions that 384fs was "atypical" -- the SNES and Saturn used the more common 256fs. Sony's choice of 384fs gives the AK4309 MORE oversampling headroom than the same DAC would have in 256fs mode, resulting in slightly better noise performance.

**Confidence: HIGH.**

---

## 4. Zero-Order Hold (ZOH) Behavior in a 1-Bit Stream

### The classical ZOH model

In a conventional multibit DAC, the ZOH effect comes from holding each sample value constant for one sample period T. The frequency response is:

```
|H_ZOH(f)| = |sin(pi * f * T) / (pi * f * T)| = sinc(f * T)
```

At the Nyquist frequency (f = fs/2), this gives -3.92 dB attenuation.

### How ZOH works in a 1-bit oversampled DAC

In a 1-bit delta-sigma DAC, the output is a pulse density modulated (PDM) bitstream at 16.9344 MHz. Each bit is held for one modulator clock period:

```
T_mod = 1 / 16,934,400 = 59.05 ns
```

The ZOH sinc rolloff applies at the **modulator's sample rate**, NOT the audio sample rate. The sinc droop at 20 kHz relative to the modulator rate is:

```
|H_ZOH(20kHz)| = sinc(20000 / 16934400)
               = sinc(0.001181)
               = sin(pi * 0.001181) / (pi * 0.001181)
               = sin(0.00371) / 0.00371
               = 0.999998
               = -0.000009 dB
```

**The ZOH sinc droop at 20 kHz is 0.000009 dB -- completely negligible.**

This is the fundamental difference between a 1-bit oversampled DAC and a NOS (non-oversampling) multibit DAC. In a NOS DAC running at 44.1 kHz, the ZOH hold time is 22.7 microseconds and the sinc droop at 20 kHz is -3.5 dB. In the AK4309, the hold time is 59 nanoseconds and the droop at 20 kHz is essentially zero.

### The -3.9 dB figure is NOT correct for a 1-bit stream

The -3.9 dB at Nyquist figure applies to a DAC where each output sample is held for one audio sample period (1/44100 seconds). In the AK4309:

- The 1-bit PDM stream runs at 16.9 MHz
- Each bit is held for 59 ns
- The sinc droop is negligible across the entire audio band
- The SCF + CTF analog reconstruction filter removes the ultrasonic PDM energy
- The resulting audio output has essentially ZERO ZOH droop

**The "ZOH sinc droop" artifact is irrelevant to the AK4309.** Any frequency response coloration in the audio band comes from the digital interpolation filter's passband ripple and the analog reconstruction filter's rolloff -- NOT from ZOH.

**Confidence: HIGH.** This is straightforward DSP mathematics applied to known clock rates.

---

## 5. Switched-Capacitor Filter (SCF)

### What the AK4309 has

The AK4309B datasheet specifies "2nd order SCF and CTF" for post-DAC reconstruction. The SCF is the primary reconstruction filter; the CTF (continuous-time filter) is a simple RC-type smoothing filter after the SCF.

### How an SCF works at the component level

A switched-capacitor filter replaces resistors in an analog active filter with capacitor-switch networks. The fundamental equivalence:

```
A capacitor C switched at frequency f_clk is equivalent to a resistor:
R_equiv = 1 / (C * f_clk)
```

**Operating principle:**
1. A small capacitor (C_in) alternates between two MOSFET switches controlled by a non-overlapping two-phase clock
2. During phase 1: C_in charges to the input voltage
3. During phase 2: C_in's charge transfers to the integration capacitor (C_int)
4. The net charge transfer per clock cycle mimics current flow through a resistor
5. The filter's cutoff frequency is determined by the ratio of capacitors AND the clock frequency

**Key advantage for DAC use:** The filter's cutoff frequency scales proportionally with the clock frequency. Since the SCF clock is derived from the master clock (which is already locked to the audio sample rate), the filter automatically tracks the sample rate. This eliminates the need for precision resistors and makes the filter inherently sample-rate-independent -- a major advantage for a DAC that supports 8-50 kHz sample rates.

The typical clock-to-cutoff ratio for an audio SCF is **50:1 to 100:1**. With the AK4309's master clock at 16.9344 MHz, the SCF clock could be the master clock itself or a division of it. At 100:1, this would give a cutoff around 169 kHz; at 50:1, around 339 kHz. Both are well above the audio band, consistent with a gentle reconstruction rolloff.

**Confidence: HIGH** for SCF operating principle. **MEDIUM** for specific AK4309 SCF clock and cutoff.

### Frequency response characteristics of the AK4309's SCF

The "2nd order SCF" most likely implements a **2nd-order Butterworth or Bessel lowpass**. Industry practice in the 1990s:

- **Butterworth:** Maximally flat passband, monotonic rolloff. Most common for cost-optimized consumer parts.
- **Bessel:** Best group delay (phase linearity). Preferred for "audiophile" applications but more expensive (requires more precise capacitor ratios).

A 2nd-order lowpass rolls off at **-12 dB/octave** (40 dB/decade) above its cutoff. For a cutoff at approximately 50-100 kHz (typical for an oversampled DAC's SCF), this provides:
- At 22 kHz: essentially flat (0 dB attenuation)
- At 100 kHz: -3 dB (at cutoff)
- At 200 kHz: -15 dB
- At 352.8 kHz: -25 to -30 dB (attenuating the first image from the 8x interpolation)
- Above 1 MHz: heavy attenuation of the PDM bitstream's ultrasonic energy

The SCF does NOT provide a brick-wall filter. The 23.9 kHz alias visible in Stereophile's measurements is evidence of the SCF's gentle rolloff allowing some image energy to pass -- this is expected behavior for a 2nd-order filter.

**Confidence: MEDIUM.** The general behavior is well-understood; the specific cutoff frequency and filter type (Butterworth vs. Bessel) are inferred, not documented.

### SCF-specific artifacts

**Clock feedthrough:** The switching action of the MOSFET switches couples the clock signal into the output. Typical magnitude: approximately 10 mV at the clock frequency. For the AK4309, this would appear at the SCF clock rate (likely 16.9 MHz or a submultiple) -- well above the audio band and easily filtered by the subsequent CTF. **NOT audible.**

**Charge injection:** When MOSFET switches turn off, residual channel charge is dumped onto the capacitors, causing a small voltage error. This manifests as a DC offset and low-level distortion. AKM's datasheet claim of -84 dB THD+N suggests charge injection is well-controlled. Mitigation techniques standard in 1990s silicon: dummy switches, complementary NMOS/PMOS switching, non-overlapping clocks.

**Sampling artifacts:** The SCF is itself a sampled-data system. If its input contains energy above half the SCF clock frequency, aliasing occurs. Since the SCF's input is the 1-bit PDM stream at 16.9 MHz, and the SCF clock is at or near this rate, the aliasing risk is minimal -- the PDM energy IS the signal.

**Confidence: HIGH** for the artifact types (well-established in SC circuit theory). **MEDIUM** for their magnitudes in the AK4309 specifically.

### How the SCF differs from a simple RC lowpass

| Property | RC Lowpass | Switched-Capacitor Filter |
|----------|-----------|--------------------------|
| Cutoff frequency | Fixed by R and C values | Tracks clock frequency (tunable) |
| Sample-rate tracking | None (requires different components per rate) | Automatic (cutoff scales with clock) |
| Precision | Depends on resistor/capacitor tolerance | Depends on capacitor ratios (much more precise in IC fabrication) |
| Power | Passive (zero) | Active (requires op-amps and clock) |
| Clock feedthrough | None | Present (~10 mV, ultrasonic) |
| Noise | Thermal noise from resistors | KT/C noise from switching + op-amp noise |
| Integration | External components needed | Fully on-chip |

The SCF's primary advantage for the AK4309 is that it works across the 8-50 kHz sample rate range without any external component changes.

---

## 6. Idle Tones and Limit Cycles

### Theory

When a delta-sigma modulator receives a constant DC input, the output bitstream becomes periodic. For a 1st-order modulator with input representable as the rational fraction p/q, the output repeats with period q, creating a spectral line (idle tone) at:

```
f_idle = f_mod / q
```

where f_mod is the modulator clock frequency.

For the AK4309 at 16.9344 MHz:
- DC input of exactly zero: output alternates 0101..., period = 2, idle tone at 8.467 MHz (inaudible)
- DC input of 1/3 full scale: period = 3, idle tone at 5.645 MHz (inaudible)
- DC input of 1/1024 (very quiet signal): period = 1024, idle tone at 16.5 kHz (**AUDIBLE**)

### Modulator order effects on idle tones

- **1st-order modulators:** Worst idle tone behavior. Output is strictly periodic for rational DC inputs. The idle tones have full amplitude relative to the quantization noise floor.
- **2nd-order modulators:** Idle tone patterns are less regular and spread across more frequencies. The modulator exhibits "quasi-periodic" behavior rather than strict periodicity. Idle tones still exist but are lower in amplitude and distributed across a wider spectral range.
- **3rd-order and higher:** Idle tone amplitudes are further reduced. The modulator behavior approaches "pseudo-random" for most DC inputs, though specific input values can still trigger limit cycles.

For a 2nd-order modulator (our best estimate for the AK4309), idle tones are present but significantly reduced compared to 1st-order. The key risk zone is **very quiet signals near DC** -- which is exactly what a reverb tail decays into.

### Practical relevance for PS1 audio and reverb tails

A reverb tail decaying toward silence passes through the "danger zone" where idle tones are most prominent: signal levels near integer fractions of the full scale. The question is whether these tones are audible above the modulator's own noise floor.

For a 2nd-order modulator with OSR=384:
- The in-band noise floor is approximately -90 dB
- Idle tones from a 2nd-order modulator are typically 10-20 dB above the noise floor at worst
- This would place idle tones at approximately -70 to -80 dB during the quietest moments of a reverb tail

Whether this is audible depends on context: in isolation (headphones, quiet room), possibly; in a game mix with other audio, almost certainly not.

**Confidence: MEDIUM.** The theory is well-established but the specific behavior of the AK4309's modulator depends on its exact architecture and whether it uses internal dither (see Section 7).

### Idle tone frequencies in the audio band

For a 2nd-order modulator at 16.9 MHz, idle tones that fall in the audio band (20 Hz to 20 kHz) correspond to modulator periods of:

```
q = f_mod / f_idle
q_min = 16,934,400 / 20,000 = 847
q_max = 16,934,400 / 20 = 846,720
```

So any DC input representable as p/q with q between 847 and 846,720 could (in theory) produce an audio-band idle tone. In practice, a 2nd-order modulator does not produce clean tones at these periods -- the energy is spread across multiple spectral lines, and the amplitude decreases with increasing modulator order.

**The most likely audible idle tone scenario:** A reverb tail decaying to a very small positive or negative DC offset (e.g., from accumulated rounding errors in the SPU's fixed-point reverb path). The modulator would produce low-level tonal coloration in the 1-10 kHz range. On real PS1 hardware, this would be masked by the analog output stage's noise floor. In a digital model, it would only be audible if specifically modeled.

---

## 7. Dither in the AK4309

### What we know

The AK4309B datasheet does NOT mention internal dither. This is not unusual for mid-1990s consumer parts -- dither was considered an implementation detail rather than a marketing feature.

### What is likely

**Argument for internal dither:** By the mid-1990s, the idle tone problem in 1-bit delta-sigma DACs was well-understood. Adding rectangular probability density function (RPDF) dither inside the quantizer loop was standard practice -- it requires minimal additional silicon (a simple LFSR) and effectively eliminates idle tones. The required dither amplitude is very small: approximately 0.0013 of full scale peak-to-peak for RPDF, which is 60.7 dB below full TPDF dither. AKM's first delta-sigma DAC was released in 1989; by the AK4309's release (early-to-mid 1990s), they had several years of delta-sigma design experience.

**Argument against:** Some 1990s 1-bit DACs, particularly low-cost consumer parts, omitted dither to save silicon area and power. The AK4309 is positioned as a low-cost multimedia DAC (80 mW, 20-pin SSOP) -- not a premium audio part.

**Best estimate:** The AK4309 **probably** includes minimal internal dither (RPDF), but this is not confirmed. If it does, idle tones are effectively eliminated. If it does not, idle tones are present but at levels below -70 dB (2nd-order modulator at high OSR).

**Dither type, if present:** Almost certainly RPDF (rectangular/uniform distribution). TPDF (triangular) dither cannot be adequately applied inside a 1-bit quantizer loop -- the required amplitude exceeds the quantizer's range. This is a known limitation of 1-bit architectures noted in the AES literature: "adequate amounts of dither cannot be used in the feedback loop" for 1-bit designs. The practical compromise is minimal RPDF dither, which breaks up limit cycles without adding significant noise.

Shaped dither (noise-shaped TPDF) is a more advanced technique that was NOT standard in mid-1990s consumer parts. It is associated with later designs (DSD/SACD era, late 1990s onward).

**Confidence: LOW** for whether the AK4309 includes dither. **MEDIUM** for the dither type being RPDF if present.

---

## 8. True 1-Bit vs. Multi-Level

### The marketing landscape

In the 1990s, "1-bit DAC" was a marketing term used by multiple manufacturers. However, not all "1-bit" DACs were truly 1-bit:

- **Philips TDA1305:** Marketed as "bitstream" but used a 5-bit internal DAC
- **Philips SAA7350:** Truly 384x oversampled 1-bit output
- **Burr-Brown/TI PCM1716:** Multi-bit delta-sigma internally
- **Crystal/Cirrus Logic CS4397:** Multi-bit with DEM (Dynamic Element Matching)

The industry trended toward "multi-bit delta-sigma" after approximately 1995, because multi-bit quantizers (3-level, 5-level, or more) offer:
1. More quantization levels = less quantization noise = lower required OSR
2. Easier dither application (more headroom in the quantizer)
3. Better idle tone suppression
4. The downside (level mismatch) is managed by DEM techniques

### Is the AK4309 truly 1-bit?

**Evidence for truly 1-bit:**
1. The AK4309B datasheet explicitly says "1-bit stereo DAC"
2. The datasheet claims the part "can achieve monotonicity and low distortion with no adjustment" -- the "no adjustment" phrasing is a hallmark of true 1-bit (only two output levels, no need for element matching or calibration)
3. The "superior to traditional R-2R ladder based DACs" positioning emphasizes inherent linearity -- the primary advantage of true 1-bit
4. The AK4309's 90 dB dynamic range is achievable with a true 1-bit quantizer at 384x OSR (see Section 1 calculation)
5. The 1-bit output feeds a switched-capacitor reconstruction filter, which is a natural match for PDM (1-bit) signals
6. AKM's first premium multi-bit DAC (using DWA) was not released until 1998, three years after the AK4309

**Evidence for multi-level (weak):**
1. Some "1-bit" DACs of the era were actually multi-level
2. The AK4309's -84 dB THD+N is modest -- multi-level would easily achieve better

**Assessment: The AK4309 is almost certainly a true 1-bit (2-level) output DAC.** The "no adjustment" claim, the SCF-based reconstruction, and the timeline (pre-DWA era at AKM) all point to genuine 1-bit architecture. A multi-level design would not need to emphasize "no adjustment" and "superior to R-2R" -- those are selling points specific to 1-bit's inherent linearity.

**Modeling implication:** True 1-bit means:
- Perfect output linearity (no DNL/INL artifacts whatsoever)
- No level mismatch artifacts
- No DEM-related noise modulation
- The only conversion artifacts are noise shaping and idle tones
- The 90 dB dynamic range limitation is entirely from the noise-shaping floor, not from analog element matching

**Confidence: MEDIUM-HIGH.** Strong circumstantial evidence, but not explicitly confirmed by a teardown or die photo.

---

## 9. Summary of Implications for Modeling

### What to model (revised based on this research)

| Artifact | Source | Audibility | Model? | How |
|----------|--------|-----------|--------|-----|
| Interpolation filter passband ripple | 8x cascaded half-band FIR, +/-0.5 dB at 20 kHz | PRIMARY coloration | YES | Short IIR or FIR at 44.1 kHz matching measured response |
| Delta-sigma noise floor | 2nd-order noise shaping, ~90 dB DR | Audible on quiet material, reverb tails | YES | 2nd-order HP-shaped noise (not 1st-order as previously proposed) |
| SCF + CTF reconstruction rolloff | 2nd-order Butterworth/Bessel LPF, ~50-100 kHz cutoff | Barely audible (gentle slope in top octave) | MAYBE | Biquad LPF at 44.1 kHz, tuned to measurements |
| ZOH sinc droop | 0.000009 dB at 20 kHz | ZERO | NO | N/A -- negligible at 384x oversampling |
| Idle tones | 2nd-order modulator limit cycles near DC | Possible on reverb tail decay, probably masked | NO (v1.2) | Revisit if hardware captures reveal them |
| Clock feedthrough | SCF switching at ~16.9 MHz | ZERO (ultrasonic) | NO | N/A |
| DNL/INL | Absent (true 1-bit) | ZERO | NO | N/A |
| Charge injection | SCF MOSFET switching | Negligible (part of THD+N spec) | NO | N/A |

### Correction from previous research

The ARCHITECTURE-v1.2.md proposed a 1st-order high-pass noise shaping model. Based on this deeper analysis, the noise should be **2nd-order high-pass shaped** to match the likely 2nd-order modulator. The difference:

| Shaping | Noise spectrum slope | Character |
|---------|---------------------|-----------|
| 1st-order HP | +6 dB/octave | Gentle HF bias |
| 2nd-order HP | +12 dB/octave | Steeper HF bias, quieter at low frequencies |

The 2nd-order shape better represents the actual delta-sigma noise profile and would produce a more accurate noise floor character on reverb tail decay.

### The 384fs finding

The confirmation that the PS1 uses 384fs (not 256fs) is significant because:
1. It means the modulator operates at 16.9 MHz, not 11.3 MHz
2. This gives 50% more oversampling headroom than 256fs
3. The noise floor is pushed further into the ultrasonics
4. Idle tone frequencies are shifted higher (by 1.5x)
5. The overall conversion quality is slightly better than the AK4309 would achieve in 256fs mode

---

## Sources

### Primary (HIGH confidence)

- [firebrandx PSX Digital Audio Mod](https://www.firebrandx.com/psxdigitalaudio.html) -- Confirmed master clock = 16.9344 MHz = 384fs; bit clock = 2.1168 MHz = 48fs; AK4309AVM identification on SCPH-5501
- [GamingDoc TOSLink Optical Out](https://gamingdoc.org/modding/consoles/sony-playstation/audio/toslink-optical-out/) -- Independent confirmation of 384fs master clock and I2S signal format
- [psx-spx Pinouts](https://psx-spx.consoledev.net/pinouts/) -- AK4309VM pin assignments, CKS pin for 256fs/384fs selection
- [Wikipedia: Delta-sigma modulation](https://en.wikipedia.org/wiki/Delta-sigma_modulation) -- NTF formula, SNR formula, idle tone theory, 1-bit vs multi-bit
- [beis.de Delta-Sigma Introduction](https://www.beis.de/Elektronik/DeltaSigma/DeltaSigma.html) -- Modulator order effects on SNR, noise spectrum shape, 5th-order typical for audio

### Secondary (MEDIUM confidence)

- [AK4309B datasheet summary (AllDatasheet)](https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html) -- 1-bit stereo DAC, 8x FIR interpolator, 2nd-order SCF+CTF, 90 dB DR, 384fs/256fs MCLK
- [1-bit DAC Wikipedia](https://en.wikipedia.org/wiki/1-bit_DAC) -- 1-bit linearity advantage, Philips TDA1305 was actually 5-bit, marketing vs. reality
- [Stereophile: PS1 measurements](https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements) -- "Ripple in top three octaves indicates an underspecified digital filter"
- [Archimago PS1 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) -- ~15-bit effective dynamic range, 23.9 kHz alias
- [DSPRelated: Idle tones discussion](https://www.dsprelated.com/thread/6407/tones-in-sigma-delta-modulator-and-dithering) -- Idle tone frequencies, 1st-order periodicity, dither solutions
- [Engineering LibreTexts: Switched-Capacitor Filters](https://eng.libretexts.org/Bookshelves/Electrical_Engineering/Electronics/Operational_Amplifiers_and_Linear_Integrated_Circuits_-_Theory_and_Application_(Fiore)/11:_Active_Filters/11.10:_Switched-Capacitor_Filters) -- R_equiv = 1/(C*f_clk), clock feedthrough ~10 mV, operation principle
- [AKM History / Velvet Sound](https://velvetsound.akm.com/us/en/stories/history/) -- AKM's first delta-sigma DAC in 1989, first premium multi-bit (DWA) in 1998
- [jsgroth PS1 SPU blog](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) -- SPU clock = 44100 Hz = CPU_clock / 768

### Tertiary (LOW confidence -- theoretical inference)

- Modulator order inference (2nd-order) from SNR calculation -- needs validation against actual silicon
- Dither presence/absence -- not documented, inferred from era and market positioning
- Interpolation filter cascade architecture -- inferred from standard practice, not confirmed by datasheet
- SCF filter type (Butterworth vs. Bessel) -- inferred from industry norms
- Idle tone audibility on reverb tails -- theoretical, no PS1-specific measurement exists

---

*Deep delta-sigma research for: SPU-94 DAC modeling, v1.2 milestone*
*Researched: 2026-04-28*
