# Feature Landscape: v1.9 Complete Voice

**Domain:** PS1 SPU voice modulation features -- PMON, NON, Volume Sweep, Signed Volume
**Researched:** 2026-05-21
**Primary source:** nocash psx-spx (problemkaputt.de / psx-spx.consoledev.net)
**Cross-checked:** DuckStation spu.cpp (pitch modulation factor source, noise LFSR, volume sweep)
**Overall confidence:** HIGH -- register layouts and formulas explicit in spec; emulator consensus on gray areas

---

## How These Four Features Fit the Existing Voice Path

The v1.8 voice tick processes each voice in this order:

1. Decode ADPCM block if needed
2. Gaussian interpolation (or zero-order-hold bypass)
3. ADSR envelope multiply (gauss_out * adsr_level)
4. Per-voice L/R volume multiply
5. Advance pitch counter

The four v1.9 features inject into specific points in this chain:

```
                         +---------+
  [ADPCM decode] ------->|  Gauss  |---+
                         +---------+   |
                                       |  <-- NON: noise output REPLACES this tap
                                       v
                                  [gauss_out]
                                       |
                                       v
                                  [ADSR multiply]
                                       |
                                       v
                                 [ADSR-scaled out]  <-- PMON reads THIS value
                                       |                for voice N-1, feeds voice N
                                       v
                              [Volume L/R multiply] <-- Signed Volume: negative values
                                       |                flip waveform polarity here
                                       |            <-- Volume Sweep: auto-ramps
                                       v                the volume value itself
                              [to mixer accumulator]
                                       |
                                       v
                              [pitch counter += step] <-- PMON modulates step HERE
```

---

## Table Stakes

Features the PS1 hardware has that a faithful "complete voice" must include. Missing any means
the voice is not spec-complete.

### 1. PMON -- Pitch Modulation

| Aspect | Detail |
|--------|--------|
| **What it does** | Voice N-1's post-ADSR amplitude modulates voice N's pitch step, creating FM-style synthesis |
| **Register** | `1F801D90h` -- 24-bit bitmask; bits 1..23 enable PMON for voices 1..23; bit 0 is unused (voice 0 cannot be modulated) |
| **Spec formula** | `Factor = VxOUTX(x-1) + 0x8000` (range 0x0000..0xFFFF = 0.00..1.99x); `Step = (Step * Factor) >> 15`; if `Step > 0x3FFF` then `Step = 0x4000`; `Counter += Step` |
| **VxOUTX source** | The Gauss-interpolated sample AFTER ADSR envelope multiply, BEFORE per-voice volume L/R. Verified in DuckStation: `voice.last_volume = ApplyVolume(sample, voice.regs.adsr_volume)` -- this is the PMON factor source. HIGH confidence. |
| **Musical meaning** | When voice N-1 outputs silence (0), Factor = 0x8000, Step = Step * 0x8000 >> 15 = Step/2. When voice N-1 outputs max positive (+0x7FFF), Factor = 0xFFFF, Step is approximately 2x. When voice N-1 outputs max negative (-0x8000), Factor = 0x0000, Step = 0 (voice N stops). This means a modulator voice's ADSR directly controls the depth and character of the FM effect. |
| **Edge case: silent modulator** | When voice N-1 is inactive (output = 0), Factor = 0x8000, which halves the pitch. The modulated voice plays at half its nominal pitch, not at full pitch. This is authentic hardware behavior -- no special-casing. |
| **Edge case: pitch > 0x7FFF** | The formula includes `SignExpand16to32(Step)` before multiplication -- a "hardware glitch" for VxPitch values above 0x7FFF. In practice, pitch values > 0x3FFF are clamped post-modulation anyway. |
| **Processing order** | Voices processed sequentially 0..23. Voice N reads voice N-1's last output. Already correct in current mixer loop order. |
| **Complexity** | Low-Medium |
| **Dependencies** | Needs a `last_volume` field per voice to store the post-ADSR output for the next voice to read. Existing tick loop already processes voices in order 0..23 -- no reordering needed. |

### 2. NON -- Noise Generator

