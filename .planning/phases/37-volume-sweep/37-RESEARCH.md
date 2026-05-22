# Phase 37: Volume Sweep - Research

**Researched:** 2026-05-22
**Domain:** PS1 SPU per-voice volume sweep (hardware-driven volume ramp)
**Confidence:** HIGH (cross-verified: nocash psx-spx, DuckStation source, existing ADSR implementation)

## Summary

Volume sweep adds a second independent envelope per voice that modifies `vol_l` and `vol_r` directly, creating hardware-driven volume ramps, auto-panning, and fade effects without CPU intervention. The sweep uses the identical counter-accumulate mechanism already implemented in `spu94_adsr.c` -- same formulas, same timing, same bit-15-trigger logic -- but operates on volume registers instead of ADSR level.

The core engineering work is: (1) extract the counter-accumulate step math from `spu94_adsr_tick()` into a shared helper `spu94_envelope_step()`, (2) verify ADSR golden files remain bit-identical after refactor, (3) implement `spu94_sweep_t` using that helper, (4) wire sweep tick into the voice pipeline before volume multiply. The negative-phase sweep (SWEEP-09/10) is LOW confidence from the spec side -- nocash says "not yet tested" and DuckStation marks it "TODO: needs hardware test" -- so it needs an ADR documenting uncertainty.

**Primary recommendation:** Refactor ADSR stepping core into shared helper first (with golden-file regression gate), then build sweep on top. Two plans: Plan 01 = refactor + sweep implementation + tests; Plan 02 = ADR for negative-phase sweep.

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Sweep state machine (counter-accumulate) | C core (DSP) | -- | Pure fixed-point math, RT-safe, same tier as ADSR |
| Shared envelope step helper | C core (DSP) | -- | Factored from existing ADSR code, called by both |
| Sweep parameter API | Mixer API | -- | Follows existing set_pmon/set_non pattern |
| Per-voice sweep state storage | Voice struct | -- | Sweep state travels with voice (same as ADSR) |
| Sweep trigger/reset on volume write | Mixer API | -- | Equivalent to DuckStation's register write handler |

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SWEEP-01 | Per-voice L/R independent sweep state machines | Two `spu94_sweep_t` per voice (sweep_l, sweep_r), each with own counter/level/params |
| SWEEP-02 | Sweep modes: linear inc, linear dec, exp inc (fake >0x6000), exp dec (proportional) | Shared helper handles all 4 modes via direction + exponential flags |
| SWEEP-03 | Step values: increase +7,+6,+5,+4; decrease -8,-7,-6,-5 | `base = 7 - step`; decrease uses `~base` (bitwise NOT); verified identical to `-(8 - step)` |
| SWEEP-04 | Counter-accumulate mechanism identical to ADSR | Shared `spu94_envelope_step()` helper called by both ADSR and sweep |
| SWEEP-05 | Sweep modifies vol_l/vol_r directly (IS the working state) | `sweep.level` IS `vol_l`/`vol_r`; no separate multiplier |
| SWEEP-06 | Sweep and ADSR run concurrently as independent envelopes | ADSR shapes amplitude; sweep shapes spatial position; both multiply into signal chain |
| SWEEP-07 | KON resets sweep state; KOFF does not affect sweep | KON via `key_on()` sets fixed volume (deactivates sweep); KOFF only affects ADSR |
| SWEEP-08 | Anti-stall guard for exponential decrease near zero | `if (scaled_step == 0 && level > 0) scaled_step = -1` -- already in ADSR decay/release |
| SWEEP-09 | Negative-phase sweep mode (Phase bit = 1) | LOW confidence; DuckStation has TODO; nocash says "not yet tested"; needs ADR |
| SWEEP-10 | ADR documenting negative-phase behavior and spec uncertainty | Document what's known, what's uncertain, SPU-94's chosen interpretation |
</phase_requirements>

## Standard Stack

### Core (no new external dependencies)

This phase adds no new libraries. All work is in the existing C core using existing patterns.

| Component | File | Purpose | Pattern Source |
|-----------|------|---------|----------------|
| `spu94_sweep_t` | `spu94_sweep.h` / `spu94_sweep.c` | Per-voice volume sweep envelope | New module, follows `spu94_noise.h` pattern |
| `spu94_envelope_step()` | `spu94_envelope_step.h` | Shared counter-accumulate helper | Extracted from `spu94_adsr.c` |

### Supporting (existing, no changes)

