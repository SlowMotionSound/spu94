# Phase 36: Noise Generator (NON) - Research

**Researched:** 2026-05-22
**Domain:** PS1 SPU global noise generator with LFSR, timer-driven frequency control, per-voice NON enable
**Confidence:** HIGH

## Summary

The PS1 SPU noise generator is a single global 16-bit LFSR that produces pseudo-random noise shared by all NON-enabled voices. It replaces the ADPCM decode + Gaussian interpolation stage in the voice pipeline while ADSR envelope and volume multiply still apply normally. The noise frequency is controlled exclusively by two fields in the SPUCNT register (NoiseShift and NoiseStep); per-voice pitch registers have zero effect on noise output.

The implementation is a standalone new component (`spu94_noise_gen_t`) living in its own source files, entirely separate from the existing DAC noise LFSR in `spu94_dac_noise.c`. The two share the name "noise" but differ in polynomial, bit width, purpose, and spectral characteristics. The SPU noise is flat-spectrum percussion/texture source; the DAC noise is shaped (+12 dB/octave) quantization error modeling.

**Primary recommendation:** Implement the noise generator as a new `spu94_noise.h`/`spu94_noise.c` module with `spu94_noise_gen_t` struct. Add `non_flags` and `spu94_noise_gen_t noise_gen` to the mixer struct. Tick the noise generator ONCE per mixer tick before the voice loop. For NON-enabled voices, substitute the global NoiseLevel for the Gaussian output at the branch point between Steps 2 and 2.5 in voice_tick. ADPCM decode must still run for NON voices (loop flags, ENDX status), but its decoded output is discarded.

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| NON-01 | Single global LFSR, taps at bits 15,12,11,10 XOR 1 (XNOR), seed=1, left-shift | Nocash pseudocode + DuckStation reset code confirm seed=1; XNOR formula verified mathematically equivalent to DuckStation lookup table |
| NON-02 | Timer decrement by NoiseStep (4-7), shift on underflow, double-reload | Nocash pseudocode shows exact 5-line algorithm; double-reload confirmed in both nocash and DuckStation |
| NON-03 | Noise frequency from SPUCNT[13:10] (NoiseShift) and [9:8] (NoiseStep); per-voice pitch ignored | Nocash: "frequency solely controlled by Shift/Step"; psx-spx register docs confirm bit positions |
| NON-04 | NON 24-bit bitmask register selects noise voices | Register 1F801D94h documented in psx-spx |
| NON-05 | All NON voices share same NoiseLevel per tick | Nocash: "all forcefully having same frequency"; architectural consequence of single global generator |
| NON-06 | ADPCM decode still runs for NON voices (flag side effects) | DuckStation SampleVoice() always runs ADPCM fetch; flags fire regardless of NON |
| NON-07 | ADSR still applies to noise output | Noise replaces gauss_out, not the full pipeline; ADSR step 2.5 runs on noise value |
| NON-08 | Noise ticks once globally before voice loop | Architectural decision from ARCHITECTURE-v1.9.md; matches hardware behavior (one counter, not per-voice) |
| NON-09 | ADR documenting noise initial state, ADPCM-fetch-during-NON, LFSR polynomial | ADR-0058 planned; covers all three topics |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| LFSR noise generation | C core (spu94_noise.c) | -- | Pure integer math, no UI involvement |
| Timer / frequency control | C core (spu94_noise.c) | -- | SPUCNT register fields decoded in noise_gen |
| NON flag routing | C core (spu94_voice.c mixer) | -- | Mixer decides noise vs ADPCM branch per voice |
| ADPCM-during-NON | C core (spu94_voice.c) | -- | Voice tick still decodes blocks for flag dispatch |
| Noise frequency UI | JUCE plugin (deferred) | -- | Not in Phase 36 scope; SPUCNT bits exposed via API |

## Standard Stack

### Core

No external libraries needed. This phase is pure C integer math within the existing codebase.

| Component | File | Purpose | Why |
|-----------|------|---------|-----|
| `spu94_noise_gen_t` | `spu94_noise.h` / `spu94_noise.c` | Global LFSR noise generator | New module, ~60-80 lines |
| `spu94_voice.c` (modified) | existing | Branch NON voices to noise output | Minimal change in voice_tick |
| `spu94_voice.h` (modified) | existing | Add non_flags + noise_gen to mixer struct | 3 new fields |

### Supporting

