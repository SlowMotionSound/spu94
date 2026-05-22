# Architecture Patterns: v1.9 Complete Voice

**Domain:** PS1 SPU per-voice modulation features integrated into existing 24-voice sampler engine
**Researched:** 2026-05-21
**Confidence:** HIGH -- all patterns are constrained by PS1 hardware spec (nocash psx-spx) and by the existing codebase architecture (directly observed in source).

## Recommended Architecture

### Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| `spu94_noise_gen` | Step the global LFSR noise generator once per mixer tick; expose current level | `spu94_voice_mixer_tick` reads `noise_gen.level` for NON-flagged voices |
| `spu94_vol_sweep` | Per-voice volume ramp via counter-accumulate; modify vol_l/vol_r in place | `spu94_voice_tick` calls sweep tick before applying volume; reads/writes `voice->vol_l`, `voice->vol_r` |
| PMON logic (inline in mixer tick) | Compute modulated pitch from previous voice output | Reads cached `prev_voice_outx`; writes modulated pitch to next voice's counter advance |
| Signed volume (no new component) | Negative vol_l/vol_r produces phase-inverted output | Existing `q15_mul_truncate` handles this; API/GUI expose the range |

### Signal Flow Per Voice (v1.9)

```
                 +------ NON flag? ------+
                 |                       |
                 v                       v
          noise_gen.level          ADPCM decode
                 |                       |
                 |                       v
                 |              Gaussian interpolate
                 |                       |
                 +--------> mux --------+
                              |
                              v
                     ADSR envelope apply
                              |
                              v
                    (tap: prev_voice_outx)  --> PMON for voice N+1
                              |
                              v
                    Volume apply (vol_l, vol_r)
                     (may be sweep-updated)
                     (may be negative = phase invert)
                              |
                              v
                    L/R output to mixer accumulators
```

### Mixer Tick Order (v1.9)

```
1. Apply pending KON/KOFF (existing, unchanged)
2. Step noise generator once (NEW)
3. FOR v = 0 to 23:
   a. Step volume sweep L/R (NEW -- updates vol_l/vol_r)
   b. Determine effective pitch:
      - If PMON bit set AND v > 0: use modulated pitch from step 3g of previous iteration
      - Else: use voice->pitch (existing)
   c. If NON bit set: gauss_out = noise_gen.level (NEW -- skip decode + interp)
      Else: existing decode + Gaussian interpolation with effective pitch
   d. ADSR tick (existing)
   e. Volume apply: q15_mul_truncate with vol_l/vol_r (existing -- now supports negatives)
   f. Cache gauss_out (post-ADSR, pre-volume) as prev_voice_outx (NEW)
   g. Compute modulated pitch for voice v+1 if pmon_flags & (1 << (v+1)) (NEW)
   h. Accumulate to dry_sum + rev_sum (existing)
4. Saturate, apply master volume (existing, unchanged)
```

### PMON Data Flow Detail

```
Voice 0 tick -> produces outx_0
                    |
                    v
Voice 1 tick: if PMON.bit1, pitch = modulate(voice1.pitch, outx_0)
             -> produces outx_1
                    |
                    v
Voice 2 tick: if PMON.bit2, pitch = modulate(voice2.pitch, outx_1)
             -> produces outx_2
                    ...
Voice 23 tick: if PMON.bit23, pitch = modulate(voice23.pitch, outx_22)
```

This is a forward chain: voice 0 can modulate voice 1, which can modulate voice 2, etc. Chaining PMON across multiple voices creates cascaded FM -- three-operator FM synthesis with ADPCM carriers. This is authentic PS1 behavior.

## Patterns to Follow

### Pattern 1: Counter-Accumulate Reuse

**What:** Volume sweep uses the exact same counter-accumulate mechanism as ADSR.
**When:** Any PS1 SPU hardware timer -- ADSR, sweep, and noise all use variations of the same counter pattern.
**Why:** The PS1 SPU has one counter mechanism in hardware shared across subsystems. Using the same pattern in code means the same integer math, the same timing characteristics, the same audit trail.

```c
/* Pattern: counter-accumulate, fire on bit 15 */
uint32_t increment = 0x8000u >> max(0, shift - 11);
counter += increment;
if (counter & 0x8000u) {
    counter &= ~0x8000u;  /* clear the trigger bit */
    /* apply the step */
}
```

ADSR already implements this. Volume sweep copies the pattern with different step semantics (linear add or exponential multiply).

### Pattern 2: Bitmask Control Registers

**What:** PMON, NON, and EON are all 24-bit bitmasks stored as `uint32_t`.
**When:** Any per-voice binary flag in the PS1 SPU.
**Why:** Matches hardware register layout. Efficient (one bit per voice). The mixer already uses this pattern for `eon_flags` and `pending_kon`/`pending_koff`.