| Component | File | Used For |
|-----------|------|----------|
| `spu94_q15.h` | Q15 fixed-point helpers | `q15_mul_truncate` for exponential scaling |
| `spu94_adsr.h/c` | ADSR envelope | Refactored to use shared helper |
| Unity test framework | `tests/unit/vendor/Unity/` | Unit testing |

## Architecture Patterns

### System Architecture Diagram

```
Voice Volume Register Write (bit15 = 0 or 1)
    |
    +--> bit15=0: Fixed Volume Mode
    |      vol_l/vol_r = value * 2
    |      sweep.active = false
    |
    +--> bit15=1: Sweep Mode
           Configure sweep params (mode, dir, phase, shift, step)
           sweep.active = true
           sweep.level = current vol_l or vol_r (unchanged)
                |
                v
    Each Tick (44.1 kHz):
        +-----> spu94_envelope_step()   <--- shared helper
        |       counter += increment
        |       if bit15: level += step (linear or exp-scaled)
        |       clamp to boundary
        |       return new level
        |
        +--> vol_l = sweep_l.level   (replaces static volume)
        +--> vol_r = sweep_r.level
                |
                v
    Voice Tick Pipeline:
        STEP 0:  Sweep tick (update vol_l, vol_r)        <-- NEW
        STEP 1:  ADPCM decode (unchanged)
        STEP 2:  Gaussian interpolation / NON (unchanged)
        STEP 2.5: ADSR envelope tick (uses same helper)
        STEP 2.75: Store VxOUTX (unchanged)
        STEP 3:  Volume multiply: out = gauss * vol_l/r   <-- uses swept volume
        STEP 4:  Advance pitch counter (unchanged)
```

### Shared Envelope Step Helper

The critical architectural decision: extract the counter-accumulate math from `spu94_adsr_tick()` into a reusable helper. Both ADSR and sweep call the same function, making divergence impossible by construction.

**Existing ADSR pattern (to be refactored):**
```c
// Currently inline in each ADSR phase case:
int shift_amt = adsr_max(0, (int)shift - 11);
counter_increment = (uint32_t)0x8000 >> shift_amt;
int step_shift = adsr_max(0, 11 - (int)shift);
step = base_step << step_shift;
// ... exponential scaling, counter accumulate, bit-15 check
```

**Proposed shared helper signature:**
```c
// [ASSUMED] -- API design is Claude's discretion; exact signature TBD at implementation
typedef struct {
    int16_t  level;     // current envelope level
    uint32_t counter;   // bit-15 accumulator
} spu94_envelope_state_t;

// Returns 1 if step was applied this tick, 0 if not.
// Modifies state->level and state->counter in place.
int spu94_envelope_step(
    spu94_envelope_state_t *state,
    uint8_t  shift,          // 0..31 (sweep) or phase-specific (ADSR)
    uint8_t  step_index,     // 0..3 (maps to +7/+6/+5/+4 or -8/-7/-6/-5)
    uint8_t  decrease,       // 0=increase, 1=decrease
    uint8_t  exponential,    // 0=linear, 1=exponential
    uint8_t  phase_negative  // 0=positive, 1=negative (sweep only; ADSR always 0)
);
```

### Sweep State Struct

```c
// [CITED: nocash psxspx-spu-volume-and-adsr-generator.htm]
// [CITED: DuckStation spu.cpp VolumeSweep struct]
typedef struct {
    int16_t  level;       // current volume level (-0x8000..+0x7FFF)
    uint32_t counter;     // bit-15 accumulator (same as ADSR)
    uint8_t  shift;       // 0..31 from register bits 6-2
    uint8_t  step;        // 0..3 from register bits 1-0
    uint8_t  mode;        // 0=linear, 1=exponential (bit 14)
    uint8_t  direction;   // 0=increase, 1=decrease (bit 13)
    uint8_t  phase;       // 0=positive, 1=negative (bit 12)
    uint8_t  active;      // 1=sweeping, 0=fixed volume
} spu94_sweep_t;
```

### Recommended Project Structure (new/modified files)

```
include/spu94/
    spu94_envelope_step.h    # NEW: shared counter-accumulate helper
    spu94_sweep.h            # NEW: sweep state struct + tick API
    spu94_adsr.h             # UNCHANGED (public API preserved)
    spu94_voice.h            # MODIFIED: add sweep_l/sweep_r to voice_t; add sweep API

src/spu94/
    spu94_envelope_step.c    # NEW: shared helper implementation (~40 LOC)
    spu94_sweep.c            # NEW: sweep init/tick/reset (~60 LOC)
    spu94_adsr.c             # MODIFIED: refactor to call shared helper
    spu94_voice.c            # MODIFIED: tick sweep before volume; add API functions

tests/unit/voice/
    test_sweep.c             # NEW: sweep-specific unit tests
    test_adsr.c              # EXISTING: must still pass (golden-file regression)
    test_voice_tick.c        # MODIFIED: add sweep integration tests
```