| Component | File | Purpose |
|-----------|------|---------|
| Unity test framework | vendor/Unity | Existing test infrastructure |
| `spu94_q15.h` | existing | `sat_s16` used for noise level clamping |

## Architecture Patterns

### System Architecture Diagram

```
SPUCNT register bits [13:8]
        |
        v
  +------------------+
  |  Noise Generator  |  ticked ONCE per mixer tick
  |  (spu94_noise_gen) |
  |                    |
  |  LFSR: 16-bit     |
  |  Timer: int32_t   |
  |  Level: int16_t   |
  +--------+---------+
           |
           | NoiseLevel (shared)
           |
   +-------+-------+-------+-- ... --+
   |       |       |       |         |
   v       v       v       v         v
 Voice 0  Voice 1  Voice 2  ...    Voice 23
 NON=0    NON=1    NON=0   ...    NON=1
   |       |       |                 |
   v       v       v                 v
 ADPCM   NOISE   ADPCM            NOISE
 Gauss   Level   Gauss            Level
   |       |       |                 |
   v       v       v                 v
 ADSR    ADSR    ADSR              ADSR
   |       |       |                 |
   v       v       v                 v
 outx    outx    outx              outx    <-- VxOUTX for PMON
   |       |       |                 |
   v       v       v                 v
 Volume  Volume  Volume            Volume
   |       |       |                 |
   v       v       v                 v
  dry+rev accumulator
```

### Recommended Project Structure

```
include/spu94/
  spu94_noise.h          # NEW: noise generator type + API
  spu94_voice.h          # MODIFIED: non_flags + noise_gen in mixer
src/spu94/
  spu94_noise.c          # NEW: noise generator implementation
  spu94_voice.c          # MODIFIED: branch NON voices, tick noise
tests/unit/voice/
  test_voice_tick.c      # MODIFIED: add NON tests
docs/
  DECISIONS.md           # MODIFIED: ADR-0058
```

### Pattern 1: Noise-ADPCM Branch in voice_tick

**What:** When NON is enabled for a voice, substitute the global NoiseLevel for the Gaussian interpolation output, but still run ADPCM decode for loop flag side effects.

**When to use:** Every voice tick where `non_enabled` flag is true.

**How it integrates into the existing pipeline:**

```c
// Inside spu94_voice_tick, after STEP 1 (ADPCM decode) which runs ALWAYS:
// [CITED: nocash psxspx-spu-noise-generator.htm]

int16_t gauss_out;
if (non_enabled) {
    // NON-04/NON-05: substitute global noise level for Gauss output
    gauss_out = noise_level;
    // ADPCM decode above still ran -- loop flags, ENDX status are side effects
} else {
    // Existing Gaussian interpolation code (Step 2)
    // ... gauss_ring lookup ...
}

// Steps 2.5 onward are UNCHANGED:
// ADSR envelope applies to gauss_out (which may be noise)
// VxOUTX captures post-ADSR value (noise * ADSR = shaped noise for PMON)
// Volume multiply applies normally
```

### Pattern 2: Noise Generator Tick

**What:** Advance the global LFSR once per mixer tick.

**When to use:** Called once in `spu94_voice_mixer_tick` before the voice loop.

```c
// [CITED: nocash psxspx-spu-noise-generator.htm]
void spu94_noise_gen_tick(spu94_noise_gen_t *ng) {
    // Step 1: decrement timer by NoiseStep
    ng->timer -= ng->step;

    // Step 2: compute parity bit from CURRENT level (before any shift)
    // Taps: bit15, bit12, bit11, bit10, XOR 1 (XNOR)
    uint16_t lvl = (uint16_t)ng->level;
    int parity = ((lvl >> 15) ^ (lvl >> 12) ^ (lvl >> 11) ^ (lvl >> 10) ^ 1) & 1;

    // Step 3: if timer underflowed, shift LFSR left and insert parity
    if (ng->timer < 0) {
        ng->level = (int16_t)((uint16_t)(lvl << 1) | parity);
        // Reload timer
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }

    // Step 4: double-reload if still negative (does NOT re-shift level)
    if (ng->timer < 0) {
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }
}
```

### Pattern 3: voice_tick Signature Change

**What:** The voice_tick function needs two new parameters: the current noise level and whether this voice is NON-enabled.

**Approach:** Add `int16_t noise_level` and `uint8_t non_enabled` parameters to `spu94_voice_tick()`.