| Aspect | Detail |
|--------|--------|
| **What it does** | Replaces ADPCM/Gauss output with LFSR pseudo-random noise for selected voices |
| **Register** | `1F801D94h` -- 24-bit bitmask; bit N set = voice N outputs noise instead of ADPCM |
| **Noise frequency** | Controlled by SPUCNT (`1F801DAAh`) bits 13..10 (NoiseShift, 0..15) and bits 9..8 (NoiseStep, maps to 4,5,6,7). Per-voice VxPitch is IGNORED for noise voices. |
| **LFSR algorithm** | `ParityBit = NoiseLevel.Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1`; when timer underflows: `NoiseLevel = NoiseLevel * 2 + ParityBit`; timer reloaded with `0x20000 >> NoiseShift`. Timer decrements by NoiseStep each 44.1kHz tick. Double-reload if still negative after first reload. |
| **Output** | Signed 16-bit value (the entire NoiseLevel register). This replaces the Gaussian interpolation output for that voice. ADSR still applies on top. |
| **Critical constraint** | ALL noise-enabled voices share the SAME noise output at the same frequency. There is exactly ONE noise generator in the SPU, not one per voice. Individual noise frequencies per voice are impossible -- the only workaround is to use ADPCM samples of pre-recorded noise. |
| **Musical meaning** | Hi-hats, cymbals, snare noise layer, wind/breath textures, white noise pads. The shared-frequency constraint means layering noise voices gives volume but not timbral variety. |
| **Edge case: ADPCM still fetches?** | Spec is ambiguous. DuckStation skips ADPCM entirely when NON is set (`if noise_enabled: sample = GetVoiceNoiseLevel()`). For SPU-94, skip ADPCM decode when NON is set -- saves cycles and matches emulator consensus. Needs an ADR. |
| **Edge case: PMON + NON** | A noise voice's output goes through ADSR and can then serve as a PMON factor for the next voice. This is spec-orthogonal -- noise modulating pitch creates random pitch jitter, a valid creative effect. |
| **Edge case: pitch = 0 + NON** | A zero-pitch ADPCM voice outputs DC (counter frozen). A zero-pitch noise voice still outputs noise at the global noise frequency -- VxPitch is irrelevant for NON voices. |
| **Complexity** | Medium |
| **Dependencies** | Needs a global noise generator state (NoiseLevel, NoiseTimer) on the mixer struct, NOT per voice. Needs NoiseShift and NoiseStep fields (from SPUCNT). Needs the NON bitmask as a mixer-level field. |

### 3. Signed Volume / Phase Inversion

| Aspect | Detail |
|--------|--------|
| **What it does** | Allows negative per-voice volume values that flip the waveform phase while maintaining amplitude |
| **Register** | `1F801C00h + N*10h` (VxVolumeLeft), `1F801C02h + N*10h` (VxVolumeRight) -- in fixed mode (bit 15 = 0), bits 0..14 represent volume/2 in range -0x4000..+0x3FFF (effective -0x8000..+0x7FFE) |
| **How it works** | `output = (sample * (int32_t)volume) >> 15`. When volume is negative, the product is negative, flipping the waveform polarity. Standard signed multiplication -- no special code path needed. |
| **Musical meaning** | (a) Dolby Pro Logic surround -- flipping phase of one channel creates "rear speaker" placement. (b) Stereo widening -- inverting one channel relative to the other widens the perceived stereo image. (c) Cancellation effects -- two voices playing the same sample with opposite phase cancel. |
| **Already partially built** | `spu94_voice_t` declares `vol_l` and `vol_r` as `int16_t` with a comment noting "S2: negative = polarity flip, which is correct SPU behavior." The `q15_mul_truncate` function already handles signed values correctly. |
| **What needs to change** | The GUI/API currently documents "unsigned semantics (0-32767)" and the SamplerWindow knobs enforce positive-only range. The C core already works -- the change is exposing negative values through the API and GUI. |
| **Complexity** | Low |
| **Dependencies** | None beyond what exists. The Q15 multiply path is already signed-correct. |

### 4. Volume Sweep

