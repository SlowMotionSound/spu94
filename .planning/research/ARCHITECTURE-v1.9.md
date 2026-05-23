# Architecture: v1.9 Complete Voice (PMON, NON, Volume Sweep, Signed Volume)

**Domain:** PS1 SPU per-voice modulation features
**Researched:** 2026-05-21
**Confidence:** HIGH (cross-verified: nocash psx-spx, hitmen docs, DEEP-SPU-VOICE-PATH.md prior research)

---

## Executive Summary

v1.9 adds four SPU voice features that integrate at different stages of the existing `spu94_voice_tick()` pipeline. Two features (PMON, NON) alter the signal source, one (Volume Sweep) adds a parallel envelope alongside ADSR, and one (Signed Volume) is mostly already supported by the existing `int16_t vol_l/vol_r` declaration but needs its semantics unlocked.

The features have a strict dependency chain: Signed Volume is a prerequisite for Volume Sweep (sweep outputs signed values), and PMON requires a well-defined VxOUTX capture point. NON is fully independent.

---

## 1. Current Voice Tick Processing Order

From `spu94_voice_tick()` in `spu94_voice.c`:

```
STEP 1: Decode ADPCM block if needed (has_block == 0)
STEP 2: Gaussian interpolation (4-tap from gauss_ring)
         -- OR zero-order hold if gauss_bypass=1
STEP 2.5: ADSR envelope (counter-accumulate step, Q15 multiply)
STEP 3: Apply per-voice volume (vol_l, vol_r as Q15 multiply)
STEP 4: Advance pitch counter, push decoded samples into ring
```

---

## 2. Where Each Feature Integrates

### 2.1 PMON (Pitch Modulation)

**What it does:** Voice N's output amplitude modulates voice N+1's pitch. Creates FM synthesis, vibrato, frequency sweeps.

**Register:** PMON flags (bits 1-23; voice 0 cannot be modulated, only modulates).

**Signal chain position:** BEFORE Step 4 (pitch counter advancement). The effective pitch used in counter += step is modified by the previous voice's output.

**Formula (from nocash):**
```
Step = VxPitch
IF PMON enabled AND voice_index > 0:
    Factor = VxOUTX(voice_index - 1) + 0x8000    // range 0..0xFFFF (0.00..1.99)
    Step = (SignExpand16to32(Step) * Factor) >> 15
    Step = Step & 0x0000FFFF                       // mask to 16-bit
IF Step > 0x3FFF:
    Step = 0x4000                                  // hard clip (not 0x3FFF!)
Counter += Step
```

**Critical question: What is VxOUTX?**
The nocash documentation says "amplitude from channel (x-1)" and prior DEEP-SPU-VOICE-PATH research (Section 5.1) confirms DuckStation's SampleVoice() processing order shows ADSR is applied BEFORE volume. VxOUTX is the voice output AFTER Gaussian interpolation and AFTER ADSR envelope, but BEFORE per-voice volume. This is the "raw enveloped sample" -- the signal with tonal shape but not yet panned.

**Confidence:** MEDIUM on VxOUTX capture point. The spec says "voice amplitude" without precision. The most common emulator interpretation (DuckStation, Mednafen) captures post-ADSR, pre-volume. This makes physical sense: volume controls L/R panning; a modulator should modulate with the tonal amplitude, not the panned level.

**Integration point in code:**
- The mixer tick loop (`spu94_voice_mixer_tick`) processes voices 0-23 sequentially. After each voice produces output, we capture VxOUTX (the post-ADSR, pre-volume sample) for use by the NEXT voice.
- The pitch counter advancement in Step 4 must use the PMON-modified step instead of raw `v->pitch`.

**New state needed:**
- `uint32_t pmon_flags` in `spu94_voice_mixer_t` (bit N = voice N uses PMON)
- `int16_t prev_voice_outx` stored in mixer (output of voice N-1 for voice N)

**Architectural change:**
- `spu94_voice_tick()` must accept the effective pitch step (already PMON-modified) OR accept an `outx` pointer to write its raw output for the next voice.
- Better: split pitch counter advancement to accept the effective step, computed by the mixer. The voice tick already takes `pitch` indirectly from `v->pitch`; we add an optional `effective_step` override parameter OR make the mixer compute the step before calling tick.

**Recommended approach:** The mixer loop computes the effective step per voice (applying PMON formula if enabled), then writes it into a temporary before invoking `spu94_voice_tick()`. Inside voice_tick, Step 4 uses the pre-computed effective step instead of raw `v->pitch`. This keeps PMON logic in the mixer (where it has access to both voices) and voice_tick stays single-voice-focused.