```c
// [ASSUMED] -- API design choice, consistent with existing parameter-passing pattern
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      uint8_t gauss_bypass,
                      int16_t noise_level,    // NEW: global noise value
                      uint8_t non_enabled,     // NEW: 1 = use noise, 0 = use ADPCM
                      int16_t *out_l, int16_t *out_r);
```

### Anti-Patterns to Avoid

- **Per-voice LFSR:** PS1 has exactly ONE noise generator. Do not create per-voice instances. All NON voices output identical noise.
- **Skipping ADPCM decode for NON voices:** The hardware always decodes. Loop flags must fire, ENDX must update, current_addr must advance. Skipping decode breaks loop mechanics if a voice switches between NON and normal mid-playback.
- **Confusing with DAC noise:** `spu94_dac_noise.c` is a 32-bit Galois LFSR for analog DAC quantization modeling. `spu94_noise.c` is a 16-bit Fibonacci-style LFSR for digital percussion. Different polynomial, different purpose, different spectral character.
- **Using unsigned timer:** The timer MUST be `int32_t` (signed). The `if (timer < 0)` check requires signed comparison. Unsigned would never go negative.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| LFSR feedback | Custom polynomial search | Exact taps from nocash spec (bits 15,12,11,10 XOR 1) | PS1 uses a specific fixed polynomial; any other produces wrong noise character |
| Timer mechanism | Custom frequency control | Exact nocash pseudocode (decrement + reload) | Double-reload is subtle; getting the timer wrong changes noise pitch by orders of magnitude |
| Noise level clamping | Manual saturation | `int16_t` type + left-shift wrapping naturally to 16 bits | The LFSR level IS a 16-bit signed value; it wraps naturally through shift+insert |

**Key insight:** The noise generator is exactly 5 lines of pseudocode in the nocash spec. The implementation should be nearly as compact. Over-engineering this (lookup tables, precomputation, optimization) adds complexity with no benefit -- it runs once per 44.1 kHz tick.

## Common Pitfalls

### Pitfall 1: Timer Sign Extension (Pitfall 5 from PITFALLS-v1.9.md)

**What goes wrong:** Declaring the noise timer as `uint32_t` makes the `if (timer < 0)` check always false.
**Why it happens:** Instinct to use unsigned for counters.
**How to avoid:** Declare as `int32_t`. The timer starts positive (after reload), decrements by NoiseStep (4-7), and goes negative when it underflows.
**Warning signs:** Noise never changes frequency regardless of NoiseShift/NoiseStep settings.

### Pitfall 2: Missing Double-Reload (Pitfall 6 from PITFALLS-v1.9.md)

**What goes wrong:** Only reloading the timer once after LFSR shift. At high NoiseStep (7) and high NoiseShift (15), reload value is `0x20000 >> 15 = 1`. A single reload of +1 after decrementing by 7 leaves the timer at -6, still negative. Next tick would immediately shift again.
**Why it happens:** The nocash pseudocode has TWO sequential `IF Timer<0` lines for the reload, but the second does NOT shift the LFSR -- only reloads the timer. Easy to miss.
**How to avoid:** Implement exactly as spec: shift LFSR on first underflow check, then reload timer, then check underflow again and reload again (but NOT re-shift).
**Warning signs:** At extreme shift/step combinations, noise updates every tick instead of at the configured rate, producing a much higher pitch than expected.

### Pitfall 3: Parity Bit Computed from Wrong Level