```c
/* Pattern: bitmask-gated per-voice behavior */
if (m->non_flags & (1u << v)) {
    /* noise path */
} else {
    /* sample path */
}
```

### Pattern 3: Module-Per-Feature with Isolation

**What:** Each new SPU subsystem gets its own .c/.h pair with init/tick functions and an isolated state struct.
**When:** Adding any new DSP module to libspu94.
**Why:** Established by spu94_adsr (Phase 28), spu94_dac_noise (Phase 6). Enables unit testing in isolation. Prevents cross-contamination between subsystems.

```c
/* Pattern: isolated state + pure tick function */
void spu94_noise_gen_init(spu94_noise_gen_t *n);
void spu94_noise_gen_tick(spu94_noise_gen_t *n);    /* modifies n->level */
int16_t spu94_noise_gen_get_level(const spu94_noise_gen_t *n);
```

### Pattern 4: Inline Integration Over Callback Architecture

**What:** PMON and NON logic are implemented directly in the mixer tick loop, not via function pointers or callback tables.
**When:** Feature logic is small (<20 lines) and tightly coupled to the voice iteration order.
**Why:** Function pointer dispatch adds indirection and complexity for zero benefit when the logic is a single conditional branch. The mixer tick is the hot path -- inline keeps it cache-friendly.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Separate Voice Output Buffer

**What:** Allocating a 24-element array to hold all voice outputs before applying PMON.
**Why bad:** Wastes stack space. Breaks the sequential evaluation that PMON requires. Makes the code look like a two-pass algorithm when it's actually single-pass.
**Instead:** Use a single `int16_t prev_voice_outx` local variable. Each voice overwrites it after producing output. The next voice reads it if PMON is active.

### Anti-Pattern 2: Abstracting the Counter-Accumulate Mechanism

**What:** Creating a generic "spu94_counter_t" struct shared between ADSR, sweep, and noise.
**Why bad:** ADSR, sweep, and noise all use variations of the counter pattern with different step semantics. Abstracting them into one type forces either:
  - A union/tag-dispatch design that's harder to read than three separate implementations, or
  - Parameter overloading that obscures which counter semantics are active.
**Instead:** Let each module implement its own counter inline. The pattern is small enough (~5 lines) that repetition is cheaper than abstraction.

### Anti-Pattern 3: Noise Generator as a Voice Feature

**What:** Putting `noise_on` as a field in `spu94_voice_t` and having `spu94_voice_tick` handle the NON switch internally.
**Why bad:** NON is a mixer-level concern, not a voice-level concern. The noise generator is global (one LFSR shared by all voices). If `spu94_voice_tick` handles NON, it needs a pointer to the shared noise state, breaking its current isolation (voice tick only reads its own state + voice RAM).
**Instead:** NON dispatch lives in `spu94_voice_mixer_tick`. When NON is set for a voice, the mixer skips calling `spu94_voice_tick` for the ADPCM path and substitutes `noise_gen.level` directly. The voice tick function remains pure per-voice logic.

### Anti-Pattern 4: Exposing Raw Register Bit Layout in API

**What:** Making the caller pack Bit15/Bit14/Bit13 into a uint16_t to set volume sweep mode, matching the PS1's register bit layout.
**Why bad:** SPU-94 is an instrument, not a hardware emulator. Callers (GUI, MIDI, DAW hosts) think in terms of "linear mode, increasing, positive phase" -- not in packed register bits.
**Instead:** Expose sweep parameters as individual fields (mode, direction, phase, shift, step). The C core can pack/unpack internally if needed for preset serialization.

## Scalability Considerations

| Concern | At 24 voices (PS1 spec) | At 48 voices (hypothetical doubling) | Notes |
|---------|------------------------|--------------------------------------|-------|
| Noise generator | 1 instance, O(1) per tick | 1 instance, O(1) per tick | Global; voice count doesn't matter |
| PMON chain | O(N) sequential, N=24 | O(N) sequential, N=48 | Must remain sequential; cannot parallelize |
| Volume sweep | O(N), 2 instances per voice | O(N), 2 per voice | Trivial; sweep tick is ~10 instructions |
| Memory (mixer struct) | ~530 KB (dominated by 512 KB voice RAM) | ~532 KB | Sweep adds ~1.2 KB total across 24 voices |

The 24-voice limit is a PS1 hardware constraint. SPU-94 follows spec; scalability beyond 24 is not a goal.

## Sources

- nocash psx-spx SPU documentation: https://psx-spx.consoledev.net/soundprocessingunitspu/
- Existing mixer implementation: `src/spu94/spu94_voice.c` lines 410-482
- Existing ADSR counter-accumulate: `src/spu94/spu94_adsr.c`
- Existing voice struct: `include/spu94/spu94_voice.h`
- Existing DAC noise module pattern: `include/spu94/spu94_dac_noise.h`