---

### 2.2 NON (Noise Generator)

**What it does:** Replaces the ADPCM decode + Gaussian interpolation output with pseudo-random noise. The ADSR envelope and volume still apply normally. Used for hi-hats, cymbals, wind, explosions.

**Register:** NON flags (bits 0-23; any voice can be noise).

**LFSR specification (nocash):**
```
Per 44.1 kHz tick (GLOBAL, not per-voice):
    Timer -= NoiseStep                   // Step = 4,5,6,7 from SPUCNT[9:8]+4
    ParityBit = Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
    IF Timer < 0:
        NoiseLevel = NoiseLevel * 2 + ParityBit     // shift left + insert
        Timer += (0x20000 >> NoiseShift)            // reload
        IF Timer < 0:                               // double-reload possible
            Timer += (0x20000 >> NoiseShift)
```

**Key characteristics:**
- NoiseLevel is a signed 16-bit value (range -0x8000..+0x7FFF)
- NoiseShift = SPUCNT[13:10] (0..15, controls frequency)
- NoiseStep = SPUCNT[9:8] + 4 (always 4,5,6,7)
- ALL noise-enabled voices share the SAME NoiseLevel value at any given tick
- The noise frequency is GLOBAL -- there is NO per-voice noise pitch control
- The voice's VxPitch register has NO effect when NON is enabled

**Signal chain position:** Replaces Steps 1 + 2 (ADPCM decode + Gaussian interpolation). The noise output goes directly to Step 2.5 (ADSR) and Step 3 (volume). The pitch counter (Step 4) still advances but its output is unused for sample generation (only relevant if the voice later switches back to ADPCM mode).

**Integration point in code:**
- New global noise generator state: `spu94_noise_gen_t` with LFSR, timer, level
- `uint32_t non_flags` in `spu94_voice_mixer_t` (bit N = voice N uses noise)
- In the mixer tick: step the global noise generator ONCE per tick (before any voice). Then for NON-enabled voices, substitute NoiseLevel for the Gaussian output in `spu94_voice_tick()`.

**New component:** `spu94_noise_gen_t`
```c
typedef struct {
    int16_t  level;       // current noise output (-0x8000..+0x7FFF)
    int32_t  timer;       // countdown timer (signed for < 0 check)
    uint8_t  shift;       // 0..15 from SPUCNT[13:10]
    uint8_t  step;        // 4..7 from SPUCNT[9:8]+4
} spu94_noise_gen_t;
```

**NOTE:** This LFSR is completely different from the DAC noise LFSR in `spu94_dac_noise.c`. The DAC noise uses a 32-bit Galois LFSR with polynomial 0x80200003. The SPU voice noise uses a 16-bit Fibonacci-style shift register with taps at bits 15, 12, 11, 10. They serve different purposes and must remain separate.

---

### 2.3 Volume Sweep

**What it does:** Hardware-driven automatic volume ramp on a per-voice basis, independent of ADSR. Creates autopanning, stereo widening, fade-ins/outs that run without CPU intervention.

**Register format (per-voice, L and R independently):**
```
Bit 15:   1 = Sweep mode active (0 = fixed volume, write literal value)
Bit 14:   Mode (0 = Linear, 1 = Exponential)
Bit 13:   Direction (0 = Increase, 1 = Decrease)
Bit 12:   Phase (0 = Positive, 1 = Negative)
Bits 11-7: Unused
Bits 6-2:  Shift (0..0x1F = Fast..Slow)
Bits 1-0:  Step (0..3 maps to +7/+6/+5/+4 or -8/-7/-6/-5)
```

**Mechanism:** Uses the IDENTICAL counter-accumulate mechanism as ADSR:
```
CounterIncrement = 0x8000 >> max(0, ShiftValue - 11)
AdsrStep = (7 - StepValue) << max(0, 11 - ShiftValue)

IF Direction == Decrease:
    AdsrStep = -AdsrStep    // negate to subtract
    IF Exponential:
        AdsrStep = AdsrStep * CurrentVolume / 0x8000    // scale by level

IF Direction == Increase AND Exponential:
    IF CurrentVolume > 0x6000:
        CounterIncrement >>= 2    // fake exponential (same as ADSR attack)

Counter += CounterIncrement
IF Counter.Bit15:
    Counter &= ~0x8000
    CurrentVolume += AdsrStep
    Clamp CurrentVolume to [-0x8000, +0x7FFF]
    // Direction-specific clamp:
    //   Increasing positive: clamp at +0x7FFF
    //   Decreasing positive: clamp at 0x0000
    //   Increasing negative (phase=1): clamp at -0x7FFF
    //   Decreasing negative (phase=1): clamp at 0x0000
```

