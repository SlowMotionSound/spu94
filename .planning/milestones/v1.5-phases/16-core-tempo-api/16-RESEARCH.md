# Phase 16: Core Tempo API - Research

**Researched:** 2026-05-02
**Domain:** Integer arithmetic tempo computation, per-register binding state, circular buffer delay computation
**Confidence:** HIGH

## Summary

Phase 16 adds BPM state, a compile-time subdivision lookup table, and delay-register snapping to the SPU-94 C core. The core arithmetic is straightforward: `samples = (60 * 22050 * numerator) / (bpm * denominator)` computed entirely in uint32 with integer truncation. All intermediate values fit comfortably within uint32 (max numerator product is 3,969,000). The existing `spu94_set_reg_u16` pathway provides the write mechanism for snapped values, and the TICK_LATCHED policy already handles atomic staging.

The principal design challenge is the virtual comb delay computation. Analysis of the reverb body shows that mCOMB addresses represent **offsets from buffer_address** -- when the IIR writes at offset mLSAME and the comb reads at offset mLCOMB, the effective delay is `mLSAME - mLCOMB` halfwords (for mLCOMB < mLSAME). The virtual comb delay therefore computes `mCOMB = mLSAME_or_mLDIFF - desired_delay_samples`, with validity constrained by the current reference write offset.

The per-register binding state (grid-bound / proportional / fixed) adds approximately 44 bytes to `spu94_state` -- well within the 15,144 bytes of remaining headroom before `SPU94_STATE_SIZE_MAX`.

**Primary recommendation:** Implement as a single new translation unit (`spu94_tempo.c`) with a public header section appended to `spu94.h`, following the established boolean-toggle + error-return patterns.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: 10 registers total -- 6 d-prefix (dAPF1, dAPF2, dLSAME, dRSAME, dLDIFF, dRDIFF) + 4 virtual comb-delay targets (dCOMB1-4) that internally compute mCOMB addresses
- D-02: Two independent group toggles: "reflection sync" (6 d-prefix) and "comb sync" (4 virtual comb delays)
- D-03: d-prefix = rhythmic echo snapping; virtual comb = reverb tail resonance tuning
- D-04: Persistent binding -- changing BPM auto-resnaps all grid-bound registers
- D-05: Three register states: grid-bound, proportional, fully-fixed
- D-06: Manual write to grid-bound register transitions it to proportional state
- D-07: Re-binding a proportional or fixed register returns it to grid-bound
- D-08: Invalid BPM/subdivision combinations rejected -- register unchanged, error returned
- D-09: Query function `spu94_subdivision_valid(bpm, subdivision)` for pre-check
- D-10: All useful subdivisions available at realistic BPMs (40-300)
- D-11: Pure integer math -- `samples = (60 * 22050 * sub_numerator) / (bpm * sub_denominator)`
- D-12: Integer truncation (C default) -- PS1 MIPS R3000A integer behavior
- D-13: 15 subdivision entries as rational fractions in compile-time lookup table

### Claude's Discretion
- Exact virtual comb-delay computation formula
- Internal struct layout for per-register binding state
- Error code choice for overflow rejection (SPU94_INVALID_ARG vs new code)
- Whether proportional state stores BPM-at-write or ratio directly

