# Domain Pitfalls: v1.9 Complete Voice

**Domain:** Adding pitch modulation (PMON), noise generation (NON), volume sweep, and signed volume (phase inversion) to an existing 24-voice ADPCM sampler engine that already has Gaussian interpolation, counter-accumulate ADSR, loop mechanics, and a polyphonic mixer with reverb send.
**Researched:** 2026-05-21
**Confidence:** HIGH on PMON mechanics (nocash formula verified against DuckStation source; voice processing order confirmed as 0-23 sequential). HIGH on NON LFSR algorithm (polynomial taps verified across nocash and DuckStation; initial seed = 1 confirmed in DuckStation reset). MEDIUM on Volume Sweep edge cases (nocash spec has known "not yet tested" annotations on negative-phase behavior). MEDIUM on Signed Volume (multiplication semantics clear; interaction with existing unsigned-assumption volume path needs careful audit).

---

## Orientation

The existing v1.8 codebase has:
- 24 per-voice structs (`spu94_voice_t`) with pitch counter, ADPCM decode, Gaussian interpolation ring
- Counter-accumulate ADSR envelope (`spu94_adsr_state_t`) using the same counter/bit-15 mechanism that Volume Sweep will reuse
- Voice mixer (`spu94_voice_mixer_t`) processing voices in a `for (v = 0; v < 24; v++)` loop, accumulating int32 sums, saturating to int16
- `vol_l`/`vol_r` declared as `int16_t` but documented as "unsigned semantics (0..32767)" in VOICE-04 -- the signed type was a deliberate forward-looking choice for phase inversion (S2 note in the header)
- EON-gated reverb send per voice
- Pending KON/KOFF bitmask semantics (C8)

Phase labels for the v1.9 work:
- **P-PMON** -- pitch modulation (voice chain dependency)
- **P-NON** -- noise generator (LFSR + per-voice noise-enable)
- **P-SWEEP** -- volume sweep envelope (per-voice, separate from ADSR)
- **P-SIGNED** -- signed volume / phase inversion
- **P-INTEGRATE** -- wiring new features into existing voice tick, mixer, and reverb-send pipeline
- **P-VERIFY** -- test infrastructure, golden files, edge-case coverage

---

## Critical Pitfalls

Mistakes that produce fundamentally wrong audio or require a rewrite.

---

### C1: Processing voices in wrong order breaks PMON

**What goes wrong:**
PMON uses voice N-1's output amplitude to modulate voice N's pitch. The spec formula reads: `Factor = VxOUTX(x-1) + 8000h`. This requires that voice N-1's output has been computed and stored before voice N reads it. If voices are processed in parallel, out of order, or if VxOUTX is latched at the wrong moment, the modulation factor is stale or zero.

**Why it happens:**
The existing `spu94_voice_mixer_tick` processes voices in a `for (int v = 0; v < 24; v++)` loop, which is correct -- but only if VxOUTX is written immediately after each voice completes, not after the entire loop finishes. A common mistake: accumulate all voice outputs into a batch, then store VxOUTX at the end. This means voice 1 would read voice 0's VxOUTX from the *previous tick*, introducing a one-tick latency that changes the modulation frequency and character.

**Consequences:**
- FM synthesis sounds are pitched wrong
- Vibrato effects are phase-shifted by one sample
- At high modulation depths, the one-tick delay produces audible beating/phasing not present on real hardware

**Prevention:**
1. Store VxOUTX for each voice immediately after computing its ADSR-scaled output, before processing the next voice
2. VxOUTX must be stored *after* Gaussian interpolation and *after* ADSR envelope, but *before* per-voice volume left/right multiply. This is confirmed by DuckStation: `volume = ApplyVolume(sample, voice.regs.adsr_volume); voice.last_volume = volume;` -- then left/right volumes are applied afterward
3. Add a `int16_t outx` field to `spu94_voice_t` for VxOUTX storage
4. Voice 0's PMON bit must be ignored (spec: PMON applies to voices 1-23 only; voice 0 has no predecessor)