**Signal chain position:** Step 3 -- Volume Sweep modifies `vol_l` / `vol_r` each tick BEFORE they are applied as the Q15 multiply. ADSR and Sweep are independent parallel envelopes; ADSR shapes the note timbre, Sweep shapes the spatial position.

**Integration point in code:**
- New per-voice sweep state: `spu94_sweep_state_t` (one for L, one for R)
- Inside `spu94_voice_tick()` between Steps 2.5 and 3: tick both sweep envelopes, then use swept volume for the Q15 multiply
- OR: sweep ticks in the mixer before voice_tick, updating `v->vol_l` / `v->vol_r` directly

**Recommended approach:** Add sweep state inside `spu94_voice_t`. Tick the sweep at the START of `spu94_voice_tick()` (or in the mixer loop, before calling voice_tick). The swept volume value is what gets used in Step 3. This way, `v->vol_l` / `v->vol_r` always hold the CURRENT volume level (not the register programming).

**New component:** `spu94_sweep_t`
```c
typedef struct {
    int16_t  level;       // current volume level (-0x8000..+0x7FFF)
    uint32_t counter;     // same counter-accumulate as ADSR
    uint8_t  mode;        // 0=linear, 1=exponential
    uint8_t  direction;   // 0=increase, 1=decrease
    uint8_t  phase;       // 0=positive, 1=negative
    uint8_t  shift;       // 0..31
    uint8_t  step;        // 0..3
    uint8_t  active;      // 1=sweeping, 0=fixed volume
} spu94_sweep_t;
```

**Relationship to existing ADSR code:** The stepping math is IDENTICAL. Factor out the counter-accumulate-step core into a shared helper:
```c
// Returns new level after one tick of counter-accumulate stepping
int16_t spu94_envelope_step(int16_t current_level, uint32_t *counter,
                            uint8_t shift, uint8_t step,
                            uint8_t exponential, uint8_t decrease,
                            uint8_t phase_negative);
```

This helper can be called by BOTH spu94_adsr_tick AND spu94_sweep_tick, eliminating code duplication and ensuring they use the exact same counter mechanism.

---

### 2.4 Signed Volume (Phase Inversion)

**What it does:** Allows per-voice volume registers to hold negative values (-0x8000 to +0x7FFF). Negative volume inverts the signal phase. Used for stereo widening, "Dolby Surround" simulation, cancellation effects.

**Current state in codebase:** The `spu94_voice_t` already declares `vol_l` and `vol_r` as `int16_t` with this comment:
```c
int16_t   vol_l;  /* per-voice left volume (0..32767, unsigned semantics) */
int16_t   vol_r;  /* per-voice right volume (0..32767, unsigned semantics) */
```

The declaration is signed, but the SEMANTICS are documented as unsigned (0..32767). The existing `q15_mul_truncate()` function already handles signed multiplication correctly -- if you pass a negative volume, the output will be phase-inverted. So **the math already works**; what needs changing is:

1. Remove the "unsigned semantics" constraint from documentation and API
2. Allow `spu94_voice_mixer_key_on()` to accept negative volume values
3. Ensure the GUI / external API communicates that negative volume = phase inversion
4. When Volume Sweep crosses zero with phase=1, the volume becomes negative (phase-inverted)

**Signal chain position:** No change to processing order. `q15_mul_truncate(sample, negative_volume)` naturally produces an inverted sample.

**Integration cost:** Near-zero in the C core. This is primarily a documentation and API validation change. The real engineering is in making Volume Sweep output negative values correctly when the phase bit is set.

---

## 3. Modified Voice Tick Pipeline (After v1.9)

```
MIXER LEVEL (per tick, before voices):
  - Step global noise generator once
  - For each voice 0..23:
      a) Compute effective_step (apply PMON if enabled)
      b) Call spu94_voice_tick(v, effective_step, noise_level, non_enabled, ...)
      c) Capture VxOUTX = post-ADSR, pre-volume sample (for next voice's PMON)

VOICE TICK (inside spu94_voice_tick):
  STEP 0: Volume Sweep tick (update vol_l, vol_r if sweep active)
  STEP 1: IF NON enabled: gauss_out = noise_level (skip ADPCM decode + Gauss)
           ELSE: Decode ADPCM block if needed
  STEP 2: IF NOT NON: Gaussian interpolation (or zero-order hold)
  STEP 2.5: ADSR envelope (unchanged)
  STEP 2.75: Capture VxOUTX = gauss_out * adsr_level (write to output param)
  STEP 3: Apply per-voice volume (vol_l, vol_r -- now possibly negative)
  STEP 4: Advance pitch counter using effective_step (PMON-modified)
           Push samples into ring (even for NON -- counter still advances)
```