### Deferred Ideas (OUT OF SCOPE)
- Groove-feel offset (lever layer)
- DAW host tempo sync (plugin milestone v1.6)
- Tempo-modulated delays with crossfade/interpolation (lever layer)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| TEMPO-01 | `spu94_set_tempo` stores BPM value in engine state | Struct field layout, API pattern from existing set/get pairs |
| TEMPO-02 | `spu94_set_subdivision` snaps a delay register to nearest musical subdivision at current BPM | Integer arithmetic formula verified, overflow analysis complete, virtual comb computation derived |
| TEMPO-03 | Supported subdivisions: 1/1 through 1/16, dotted and triplet variants (15 total) | Rational fraction table verified, all valid at BPM 40-300 except 1/1 dotted at BPM < 31 |
| TEMPO-04 | Conversion formula uses 22,050 Hz sample rate | Formula verified: `(60 * 22050 * num) / (bpm * den)`, uint32 safe |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| BPM state storage | C core (spu94_state) | -- | All engine state lives in the opaque state struct |
| Subdivision→samples conversion | C core (spu94_tempo.c) | -- | Pure integer math, no external dependencies |
| Per-register binding tracking | C core (spu94_state) | -- | State that persists across ticks, part of engine internals |
| Virtual comb→mCOMB computation | C core (spu94_tempo.c) | -- | Reads mLSAME/mLDIFF from state, computes offset |
| Group toggle management | C core (spu94_state) | -- | Boolean flags with set/get API surface |
| Write-interception for state transition | C core (spu94_tempo.c) | -- | Hooks into existing spu94_set_reg_u16 pathway |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C99 freestanding | -- | Implementation language | Project constraint (API-07, API-09) |
| Unity test framework | vendored | Unit testing | Already integrated in tests/unit/ |

### Supporting
| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| None | -- | -- | No external dependencies needed for this phase |

**No new dependencies.** This phase is pure C99 integer arithmetic within the existing build system.

## Architecture Patterns

### System Architecture Diagram

```
              spu94_set_tempo(bpm)
                     |
                     v
        +------------------------+
        |    spu94_state         |
        |  .tempo_bpm = bpm     |
        |  .reflection_sync     |     spu94_set_subdivision(reg, sub_idx)
        |  .comb_sync           |<-----------+
        |  .binding[10]         |            |
        +------------------------+            |
                     |                        |
                     | (BPM changed?)         |
                     v                        v
        +------------------------+   +-------------------+
        | Auto-resnap loop       |   | Compute samples   |
        | for each grid-bound    |   | from BPM + ratio  |
        | register in active     |   +-------------------+
        | sync group             |            |
        +------------------------+            v
                     |               +-------------------+
                     v               | Validity check    |
        +------------------------+   | samples <= 65535  |
        | spu94_set_reg_u16()   |   | (+ comb geometry) |
        | via TICK_LATCHED path  |   +-------------------+
        | (existing mechanism)   |            |
        +------------------------+            v
                                     +-------------------+
                                     | For d-prefix:     |
                                     |   write register  |
                                     | For dCOMB virtual:|
                                     |   compute mCOMB   |
                                     |   = ref - delay   |
                                     |   write mLCOMBn   |
                                     |   write mRCOMBn   |
                                     +-------------------+
```

### Recommended Project Structure
```
include/spu94/
├── spu94.h                      # Tempo API declarations appended here
src/spu94/
├── spu94_tempo.c                # New TU: tempo logic, subdivision table, snap computation
├── spu94_state_internal.h       # Add tempo fields to struct spu94_state
├── spu94_register_io.c          # Add write-interception hook for binding state transition
tests/unit/tempo/
├── test_tempo_basic.c           # BPM set/get, subdivision table
├── test_tempo_snap.c            # Snap computation correctness
├── test_tempo_comb.c            # Virtual comb delay computation
├── test_tempo_binding.c         # Binding state transitions
```

### Pattern 1: Boolean Toggle Pair (from existing API)
**What:** Set/get function pairs for sync group toggles
**When to use:** reflection_sync and comb_sync toggles
**Example:**
```c
// Source: include/spu94/spu94.h (existing pattern: spu94_set_adpcm_enabled)
void     spu94_set_reflection_sync(spu94_state *state, int enabled);
int      spu94_get_reflection_sync(const spu94_state *state);
void     spu94_set_comb_sync(spu94_state *state, int enabled);
int      spu94_get_comb_sync(const spu94_state *state);
```

