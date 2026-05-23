# Domain Pitfalls: v1.9 Complete Voice

**Domain:** PS1 SPU per-voice modulation features
**Researched:** 2026-05-21

## Critical Pitfalls

Mistakes that cause incorrect audio output or architectural dead ends.

### Pitfall 1: PMON VxOUTX Capture Point Wrong

**What goes wrong:** Capturing VxOUTX at the wrong stage (pre-ADSR, or post-volume) produces incorrect pitch modulation depth. Post-volume capture would make L/R panning affect FM modulation depth, which is not how the hardware works. Pre-ADSR capture would ignore envelope shaping, making a dying note still modulate at full depth.

**Why it happens:** The nocash spec says "amplitude from channel (x-1)" without specifying where in the pipeline.

**Consequences:** FM synthesis sounds wrong. Vibrato depth depends on panning. Or dying notes create pitch artifacts instead of gracefully fading.

**Prevention:** Capture VxOUTX after ADSR multiply, before volume multiply. This is Step 2.75 in the modified pipeline. Verify by: set voice 0 as sine wave with ADSR decay, voice 1 PMON-enabled -- modulation depth should decrease as voice 0 decays.

**Detection:** Compare SPU-94S PMON output against DuckStation on a known test ROM. Frequency sweep depth should match.

### Pitfall 2: Noise LFSR Different From DAC LFSR

**What goes wrong:** Reusing or confusing the DAC noise LFSR (32-bit Galois, `spu94_dac_noise.c`) with the SPU voice noise LFSR (16-bit Fibonacci-style with taps at 15/12/11/10).

**Why it happens:** Both are called "noise" and both use LFSRs, but they serve completely different purposes. The DAC noise models analog delta-sigma quantization artifacts. The SPU voice noise is a digital percussion source. They have different polynomials, different bit widths, different stepping mechanisms.

**Consequences:** Wrong noise character. The DAC LFSR has highpass shaping built into its output path; the SPU noise LFSR does not. Using the wrong one produces either too-bright or too-flat noise.

**Prevention:** Implement `spu94_noise_gen_t` as a completely separate type from `spu94_dac_noise_state`. Different files, different namespaces, different polynomials. Comment both with their purposes. Name the SPU noise generator `spu94_noise_gen_t` (not `spu94_noise_state` which could be confused with DAC noise).

**Detection:** Spectral analysis of noise output. SPU noise should be flat (no HP shaping); DAC noise has +12dB/octave slope.

### Pitfall 3: Volume Sweep Counter Not Identical to ADSR Counter

**What goes wrong:** Implementing the sweep counter-accumulate mechanism slightly differently from ADSR (different bit widths, different clamping, different shift formulas).

**Why it happens:** Copy-paste divergence. Or implementing sweep from scratch without referencing the working ADSR code.

**Consequences:** Sweep timing doesn't match real hardware. Exponential curves are wrong. Step sizes at boundary shift values are wrong.

**Prevention:** Extract the counter-accumulate core from `spu94_adsr_tick()` into a shared `spu94_envelope_step()` helper. Both ADSR and Sweep call the same function. This makes divergence impossible by construction.

**Detection:** Golden-file regression on ADSR (refactor must be bit-identical). Sweep-specific test: known shift/step values should produce known ramp times (verify against nocash timing table).

### Pitfall 4: PMON Hard-Clip Value is 0x4000, Not 0x3FFF

**What goes wrong:** Clipping the PMON-modified step to 0x3FFF (the normal pitch clamp) instead of 0x4000.

**Why it happens:** The normal pitch clamp (without PMON) caps at 0x3FFF. But the nocash spec says: "IF Step > 3FFFh THEN Step = 4000h" for the PMON path. This is a DIFFERENT clamp value.

**Consequences:** Maximum PMON-modulated pitch is wrong by one LSB, potentially producing different aliasing behavior at extreme modulation depths.

**Prevention:** The PMON path uses `if (step > 0x3FFF) step = 0x4000` (note: 4000, not 3FFF). Document this discrepancy as ADR. The unmodulated path continues to clamp at 0x3FFF.

**Detection:** Test with VxPitch = 0x3FFF and Factor = 0xFFFF (maximum modulation). Output step should be 0x4000.

---

## Moderate Pitfalls

### Pitfall 5: Noise Timer Sign Extension

**What goes wrong:** Treating the noise timer as unsigned, so the `IF Timer < 0` check never fires.

**Prevention:** Declare timer as `int32_t`, not `uint32_t`. The timer starts positive, decrements by NoiseStep, and goes negative when it underflows.

### Pitfall 6: Noise Double-Reload Missed

**What goes wrong:** Only reloading the timer once when it goes negative, missing the "IF Timer < 0 THEN reload again" second check.