---

## 4. New Components

| Component | File | Purpose | Lines (est) |
|-----------|------|---------|-------------|
| `spu94_noise_gen_t` | `spu94_noise.h` / `spu94_noise.c` | Global LFSR noise generator | ~80 |
| `spu94_sweep_t` | `spu94_sweep.h` / `spu94_sweep.c` | Per-voice volume sweep envelope | ~120 |
| `spu94_envelope_step()` | `spu94_envelope_step.h` | Shared counter-accumulate core (refactored from ADSR) | ~40 |

---

## 5. Modified Components

| Component | File | Change |
|-----------|------|--------|
| `spu94_voice_t` | `spu94_voice.h` | Add 2x `spu94_sweep_t` (L/R), remove "unsigned semantics" comment |
| `spu94_voice_tick()` | `spu94_voice.c` | Accept effective_step + noise_level + non_flag; add sweep tick; capture VxOUTX |
| `spu94_voice_mixer_t` | `spu94_voice.h` | Add `pmon_flags`, `non_flags`, `spu94_noise_gen_t`, `int16_t outx[24]` |
| `spu94_voice_mixer_tick()` | `spu94_voice.c` | Compute PMON steps, step noise gen, pass params to voice_tick |
| `spu94_voice_mixer_key_on()` | `spu94_voice.c` | Accept negative volumes (signed range validation) |
| `spu94_adsr.c` | `spu94_adsr.c` | Refactor stepping core to use shared `spu94_envelope_step()` |

---

## 6. Data Flow Diagrams

### 6.1 PMON Data Flow

```
Voice 0 tick:
  ADPCM -> Gauss -> ADSR -> [VxOUTX captured] -> Volume -> Output
                              |
                              v
Voice 1 tick (if PMON.bit1):  |
  Step = Voice1.pitch         |
  Factor = VxOUTX[0] + 0x8000
  Step = (Step * Factor) >> 15
  Step = clamp(Step, 0x4000)
  Counter += Step
  ADPCM -> Gauss -> ADSR -> [VxOUTX captured] -> Volume -> Output
                              |
                              v
Voice 2 tick (if PMON.bit2):  ...
```

### 6.2 NON Data Flow

```
                    Global Noise Gen (ticked once per mixer tick)
                           |
                           v
                      NoiseLevel (int16_t)
                           |
    +-------+--------------+---------------+
    |       |              |               |
    v       v              v               v
  Voice 3  Voice 7       Voice 12        Voice 20
  (NON=1)  (NON=1)      (NON=1)         (NON=1)
    |       |              |               |
    v       v              v               v
  ADSR    ADSR           ADSR            ADSR
    |       |              |               |
    v       v              v               v
  Volume  Volume         Volume          Volume
    |       |              |               |
    v       v              v               v
  Output  Output         Output          Output

Note: ALL noise voices output the SAME NoiseLevel each tick
      (different ADSR and volume shapes differentiate them)
```

### 6.3 Volume Sweep Data Flow

```
Per Voice (independent L and R):

  Sweep Register Write (bit15=1):
    -> Configure sweep params (mode, dir, phase, shift, step)
    -> sweep.active = 1
    -> sweep.level = current vol_l or vol_r (initial position)

  Each Tick:
    sweep_counter += CounterIncrement
    IF bit15 set:
        sweep.level += computed_step (linear or exponential)
        Clamp to boundaries
    vol_l = sweep_l.level   (replaces static volume)
    vol_r = sweep_r.level

  Applied in Step 3 of voice_tick:
    output_l = q15_mul_truncate(adsr_output, vol_l)  // vol_l may be negative
    output_r = q15_mul_truncate(adsr_output, vol_r)  // vol_r may be negative
```

---

## 7. Mixer Struct Changes