### Pattern 2: Error-Returning Setter (from existing API)
**What:** Functions that validate arguments and return spu94_result_t
**When to use:** set_tempo, set_subdivision -- both can fail on invalid args
**Example:**
```c
// Source: include/spu94/spu94.h (existing pattern: spu94_load_preset)
spu94_result_t spu94_set_tempo(spu94_state *state, uint16_t bpm);
spu94_result_t spu94_set_subdivision(spu94_state *state,
                                     spu94_tempo_reg_t reg,
                                     spu94_subdivision_t subdivision);
```

### Pattern 3: Compile-Time Lookup Table (from existing presets)
**What:** Static const array of rational fractions for subdivision ratios
**When to use:** Subdivision table -- 15 entries, read-only, .rodata section
**Example:**
```c
// Source: derived from src/spu94/spu94_presets.c pattern (static const table)
typedef struct {
    uint8_t numerator;
    uint8_t denominator;
} spu94_subdivision_ratio_t;

static const spu94_subdivision_ratio_t spu94_subdivision_table[SPU94_SUBDIVISION__COUNT] = {
    [SPU94_SUB_1_1]          = {1,  1},
    [SPU94_SUB_1_1_DOTTED]   = {3,  2},
    [SPU94_SUB_1_1_TRIPLET]  = {2,  3},
    [SPU94_SUB_1_2]          = {1,  2},
    // ... 15 entries total
};
```

### Anti-Patterns to Avoid
- **Float computation even "temporarily":** The formula MUST use integer division throughout. Do not compute as float and cast to int. The truncation behavior of C integer division IS the desired musical character (D-12).
- **Heap allocation for subdivision table:** Use static const. The table is 15 * 2 = 30 bytes.
- **Intercepting writes inside spu94_tick:** The binding-state transition on manual write must happen at write time (in spu94_set_reg_u16 path), not at tick time. Otherwise the user sees stale binding state between write and tick.
- **Modifying reverb_buf_read/write:** The virtual comb feature computes mCOMB addresses and writes them via the existing register API. It does NOT change the reverb body's read/write mechanics.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Register writes | Custom memory poke | `spu94_set_reg_u16()` | Existing engine layer handles TICK_LATCHED staging, mBASE side effects |
| Overflow checking | Manual bit-width analysis at each call site | Centralized validity function | `spu94_subdivision_valid()` checks once, callers trust it |
| Subdivision ratio table | Runtime-computed divisions | Compile-time const array | 15 fixed entries, never changes, .rodata is free |
| Binding state persistence | External bookkeeping | Fields in spu94_state | Everything lives in the opaque state -- same lifetime as engine |

## Common Pitfalls

### Pitfall 1: Integer Overflow in Numerator Product
**What goes wrong:** `60 * 22050 * numerator` computed in int16 or without explicit widening
**Why it happens:** C integer promotion rules can surprise -- `60 * 22050` is 1,323,000 which fits int32 but the compiler might use int if int is 16-bit on embedded targets
**How to avoid:** Use `60u * 22050u * (uint32_t)numerator` -- explicit uint32 widening from the start
**Warning signs:** Compilation warnings about implicit narrowing, unexpected zero results on embedded

### Pitfall 2: Division by Zero on BPM=0
**What goes wrong:** Dividing by `bpm * denominator` when bpm=0 is undefined behavior
**Why it happens:** BPM might be unset (default zero from struct zeroing)
**How to avoid:** Reject bpm=0 at set_tempo time (return SPU94_INVALID_ARG). Also guard at snap computation: if tempo_bpm==0, do not attempt computation
**Warning signs:** Crash on first use before calling set_tempo

### Pitfall 3: Virtual Comb Delay Exceeds Buffer Geometry
**What goes wrong:** Computing `mCOMB = mLSAME - delay` when delay > mLSAME wraps to a large uint16 value, reading stale/uninitialized buffer data
**Why it happens:** The effective delay range is constrained by the current preset's buffer geometry -- Hall preset mLSAME=5562 limits delays to ~252ms
**How to avoid:** Validity check: `delay_samples <= current_reference_offset`. Return error if exceeded.
**Warning signs:** Metallic artifacts or silence from comb outputs when using long subdivisions

