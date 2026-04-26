# Comrex Frequency Extender — Deep Research

**Domain:** Broadcast audio frequency extension / frequency shifting as creative effect
**Researched:** 2026-04-26
**Overall confidence:** HIGH (core algorithm), MEDIUM (specific model internals), LOW (exact analog circuit topology)

---

## 1. How It Works

### The Problem

Standard telephone lines (POTS — Plain Old Telephone Service) pass audio from roughly 300 Hz to 3100 Hz. That is just over three octaves — enough for speech intelligibility but terrible for broadcast audio. Bass is completely absent. Music sounds like it is coming through a tin can.

### The Solution: Frequency Shifting

The Comrex Frequency Extender shifts the entire audio spectrum **up by 250 Hz** at the transmit (encode) side before sending it down the phone line. At the receive (decode) side, it shifts the spectrum back **down by 250 Hz** to restore the original frequencies.

**What this accomplishes:**

- Audio at 50 Hz gets shifted to 300 Hz — now it passes through the phone line
- Audio at 2850 Hz gets shifted to 3100 Hz — still within the phone line passband
- The effective transmitted bandwidth becomes **50 Hz to 2850 Hz** instead of 300-3100 Hz
- This recovers roughly two extra octaves of low-frequency content

**The tradeoff:** You lose the top end (2850-3100 Hz of the original signal gets pushed above the phone line's passband and is lost), but you gain the bottom end (50-300 Hz). For voice and most broadcast content, the low frequencies matter far more than the extreme high end.

### The Algorithm: Single-Sideband (SSB) Modulation

The frequency shift is accomplished via **single-sideband modulation**, the same technique used in amateur and military radio communications. This is the core algorithm:

1. **Input signal** x(t) contains frequencies f1, f2, f3, etc.
2. **Multiply by a carrier** at frequency fc (250 Hz for the Comrex)
3. Ring modulation would produce **both** sum and difference frequencies: (f + fc) and (f - fc)
4. SSB modulation suppresses one sideband, keeping **only** the upper sideband: (f + fc)
5. Result: every frequency component is shifted up by exactly 250 Hz

**To suppress one sideband**, you need a **Hilbert transform** (90-degree phase shift network). The math:

```
Given input signal x(t):
  x_analytic(t) = x(t) + j * H{x(t)}     where H{} is the Hilbert transform

Frequency shift up by fc:
  y(t) = Re{ x_analytic(t) * e^(j*2*pi*fc*t) }
       = x(t) * cos(2*pi*fc*t) - H{x(t)} * sin(2*pi*fc*t)

Frequency shift down by fc (decoder):
  y(t) = Re{ x_analytic(t) * e^(-j*2*pi*fc*t) }
       = x(t) * cos(2*pi*fc*t) + H{x(t)} * sin(2*pi*fc*t)
```

In the original analog Comrex units (1978 era), this was implemented with **analog phase-difference networks** — cascaded allpass filters that maintain approximately 90 degrees of phase difference across the audio band. This is identical in principle to the Bode frequency shifter used in synthesizers, just with a fixed carrier frequency of 250 Hz instead of a variable one.

### Multi-Line Operation

For higher bandwidth, Comrex developed multi-line systems that split the audio spectrum into bands, shift each band into the phone line passband, and send each on a separate phone line:

| Lines | Bandwidth | How |
|-------|-----------|-----|
| 1 | ~2.8 kHz (50-2850 Hz) | Shift up 250 Hz |
| 2 | ~5 kHz | Line 1: direct passband. Line 2: shift 2800-5600 Hz down by 3 kHz into phone passband. Mix at receive. |
| 3 | ~8 kHz | Three bands, each ~2.8 kHz, shifted into three phone lines |
| 4 | ~11 kHz | Four bands (diminishing returns) |
| 5 | ~14 kHz | Five bands (approaching FM quality, rarely practical) |

The multi-line approach faces **diminishing returns** because each successive octave is double the bandwidth of the last, but each phone line provides only a fixed ~2.8 kHz of usable bandwidth.

---

## 2. Model History

### Comrex Product Timeline

| Year | Model(s) | Type | Key Feature |
|------|----------|------|-------------|
| 1978 | PLX, TLX | Transmitter (encoder) | Original single-line frequency extender, 250 Hz shift |
| 1978 | RLX | Receiver (decoder) | Companion decoder for PLX/TLX |
| 1978 | 2F | Transmitter-receiver | Combined encode/decode in one unit |
| 1983 | PTLX, RTLX | Two-line encoder/decoder | 5 kHz bandwidth over two phone lines; five-band companding noise reduction |
| 1985 | STLX | Sports console | Two-line frequency extender built into a sportscaster mixer |
| 1987 | PLXMicro | Portable encoder | Original design repackaged in portable case; cellphone-compatible |
| 1989 | 2XP, 2XR | Two-line encoder/decoder | Replaced PTLX/RTLX; improved two-line system |
| 1990 | 3XP, 3XR | Three-line encoder/decoder | 8 kHz bandwidth; audio split into three bands across three lines |
| ~1990s | LXL, LXTR | Unknown variant | Listed in Comrex support pages |
| 1997 | Hotline | POTS codec | Digital replacement; 10 kHz bidirectional — **obsoleted analog frequency extenders** |
| 1998 | Vector | POTS codec | 15 kHz response; backward-compatible with Hotline |

**Naming convention:**
- P = Portable / Transmit side
- R = Receive side
- LX = "Line eXtender"
- T = Telephone / Transmitter
- 2X, 3X = number of phone lines used

The PLX unit is held in the Science Museum Group Collection (UK), catalog #1990-36, noted as having been used by BBC Radio One's Simon Bates and Jonathan Ruffle on their round-the-world trip in 1989.

### Competitors

| Company | Product | Notes |
|---------|---------|-------|
| **Gentner** | EFT-3000 | Digital frequency extender; split ~7.5 kHz audio into up to three ~2.5 kHz passbands; shifted and sent on separate phone lines. Later acquired by / merged with Comrex. |
| **Marti Electronics** | STL systems | Primarily microwave studio-to-transmitter links rather than telephone frequency extension. Sold to Broadcast Electronics in 1994. |
| **Republic Telcom Systems** | Line expander (US4755994) | Different approach: digitized speech, used autocorrelation pitch detection + full-period splicing to time-compress speech to half bandwidth, then frequency-stacked two conversations on one line. Not a broadcast product — telecom capacity doubling. |

Comrex was the dominant player in broadcast frequency extension. Gentner was the main competitor and eventually became part of Comrex.

---

## 3. What Encode-Only Sounds Like

This is the most creatively interesting part for SPU-94's "digital patina engine" concept.

### What happens when you only encode (shift up 250 Hz) without decoding

Every frequency in the input is shifted up by a fixed 250 Hz. This is **not** pitch shifting — it is **frequency shifting**, and the distinction is critical:

| Operation | 100 Hz fundamental | 200 Hz (2nd harmonic) | 300 Hz (3rd harmonic) | Ratio 2nd:1st | Ratio 3rd:1st |
|-----------|-------------------|-----------------------|-----------------------|---------------|---------------|
| Original | 100 | 200 | 300 | 2:1 | 3:1 |
| Pitch shift +1 octave | 200 | 400 | 600 | 2:1 | 3:1 |
| **Freq shift +250 Hz** | **350** | **450** | **550** | **1.29:1** | **1.57:1** |

Frequency shifting **destroys harmonic relationships**. The overtone series becomes **inharmonic**. This is why it sounds metallic, bell-like, and alien — the same physics that makes bells and metallic percussion sound the way they do. Their partials are inharmonic.

### Why drums through encode-only sound awesome

Drums are already rich in inharmonic content (especially cymbals, snares, toms). Frequency shifting them by 250 Hz:

1. **Kick drums:** The fundamental shifts up, the body thins out, but the attack transient gains a metallic ring. The beater click at ~3-5 kHz shifts to ~3.25-5.25 kHz — subtle but adds sheen.

2. **Snares:** The wire buzz and body become completely transformed. The harmonic content of the drum shell resonance becomes inharmonic, producing a **sizzling, metallic, almost synthesized** quality. Snare wires, already noisy/inharmonic, shift into a different spectral region.

3. **Hi-hats and cymbals:** Already inharmonic, so the shift produces a **different flavor of metallic** rather than a fundamentally different character. The result is like hearing cymbals from an alternate universe — recognizable but wrong in an interesting way.

4. **Toms:** The pitched resonance of toms becomes detuned from its natural harmonics. A tom hit that normally decays as a pitched note instead decays with a **bell-like, gamelan-adjacent** quality.

The 250 Hz shift is large enough to be clearly audible and produce strong inharmonic effects, but small enough that transient characteristics (attack shapes, envelope contours) are preserved. The rhythm and dynamics come through; the timbre is radically altered.

**This is functionally identical to a Bode frequency shifter** set to +250 Hz — because that is literally what the Comrex encoder is doing. The Bode 1630 Frequency Shifter (designed for Moog modular synthesizers) uses the exact same SSB modulation technique. The Comrex just has a fixed shift amount where the Bode has a knob.

---

## 4. Technical Implementation in DSP/C

### Core Operations Required

1. **Hilbert Transform** — produce a 90-degree phase-shifted copy of the input
2. **Quadrature oscillator** — generate cos(2*pi*fc*n/fs) and sin(2*pi*fc*n/fs) at the shift frequency
3. **Complex multiplication** — combine the analytic signal with the oscillator
4. **Take real part** — extract the frequency-shifted output

### IIR Allpass Approach (Recommended — Low Latency, Low CPU)

Use two parallel chains of cascaded first-order allpass filters whose phase responses differ by ~90 degrees across the audio band. This is the approach used in the original analog Comrex units and the Bode frequency shifter.

**Allpass section transfer function:**

```
H(z) = (a^2 + z^-2) / (1 + a^2 * z^-2)

Processing equation:
  out[n] = a^2 * (in[n] + out[n-2]) - in[n-2]
```

One multiplication per section. Eight sections total (four per path) = eight multiplications for the entire Hilbert transform.

**Proven coefficients** (from Olli Niemitalo, optimized via differential evolution, +-0.7 degree phase error, 20 Hz to 20 kHz at 44.1 kHz):

```c
// Path A coefficients (a^2 values) — apply 1-sample delay to this path
static const double hilbert_a[4] = {
    0.6923878,
    0.9360654322959,
    0.9882295226860,
    0.9987488452737
};

// Path B coefficients (a^2 values)
static const double hilbert_b[4] = {
    0.4021921162426,
    0.8561710882420,
    0.9722909545651,
    0.9952884791278
};
```

**Complete frequency shifter in C (sketch):**

```c
typedef struct {
    // Hilbert transform state (4 sections per path, 2 delay elements each)
    double a_state[4][2];  // path A delay lines
    double b_state[4][2];  // path B delay lines
    double a_prev;         // 1-sample delay for path A

    // Quadrature oscillator
    double osc_phase;
    double osc_freq;       // shift frequency in Hz
    double sample_rate;
} FreqShifter;

static double allpass_section(double input, double a2, double state[2]) {
    double output = a2 * (input + state[1]) - state[0];
    state[1] = state[0];
    state[0] = output;  // Note: state[0] is current output for next iteration's in[n-2]
    // Actually need to track input too:
    // More precisely:
    //   out[n] = a2 * (in[n] + out[n-2]) - in[n-2]
    // state needs: { in[n-2], out[n-2] }
    return output;
}

// More careful implementation:
typedef struct {
    double in_z2;   // in[n-2]
    double out_z2;  // out[n-2]
} AllpassState;

static double allpass(double input, double a2, AllpassState *s) {
    double output = a2 * (input + s->out_z2) - s->in_z2;
    s->in_z2 = input;    // Will become in[n-2] two samples later
    // Need intermediate storage — this is a 2-sample delay structure
    // Correct version needs in[n-1] and out[n-1] too
    return output;
}

// The actual per-sample process:
void freq_shift_process(FreqShifter *fs, const float *in, float *out,
                        float *out_down,  // optional: down-shifted output
                        int num_samples) {
    for (int i = 0; i < num_samples; i++) {
        double x = in[i];

        // Run through path A (4 cascaded allpass sections)
        double path_a = x;
        for (int j = 0; j < 4; j++)
            path_a = allpass(path_a, hilbert_a[j], &fs->a_state[j]);
        // Apply 1-sample delay to path A
        double real_part = fs->a_prev;
        fs->a_prev = path_a;

        // Run through path B (4 cascaded allpass sections)
        double imag_part = x;
        for (int j = 0; j < 4; j++)
            imag_part = allpass(imag_part, hilbert_b[j], &fs->b_state[j]);

        // Quadrature oscillator
        double cos_w = cos(fs->osc_phase);
        double sin_w = sin(fs->osc_phase);
        fs->osc_phase += 2.0 * M_PI * fs->osc_freq / fs->sample_rate;
        if (fs->osc_phase > 2.0 * M_PI)
            fs->osc_phase -= 2.0 * M_PI;

        // SSB modulation — upper sideband (shift up)
        out[i] = (float)(real_part * cos_w - imag_part * sin_w);

        // Lower sideband (shift down) — if needed for decode
        if (out_down)
            out_down[i] = (float)(real_part * cos_w + imag_part * sin_w);
    }
}
```

**Note on the allpass state:** The 2-sample-delay allpass sections need careful state management. Each section needs to track `{in[n-1], in[n-2], out[n-1], out[n-2]}` for the recurrence `out[n] = a^2 * (in[n] + out[n-2]) - in[n-2]`. The sketch above is simplified — production code should use a properly validated allpass cascade. The Csound `hilbert` opcode and numerous open-source implementations (soundspotter/HilbertTransform on GitHub) provide tested reference code.

### FIR Approach (Alternative — Higher Latency, Easier to Validate)

An FIR Hilbert transformer uses the impulse response `h[n] = 2/(pi*n)` for odd n, 0 for even n, windowed and made causal. Advantages: linear phase, easy to verify. Disadvantages: significant latency (half the filter length), more multiplications. A Hamming window provides ~18 dB better sideband suppression than rectangular.

For a creative effect (not a codec), the IIR approach is strongly preferred because:
- Near-zero latency (critical for real-time use in a DAW)
- Very low CPU (8 multiplications + oscillator per sample)
- The slight phase imperfection adds character consistent with the original analog units

### Sample Rate Considerations

The original Comrex units were analog (no sample rate). For digital implementation:

- **44.1 kHz / 48 kHz:** The Niemitalo coefficients above are designed for this range. Phase error stays within +-0.7 degrees from 20 Hz to 20 kHz.
- **Higher sample rates:** Coefficients need recalculation or the input should be downsampled. The allpass poles are sample-rate-dependent.
- **Anti-aliasing:** When shifting up by fc Hz, apply a lowpass filter at (fs/2 - fc) to prevent aliasing. For a 250 Hz shift at 44.1 kHz, this means filtering above ~21.8 kHz — well above audibility, so no filter needed in practice.

### Fixed-Point Feasibility

The allpass coefficients are close to 1.0 (ranging from 0.40 to 0.999). In Q15 or Q31 fixed-point, these are representable with good precision. The oscillator can use a lookup table or CORDIC. This is absolutely feasible on MCU/DSP hardware — the computational requirements are trivial compared to the SPU reverb network already implemented in SPU-94.

---

## 5. Related Technologies

### Broadcast Frequency Extension Family

| Technology | Mechanism | Era | Bandwidth |
|------------|-----------|-----|-----------|
| **Comrex Frequency Extender** | SSB frequency shift (250 Hz) | 1978-1990s | 2.8 kHz (1 line) to 14 kHz (5 lines) |
| **Gentner EFT-3000** | Digital frequency extension, splits into 2.5 kHz bands | 1980s-1990s | ~7.5 kHz (3 lines) |
| **POTS Codecs** (Comrex Hotline, Telos Zephyr) | Digital encoding (MPEG, CELP, proprietary) over dialup modem | 1997+ | 10-15 kHz on single line |
| **ISDN Codecs** | Digital audio over ISDN (64/128 kbps) | 1990s-2010s | 15-20 kHz |
| **IP Codecs** (Comrex ACCESS, Tieline) | Digital audio over internet | 2000s+ | Full bandwidth |

### Synthesizer Frequency Shifters (Same Algorithm, Different Application)

| Device | Type | Notes |
|--------|------|-------|
| **Bode/Moog 1630 Frequency Shifter** | Analog synthesizer module | Uses dome filters (analog allpass networks) for Hilbert transform. Variable carrier frequency. Identical SSB algorithm to Comrex. |
| **Polyfusion 755 Feedback Eliminator** | Analog rack unit | Frequency shifter limited to +-6 Hz shift; used to prevent PA feedback |
| **Ableton Frequency Shifter** | Software plugin | Built into Ableton Live |
| **Valhalla FreqEcho** | Software plugin | Frequency shifter + delay by Sean Costello |

### The Weaver Method (Alternative SSB Implementation)

Donald K. Weaver published "A Third Method of Generation and Detection of Single-Sideband Signals" (Proceedings of the IRE, December 1956). His method avoids the Hilbert transform entirely:

1. Use **four** ring modulators instead of two
2. Use **two** quadrature oscillators instead of one
3. Use **lowpass filters** instead of a wideband phase-shift network

The Weaver method avoids the difficulty of building a wideband 90-degree phase-shift network, trading it for more multipliers and an additional oscillator. In DSP where multipliers are cheap, both approaches work well. The maximum modulation frequency is limited to one-quarter of the sampling rate.

---

## 6. Patents and Published Technical Documentation

### Directly Relevant Patents

| Patent | Title | Assignee | Filed | Status | Relevance |
|--------|-------|----------|-------|--------|-----------|
| **US7080017B2** | "Frequency compander for a telephone line" | Fisher, Baxter, Holmes (individual) | 2002 | Expired 2024 | Describes a **digital** frequency compander using FFT-based spectrum compression. References Comrex-style 250 Hz shift as prior art. Contains alternative Hilbert-filter trigonometric method. Extremely detailed algorithmic description. |
| **US4755994A** | "Capacity expander for telephone line" | Republic Telcom Systems | 1985 | Expired 2005 | Time-domain speech compression via autocorrelation pitch detection + full-period splicing. Different approach from Comrex (pitch-based compression, not frequency shifting). Referenced by US7080017 as prior art. |

### Key Technical Details from US7080017B2

This patent describes the FFT-based approach to frequency companding and includes an alternative **trigonometric method** that is essentially a mathematical description of the Comrex algorithm:

**Compression (encode / shift down one octave):**
```
cos(X/2) = sqrt(1/2 + cos(X)/2)
```
Where cos(X) is the audio input and cos(X/2) is the output with frequency halved.

**Expansion (decode / shift up one octave):**
```
sin(2X) = 2 * sin(X) * cos(X)
```
This requires a Hilbert filter (minimum 17 coefficients) for the 90-degree phase shift to develop the cosine component from a sine input.

The patent also describes an **optional 250 Hz frequency shift** that can be applied on top of the compression:
```
sin(X + 250) = sin(X) * cos(250) + cos(X) * sin(250)
```

**The patent explicitly identifies this as the prior art technique used by analog frequency extenders.**

### No Comrex-Specific Patents Found

Comrex does not appear to have patented their frequency extender algorithm. This makes sense: SSB modulation was well-established radio engineering by the 1970s. The innovation was applying it to broadcast telephone remotes, not the algorithm itself. The technique was published knowledge — the product innovation was packaging and reliability.

### Academic References

- **Donald K. Weaver**, "A Third Method of Generation and Detection of Single-Sideband Signals," Proceedings of the IRE, December 1956. The foundational paper for the Weaver SSB method.
- **Scott Wardle**, "A Hilbert-Transformer Frequency Shifter for Audio," AES (available on ResearchGate). Describes IIR Hilbert transformer design for audio frequency shifting.
- **Harris, Berdahl, Abel**, "An Infinite Impulse Response (IIR) Hilbert Transformer Filter Design Technique for Audio," AES Convention Paper 129. Comprehensive design procedure for IIR Hilbert transformers starting from half-band filter design.
- **Olli Niemitalo** (yehar.com/blog/?p=368). Optimized allpass Hilbert transformer coefficients via differential evolution, with C-ready coefficient values.

---

## 7. The Broader Category of Frequency Extension

### Approaches to Wideband Audio Over Narrowband Channels

| Approach | How | Pros | Cons |
|----------|-----|------|------|
| **Frequency shifting** (Comrex) | SSB modulation shifts spectrum into phone passband | Simple, analog, low latency, preserves transients | Loses top-end bandwidth; multi-line = multiple phone bills |
| **Frequency compression** (US7080017) | FFT-based spectrum squeezing; halves all frequencies | Doubles bandwidth on single line | Degrades SNR; introduces artifacts; requires DSP |
| **Time-domain compression** (Republic Telcom US4755994) | Pitch detection + period splicing; removes redundant pitch periods | Can double line capacity (2 calls on 1 line) | Only works well for voiced speech; complex pitch tracking |
| **Digital coding** (POTS codecs) | Modem-based digital encoding (MPEG, G.722, etc.) | High bandwidth, single line | Latency, codec artifacts, depends on line quality |
| **Companding noise reduction** | Multi-band compression on transmit, expansion on receive | Improves effective SNR | Does not actually extend bandwidth; complements other approaches |

### The Comrex Two-Line System's Five-Band Compander

The PTLX/RTLX (1983) and 2XP/2XR (1989) combined frequency extension with a **five-band companding noise reduction system**. This compressed the dynamic range in five frequency bands before transmission, then expanded it back at the receiver — essentially a broadcast-specific version of Dolby or dbx noise reduction optimized for telephone line noise characteristics. This was layered on top of the frequency shifting, not a replacement for it.

---

## 8. Implementation Recommendations for SPU-94

### As a Creative Effect Module

The Comrex encode-only effect is a **frequency shifter** — a well-understood DSP operation with a tiny computational footprint. It fits naturally into SPU-94's "digital patina engine" concept:

**Recommended implementation:**
- IIR allpass Hilbert transformer (Niemitalo coefficients)
- Fixed-point compatible (Q31 arithmetic)
- Shift amount as a parameter (default 250 Hz for authentic Comrex, but make it variable like a Bode)
- Optional: wet/dry mix for blending shifted and original signal
- Optional: both up-shift and down-shift outputs (like a proper Bode frequency shifter)

**Computational cost:** ~10 multiplications + 1 sin/cos per sample. Negligible compared to the SPU reverb network.

**Character notes:**
- At exactly 250 Hz shift, this reproduces the Comrex encode-only sound
- At small shifts (1-10 Hz), produces phasing / barberpole effects
- At moderate shifts (50-500 Hz), produces the metallic / inharmonic character
- At large shifts (1000+ Hz), produces extreme ring-modulator-like effects
- Negative shifts (down-shifting) produce a different character — lower, darker, submarine-like

### Integration with SPU-94 Signal Chain

Could be placed at multiple points:
1. **Pre-reverb:** Shift signal before reverb processing — the reverb then operates on inharmonic content, producing unique decay textures
2. **Post-reverb:** Shift the reverb output only — dry signal stays clean, reverb tail becomes metallic
3. **Standalone:** Independent effect module in the patina engine

### What NOT to Build

Do not attempt to implement the full encode-decode frequency extender as a broadcast tool. That is a solved problem with no modern relevance. The creative value is entirely in the **encode-only misuse** — the frequency shifter as an effect.

---

## Sources

### Primary Sources (HIGH confidence)
- [Comrex Wikipedia article](https://en.wikipedia.org/wiki/Comrex) — Company and product history
- [Comrex Frequency Extenders resources](https://www.comrex.com/resources/frequency-extenders/) — Official model list and manuals
- [Frequency Extender Wikipedia article](https://en.wikipedia.org/wiki/Frequency_extender) — Technical overview of the category
- [US7080017B2 Patent — Frequency Compander](https://patents.google.com/patent/US7080017) — Detailed algorithmic description with Comrex prior art references
- [US4755994A Patent — Capacity Expander](https://patents.google.com/patent/US4755994) — Alternative approach (Republic Telcom)

### Technical Implementation Sources (HIGH confidence)
- [Olli Niemitalo — Hilbert Transform allpass coefficients](https://yehar.com/blog/?p=368) — Optimized IIR coefficients
- [Nathan Ho — Analog-Style Frequency Shifter](https://nathan.ho.name/posts/frequency-shifter/) — Phase difference network design with pole values
- [katjaas.nl — Hilbert Transform](https://www.katjaas.nl/hilbert/hilbert.html) — Allpass biquad implementation details
- [Risto Holopainen — Frequency Shifting](https://ristoid.net/modular/freqshift.html) — Mathematical foundations, FIR and IIR approaches
- [soundspotter/HilbertTransform on GitHub](https://github.com/soundspotter/HilbertTransform) — C reference implementation

### Sound Design and Effect Character (MEDIUM confidence)
- [Valhalla DSP — Frequency Shifting, 10 Years Later](https://valhalladsp.com/2009/05/18/frequency-shifting-10-years-later/) — Creative applications
- [McGill — Frequency and Pitch Shifting](https://cim.mcgill.ca/~clark/nordmodularbook/nm_spectrum_shift.html) — Technical distinction between freq and pitch shifting, Weaver method
- [Dogs on Acid — Frequency Shifting Drums](https://www.dogsonacid.com/threads/frequency-shifting-drums.800437/) — Community discussion of drums through freq shifters

### Broadcast History (MEDIUM confidence)
- [Comrex PLX in Science Museum Group Collection](https://collection.sciencemuseumgroup.org.uk/objects/co34715/comrex-encoder-decoder-unit-type-plx-for-frequency-extension-in-telephone-channels) — Museum record of PLX unit
- [Telos Alliance — Vintage Audio: Comrex Buddy](https://blogs.telosalliance.com/buddy-remote-mixer) — Broadcast history context