```c
typedef struct {
    spu94_voice_t voices[24];
    uint8_t       voice_ram[SPU94_SPU_RAM_BYTES];
    spu94_voice_t pending_config[24];
    uint32_t      pending_kon;
    uint32_t      pending_koff;
    uint32_t      eon_flags;
    int16_t       master_vol_l;
    int16_t       master_vol_r;
    uint8_t       enabled;
    uint8_t       gauss_bypass;

    /* v1.9 additions */
    uint32_t      pmon_flags;        /* bit N = voice N pitch-modulated by voice N-1 */
    uint32_t      non_flags;         /* bit N = voice N outputs noise instead of ADPCM */
    spu94_noise_gen_t noise_gen;     /* global noise generator (shared by all NON voices) */
    int16_t       outx[24];          /* per-voice VxOUTX capture (post-ADSR, pre-volume) */
} spu94_voice_mixer_t;
```

---

## 8. Voice Struct Changes

```c
typedef struct {
    /* ... existing fields unchanged ... */
    int16_t   vol_l;              /* per-voice left volume (-0x8000..+0x7FFF, SIGNED) */
    int16_t   vol_r;              /* per-voice right volume (-0x8000..+0x7FFF, SIGNED) */
    spu94_sweep_t sweep_l;        /* v1.9: left volume sweep state */
    spu94_sweep_t sweep_r;        /* v1.9: right volume sweep state */
    /* ... remaining existing fields ... */
} spu94_voice_t;
```

---

## 9. API Changes

### New Public Functions

```c
/* Global noise generator control (affects all NON-enabled voices) */
void spu94_noise_gen_init(spu94_noise_gen_t *ng);
void spu94_noise_gen_tick(spu94_noise_gen_t *ng);  // advances LFSR+timer
void spu94_noise_gen_set_freq(spu94_noise_gen_t *ng, uint8_t shift, uint8_t step);

/* Mixer-level feature flags */
spu94_result_t spu94_voice_mixer_set_pmon(spu94_voice_mixer_t *m, int voice_idx, int enabled);
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m, int voice_idx, int enabled);
spu94_result_t spu94_voice_mixer_set_noise_freq(spu94_voice_mixer_t *m, uint8_t shift, uint8_t step);

/* Per-voice sweep control */
spu94_result_t spu94_voice_mixer_set_sweep_l(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase, uint8_t shift, uint8_t step);
spu94_result_t spu94_voice_mixer_set_sweep_r(spu94_voice_mixer_t *m, int voice_idx,
    uint8_t mode, uint8_t direction, uint8_t phase, uint8_t shift, uint8_t step);
spu94_result_t spu94_voice_mixer_set_volume(spu94_voice_mixer_t *m, int voice_idx,
    int16_t vol_l, int16_t vol_r);  // signed: negative = phase inversion

/* VxOUTX readback (for diagnostics, display) */
int16_t spu94_voice_mixer_get_outx(const spu94_voice_mixer_t *m, int voice_idx);
```

---

## 10. Suggested Build Order

**Rationale:** Dependencies flow from simple (no dependencies) to complex (requires earlier features).

### Phase A: Signed Volume (1 day)
- Zero-risk change. Unlock negative values on existing `vol_l`/`vol_r`.
- Update `spu94_voice_mixer_key_on()` and `spu94_voice_mixer_set_volume()` validation.
- Remove "unsigned semantics" documentation/comments.
- Test: verify `q15_mul_truncate(-0x4000, 0x7FFF)` == `-0x4000` (phase flip).
- **Why first:** Sweep requires signed volumes to work correctly. Get the simplest piece locked first.

### Phase B: Noise Generator (2-3 days)
- Standalone new component, no coupling to other v1.9 features.
- Implement `spu94_noise_gen_t` with timer/LFSR/tick.
- Add `non_flags` to mixer; in voice tick, substitute NoiseLevel for Gaussian output when NON bit set.
- Test: frequency characterization (verify spectral content varies with shift/step), timer reload behavior, parity bit calculation.
- **Why second:** Fully independent, no interaction with other new features. Provides immediate musical value (hi-hats, noise sweeps with ADSR).

### Phase C: Volume Sweep + Envelope Step Refactor (3-4 days)
- Refactor `spu94_adsr_tick()` counter-accumulate core into `spu94_envelope_step()`.
- Verify ADSR golden files still pass (refactor must be bit-identical).
- Implement `spu94_sweep_t` using the shared helper.
- Add sweep_l/sweep_r to `spu94_voice_t`.
- Wire sweep tick into voice pipeline (before volume apply).
- Test: linear ramp, exponential ramp, phase inversion through zero, boundary clamping.
- **Why third:** Requires Signed Volume (Phase A). Moderate complexity. Shared helper refactor has golden-file regression risk.

