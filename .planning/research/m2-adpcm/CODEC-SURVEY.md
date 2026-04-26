# Historical Codec Algorithm Survey for Creative Audio Exploitation

**Project:** SPU-94 / Digital Patina Engine
**Researched:** 2026-04-26
**Purpose:** Technical survey of historically interesting codecs for faithful reimplementation as creative audio effects
**Overall Confidence:** HIGH for ADPCM-family codecs, MEDIUM for transform codecs, HIGH for companding

---

## Table of Contents

1. [SNES BRR (Bit Rate Reduction)](#1-snes-brr-bit-rate-reduction)
2. [IMA-ADPCM](#2-ima-adpcm)
3. [mu-law and A-law Companding (G.711)](#3-mu-law-and-a-law-companding-g711)
4. [Sony ATRAC (MiniDisc)](#4-sony-atrac-minidisc)
5. [MPEG Audio Layer 2 (MP2)](#5-mpeg-audio-layer-2-mp2)
6. [GSM 06.10 Full Rate](#6-gsm-0610-full-rate)
7. [Sega Saturn / Dreamcast ADPCM (Yamaha AICA)](#7-sega-saturn--dreamcast-adpcm-yamaha-aica)
8. [CVSD (Continuously Variable Slope Delta Modulation)](#8-cvsd-continuously-variable-slope-delta-modulation)
9. [Other Historically Interesting Codecs](#9-other-historically-interesting-codecs)
10. [Comparative Summary](#10-comparative-summary)
11. [Implementation Priority Recommendations](#11-implementation-priority-recommendations)

---

## 1. SNES BRR (Bit Rate Reduction)

**Confidence:** HIGH -- well-documented by emulator community (SnesLab, nesdev, fullsnes)

### Algorithm Overview

BRR is a 4-bit ADPCM variant used by the Sony SPC700 S-DSP in the Super Nintendo. It is the closest sibling to PS1 SPU-ADPCM -- both are Sony designs, both are 4-bit predictive codecs with selectable filters. BRR is the predecessor; SPU-ADPCM is the evolution.

**Structure:** Each block is 9 bytes encoding 16 PCM samples (compression ratio 32:9, or ~3.56:1).

| Byte | Content |
|------|---------|
| 0 | Header: `ssssffle` -- 4-bit shift, 2-bit filter, loop flag, end flag |
| 1-8 | 8 bytes of packed 4-bit nibbles (high nibble first per byte) |

Compare to PS1 SPU-ADPCM: 16 bytes encoding 28 samples (32:16, or 2:1). PS1 gets more samples per block and has a 5th filter. BRR is more compressed, with fewer filters and fewer samples per block.

### Key Parameters

**Shift (0-12 valid, 13-15 special):**
- Values 0-12: `sample = (nibble << shift) >> 1`
- Values 13-15: `sample = (nibble >> 3) << 11` -- effectively only the sign bit survives, producing -2048 or 0. This is a hardware quirk, not a useful encoding mode.

**Four Filters (vs PS1's five):**

| Filter | Coefficient a (prev) | Coefficient b (prev-1) | Fixed-point |
|--------|----------------------|------------------------|-------------|
| 0 | 0 | 0 | Direct |
| 1 | 15/16 (0.9375) | 0 | 1st-order |
| 2 | 61/32 (1.90625) | -15/16 (-0.9375) | 2nd-order |
| 3 | 115/64 (1.796875) | -13/16 (-0.8125) | 2nd-order |

PS1 SPU-ADPCM has these same four filters PLUS filter 4 (122/64, -60/64). The 5th filter gives PS1 slightly more prediction accuracy on certain waveforms.

### The 15-bit Clamp Bug

This is the most sonically distinctive hardware quirk of the SNES S-DSP:

1. The BRR decoder outputs a 16-bit value
2. This value is then **clamped to 15-bit signed range** (-16384 to +16383, i.e., -0x4000 to +0x3FFF)
3. If the value exceeds this range, it **wraps** (overflows) to the opposite side ONCE
4. After one wrap, it clips instead of wrapping again

The wrap formula: `((sample & 0x7FFF) ^ 0x4000) - 0x4000`

This creates a distinctive harsh distortion on loud samples -- not a clean clip, but a sign-flip that produces an instantaneous phase inversion. This is one of the defining characteristics of the "SNES sound."

Additionally, the Gaussian interpolation stage has its own 15-bit wrapping behavior on the first 3 of 4 interpolation taps, adding further coloration.

### Characteristic Artifacts

- **15-bit wrap distortion:** Harsh, asymmetric clipping with phase-inversion character on loud transients. Sounds "crunchy" in a way that's different from both clean clipping and PS1 ADPCM.
- **Coarser prediction:** With only 4 filters (vs PS1's 5), BRR produces slightly more prediction error on complex waveforms, especially high-frequency content with irregular periodicity.
- **Smaller block = less prediction context:** 16 samples per block vs PS1's 28 means filter state resets more frequently, producing more "blocky" artifacts at block boundaries.
- **Gaussian interpolation smear:** The S-DSP's fixed Gaussian interpolation filter applies a ~16 kHz lowpass that softens transients and rolls off high frequencies. This is part of the "warm SNES sound" but is technically a separate DSP stage from BRR itself.
- **Signal-correlated noise:** Like all ADPCM variants, quantization noise is correlated with the input signal. Quiet passages are cleaner; loud complex passages get noisier.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Filter selection override | Force filter 0 (no prediction) | Raw 4-bit quantization noise, very gritty |
| Shift override | Force low shift values | Massive quantization, bit-crushed character |
| 15-bit clamp enable/disable | Toggle the wrap bug | With = SNES authentic; without = cleaner |
| Block size (if variable) | Shorter blocks = more artifacts | More frequent prediction resets |
| Gaussian interpolation enable | Apply the SNES LPF character | Softens transients, rolls off highs |

### Implementation Complexity

- **Code size:** ~150-200 lines of C for encoder+decoder
- **State:** 4 bytes (two int16 previous samples) for decoder, same for encoder
- **Tables:** Filter coefficient table (4 entries x 2 coefficients = 8 values)
- **Heap:** Zero -- trivially stack/struct allocated
- **Real-time:** Absolutely feasible. Simpler than PS1 ADPCM (fewer filters, smaller blocks).
- **Difficulty:** LOW. If you can implement PS1 ADPCM, BRR is a subset.

### Licensing / Documentation

- **Specs:** Freely documented by SnesLab, fullsnes (nocash), nesdev community
- **Patents:** Expired (SNES released 1990)
- **Reference implementations:** Multiple open-source BRR encoders/decoders (snesbrr by mukunda, boldowa)
- **No legal concerns whatsoever**

### Sources

- [SnesLab BRR Documentation](https://sneslab.net/wiki/Bit_Rate_Reduction)
- [Super Famicom Development Wiki](https://wiki.superfamicom.org/bit-rate-reduction-(brr))
- [nesdev forums BRR discussions](https://forums.nesdev.org/viewtopic.php?t=5737)
- [SNESdev Errata (S-DSP bugs)](https://snes.nesdev.org/wiki/Errata)

---

## 2. IMA-ADPCM

**Confidence:** HIGH -- ITU/IMA standardized, extensively documented

### Algorithm Overview

IMA-ADPCM (also called DVI-ADPCM or Intel DVI) is a 4-bit adaptive differential PCM codec standardized by the Interactive Multimedia Association. It is the "generic ADPCM" of the PC era -- found in WAV files, Windows, PC games from the early-to-mid 1990s, and embedded systems.

**Critical difference from PS1 ADPCM:** IMA-ADPCM has NO prediction filters. It uses only a step-size adaptation mechanism. The "prediction" is simply the previous output sample (zero-order hold). PS1 ADPCM uses 2nd-order IIR prediction filters that model the signal's spectral shape; IMA-ADPCM does not.

**Encoding process per sample:**
1. Compute difference: `diff = input - predictor`
2. Quantize diff to 4-bit nibble using current step size
3. Update predictor by reconstructing the quantized difference
4. Update step index using the index table based on the nibble value

### Key Parameters

**Step Size Table:** 89 fixed values forming a roughly exponential curve:
```
7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
18500, 20350, 22385, 24623, 27086, 29794, 32767
```

**Index Adaptation Table** (16 entries, symmetric for positive/negative nibbles):
```
-1, -1, -1, -1, 2, 4, 6, 8,
-1, -1, -1, -1, 2, 4, 6, 8
```

**Decode per nibble:**
```c
step = step_table[step_index];
diff = step >> 3;                    // start with step/8
if (nibble & 4) diff += step;        // add step
if (nibble & 2) diff += step >> 1;   // add step/2
if (nibble & 1) diff += step >> 2;   // add step/4
if (nibble & 8) diff = -diff;        // sign bit
predictor += diff;
predictor = clamp(predictor, -32768, 32767);
step_index += index_table[nibble];
step_index = clamp(step_index, 0, 88);
```

**State:** Only 23 bits total -- 16-bit predictor + 7-bit step index. This is remarkably minimal.

### Characteristic Artifacts

- **Grainy, "buzzy" quantization noise:** Without prediction filters to shape the noise spectrum, IMA-ADPCM's quantization error is spectrally flat relative to the signal. PS1 ADPCM's filters push noise energy into frequency ranges where the signal has less energy; IMA does not.
- **Step-size hunting artifacts:** When the signal changes character rapidly (e.g., a transient after silence), the step size must ramp up from minimum, producing a brief "staircase" effect as it catches up. This manifests as a brief "zipping" or "clicking" on transients.
- **No spectral shaping:** The noise floor rises proportionally across all frequencies. This gives IMA a more "digital" and "raw" quality compared to PS1's filtered ADPCM, which sounds "smoother" even at the same bit depth.
- **Signal-correlated noise:** Yes, strongly. Quiet passages are clean; complex loud passages have audible quantization noise that tracks the signal envelope.
- **Block boundary artifacts (in WAV format):** Microsoft WAV IMA-ADPCM uses blocks with preambles that reset the predictor. Block boundaries can produce subtle discontinuities.
- **The "90s PC game" sound:** A recognizable graininess associated with DOS-era sound effects and compressed speech.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Step table replacement | Custom exponential curve, compressed range | Changes the adaptation character entirely |
| Step index clamp range | Limit to lower indices only | Forces small step sizes, creates overload distortion on loud signals |
| Adaptation speed | Modify index table values | Faster = more responsive but noisier; slower = smoother but more overload |
| Predictor reset frequency | Reset predictor every N samples | Creates rhythmic "restart" artifacts |
| Output bit depth | Reduce effective bits below 4 | Extreme quantization |

### Implementation Complexity

- **Code size:** ~80-120 lines of C for encoder+decoder
- **State:** 3 bytes (16-bit predictor + 7-bit step index)
- **Tables:** 89-entry step table + 16-entry index table = ~200 bytes
- **Heap:** Zero
- **Real-time:** Trivially feasible. One of the simplest codecs in existence.
- **Difficulty:** VERY LOW. Simpler than PS1 ADPCM (no filter selection, no block structure decisions).

### Licensing / Documentation

- **Specs:** IMA/DVI standard document freely available. Also documented on MultimediaWiki.
- **Patents:** None. Standardized by IMA (defunct organization). Freely implementable.
- **No legal concerns**

### Sources

- [MultimediaWiki IMA ADPCM](https://wiki.multimedia.cx/index.php/IMA_ADPCM)
- [Original IMA spec (Columbia)](http://www.cs.columbia.edu/~hgs/audio/dvi/IMA_ADPCM.pdf)
- [Microchip AN643 (ADPCM tutorial)](https://ww1.microchip.com/downloads/en/AppNotes/00643b.pdf)

---

## 3. mu-law and A-law Companding (G.711)

**Confidence:** HIGH -- ITU G.711 is one of the most thoroughly documented codecs in existence

### Algorithm Overview

G.711 is NOT a predictive codec. It is a **companding** (compressing + expanding) codec that applies a nonlinear transfer function to map 14-bit (mu-law) or 13-bit (A-law) linear PCM to 8-bit nonlinear PCM. There is no prediction, no adaptation, no block structure. Each sample is independently transformed.

The nonlinear curve is logarithmic, allocating more bits to quiet signals and fewer to loud signals. This matches human hearing's logarithmic loudness perception.

**mu-law formula (continuous):**
```
F(x) = sign(x) * ln(1 + mu * |x|) / ln(1 + mu)
```
where mu = 255 (North America/Japan standard).

**A-law formula (continuous):**
```
F(x) = sign(x) * A * |x| / (1 + ln(A))           for |x| < 1/A
F(x) = sign(x) * (1 + ln(A * |x|)) / (1 + ln(A)) for 1/A <= |x| <= 1
```
where A = 87.6 (Europe/international standard).

**Practical implementation:** Both use a piecewise linear approximation with 8 segments (chords). The 8-bit output byte is structured as: 1 sign bit + 3 chord bits + 4 mantissa bits.

**Key distinction:** mu-law compresses 14-bit linear to 8-bit; A-law compresses 13-bit linear to 8-bit. mu-law has slightly better SNR at high amplitudes; A-law has more uniform noise and a linear segment near zero (lower distortion on very quiet signals).

### Key Parameters

| Parameter | mu-law | A-law |
|-----------|--------|-------|
| Input precision | 14-bit linear | 13-bit linear |
| Output precision | 8-bit nonlinear | 8-bit nonlinear |
| Compression parameter | mu = 255 | A = 87.6 |
| Sample rate (G.711) | 8 kHz | 8 kHz |
| Bitrate | 64 kbit/s | 64 kbit/s |
| Bandwidth | 300-3400 Hz (telephony) | 300-3400 Hz (telephony) |
| Dynamic range | ~72 dB | ~72 dB |
| Idle channel noise | Higher (no linear segment) | Lower (linear segment near zero) |

### Characteristic Artifacts

The "telephone sound" is a combination of G.711 companding artifacts AND the 300-3400 Hz bandpass filter inherent to telephony. For creative purposes, these are separable:

**Companding-only artifacts (the interesting part for SPU-94):**
- **Quantization noise that scales with signal level:** Unlike linear PCM where noise is constant, companded audio has noise proportional to signal amplitude. Loud signals have more absolute noise, but the SNR remains roughly constant across the dynamic range. This produces a subtle "breathing" quality.
- **Stepped amplitude response:** The piecewise-linear approximation creates subtle staircasing at chord boundaries (8 segments). Signals crossing chord boundaries exhibit slightly different quantization characteristics.
- **Granular noise on quiet signals:** mu-law's lack of a linear segment near zero means very quiet signals get relatively coarsely quantized. A-law handles quiet signals slightly better.
- **8-bit character:** With only 8 bits of output, even with companding, the noise floor is audible on complex program material. This is the "lo-fi warmth" of telephone audio.
- **No spectral coloration from the codec itself.** Companding is a purely amplitude-domain operation -- it doesn't shape the frequency spectrum. The telephony bandwidth limitation is a separate filter, not part of the codec.

**The "telephone" sound specifically:**
- Bandpass 300-3400 Hz (removes bass rumble and air/presence)
- 8-bit companded quantization
- 8 kHz sample rate (4 kHz Nyquist -- no content above 4 kHz)
- Combined, these produce the recognizable thin, nasal, slightly granular telephone quality

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Companding curve (mu/A value) | Vary the compression parameter | Lower mu = less compression, more linear; higher = more aggressive nonlinearity |
| Bit depth | Reduce from 8 bits to fewer | Extreme companded quantization |
| Apply companding without expansion | Encode but don't decode -- compresses dynamics | Natural-sounding limiter/compressor effect |
| Apply expansion without companding | Expand linear audio as if it were companded | Extreme dynamic range expansion, gates quiet signals |
| Bandpass filter (separate) | Telephony filter on/off, variable bandwidth | Control the "telephone" character independently |
| Mix law types | mu-law encode, A-law decode (or vice versa) | Deliberate mismatch creates tonal distortion |
| Bit inversion patterns | G.711 specifies bit inversion for idle channel | Toggling creates digital noise patterns |

### Implementation Complexity

- **Code size:** ~40-80 lines of C for encode+decode. Lookup table implementation is ~15 lines + 256-byte table.
- **State:** ZERO. Each sample is independently transformed. No state between samples.
- **Tables:** 256-byte encode table + 256-byte decode table (or ~128-byte segment table for formula approach)
- **Heap:** Zero
- **Real-time:** Trivially feasible. The simplest codec in this survey.
- **Difficulty:** TRIVIAL. Can be implemented in an afternoon.

### Licensing / Documentation

- **Specs:** ITU-T G.711 is freely available. Formula is in every DSP textbook.
- **Patents:** None. Public standard since 1972.
- **Reference implementations:** Everywhere. ITU provides reference C code.
- **No legal concerns**

### Sources

- [mu-law algorithm (Wikipedia)](https://en.wikipedia.org/wiki/Mu-law_algorithm)
- [G.711 (Wikipedia)](https://en.wikipedia.org/wiki/G.711)
- [Dystopian Code: PCM companding in C](http://dystopiancode.blogspot.com/2012/02/pcm-law-and-u-law-companding-algorithms.html)

---

## 4. Sony ATRAC (MiniDisc)

**Confidence:** MEDIUM -- algorithm described in AES papers but spec is proprietary. Open-source encoder exists (atracdenc).

### Algorithm Overview

ATRAC (Adaptive Transform Acoustic Coding) is Sony's proprietary codec used in MiniDisc. Unlike all the ADPCM variants above, ATRAC is a **transform codec** -- it converts audio to the frequency domain, applies psychoacoustic masking, and quantizes spectral coefficients.

**ATRAC1 (original MiniDisc, 1992) signal path:**
1. **QMF analysis bank:** Two-stage Quadrature Mirror Filter splits audio into 3 subbands:
   - Band 1: 0-5.5 kHz (low)
   - Band 2: 5.5-11 kHz (mid)
   - Band 3: 11-22 kHz (high)
2. **MDCT:** Each subband is transformed using the Modified Discrete Cosine Transform with adaptive block length:
   - **Long mode:** 11.6 ms -- better frequency resolution for stationary signals
   - **Short mode:** 1.45 ms (high band) or 2.9 ms (low/mid) -- better time resolution for transients
3. **Spectral quantization:** MDCT coefficients grouped into Block Floating Units (BFUs) with non-uniform frequency spacing (denser at low frequencies). Each BFU gets a scale factor and wordlength.
4. **Bit allocation:** Psychoacoustic model determines bits per BFU. Formula: `btot(k) = T*bvar + (1-T)*bfix` where T is signal tonality.

**Frame size:** 512 input samples at 44.1 kHz = 11.6 ms per frame.

**Bitrate:** 292 kbit/s (stereo, standard MiniDisc).

### Key Parameters

- **QMF filter length:** Not publicly specified (likely 24-48 taps based on typical implementations)
- **MDCT block sizes:** Long = 512 points per frame; Short = 32-64 points per subframe (adaptive per band)
- **BFU structure:** Non-uniform grouping of spectral coefficients reflecting critical bands
- **Quantization wordlength:** Variable per BFU, controlled by bit allocation
- **Tonality measure (T):** Spectral flatness measure determining ratio of signal-adaptive vs fixed allocation

### Characteristic Artifacts

ATRAC artifacts are fundamentally different from ADPCM artifacts because they occur in the frequency domain:

- **Pre-echo:** The signature ATRAC artifact. When a transient (drum hit, consonant) occurs partway through a long MDCT block, quantization noise spreads backward in time to fill the entire block. This means you hear a brief "ghost" of noise BEFORE the transient. Later ATRAC versions (ATRAC2+) mitigate this by adaptive pre-amplification or shorter blocks.
- **Spectral holes:** At lower bitrates, the bit allocator may assign zero bits to some BFUs, creating "holes" in the frequency spectrum. These manifest as a slight "thinning" of the sound -- certain frequency bands simply disappear.
- **"Swirly" artifacts on complex textures:** Sustained complex signals (applause, ocean, cymbals) expose the psychoacoustic model's limitations. The bit allocator's frame-by-frame decisions create a subtle modulation of the noise floor that sounds "swirly" or "watery."
- **QMF aliasing at band edges:** The 5.5 kHz and 11 kHz subband boundaries can exhibit aliasing artifacts if the QMF filters aren't perfectly matched. This manifests as a subtle "roughness" near those frequencies.
- **Less signal-correlated than ADPCM:** Transform codec noise is more "diffuse" and less directly tied to the signal waveform. It sounds more like broadband noise than signal-tracking graininess.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Bit allocation aggressiveness | Reduce total bits available | Forces more spectral holes, thins the sound |
| Block size override | Force long mode always | Maximizes pre-echo -- "ghostly" transients |
| Block size override | Force short mode always | Eliminates pre-echo but reduces frequency resolution -- "smeared" spectrum |
| Tonality parameter (T) | Override the psychoacoustic model | T=0: fixed allocation (ignores signal content); T=1: fully adaptive |
| BFU wordlength minimum | Force minimum quantization per band | Adds broadband quantization noise |
| QMF band disable | Zero out one or more subbands | Spectral "holes" at specific frequency ranges |
| Pre-echo enhancement | Remove short-mode switching entirely | Deliberately maximizes the pre-echo artifact |

### Implementation Complexity

- **Code size:** ~2000-4000 lines of C for a basic ATRAC1 encoder+decoder
- **State:** QMF filter delay lines (~256 samples per band x 3 bands x 2 for analysis/synthesis = ~3 KB), MDCT window overlap buffers (~2 KB), working buffers for spectral coefficients (~4 KB). Total: ~10-15 KB of state.
- **Tables:** MDCT twiddle factors, QMF filter coefficients, psychoacoustic tables. ~5-10 KB of tables.
- **Heap:** Can be done zero-heap with sufficiently large fixed buffers, but it's tight. 15-25 KB total fixed allocation is feasible.
- **Real-time:** Feasible on modern hardware. ATRAC was designed to run on 1992-era embedded processors. A modern C implementation would use a tiny fraction of available CPU.
- **Difficulty:** MODERATE-HIGH. Significantly more complex than ADPCM. Requires implementing QMF filter banks, MDCT/IMDCT, psychoacoustic model, bit allocation algorithm. The decoder is simpler than the encoder.
- **Reference code:** atracdenc (open source, C++) provides a working ATRAC1 encoder. FFmpeg has ATRAC3 decoder code. Neither is zero-heap C, but both serve as reference.

### Licensing / Documentation

- **Specs:** The ATRAC1 algorithm is described in a 1992 AES paper by Tsutsui et al. The paper is detailed enough to implement from. No formal public standard document exists (unlike G.711 or GSM).
- **Patents:** Original ATRAC patents (filed ~1991) have expired (20-year patent term). ATRAC3 and ATRAC3plus may have later patents -- check specific filing dates.
- **Concern level:** LOW for ATRAC1. The algorithm is well-described in published papers. Patents expired. Sony has not enforced against open-source implementations (atracdenc has existed for years).

### Sources

- [ATRAC AES Paper (minidisc.org mirror)](https://www.minidisc.org/aes_atrac.html)
- [ATRAC (Wikipedia)](https://en.wikipedia.org/wiki/ATRAC)
- [atracdenc (GitHub)](https://github.com/dcherednik/atracdenc)
- [Signals that Trip-up ATRAC](http://www.minidisc.org/atrac_breakdown.html)

---

## 5. MPEG Audio Layer 2 (MP2)

**Confidence:** MEDIUM -- algorithm well-documented in ISO 11172-3 standard, but implementation complexity is high

### Algorithm Overview

MP2 (MPEG-1 Audio Layer II, also known as MUSICAM) is a subband codec that was the dominant broadcast audio codec through the 2000s and is used in DVB, DAB, PS2, and many broadcast systems.

**Signal path:**
1. **32-subband polyphase filter bank:** Splits input into 32 equally-spaced frequency subbands using a critically-sampled QMF. Each subband covers ~689 Hz at 44.1 kHz sampling.
2. **Psychoacoustic model:** Analyzes the input signal (in parallel with the filter bank) to determine masking thresholds. ISO 11172-3 defines two psychoacoustic models.
3. **Scale factor computation:** For each subband, in groups of 12 samples (36 samples per frame = 3 groups), a scale factor is computed. Scale factor selection coding reduces redundancy between consecutive scale factors.
4. **Bit allocation:** Based on masking thresholds, bits are distributed across subbands. Each subband gets 2-16 bits per sample (or 0 = zeroed out).
5. **Quantization:** Subband samples quantized to allocated bit depth relative to scale factor.

**Frame size:** 1152 samples per frame (3 groups of 12 samples across 32 subbands).
**Bitrates:** 32-384 kbit/s (typical broadcast: 192-256 kbit/s stereo).

### Key Parameters

- **Filter bank:** 32 subbands, 512-tap polyphase filter
- **Frame:** 1152 samples = ~26 ms at 44.1 kHz
- **Scale factors:** 63 possible values per subband per group of 12 samples
- **Bit allocation:** Per-subband, 0-16 bits
- **Psychoacoustic model:** ISO model 1 or model 2 (model 2 is more complex but better)

### Characteristic Artifacts

- **"Underwater" quality at low bitrates:** When too few bits are allocated across subbands, the quantization noise becomes audible as a "watery" or "bubbly" modulation. This is because the noise in each subband is modulated by the scale factors, creating a pumping effect.
- **Subband "birdies":** Isolated quantization noise tones that appear in subbands where the psychoacoustic model incorrectly predicts masking. These sound like faint twittering or warbling.
- **Less pre-echo than ATRAC:** MP2's subband filter has inherent temporal masking properties that partially conceal pre-echo artifacts. The 512-tap filter spreads energy in time, which masks some transient smearing.
- **Bandwidth limitation at low bitrates:** The encoder may zero out high-frequency subbands entirely to save bits, creating a lowpass effect. At 128 kbit/s, content above ~10-12 kHz may be removed.
- **Better temporal behavior than MP3:** MP2's lower frequency resolution (32 subbands vs MP3's hybrid approach) means less temporal smearing. This is why broadcast engineers historically preferred MP2 for critical monitoring.
- **Joint stereo artifacts:** At lower bitrates, intensity stereo encoding can create stereo image artifacts where high-frequency content collapses to mono.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Bitrate | 32-384 kbit/s | Lower = more artifacts, "underwater" character |
| Psychoacoustic model disable | Force flat bit allocation | Removes masking intelligence, exposes raw quantization |
| Per-subband bit override | Zero specific subbands | Surgical frequency removal |
| Scale factor manipulation | Exaggerate or reduce scale factors | Changes dynamic range compression per band |
| Frame-by-frame bitrate modulation | Vary bitrate over time | Creates rhythmic degradation effects |

### Implementation Complexity

- **Code size:** ~3000-5000 lines of C for a basic encoder+decoder (see kjmp2: a decoder in ~1500 lines)
- **State:** 512-tap filter delay line per channel (~2 KB), scale factor memory, bit allocation tables. Total: ~5-10 KB.
- **Tables:** Psychoacoustic model tables, filter coefficients, quantization tables. ~10-20 KB.
- **Heap:** Possible to do zero-heap with fixed buffers, but requires significant static allocation (~30-40 KB total).
- **Real-time:** Feasible. MP2 encoders ran on 486 processors in real-time.
- **Difficulty:** HIGH. The psychoacoustic model alone is complex. A decoder-only implementation is much simpler (~1500 lines, see kjmp2).
- **Note:** For creative purposes, a simplified encoder (with a crude or bypassable psychoacoustic model) paired with a faithful decoder would be sufficient and much simpler to implement.

### Licensing / Documentation

- **Specs:** ISO 11172-3 (MPEG-1 Audio) defines the format. The standard costs money from ISO but the algorithm is extensively described in textbooks and open-source implementations.
- **Patents:** All MPEG-1 Audio Layer II patents have expired (MPEG-1 was standardized in 1993; patents filed in the late 1980s-early 1990s).
- **Reference implementations:** twolame (open source, LGPL), kjmp2 (public domain decoder in ~1500 lines).
- **No legal concerns**

### Sources

- [MPEG-1 Audio Layer II (Wikipedia)](https://en.wikipedia.org/wiki/MPEG-1_Audio_Layer_II)
- [kjmp2: 4K MP2 decoder](https://keyj.emphy.de/kjmp2/)
- [twolame MP2 encoder (GitHub)](https://github.com/njh/twolame)
- [MUSICAM description](https://questtel.com/wiki/mpeg-layer-2-audio-coding-musicam)

---

## 6. GSM 06.10 Full Rate

**Confidence:** HIGH -- ETSI standard freely available, reference C implementation exists and is well-tested

### Algorithm Overview

GSM 06.10 is a **speech codec** using RPE-LTP (Regular Pulse Excitation with Long Term Prediction). Unlike the codecs above, this is a **vocoder** -- it models speech production rather than encoding arbitrary audio waveforms. Applying it to music or non-speech audio produces spectacularly wrong (and creatively interesting) results.

**Signal path per 20 ms frame (160 samples at 8 kHz):**
1. **Pre-emphasis filter:** Boosts high frequencies to flatten speech spectrum
2. **Short-term analysis (LPC):** Computes 8 reflection coefficients using Schur recursion (models the vocal tract's resonant structure)
3. **Short-term residual:** Filter the input through the inverse LPC filter to get the residual (the part not predicted by the vocal tract model)
4. **Long-term prediction (LTP):** Find the pitch period (delay) and gain that best predicts the current residual from a previous residual. This models vocal cord periodicity.
5. **RPE encoding:** The LTP residual is decimated to 1/3 rate, selecting the best of 3 possible subsequences (each 13 samples from the 40-sample subframe). Each pulse is quantized to 3 bits.
6. **Pack:** All parameters packed into 260 bits (padded to 33 bytes)

**Per-frame output: 76 parameters**
- 8 LPC reflection coefficients (LAR-coded)
- 4 subframes x (LTP delay + LTP gain + RPE grid position + 13 RPE pulses at 3 bits each)

### Key Parameters

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Frame size | 160 samples (20 ms) | Processing unit |
| Sample rate | 8 kHz | Narrowband speech |
| Bitrate | 13 kbit/s | Very low |
| LPC order | 8 | Vocal tract model |
| Subframes per frame | 4 (40 samples each) | LTP + RPE unit |
| RPE pulses per subframe | 13 at 3 bits | Excitation signal |
| LTP delay range | 40-120 samples (5-15 ms) | Pitch period |

### Characteristic Artifacts

- **"Robot phone" quality on speech:** The LPC model smooths formant transitions, the 8 kHz sample rate removes all content above 4 kHz, and the 3-bit RPE excitation adds a gritty, buzzy texture. This is THE sound of early 2G cell phones.
- **Spectacular failure on music:** Since the codec models speech production (vocal tract + pitched excitation), music is fundamentally mismodeled. Instruments with harmonic structure may survive partially, but:
  - **Drums/transients:** Horribly mangled. The LTP pitch predictor tries to find periodicity in impulsive signals and fails.
  - **Polyphonic material:** The single-pitch LTP model cannot represent multiple simultaneous pitches. Chords become garbled.
  - **Noise-like signals (cymbals, distortion guitar):** The RPE excitation cannot represent broadband noise with only 13 pulses at 3 bits. These become buzzy and metallic.
- **8 kHz bandwidth limitation:** Nothing above 4 kHz. All "air" and presence is removed. On music, this creates an extreme telephonic character.
- **Frame-boundary artifacts:** 20 ms frames mean ~50 parameter updates per second. Rapid parameter changes across frame boundaries create audible "clicking" or "chirping."
- **Iterative degradation:** Running audio through GSM encode/decode multiple times creates progressively more robotic quality. Each pass loses more information. This is the basis of the "GSM feedback" effect used in experimental music.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| LPC order | Reduce from 8 to fewer | Cruder vocal tract model, more "robotic" |
| LTP enable/disable | Disable pitch prediction | Removes pitched modeling, everything becomes noise-excited |
| RPE pulse count | Reduce from 13 | Cruder excitation, more buzzy/metallic |
| RPE bit depth | Reduce from 3 bits | Extreme quantization of excitation |
| Frame overlap / crossfade | Add crossfading between frames | Reduces frame-boundary clicks |
| Multiple encode passes | Re-encode decoded output | Progressive "robotification" |
| LTP delay override | Force fixed pitch | Impose a pitch on any input signal -- robotic pitch effect |
| Sample rate change | Process at rates other than 8 kHz | Applies speech model at wrong scale -- unpredictable results |

### Implementation Complexity

- **Code size:** ~2000-3000 lines of C (the quut.com/gsm library is the reference)
- **State:** gsm object holds ~400-500 bytes of state (LPC coefficients, LTP delay buffer, RPE parameters, previous frame data)
- **Tables:** Small -- LPC quantization tables, pre-emphasis coefficients. ~200 bytes.
- **Heap:** The reference implementation uses malloc for the gsm object, but the state is small enough to easily refactor to fixed allocation.
- **Real-time:** Feasible. GSM 06.10 ran on early-1990s DSP chips at 8 kHz. On modern CPUs it's negligible.
- **Difficulty:** MODERATE. The algorithm has many interacting stages. However, the reference C implementation from quut.com is BSD-licensed, well-tested, and can be adapted directly.

### Licensing / Documentation

- **Specs:** ETSI standard freely downloadable (GSM 06.10, EN 300 961). Includes reference C code.
- **Patents:** Standard is from 1991. All patents expired.
- **Reference implementation:** The Jutta Degener / Carsten Bormann implementation at quut.com/gsm is BSD-licensed, widely used, and well-tested. Patchlevel 24.
- **No legal concerns**

### Sources

- [quut.com/gsm (reference implementation)](https://www.quut.com/gsm/)
- [Full Rate (Wikipedia)](https://en.wikipedia.org/wiki/Full_Rate)
- [ETSI GSM 06.10 standard (PDF)](https://www.etsi.org/deliver/etsi_gts/06/0610/03.02.00_60/gsmts_0610sv030200p.pdf)
- [VOCAL GSM-FR technical brief](https://vocal.com/wp-content/uploads/2012/05/gsmfr.pdf)

---

## 7. Sega Saturn / Dreamcast ADPCM (Yamaha AICA)

**Confidence:** MEDIUM -- chip documentation fragmented; algorithm based on Jayant 1973 paper

### Algorithm Overview

**Correction on Saturn SCSP:** The Yamaha YMF292 (SCSP) in the Sega Saturn does NOT have hardware ADPCM decode. It supports only raw PCM (8-bit and 16-bit) playback. ADPCM on Saturn, if used at all, was decoded in software by the onboard 68EC000 CPU.

**Dreamcast AICA:** The Yamaha AICA (Super Intelligent Sound Processor) used in the Sega Dreamcast and NAOMI arcade boards DOES support hardware 4-bit ADPCM decode. This is a Yamaha-specific ADPCM variant.

**Yamaha ADPCM** is documented as "almost a reference implementation of the codec described in Jayant's 1973 paper" (Table VIII, 'DPCM' column, B=4). It is essentially a simpler version of IMA-ADPCM with a different step-size table and adaptation logic.

**Chips using Yamaha ADPCM:**
- Y8950 (MSX-SOUND)
- YM2608 (OPNA) -- used in NEC PC-88/PC-98
- YM2610 (OPNB) -- used in Neo Geo arcade
- Yamaha AICA -- Dreamcast/NAOMI
- Various Yamaha mobile sound chips (MA-2, MA-3, MA-5, MA-7)

### Key Differences from IMA-ADPCM

| Aspect | IMA-ADPCM | Yamaha ADPCM |
|--------|-----------|--------------|
| Step table size | 89 entries | 49 entries (per Dialogic/OKI variant) |
| Output resolution | 16-bit | 12-bit (Dialogic/OKI variant) |
| Adaptation | Index table lookup | Based on Jayant 1973 model |
| State | 23 bits | Similar (~19 bits with 12-bit predictor) |

**Note:** The exact Yamaha ADPCM step table and adaptation parameters are documented in the Y8950 Application Manual and YM2608 datasheet. The OKI/Dialogic variant (used in MSM5205, MSM6295 arcade chips) uses a 49-entry step table with 12-bit output, which is closely related.

### Characteristic Artifacts

- **12-bit output resolution (OKI variant):** 4 fewer bits than IMA-ADPCM's 16-bit output means a higher noise floor. The sound has a distinctly "crunchy" lo-fi character.
- **Fewer step sizes:** 49 entries vs IMA's 89 means the step-size adaptation is coarser. The codec overshoots and undershoots more during rapid amplitude changes.
- **Narrower dynamic range:** 12-bit output = ~72 dB dynamic range vs ~96 dB for 16-bit IMA. Quiet details are lost sooner.
- **The "Neo Geo voice sample" sound:** Yamaha ADPCM is the codec behind Neo Geo voice samples ("FATAL FURY!", announcer clips). The slightly crunchy, slightly compressed character is recognizable.
- **The "90s arcade" sound:** OKI MSM6295 ADPCM is in hundreds of arcade games from Capcom, SNK, Konami, etc. The sound is warm but grainy, with a character distinct from both CD-quality PCM and SNES BRR.

### Exploitable Internals

Similar to IMA-ADPCM but with the additional control of:

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Output bit depth | Vary 12-bit vs 16-bit reconstruction | 12-bit = arcade authentic; 16-bit = cleaner |
| Step table swap | Use IMA table vs OKI table vs custom | Changes adaptation character |
| Step table size | Reduce entries further | Coarser adaptation = more overshoot artifacts |

### Implementation Complexity

- **Code size:** ~60-100 lines of C (simpler than IMA-ADPCM due to smaller tables)
- **State:** ~3 bytes (12-bit predictor + 6-bit step index)
- **Tables:** 49-entry step table + 16-entry index table = ~120 bytes
- **Heap:** Zero
- **Real-time:** Trivially feasible
- **Difficulty:** VERY LOW

### Licensing / Documentation

- **Specs:** Y8950 and YM2608 datasheets available from various archives. OKI MSM6295 datasheet available.
- **Patents:** All expired (chips from 1985-1995 era)
- **Reference implementations:** MAME has well-tested implementations of both Yamaha and OKI ADPCM
- **No legal concerns**

### Sources

- [Yamaha ADPCM (MultimediaWiki)](https://wiki.multimedia.cx/index.php/Yamaha_ADPCM)
- [Dialogic IMA ADPCM (MultimediaWiki)](https://wiki.multimedia.cx/index.php/Dialogic_IMA_ADPCM)
- [OKI MSM6295 (vgmrips)](https://vgmrips.net/wiki/Oki_MSM6295)
- [Yamaha YMF292 (Wikipedia)](https://en.wikipedia.org/wiki/Yamaha_YMF292)

---

## 8. CVSD (Continuously Variable Slope Delta Modulation)

**Confidence:** HIGH -- well-documented in military standards and academic literature

### Algorithm Overview

CVSD is the extreme end of digital audio compression: **1-bit encoding**. Each sample is represented by a single bit indicating whether the signal went up or down relative to the previous sample. The step size adapts based on recent bit history to handle varying signal amplitudes.

**Encoder:**
```
for each input sample:
    if input > reference:
        emit bit = 1
        reference += step_size
    else:
        emit bit = 0
        reference -= step_size

    if last N bits are all same (all 1s or all 0s):
        step_size = increase (multiply by ~1.5-2.0)
    else:
        step_size = decrease (exponential decay, tau ~5ms)

    reference = leaky_integrate(reference)  // tau ~1ms
```

**Decoder:** Identical logic -- reads bits, applies same step-size adaptation, reconstructs reference signal. Then lowpass-filters the staircase output.

### Key Parameters

| Parameter | Typical Value | Range |
|-----------|---------------|-------|
| Sample rate | 16 kHz (voice) or 32 kHz | 9.6-128 kbit/s |
| Run length N | 3 or 4 | Adaptation sensitivity |
| Step increase factor | ~1.5-2.0x | Slope overload recovery speed |
| Step decrease time constant | ~5 ms | Granular noise smoothing |
| Reference leaky integrator tau | ~1 ms | DC bias removal |
| Minimum step size | ~floor value | Sets noise floor |
| Maximum step size | ~ceiling value | Sets maximum slew rate |

### Characteristic Artifacts

- **Granular noise:** When the signal is relatively constant, the output oscillates around the true value with 1-bit steps, creating a characteristic "grainy" or "sandy" texture. This is the baseline noise of CVSD.
- **Slope overload distortion:** When the signal changes faster than the step size can track (even after adaptation), the codec distorts. This sounds like a "fuzzy" or "buzzy" clipping, distinct from both hard clipping and ADPCM distortion.
- **Extreme bandwidth limitation:** The 1-bit encoding fundamentally limits the achievable SNR. At 16 kbit/s, CVSD achieves roughly 20-30 dB SNR on speech -- dramatically worse than any other codec in this survey. The sound is inherently noisy.
- **The "military radio" sound:** Recognizable from movies, games, and real military communications. Harsh, nasal, noisy, but intelligible speech. Music through CVSD is barely recognizable -- a kind of aggressive lo-fi effect.
- **Adaptation "pumping":** The step-size adaptation creates an envelope-following effect. After a loud transient, the step size is large, causing increased granular noise during the subsequent quiet period until the step size decays. This creates an audible "breathing" effect.
- **1-bit character:** There is literally no amplitude resolution per sample. All dynamics are encoded through the adaptation mechanism over time. This creates a fundamentally different kind of distortion than any multi-bit codec.

### Exploitable Internals

| Parameter | Creative Control | Effect |
|-----------|-----------------|--------|
| Run length N | 2 = very fast adaptation; 6 = very slow | Fast = less overload but more noise; slow = more overload distortion |
| Step increase/decrease ratio | Asymmetric attack/release | Controls "pumping" character |
| Minimum step size | Floor level | Higher floor = more granular noise |
| Maximum step size | Ceiling level | Lower ceiling = more slope overload |
| Leaky integrator time constants | Faster/slower reference tracking | Changes DC behavior and low-frequency response |
| Output lowpass filter cutoff | Variable smoothing | Less filtering = more "digital"; more = warmer but mushier |
| Multi-pass encoding | Re-encode decoded output | Progressive degradation |

### Implementation Complexity

- **Code size:** ~50-80 lines of C
- **State:** ~10-15 bytes (reference sample, step size, N-bit history shift register, integrator state)
- **Tables:** None (or a small lookup for step multiplier)
- **Heap:** Zero
- **Real-time:** Trivially feasible. Simplest possible codec.
- **Difficulty:** VERY LOW. Arguably the simplest codec that exists.

### Licensing / Documentation

- **Specs:** MIL-STD-188-113 (military standard). Also described in academic literature (Greefkes and Riemens, 1970). liquidsdr.org has a good reference implementation.
- **Patents:** None active. Algorithm from 1970.
- **Reference implementations:** liquidsdr CVSD module (open source), various military/telecom implementations.
- **No legal concerns**

### Sources

- [CVSD (Wikipedia)](https://en.wikipedia.org/wiki/Continuously_variable_slope_delta_modulation)
- [CVSD Codec (VOCAL)](https://vocal.com/speech-coders/cvsd/)
- [liquidsdr CVSD module](https://liquidsdr.org/doc/cvsd/)
- [Adaptive Digital CVSD](https://www.adaptivedigital.com/cvsd/)
- [TRMC CVSD appendix (PDF)](https://www.trmc.osd.mil/wiki/download/attachments/113019602/appendixF.pdf)

---

## 9. Other Historically Interesting Codecs

### 9a. OKI/Dialogic ADPCM (MSM5205 / MSM6295)

**Interest level: HIGH -- the arcade game ADPCM standard**

Already partially covered in section 7, but deserves separate mention for its ubiquity in arcade games.

- **Step table:** 49 entries (indices 0-48), values 16-1552
- **Output:** 12-bit signed (-2048 to +2047)
- **Chips:** OKI MSM5205 (1-channel), MSM6295 (4-channel), MSM6585
- **Games:** Hundreds of arcade titles -- CPS1/CPS2 (Capcom), Neo Geo (SNK), various Konami and Sega boards
- **Sonic character:** Warmer and crunchier than IMA-ADPCM. The 12-bit output gives it a distinctive mid-fi quality. Voice samples have a characteristic "punchy" quality.
- **Implementation:** ~60 lines of C. VERY LOW difficulty.
- **Licensing:** Expired patents, public datasheets.

### 9b. NICAM (Near Instantaneous Companded Audio Multiplex)

**Interest level: MODERATE -- interesting companding algorithm but narrow artifact window**

- **Algorithm:** 14-bit linear PCM companded to 10 bits using block-based adaptive scaling
- **Block size:** 32 samples
- **Scale factor:** 3-bit control per block selects which 10 of the 14 bits to transmit
- **Sample rate:** 32 kHz (15 kHz bandwidth)
- **Bitrate:** 728 kbit/s
- **Sonic character:** At full quality, nearly transparent. Artifacts only emerge on signals with rapid dynamic changes within a 32-sample block -- the "near instantaneous" companding can misjudge when a block starts quiet and ends loud (or vice versa). This manifests as a very subtle "pumping" or quantization noise modulation.
- **Creative potential:** LIMITED. NICAM's artifacts are subtle and hard to exaggerate without fundamentally altering the algorithm. More interesting as a "vintage TV" effect when combined with bandwidth limitation.
- **Implementation:** ~100 lines of C. LOW difficulty. Zero-heap trivial.
- **Licensing:** ETSI EN 300 163. No patent concerns.

### 9c. aptX

**Interest level: MODERATE -- interesting subband ADPCM but patent concerns**

- **Algorithm:** 4-subband QMF (2-stage, 64-tap filters) + per-band ADPCM
- **Bit allocation:** 8/4/2/2 bits for bands 1-4 (total 16 bits/sample = lossless at first glance, but the QMF + ADPCM quantization introduces subtle artifacts)
- **Latency:** 1.8-2.0 ms (extremely low for a subband codec)
- **Sonic character:** Very subtle artifacts. aptX was designed for professional broadcast monitoring where transparency is paramount. At standard quality, artifacts are nearly inaudible. The creative potential lies in:
  - Exaggerating the bit allocation asymmetry (e.g., 4/2/1/1 instead of 8/4/2/2)
  - The QMF band-splitting itself as a creative tool
- **Implementation:** ~500-800 lines of C. MODERATE difficulty due to QMF filters.
- **Licensing:** CAUTION. Qualcomm owns aptX. Patent EP 0398973B1 filed 1988, likely expired (20-year term from filing = 2008). But Qualcomm may hold continuation patents. **Needs careful patent review before implementation.**

### 9d. Dolby AC-2

**Interest level: LOW -- proprietary, poorly documented, superseded by AC-3**

- **Algorithm:** Transform-based (MDCT), predecessor to AC-3/Dolby Digital
- **Bitrate:** ~128-256 kbit/s stereo
- **Documentation:** Very limited public documentation. The AES paper describing it is paywalled.
- **Sonic character:** Similar to ATRAC but with Dolby's psychoacoustic model. Pre-echo artifacts present.
- **Creative potential:** Limited due to documentation scarcity. AC-3/Dolby Digital is better documented but encumbered by licensing.
- **Implementation:** NOT RECOMMENDED due to poor documentation and potential patent/licensing issues.

### 9e. Speex (before Opus)

**Interest level: MODERATE -- CELP codec, interesting "robot" artifacts**

- **Algorithm:** CELP (Code Excited Linear Prediction) -- more advanced vocoder than GSM
- **Bitrate:** 2.15-44.2 kbit/s (variable)
- **Sonic character:** At low bitrates, produces a different flavor of "robot voice" than GSM -- smoother but with "codebook" artifacts (the excitation is selected from a pre-computed codebook rather than RPE)
- **Creative potential:** Less distinctive than GSM 06.10. GSM's RPE artifacts are more sonically interesting.
- **Implementation:** ~5000+ lines of C. HIGH difficulty. Not recommended when GSM 06.10 is simpler and more distinctive.
- **Licensing:** BSD. Fully open. But superseded by Opus.

### 9f. Adaptive Delta Modulation (ADM)

**Interest level: LOW -- CVSD is the better version**

- **Algorithm:** Like CVSD but with fixed (non-adaptive) step size
- **Sonic character:** Worse than CVSD in every way. More slope overload, more granular noise, no adaptation.
- **Creative potential:** CVSD already covers this territory and is more controllable.
- **Implementation:** ~30 lines of C. Trivially simple but sonically redundant with CVSD.

### 9g. A-law/mu-law at Non-Standard Parameters

**Interest level: HIGH -- already covered in section 3, but worth noting**

Using G.711 companding at sample rates other than 8 kHz (e.g., 44.1 kHz) and with modified mu/A parameters opens up interesting creative territory:
- Full-bandwidth audio with logarithmic quantization character
- 8-bit "warm" character without telephony bandwidth limitation
- Adjustable companding aggressiveness

---

## 10. Comparative Summary

### Artifact Character Matrix

| Codec | Noise Type | Frequency Character | Temporal Character | "Era" Sound |
|-------|-----------|---------------------|-------------------|-------------|
| SNES BRR | Signal-correlated + wrap distortion | Broadband, softened by Gaussian LPF | Block-boundary clicks possible | SNES (1990-1996) |
| PS1 SPU-ADPCM | Signal-correlated, spectrally shaped | Shaped by prediction filters | Smooth (28 samples/block) | PS1 (1994-2004) |
| IMA-ADPCM | Signal-correlated, spectrally flat | Flat, "grainy" across spectrum | Step-hunting on transients | PC games (1992-2000) |
| G.711 mu/A-law | Amplitude-proportional | Flat (no spectral shaping) | None (stateless) | Telephone (1972-present) |
| ATRAC | Diffuse, frequency-domain | Spectral holes, band-edge roughness | Pre-echo on transients | MiniDisc (1992-2010) |
| MP2 | Psychoacoustically masked | "Underwater" subband artifacts | Less pre-echo than MP3 | Broadcast/PS2 (1993-2010) |
| GSM 06.10 | Speech-model mismatch | Narrowband (4 kHz max) | Frame-boundary artifacts (20ms) | 2G cell phone (1991-2005) |
| Yamaha/OKI ADPCM | Signal-correlated, 12-bit floor | Flat, crunchy | Step-hunting | Arcade (1987-2000) |
| CVSD | Granular + slope overload | Extreme bandwidth limitation | Adaptation pumping | Military radio (1970s-present) |

### Implementation Complexity Ranking (simplest to hardest)

| Rank | Codec | Lines of C | State (bytes) | Difficulty |
|------|-------|-----------|---------------|------------|
| 1 | G.711 mu/A-law | 40-80 | 0 | TRIVIAL |
| 2 | CVSD | 50-80 | 10-15 | VERY LOW |
| 3 | OKI/Dialogic ADPCM | 60-100 | 3 | VERY LOW |
| 4 | IMA-ADPCM | 80-120 | 3 | VERY LOW |
| 5 | SNES BRR | 150-200 | 4 | LOW |
| 6 | PS1 SPU-ADPCM | 200-300 | 8 | LOW |
| 7 | GSM 06.10 | 2000-3000 | 400-500 | MODERATE |
| 8 | ATRAC1 | 2000-4000 | 10K-15K | MODERATE-HIGH |
| 9 | MP2 (encoder+decoder) | 3000-5000 | 5K-10K | HIGH |

### Creative Value Ranking (most distinctive artifacts to least)

| Rank | Codec | Why |
|------|-------|-----|
| 1 | GSM 06.10 | Speech vocoder applied to music = spectacular destruction. Most distinctive artifact character. |
| 2 | CVSD | 1-bit encoding = extreme, unique distortion character unlike anything else. |
| 3 | SNES BRR | 15-bit wrap bug creates a distortion character unique to SNES. Closest relative to PS1 ADPCM but distinctly different. |
| 4 | G.711 mu/A-law | Companding is a fundamentally different artifact type (amplitude-domain, not time/frequency). |
| 5 | ATRAC | Pre-echo is a unique and recognizable artifact type. Frequency-domain operation is fundamentally different from ADPCM. |
| 6 | OKI/Dialogic ADPCM | 12-bit output creates a specific "arcade" character. |
| 7 | IMA-ADPCM | The "generic" ADPCM. Useful as a baseline comparison point for PS1 ADPCM. |
| 8 | MP2 | "Underwater" artifacts are interesting but require significant implementation effort. |
| 9 | NICAM | Artifacts too subtle to be creatively useful at standard parameters. |

---

## 11. Implementation Priority Recommendations

Based on creative value, implementation complexity, and relationship to the existing PS1 ADPCM codebase:

### Tier 1: Build These First (Low Effort, High Creative Value)

These can be implemented in a few days each and produce distinctive, immediately useful effects.

1. **SNES BRR** -- Closest sibling to PS1 ADPCM. Shares the encode/decode architecture but sounds distinctly different due to 15-bit wrap bug, fewer filters, and smaller blocks. Can reuse PS1 ADPCM infrastructure with modifications. ~1 day of work.

2. **G.711 mu-law / A-law** -- Completely different artifact type (companding vs prediction). Zero state, trivially simple. Can be combined with other codecs in chain. ~half a day of work.

3. **IMA-ADPCM** -- The "vanilla ADPCM" baseline. Useful for A/B comparison with PS1 ADPCM to demonstrate what Sony's prediction filters add. ~1 day of work.

### Tier 2: Build These Next (Moderate Effort, High Creative Value)

4. **CVSD** -- 1-bit encoding is a fundamentally unique character. Tiny implementation. ~1 day of work.

5. **OKI/Dialogic ADPCM** -- The "arcade game" ADPCM. 12-bit output gives it a specific character. Tiny implementation. ~1 day of work.

6. **GSM 06.10** -- The speech vocoder. Most creatively interesting single codec in this list. Moderate implementation effort but the reference C code exists and is BSD-licensed. ~3-5 days to adapt to zero-heap, integrate into the pipeline.

### Tier 3: Build If There's Demand (High Effort, Distinctive Character)

7. **ATRAC1** -- Pre-echo and spectral artifacts are fundamentally different from ADPCM. But the implementation is an order of magnitude more complex. Worth doing if the "digital patina engine" concept takes off, but not part of an MVP. ~2-4 weeks.

8. **MP2** -- "Underwater" subband artifacts are interesting but the psychoacoustic model adds substantial complexity. Consider a decoder-only implementation (kjmp2 is ~1500 lines, public domain) paired with a simplified encoder. ~1-2 weeks.

### Not Recommended

- **aptX:** Patent risk, subtle artifacts, limited creative value relative to complexity.
- **Dolby AC-2:** Poorly documented, licensing concerns.
- **Speex/CELP:** Too complex, GSM 06.10 covers the vocoder territory better.
- **NICAM:** Artifacts too subtle.
- **ADM (non-adaptive delta mod):** CVSD is strictly better.

---

## Sources Index

### Primary Documentation
- [SnesLab BRR](https://sneslab.net/wiki/Bit_Rate_Reduction) -- SNES BRR definitive reference
- [MultimediaWiki IMA ADPCM](https://wiki.multimedia.cx/index.php/IMA_ADPCM) -- IMA-ADPCM algorithm
- [ITU G.711 / mu-law (Wikipedia)](https://en.wikipedia.org/wiki/Mu-law_algorithm) -- Companding algorithms
- [ATRAC AES Paper (minidisc.org)](https://www.minidisc.org/aes_atrac.html) -- ATRAC1 algorithm description
- [MPEG-1 Audio Layer II (Wikipedia)](https://en.wikipedia.org/wiki/MPEG-1_Audio_Layer_II) -- MP2 overview
- [quut.com/gsm](https://www.quut.com/gsm/) -- GSM 06.10 reference C implementation
- [Yamaha ADPCM (MultimediaWiki)](https://wiki.multimedia.cx/index.php/Yamaha_ADPCM) -- Yamaha ADPCM variants
- [CVSD (Wikipedia)](https://en.wikipedia.org/wiki/Continuously_variable_slope_delta_modulation) -- CVSD algorithm
- [Dialogic ADPCM (MultimediaWiki)](https://wiki.multimedia.cx/index.php/Dialogic_IMA_ADPCM) -- OKI/Dialogic variant

### Standards Documents
- [ETSI GSM 06.10 (PDF)](https://www.etsi.org/deliver/etsi_gts/06/0610/03.02.00_60/gsmts_0610sv030200p.pdf)
- [ETSI NICAM EN 300 163](https://www.etsi.org/deliver/etsi_en/300100_300199/300163/01.02.01_60/en_300163v010201p.pdf)

### Reference Implementations
- [snesbrr (GitHub)](https://github.com/mukunda-/snesbrr) -- BRR encoder/decoder
- [atracdenc (GitHub)](https://github.com/dcherednik/atracdenc) -- ATRAC1/3 encoder
- [kjmp2 (KeyJ)](https://keyj.emphy.de/kjmp2/) -- Minimal MP2 decoder
- [twolame (GitHub)](https://github.com/njh/twolame) -- MP2 encoder
- [liquidsdr CVSD](https://liquidsdr.org/doc/cvsd/) -- CVSD reference
- [GSM library (quut.com)](https://www.quut.com/gsm/) -- BSD-licensed GSM 06.10

### Creative Audio Context
- [Lese Codec plugin](https://bedroomproducersblog.com/2022/08/08/lese-codec/) -- Existing lo-fi codec emulation plugin
- [Designing Sound with Artifacts](https://designingsound.org/2018/10/18/designing-sound-with-artifacts-an-experiment/) -- Creative use of codec artifacts