**Detection:**
- Test: Enable PMON on voice 1, play a slow sine on voice 0 and a tone on voice 1. Verify the pitch sweep is sample-accurate against a reference. Any one-tick phase shift indicates wrong VxOUTX timing.

---

### C2: VxOUTX captured at wrong point in the voice pipeline

**What goes wrong:**
VxOUTX must represent the sample after Gaussian interpolation and after ADSR envelope, but before per-voice left/right volume multiplication. If captured too early (after interpolation but before ADSR), the modulator amplitude ignores the envelope -- an attack phase that should produce increasing modulation depth would produce full-depth modulation immediately. If captured too late (after volume left/right), the modulator would include stereo panning, which the hardware does not do.

**Why it happens:**
The existing voice pipeline in `spu94_voice_tick` applies operations in this order:
1. Decode ADPCM block
2. Gaussian interpolation -> `gauss_out`
3. ADSR envelope -> `gauss_out = q15_mul_truncate(gauss_out, adsr_level)`
4. Per-voice volume -> `*out_l = q15_mul_truncate(gauss_out, v->vol_l)`

VxOUTX must be captured between steps 3 and 4. The `gauss_out` value after step 3 is mono and ADSR-shaped. The temptation is to capture `out_l` or `out_r` (after step 4), which would inject stereo panning into the modulation chain.

**Consequences:**
- Incorrect modulation depth (too much or too little)
- Stereo-dependent pitch modulation that doesn't exist on real hardware
- Subtle frequency differences that compound through chains of modulated voices

**Prevention:**
1. Add `v->outx = gauss_out;` after the ADSR multiply (step 3) and before the volume multiply (step 4)
2. The PMON formula reads `voices[x-1].outx`, not `voices[x-1].out_l` or `voices[x-1].out_r`
3. Document this capture point in an ADR -- it's a gray area where the spec says "prev voice amplitude" without specifying which stage

**Detection:**
- Test: Set voice 0 with ADSR attack = slow ramp. Set voice 1 with PMON enabled. The modulation depth should increase as voice 0's ADSR ramps up. If modulation is full-depth from the start, VxOUTX is captured before ADSR.

---

### C3: LFSR polynomial or initial seed wrong for noise generator

**What goes wrong:**
The PS1 SPU noise generator uses a specific 16-bit LFSR with feedback polynomial taps at bits 15, 12, 11, and 10, plus a constant 1 XOR (making this technically an XNOR-based LFSR). The shift-and-feedback formula is:

```
ParityBit = NoiseLevel.Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
NoiseLevel = NoiseLevel * 2 + ParityBit
```

Getting any tap position wrong, or omitting the constant `XOR 1`, produces a completely different noise sequence. The `XOR 1` is especially easy to miss -- it inverts the standard LFSR parity, and most LFSR implementations in textbooks don't include it.