| Aspect | Detail |
|--------|--------|
| **What it does** | Hardware-driven automatic per-voice volume ramp, independent of ADSR. When bit 15 of VxVolumeL or VxVolumeR is set, the volume register enters sweep mode and auto-increments/decrements. |
| **Register layout (sweep mode, bit 15 = 1)** | Bit 14: mode (0=linear, 1=exponential). Bit 13: direction (0=increase toward +0x7FFF, 1=decrease toward 0). Bit 12: phase (0=positive, 1=negative/inverted). Bits 6..2: shift (0..31, fast..slow). Bits 1..0: step (0..3). |
| **Sweep formula** | Identical step/shift/counter mechanism to ADSR: `AdsrCycles = 1 << max(0, Shift - 11)`; `AdsrStep = StepValue << max(0, 11 - Shift)`. Same fake-exponential-above-0x6000 for increase; same proportional-to-level for exponential decrease. |
| **Step values** | Increase: +7, +6, +5, +4 (step 0..3, formula `7 - step`). Decrease: -8, -7, -6, -5 (step 0..3, formula `-(8 - step)`). NOTE: the increase/decrease step formulas are asymmetric. |
| **Relationship to ADSR** | Sweep is "another Volume envelope, additionally to the ADSR volume envelope." Both are multiplicative in the signal chain. ADSR shapes the per-voice amplitude envelope (attack/decay/sustain/release). Sweep shapes the per-voice volume itself (fade-in, fade-out, pan automation). The final output = sample * adsr_level * sweep_volume (with Q15 scaling at each stage). |
| **Independent L/R** | Left and right volume sweep are SEPARATE state machines. Left can be sweeping up while right sweeps down. This enables automatic stereo panning and cross-fade effects without CPU intervention. |
| **Phase bit** | When phase = 1, sweep operates on negative volume values. Spec notes: "Phase invert causes the step to be positive in decreasing mode." Clamping range changes to -0x8000..0 instead of 0..+0x7FFF. The nocash spec describes this as "not yet tested." LOW confidence on negative-phase behavior. |
| **Timing caution** | Setting fixed volume (bit 15=0) then immediately setting sweep mode (bit 15=1) requires a 1-tick delay -- the fixed volume write is not applied until the next 44.1kHz cycle. |
| **Musical meaning** | Auto-fade-in/out per voice, stereo pan automation (left sweep up while right sweeps down), volume tremolo (with CPU re-triggering), stereo-field movement. |
| **Complexity** | High |
| **Dependencies** | Needs a `volume_sweep_t` state struct per voice per channel (2 per voice = 48 total). Reuses counter-accumulate pattern from ADSR (can share a helper). Needs mode-detect logic: when volume register is written, bit 15 decides fixed vs sweep. |

---

## Differentiators

Features that go beyond basic spec compliance to create unique creative value.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **PMON as FM synth engine** | Allocating voice pairs (modulator+carrier) enables classic 2-op FM synthesis. The PS1 SPU is literally an FM synth when PMON is used -- the modulator voice's sample and ADSR shape the FM timbre. This is a headline creative feature for sound design. | N/A (comes free with PMON implementation) | Consider dedicated FM preset examples showing bell, brass, evolving-pad sounds. |
| **PMON chain stacking** | Voices 0-1-2 can chain: voice 0 modulates 1, voice 1 modulates 2. This creates 3-operator FM. Up to 12 modulator+carrier pairs, or a 24-voice FM chain for maximum chaos. | N/A (comes free with sequential PMON) | Document this capability prominently -- it is unique to the PS1 architecture. |
| **Noise + Reverb** | Noise voices sent through the existing reverb engine create atmospheric textures (wind through a PS1 cathedral). The reverb's characteristic PS1 coloration on noise is a distinctive sound no other plugin produces. | N/A (EON gating already built) | Marketing-worthy combination. |
| **Noise + PMON (random pitch jitter)** | A noise voice feeding PMON into the next voice creates random pitch modulation -- a lo-fi vibrato/detuning effect that sounds unlike any traditional LFO. | N/A (spec-orthogonal) | Document as a creative recipe. |
| **Signed Volume + Reverb cancellation** | Phase-inverted voices through reverb create unusual spatial artifacts. The reverb sums L+R at input -- phase-inverted voices partially cancel in the reverb while remaining present in the dry bus. | N/A (comes free with signed volume) | Document as an exploitable quirk. |
| **Volume Sweep as auto-tremolo** | Fast sweep cycling between increase and decrease creates tremolo. Combined with PMON, this automates vibrato depth without CPU intervention. | Medium (requires CPU re-trigger via register write) | The PS1 sweep is one-shot -- oscillation requires re-triggering the sweep when it hits its limit. |

---

## Anti-Features