### Pitfall 4: Manual Write Interception Race with TICK_LATCHED
**What goes wrong:** A manual write to a d-prefix register goes through spu94_set_reg_u16 which stages to pending. The binding-state transition (grid-bound -> proportional) must happen NOW, but the register value doesn't become active until next tick.
**Why it happens:** The existing TICK_LATCHED path doesn't have a callback mechanism
**How to avoid:** The binding-state transition is based on the WRITE, not the commit. Transition the binding state immediately when spu94_set_reg_u16 is called for a tracked register. The actual register value commits later via the existing path -- that's fine.
**Warning signs:** Binding state appears stale for one tick after manual write

### Pitfall 5: Auto-Resnap Loop Writes Invalid Values
**What goes wrong:** BPM changes, auto-resnap loop recomputes all grid-bound registers, but one register's new value exceeds uint16 at the new BPM
**Why it happens:** A register was bound to 1/1 dotted at BPM 35, valid then. BPM changes to 25, now 1/1 dotted overflows.
**How to avoid:** During auto-resnap: if recomputed value overflows, transition that register to FIXED state (preserving its current sample value) rather than writing an invalid value. Alternatively, silently skip -- the register keeps its last valid snap value.
**Warning signs:** SPU94_INVALID_ARG errors bubbling up from internal auto-resnap (these should be handled, not propagated)

### Pitfall 6: Confusing the 10 Tempo Register IDs with the 35-Register spu94_reg_t Enum
**What goes wrong:** Using SPU94_REG_dAPF1 (value 3) as an index into the 10-element binding array, causing out-of-bounds access
**Why it happens:** Two different register identity spaces coexist -- the 35-register hardware enum and the 10-register tempo subset
**How to avoid:** Define a separate `spu94_tempo_reg_t` enum (0..9) for the tempo API. Provide a mapping function `tempo_reg_to_hw_reg()` internally.
**Warning signs:** Array overflows in binding state, wrong registers getting snapped

## Code Examples

### Tempo Computation (core formula)
```c
// Source: verified by overflow analysis (this research)
static uint32_t spu94_compute_delay_samples(uint16_t bpm,
                                            uint8_t numerator,
                                            uint8_t denominator)
{
    /* All intermediates fit uint32:
     * max numerator product = 60 * 22050 * 3 = 3,969,000
     * max denominator product = 300 * 32 = 9,600
     * Both well within uint32 range. */
    uint32_t num_product = 60u * 22050u * (uint32_t)numerator;
    uint32_t den_product = (uint32_t)bpm * (uint32_t)denominator;
    return num_product / den_product;  /* truncation = PS1 character (D-12) */
}
```

### Virtual Comb Delay Computation
```c
// Source: derived from reverb body analysis (spu94_reverb.c line 390-423)
// mCOMB reads at offset mCOMB from buffer_address.
// IIR writes at offset mLSAME from buffer_address.
// Delay between write and read = mLSAME - mCOMB (when mCOMB < mLSAME).
// Therefore: mCOMB = mLSAME - desired_delay_samples

static spu94_result_t compute_comb_offset(const spu94_state *state,
                                          uint8_t comb_idx,  /* 0-3 */
                                          uint16_t delay_samples,
                                          uint16_t *out_mL, uint16_t *out_mR)
{
    /* Combs 1-2 reference the SAME IIR write position (mLSAME / mRSAME).
     * Combs 3-4 reference the DIFF IIR write position (mLDIFF / mRDIFF). */
    uint16_t ref_L, ref_R;
    if (comb_idx < 2) {
        ref_L = spu94_get_reg_u16(state, SPU94_REG_mLSAME);
        ref_R = spu94_get_reg_u16(state, SPU94_REG_mRSAME);
    } else {
        ref_L = spu94_get_reg_u16(state, SPU94_REG_mLDIFF);
        ref_R = spu94_get_reg_u16(state, SPU94_REG_mRDIFF);
    }

    /* Validity: delay must not exceed reference offset */
    if (delay_samples > ref_L || delay_samples > ref_R) {
        return SPU94_INVALID_ARG;
    }

    *out_mL = ref_L - delay_samples;
    *out_mR = ref_R - delay_samples;
    return SPU94_OK;
}
```

