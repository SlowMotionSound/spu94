# Phase 20: Macro Engine + Safety Core - Research

**Researched:** 2026-05-03
**Domain:** C core macro engine (proportional scaling, gang clamping, safety constraints)
**Confidence:** HIGH

## Summary

Phase 20 builds a macro engine and safety enforcement layer in the C core (`src/spu94/`). The macro engine coordinates register groups via proportional scaling from current state -- no absolute lookup tables. The safety layer enforces a vIIR x vWALL stability ceiling and m-prefix address bounds checking at the register write level, covering both macro and raw mode writes.

The codebase is mature (1,288 bytes of struct state, 15,096 bytes remaining in the 16,384-byte budget). The tempo system (`spu94_tempo.c`) is the closest architectural analog: per-register tracking arrays, re-entrancy guards for write interception, and state that lives inside `struct spu94_state`. The macro engine follows this pattern but with per-GROUP state instead of per-register state.

**Primary recommendation:** Build the safety layer first (it's independent and must cover both raw and macro writes), then the macro group definition struct and engine, then the re-derivation and gang-clamping logic. Safety hooks attach at the `spu94_set_reg_*` level or wrap it, using the same cross-TU extern-declaration pattern as `spu94_mbase_on_write` and `spu94_tempo_on_reg_write`.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- Macro engine lives in C core (`src/spu94/`) as new files -- not in JUCE, CLI, or Python
- Engine operates on relative scaling from current register state, not absolute lookup tables
- A macro group is a struct defining: which registers belong to the group, their signedness, and their floor/ceiling constraints
- Knob position is a normalized float or Q15 value that maps to the proportional range between floor and ceiling of the most-constrained register in the group
- Gang clamping: when any register in a group hits its ceiling or floor during a sweep, all registers stop together and ratios are preserved
- Dynamic knob range: the engine recalculates min/max from the most-constrained register so full knob travel always maps to the full available range
- Re-derivation: switching from raw to macro mode computes knob position from current register values (not stored last-position)
- Ratio preservation: if a user hand-sculpts register ratios in raw mode, macro sweep preserves those ratios via proportional scaling
- vIIR x vWALL stability ceiling enforced at the engine level -- both macro and raw register writes are checked
- The stability ceiling is a product constraint: `abs(vIIR) * abs(vWALL) <= STABILITY_LIMIT`
- Address bounds checking: all m-prefix (u16) address registers are clamped against `work_buf_size`
- Safety enforcement applies uniformly in both macro mode and direct register writes (raw mode)
- Safety enforcement hooks into the existing `spu94_set_reg_*` path or wraps it

### Claude's Discretion
- Internal struct layout for macro group definitions
- Whether stability limit is a compile-time constant or runtime-configurable
- Whether safety clamping returns a result code or silently clamps (lean toward result code for consistency with existing `spu94_result_t`)
- Exact file naming for new source files
- Whether to add a public header `spu94_macro.h` or extend `spu94.h`
- Test file organization

### Deferred Ideas (OUT OF SCOPE)
- Independent clamping mode (ratio drift as individual registers hit ceilings)
- Parameter slew / smoothing -- separate feature
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| MACRO-01 | Macro engine lives in C core, operates on relative scaling from current register state | Architecture patterns section: macro group struct, proportional scaling math, register I/O hooks |
| MACRO-02 | Gang clamping -- all registers stop together when any hits ceiling/floor | Gang clamping algorithm section: find most-constrained register, limit all proportionally |
| MACRO-03 | Dynamic knob range -- recalculate min/max from most-constrained register | Dynamic range calculation section: derive effective_range from current register state |
| MACRO-04 | Raw-to-macro re-derivation of knob position | Re-derivation algorithm: inverse of the apply function, recover position from register ratios |
| MACRO-05 | Macros preserve hand-sculpted register ratios | Ratio preservation: store base ratios on re-derivation, proportional scaling multiplies all by same factor |
| SAFE-01 | vIIR x vWALL product enforced below stability ceiling | Stability analysis section: factory preset product survey, ceiling derivation, enforcement hook |
| SAFE-02 | m-prefix address registers bounds-checked against work buffer size | Address bounds section: work_buf_size / 2 ceiling for halfword offsets, clamp-on-write |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Macro group definition | C core (src/spu94/) | -- | Pure data structure, no UI dependency |
| Proportional scaling | C core (src/spu94/) | -- | Integer arithmetic on register values |
| Gang clamping | C core (src/spu94/) | -- | Register coordination logic |
| Dynamic knob range | C core (src/spu94/) | -- | Computed from register state |
| Re-derivation | C core (src/spu94/) | -- | Inverse mapping from registers to knob position |
| vIIR x vWALL stability | C core (src/spu94/) | -- | Enforced at register write level |
| m-prefix address bounds | C core (src/spu94/) | -- | Enforced at register write level |

Everything in this phase is C core library. No JUCE, CLI, or Python changes. [VERIFIED: codebase analysis + CONTEXT.md locked decisions]

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| C11 | -- | Implementation language | Project standard (CMakeLists.txt: `CMAKE_C_STANDARD 11`) |
| Unity | vendored | Unit test framework | Already vendored at `tests/unit/vendor/Unity/` |
| CMake | >= 3.22 | Build system | Project standard |

[VERIFIED: CMakeLists.txt and tests/unit/CMakeLists.txt]

### Supporting
No new libraries needed. This phase is pure C core: integer arithmetic, struct definitions, and test assertions. All tools are already in the project.

## Architecture Patterns

### System Architecture Diagram

```
                    +--------------------------+
                    |   External Caller        |
                    |  (JUCE / CLI / Python)   |
                    +--------+---------+-------+
                             |         |
                    set macro knob    set raw register
                             |         |
                    +--------v---------v-------+
                    |   Macro Engine Layer      |
                    |  spu94_macro.c            |
                    |                           |
                    |  - group definitions      |
                    |  - proportional scaling   |
                    |  - gang clamping          |
                    |  - re-derivation          |
                    |  - dynamic range calc     |
                    +--------+-----------------+
                             |
                    calls spu94_set_reg_{i16,u16}
                             |
                    +--------v-----------------+
                    |   Safety Layer            |
                    |  spu94_safety.c           |
                    |                           |
                    |  - vIIR x vWALL ceiling   |
                    |  - m-prefix addr bounds   |
                    |  - returns result codes   |
                    +--------+-----------------+
                             |
                    routes to existing engine layer
                             |
                    +--------v-----------------+
                    |   Register I/O Layer      |
                    |  spu94_register_io.c      |
                    |  (existing, untouched)    |
                    |                           |
                    |  - write policy routing   |
                    |  - pending/immediate      |
                    |  - mBASE snap-on-write    |
                    |  - tempo notification     |
                    +--------+-----------------+
                             |
                    +--------v-----------------+
                    |   State Storage           |
                    |  struct spu94_state       |
                    |  (1288 bytes, 15096 free) |
                    +--------------------------+
```

### Recommended Project Structure

New files for this phase:

```
include/spu94/
  spu94_macro.h          # Public macro engine API (group types, engine functions)
src/spu94/
  spu94_macro.c          # Macro engine: group apply, re-derive, gang clamp
  spu94_safety.c         # Safety enforcement: stability ceiling, address bounds
tests/unit/
  macro/
    CMakeLists.txt
    test_macro_group.c    # Group definition, proportional scaling
    test_macro_gang.c     # Gang clamping behavior
    test_macro_derive.c   # Re-derivation from register state
    test_macro_range.c    # Dynamic knob range recalculation
  safety/
    CMakeLists.txt
    test_safety_stability.c  # vIIR x vWALL ceiling enforcement
    test_safety_bounds.c     # m-prefix address bounds checking
```

### Pattern 1: Safety Hook at Register Write Level

**What:** Safety enforcement that intercepts register writes and clamps values before they reach the engine layer.

**When to use:** Both macro engine and raw register writes must be safety-checked.

**Design decision:** The safety layer wraps the existing `spu94_set_reg_*` functions rather than modifying them. Two approaches are viable:

**Option A -- Wrapper functions (recommended):**
The safety layer provides `spu94_safe_set_reg_i16` / `spu94_safe_set_reg_u16` that check constraints, clamp if needed, then call the real setters. The macro engine calls the safe setters. Raw mode callers also call the safe setters. The existing `spu94_set_reg_*` remain unchanged for low-level use (presets, tempo system, internal engine). [ASSUMED]

This avoids modifying the existing register I/O code (zero blast radius on the tested hot path) while still enforcing safety for both macro and raw surface writes.

**Option B -- Inline hook in spu94_set_reg_*:**
Add safety checks inside the existing setter bodies. Simpler call graph but modifies the 196-line register_io.c that every existing caller depends on. Risk: the stability check runs on EVERY register write including preset load, tempo resnap, etc., adding overhead to paths that don't need it. [ASSUMED]

**Example (Option A):**
```c
// Source: project convention from spu94_register_io.c pattern
spu94_result_t spu94_safe_set_reg_i16(spu94_state *state, spu94_reg_t reg, int16_t value) {
    if (state == NULL) return SPU94_INVALID_STATE;
    
    // vIIR: check product with current vWALL
    if (reg == SPU94_REG_vIIR) {
        int16_t vWALL = state->reg_values[SPU94_REG_vWALL];
        int32_t product = (int32_t)abs_i16(value) * (int32_t)abs_i16(vWALL);
        if (product > SPU94_STABILITY_LIMIT) {
            // Clamp vIIR to the max value that keeps product under limit
            int16_t max_viir = (int16_t)(SPU94_STABILITY_LIMIT / (int32_t)abs_i16(vWALL));
            value = (value >= 0) ? max_viir : -max_viir;
            spu94_set_reg_i16(state, reg, value);
            return SPU94_STABILITY_CLAMPED;
        }
    }
    // vWALL: check product with current vIIR
    if (reg == SPU94_REG_vWALL) {
        int16_t vIIR = state->reg_values[SPU94_REG_vIIR];
        int32_t product = (int32_t)abs_i16(vIIR) * (int32_t)abs_i16(value);
        if (product > SPU94_STABILITY_LIMIT) {
            int16_t max_vwall = (int16_t)(SPU94_STABILITY_LIMIT / (int32_t)abs_i16(vIIR));
            value = (value >= 0) ? max_vwall : -max_vwall;
            spu94_set_reg_i16(state, reg, value);
            return SPU94_STABILITY_CLAMPED;
        }
    }
    return spu94_set_reg_i16(state, reg, value);
}
```

### Pattern 2: Macro Group Definition Struct

**What:** A compile-time struct that defines which registers belong to a macro group, their type constraints, and their scaling behavior.

**When to use:** Phase 20 defines the engine; Phase 21 wires up the 8 specific groups.

**Example:**
```c
// Source: project convention from spu94_tempo.c binding state pattern

typedef struct {
    spu94_reg_t     reg;           // which hardware register
    spu94_reg_type_t type;         // I16 or U16 (redundant but explicit for auditability)
    int32_t         floor;         // minimum value (signed for I16, cast for U16)
    int32_t         ceiling;       // maximum value
} spu94_macro_member_t;

#define SPU94_MACRO_MAX_MEMBERS 14  // Room Size has 14 registers (largest group)

typedef struct {
    const char              *name;
    uint8_t                  member_count;
    spu94_macro_member_t     members[SPU94_MACRO_MAX_MEMBERS];
} spu94_macro_group_t;
```

### Pattern 3: Proportional Scaling with Gang Clamping

**What:** When a macro knob sweeps, all registers in the group scale by the same factor from their reference ratios. When any register hits its floor or ceiling, all stop.

**When to use:** Every macro knob apply operation.

**Algorithm:**
1. Store "base values" -- the register state at the moment macro mode is entered (or re-derived)
2. Compute each register's headroom: distance from base to ceiling (for upward sweep), distance from base to floor (for downward sweep)
3. Find the most-constrained register: the one with the LEAST headroom as a fraction of its base-to-limit distance
4. That register's headroom defines the full knob range (0.0 to 1.0 maps to floor-to-ceiling of the most constrained)
5. All other registers scale proportionally: `new_value = base + (base_ratio * knob_delta * scale_factor)`
6. Gang clamp: if the most-constrained member hits its limit at knob position P, ALL members use position P as their effective max

### Pattern 4: Re-derivation (Position Recovery)

**What:** When switching from raw to macro mode, compute the knob position from current register state.

**Algorithm:**
1. Read all current register values for the group
2. Compute what fraction of the way each register is between its floor and ceiling
3. The knob position is the median (or normalized average) of these fractions -- but since ratios are preserved by the engine, in practice all registers should report the same fractional position
4. If a user hand-sculpted registers in raw mode, the ratios may differ. Store the current values as the NEW base ratios and set knob to the position that matches the most-constrained register

### Anti-Patterns to Avoid
- **Modifying spu94_register_io.c hot path:** The reverb body calls `spu94_get_reg_*` thousands of times per second. Safety checks belong on the WRITE side (low frequency), not the read side.
- **Absolute lookup tables:** The locked decision explicitly forbids lookup-table-based macros. All scaling is relative to current state.
- **Floating-point arithmetic in the core engine:** The project uses Q15 integer math throughout. The knob position can be a float at the API boundary (callers pass float, engine converts to fixed-point internally), but all internal scaling math should use integer multiply+shift. [ASSUMED]
- **Global state for macro groups:** Per the tempo system pattern, all macro state lives inside `struct spu94_state` (instance-scoped, not global).

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Q15 multiplication | Custom multiply | `q15_mul_truncate_with_err` | Already exists, handles saturation and error tracking |
| Signed abs() for int16 | Inline ternary | Dedicated `abs_i16` helper | INT16_MIN edge case (-32768 has no positive int16 representation) |
| Register type checking | Switch on reg name | `spu94_reg_type(reg)` | Already exists, packed bitmask, tested |
| Pending-value reads | Direct state access | `get_latest_u16` pattern from `spu94_tempo.c` | TICK_LATCHED registers may have unseen pending values |

**Key insight:** The Q15 arithmetic primitives, register type system, and write-timing policy are already battle-tested across 19 phases. The macro engine should use these existing tools, not reimplement them.

## Common Pitfalls

### Pitfall 1: INT16_MIN Asymmetry in Stability Checks
**What goes wrong:** `abs(-32768)` is undefined behavior for int16_t because +32768 doesn't fit in int16_t. If vIIR or vWALL is INT16_MIN (-0x8000), the stability product computation overflows.
**Why it happens:** C's `abs()` for int16 produces UB at INT16_MIN. The PS1 uses INT16_MIN as -1.0 in Q15 (the vIIR anomaly documented in D-10).
**How to avoid:** Use int32 widening before abs: `int32_t a = (value < 0) ? -(int32_t)value : (int32_t)value;`. This is already the project pattern (see `spu94_reverb.c` lines 155-156).
**Warning signs:** Safety checks that use `abs()` or `(int16_t)(-value)` directly.

### Pitfall 2: Pending vs Active Values in Safety Checks
**What goes wrong:** m-prefix registers are TICK_LATCHED -- a write stages into `pending_values[]` and only commits at the next `spu94_tick()`. If safety checks read `reg_values[]` (active) to compute the current vWALL when checking a vIIR write, they see stale data if vWALL was just written but not yet ticked.
**Why it happens:** The write-timing split (IMMEDIATE for v-prefix, TICK_LATCHED for d/m-prefix) means different registers have different "latest value" semantics.
**How to avoid:** vIIR and vWALL are both v-prefix IMMEDIATE registers, so their `reg_values[]` ARE the latest. But m-prefix address bounds checks must use `get_latest_u16()` (the pending-aware helper from `spu94_tempo.c`) to see the most recent m-prefix value. [VERIFIED: spu94_register_io.c -- all i16/v-prefix registers are IMMEDIATE]

### Pitfall 3: mBASE Snap-on-Write Side Effect
**What goes wrong:** When the macro engine scales m-prefix address registers, if it also touches mBASE, the `spu94_mbase_on_write` side effect fires and snaps the BufferAddress. This could cause audio glitches if unintended.
**Why it happens:** mBASE has an IMMEDIATE write policy plus a snap side effect (ADR-0006). The macro engine must not interfere with this.
**How to avoid:** Room Size macro scales m-prefix ADDRESS registers (mLSAME, mRSAME, mLCOMB*, etc.) but NOT mBASE. mBASE is a base-address pointer, not a "room size" register. The macro group definition for Room Size must explicitly EXCLUDE mBASE from its member list. [VERIFIED: CONTEXT.md lists 14 m-prefix registers; mBASE is not among them]

### Pitfall 4: Zero-Division in Dynamic Range Calculation
**What goes wrong:** When computing how much headroom a register has, dividing by the base value to get a ratio. If any register's base value is zero, division by zero.
**Why it happens:** The "Off" preset has many zero-valued registers. Loading Off then switching to macro mode means zero base values.
**How to avoid:** Zero base values mean the register has no contribution to the group. Treat them as having infinite headroom (they can only go up from zero) OR skip them in the most-constrained calculation. The "ratio" for a zero-base register is undefined -- use additive scaling (absolute offset) rather than multiplicative for registers at zero, or treat zero-base as "this register stays at zero during the sweep." [ASSUMED]

### Pitfall 5: Re-entrancy Between Safety and Macro Engine
**What goes wrong:** The macro engine calls the safe setter, which clamps a value, which triggers the macro engine to re-derive positions, which calls the setter again.
**Why it happens:** Same pattern as the tempo system's `tempo_writing` guard -- subsystem writes should not trigger their own hooks.
**How to avoid:** Use a re-entrancy guard (boolean flag in struct spu94_state) exactly like `state->tempo_writing` in `spu94_tempo.c`. Set it before the engine writes registers, clear it after. The safety layer checks it and skips re-derivation triggers during engine writes. [VERIFIED: spu94_tempo.c re-entrancy pattern at line 73]

### Pitfall 6: Stability Ceiling Value Derivation
**What goes wrong:** Choosing a stability limit that is too aggressive (factory presets get clamped) or too permissive (runaway feedback).
**Why it happens:** The product `abs(vIIR) * abs(vWALL)` across factory presets ranges from 0 (Off/Delay) to 1,065,320,704 (Echo: vIIR=0x7FFF, vWALL=0x8100). The Q15 product space maximum is 32768*32768 = 1,073,741,824. The Echo preset sits at 99.22% of the theoretical max.
**How to avoid:** Set the stability limit at or slightly above the Echo preset's product. A clean power-of-two constant like `1,073,741,824` (2^30, which is exactly 32768*32768 = 0x40000000) represents the theoretical "both coefficients at unity" ceiling. All factory presets fall below this. Any product above this means the combined loop gain exceeds 1.0 in Q15 space and the IIR filter is mathematically divergent. [VERIFIED: preset product survey computed from spu94_presets.c]

## Code Examples

### Safety: Stability Product Check
```c
// Source: derived from spu94_reverb.c IIR formula analysis + preset survey

// The IIR formula: [m] = (input + [d]*vWALL - [m-2])*vIIR + [m-2]
// Rearranging: the filter is stable when |vIIR| < 1.0 AND the combined
// loop gain |vIIR * vWALL| < 1.0 in Q15 space.
// Q15 unity = 0x7FFF (32767). Product of two Q15 values scaled to Q30:
//   product_q30 = (int32_t)abs(vIIR) * (int32_t)abs(vWALL)
//   Unity in Q30 = 32768 * 32768 = 1,073,741,824 = 0x40000000
//
// All 10 factory presets satisfy: product_q30 < 0x40000000
// Echo preset is the most aggressive at 0x3F7F0100 (99.22% of unity).

#define SPU94_STABILITY_LIMIT  0x40000000  // 2^30 = Q15 unity squared

static inline int32_t abs_i32_safe(int16_t v) {
    return (v < 0) ? -(int32_t)v : (int32_t)v;
}

static int stability_check(int16_t vIIR, int16_t vWALL) {
    int32_t product = abs_i32_safe(vIIR) * abs_i32_safe(vWALL);
    return product <= SPU94_STABILITY_LIMIT;  // 1 = safe, 0 = would exceed
}
```

### Safety: Address Bounds Clamping
```c
// Source: derived from spu94_reverb.c reverb_buf_read/write bounds check pattern

// m-prefix registers are halfword offsets. Byte offset = halfword * 2.
// The reverb body masks with 0x7FFFE and checks against work_buf_size.
// Safety enforcement: clamp the halfword value so the byte offset stays
// within [0, work_buf_size).

static uint16_t clamp_address(uint16_t value, size_t work_buf_size) {
    if (work_buf_size == 0) return 0;
    // Max valid halfword: (work_buf_size / 2) - 1
    // But the reverb body reads halfword + buffer_address, so the raw
    // register value alone isn't the full address. The simplest safe bound
    // is: value must not exceed (work_buf_size / 2) - 1.
    uint16_t max_halfword = (uint16_t)((work_buf_size / 2) - 1);
    return (value > max_halfword) ? max_halfword : value;
}
```

### Macro: Proportional Scaling with Integer Math
```c
// Source: derived from project Q15 patterns in spu94_reverb.c

// Given: base_values[N] (register state when macro mode entered)
//        knob_position (0.0 to 1.0, or 0 to 32767 in Q15)
//        floor/ceiling per register
//
// For each register in group:
//   range = ceiling - base_value  (upward sweep)
//   new_value = base_value + (range * knob_fraction) / 32768
//
// Gang clamp: find the register with the smallest range fraction,
// that register's range defines the effective 0-to-1 mapping for all.

typedef struct {
    int32_t  base_values[SPU94_MACRO_MAX_MEMBERS]; // snapshot at derive time
    int32_t  effective_min;   // in knob-position units (Q15)
    int32_t  effective_max;   // in knob-position units (Q15)
} spu94_macro_state_t;
```

### Re-derivation: Recover Knob Position from Register State
```c
// Source: project pattern -- similar to spu94_tempo.c's proportional binding

// For a group of U16 registers with known floor/ceiling:
//   fraction[i] = (current_value[i] - floor[i]) / (ceiling[i] - floor[i])
//
// Since proportional scaling keeps all fractions equal, they should all
// agree. Pick the median or use the most-constrained register's fraction
// as the knob position. Store current values as base_values for future sweeps.

static int32_t derive_knob_position(const spu94_macro_group_t *group,
                                     const int32_t *current_values) {
    // Find the fraction for the most-constrained register
    int32_t min_fraction = INT32_MAX;
    for (int i = 0; i < group->member_count; i++) {
        int32_t range = group->members[i].ceiling - group->members[i].floor;
        if (range <= 0) continue;  // degenerate: skip
        int32_t offset = current_values[i] - group->members[i].floor;
        // Q15 fraction: (offset * 32768) / range
        int32_t frac = (offset * 32768) / range;
        if (frac < min_fraction) min_fraction = frac;
    }
    return (min_fraction == INT32_MAX) ? 0 : min_fraction;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Direct register manipulation only | Macro knobs + raw registers | This phase (v1.6) | Users get musical controls on top of raw access |
| No safety constraints on writes | vIIR x vWALL ceiling + address bounds | This phase (v1.6) | Prevents runaway feedback and OOB buffer access |
| Tempo system as only register coordination | Macro engine as general-purpose group coordinator | This phase (v1.6) | Foundation for all 8 macro controls in Phase 21 |

## Stability Ceiling Analysis

Factory preset vIIR x vWALL product survey (abs(vIIR) * abs(vWALL)):

| Preset | vIIR | vWALL | Product | % of Q15 Unity |
|--------|------|-------|---------|----------------|
| Off | 0x0000 | 0x0000 | 0 | 0.0% |
| Room | 0x6D80 | 0xBA80 (-17792) | 498,745,344 | 46.5% |
| Studio A | 0x70F0 | 0x9C00 (-25600) | 740,147,200 | 68.9% |
| Studio B | 0x70F0 | 0xB4C0 (-19264) | 556,960,768 | 51.9% |
| Studio C | 0x6F60 | 0xA680 (-22912) | 653,266,944 | 60.8% |
| Hall | 0x6000 | 0xC000 (-16384) | 402,653,184 | 37.5% |
| Half Echo | 0x70F0 | 0x8500 (-31488) | 910,381,056 | 84.8% |
| Space Echo | 0x7E00 | 0xB000 (-20480) | 660,602,880 | 61.5% |
| **Echo** | **0x7FFF** | **0x8100 (-32512)** | **1,065,320,704** | **99.2%** |
| Delay | 0x7FFF | 0x0000 | 0 | 0.0% |
| Init | 0x6000 | 0xC000 (-16384) | 402,653,184 | 37.5% |

[VERIFIED: computed from spu94_presets.c factory table values]

**Ceiling recommendation:** `0x40000000` (2^30 = 1,073,741,824). This is exactly the Q15 product-space unity point (32768 * 32768). All factory presets pass. The Echo preset at 99.2% is the tightest. Values above this represent a combined loop gain > 1.0, which causes the IIR filter to diverge (grow without bound until Q15 saturation creates nasty hard-clipping distortion).

**Sound character note:** The narrow gap between Echo's product (99.2%) and the ceiling (100%) is the region where the reverb tail gets VERY long and dense -- nearly infinite sustain. Users exploring above the Echo preset's aggressiveness would be in "creative feedback territory" that the ceiling prevents from tipping into digital screech. This is the right place to draw the line. [ASSUMED -- based on DSP theory; exact PS1 clamping behavior not verified against hardware]

## State Budget Analysis

Current `struct spu94_state` size: **1,288 bytes** [VERIFIED: compiled and measured]
Maximum: **16,384 bytes** (`SPU94_STATE_SIZE_MAX`)
Remaining: **15,096 bytes**

Estimated macro engine state additions:
- 8 macro groups x per-group state (base values, effective range, active flag): ~8 x (14 regs x 4 bytes + 16 bytes overhead) = ~576 bytes
- Safety state (minimal -- just the re-entrancy guard): ~4 bytes
- **Total estimated: ~600 bytes**

Conclusion: Well within budget. No need to bump `SPU94_STATE_SIZE_MAX`. [VERIFIED: measurement + estimate]

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Wrapper-function approach (Option A) preferred over inline hooks (Option B) for safety enforcement | Pattern 1 | Low -- either approach works; Option B would require modifying spu94_register_io.c which is tested but stable |
| A2 | Float knob position at API boundary, integer math internally | Anti-Patterns | Low -- could use pure integer (Q15 knob position); float is more natural for GUI callers |
| A3 | Zero-base registers treated as "stays at zero" during sweep | Pitfall 4 | Medium -- could miss creative use cases where user wants to "grow" a zero-value register |
| A4 | Stability limit of 0x40000000 is the right threshold | Stability Ceiling | Low -- all factory presets pass, and the Q15 math is clear: above this = divergent loop gain |
| A5 | PS1 hardware clamps at a similar boundary | Stability Ceiling sound note | Medium -- we're modeling observed behavior, not verified hardware clamping point |

## Open Questions (RESOLVED)

1. **Knob position representation: float vs Q15 integer?**
   - RESOLVED: Float at API boundary (`float position`, 0.0-1.0). JUCE slider sends float naturally; internal math uses float * int32 scaling. Decided in Plan 20-02.

2. **Should safety enforcement be optional (toggle)?**
   - RESOLVED: No toggle. Safety always on in both macro and raw mode per CONTEXT.md decisions. If experimental feedback becomes a feature, a flag can be added later.

3. **What happens when a preset is loaded that would violate safety?**
   - RESOLVED: Deferred to Phase 21 (PRESET-01). Factory presets bypass safety (pre-audited). User preset load should go through safety layer.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored, `tests/unit/vendor/Unity/`) |
| Config file | `tests/unit/CMakeLists.txt` (add_subdirectory per module) |
| Quick run command | `cd build && ctest -R "test_macro\|test_safety" --output-on-failure` |
| Full suite command | `cd build && ctest --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| MACRO-01 | Macro engine applies proportional scaling from current state | unit | `ctest -R test_macro_group` | Wave 0 |
| MACRO-02 | Gang clamping stops all registers when any hits limit | unit | `ctest -R test_macro_gang` | Wave 0 |
| MACRO-03 | Dynamic knob range recalculates from most-constrained | unit | `ctest -R test_macro_range` | Wave 0 |
| MACRO-04 | Re-derivation computes position from register state | unit | `ctest -R test_macro_derive` | Wave 0 |
| MACRO-05 | Ratios preserved after raw editing + macro sweep | unit | `ctest -R test_macro_derive` | Wave 0 |
| SAFE-01 | vIIR x vWALL product enforced below ceiling | unit | `ctest -R test_safety_stability` | Wave 0 |
| SAFE-02 | m-prefix address registers clamped to buffer size | unit | `ctest -R test_safety_bounds` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build && cmake --build . --target test_macro_group test_macro_gang test_macro_derive test_macro_range test_safety_stability test_safety_bounds -j4 && ctest -R "test_macro|test_safety" --output-on-failure`
- **Per wave merge:** `cd build && cmake --build . -j4 && ctest --output-on-failure`
- **Phase gate:** Full suite (116+ tests) green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/macro/CMakeLists.txt` -- test targets for macro engine
- [ ] `tests/unit/macro/test_macro_group.c` -- covers MACRO-01
- [ ] `tests/unit/macro/test_macro_gang.c` -- covers MACRO-02
- [ ] `tests/unit/macro/test_macro_range.c` -- covers MACRO-03
- [ ] `tests/unit/macro/test_macro_derive.c` -- covers MACRO-04, MACRO-05
- [ ] `tests/unit/safety/CMakeLists.txt` -- test targets for safety layer
- [ ] `tests/unit/safety/test_safety_stability.c` -- covers SAFE-01
- [ ] `tests/unit/safety/test_safety_bounds.c` -- covers SAFE-02
- [ ] `tests/unit/CMakeLists.txt` update -- add_subdirectory(macro), add_subdirectory(safety)

## Sources

### Primary (HIGH confidence)
- `src/spu94/spu94_register_io.c` -- register write path, signedness enforcement, write-timing routing
- `src/spu94/spu94_tempo.c` -- per-register binding state, re-entrancy guard pattern, write interception
- `src/spu94/spu94_state_internal.h` -- struct layout (1,288 bytes measured)
- `src/spu94/spu94_presets.c` -- factory preset register values (stability product survey)
- `src/spu94/spu94_reverb.c` -- IIR formula, buffer tap helpers, INT16_MIN handling patterns
- `include/spu94/spu94.h` -- result codes (append-only enum), public API surface
- `include/spu94/spu94_registers.h` -- register enum, signedness types, write policies

### Secondary (MEDIUM confidence)
- DSP stability theory: IIR filter with loop gain |vIIR * vWALL| > 1.0 diverges [standard DSP textbook knowledge]

### Tertiary (LOW confidence)
- PS1 hardware clamping behavior at the stability boundary [ASSUMED -- not verified against real hardware]

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- no new libraries, pure C core addition
- Architecture: HIGH -- tempo system pattern is proven, safety hooks follow existing register I/O conventions
- Pitfalls: HIGH -- INT16_MIN handling, pending vs active, mBASE snap all verified against codebase
- Stability ceiling: HIGH for the math, MEDIUM for the exact threshold choice (all factory presets verified, threshold is mathematically sound)

**Research date:** 2026-05-03
**Valid until:** 2026-06-03 (stable -- C core, no external dependencies)