Features to explicitly NOT build.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Per-voice noise frequency** | The PS1 has exactly ONE noise generator shared by all 24 voices. Building per-voice noise frequency is unfaithful and musically misleading -- it implies the PS1 could do something it could not. | Single shared LFSR. For per-voice noise variety, load different ADPCM noise samples. |
| **Smooth PMON interpolation** | PMON uses the raw per-sample output of voice N-1 as a pitch factor. Adding smoothing hides the characteristic FM aliasing that gives PS1 FM its distinctive character. | Use the raw value. The aliasing IS the sound. |
| **Volume sweep auto-oscillation** | The PS1 sweep runs in one direction until it hits the limit and stops. It does NOT auto-reverse or oscillate like a DAW LFO. Building auto-oscillation is a non-PS1 feature. | One-directional sweep per spec. If LFO is desired later, build as a separate creative-mode feature clearly labeled as non-PS1. |
| **ADSR bypass when sweep is active** | Sweep and ADSR are independent. Both run simultaneously and multiply together. Disabling ADSR when sweep controls volume would be wrong. | Apply both: sample * adsr_level * sweep_volume with Q15 scaling at each stage. |
| **Configurable LFSR taps** | The PS1 LFSR polynomial (bits 15, 12, 11, 10 XOR 1) is fixed hardware. Changing taps changes the noise character away from authentic PS1. | Use the exact tap positions from spec. |
| **PMON depth/mix parameter** | PS1 PMON is binary: on or off. No "modulation depth" knob exists in hardware. | Toggle only. Depth is controlled by the modulator voice's volume/ADSR, which naturally scales the modulation factor. |
| **VxOUTX readable register API** | The real PS1 exposes per-voice output at `1F801E00h`. Building a register-mapped read API adds complexity for zero musical benefit in an instrument context. | Store `last_volume` internally for PMON. No need for external API. |
| **Master volume sweep** | PS1 master volume registers are NOT sweep-capable. Only per-voice volumes have hardware sweep. | Master volume stays as a direct-set control. |

---

## Feature Dependencies

```
Signed Volume (independent -- existing Q15 path already handles it)
  --> only needs API/GUI exposure of negative vol_l/vol_r values

PMON:
  --> needs: last_volume stored per voice after ADSR multiply
  --> needs: sequential voice processing 0..23 (already the case)
  --> needs: PMON bitmask on mixer struct
  --> interacts with: pitch counter step calculation in voice tick

NON:
  --> needs: global noise generator (NoiseLevel, NoiseTimer) on mixer
  --> needs: NoiseShift/NoiseStep from SPUCNT on mixer struct
  --> needs: NON bitmask on mixer struct
  --> replaces: ADPCM decode + Gaussian interpolation output for flagged voices
  --> interacts with: ADSR (still applies to noise output)
  --> interacts with: PMON (noise voice output can feed PMON factor)

Volume Sweep:
  --> needs: volume_sweep_t state per voice per channel (L and R separate)
  --> needs: sweep tick function (reuses ADSR counter-accumulate mechanism)
  --> needs: mode-detect on volume register write (bit 15 = fixed vs sweep)
  --> needs: current_volume tracking per channel (sweep modifies this)
  --> interacts with: ADSR (multiplicative, both apply)
  --> interacts with: Signed Volume (sweep phase bit controls sign)
```

No circular dependencies. Recommended build order driven by complexity and payoff:

```
1. Signed Volume (near-free -- expose what already works)
     |
2. PMON (low-medium complexity, highest musical payoff)
     |
3. NON (medium complexity, self-contained new module)
     |
4. Volume Sweep (high complexity, reuses ADSR mechanism)
```

---

## Interaction Matrix

How the four features interact with each other and existing systems:

| | PMON | NON | Signed Vol | Vol Sweep | ADSR | Gauss | Reverb |
|---|---|---|---|---|---|---|---|
| **PMON** | -- | Noise output can be PMON factor | Signed vol does NOT affect PMON factor (reads pre-volume) | Sweep does NOT affect PMON factor (reads pre-volume) | ADSR output IS the PMON factor | PMON modifies pitch step which drives Gauss index | No direct interaction |
| **NON** | See above | -- | Signed vol applies to noise output | Sweep applies to noise volume | ADSR applies to noise output | Noise REPLACES Gauss output | Noise sent to reverb via EON |
| **Signed Vol** | No interaction | See above | -- | Sweep phase bit = sign of volume | Multiplicative with ADSR | No interaction | Phase-inverted voices partially cancel in reverb L+R sum |
| **Vol Sweep** | No interaction | See above | See above | -- | Both apply multiplicatively | No interaction | Swept volume affects reverb send level |

---

## Gray Areas Needing ADR Documentation

1. **VxOUTX tap point for PMON**: nocash does not explicitly state whether VxOUTX is post-ADSR or post-volume. DuckStation uses post-ADSR, pre-volume. Document as ADR with DuckStation as behavioral witness. HIGH confidence in the DuckStation approach.

