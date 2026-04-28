# PS1 Audio Output: Published Measurements

**Purpose:** Calibration reference for libspu94 DAC model (Milestone 3)
**Researched:** 2026-04-28
**Overall confidence:** MEDIUM -- two professional measurement sources exist (Stereophile, Archimago), but both measured CD playback only. No published SPU game-audio measurements found.

---

## Source 1: Stereophile (John Atkinson) -- SCPH-1001

**URL:** https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements
**Date:** July 1997 (magazine), online archive
**Confidence:** HIGH -- professional measurement lab, calibrated equipment, documented methodology
**Limitation:** Stereophile's site blocks scraping; data extracted from search snippets and secondary citations. Some figures are from verbal descriptions of graphs rather than exact readings.

### Test Setup
- **Model:** SCPH-1001, serial U7163475 (first-generation, RCA output jacks)
- **DAC:** AKM AK4309AVM 16-bit sigma-delta
- **Equipment:** Audio Precision System One (Stereophile's standard test bench at the time)
- **Test signal:** Standard Stereophile CD test disc, 16-bit/44.1kHz
- **Load:** 100k ohms (standard high-impedance measurement load)
- **Signal path:** CD playback through analog RCA outputs

### Extracted Measurements

| Parameter | Value | Notes |
|-----------|-------|-------|
| Maximum output level | 1.09V RMS @ 1kHz | >5dB below CD standard 2V |
| Frequency response | Ripple in top 3 octaves (~5kHz-20kHz) | "underspecified digital filter" |
| Pre-emphasis handling | Identical to non-emphasized data | Good: no pre-emphasis error |
| Channel separation (<1kHz) | >90dB | Excellent |
| Channel separation (20kHz) | 72dB | Adequate, typical crosstalk increase at HF |
| THD @ 0dBFS, 1kHz | Low-level 2nd and 3rd harmonics visible | "subjectively innocuous"; no exact % given in available text |
| Noise floor | 15dB higher than good budget CD player | Compared to Onkyo DX-7555 |
| Noise floor artifacts | Peaks at 60Hz and 180Hz | Magnetically induced hum from AC transformer |
| Noise floor character | "Rather granular in appearance" | Suggests delta-sigma quantization noise visible |
| Effective resolution | ~14 bits | Atkinson's estimate from linearity/noise analysis |
| Jitter (Dunn-Miller J-Test) | 737 picoseconds peak-peak | Moderate; sidebands at 11.025kHz +/-230Hz and +/-60Hz |
| Linearity error | Noted at low levels | "low-level compression" from linearity error |
| Dynamic range | Not explicitly stated | Implied ~84dB from 14-bit estimate (6.02*14 = 84.3dB) |

### Key Observations from Atkinson
- "A pretty poor set of measurements"
- The PS1's appeal is that it "smears over and disguises much of what is wrong with typical CD sound quality"
- Frequency response ripple in top 3 octaves = underspecified digital reconstruction filter
- The ripple "will tend to smear time-domain information" (i.e., impulse response ringing/dispersion)
- Low-level linearity error creates a subtle compression effect

### What This Tells Us for DAC Modeling
- The AK4309's on-chip digital filter is the primary source of the frequency response character
- The HF ripple is a digital filter artifact, not an analog rolloff
- The noise floor includes both DAC quantization noise and analog-domain hum (60/180Hz)
- The 1.09V output (vs 3.4Vpp DAC spec = ~1.2V RMS) suggests the output stage attenuates slightly
- Jitter sidebands at data-related frequencies indicate clock recovery imperfections

---

## Source 2: Archimago's Musings -- SCPH-5501

**URL:** http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html
**Date:** March 2013
**Confidence:** MEDIUM-HIGH -- hobbyist with semi-pro equipment, documented methodology, well-regarded in audio measurement community
**Limitation:** Uses consumer-grade measurement interface rather than Audio Precision gear. Numerical values often described qualitatively with graphs rather than tables.

### Test Setup
- **Model:** SCPH-5501 (later revision, no RCA jacks -- uses AV Multi output)
- **DAC:** AKM AK4309AVM (same chip as SCPH-1001)
- **Equipment:** E-MU 0404 USB audio interface as ADC, AMD Windows laptop
- **Software:** RightMark Audio Analyzer (RMAA)
- **Test:** 16-bit/44.1kHz CD playback through stock AV Multi-to-RCA cable
- **Signal path:** CD playback through analog outputs

### Extracted Measurements

| Parameter | Value | Notes |
|-----------|-------|-------|
| Dynamic range | ~90dB (~15-bit) | Matches AK4309 spec sheet |
| Frequency response | Slight deviance above 3kHz | "unlikely to be noticeable through speakers" |
| THD | "Respectable" | Lower distortion than TDA1543 NOS DAC comparison |
| THD above 10kHz | "Cleaner graph notably above 10kHz" | Compared to NOS DAC; delta-sigma advantage in HF distortion |
| Jitter (J-Test sidebands) | <-100dB below 11kHz primary | "Looks fine" |
| Noise floor | Inferior to modern DACs | Expected given 90dB dynamic range spec |

### Key Observations from Archimago
- "Capable of around 15-bit dynamic range" -- slightly more generous than Stereophile's 14-bit estimate
- Performance matches the AK4309 datasheet spec of 90dB dynamic range
- THD and IMD "respectable" -- not state-of-art but functional
- Jitter "not of concern" -- sidebands well below audible threshold
- Frequency response: "some slight deviance from flat above 3kHz" -- consistent with Stereophile's "ripple in top 3 octaves"

### Discrepancy with Stereophile
- Archimago estimates ~15-bit / 90dB; Stereophile estimates ~14-bit / ~84dB
- Possible explanations: different PS1 model (SCPH-5501 vs SCPH-1001), different measurement equipment resolution, different test methodology (RMAA vs Audio Precision), analog output path differences (AV Multi vs RCA)
- The AK4309 chip is the same in both units; difference likely in analog output stage or measurement floor

---

## Source 3: AK4309/AK4309B Datasheet Specifications

**URLs:**
- https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html
- https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html
- https://elcodis.com/parts/6259223/AK4309.html
**Confidence:** HIGH (manufacturer specifications), but note: the AK4309AVM-specific datasheet is lost; these are AK4309/AK4309B general specs
**Limitation:** Full 14-page PDF not fully accessible; specs extracted from summary pages and third-party caches

### Specifications

| Parameter | Spec | Notes |
|-----------|------|-------|
| Resolution | 16-bit input | 1-bit delta-sigma internal |
| Architecture | Delta-sigma with SCF post-filter | Switched Capacitor Filter + Continuous Time Filter |
| Dynamic range | 90dB | Typical; this is the headline spec |
| THD+N | -84dB | Typical at 1kHz |
| Frequency response | +/-0.5dB at 20kHz | Total system response including digital filter |
| Digital filter | 8x FIR interpolator | Oversamples 44.1kHz to 352.8kHz |
| Analog post-filter | 2nd-order SCF + CTF | On-chip; no external components needed |
| Output level | 3.4Vpp | Full-scale differential; single-ended ~1.2V RMS |
| Master clock | 256fs or 384fs | PS1 uses 384fs (16.9344MHz) per FirebrandX SPDIF mod data |
| Sampling rate range | 8kHz-50kHz | PS1 operates at 44.1kHz |
| Power supply | 5V +/-10% | |
| Power dissipation | 80mW @ 5V | |
| Package | 20-pin SSOP (AK4309B) / 24-pin (AK4309AVM) | |
| Jitter tolerance | "High tolerance to clock jitter" | SCF technique reduces jitter sensitivity |

### Filter Architecture (Critical for DAC Model)
1. **8x FIR interpolator:** Upsamples from 44.1kHz to 352.8kHz. This is the "digital filter" Stereophile called "underspecified." The +/-0.5dB at 20kHz spec means the passband is not perfectly flat -- there is allowed ripple up to 0.5dB.
2. **2nd-order SCF:** Switched capacitor filter at the oversampled rate. Provides initial analog smoothing.
3. **Continuous Time Filter (CTF):** Final analog low-pass, removes residual high-frequency noise from delta-sigma modulation.

### What "+/-0.5dB at 20kHz" Means
- The FIR filter passband has up to 0.5dB of ripple
- This is consistent with Stereophile's "ripple in the top 3 octaves"
- A well-designed modern DAC targets +/-0.01dB; +/-0.5dB is "underspecified" by modern standards but was typical for 1994-era multimedia DACs
- The ripple creates the "smeared time-domain information" Stereophile noted

---

## Source 4: Dogbreath.de (Mick Feuerbacher) -- SCPH-1002

**URLs:**
- https://dogbreath.de/PS1/DAC/DAC.html (DAC analysis, Feb 2007)
- https://dogbreath.de/PS1/output/output.html (Output stage mods, Dec 2005)
- https://dogbreath.de/PS1/ (Index page)
**Confidence:** MEDIUM -- detailed hardware analysis by electronics-literate audiophile, but no instrument measurements (oscilloscope/spectrum analyzer). All conclusions are from circuit analysis and subjective listening.
**Limitation:** Qualitative only. No frequency response plots, no spectral data, no THD measurements.

### Technical Findings

**DAC Identification:**
- SCPH-100x models: AKM AK4309AVM (24-pin, confirmed)
- SCPH-55xx models: Also AK4309AVM
- SCPH-700x models: AK4309BM (20-pin, compatible successor)
- SCPH-75xx and later: DAC integrated into CD/DSP SoC (no discrete DAC)

**Output Stage Circuit (SCPH-100x):**
- DAC outputs on pins 15 (L) and 16 (R)
- NJM2100 op-amp buffers for RCA output path
- DC blocking capacitors in signal path
- 1k ohm and 100 ohm output resistors
- Muting transistors in signal path (for power-on/off click suppression)
- Separate parallel path for AV Multi output

**Frequency Response Effects of Output Stage:**
- DC blocking caps create a high-pass filter
- With stock values, bass rolloff is noted ("remarkable bass rolloff")
- Feuerbacher's mod: 3.3uF Wima polyester + 22k resistor = ~2.2Hz corner frequency
- With 22k load impedance (typical preamp input), corner shifts to ~4.4Hz
- Muting transistor removal claimed to improve "veil" and dynamics

**Key Insight for DAC Modeling:**
- The analog output stage contributes bass rolloff (high-pass from DC blocking caps)
- The muting transistor adds series resistance/nonlinearity
- The NJM2100 op-amp has its own bandwidth/slew-rate characteristics
- These are all *analog domain* effects layered on top of the DAC's digital filter character

---

## Source 5: FirebrandX -- PS1 Digital Audio (SPDIF) Mod

**URL:** https://www.firebrandx.com/psxdigitalaudio.html
**Confidence:** MEDIUM -- detailed mod guide with signal identification, but no comparative measurements
**Limitation:** Subjective quality comparison only ("digital audio will almost always sound considerably more clean")

### Technical Findings

**I2S Bus Signals Tapped (Pre-DAC):**
| Signal | Frequency | Purpose |
|--------|-----------|---------|
| SDATA (DAT) | Serial | 16-bit audio data |
| BICK (BC) | 2.1168 MHz | Bit clock (48x sample rate) |
| LRCK (LRC) | 44.1 kHz | Left/right channel select |
| MCLK (MC) | 16.9344 MHz | Master clock = 384fs |

**Key Finding:** The PS1 uses 384fs master clock (not 256fs). This means the AK4309 operates in 384fs mode, where MCLK = 384 * 44100 = 16,934,400 Hz. This affects the SCF filter timing.

**Model Compatibility:**
- Works on models with discrete AK4309 DAC (SCPH-1001 through ~SCPH-5501)
- SCPH-750x and later: DAC integrated into SoC, no discrete I2S bus to tap

**Implication for DAC Modeling:**
- The SPDIF mod captures the pure digital signal before the DAC
- This means the "PS1 sound character" from the DAC onwards is: 8x FIR interpolation + delta-sigma modulation + SCF + CTF + analog output stage
- Anyone comparing SPDIF output to analog output is hearing exactly the DAC coloration we want to model

---

## Source 6: ConsoleMods Wiki / PS1 Community

**URL:** https://consolemods.org/wiki/PS1:Audio_Information
**Confidence:** MEDIUM -- community wiki, cross-referenced with multiple sources

### Model-Specific DAC Information

| Model(s) | Board | SPU Chip | DAC Chip | Notes |
|-----------|-------|----------|----------|-------|
| SCPH-1000/1001/1002 | PU-7, PU-8 | CXD2922Q | AK4309AVM | "Audiophile" models, RCA jacks (1001 only) |
| SCPH-3000/3500 | PU-8 variants | CXD2925Q | AK4309AVM | Same DAC, different SPU revision |
| SCPH-5001/5501/5502 | PU-8 late / PU-18 | CXD2925Q | AK4309AVM | Same DAC, AV Multi output only |
| SCPH-7001/7501 | PU-22 | CXD2925Q | AK4309BM | 20-pin variant, compatible |
| SCPH-9001 | PU-23 | Integrated | Integrated | DAC inside SoC, not moddable |
| PSone (SCPH-101) | PM-41 | Integrated | Integrated | Smallest form factor, integrated everything |

### CD vs SPU Audio Path
Both CD audio and SPU game audio feed through the same DAC. The SPU mixes CD audio (received via I2S input from CD DSP) with its 24 voice channels internally, then outputs a single stereo I2S stream to the DAC.

**Critical implication:** There is NO separate CD-only analog path on early PS1 models. ALL audio -- CD-DA and game SPU -- goes through: SPU mixer -> I2S -> AK4309 DAC -> analog output stage.

---

## Source 7: emu-russia/psxrev (PS1 Reverse Engineering)

**URL:** https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
**Confidence:** HIGH -- reverse-engineered from silicon analysis

### SPU Output Signal Chain
1. 24 voice channels (ADPCM decode -> pitch interpolation -> ADSR envelope -> L/R volume)
2. Mix bus: sum of all voices (16-bit signed, hard clip on overflow)
3. CD audio input: mixed into sum (via DTIA/LRIA/BCIA I2S input pins)
4. Reverb processor: parallel path from mix bus
5. Master volume applied
6. Output I2S: DATO (data), LRCO (L/R clock), BCKO (bit clock) -> to DAC

**Output pins to DAC:**
- DATO: Serial audio data (16-bit signed, 2's complement, MSB first)
- LRCO: Left/right clock at 44.1kHz
- BCKO: Bit clock
- XCK: Master clock forwarded to DAC

### SPU Internal Sample Format
- All internal processing: 16-bit signed integers
- Sample rate: 44.1kHz (fixed)
- Mix bus: voices summed with saturation (hard clip at +/-32767)
- Reverb buffer: 16-bit signed samples in SPU RAM

---

## Source 8: MiSTer FPGA PS1 Core

**URL:** https://mister-devel.github.io/MkDocs_MiSTer/cores/highlights/psx/
**Confidence:** LOW-MEDIUM -- no published audio measurement comparisons found

### What Is Known
- SPU implementation by Robert Peip is "feature complete"
- Dual SDRAM build provides "measurably more accurate" audio samples
- Single SDRAM build may drop ~1-2 samples per 44100 per second (inaudible)
- Core timing is not 100% cycle-accurate due to DDR3 latency (affects GPU more than SPU)
- Community has done waveform-level comparisons for other console cores

### What Is NOT Known
- No published spectral analysis comparing MiSTer PSX audio to real hardware
- No frequency response comparison
- No THD/noise measurements of MiSTer digital output vs PS1 analog output
- No public documentation of which audio behaviors differ from real hardware

### Implication for DAC Modeling
MiSTer's PSX core outputs a digital audio stream that represents "what the SPU produces before the DAC." In theory, capturing MiSTer's audio output gives us the pre-DAC reference signal. However, without published comparisons confirming MiSTer SPU accuracy, this is an unvalidated reference.

---

## NO SOURCES FOUND: SPU Game Audio Measurements

**Exhaustive search conducted. No published measurements of PS1 GAME audio quality exist.**

All measurement sources (Stereophile, Archimago, cheaptubeaudio, diyaudio community) tested CD playback only. Nobody has published frequency response, THD, or spectral analysis of PS1 SPU game audio output.

### Why This Matters
- CD playback tests the DAC + analog output stage only
- Game audio tests SPU ADPCM decoding + SPU mixer + reverb + DAC + analog output
- The SPU's internal processing (ADPCM artifacts, mixer saturation, reverb coloration) is NOT captured in any published measurement
- The CD audio path goes through the SPU mixer, so the DAC measurements ARE relevant, but SPU-specific coloration is untested

### Recommended Action
This is a gap that Anthony's hardware (original PSX) could fill in Milestone 5. A simple test: play a known WAV through SPU (via homebrew) and capture analog output with measurement equipment. Compare to the same WAV played as CD-DA.

---

## Summary Comparison Table

| Parameter | AK4309 Spec | Stereophile (SCPH-1001) | Archimago (SCPH-5501) | Notes |
|-----------|-------------|-------------------------|----------------------|-------|
| Dynamic range | 90dB | ~84dB (14-bit est.) | ~90dB (15-bit est.) | Discrepancy likely from measurement methodology |
| THD+N | -84dB | Low H2/H3 visible | "Respectable" | Neither source gives exact number |
| Freq response @ 20kHz | +/-0.5dB | Ripple in top 3 octaves | Slight deviance >3kHz | Consistent: HF ripple from FIR filter |
| Max output level | 3.4Vpp (~1.2V RMS) | 1.09V RMS | Not stated | 1.09V suggests ~10% loss in output stage |
| Channel separation (<1kHz) | Not specified | >90dB | Not stated | Excellent |
| Channel separation (20kHz) | Not specified | 72dB | Not stated | Typical crosstalk increase |
| Jitter | "High tolerance" | 737ps p-p | <-100dB sidebands | Moderate but not problematic |
| Noise floor character | Delta-sigma shaped | "Granular" + 60/180Hz hum | Inferior to modern | Hum is analog-domain, not DAC |
| Effective resolution | 16-bit nominal | ~14 bits | ~15 bits | Analog stage degrades DAC performance |

---

## Consensus Frequency Response Shape

Based on all sources:

1. **Below 3kHz:** Essentially flat. No significant deviation reported by any source.
2. **3kHz-5kHz:** Slight deviation begins. Archimago notes "above 3kHz." This is where the 8x FIR filter's passband ripple becomes noticeable.
3. **5kHz-20kHz:** Ripple in the "top 3 octaves" (Stereophile). The AK4309 spec allows +/-0.5dB at 20kHz. The ripple is NOT a smooth rolloff -- it is oscillatory, characteristic of an FIR filter with insufficient tap count.
4. **Bass:** Flat from the DAC itself. Any bass rolloff is from the DC blocking capacitors in the analog output stage (dogbreath.de data), not the DAC. Corner frequency depends on cap values and load impedance.

### Expected Filter Shape (Theoretical)
The AK4309's 8x FIR interpolator is a linear-phase filter with:
- Passband: 0-20kHz, +/-0.5dB ripple
- Transition band: 20kHz-24.1kHz (Nyquist - passband edge at 8x rate)
- Stopband: >24.1kHz (images at 44.1kHz and multiples, attenuated)

The passband ripple creates the characteristic "PS1 sound" in the treble: not a rolloff, but a series of small peaks and dips that smear transients.

After the FIR, the 2nd-order SCF + CTF provide analog smoothing. Being only 2nd order, these contribute:
- Gentle rolloff above the transition band
- No significant passband effect (already handled by FIR)

### ZOH Droop
The delta-sigma 1-bit output has a zero-order hold (ZOH) characteristic at the oversampled rate (352.8kHz), NOT at 44.1kHz. The ZOH droop at 20kHz relative to 352.8kHz is negligible (<0.1dB). This is NOT a significant contributor to the PS1's HF character.

---

## Noise Floor Analysis

### Character
- **Delta-sigma noise shaping:** Quantization noise is pushed to ultrasonic frequencies by the delta-sigma modulator. The on-chip SCF+CTF filters this, but residual shaped noise remains.
- **In-band noise:** Higher than modern DACs by ~15dB (Stereophile comparison). The 90dB dynamic range spec means the noise floor sits around -90dBFS.
- **Hum components:** 60Hz fundamental and 180Hz 3rd harmonic from magnetic coupling with the internal AC transformer. These are analog-domain artifacts.
- **Appearance:** "Granular" (Stereophile) -- not smooth white noise, suggesting the delta-sigma noise shaping creates a textured noise floor.

### For DAC Modeling
- Model the noise floor as approximately -90dBFS broadband
- Add shaped components: slight rise toward higher frequencies (delta-sigma residual)
- 60Hz and 180Hz hum components are console-specific, NOT DAC-specific. Model them separately if at all (they vary by PS1 unit and physical environment).
- The noise floor contributes to the "15-bit" effective resolution: the bottom 1-2 bits are buried in noise.

---

## Measurement Artifacts to Separate from "PS1 Character"

| Artifact | Source | PS1 Character? | Model It? |
|----------|--------|----------------|-----------|
| HF passband ripple (+/-0.5dB) | AK4309 FIR filter | YES -- core of DAC sound | YES |
| 60/180Hz hum | AC transformer magnetic coupling | NO -- varies by unit | OPTIONAL (parameter) |
| Granular noise floor (~-90dBFS) | Delta-sigma quantization residual | YES -- inherent to architecture | YES |
| Bass rolloff below ~5Hz | DC blocking capacitors | PARTIALLY -- component-dependent | OPTIONAL (parameter) |
| 1.09V output (vs 2V standard) | Output stage attenuation | NO -- gain staging only | NO (just a level) |
| Jitter sidebands at 11.025kHz | Clock recovery from CD mechanism | NO -- CD transport artifact | NO |
| Low-level compression (linearity) | DAC nonlinearity at low levels | YES -- inherent to DAC | YES (subtle) |

---

## Comparison: Measured PS1 vs AK4309 Theoretical Specs

| Parameter | AK4309 Spec | Best PS1 Measurement | Degradation | Likely Cause |
|-----------|-------------|---------------------|-------------|--------------|
| Dynamic range | 90dB | 84-90dB | 0-6dB | Analog output stage noise, measurement equipment |
| THD+N | -84dB | Not exactly measured | Unknown | Output stage adds some distortion |
| Freq response | +/-0.5dB @ 20kHz | "Ripple in top 3 octaves" | Consistent | This IS the spec behavior, not degradation |
| Output level | 3.4Vpp (1.2V RMS) | 1.09V RMS | ~0.8dB | Output resistor divider + op-amp losses |

**Conclusion:** The PS1's analog output stage degrades the AK4309's native performance by 0-6dB in dynamic range (adding noise from the power supply and op-amp). The frequency response character is dominated by the DAC's own digital filter, not the analog output stage. The analog stage's primary contribution is the bass high-pass from DC blocking caps and additional broadband noise.

---

## CD Playback vs Game Audio: The Critical Gap

### What We Know (from circuit analysis)
- CD audio enters the SPU via I2S input from the CD DSP chip
- SPU mixes CD audio with its 24 voice channels
- All audio exits the SPU as a single I2S stream to the DAC
- The DAC sees the same format regardless of source: 16-bit, 44.1kHz, I2S

### What This Means
- CD-only playback (no game running): SPU passes CD audio through its mixer at unity gain
- Game audio: SPU decodes ADPCM voices, applies reverb, mixes with optional CD audio
- Both go through the same DAC and analog path
- Therefore: DAC measurements from CD playback ARE valid for the DAC model
- SPU-specific coloration (ADPCM artifacts, reverb character, mixer saturation) is SEPARATE from DAC coloration

### The Gap
No one has measured SPU game audio output with test signals. We cannot confirm:
- Whether the SPU's mixer introduces additional noise or distortion
- Whether the ADPCM encode/decode path degrades the signal beyond the expected codec artifacts
- Whether the reverb processor affects the noise floor
- Whether the SPU's internal processing changes the effective dynamic range

**This is explicitly documented as an open research gap.**

---

## Design Targets for DAC Model

Based on all evidence gathered, the recommended design targets for the libspu94 DAC model:

| Parameter | Target | Source | Priority |
|-----------|--------|--------|----------|
| Passband ripple | +/-0.5dB, concentrated above 5kHz | AK4309 spec, Stereophile confirmation | HIGH |
| Filter type | Linear-phase FIR, 8x oversampling | AK4309 spec | HIGH |
| Post-filter | 2nd-order low-pass (SCF+CTF equivalent) | AK4309 spec | MEDIUM |
| Noise floor | ~-90dBFS broadband | AK4309 spec, Archimago measurement | MEDIUM |
| Noise shape | Slight HF rise (delta-sigma residual) | Theoretical, Stereophile "granular" | LOW |
| Low-level nonlinearity | Subtle compression below -60dBFS | Stereophile observation | LOW |
| DC blocking HP filter | ~2-5Hz corner frequency | Dogbreath.de circuit analysis | LOW |
| Output level | Unity reference (not modeling gain) | N/A | N/A |

### What NOT to Model (initially)
- 60/180Hz hum (unit-specific, environment-dependent)
- Jitter (clock recovery artifact, not DAC character)
- Muting transistor effects (would be removed on modded units)
- Op-amp bandwidth limitations (above audible range)

---

## Sources Index

| Source | URL | Type | Confidence |
|--------|-----|------|------------|
| Stereophile measurements | https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements | Professional measurement | HIGH |
| Archimago measurements | http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html | Semi-pro measurement | MEDIUM-HIGH |
| AK4309 datasheet | https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html | Manufacturer spec | HIGH |
| AK4309B datasheet | https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html | Manufacturer spec | HIGH |
| Dogbreath.de DAC | https://dogbreath.de/PS1/DAC/DAC.html | Hardware analysis | MEDIUM |
| Dogbreath.de output | https://dogbreath.de/PS1/output/output.html | Circuit analysis | MEDIUM |
| FirebrandX SPDIF mod | https://www.firebrandx.com/psxdigitalaudio.html | Mod guide w/ signal data | MEDIUM |
| ConsoleMods PS1 audio | https://consolemods.org/wiki/PS1:Audio_Information | Community wiki | MEDIUM |
| emu-russia SPU reverse eng | https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md | Silicon analysis | HIGH |
| MiSTer PSX docs | https://mister-devel.github.io/MkDocs_MiSTer/cores/highlights/psx/ | FPGA core docs | LOW-MEDIUM |
| Cheaptubeaudio review | https://cheaptubeaudio.blogspot.com/2012/07/review-sony-playstation-1.html | Subjective review | LOW |
| Stereophile spec page | https://www.stereophile.com/content/sony-playstation-1-cd-player-specifications | Specs listing | HIGH |
| diyaudio PS1 thread | https://www.diyaudio.com/community/threads/playstation-as-cd-player.31123/ | Community discussion | LOW |
| Head-Fi extreme mod | https://www.head-fi.org/threads/the-sony-ps1-scph-1001-extreme-mod-thread.340509/ | Mod community | LOW |