**Why it happens:** The nocash pseudocode shows TWO sequential "IF Timer<0" checks after the NoiseLevel update. At high NoiseStep / low NoiseShift combinations, a single reload may not make the timer positive.

**Prevention:** Implement exactly: `if (timer < 0) { level = shift+parity; timer += reload; } if (timer < 0) { timer += reload; }` -- note the second reload does NOT update NoiseLevel, only the timer.

**Detection:** Test with maximum step (7) and maximum shift (15): timer reload value is `0x20000 >> 15 = 1`. Step of 7 means timer decrements fast; without double-reload, timer stays negative permanently and noise updates every tick instead of at the configured rate.

### Pitfall 7: PMON Sign Extension for Pitch > 0x7FFF

**What goes wrong:** Not sign-extending VxPitch from 16-bit to 32-bit before the multiply. Pitches above 0x7FFF are rare but valid in the PMON context.

**Why it happens:** The nocash spec explicitly says "Step = SignExpand16to32(Step)" before the multiply. Values 0x8000-0xFFFF are treated as negative when sign-extended, producing a negative step which gets masked back to 16-bit positive by "Step = Step AND 0x0000FFFF".

**Prevention:** Cast Step to `int16_t` then to `int32_t` for sign extension, multiply, then mask with `& 0xFFFF`.

### Pitfall 8: Volume Sweep Phase Bit Interaction with Boundaries

**What goes wrong:** Incorrect clamping when the phase bit is set. With phase=1 and direction=increase, the volume increases toward -0x7FFF (not +0x7FFF). With phase=1 and direction=decrease, it decreases toward 0x0000.

**Why it happens:** The phase bit inverts the meaning of "maximum" but the clamping logic is implemented assuming positive volumes only.

**Prevention:** Implement clamping per the spec: positive-increase clamps at +0x7FFF, positive-decrease clamps at 0x0000, negative-increase clamps at -0x7FFF, negative-decrease clamps at 0x0000.

### Pitfall 9: Sweep Starting Volume Race Condition

**What goes wrong:** Setting sweep parameters (bit15=1) before the fixed volume (bit15=0) has been latched.

**Why it happens:** The nocash spec warns: "the Bit15=0 setting isn't applied until the next 44.1kHz cycle." Writing fixed volume then immediately writing sweep parameters in the same tick means the sweep starts from the OLD volume, not the intended initial volume.

**Prevention:** In the mixer API, provide a `set_volume_and_sweep()` function that sets the initial volume AND configures sweep atomically. Or document that callers must ensure at least one tick between volume set and sweep activation.

---

## Minor Pitfalls

### Pitfall 10: Forgetting to Advance Pitch Counter for NON Voices

**What goes wrong:** Skipping the pitch counter advancement when NON is enabled because "the counter isn't used for sample generation."

**Why it happens:** Optimization instinct -- if noise ignores the counter, why advance it?

**Prevention:** Hardware always advances the counter. This matters if: (1) the voice switches from NON back to ADPCM mid-playback, (2) the voice's VxOUTX is used for PMON by the next voice.

### Pitfall 11: VxOUTX Array Not Cleared on Mixer Init

**What goes wrong:** Stale VxOUTX values from a previous session affect PMON modulation on the first tick.

**Prevention:** Zero the `outx[24]` array in `spu94_voice_mixer_init()`. Also clear a voice's outx entry on KON.

### Pitfall 12: Sweep Persists Across KON

**What goes wrong:** A voice key-on does not reset the sweep state, so the new note inherits the previous note's sweep position.

**Prevention:** On KON, initialize sweep_l and sweep_r to non-active state with the KON volume as the initial level.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Signed Volume | Existing code assumes unsigned | Grep for 0..32767 comments, vol_l/vol_r range checks |
| NON (Noise) | Confuse with DAC noise LFSR | Separate type, separate file, different polynomial |
| NON (Noise) | Timer sign and double-reload | int32_t timer, two sequential IF checks |
| Volume Sweep | Counter diverges from ADSR | Shared envelope_step() helper |
| Volume Sweep | Phase bit clamping wrong | Explicit per-quadrant clamp table |
| PMON | VxOUTX capture point | post-ADSR, pre-volume; verify with decay test |
| PMON | Hard-clip 0x4000 vs 0x3FFF | Different clamp for PMON path vs normal path |
| PMON | Sign extension for pitch > 0x7FFF | Cast to int16_t then int32_t before multiply |

## Sources

- [nocash psx-spx: SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) -- PMON formula, clamp values
- [nocash psx-spx: SPU Noise Generator](https://problemkaputt.de/psxspx-spu-noise-generator.htm) -- timer mechanism, double-reload
- [nocash psx-spx: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- sweep mechanism, phase bit behavior
- Existing `spu94_dac_noise.c` -- DAC LFSR (must not confuse with SPU noise)
- Existing `spu94_adsr.c` -- counter-accumulate reference implementation