2. **ADPCM fetch during NON**: Spec is ambiguous on whether ADPCM blocks are still read from RAM when NON is enabled. DuckStation skips ADPCM entirely. Document decision to skip. MEDIUM confidence.

3. **Noise initial state**: NoiseLevel initial value at power-on is undocumented. DuckStation initializes to 0. Document the choice. LOW confidence.

4. **Volume sweep phase bit in exponential decrease**: nocash notes "no effect in Exponential Decrease mode." Document this edge case in the implementation. MEDIUM confidence.

5. **Volume sweep negative-phase clamping**: nocash says negative-phase sweep clamps to -0x8000..0 but notes "not yet tested." Document with LOW confidence; implement positive-phase path first.

6. **Step value asymmetry (increase vs decrease)**: Increase uses `7 - step` (+7,+6,+5,+4). Decrease uses `-(8 - step)` (-8,-7,-6,-5). The existing ADSR sustain-decrease code uses `-(7 - step)` which produces -7,-6,-5,-4 -- off by 1 from the spec's stated values. Volume Sweep must use the correct `-(8 - step)` formula. The ADSR discrepancy should be audited separately as a pre-existing concern. HIGH confidence in the spec's stated values.

---

## Complexity Summary

| Feature | Implementation Complexity | Test Complexity | Musical Impact |
|---------|--------------------------|-----------------|----------------|
| Signed Volume | Low (already works in C core) | Low (sign-flip golden test) | Medium (stereo/spatial tricks) |
| PMON | Low-Medium (formula + last_vol storage) | Medium (FM accuracy verification) | High (FM synthesis, vibrato, frequency sweep) |
| NON | Medium (LFSR + timer + global state) | Medium (noise spectral verification) | Medium (percussion, texture, atmospherics) |
| Volume Sweep | High (second envelope per voice per channel) | High (sweep curve accuracy, mode interactions) | Low-Medium (auto-fade, pan automation) |

---

## MVP Recommendation

All four features are table stakes for "complete PS1 voice." Prioritize by dependency and payoff:

1. **Signed Volume** -- near-zero cost; expose negative volume through API and GUI. Unlocks sweep's negative-phase capability and phase-inversion creative effects.

2. **PMON** -- highest musical payoff; enables FM synthesis, vibrato, frequency sweeps. Add `last_volume` per voice, PMON bitmask, and pitch-step modulation formula in the counter-advance section.

3. **NON** -- enables percussion and texture voices. Add one global LFSR generator on the mixer, NON bitmask, and noise output substitution before the ADSR multiply in voice tick.

4. **Volume Sweep** -- most complex; reuses the proven ADSR counter-accumulate mechanism. Independent L/R sweep state per voice. Build last because it has the lowest musical impact relative to its complexity.

Defer:
- Volume sweep negative-phase mode: implement positive-phase first; add negative-phase as follow-up. The spec itself says negative phase is "not yet tested."
- GUI multi-voice sweep editing (only matters when editing multiple voices simultaneously).
- Volume sweep LFO re-trigger automation (the PS1 sweep is one-shot; oscillation requires CPU-driven re-triggering which is a host-level concern).

---

## Sources

- [nocash psx-spx: SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) -- PMON formula, pitch counter modulation, VxOUTX factor, 0x3FFF clamping, voice 0 exclusion. HIGH confidence.
- [nocash psx-spx: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- volume sweep register layout, step/shift formula, exponential fake, phase bit, signed volume, step value tables (+7..+4 / -8..-5). HIGH confidence.
- [psx-spx.consoledev.net: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- NON register, noise LFSR algorithm, SPUCNT noise frequency bits, voice register map, VxOUTX address, signal flow description. HIGH confidence.
- [DuckStation spu.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp) -- `voice.last_volume = ApplyVolume(sample, voice.regs.adsr_volume)` confirms PMON reads post-ADSR pre-volume; `GetVoiceNoiseLevel()` confirms noise substitution; `VolumeSweep::Tick()` confirms independent L/R sweep; `ApplyVolume()` confirms signed multiply. HIGH confidence (cross-verification).
- [hitmen SPU docs](https://hitmen.c02.at/files/docs/psx/spu.txt) -- secondary reference; confirms sweep mode, noise mode, volume register layout. MEDIUM confidence.
- Existing SPU-94 code: `spu94_voice.c` (voice tick processing order, signed vol_l/vol_r declaration), `spu94_adsr.c` (counter-accumulate mechanism to reuse for sweep), `spu94_voice.h` (data structures, mixer struct). HIGH confidence (internal).
