# Architecture Research: DAC Modeling Integration into libspu94

**Domain:** PS1 AKM AK4309AVM delta-sigma DAC coloration modeling as an optional stage in libspu94
**Researched:** 2026-04-28
**Confidence:** HIGH on signal chain placement and integration pattern (follows ADPCM precedent, hardware signal flow is documented). MEDIUM on specific artifact modeling parameters (AK4309AVM datasheet is scarce; measured behavior available from Stereophile/Archimago but not full characterization). MEDIUM on state budget (estimates below are well within the 15.5 KB remaining budget).

---

## 1. The PS1 DAC: Hardware Facts

### 1.1 Chip Identification

The PS1 uses the **AKM (Asahi Kasei Microsystems) AK4309AVM** in early board revisions (PU-7, PU-8, SCPH-1001/1002/5501). Later revisions (SCPH-700x onward) use the AK4309BM or integrate the DAC into the CD/DSP combo chip.

SPU-94 targets the SCPH-1001/5501 era -- the "audiophile PS1" that Anthony owns -- so the AK4309AVM is the reference.

Sources: [dogbreath.de PS1 DAC page](https://dogbreath.de/PS1/DAC/DAC.html), [Archimago PS1 measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html), [emu-russia psxrev SPU wiki](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md).

### 1.2 Converter Topology

**1-bit delta-sigma ("bitstream") DAC.** Not R2R.

Key AK4309AVM specifications (from datasheet fragments and distributor pages):

| Parameter | Value | Source |
|-----------|-------|--------|
| Architecture | 1-bit delta-sigma | Datasheet, multiple confirmations |
| Resolution | 16-bit input | Datasheet |
| Oversampling | 8x FIR interpolator | Datasheet summary |
| Reconstruction | 2nd-order SCF + CTF | Datasheet (Switched Capacitor Filter + Continuous Time Filter) |
| Dynamic range (rated) | 90 dB | Datasheet |
| Dynamic range (measured) | ~85-90 dB (~14-15 bit effective) | Stereophile, Archimago |
| THD+N | -84 dB (rated) | Datasheet |
| Frequency response | +/-0.5 dB at 20 kHz | Datasheet |
| Sample rate range | 8 kHz - 50 kHz | Datasheet |
| Master clock | 256fs or 384fs | Datasheet |
| Output | Single-ended, 3.4 Vpp, on-chip buffer | Datasheet |
| Package | 20-pin SSOP (AK4309B) / 24-pin (AK4309AVM) | Datasheet, dogbreath |

### 1.3 SPU-to-DAC Interface

The SPU outputs a serial digital audio stream to the DAC via three pins:
- **DATO** (SDATA) -- serial audio data, 16-bit signed, MSB-first
- **LRCO** (LRCK) -- left/right clock (frame sync), LRC=1 for left, LRC=0 for right
- **BCKO** (BICK) -- bit clock
- **XCK** -- system/master clock

The SPU outputs **16-bit signed PCM at 44.1 kHz** to the DAC. The DAC receives this digitally and performs all conversion internally (8x oversampling, delta-sigma modulation, analog reconstruction).

Source: [emu-russia psxrev SPU](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md), [psx-spx pinouts](https://psx-spx.consoledev.net/pinouts/).

### 1.4 Measured Characteristics (Real PS1 Units)

From Stereophile and Archimago measurements of SCPH-5501:

- **Effective resolution:** ~14-15 bits (noise floor 15 dB above good CD players)
- **Frequency response:** slight deviation above 3 kHz; ripple in top three octaves attributed to "underspecified digital filter"
- **Harmonic distortion:** low-order (2nd, 3rd) harmonics visible but subjectively innocuous
- **Jitter:** sidebands below -100 dB from primary 11 kHz J-test signal
- **Alias artifact:** visible at 23.9 kHz (near 44.1-20.2 kHz) in intermodulation tests
- **Noise floor:** elevated compared to modern DACs, with 60 Hz and 180 Hz hum peaks (transformer, not DAC)

**Key insight for modeling:** The PS1 DAC's audible character comes from three sources:
1. The 8x oversampling digital filter's passband ripple (frequency response coloration)
2. The delta-sigma quantization noise floor (~14-15 bit effective resolution)
3. The analog reconstruction path (2nd-order SCF + CTF -- gentle rolloff, not brick-wall)

The transformer hum and power-supply artifacts are board-level, NOT DAC-intrinsic. Out of scope per PROJECT.md ("DAC analog output stage deferred").

---

## 2. Signal Chain Placement

### 2.1 Where Does the DAC Sit in the Real Hardware?

```
SPU internal processing (22.05 kHz)
    |
    v
SPU output interpolation FIR (22.05 -> 44.1 kHz)
    |
    v
SPU serial output (44.1 kHz, 16-bit signed PCM)
    |
    v
AK4309AVM DAC chip
    |-- 8x oversampling FIR interpolator (44.1 -> 352.8 kHz internal)
    |-- delta-sigma modulator (1-bit output)
    |-- 2nd-order SCF (switched-capacitor filter)
    |-- CTF (continuous-time filter)
    |
    v
Analog output (AOUTL, AOUTR)
```

The DAC sees the **post-FIR 44.1 kHz signal**. This is unambiguous: the SPU outputs 44.1 kHz digital serial data to the DAC.

### 2.2 Where in libspu94's Signal Chain?

Current signal chain in `spu94_process`:

```
44.1 kHz input
    |
    v
[ADPCM encode/decode] (optional, default-off)
    |
    v
FIR decimation (44.1 -> 22.05 kHz)
    |
    v
reverb network (22.05 kHz)
    |
    v
FIR interpolation (22.05 -> 44.1 kHz)
    |
    v
44.1 kHz output  <-- DAC model goes HERE
```

**The DAC model operates at 44.1 kHz, AFTER the output FIR interpolation.** This matches hardware: the SPU's digital output at 44.1 kHz feeds the DAC.

**Recommendation: place DAC modeling as the LAST stage before output, operating on the 44.1 kHz post-FIR signal.**

Rationale:
- Matches physical signal flow exactly
- The FIR output is what the real DAC sees
- Clean separation: reverb core stays untouched, DAC is a post-processor
- Same pattern as ADPCM (pre-processor on input side)

### 2.3 NOT at 22.05 kHz

The DAC must NOT operate at 22.05 kHz. The real DAC never sees the 22.05 kHz internal signal -- it only sees the SPU's final 44.1 kHz serial output. Placing DAC modeling at 22.05 kHz would:
- Model the wrong signal (pre-interpolation)
- Miss the FIR's frequency-domain shaping
- Be physically incorrect

### 2.4 Integration Point in Code

In `spu94_process.c`, the DAC stage inserts after `spu94_fir_chain_step` returns:

```c
/* Current code (simplified): */
spu94_fir_chain_step(state, l, r, &lo, &ro);
if (L_out != NULL) L_out[i] = lo;
if (R_out != NULL) R_out[i] = ro;

/* With DAC stage: */
spu94_fir_chain_step(state, l, r, &lo, &ro);
if (state->dac_enabled) {
    spu94_dac_process_sample(state, &lo, &ro);
}
if (L_out != NULL) L_out[i] = lo;
if (R_out != NULL) R_out[i] = ro;
```

This is the ONLY code change to the existing audio path. Everything else is new code in a new file.

---

## 3. What to Model (and What Not To)

### 3.1 Modelable Artifacts

Three artifact categories, ordered by audible impact and implementation complexity:

#### Artifact 1: Quantization Noise Floor (delta-sigma noise shaping)

The AK4309's delta-sigma modulator shapes quantization noise upward in frequency. At 16-bit input, the effective resolution is ~14-15 bits, meaning ~1-2 LSBs of noise are added. This is the DAC's most audible coloration on quiet passages.

**Model approach:** Add shaped noise. A 1st-order noise-shaping profile (high-pass filtered TPDF dither scaled to ~1-2 LSB) approximates the delta-sigma noise floor without needing to simulate the actual modulator. The noise is spectrally shaped -- more energy above 10 kHz, less below -- matching delta-sigma behavior.

**Implementation:** Per-sample LFSR-based noise generator with a 1st-order high-pass filter, scaled to 1-2 LSB amplitude. Fixed-point, zero-allocation.

**State needed:** ~8 bytes (LFSR state + filter state)

#### Artifact 2: Digital Filter Passband Ripple

The AK4309's 8x oversampling FIR has passband ripple measured at +/-0.5 dB at 20 kHz. This creates the "ripple in the top three octaves" noted by Stereophile. The filter's impulse response also affects time-domain behavior.

**Model approach:** A short FIR or IIR filter that approximates the measured frequency response deviation. Since we do NOT have the exact AK4309 filter coefficients (datasheet unavailable), this must be designed from the measured +/-0.5 dB spec and Stereophile's frequency response plots.

**Implementation:** Low-order IIR (biquad, 2nd order) or short FIR (8-16 taps) at 44.1 kHz. The passband ripple is subtle -- a gentle high-frequency shelf with slight undulation is likely sufficient. Fixed-point Q15 coefficients.

**State needed:** ~16-32 bytes (biquad: 4 delay elements x 2 channels x 2 bytes = 16; FIR: tap count x 2 channels x 2 bytes)

#### Artifact 3: Reconstruction Filter Rolloff (SCF + CTF)

The analog reconstruction uses a 2nd-order SCF followed by a CTF. This is a gentle lowpass, NOT a brick-wall filter. It allows some energy above Nyquist/2 to pass, which is part of the PS1's "airy" treble character. The 23.9 kHz alias visible in Stereophile's measurements is evidence of this gentle rolloff.

**Model approach:** A 2nd-order Butterworth or Bessel lowpass at ~20-22 kHz, implemented digitally at 44.1 kHz. Since the real filter is analog (continuous-time), the digital model uses bilinear-transform-designed IIR coefficients.

**Implementation:** One biquad per channel. Fixed-point Q15. Cutoff frequency tuned to match measured rolloff.

**State needed:** ~16 bytes (same as above)

### 3.2 What NOT to Model

| Artifact | Why Skip |
|----------|----------|
| Delta-sigma modulator internals | Requires 352.8 kHz (8x) processing rate. Massive CPU cost for inaudible benefit at 44.1 kHz output. The modulator's audible effect is captured by the noise-floor model. |
| DNL/INL nonlinearities | 1-bit delta-sigma DACs have no resistor ladder, so classical DNL/INL is irrelevant. Nonlinearity manifests as harmonic distortion, already captured by the noise model. |
| Analog output stage (op-amps, coupling caps) | Explicitly deferred in PROJECT.md. Board-level, not DAC-intrinsic. |
| Power supply artifacts (60/180 Hz hum) | Board-level noise, not DAC behavior. |
| Jitter | Measured below -100 dB. Inaudible. |
| Zero-order hold (ZOH) sinc droop | The AK4309 uses 8x oversampling, which virtually eliminates ZOH sinc droop within the audio band (droop at 20 kHz with 8x oversampling is < 0.01 dB). ZOH modeling is relevant for NOS (non-oversampling) DACs; the AK4309 is not NOS. |

### 3.3 Summary: Three-Layer Model

| Layer | Artifact | Complexity | Audibility | Priority |
|-------|----------|-----------|------------|----------|
| 1 | Noise floor (shaped dither) | Low | High on quiet material | Build first |
| 2 | Passband ripple (filter coloration) | Medium | Medium (treble character) | Build second |
| 3 | Reconstruction rolloff | Medium | Low-Medium (subtle treble) | Build third |

Each layer is independently toggleable (like ADPCM). A single `dac_enabled` flag gates all three; individual sub-flags are a refinement for later.

---

## 4. State Budget

### 4.1 Current Budget

| Component | Bytes |
|-----------|-------|
| Current `spu94_state` | 792 |
| `SPU94_STATE_SIZE_MAX` | 16,384 |
| **Remaining** | **15,592** |

### 4.2 DAC Stage Estimate

| DAC Component | Bytes | Notes |
|---------------|-------|-------|
| Enable flag (`dac_enabled`) | 1 | uint8_t, matches `adpcm_enabled` pattern |
| Noise LFSR state | 4 | uint32_t |
| Noise HP filter state (L+R) | 8 | 2 x int16_t prev_noise + 2 x int16_t prev_out |
| Passband ripple biquad (L) | 8 | 4 x int16_t delay elements |
| Passband ripple biquad (R) | 8 | 4 x int16_t delay elements |
| Reconstruction biquad (L) | 8 | 4 x int16_t delay elements |
| Reconstruction biquad (R) | 8 | 4 x int16_t delay elements |
| Biquad coefficients (shared) | 20 | 5 x int16_t (or int32_t for precision) per biquad x 2 biquads = 40 if Q15+Q30 |
| Padding/alignment | ~8 | worst case |
| **Total estimate** | **~80-120 bytes** | |

**Budget impact: negligible.** Even at the high estimate, DAC state is ~0.7% of the remaining budget. The 16 KB ceiling is not threatened.

If the passband ripple model uses a short FIR instead of biquad:
- 16-tap FIR x 2 channels x 2 bytes = 64 bytes for delay lines
- 16 taps x 2 bytes = 32 bytes for coefficients (shared between channels)
- Total FIR variant: ~96 bytes for the delay lines, still negligible

### 4.3 No Lookup Tables Needed

Unlike R2R DAC modeling (which might need DNL/INL lookup tables of 65536 entries), the delta-sigma model needs NO lookup tables. All processing is arithmetic: LFSR, multiply-accumulate for filters. This is ideal for the zero-heap, MCU-portable constraint.

---

## 5. Interaction with Existing Stages

### 5.1 ADPCM Interaction

ADPCM is a **pre-processor** (before FIR decimation). DAC is a **post-processor** (after FIR interpolation). They do not interact directly. Both can be enabled simultaneously:

```
Input -> [ADPCM] -> FIR down -> reverb -> FIR up -> [DAC] -> Output
```

This mirrors the real hardware signal flow: ADPCM-encoded voices are decoded by the SPU, mixed, reverbed, output through the FIR, and then the DAC converts. Enabling both gives the full PS1 coloration chain.

### 5.2 FIR Filter Interaction

The DAC model operates on the FIR interpolator's output. It does NOT modify FIR behavior. The FIR's passband characteristics and the DAC's passband ripple are additive -- both color the output. This is correct: in hardware, the SPU's half-band FIR and the DAC's oversampling FIR are in series.

### 5.3 Latency Impact

The DAC model adds:
- **Noise floor layer:** 0 samples latency (per-sample additive noise)
- **Passband ripple (biquad):** 0 samples group delay at DC (IIR, causal, minimal phase). Frequency-dependent group delay is part of the coloration.
- **Passband ripple (FIR):** ~N/2 samples if linear-phase FIR is used. For 16-tap FIR at 44.1 kHz, this is ~8 samples = 0.18 ms. Negligible, but must be added to `spu94_get_total_latency_samples` if FIR variant is chosen.
- **Reconstruction rolloff (biquad):** 0 samples group delay at DC.

**Recommendation: use IIR (biquad) for both filter stages.** This adds zero reported latency (IIR filters have instantaneous onset), matches the "analog filter modeling" use case, and uses less state than FIR. The ADPCM precedent added 28 samples of latency to the total; DAC with biquads adds zero.

If biquads are used: `spu94_get_total_latency_samples` is unchanged when DAC is enabled.

### 5.4 Latency Reporting Update

```c
/* Updated latency calculation: */
uint32_t spu94_get_total_latency_samples(const spu94_state *state) {
    if (state == NULL) return SPU94_LATENCY_SAMPLES;
    uint32_t lat = SPU94_LATENCY_SAMPLES;
    if (state->adpcm_enabled) lat += SPU94_ADPCM_BLOCK_SAMPLES;
    /* DAC biquads: zero additional latency (IIR, no lookahead) */
    /* If FIR variant: lat += SPU94_DAC_FIR_LATENCY; */
    return lat;
}
```

---

## 6. Component Decomposition

### 6.1 New Files

| File | Purpose | LOC Estimate |
|------|---------|-------------|
| `src/spu94/spu94_dac.c` | DAC model implementation: noise generator, filter processing, per-sample entry point | ~150-200 |
| `include/spu94/spu94_dac.h` | Public API: enable/disable, parameter query | ~30-40 |
| `tests/unit/dac/test_dac_noise.c` | Noise floor: spectral shape verification, amplitude bounds | ~100 |
| `tests/unit/dac/test_dac_filter.c` | Filter: frequency response verification against design target | ~100 |
| `tests/unit/dac/test_dac_integration.c` | Full-chain: DAC enabled with reverb, golden-file regression | ~80 |

### 6.2 Modified Files

| File | Change | Scope |
|------|--------|-------|
| `src/spu94/spu94_state_internal.h` | Add DAC state fields to `struct spu94_state` | ~10 lines |
| `src/spu94/spu94_process.c` | Insert DAC call after `spu94_fir_chain_step` | ~5 lines |
| `src/spu94/spu94_io_chain.c` | Add `spu94_set_dac_enabled` / `spu94_get_dac_enabled` (following ADPCM pattern) | ~20 lines |
| `src/spu94/spu94_state.c` | Zero DAC state in `spu94_init` / `spu94_reset` | ~5 lines |
| `include/spu94/spu94.h` | Include `spu94_dac.h`, update API docs | ~3 lines |
| `CMakeLists.txt` | Add `spu94_dac.c` to library sources, test targets | ~10 lines |

### 6.3 Unchanged Files

The reverb core (`spu94_reverb.c`, `spu94_tick.c`, `spu94_buffer.c`, `spu94_fir.c`) remains completely untouched. The DAC is a pure post-processor with no feedback into the reverb path. This matches ADPCM's zero-blast-radius integration.

---

## 7. Public API Design

Following the ADPCM precedent exactly:

```c
/* include/spu94/spu94_dac.h */
#ifndef SPU94_DAC_H
#define SPU94_DAC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spu94_state spu94_state;

/* Enable/disable the DAC coloration stage.
 * Default: disabled (0). When enabled, the DAC model processes
 * every output sample after FIR interpolation.
 * Disabling resets internal DAC filter state (no stale audio on re-enable).
 * Safe to call at any time, including mid-block. */
void spu94_set_dac_enabled(spu94_state *state, int enabled);
int  spu94_get_dac_enabled(const spu94_state *state);

#ifdef __cplusplus
}
#endif

#endif /* SPU94_DAC_H */
```

**Design notes:**
- Same enable/disable pattern as ADPCM (`spu94_set_adpcm_enabled`).
- Disable resets filter state (prevents stale audio leak, same as ADPCM disable).
- No separate "configure" API in v1.2. The DAC model targets a single reference (AK4309AVM). Future milestones could add DAC variant selection if PS1 board revisions with different DACs are modeled.
- No per-artifact sub-toggles in v1.2. Single flag. Simpler API, simpler testing. Sub-toggles are a v1.3+ refinement if needed.

---

## 8. Implementation Details

### 8.1 Noise Floor Model

```c
/* LFSR-based noise with 1st-order high-pass shaping.
 * Approximates delta-sigma quantization noise spectral profile:
 * rising noise floor from ~-96 dB at DC to ~-84 dB at 20 kHz.
 *
 * The LFSR generates white noise; the HP filter shapes it to match
 * delta-sigma noise profile (1st-order: +6 dB/octave slope).
 * Amplitude scaled to ~1 LSB RMS at the output. */

static inline int16_t dac_noise_sample(spu94_state *state) {
    /* 32-bit maximal-length LFSR (taps: 32, 22, 2, 1) */
    uint32_t lfsr = state->dac_lfsr;
    uint32_t bit = ((lfsr >> 31) ^ (lfsr >> 21) ^ (lfsr >> 1) ^ lfsr) & 1u;
    lfsr = (lfsr << 1) | bit;
    state->dac_lfsr = lfsr;

    /* Raw white noise, scaled to ~2 LSB peak */
    int16_t white = (int16_t)((lfsr >> 16) & 0x3) - 1; /* {-1, 0, 0, 1} */

    /* 1st-order HP: y[n] = x[n] - x[n-1] (difference = HP) */
    int16_t shaped = white - state->dac_noise_prev;
    state->dac_noise_prev = white;

    return shaped;
}
```

The noise amplitude and shaping are design parameters tuned during implementation against the Stereophile/Archimago measurements. The LFSR seed should be deterministic (seeded from a constant in `spu94_init`) for golden-file reproducibility, with an optional seed API for variation.

### 8.2 Filter Design Approach

Both biquad filters (passband ripple + reconstruction rolloff) use the standard direct-form-II-transposed structure in Q15 fixed-point:

```c
typedef struct {
    int16_t b0, b1, b2;  /* numerator coefficients, Q15 */
    int16_t a1, a2;       /* denominator coefficients, Q15 (a0 = 1 implicit) */
    int32_t z1, z2;       /* state variables, Q15 extended to Q30 for precision */
} spu94_biquad_t;

static inline int16_t biquad_process(spu94_biquad_t *bq, int16_t x) {
    int32_t in = (int32_t)x;
    int32_t out = (in * bq->b0 + bq->z1) >> 15;
    bq->z1 = in * bq->b1 - out * bq->a1 + bq->z2;
    bq->z2 = in * bq->b2 - out * bq->a2;
    return sat_s16(out);
}
```

**Coefficient design (offline, in Python):**
1. Passband ripple: design a low-shelf or peaking EQ that introduces +/-0.5 dB ripple in the 5-20 kHz range, matching Stereophile's measured frequency response.
2. Reconstruction rolloff: design a 2nd-order Butterworth LPF with cutoff ~20 kHz (bilinear transform from analog prototype).
3. Quantize coefficients to Q15 and verify frequency response in fixed-point.
4. Commit coefficients as constants in `spu94_dac.c` (same as FIR coefficients are constants in `spu94_fir_coef.c`).

### 8.3 Per-Sample Processing

```c
void spu94_dac_process_sample(spu94_state *state,
                              int16_t *l, int16_t *r) {
    /* Layer 1: shaped noise floor */
    int16_t noise_l = dac_noise_sample(state);
    int16_t noise_r = dac_noise_sample(state);

    int32_t sl = (int32_t)*l + noise_l;
    int32_t sr = (int32_t)*r + noise_r;

    int16_t nl = sat_s16(sl);
    int16_t nr = sat_s16(sr);

    /* Layer 2: passband ripple filter */
    nl = biquad_process(&state->dac_ripple_l, nl);
    nr = biquad_process(&state->dac_ripple_r, nr);

    /* Layer 3: reconstruction rolloff */
    nl = biquad_process(&state->dac_recon_l, nl);
    nr = biquad_process(&state->dac_recon_r, nr);

    *l = nl;
    *r = nr;
}
```

This is the entire hot-path addition: three operations per sample per channel. No branching beyond the `dac_enabled` gate in `spu94_process.c`.

---

## 9. Build Order

### 9.1 Suggested Incremental Build Sequence

Each step produces a testable, shippable increment:

**Phase 1: Scaffold + Noise Floor**
1. Add DAC state fields to `spu94_state_internal.h`
2. Create `spu94_dac.h` + `spu94_dac.c` with enable/disable API
3. Implement LFSR noise generator with HP shaping
4. Wire into `spu94_process.c` (single insertion point)
5. Test: noise spectral shape, amplitude bounds, determinism, golden files
6. ADR: document noise amplitude and shaping rationale

**Phase 2: Passband Ripple Filter**
1. Design biquad coefficients in Python (scipy.signal)
2. Implement biquad in `spu94_dac.c` with Q15 fixed-point
3. Add passband ripple biquad state to struct
4. Test: frequency response matches design within tolerance, impulse response
5. ADR: document coefficient design and target spec

**Phase 3: Reconstruction Rolloff**
1. Design 2nd-order LPF biquad coefficients in Python
2. Add reconstruction biquad to `spu94_dac.c`
3. Test: frequency response, combined with ripple filter
4. ADR: document reconstruction model rationale

**Phase 4: Verification + Golden Files**
1. Full-chain integration tests (ADPCM + reverb + DAC enabled together)
2. Generate new golden files for DAC-enabled configurations
3. Verify existing goldens are unaffected when DAC is disabled (zero blast radius)
4. CLI integration (`--dac` flag or similar)
5. Python binding for enable/disable
6. Coverage map update

### 9.2 Dependency Graph

```
spu94_state_internal.h (DAC fields)
    |
    v
spu94_dac.h + spu94_dac.c (new)
    |
    v
spu94_process.c (1 insertion point)
spu94_io_chain.c (enable/disable API)
spu94_state.c (init/reset zeroing)
```

No dependencies on reverb core, FIR, buffer, or ADPCM code. The DAC module depends only on the state struct definition and the Q15 math helpers (already available).

---

## 10. Gray Areas and ADR Candidates

### 10.1 Noise Amplitude Calibration

The exact noise floor of the AK4309AVM at 44.1 kHz with SPU-sourced audio is not precisely documented. Stereophile measured the PS1 as a CD player (CD path may differ from SPU path). The noise model's amplitude must be tuned by ear or against hardware captures from Anthony's PS1.

**ADR needed:** Document chosen noise amplitude, spectral shape, and calibration method.

### 10.2 Passband Ripple Shape

The +/-0.5 dB spec is from the AK4309 datasheet summary. The exact ripple shape (equiripple? monotonic? Chebyshev?) is unknown without the full datasheet or measured transfer function.

**ADR needed:** Document whether the ripple model is parametric (designed from spec) or empirical (fitted to measurements).

### 10.3 Reconstruction Filter Order and Type

The datasheet says "2nd order SCF + CTF" but does not specify the exact filter topology (Butterworth, Bessel, Chebyshev, etc.). Butterworth is the default assumption for SCF-based reconstruction in mid-90s consumer DACs.

**ADR needed:** Document assumed filter type and cutoff frequency.

### 10.4 Model Fidelity Validation

Without direct digital capture from the PS1's DAC output, the model cannot be validated against hardware. Anthony's PS1 could provide analog recordings, but analog-domain comparison introduces measurement uncertainty.

**Flagged for M5 (hardware validation):** DAC model validation is a natural M5 activity once the digital-capture path is available.

---

## 11. Patterns Used

### Pattern: Toggleable Post-Processing Stage (ADPCM Precedent)

Same as ADPCM integration:
- Default-off, enable via API flag
- State fields in `spu94_state`, zeroed on init/reset
- Disable resets internal state (no stale audio)
- Single insertion point in `spu94_process.c`
- Zero blast radius on existing code when disabled
- Latency reporting updated (DAC biquads: zero additional)

### Pattern: Fixed-Point Biquad IIR

Standard direct-form-II-transposed with Q15 coefficients and Q30 state accumulators. Well-understood on MCU targets; deterministic, no heap, no branching in the hot path.

### Pattern: LFSR Noise Generation

Maximal-length LFSR for deterministic pseudo-random noise. Seeded at init for reproducibility. Used across embedded DSP for decades; zero-cost, zero-state-beyond-the-LFSR.

---

## 12. Anti-Patterns to Avoid

### Anti-Pattern 1: Simulating the Full Delta-Sigma Modulator

**What:** Implement the AK4309's 8x oversampling + 1-bit modulator at 352.8 kHz internally.
**Why wrong:** 8x CPU cost for artifacts that are entirely out-of-band at 44.1 kHz output. The audible effect (shaped noise floor) is captured far more cheaply by the noise model.
**Instead:** Model the audible artifacts directly at 44.1 kHz.

### Anti-Pattern 2: DNL/INL Lookup Tables

**What:** Create a 65536-entry distortion lookup table for the DAC.
**Why wrong:** Delta-sigma DACs have no resistor ladder. DNL/INL is an R2R concept. The AK4309's nonlinearity manifests as modulator-induced distortion, not static transfer-function errors.
**Instead:** The noise model captures the stochastic nonlinearity; harmonic distortion is too subtle to warrant a separate model.

### Anti-Pattern 3: Coupling DAC into the Reverb Network

**What:** Apply DAC coloration inside the reverb feedback loop (e.g., at the 22.05 kHz stage).
**Why wrong:** Physically incorrect. The DAC is outside the SPU chip. It never feeds back into the reverb. Putting DAC coloration inside the reverb would create accumulating artifacts that do not exist in hardware.
**Instead:** DAC is a terminal post-processor. Output only, no feedback.

### Anti-Pattern 4: Float Conversion in the DAC Model

**What:** Convert to float for filter processing, then back to int16.
**Why wrong:** Breaks the int16-throughout discipline. Introduces float-to-int rounding that differs from the Q15 truncation semantics used everywhere else. Also breaks MCU portability (some targets lack FPU).
**Instead:** Q15 fixed-point throughout, matching the rest of libspu94.

---

## Sources

- [dogbreath.de: Remarks on the DAC used in the Sony Playstation](https://dogbreath.de/PS1/DAC/DAC.html) -- AK4309AVM identification, pin configuration. **MEDIUM confidence** (well-sourced hardware teardown, but no electrical measurements).
- [Archimago's Musings: PS1 SCPH-5501 Measurements](http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html) -- Measured dynamic range ~90 dB, THD, jitter. **MEDIUM confidence** (independent measurement, consistent with Stereophile).
- [Stereophile: Sony PlayStation 1 CD Player Measurements](https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements) -- Frequency response ripple, noise floor, harmonic distortion, alias at 23.9 kHz. **HIGH confidence** (professional measurement lab, John Atkinson).
- [emu-russia psxrev: SPU wiki](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md) -- SPU-to-DAC serial interface (DATO, LRCO, BCKO), 16-bit signed PCM at 44.1 kHz. **HIGH confidence** (hardware reverse-engineering project with signal traces).
- [psx-spx.consoledev.net: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- "Mixer and DAC supports 44.1kHz output rate." **HIGH confidence** (primary spec reference).
- [psx-spx.consoledev.net: Pinouts](https://psx-spx.consoledev.net/pinouts/) -- SPU pin assignments for audio serial output. **HIGH confidence**.
- [AK4309 datasheet summary (via Lisleapex/Kynix)](https://www.lisleapex.com/product-ak4309avm) -- 8x FIR interpolator, 2nd-order SCF+CTF, 90 dB dynamic range, +/-0.5 dB at 20 kHz. **MEDIUM confidence** (distributor summary, not full datasheet; original AK4309AVM datasheet appears to be offline).
- [DSPRelated: DAC Zero-Order Hold Models](https://www.dsprelated.com/showarticle/1627.php) -- ZOH modeling approach (used to confirm ZOH is NOT relevant for 8x oversampling DACs). **HIGH confidence** (peer-reviewed DSP reference).

---

*Architecture research for: SPU-94, v1.2 DAC Modeling milestone*
*Researched: 2026-04-28*
