# Deep Dive: ADPCM Coloration Character and Psychoacoustic Properties

**Project:** SPU-94 (M2 ADPCM milestone)
**Researched:** 2026-04-26
**Overall confidence:** MEDIUM-HIGH (filter math is exact; subjective sonic descriptions are synthesized from DSP theory, community descriptions, and signal processing first principles -- not from controlled listening tests)

---

## 1. Quantization Noise Character: What 4-Bit ADPCM Actually Sounds Like

### It Is Not White Noise

The single most important thing to understand about ADPCM quantization noise: **it is signal-correlated distortion, not random noise.** White noise has a constant power spectral density regardless of input. ADPCM quantization error tracks the signal -- it gets louder when the signal gets louder, quieter when it gets quieter, and its spectral content shadows the signal's spectrum. The technical term is **modulation noise**.

### Three Distinct Noise Regimes

ADPCM quantization artifacts fall into three regimes, each with a different sonic character:

**1. Granular noise (low-to-mid signal levels)**
When the signal amplitude is moderate relative to the quantization step, the error behaves like a coarse rounding. With only 16 possible residual values per sample (-8 to +7), the reconstructed waveform is a staircase approximation of the original. This sounds like:
- A gritty, sandy texture overlaid on the signal
- Closest recording-world analog: **the sound of a worn cassette tape** or **a budget A/D converter from the late 1980s**
- Spectrally, the granular noise concentrates energy at odd harmonics of the signal, similar to crossover distortion in a class-B amplifier
- It is most audible on sustained tones, vocals, and pads -- any signal where you can hear "through" to the noise floor

**2. Slope overload (fast transients)**
When the signal changes faster than the predictor can follow, the encoder's residuals hit the -8/+7 clamp limits. The decoder output "chases" the input but can't keep up, producing a characteristic squashed, flattened version of fast edges. This sounds like:
- Soft clipping on attack transients
- A slight "rounding" of sharp edges (drum hits, consonants in speech)
- Closest analog: **a limiter with a too-slow attack time** -- the first few samples of a transient get compressed
- Less objectionable than hard clipping because the predictor partially compensates

**3. Idle channel noise (silence and near-silence)**
When the input signal is near zero, ADPCM cannot represent true silence because there is no residual value that means "step size zero." The decoder oscillates between the smallest positive and negative steps. This produces:
- A low-level buzzy/crackling texture during quiet passages
- Spectrally rich -- contains harmonics related to the block rate
- Closest analog: **the hum and crackle of a vinyl record in the runout groove**, or the "digital silence" buzz of an early CD player with poor dithering
- Most noticeable in reverb tails as the signal decays toward zero

### SNR by Regime

| Condition | Effective SNR | Character |
|-----------|---------------|-----------|
| Filter 0, no prediction | ~26 dB | Rough, 8-bit-like quality. Obvious to any listener. |
| Good prediction (filter 2-4, tonal content) | ~38-50 dB | Comparable to cassette tape or mu-law telephone. Audible on critical listening but not immediately offensive. |
| Worst case (transient with wrong filter) | <20 dB | Momentary crunch/distortion on attacks. Lasts one block (~0.6ms). |
| Near-silence | Floor depends on last step size | Idle channel buzz/crackle, signal-independent once the step size has settled. |

### The Recording Engineer's Ear

To an experienced recording engineer, PS1 ADPCM sounds most like **a combination of cassette tape noise floor and early sampler character** (think E-mu SP-1200 or Akai S900 at 12-bit). It is NOT like:
- MP3 artifacts (which are spectral holes and pre-echo from transform coding)
- Vinyl surface noise (which is truly random, uncorrelated with signal)
- Tape saturation (which is smooth, even-harmonic distortion)

The ADPCM character sits in a specific perceptual space: it adds **grit without warmth**. Tape saturates smoothly; ADPCM quantizes harshly. Tape adds even harmonics; ADPCM adds odd harmonics and intermodulation products. The combination is what gives PS1 audio its distinctively "digital but crunchy" character -- not the smooth warmth of analog, not the clinical precision of modern digital, but something in between that reads as "lo-fi digital" to trained ears.