**What goes wrong:** Computing the parity bit from the SHIFTED level (after the `level << 1` operation) instead of the pre-shift level.
**Why it happens:** Natural instinct to compute parity and shift in one step.
**How to avoid:** The nocash pseudocode computes ParityBit on line 3, but the shift happens on line 4 (conditionally). The parity is always computed from the current level before any modification. Even if `Timer >= 0` and no shift happens, the parity is still computed (it just isn't used).
**Warning signs:** Noise sequence diverges from reference emulators after a few hundred ticks.

### Pitfall 4: Confusing SPU Noise with DAC Noise

**What goes wrong:** Reusing or referencing `spu94_dac_noise_state` types, or sharing the LFSR polynomial between the two noise sources.
**Why it happens:** Both are "noise" and both use LFSRs.
**How to avoid:** Completely separate type names, separate files, different polynomials. `spu94_noise_gen_t` (SPU voice noise) vs `spu94_dac_noise_state` (DAC quantization noise). Comment both with their purposes.
**Warning signs:** Noise has wrong spectral character -- SPU noise should be flat, DAC noise has +12 dB/octave slope.

### Pitfall 5: Skipping ADPCM Decode for NON Voices

**What goes wrong:** Optimization instinct: "if we're outputting noise, skip the decode." But ADPCM decode has SIDE EFFECTS: loop flag dispatch (LOOP-01 through LOOP-05), ENDX status (LOOP-05, M3), and current_addr advancement.
**Why it happens:** Performance optimization that ignores side effects.
**How to avoid:** Always run ADPCM decode (Step 1) for active voices regardless of NON. The branch point is after decode: use noise_level instead of gauss_out, but the decode still happened.
**Warning signs:** A NON voice that has a looping sample never fires ENDX, or never loops back to loop_addr.

### Pitfall 6: Forgetting Pitch Counter Advancement for NON Voices

**What goes wrong:** Skipping Step 4 (pitch counter advance, sample push to ring) because "the counter isn't used for sample generation."
**Why it happens:** Same optimization instinct.
**How to avoid:** The hardware always advances the counter. This matters for: (1) voice switching back to ADPCM mid-playback, (2) PMON on the next voice uses this voice's outx (which comes from noise * ADSR, not from pitch counter), (3) the ADPCM decode trigger depends on sample consumption from the counter.
**Warning signs:** After disabling NON, the voice resumes ADPCM from a stale position in the decode buffer.

## Code Examples

### Complete Noise Generator Type and Init

```c
// [CITED: nocash psxspx-spu-noise-generator.htm]
// [CITED: DuckStation spu.cpp Reset() -- seed = 1]
typedef struct {
    int16_t  level;       // current noise output (-0x8000..+0x7FFF)
    int32_t  timer;       // countdown timer (MUST be signed for < 0 check)
    uint8_t  shift;       // 0..15 from SPUCNT[13:10] (NoiseShift)
    uint8_t  step;        // 4..7 from SPUCNT[9:8]+4 (NoiseStep)
} spu94_noise_gen_t;

void spu94_noise_gen_init(spu94_noise_gen_t *ng) {
    ng->level = 1;        // initial seed = 1 (NOT 0 -- zero would be absorbing)
    ng->timer = 0;
    ng->shift = 0;
    ng->step  = 4;        // minimum step (SPUCNT[9:8] = 0 -> step = 0+4 = 4)
}
```

### Complete Noise Tick (from nocash pseudocode)

```c
// [CITED: nocash psxspx-spu-noise-generator.htm]
void spu94_noise_gen_tick(spu94_noise_gen_t *ng) {
    // Line 2: Timer = Timer - NoiseStep
    ng->timer -= (int32_t)ng->step;

    // Line 3: ParityBit = Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
    // Computed from CURRENT level, BEFORE any shift
    uint16_t lvl = (uint16_t)ng->level;
    int parity = ((lvl >> 15) ^ (lvl >> 12) ^ (lvl >> 11) ^ (lvl >> 10) ^ 1) & 1;

    // Line 4: IF Timer<0 THEN NoiseLevel = NoiseLevel*2 + ParityBit
    if (ng->timer < 0) {
        ng->level = (int16_t)((uint16_t)(lvl << 1) | (uint16_t)parity);
    }

    // Line 5: IF Timer<0 THEN Timer = Timer + (20000h SHR NoiseShift)
    if (ng->timer < 0) {
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }

    // Line 6: IF Timer<0 THEN Timer = Timer + (20000h SHR NoiseShift)
    // Double-reload: does NOT re-shift NoiseLevel, only reloads timer
    if (ng->timer < 0) {
        ng->timer += (int32_t)(0x20000u >> ng->shift);
    }
}
```

### Mixer Integration Point

```c
// [ASSUMED] -- integration approach, follows existing mixer pattern
void spu94_voice_mixer_tick(spu94_voice_mixer_t *m, ...) {
    // ... existing pending KON/KOFF application ...

    // NON-08: tick noise generator ONCE before voice loop
    spu94_noise_gen_tick(&m->noise_gen);

    for (int v = 0; v < 24; v++) {
        if (!m->voices[v].active) continue;

        // ... existing PMON logic ...

        // Determine if this voice uses noise
        uint8_t non_enabled = (m->non_flags & (1u << v)) ? 1 : 0;

        int16_t vl = 0, vr = 0;
        spu94_voice_tick(&m->voices[v],
                         m->voice_ram, SPU94_SPU_RAM_BYTES,
                         m->gauss_bypass,
                         m->noise_gen.level,  // NON-05: same value for all voices
                         non_enabled,
                         &vl, &vr);

        // Restore pitch (PMON), accumulate dry/rev -- unchanged
    }
}
```

### Frequency Control API

```c
// [ASSUMED] -- API design, follows existing mixer set_pmon / set_eon pattern
spu94_result_t spu94_voice_mixer_set_non(spu94_voice_mixer_t *m,
                                          int voice_idx, int enabled) {
    if (m == NULL || voice_idx < 0 || voice_idx >= 24)
        return SPU94_INVALID_ARG;
    if (enabled)
        m->non_flags |= (1u << voice_idx);
    else
        m->non_flags &= ~(1u << voice_idx);
    return SPU94_OK;
}

spu94_result_t spu94_voice_mixer_set_noise_freq(spu94_voice_mixer_t *m,
                                                 uint8_t shift, uint8_t step_raw) {
    if (m == NULL || shift > 15 || step_raw > 3)
        return SPU94_INVALID_ARG;
    m->noise_gen.shift = shift;
    m->noise_gen.step = step_raw + 4;  // SPUCNT[9:8] + 4 = 4..7
    return SPU94_OK;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Per-voice noise frequency | Single global noise generator (PS1 hardware) | Always (PS1 design) | All NON voices share one pitch -- no workaround in hardware |
| Lookup table LFSR (DuckStation) | Direct XNOR computation (nocash) | Both valid | Mathematically equivalent; direct computation is clearer for our codebase |
| Unsigned timer | Signed timer (int32_t) | Emulator consensus | Required for `timer < 0` underflow detection |

**Deprecated/outdated:**
- Early emulators used simple white noise (random number) instead of the LFSR. This produces wrong spectral characteristics and wrong sequence determinism.

## LFSR Equivalence Verification

The DuckStation `noise_wave_add[64]` lookup table was verified to be mathematically equivalent to the nocash XNOR formula. For all 64 possible combinations of bits [15:10], the table entry matches `bit15 ^ bit12 ^ bit11 ^ bit10 ^ 1`. [VERIFIED: python3 exhaustive comparison during this research session]

We use the direct XNOR computation (not the lookup table) because:
1. It maps one-to-one to the nocash pseudocode (our primary spec reference)
2. It is self-documenting: the tap positions are visible in the code
3. It costs 4 XORs + 1 AND per tick at 44.1 kHz -- negligible

## Noise Seed Verification

Initial seed = 1, confirmed by two independent sources:
- DuckStation `spu.cpp` Reset(): `s_state.noise_level = 1;` [CITED: github.com/stenzek/duckstation spu.cpp]
- nocash does not explicitly document the initial seed, but seed=1 is the emulator consensus and produces a maximal-length sequence with this polynomial. [ASSUMED: seed=1 is consensus but nocash doesn't specify]

Seed = 0 would be absorbing (all-zero LFSR never changes state). Seed must be nonzero.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Initial LFSR seed = 1 | Noise Seed Verification | Low -- any nonzero seed produces valid noise; 1 matches DuckStation and is the emulator consensus |
| A2 | voice_tick signature change (add noise_level + non_enabled params) | Pattern 3 | Low -- API design choice; alternative is passing via struct field, but param is cleaner |
| A3 | Noise generator ticks before voice loop (not after or interleaved) | Pattern 2 / NON-08 | Medium -- if hardware interleaves noise ticks with voices, frequency behavior would differ at extreme rates |

## Open Questions

1. **Pitch counter behavior during NON**
   - What we know: Hardware advances the pitch counter regardless of NON. This is confirmed by DuckStation's implementation which does not skip counter advancement.
   - What's unclear: Does the Gaussian ring still get samples pushed (from ADPCM decode), or does the pitch counter run "dry" (advancing but not consuming)?
   - Recommendation: Keep the full pipeline running (decode, push to ring, advance counter). The ring contents are unused for output when NON=1, but they stay warm for when NON is disabled.

2. **NoiseLevel width: 16-bit signed or unsigned internally?**
   - What we know: nocash says "NoiseLevel*2+ParityBit" which is a left shift + insert. DuckStation stores it as `u32` but only uses the low 16 bits.
   - What's unclear: Whether intermediate values during shift should be clamped to 16 bits or whether wrapping is sufficient.
   - Recommendation: Store as `int16_t`. The left shift wraps naturally at 16 bits via the `uint16_t` cast before shifting, then cast back to `int16_t` for the signed output value. This is the simplest approach and matches the hardware behavior (16-bit register wraps).

## Project Constraints (from CLAUDE.md)

No project-level CLAUDE.md exists. Global constraints from user memory apply:
- Hardware-faithful is default path; no smoothing or "textbook-DSP corrections" without flagging as departures
- No naive implementations; build DSP correctly from the start
- One change at a time in builds; commit after each verified change
- Fixed-point quirks ARE the product
- Aliasing and lo-fi artifacts are creative textures, not defects

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored, C) |
| Config file | tests/unit/voice/CMakeLists.txt |
| Quick run command | `cd build_test && ctest -R noise --output-on-failure` |
| Full suite command | `cd build_test && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| NON-01 | LFSR polynomial + seed=1 produce deterministic sequence | unit | `ctest -R noise_unit --output-on-failure` | No -- Wave 0 |
| NON-02 | Timer decrement + double-reload at extreme shift/step | unit | `ctest -R noise_unit --output-on-failure` | No -- Wave 0 |
| NON-03 | Noise frequency varies with shift/step, not voice pitch | unit | `ctest -R noise_unit --output-on-failure` | No -- Wave 0 |
| NON-04 | NON bitmask selects noise vs ADPCM per voice | integration | `ctest -R voice_tick_unit --output-on-failure` | No -- Wave 0 |
| NON-05 | Two NON voices produce identical output samples | integration | `ctest -R voice_tick_unit --output-on-failure` | No -- Wave 0 |
| NON-06 | ADPCM decode runs for NON voices (ENDX fires) | integration | `ctest -R voice_tick_unit --output-on-failure` | No -- Wave 0 |
| NON-07 | ADSR shapes noise (noise * adsr_level) | integration | `ctest -R voice_tick_unit --output-on-failure` | No -- Wave 0 |
| NON-08 | Noise ticks once globally, not per voice | unit | `ctest -R noise_unit --output-on-failure` | No -- Wave 0 |
| NON-09 | ADR documents seed, polynomial, ADPCM-during-NON | manual-only | Review ADR-0058 in docs/DECISIONS.md | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build_test && ctest -R "noise_unit|voice_tick_unit" --output-on-failure`
- **Per wave merge:** `cd build_test && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/voice/test_noise_gen.c` -- covers NON-01, NON-02, NON-03, NON-08
- [ ] `tests/unit/voice/CMakeLists.txt` -- add test_noise_gen target
- [ ] Additional NON tests in `test_voice_tick.c` -- covers NON-04, NON-05, NON-06, NON-07

## Sources

### Primary (HIGH confidence)
- [nocash psx-spx: SPU Noise Generator](https://problemkaputt.de/psxspx-spu-noise-generator.htm) -- complete LFSR algorithm pseudocode, timer mechanism, frequency control
- [psx-spx consoledev: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- register addresses (1F801D94h NON, 1F801DAAh SPUCNT), bit positions
- [DuckStation spu.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp) -- reference implementation, seed=1 confirmation, noise_wave_add table equivalence verification

### Secondary (MEDIUM confidence)
- ARCHITECTURE-v1.9.md -- voice pipeline integration points, data flow diagrams, struct layout recommendations
- PITFALLS-v1.9.md -- timer sign (Pitfall 5), double-reload (Pitfall 6), DAC noise confusion (Pitfall 2)
- FEATURES-v1.9.md -- NON independence from other v1.9 features, build order rationale

### Tertiary (LOW confidence)
- [PCSX2 SPU2 noise PR #4134](https://github.com/PCSX2/pcsx2/pull/4134) -- confirms Dr. Hell's research is emulator consensus; SPU2 uses same algorithm

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no external dependencies, pure C core
- Architecture: HIGH -- integration points clearly defined by existing voice pipeline; minimal changes needed
- LFSR algorithm: HIGH -- cross-verified nocash pseudocode against DuckStation lookup table; mathematically equivalent
- Timer mechanism: HIGH -- nocash pseudocode is explicit; double-reload confirmed in multiple sources
- Pitfalls: HIGH -- all 6 pitfalls documented with concrete detection strategies

**Research date:** 2026-05-22
**Valid until:** indefinite -- PS1 hardware spec is fixed; no version drift