### Phase D: PMON (Pitch Modulation) (2-3 days)
- Requires VxOUTX capture (modify voice tick to output the post-ADSR sample).
- Add `pmon_flags` to mixer. Add `outx[24]` capture array.
- Modify mixer loop: compute effective step per voice; pass to voice_tick.
- Modify voice_tick Step 4: use effective_step parameter instead of `v->pitch` directly.
- Test: sine-wave modulator on voice N, verify voice N+1 frequency sweeps proportionally. Hard-clip at 0x4000. Sign-extend behavior for pitches > 0x7FFF.
- **Why last:** Most complex feature. Requires changes to the mixer-to-voice interface. Touches the pitch counter advancement (Step 4) which is the most performance-critical code path. Voice-to-voice dependency means any bug in capture point cascades to subsequent voices.

---

## 11. Critical Architectural Decisions Needed

| Decision | Options | Recommendation |
|----------|---------|----------------|
| VxOUTX capture point | (A) post-Gauss/pre-ADSR, (B) post-ADSR/pre-volume, (C) post-volume | (B) post-ADSR, pre-volume -- matches DuckStation/Mednafen interpretation |
| PMON parameter passing | (A) compute in mixer, pass effective_step to voice_tick, (B) voice_tick reads prev output internally | (A) compute in mixer -- keeps voice_tick single-voice-focused |
| Sweep state location | (A) inside spu94_voice_t, (B) separate array in mixer | (A) inside voice_t -- sweep is per-voice state, travels with voice on KON |
| ADSR refactor scope | (A) extract shared helper, (B) copy-paste ADSR logic for sweep | (A) shared helper -- same hardware, must be bit-identical |
| NON vs pitch counter | (A) still advance counter when NON, (B) freeze counter | (A) still advance -- hardware advances counter regardless; matters for PMON chain and mode switching |
| Noise gen location | (A) inside mixer struct, (B) global singleton | (A) inside mixer -- follows existing pattern (mixer owns all voice state) |

---

## 12. Interaction Matrix

| Feature | Interacts With | How |
|---------|---------------|-----|
| PMON | NON | A noise voice's VxOUTX can still modulate next voice's pitch (noise becomes pitch-mod source) |
| PMON | Signed Volume | VxOUTX is captured pre-volume, so phase-inverted volume does NOT affect modulation |
| PMON | Volume Sweep | Same as above -- sweep changes volume, not VxOUTX |
| NON | ADSR | Noise is shaped by ADSR normally (attack/decay on noise = filter effect) |
| NON | Volume Sweep | Sweep applies normally to noise voices |
| Volume Sweep | Signed Volume | Sweep can output negative volumes (phase bit = 1); this IS signed volume in action |
| Volume Sweep | ADSR | Independent parallel envelopes -- both shape the output multiplicatively |

---

## 13. Memory Impact

| Component | Size per voice | Total (24 voices) |
|-----------|---------------|-------------------|
| `spu94_sweep_t` x2 (L+R) | ~24 bytes | ~576 bytes |
| `outx[24]` array | 2 bytes | 48 bytes |
| `spu94_noise_gen_t` (global) | -- | ~12 bytes |
| `pmon_flags` + `non_flags` | -- | 8 bytes |
| **Total new memory** | | **~644 bytes** |

Negligible impact on the ~530 KB mixer struct.

---

## 14. RT-Safety Verification

All new components maintain the existing RT-safety invariants:
- No heap allocation (all state in caller-provided structs)
- No locks (single-threaded DSP path)
- No syscalls (pure integer math)
- No branching on external state (LFSR is deterministic from seed)
- Counter-accumulate math is branch-predictable (bit-test + conditional add)

---

## Sources

- [nocash psx-spx: SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) -- PMON formula, pitch counter advancement
- [nocash psx-spx: SPU Noise Generator](https://problemkaputt.de/psxspx-spu-noise-generator.htm) -- LFSR algorithm, timer mechanism
- [nocash psx-spx: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- sweep mechanism, counter-accumulate table, signed volume
- [psx-spx consoledev: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- register addresses, PMON/NON flag bits
- [hitmen SPU documentation](https://hitmen.c02.at/files/docs/psx/spu.txt) -- volume register format, phase inversion bit
- `.planning/research/DEEP-SPU-VOICE-PATH.md` -- prior v1.8 research on processing order, DuckStation/Mednafen behavior
- Existing `spu94_adsr.c` -- counter-accumulate implementation (verified working, basis for sweep)