---

## 2. Filter Selection Artifacts and the 1575 Hz Block Rate

### The Block Rate as a Spectral Signature

Every 28 samples, the encoder selects a new (filter, shift) pair. At 44.1 kHz, this creates a fundamental block rate of **1575 Hz**. The question: does filter switching create audible artifacts at or near this frequency?

**Analysis:**

The decoder state (the two previous samples, `old` and `older`) carries across block boundaries, providing continuity. However, when the filter coefficients change abruptly, the *prediction trajectory* shifts. This creates a small discontinuity not in the output waveform itself, but in the *derivative* of the prediction -- a subtle kink in the signal's trajectory.

**Whether this is audible depends on how aggressively the filter changes:**

| Transition | Magnitude of Discontinuity | Audibility |
|------------|---------------------------|------------|
| Filter 2 to Filter 3 | Moderate (resonance jumps from 568 Hz to 4204 Hz) | Potentially audible on sustained tones |
| Filter 1 to Filter 2 | Small (adding 2nd-order term) | Usually masked by signal |
| Filter 0 to Filter 4 | Large (no prediction to aggressive resonant) | Can create audible "click" if residuals don't absorb the discontinuity |
| Same filter, shift change only | Very small (scaling factor changes) | Virtually never audible |

**On a spectrogram**, PS1 ADPCM shows:
- A slight "roughening" of spectral lines compared to clean PCM -- harmonics become fuzzy rather than sharp
- No strong spectral line at exactly 1575 Hz (the state carryover prevents a hard periodic discontinuity)
- Possible weak energy spreading around 1575 Hz and its harmonics, visible primarily in quiet passages where the noise floor is exposed
- Block-boundary effects are more visible as **temporal** artifacts (brief amplitude modulation) than as **spectral** artifacts (discrete tones)

### The Critical Insight

The block rate artifact is not a tone -- it is a **modulation envelope**. Every ~0.635 ms, the noise characteristics shift slightly as the filter/shift combination changes. This creates an amplitude modulation of the noise floor at the block rate. On tonal, sustained signals, a trained ear might perceive this as a very subtle "warbling" or "roughness" in the noise character. On transient-heavy material (drums, percussion), it is completely masked by the signal itself.

---

## 3. Sonic Character of Each Filter: What the Five Filters Sound Like

### Pole-Zero Analysis (Computed from Exact Coefficients)

The prediction filter is an IIR system with transfer function `H(z) = 1 / (1 - K0*z^-1 - K1*z^-2)`. The poles determine the filter's resonant behavior:

| Filter | Pole Type | Pole Radius | Resonance Freq @44.1kHz | Q Factor | 60dB Ring Time @44.1kHz |
|--------|-----------|-------------|------------------------|----------|----------------------|
| 0 | None | -- | -- | -- | -- |
| 1 | Real (0.9375) | 0.9375 | DC (low-pass) | -- | <1 ms |
| 2 | Complex conjugate | 0.901 | 568 Hz | 5.07 | 1.5 ms (~67 samples) |
| 3 | Complex conjugate | 0.927 | 4204 Hz | 6.85 | 2.1 ms (~91 samples) |
| 4 | Complex conjugate | 0.968 | 1242 Hz | 15.75 | 4.9 ms (~214 samples) |

### Filter-by-Filter Sonic Signatures

**Filter 0: No prediction (raw residual only)**
- Character: Maximum quantization noise. Every sample is independently quantized to 4 bits.
- Sounds like: Bitcrushed audio. Harsh, buzzy, lo-fi in the extreme.
- Used when: The signal is so unpredictable that no filter helps (noise, heavily distorted guitar, chaotic transients). Also used for silence/near-silence blocks.
- On drums: Snare hits sound "crunchy" with obvious step artifacts on the decay.
- On vocals: Barely intelligible except at high shift values (loud signals). Extreme "robot voice" quality.
- On pads/strings: Unusable -- constant audible quantization noise.