**Why it happens:**
- Textbook LFSRs use standard polynomials. The PS1's polynomial (taps at 15,12,11,10 + constant 1) is non-standard.
- The constant `XOR 1` at the end is an XNOR gate, not an XOR gate. Implementations that copy a generic LFSR template and just change tap positions will miss this.
- The initial seed must be 1 (confirmed by DuckStation's `Reset()`: `s_state.noise_level = 1`). Starting at 0 produces all-zeros forever (LFSR halting state). Starting at any other non-zero value produces a correct-length sequence but phase-shifted, meaning noise tails won't match hardware.
- This is a left-shift (`*2`) with parity injection at bit 0 (`+ParityBit`), NOT a right-shift with feedback at the MSB. Implementing the shift direction wrong produces a mirrored sequence.

**Consequences:**
- Completely wrong noise spectrum (wrong taps change the sequence period and spectral content)
- All-zero output (seed = 0)
- Noise that sounds approximately right but fails bit-accurate golden-file comparison

**Prevention:**
1. Implement feedback as `parity = ((level >> 15) ^ (level >> 12) ^ (level >> 11) ^ (level >> 10) ^ 1) & 1`
2. Shift as `level = (int16_t)((level << 1) | parity)` -- the level is a signed 16-bit value used directly as sample output
3. Initialize `noise_level = 1` on reset
4. Write a golden-file test comparing the first 65536 noise values against a known-good reference

**Detection:**
- Generate 65536 noise samples and verify the LFSR sequence matches DuckStation output for a known SPUCNT Shift/Step setting

---

### C4: Noise timer mechanism wrong -- step/shift confusion

**What goes wrong:**
The noise generator doesn't shift the LFSR every tick. It uses a timer-subtraction mechanism:

```
Timer = Timer - NoiseStep           (step = 4,5,6,7 from SPUCNT bits 9-8)
IF Timer < 0 THEN:
    NoiseLevel = NoiseLevel * 2 + ParityBit
    Timer = Timer + (0x20000 >> NoiseShift)
IF Timer < 0 THEN:
    Timer = Timer + (0x20000 >> NoiseShift)
```

The double-reload (two consecutive `if Timer < 0` checks with reload) handles the case where the reload value is smaller than the step, allowing the timer to be properly reloaded at high frequencies. Getting the reload formula wrong, or missing the double-reload, produces wrong noise frequency.

**Why it happens:**
- Confusing NoiseStep (the decrement, 4-7) with NoiseShift (the reload divisor, 0-15). The names are similar.
- Implementing a single reload instead of the double-reload. The second `if Timer < 0` check is not a no-op -- at high shift values (small reload), the timer can still be negative after the first reload.
- Forgetting that the timer persists across ticks -- it is not reset each tick.
- Treating the timer as unsigned (it must go negative to trigger the LFSR shift)

**Consequences:**
- Noise frequency is wrong -- hi-hats and cymbals play at incorrect pitch
- At high noise frequencies, missing the double-reload cuts the maximum frequency in half
- At low noise frequencies, the noise sounds correct but golden-file comparison fails

**Prevention:**
1. Use `int32_t` for the noise timer (must go negative)
2. Implement the exact pseudocode from nocash, including both reload checks
3. Verify that NoiseStep is `4 + ((SPUCNT >> 8) & 3)` and NoiseShift is `(SPUCNT >> 10) & 0xF`
4. Note: the parity bit is computed BEFORE the timer check but the LFSR only shifts when the timer fires. This means the parity is always computed from the current level, not from a pre-shift level.

**Detection:**
- Test all 64 Shift/Step combinations (16 shifts x 4 steps) and verify the noise frequency matches expected values

---

### C5: Sweep and ADSR are SEPARATE concurrent envelopes

**What goes wrong:**
Volume Sweep uses the same counter-accumulate/bit-15-fires-step mechanism as ADSR (same `CounterIncrement`, step formulas, exponential scaling). This tempts developers to reuse the existing `spu94_adsr_state_t` struct or share counter state. But sweep and ADSR are separate, concurrent envelopes:

- **ADSR** controls the amplitude envelope (attack/decay/sustain/release), applied as a mono multiplier
- **Sweep** controls the per-voice left/right volume registers independently, applied as a stereo volume multiplier

They run simultaneously. A voice can have ADSR ramping up while sweep ramps left volume down and right volume up (stereo panning). Merging them into one state machine produces fundamentally wrong behavior.

**Why it happens:**
- The formulas are identical except for the register fields they read
- The `spu94_adsr_state_t` struct already has counter, level, phase, shift/step/exp/dir fields
- The ADSR is well-tested and working; reusing it seems safe

**Consequences:**
- Sweep cannot run independently from ADSR
- Stereo panning effects are impossible
- Volume sweep direction changes fight with ADSR phase transitions

**Prevention:**
1. Create a new `spu94_sweep_state_t` struct with its own counter, level, shift, step, exp, dir, and phase fields
2. Each voice gets TWO sweep states -- one for left volume, one for right volume -- separate from the single ADSR state
3. Both sweep states tick independently every 44.1 kHz tick
4. The sweep step function can share the same *math helper* as ADSR (the counter-accumulate formula is identical), but must use its own *state storage*
5. Final voice output: `mono_out = q15_mul(gauss_out, adsr_level)`, then `out_l = q15_mul(mono_out, sweep_vol_l)`, `out_r = q15_mul(mono_out, sweep_vol_r)`

**Detection:**
- Test: Configure a voice with ADSR attack = instant, sustain = full, and sweep_left = decrease, sweep_right = increase. The voice should pan from left to right while maintaining constant overall volume.

---

### C6: Signed volume breaks the existing unsigned volume path

**What goes wrong:**
The existing `spu94_voice_tick` applies per-voice volume as:
```c
*out_l = q15_mul_truncate(gauss_out, v->vol_l);
*out_r = q15_mul_truncate(gauss_out, v->vol_r);
```

The Q15 multiply `q15_mul_truncate(a, b) = (int32_t)a * (int32_t)b >> 15` works correctly for signed values -- a negative `vol_l` will invert the phase. The core math is fine. The danger is in the **surrounding code** that touches volume:

1. **`spu94_voice_mixer_key_on`** -- if the GUI or MIDI layer clamps `vol_l`/`vol_r` to `0..0x7FFF` before passing them, negative values never arrive
2. **Volume display** -- if the GUI shows volume as `0..100%`, it can't represent negative values
3. **Sweep integration** -- when sweep modifies volume, the level value needs to be signed-aware. Sweep decrease toward zero is different from sweep increase toward `-7FFFh`
4. **PS1 volume register format** -- fixed mode uses 15-bit magnitude + 1 sign bit: value range `-4000h..+3FFFh` which maps to `-8000h..+7FFEh` when doubled. This scaling must be documented.

**Why it happens:**
- The v1.8 code was written with unsigned volume in mind. Tests assume `vol_l >= 0`.
- `q15_mul_truncate` already handles signs correctly, so the core DSP works -- but call sites, validation, GUI, and preset serialization all assume unsigned.

**Consequences:**
- Phase inversion is silently impossible because upstream code clamps to positive
- Volume sweep into negative territory is silently clamped to zero
- Test infrastructure doesn't cover negative volume paths

**Prevention:**
1. Audit every call site that sets `vol_l`/`vol_r` -- remove positive-only clamping
2. Document the PS1 fixed-mode volume register scaling: `-4000h..+3FFFh` maps to `-8000h..+7FFEh`
3. Update `spu94_voice_mixer_key_on` to accept the full signed range
4. Add negative-volume golden-file tests: a sine wave processed with `vol_l = -0x4000` should be bit-identical to the positive version but phase-inverted
5. VxOUTX (for PMON) is captured post-ADSR, pre-volume -- it is already signed and unaffected by volume sign. No PMON changes needed for signed volume.

**Detection:**
- Golden test: process identical audio with `vol_l = +0x4000` and `vol_l = -0x4000`. Outputs should be exact negatives of each other, sample by sample.

---

## Moderate Pitfalls

Mistakes that produce wrong behavior but are fixable without a rewrite.

---

### M1: Noise replaces ADPCM output but ADPCM blocks must still be decoded

**What goes wrong:**
When NON is enabled for a voice, the noise LFSR output replaces the Gaussian interpolation output. But the ADPCM block decode must still happen -- the voice still advances through SPU RAM, processes flag bytes (loop start/end), and updates filter state. If the decode is skipped when NON is active, loop flags are never processed, ENDX is never set, and the voice's address counter stalls.

DuckStation confirms this: "ADPCM data is still decoded" even for noise voices.

**Why it happens:**
The obvious optimization: "if this voice outputs noise, skip the expensive ADPCM decode." But the decode has side effects beyond audio output -- it processes the flag byte which controls loop mechanics.

**Consequences:**
- Loop-end flags never fire -> voice never loops or terminates
- ENDX never gets set -> software polling for voice completion hangs
- Address counter never advances -> voice consumes no SPU RAM addresses

**Prevention:**
1. When NON is active, still decode ADPCM blocks and process flag bytes
2. Replace only the interpolation output: `sample = noise_level` instead of `sample = gauss_interpolate(...)`
3. Pitch counter still advances, consuming decoded samples and triggering block boundaries
4. Comment this explicitly -- it's counterintuitive

**Detection:**
- Test: Enable NON on a voice playing a looped sample. Verify ENDX is set when the loop-end block is reached.

---

### M2: Noise frequency is global, not per-voice

**What goes wrong:**
The noise LFSR is a single global generator. All NON-enabled voices read the same `NoiseLevel` value on every tick. The noise frequency is controlled by SPUCNT bits 8-13, not by per-voice pitch registers.

**Why it happens:**
- Every other voice feature (pitch, volume, ADSR) is per-voice -- the natural assumption is that noise is too
- Having a single global generator feels architecturally wrong in a modern implementation

**Consequences:**
- Per-voice noise produces different sequences on each voice, which doesn't match hardware
- Per-voice noise frequency control allows effects that are impossible on real hardware

**Prevention:**
1. Store `noise_level` and `noise_timer` in the mixer struct (`spu94_voice_mixer_t`), not in individual voice structs
2. Tick the noise generator ONCE per tick (before the voice loop), not once per voice
3. All NON-enabled voices read the same `noise_level` value for that tick
4. Document that per-voice pitch has NO effect on noise frequency (nocash: "the ADPCM Sample Rate has absolutely no effect on noise")

**Detection:**
- Test: Enable NON on voices 0 and 12. Both voices should output identical noise samples on every tick.

---

### M3: PMON cannot modulate noise frequency

**What goes wrong:**
The nocash spec states that pitch modulation can be applied over ADPCM but NOT over noise. Since PMON modulates the pitch counter step, and noise voices don't use the pitch counter for audio output (they read the global LFSR), PMON on a noise voice modulates the ADPCM decode rate (for flag processing) but not the audible noise.

**Prevention:**
1. Apply PMON to the pitch step before Gaussian interpolation -- this is the natural pipeline position
2. Document that PMON affects the ADPCM decode advancement rate even for NON voices, but the audible output is unaffected
3. This is a documentation/expectation issue, not a code bug

**Detection:**
- Informational test: verify that enabling PMON on a noise voice doesn't change the noise output samples

---

### M4: Sweep Phase bit behavior is under-documented for negative volumes

**What goes wrong:**
The nocash spec includes this note about the sweep Phase bit (bit 12 of the volume register in sweep mode): "Should be equal to the sign of the current volume (not yet tested, in the negative mode it does probably 'increase' to -7FFFh?)." This is explicitly marked as uncertain in the spec. The Phase bit also "seems to have no effect in Exponential Decrease mode."

These are gray areas that need ADR treatment:
1. What does "increase" mean when Phase=Negative? Does it increase toward `-7FFFh`?
2. Does exponential decrease ignore Phase entirely?
3. What happens if Phase disagrees with the sign of the current volume level?

**Why it happens:**
- The spec author (nocash) annotated this as untested
- Emulator consensus may not be reliable here

**Consequences:**
- Sweep direction may be inverted for negative-phase voices
- Exponential decrease may behave differently than expected in negative phase

**Prevention:**
1. Implement the formula exactly as nocash specifies: `IF Decreasing XOR PhaseNegative THEN AdsrStep = NOT AdsrStep`
2. File an ADR for the Phase/negative-volume interaction, documenting what SPU-94 does and why
3. Use DuckStation's behavior as the emulator-consensus baseline
4. Test both sweep directions with both Phase polarities against DuckStation output

**Detection:**
- Compare sweep trajectories against DuckStation for all 4 combinations of Direction x Phase

---

### M5: Volume sweep initial-value timing hazard

**What goes wrong:**
The nocash spec warns: "the Bit15=0 setting isn't applied until the next 44.1kHz cycle; so setting the initial level with Bit15=0, followed by the sweep parameter with Bit15=1 works only if there's a suitable delay between the two operations." Writing a fixed volume immediately followed by a sweep command in the same tick may not apply the fixed volume as the sweep starting point.

**Consequences:**
- Sweep starts from the wrong level -- instead of sweeping from 0 to max, it sweeps from wherever the previous sweep ended

**Prevention:**
1. In SPU-94's API, key_on sets both the initial volume AND the sweep parameters simultaneously -- document that the initial volume is the sweep's starting point
2. On key_on with sweep enabled: set vol_l/vol_r to the requested initial value, initialize sweep counter to 0, sweep begins from the initial value on the next tick
3. File an ADR documenting SPU-94's chosen behavior for this timing edge case

---

### M6: Sweep IS the volume register, not a separate multiplier

**What goes wrong:**
Implementing sweep as a separate envelope that multiplies against a fixed volume setting, keeping the original vol_l/vol_r unchanged.

**Why it happens:**
ADSR works as a separate multiplier. Intuition says sweep should too.

**Consequences:**
- Double-applying volume (the fixed value AND the sweep value)
- Extra attenuation making everything too quiet
- Reading back vol_l/vol_r shows the fixed value instead of the swept value

**Prevention:**
1. Sweep tick directly modifies `voice->vol_l` and `voice->vol_r`. There is no separate "sweep level" -- the volume register IS the sweep's working state.
2. Do NOT add a separate `* sweep_level` multiply after the existing `* vol_l` multiply
3. Each tick, update `v->vol_l` and `v->vol_r` by running the sweep counter mechanism on them
4. The multiply chain stays as two stages: `gauss_out * adsr_level`, then `result * vol_l/r`

**Detection:**
- A/B test: voice with sweep-to-max vs voice with fixed volume at max. Output amplitude should be identical when sweep reaches its target.

---

### M7: Pending KON/KOFF interaction with sweep state

**What goes wrong:**
The existing pending KON/KOFF system (C8 in v1.8) stages key-on/key-off events in bitmasks and applies them at the start of the next tick. When KON fires, it resets ADSR. It also needs to handle sweep state:
- KON must reset sweep state (new note starts fresh)
- KOFF does NOT affect sweep (sweep is independent of ADSR release)
- The pending config mechanism (`pending_config[]`) needs to carry sweep parameters

**Consequences:**
- Sweep from a previous note bleeds into a new note
- Residual sweep direction/rate persists after KON

**Prevention:**
1. Add sweep fields to `spu94_voice_t`
2. `spu94_voice_key_on` resets sweep state (counter=0, level set to initial value)
3. `spu94_voice_mixer_key_on` accepts sweep configuration in the pending config
4. KOFF does NOT reset sweep

**Detection:**
- Test: Trigger voice with sweep-decrease. Before sweep completes, KON the same voice with sweep-increase. Verify the sweep starts from the KON-specified initial value.

---

## Minor Pitfalls

Issues that cause subtle incorrectness or maintenance problems.

---

### m1: PMON on voice 0 -- the ignored bit

**What goes wrong:**
PMON bit 0 has no useful function -- voice 0 has no predecessor. If SPU-94 processes PMON bit 0 and tries to read `voices[-1].outx`, it reads out-of-bounds memory.

**Prevention:**
1. Skip PMON for voice index 0: `if (pmon_enabled && voice_idx > 0)`
2. PMON bit 0 writes should be accepted but ignored

---

### m2: PMON hardware glitch for VxPitch > 0x7FFF

**What goes wrong:**
The nocash spec documents hardware glitches when VxPitch exceeds 0x7FFF during PMON: sign-extension errors and sign-bit killing after multiplication. Since SPU-94 clamps pitch to 0x3FFF (C7 in v1.8), these glitches cannot be triggered through the normal API.

BUT: the PMON formula includes `SignExpand16to32(Step)` before the multiply. For pitch values 0x0001-0x3FFF this is a no-op (positive values sign-extend with zeros). The spec also says PMON can produce a final step of 0x4000 (clamp: `IF Step > 3FFFh THEN Step = 4000h`), which is HIGHER than the normal maximum pitch. This is intentional -- FM modulation can exceed the base pitch range.

**Prevention:**
1. The sign-extension is correct but harmless for the valid pitch range
2. Use the exact clamp value from spec: `if (step > 0x3FFF) step = 0x4000`
3. File an ADR documenting how the 0x4000 clamp is different from the 0x3FFF base-pitch clamp

---

### m3: Noise LFSR tick happens once per tick, not once per voice

**What goes wrong:**
If the noise generator is advanced inside the voice loop (once per NON-enabled voice), the LFSR shifts multiple times per tick when multiple voices use noise. This makes the noise frequency depend on how many voices are noise-enabled.

**Prevention:**
1. Call the noise tick function ONCE before the voice processing loop
2. All voices that read noise in this tick read the same `noise_level` value

---

### m4: Sweep exponential decrease stall near zero

**What goes wrong:**
Exponential decrease multiplies the step by `level / 0x8000`. As level approaches zero, the step approaches zero, and the sweep stalls. This is the same issue as ADSR exponential decay stall, already handled in `spu94_adsr_tick` with `if (scaled_step == 0 && level > 0) scaled_step = -1`.

**Prevention:**
1. Apply the same anti-stall guard: `if (scaled_step == 0 && level > 0) scaled_step = -1`
2. This matches DuckStation's behavior
3. Verify this is also needed for negative-phase exponential decrease (level approaching `-7FFFh`)

---

### m5: Sweep register bits are at different positions than ADSR register bits

**What goes wrong:**
Both sweep and ADSR use the same counter-accumulate formula but the sweep register layout differs from the ADSR register layout. The sweep register (when Bit15=1) is:
- `[15:sweep][14:mode][13:dir][12:phase][11-7:reserved][6-2:shift][1-0:step]`

If the sweep register parser confuses this layout with the ADSR register layout, all sweep rates are wrong.

**Prevention:**
1. Parse the sweep register independently from ADSR registers
2. Unit test the register parser with known bit patterns

---

### m6: NON voices still consume ADSR envelope

**What goes wrong:**
Skipping ADSR for NON-flagged voices because "they don't have a sample."

**Why it happens:**
NON replaces the SAMPLE source, not the entire voice processing chain.

**Prevention:**
1. NON replaces only the ADPCM decode + Gaussian interpolation output
2. ADSR envelope, volume, and all downstream processing still apply
3. A noise voice with a percussive ADSR envelope produces a hi-hat sound. Without ADSR, it's sustained white noise.

---

### m7: Sweep direction naming confusion with negative phase

**What goes wrong:**
Confusing "increase" to mean "volume gets louder" when phase is negative.

**Prevention:**
1. "Increase" means the register value increases toward +0x7FFF
2. "Decrease" means the register value decreases toward 0 (positive phase) or toward -0x7FFF (negative phase)
3. The terminology is about register value direction, not perceived loudness
4. The formula includes: `IF Decreasing XOR PhaseNegative THEN AdsrStep = NOT AdsrStep` -- this conditionally inverts the step

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| P-PMON | C1 (voice processing order), C2 (VxOUTX capture point) | Store VxOUTX immediately after ADSR apply, before volume. Test with slow modulator sine. |
| P-PMON | m1 (voice 0 PMON bit), m2 (pitch clamp 0x4000 vs 0x3FFF) | Guard voice 0. Document pitch range ADR. |
| P-NON | C3 (LFSR polynomial), C4 (timer mechanism) | Verify polynomial taps + seed against DuckStation. Test all 64 frequency settings. |
| P-NON | M1 (ADPCM still decodes), M2 (global noise), m3 (tick once) | Decode but replace output. Single global LFSR in mixer struct. Tick before voice loop. |
| P-SWEEP | C5 (separate from ADSR), M4 (Phase bit gray area), M5 (initial-value timing) | New struct, separate counters, ADR for Phase behavior. |
| P-SWEEP | M6 (sweep IS volume), M7 (KON resets sweep), m4 (exp stall), m5 (register parsing) | Sweep modifies vol_l/r directly. KON resets sweep. Anti-stall guard. |
| P-SIGNED | C6 (existing unsigned assumptions) | Audit all vol_l/vol_r call sites. Add negative-volume golden tests. |
| P-INTEGRATE | M3 (PMON+noise non-interaction), M7 (KON+sweep) | Document PMON/noise non-interaction. Extend pending config for sweep. |
| P-VERIFY | All | Golden files for PMON chain, noise sequence, sweep trajectories, phase inversion. |

---

## Integration: Voice Mixer Tick Must Be Restructured

The current `spu94_voice_mixer_tick` structure:
```
1. Apply pending KON/KOFF
2. Loop voices 0-23:
   a. voice_tick (decode, interpolate, ADSR, volume)
   b. accumulate dry sum
   c. accumulate reverb sum (if EON)
3. Saturate and apply master volume
```

Must become:
```
1. Apply pending KON/KOFF
2. Tick noise generator (ONCE, globally)
3. Loop voices 0-23:
   a. Tick sweep for this voice (update vol_l/vol_r in-place)
   b. If PMON enabled and voice > 0: modify pitch step using voices[v-1].outx
   c. Advance pitch counter (with modified step)
   d. Decode ADPCM block if needed (even for NON voices)
   e. If NON: sample = noise_level; else: sample = gauss_interpolate
   f. ADSR tick -> adsr_level
   g. mono_out = q15_mul(sample, adsr_level)
   h. Store voices[v].outx = mono_out  (for next voice's PMON)
   i. out_l = q15_mul(mono_out, vol_l)  (vol_l is sweep-modified)
   j. out_r = q15_mul(mono_out, vol_r)
   k. accumulate dry and reverb sums
4. Saturate and apply master volume
```

Key changes:
- Step 2 ticks noise globally before any voice runs (m3 prevention)
- Step 3a ticks sweep before computing audio so vol_l/vol_r are current (M6)
- Step 3b reads the *previous* voice's outx (C1 prevention)
- Step 3e branches between noise and interpolation (M1)
- Step 3h stores outx after ADSR but before volume (C2 prevention)

### Reverb send is unaffected

PMON, NON, sweep, and signed volume all affect the per-voice output signal. The EON-gated reverb send reads `out_l`/`out_r` which include all these effects. No changes needed to the reverb send path.

### Preset serialization

Sweep adds per-voice sweep register values. These should be serialized only if the voice engine is extended with user-facing sweep controls. For the C core, sweep parameters are set programmatically.

---

## Sources

- [nocash psx-spx SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- PRIMARY: polynomial, formulas, register layouts, noise timer
- [nocash SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) -- PMON formula, VxOUTX reference, hardware glitches, clamp behavior
- [nocash SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- sweep register format, counter mechanism, phase bit, signed volume
- [DuckStation SPU source](https://github.com/stenzek/duckstation) -- VxOUTX capture point confirmed (after ADSR, before volume), noise seed = 1, voice processing order 0-23 sequential, volume sweep as register modification
- [hitmen SPU documentation](https://hitmen.c02.at/files/docs/psx/spu.txt) -- sweep description, volume phase inversion, general SPU architecture
- [PCSX2 noise algorithm PR #4134](https://github.com/PCSX2/pcsx2/pull/4134) -- noise algorithm accuracy discussion, Dr. Hell's research reference
- Existing SPU-94 source: `spu94_voice.c` (current processing order), `spu94_adsr.c` (counter-accumulate reference), `spu94_voice.h` (struct layout, S2 forward-looking note)