### Anti-Patterns to Avoid

- **Copy-paste ADSR stepping for sweep:** Produces divergence over time. Use the shared helper.
- **Sweep as separate multiplier:** Sweep IS the volume register, not a modifier. `vol_l = sweep_l.level` directly, not `vol_l = base_vol * sweep_factor`.
- **Resetting sweep on KOFF:** Only ADSR transitions on KOFF. Sweep is unaffected. [CITED: DuckStation spu.cpp Voice::KeyOff -- no sweep references]
- **Treating negative-phase as well-understood:** It is LOW confidence. Implement it but document uncertainty with ADR.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Counter-accumulate stepping | Separate implementations for ADSR and sweep | Shared `spu94_envelope_step()` | Same hardware circuit, must be bit-identical; divergence is the #1 pitfall |
| Exponential decrease scaling | Custom exponential curve | `step = step * level / 0x8000` (the PS1's formula) | The PS1 uses proportional decrease, not a real exponential -- it's simpler |
| Fake exponential increase | True exponential function | `if (level > 0x6000) counter_increment >>= 2` | PS1 fakes it with a rate change above a threshold; match this exactly |

## Common Pitfalls

### Pitfall 1: Counter Mechanism Diverges Between ADSR and Sweep
**What goes wrong:** Copy-pasting the counter-accumulate code from ADSR into sweep with subtle differences (different bit widths, different clamping, different shift formulas).
**Why it happens:** Expedience. "I'll just copy the ADSR code and change the parts that differ."
**How to avoid:** Extract the shared helper FIRST. Verify ADSR golden files pass with the refactored code. Then build sweep on top.
**Warning signs:** Any test that checks ADSR timing starts failing after refactor.

### Pitfall 2: Phase Bit Clamping Wrong
**What goes wrong:** With phase=1 (negative), increase goes toward -0x7FFF and decrease goes toward 0. Without phase awareness, clamping assumes positive volumes only and clips incorrectly.
**Why it happens:** The phase bit inverts what "maximum" and "minimum" mean.
**How to avoid:** Explicit per-quadrant clamping: [CITED: nocash psxspx-spu-volume-and-adsr-generator.htm]
- Positive increase: clamp at +0x7FFF
- Positive decrease: clamp at 0x0000
- Negative increase: clamp at -0x7FFF (move away from zero, deeper negative)
- Negative decrease: clamp at 0x0000 (move toward zero)
**Warning signs:** Sweep in negative mode produces unexpected positive values or wraps around.

### Pitfall 3: Phase Bit in Exponential Decrease Mode
**What goes wrong:** Applying phase inversion in exponential decrease mode, where the spec says it "seems to have no effect."
**Why it happens:** Implementing phase bit uniformly across all modes without checking the exception.
**How to avoid:** DuckStation's approach: `phase_invert = phase_invert_ && !(decreasing_ && exponential_)` -- phase bit is ignored when direction=decrease AND mode=exponential. [CITED: DuckStation spu.cpp VolumeEnvelope::Reset]
**Warning signs:** Exponential decrease with phase=1 produces positive steps (because negative * negative = positive).

### Pitfall 4: Sweep Starting Volume Race Condition
**What goes wrong:** Setting sweep parameters (bit15=1) before the fixed volume (bit15=0) has been latched. The sweep starts from the OLD volume, not the intended initial volume.
**Why it happens:** nocash warns: "the Bit15=0 setting isn't applied until the next 44.1kHz cycle." [CITED: nocash psxspx-spu-volume-and-adsr-generator.htm]
**How to avoid:** In SPU-94's API, provide a single function that sets initial volume AND activates sweep atomically. Or document that callers should set volume via `key_on` or `set_volume` first, then configure sweep on the next tick.
**Warning signs:** Sweep starting from an unexpected level.

### Pitfall 5: Anti-Stall Guard Missing
**What goes wrong:** Exponential decrease near zero produces `scaled_step = 0` (because `step * very_small_level / 0x8000` rounds to zero), and the volume gets stuck above zero forever.
**Why it happens:** Integer math with small numbers rounds to zero.
**How to avoid:** Guard: `if (scaled_step == 0 && level > 0) scaled_step = -1`. This is already in ADSR decay and release -- the shared helper carries it forward.
**Warning signs:** Volume hangs at a tiny non-zero value and never reaches silence.

## Code Examples

### Shared Envelope Step (core algorithm)
```c
// [CITED: nocash psxspx-spu-volume-and-adsr-generator.htm]
// [CITED: DuckStation spu.cpp VolumeEnvelope::Reset + Tick]
//
// Counter-accumulate mechanism shared by ADSR and Volume Sweep:
//   CounterIncrement = 0x8000 >> max(0, shift - 11)
//   BaseStep = (7 - step_index) << max(0, 11 - shift)
//   For decrease: step = ~base_step (bitwise NOT = -(base+1))
//   For exponential decrease: step = step * level / 0x8000
//   For exponential increase above 0x6000: counter_increment >>= 2
//   counter += counter_increment
//   if (counter & 0x8000): counter &= ~0x8000; level += step; clamp

// DuckStation step formula equivalence proof:
//   ~(7 - step_index) == -(8 - step_index)
//   step_index=0: ~7 = -8; -(8-0) = -8  -- identical
//   step_index=1: ~6 = -7; -(8-1) = -7  -- identical
//   step_index=2: ~5 = -6; -(8-2) = -6  -- identical
//   step_index=3: ~4 = -5; -(8-3) = -5  -- identical
```

### Sweep Tick in Voice Pipeline
```c
// [CITED: DuckStation spu.cpp lines 2143-2146]
// DuckStation applies sweep volume then ticks:
//   left  = ApplyVolume(adsr_output, voice.left_volume.current_level);
//   right = ApplyVolume(adsr_output, voice.right_volume.current_level);
//   voice.left_volume.Tick();
//   voice.right_volume.Tick();
//
// SPU-94 equivalent in voice_tick:
//   STEP 0: tick sweep_l and sweep_r (updates level)
//   STEP 3: vol_l = sweep_l.active ? sweep_l.level : v->vol_l;
//           out_l = q15_mul_truncate(adsr_output, vol_l);
```

### KON Reset Behavior
```c
// [CITED: DuckStation spu.cpp Voice::KeyOn -- sweep NOT reset]
// KON does not explicitly reset sweep. Volume register writes do.
// In SPU-94's API, key_on() sets vol_l/vol_r (equivalent to bit15=0 write).
// This implicitly deactivates sweep and sets a new fixed level.
// Sweep must be re-configured after key_on via set_sweep_l/set_sweep_r.
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| ADSR and sweep as separate code paths | Shared `spu94_envelope_step()` helper | This phase | Eliminates divergence risk; both use identical math |
| KON explicitly resets sweep | KON sets fixed volume (implicitly deactivates sweep) | This phase research | Matches DuckStation; sweep requires explicit re-activation after KON |

**Key DuckStation finding:** Sweep state is NOT reset on KeyOn. It is only affected by writing to the volume register. SPU-94's `key_on()` sets vol_l/vol_r directly, which is equivalent to a bit15=0 volume register write -- so sweep should be deactivated on KON, but only as a side effect of setting fixed volume, not as an explicit sweep reset step. [CITED: DuckStation spu.cpp Voice::KeyOn lines 1673-1690]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Shared helper signature design (spu94_envelope_step params and return type) | Architecture Patterns | Low -- API detail, can adjust during implementation |
| A2 | Negative-phase sweep: increase toward -0x7FFF, decrease toward 0 | Pitfall 2, SWEEP-09 | Medium -- nocash says "not yet tested"; DuckStation has TODO. ADR required |
| A3 | Phase bit has no effect in exponential decrease mode | Pitfall 3, SWEEP-09 | Medium -- DuckStation implements this but marks it TODO for hardware test |
| A4 | KON deactivates sweep as side effect of setting fixed volume | State of the Art, SWEEP-07 | Low -- matches DuckStation behavior; SPU-94 API makes this natural |

## Open Questions

1. **Negative-phase sweep behavior (SWEEP-09)**
   - What we know: nocash says phase bit "seems to have no effect in Exponential Decrease mode" and that negative mode "does probably increase to -7FFFh" (hedged language: "not yet tested"). DuckStation disables phase_invert for exp+decrease combos but marks it TODO.
   - What's unclear: Exact clamping boundaries for all mode+direction+phase combinations in hardware. No authoritative hardware test exists.
   - Recommendation: Implement DuckStation's interpretation (ignore phase in exp decrease; otherwise invert step sign). Document as LOW confidence in ADR-0059. If hardware tests emerge later, adjust.

2. **Sweep tick timing relative to volume apply**
   - What we know: DuckStation applies volume FIRST, then ticks sweep (meaning the tick's updated level is used on the NEXT sample). SPU-94 could tick first (level used THIS sample) or after (level used NEXT sample).
   - What's unclear: Which is hardware-correct.
   - Recommendation: Tick sweep at STEP 0 (before volume apply). The one-sample difference is inaudible. DuckStation's order is apply-then-tick, meaning the sweep level from the previous tick is what gets used for volume. Either way works; document the choice.

3. **Sweep and KON interaction in SPU-94's API**
   - What we know: DuckStation does NOT reset sweep on KON. SPU-94's `key_on()` already sets vol_l/vol_r, effectively writing a fixed volume.
   - What's unclear: Should `key_on()` explicitly set `sweep_l.active = 0` and `sweep_r.active = 0`?
   - Recommendation: Yes. `key_on()` sets a fixed volume, so sweep should be deactivated. This is consistent with the PS1 behavior where writing bit15=0 to the volume register deactivates sweep.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C, already integrated) |
| Config file | tests/unit/voice/CMakeLists.txt |
| Quick run command | `cd build && ctest -R sweep_unit --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SWEEP-01 | Independent L/R sweep | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-02 | 4 sweep modes (lin/exp x inc/dec) | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-03 | Step values +7/+6/+5/+4 and -8/-7/-6/-5 | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-04 | Counter-accumulate identical to ADSR | unit + regression | `ctest -R adsr_unit` (regression) | Exists (adsr) |
| SWEEP-05 | Sweep modifies vol directly | integration | `ctest -R voice_tick_unit` | No -- Wave 0 |
| SWEEP-06 | Sweep + ADSR concurrent | integration | `ctest -R voice_tick_unit` | No -- Wave 0 |
| SWEEP-07 | KON deactivates sweep; KOFF does not | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-08 | Anti-stall guard | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-09 | Negative-phase sweep | unit | `ctest -R sweep_unit` | No -- Wave 0 |
| SWEEP-10 | ADR document | docs | manual review | No -- Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && ctest -R "sweep_unit|adsr_unit|voice_tick_unit" --output-on-failure`
- **Per wave merge:** `cd build && ctest --output-on-failure`
- **Phase gate:** Full suite green before verification

### Wave 0 Gaps
- [ ] `tests/unit/voice/test_sweep.c` -- sweep-specific unit tests (SWEEP-01..09)
- [ ] `tests/unit/voice/CMakeLists.txt` -- add test_sweep target
- [ ] Integration tests in `test_voice_tick.c` for SWEEP-05, SWEEP-06

## Sources

### Primary (HIGH confidence)
- [nocash psx-spx: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) -- sweep register format, counter-accumulate mechanism, phase bit description, step formulas
- [psx-spx consoledev: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- register layout, sweep behavior description
- [DuckStation spu.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp) -- VolumeEnvelope::Reset (line 1732), VolumeEnvelope::Tick (line 1766), VolumeSweep::Reset (line 1825), VolumeSweep::Tick (line 1838), Voice::KeyOn (line 1673)
- Existing `spu94_adsr.c` -- working counter-accumulate implementation, basis for shared helper

### Secondary (MEDIUM confidence)
- [hitmen SPU documentation](https://hitmen.c02.at/files/docs/psx/spu.txt) -- volume register format, phase inversion bit
- ARCHITECTURE-v1.9.md -- sweep struct design, pipeline integration point
- PITFALLS-v1.9.md -- pitfalls 3 (counter divergence), 8 (phase bit clamping), 9 (race condition)

### Tertiary (LOW confidence)
- nocash statement "Sweep Phase ... not yet tested" -- negative-phase behavior is unverified by hardware testing
- DuckStation TODO comment "This needs to be tested on hardware" -- phase bit in exponential decrease

## Metadata

**Confidence breakdown:**
- Shared envelope helper design: HIGH -- counter-accumulate is well-documented and already working in ADSR
- Sweep modes (linear/exp, inc/dec): HIGH -- verified across nocash, DuckStation, existing ADSR code
- Step formula: HIGH -- algebraically verified that `~(7-step)` == `-(8-step)`
- Phase bit behavior: LOW -- nocash hedges, DuckStation has TODO, no hardware confirmation
- KON/sweep interaction: MEDIUM -- DuckStation does not reset sweep on KON; SPU-94 API makes deactivation natural

**Research date:** 2026-05-22
**Valid until:** 2026-07-22 (stable domain; PS1 hardware behavior does not change)