### Subdivision Validity Query
```c
// Source: derived from overflow analysis (this research)
int spu94_subdivision_valid(uint16_t bpm, spu94_subdivision_t subdivision)
{
    if (bpm == 0) return 0;
    if ((int)subdivision < 0 || (int)subdivision >= SPU94_SUBDIVISION__COUNT) return 0;

    const spu94_subdivision_ratio_t *ratio = &spu94_subdivision_table[subdivision];
    uint32_t samples = 60u * 22050u * (uint32_t)ratio->numerator
                     / ((uint32_t)bpm * (uint32_t)ratio->denominator);
    return (samples <= UINT16_MAX) ? 1 : 0;
}
```

### Per-Register Binding State Layout
```c
// Source: design decision per CONTEXT.md Claude's Discretion
typedef enum {
    SPU94_BIND_FIXED       = 0,  /* raw sample count, ignores tempo */
    SPU94_BIND_GRID        = 1,  /* snapped to subdivision, resnaps on BPM change */
    SPU94_BIND_PROPORTIONAL = 2  /* manual value, scales with tempo (not quantized) */
} spu94_binding_state_t;

/* Per-register tempo binding info (10 entries in spu94_state) */
typedef struct {
    uint8_t  state;          /* spu94_binding_state_t */
    uint8_t  subdivision;    /* spu94_subdivision_t index (0xFF = none) */
    uint16_t ref_bpm;        /* BPM at which proportional value was set */
} spu94_tempo_binding_t;
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Float-based tempo computation | Integer-only with rational fractions | PS1 era (1994) | Deterministic, no rounding variance across platforms |
| External tempo management | Engine-internal BPM state | Phase 16 (new) | Self-contained: engine knows its own tempo |
| Raw register manipulation for rhythmic effects | Named subdivision API | Phase 16 (new) | Musical abstractions over hardware registers |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Combs 1-2 read from SAME IIR output, Combs 3-4 from DIFF IIR output | Code Examples (virtual comb) | Comb delay may be computed against wrong reference offset; wrong delay times |
| A2 | Existing `spu94_set_reg_u16` can be extended with a post-write hook without ABI break | Architecture Patterns | May need alternative interception mechanism for binding state transition |
| A3 | SPU94_INVALID_ARG is appropriate for tempo overflow rejection (vs new error code) | Code Examples | Callers relying on specific error code behavior; minor impact |

**A1 justification:** Derived from Hall preset register ordering: mLCOMB1/2 offsets are close to mLSAME; mLCOMB3/4 offsets are close to mLDIFF. The pattern `mReference - mCOMBn = small positive delay` holds consistently. The allocation decision (1-2 vs 3-4) matches the CONTEXT.md description of "4 virtual comb-delay targets" but the exact grouping per IIR source should be confirmed by the implementer against other factory presets.

## Open Questions

1. **Proportional state: store BPM or ratio?**
   - What we know: When a user manually writes to a grid-bound register, it transitions to proportional. On BPM change, proportional registers should scale.
   - What's unclear: Store the BPM-at-write (and recompute ratio = value / expected_value_at_that_bpm)? Or store the ratio directly (value / samples_that_would_be_at_current_bpm)?
   - Recommendation: Store `ref_bpm` -- the BPM at which the manual value was written. On BPM change, new_value = old_value * old_bpm / new_bpm. This is simpler, avoids fractional ratio storage, and uses pure integer math.

2. **Write interception mechanism**
   - What we know: D-06 requires that manual writes to a grid-bound register transition it to proportional. The write goes through spu94_set_reg_u16 (TICK_LATCHED path).
   - What's unclear: Best way to add the hook -- modify spu94_set_reg_u16 directly? Add a post-write callback? Separate "tempo-aware" write function?
   - Recommendation: Add a static function call inside spu94_set_reg_u16 (or just after the TICK_LATCHED branch) that checks if the register is in the tempo-tracked set and transitions binding state. Minimal, no ABI change.

3. **Auto-resnap failure policy**
   - What we know: BPM change triggers resnap of all grid-bound registers. Some may overflow at the new BPM.
   - What's unclear: Should overflow during auto-resnap silently preserve the old value? Transition to fixed? Log an internal counter?
   - Recommendation: Transition to FIXED state on overflow during auto-resnap. The register keeps its current value (which was valid at the old BPM). This matches D-08 spirit (invalid combinations rejected) without losing the register's current audio function.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored, C) |
| Config file | tests/unit/CMakeLists.txt |
| Quick run command | `cd build && ctest -R tempo -j$(nproc)` |
| Full suite command | `cd build && ctest -j$(nproc)` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| TEMPO-01 | set_tempo stores BPM, get_tempo retrieves it | unit | `ctest -R test_tempo_basic` | Wave 0 |
| TEMPO-02 | set_subdivision snaps register to correct sample count | unit | `ctest -R test_tempo_snap` | Wave 0 |
| TEMPO-03 | All 15 subdivisions produce mathematically correct results | unit | `ctest -R test_tempo_snap` | Wave 0 |
| TEMPO-04 | Formula uses 22050 Hz (verify specific known values) | unit | `ctest -R test_tempo_snap` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && ctest -R tempo -j$(nproc)`
- **Per wave merge:** `cd build && ctest -j$(nproc)`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/tempo/CMakeLists.txt` -- build config for tempo tests
- [ ] `tests/unit/tempo/test_tempo_basic.c` -- TEMPO-01 coverage
- [ ] `tests/unit/tempo/test_tempo_snap.c` -- TEMPO-02, TEMPO-03, TEMPO-04 coverage
- [ ] `tests/unit/tempo/test_tempo_comb.c` -- virtual comb computation correctness
- [ ] `tests/unit/tempo/test_tempo_binding.c` -- binding state transitions (D-04 through D-07)

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_reverb.c` -- reverb body implementation showing mCOMB read mechanics (lines 390-443)
- `src/spu94/spu94_buffer.c` -- buffer_advance wrap formula confirming ring geometry
- `src/spu94/spu94_register_io.c` -- engine-layer write path with TICK_LATCHED routing
- `src/spu94/spu94_state_internal.h` -- current struct layout, sizeof = 1240 bytes
- `src/spu94/spu94_presets.c` -- Hall preset register values confirming mCOMB < mLSAME relationship
- `include/spu94/spu94.h` -- existing API patterns (toggle pairs, error-returning setters)

### Secondary (MEDIUM confidence)
- Integer overflow analysis performed via Python computation (verified numerator product max = 3,969,000, well within uint32)
- Subdivision validity boundary analysis (only 1/1 dotted overflows uint16 below BPM 31)
- Comb delay direction analysis from Hall preset values (mLCOMB1=5314 < mLSAME=5562, delay=248 samples)

### Tertiary (LOW confidence)
- A1: Comb 1-2 vs 3-4 grouping to SAME vs DIFF IIR output [ASSUMED from preset offset patterns]

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- pure C99, no new dependencies, existing build system
- Architecture: HIGH -- follows established patterns in this codebase verbatim
- Integer arithmetic: HIGH -- verified by computation, all intermediates provably within uint32
- Virtual comb computation: MEDIUM -- direction derived from code analysis + preset data, but A1 assumption needs confirmation
- Pitfalls: HIGH -- derived from direct code reading of existing mechanism

**Research date:** 2026-05-02
**Valid until:** Indefinite (C99 integer arithmetic does not expire)