**Filter 1: First-order low-pass prediction (K0=0.9375)**
- Character: Single real pole at 0.9375 -- gentle low-pass prediction. The filter predicts that each sample will be ~94% of the previous one.
- Sounds like: Noticeably better than Filter 0. Good for signals with slowly changing amplitude.
- No ringing -- the real pole means the filter decays monotonically.
- Used when: The signal has gentle slopes but no strong periodicity (speech sibilants, noise-like content, some transient tails).
- On drums: Handles the sustain/decay phase well. Attack moments may force higher shift values.
- On vocals: Reasonable quality on sustained vowels. Consonants may cause brief slope overload.
- On pads/strings: Adequate for very slowly evolving timbres. Faster vibrato defeats the prediction.

**Filter 2: Second-order resonant at ~568 Hz (Q=5.07)**
- Character: Resonant prediction centered in the lower midrange. Optimized for signals with strong fundamental content in the 400-700 Hz range.
- Ring time: 1.5 ms (67 samples) at 44.1 kHz -- about 2.4 block periods. The ringing is brief but audible on isolated transients.
- Sounds like: Clean on bass guitar, male vocals, cello. Adds a subtle "boing" on percussive hits -- a very short resonant tail at ~568 Hz after the attack.
- On drums: Kick drum benefits (strong fundamental overlap). Snare and hi-hat: the ringing is short enough to be masked.
- On vocals: Male vocals in the sweet spot. Female soprano benefits less (fundamental too high for the filter's center).
- On pads/strings: Excellent prediction for warm pad sounds. Low residuals mean low noise.

**Filter 3: Second-order resonant at ~4204 Hz (Q=6.85)**
- Character: Resonant prediction in the upper midrange / "presence" region. The odd one out -- the highest resonance frequency of all filters.
- Ring time: 2.1 ms (91 samples) at 44.1 kHz. Longer than Filter 2, and at a frequency where ears are very sensitive.
- Sounds like: Optimized for signals with strong harmonic content around 4 kHz (hi-hats, cymbals, vocal sibilance, strings' "edge"). Adds a metallic "zing" on transients.
- On drums: Hi-hat and cymbal hits get a subtle metallic sheen from the ringing. This is part of the PS1 drum sound.
- On vocals: Handles sibilance well (good prediction), but the ringing on plosives adds a brief "tsk" quality.
- On pads/strings: String attacks get a metallic brightness. The ~4 kHz ringing sits right in the presence band, adding an artificial clarity.
- **This filter is likely responsible for the "metallic" quality that many people associate with PS1 audio.**

**Filter 4: Second-order resonant at ~1242 Hz (Q=15.75) -- SPU-exclusive**
- Character: The most aggressive predictor. Poles at radius 0.968 -- dangerously close to the unit circle. Very narrow resonance at ~1242 Hz with the highest Q of any filter.
- Ring time: **4.9 ms (214 samples)** at 44.1 kHz -- nearly 8 block periods. This is a long ring for a codec artifact.
- Sounds like: When it matches the signal, outstanding prediction quality and low noise. When it doesn't match, pronounced ringing at ~1242 Hz after any transient.
- On drums: Snare drum (which often has strong spectral content near 1200 Hz) gets a subtle pitched "ping" added to the tail. Kick drum and hi-hat: the ringing is at a frequency that doesn't reinforce their natural spectrum, creating an unnatural "ghost tone."
- On vocals: Female fundamentals around 250-500 Hz produce harmonics near 1242 Hz that the filter tracks well. Male vocals may get a "nasal" coloration from the ringing.
- On pads/strings: Sustained tones near 1242 Hz get the cleanest encoding of any filter. But evolving timbres that drift away from this frequency will hear the filter "let go" -- a brief ringing tail as the predictor's inertia fades.
- **This SPU-exclusive filter is likely the single biggest sonic difference between SPU-ADPCM (PS1) and XA-ADPCM (which stops at Filter 3). It adds a distinctive pitched ringing that is absent from CD-XA audio.**

### How the Encoder Switches Between Them

A well-designed encoder tests all 5 filters per block and picks the one with lowest reconstruction error. In practice:

- **Tonal signals** will repeatedly select whichever filter's resonance is closest to the dominant frequency, creating consistent coloration
- **Broadband noise** will oscillate between filters semi-randomly, each block picking whatever marginally wins
- **Transient-to-sustain transitions** are where the most audible filter switches happen: the attack block might use Filter 0 or 1 (handling the unpredictable transient), then the sustain blocks snap to Filter 2, 3, or 4 (tracking the tonal content). The transition between these creates a brief timbral shift

---

## 4. How ADPCM Coloration Interacts with the SPU Reverb

### The Feedback Loop Problem

The SPU reverb is fundamentally a feedback delay network with all-pass filters and comb filters. Signal is fed in, delayed, filtered, and recirculated. When the input to this network contains ADPCM artifacts, those artifacts get recirculated just like the signal itself.

**What happens to each artifact type in the reverb:**

| Artifact | Reverb Behavior | Result |
|----------|----------------|--------|
| Granular quantization noise | Recirculates through feedback paths, each pass adding the comb filter's spectral shaping | Noise accumulates at the comb filter resonance frequencies. Instead of broadband grit, the reverb tail develops pitched, tonal noise -- a kind of **harmonic shimmer** |
| Filter ringing (especially Filter 4) | The ~1242 Hz ringing feeds back through the network, which has its own resonances | If a reverb comb frequency aligns near 1242 Hz, the ringing is **amplified** and can create a subtle pitched drone in the tail. If misaligned, the ringing gets spectrally smeared across multiple comb resonances |
| Idle channel noise | Enters the reverb and gets shaped by the network's frequency response | Produces a characteristic **buzzy, metallic decay** in the last moments of the reverb tail. This is THE signature PS1 reverb sound -- the tail doesn't fade to clean silence, it fades to a shimmering, granular residue |
| Block-boundary modulation | The ~1575 Hz modulation envelope enters the reverb's delay lines | At certain reverb times, the delay may align with the block period, creating reinforcement. More commonly, the modulation gets smeared across multiple delay taps, adding to the general "texture" |

### The Characteristic PS1 Reverb Tail

The authentic PS1 reverb tail has a specific quality that is a direct result of ADPCM coloration meeting the feedback delay network:

1. **Early reflections** (first 20-50 ms): Nearly indistinguishable from clean PCM reverb. ADPCM artifacts are masked by the strong signal.

2. **Mid-tail** (50-500 ms): The granular noise becomes audible as the signal decays. The reverb's comb filters shape this noise into a quasi-pitched, shimmering texture. An engineer would describe this as "grainy" or "gritty" -- the reverb sounds like it's made of sand rather than glass.

3. **Late tail** (>500 ms): ADPCM idle-channel noise dominates. Each recirculation pass adds more quantization error. The tail develops a **metallic, buzzy quality** -- not a smooth exponential decay but a textured, fizzy fade. This is where the PS1 reverb is most distinctive and most different from any DAW reverb.

4. **Very late tail** (final decay): The reverb's feedback coefficient determines how long artifacts persist. With high feedback (long reverb times), ADPCM noise can self-sustain as the feedback loop continuously regenerates quantization errors. The tail "hangs" with a grainy residue rather than fading cleanly to silence.

### Why Clean PCM Reverb Sounds Wrong for PS1 Emulation

If you feed clean 16-bit PCM into the SPU reverb algorithm, you get a mathematically correct reverb that sounds "too clean." The missing ingredient is the ADPCM noise floor that real PS1 hardware always had in the signal. The reverb tail is too smooth, too transparent -- it lacks the grainy, textured character that PS1 game developers (and players) heard. This is precisely why M2 exists: to add the ADPCM coloration before the reverb, completing the authentic signal path.

---

## 5. Comparison with Other Vintage Compression Artifacts

### Side-by-Side Character Map

| Codec / Medium | Bit Depth | Noise Floor | Noise Character | Transient Handling | Closest PS1 Similarity |
|----------------|-----------|-------------|-----------------|--------------------|-----------------------|
| **PS1 SPU ADPCM** | 4-bit differential | ~26-50 dB SNR (signal-dependent) | Signal-correlated granular grit + filter ringing | Slope overload on fast attacks | -- |
| **mu-law (telephone)** | 8-bit companded | ~38 dB SQNR | Companding distortion, odd harmonics on low signals | Good (wide step sizes at high amplitude) | **Similar SNR range.** But mu-law noise is smoother -- no filter ringing, no block artifacts. PS1 is grittier. |
| **A-law (telephone)** | 8-bit companded | ~38 dB SQNR | Nearly identical to mu-law | Same as mu-law | Same comparison as mu-law |
| **IMA-ADPCM (PC games, WAV)** | 4-bit adaptive | ~25-40 dB SNR | Signal-correlated, but NO prediction filters. Pure step-size adaptation. | Worse -- no predictor means larger residuals | **Noisier than PS1.** IMA has no filter prediction, so it cannot exploit signal correlation as effectively. PS1 ADPCM sounds cleaner on tonal material because the 2nd-order prediction filters absorb most of the signal energy. But IMA is smoother on transients (no filter ringing). |
| **MPEG Layer 2 (PS2 era)** | Transform-coded | ~60-80 dB | Pre-echo before transients, spectral holes, "underwater" quality at low bitrates | Pre-echo is the signature artifact | **Completely different character.** MP2 artifacts are frequency-domain artifacts (missing spectral bands). PS1 artifacts are time-domain artifacts (granular noise, ringing). They don't sound alike at all. |
| **Cassette tape (Type I)** | Analog | ~50-55 dB SNR | Broadband hiss, slight high-frequency roll-off, tape saturation on peaks | Soft saturation (even harmonics) | **Similar noise floor level** with good ADPCM prediction. But tape noise is uncorrelated (random hiss), while ADPCM noise is correlated (gritty). Tape saturates warmly; ADPCM clips harshly. |
| **Reel-to-reel tape (15 ips)** | Analog | ~60-70 dB SNR | Low-level hiss, excellent transient handling, warm saturation | Excellent | **Much cleaner than PS1.** Not a useful comparison. |
| **E-mu SP-1200 / Akai S900** | 12-bit linear | ~74 dB SNR | Low-level quantization crunch, "crunchy" character | Good (12-bit captures transients well) | **The closest analog experience.** The "digital crunch" of 12-bit samplers is the nearest thing to PS1 ADPCM in the recording world. Same ballpark of "lo-fi digital" without the specific filter ringing. |
| **8-bit PCM (NES era)** | 8-bit linear | ~50 dB SNR | Obvious quantization steps on quiet signals, "buzzy" quality | Adequate for simple waveforms | **Different character.** 8-bit is uniformly quantized; PS1 is adaptively quantized. PS1 handles loud signals much better but has worse idle-channel behavior. |

### The Bottom Line for an Audio Engineer

**PS1 ADPCM sounds most like a 12-bit sampler run through a resonant filter and then into a reverb unit with a slightly noisy signal path.** The combination of:
- Granular quantization noise (the "crunch")
- Resonant filter ringing (the "metallic shimmer")
- Block-rate modulation (the "texture")

...creates a sonic fingerprint that doesn't exist in any other consumer audio system. It is harsher than tape, smoother than raw 4-bit PCM (thanks to prediction), more textured than telephone codecs, and completely unlike transform-coded formats like MP3/MP2.

---

## 6. Sample Rate Interaction: How Coloration Changes at Different Rates

### Block Rate Scales Linearly with Sample Rate

| Sample Rate | Block Rate | Block Period | Perceptual Significance |
|-------------|-----------|--------------|------------------------|
| 44100 Hz | 1575.0 Hz | 0.635 ms | Mid-frequency. Block artifacts in the "body" range of most instruments. Masked by tonal content in most program material. |
| 33075 Hz | 1181.3 Hz | 0.847 ms | Lower mid-frequency. Slightly more audible as modulation on sustained bass content. |
| 22050 Hz | 787.5 Hz | 1.270 ms | **Low midrange.** Block artifacts in the "warmth" region. More likely to interact with fundamental frequencies of vocals and instruments. Potentially more audible. |
| 11025 Hz | 393.8 Hz | 2.540 ms | **Low frequency.** Block artifacts at a pitch roughly equivalent to G4. At this rate, filter switching happens slowly enough that individual block transitions may be perceptible as rhythmic "ticking" on very clean signals. |

### Filter Resonance Frequencies Also Scale

The filter resonance frequencies are a fixed fraction of the sample rate. At lower sample rates, all resonances shift down:

| Filter | @44100 Hz | @22050 Hz | @11025 Hz |
|--------|-----------|-----------|-----------|
| 2 | 568 Hz | 284 Hz | 142 Hz |
| 3 | 4204 Hz | 2102 Hz | 1051 Hz |
| 4 | 1242 Hz | 621 Hz | 310 Hz |

**At 22050 Hz:** Filter 4's resonance drops to 621 Hz -- deep in the bass-midrange. Its ringing on transients now adds a low "thud" rather than a mid "ping." Filter 3 drops to 2102 Hz, moving from the presence band to the upper midrange. The overall coloration shifts from "metallic/bright" toward "warm/boxy."

**At 11025 Hz:** Filter 4 resonates at 310 Hz -- firmly in the bass. The ringing character becomes a low, almost subharmonic "bloom" after transients. This creates a very different character: less "PS1 metallic" and more "vintage shortwave radio." Most PS1 games that used 11025 Hz for samples did so for voice clips and sound effects where quality was less critical.

### Ring Time in Absolute Time vs Samples

The ring times in *samples* are constant (214 samples for Filter 4's 60dB decay), but the ring times in *milliseconds* scale inversely with sample rate:

| Filter | @44100 Hz | @22050 Hz | @11025 Hz |
|--------|-----------|-----------|-----------|
| 4 (60dB) | 4.9 ms | 9.7 ms | 19.4 ms |
| 3 (60dB) | 2.1 ms | 4.1 ms | 8.3 ms |
| 2 (60dB) | 1.5 ms | 3.0 ms | 6.0 ms |

**At 11025 Hz, Filter 4 rings for nearly 20 milliseconds** -- long enough to be clearly audible as a pitched decay on any percussive sound. This is approaching the range where a recording engineer would identify it as a "resonance" or even a "pitch" rather than an artifact.

### The "Sweet Spot" and Why 22050 Hz Was Common

Most PS1 games used 22050 Hz for music samples, and the sonic reasons are instructive:
- The block rate (787 Hz) is low enough that block-boundary artifacts are pushed below the region of maximum ear sensitivity (~2-5 kHz)
- The Nyquist limit (11025 Hz) still captures most musically relevant content
- Filter ringing is in a mid-range that blends with typical program material rather than sticking out
- The 2:1 memory savings over 44100 Hz was critical given the 512KB SPU RAM budget

**At 44100 Hz**, the coloration is "brighter" and "thinner" -- the filter resonances sit higher and the block rate is at a frequency where the ear is more sensitive.

**At 11025 Hz**, the coloration is "darker" and "muddier" -- filter resonances sit in the bass, the Nyquist limit cuts everything above 5.5 kHz, and the extended ring times make filter artifacts more pronounced.

**22050 Hz sits in the middle** -- the "PS1 sweet spot" where ADPCM coloration adds character without being obviously broken.

---

## 7. Implications for SPU-94 Implementation and Testing

### What to Listen For During Validation

When the ADPCM encode/decode round-trip is implemented and wired into the reverb, here is what to evaluate:

1. **Granular noise floor:** Feed a sine wave through ADPCM -> reverb. The reverb tail should develop a grainy, textured quality that clean PCM does not produce. If the tail is smooth, the ADPCM coloration is not feeding through correctly.

2. **Filter ringing on transients:** Feed a single drum hit (snare, rim shot) through the full path. Listen for a brief pitched "ring" after the attack -- around 1242 Hz for Filter 4, 4204 Hz for Filter 3. If the encoder is working correctly, you should hear different timbral colors on the attack vs. the sustain as the filter selection changes.

3. **Idle channel behavior:** Let the reverb tail decay fully. The very end of the tail should have a "buzzy" or "fizzy" quality rather than fading to clean silence. This is the ADPCM idle-channel noise being recirculated.

4. **A/B with clean PCM:** Toggle `spu94_set_adpcm_enabled()` and compare the same reverb preset. The ADPCM version should sound "grittier," "more textured," and slightly "darker" (the quantization noise adds energy in the midrange that wasn't in the original signal, partially masking high frequencies through auditory masking).

### Test Signals to Use

| Test Signal | What It Reveals | Expected Behavior |
|-------------|-----------------|-------------------|
| 1 kHz sine wave, -6 dBFS | Filter 4 prediction quality, residual noise level | Very clean -- Filter 4 resonance at 1242 Hz nearly matches. Low residual. |
| 1 kHz sine wave, -40 dBFS | Granular noise visibility | Noise becomes visible around the waveform. "Fuzzy" sine wave on oscilloscope. |
| White noise burst | Filter 0 selection (broadband signal defeats prediction) | Maximum coloration. Obvious bitcrushing quality. |
| Single snare hit | Transient handling, filter switching, ringing | Brief slope overload on attack, then filter switch to 2/3/4 on decay with audible ringing. |
| Sustained vocal "aaaah" | Filter 2 or 4 prediction, noise floor | Clean fundamental, slight granular noise on harmonics. |
| Reverb tail only (mute dry signal) | ADPCM artifact accumulation in feedback network | Grainy, metallic tail with buzzy end. |
| Silence | Idle channel noise character | Should hear very low-level buzz/crackle, not true silence. |

### Spectral Analysis Recommendations

For validation tooling (Python/numpy/scipy):
- Compute spectrograms of clean vs. ADPCM round-trip to visualize noise floor changes
- Plot difference signal (clean PCM minus ADPCM round-trip) to isolate the pure coloration
- FFT the difference signal to verify it is NOT white noise (should show signal-correlated structure)
- Measure per-block filter selection histogram to verify the encoder is using all 5 filters appropriately

---

## Sources

### High Confidence (verified against specifications or computed from exact coefficients)
- [nocash psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) -- block format, filter coefficients
- [nocash psx-spx: XA-ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) -- coefficient tables, decode formula
- [jsgroth: PlayStation SPU Part 1](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) -- decode algorithm walkthrough, filter confirmation
- Pole-zero analysis computed directly from exact integer coefficients (K0, K1 values from specification)

### Medium Confidence (cross-referenced across multiple sources)
- [Quantization Noise in ADPCM Systems (IEEE, 1977)](https://ieeexplore.ieee.org/document/1093799/) -- three noise categories (granular, slope overload, saturation)
- [ADPCM Wikipedia](https://en.wikipedia.org/wiki/Adaptive_differential_pulse-code_modulation) -- general ADPCM characteristics, signal-correlated noise
- [Idle Channel Performance of DPCM (Bell Labs, 1966)](https://onlinelibrary.wiley.com/doi/abs/10.1002/j.1538-7305.1966.tb01690.x) -- idle channel noise behavior
- [adpcm-xq: Xtreme Quality IMA-ADPCM](https://github.com/dbry/adpcm-xq) -- IMA-ADPCM noise characteristics, lookahead techniques
- [PlayStation1Vsts](https://github.com/BodbDearg/PlayStation1Vsts) -- PS1 SPU emulation VST, confirms reverb + ADPCM interaction
- [Ringing Artifacts (Wikipedia)](https://en.wikipedia.org/wiki/Ringing_artifacts) -- IIR filter ringing on transients, pole proximity effects

### Low Confidence (community discussion, secondary sources -- included for color, not as authority)
- [SoundCy: PS1 Sound Downsampling](https://soundcy.com/article/how-much-were-sounds-downsampled-ps1) -- sample rate choices, general quality descriptions
- [Splice: How Tape Machines Shaped Lo-Fi Sound](https://splice.com/blog/how-tape-machines-shaped-lo-fi-sound/) -- tape character comparison
- [Sound Processing Station (SPS)](https://soundfontguy.itch.io/sps) -- PS1 SPU emulation plugin, community descriptions of PS1 sound

---

*Deep coloration research for: PS1 SPU ADPCM (M2 milestone, SPU-94)*
*Researched: 2026-04-26*
