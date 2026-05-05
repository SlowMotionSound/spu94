# Phase 21: Macro Controls - Research

**Researched:** 2026-05-04
**Domain:** C core macro control surface -- Spread+Sweep engine extension, bipolar knobs, link/constrain toggles, Decay/Reflectivity coupling, preset derivation
**Confidence:** HIGH

## Summary

Phase 21 builds the full Room Designer control surface on top of Phase 20's macro engine. The engine currently supports single-axis unipolar [0,1] proportional scaling with gang clamping. Phase 21 needs three architectural extensions: (1) a Spread+Sweep dual-axis model that separates ratio manipulation from uniform translation, (2) a bipolar path for signed registers like Decay/Reflectivity/Early Reflections Sweep, and (3) a reference-values concept for preset-to-macro derivation distinct from the existing base-values snapshot.

The control surface defines ~30 user-facing controls mapping to the 35 SPU registers. These decompose into three categories: individual register controls (wall distances, individual taps, individual diffusion positions), Spread+Sweep macro pairs (echo speed, taps, diffusion sections, early reflections), and special single-knob controls (Room Size master, Buffer/mBASE, Decay, Reflectivity). The current `SPU94_MACRO_MAX_GROUPS` of 8 is insufficient; the expanded surface requires at least 16 group slots (detailed enumeration below). Link toggles and constrained/unconstrained modes add ~8 boolean flags to the state struct. The struct headroom is 14,488 bytes -- more than adequate for all additions.

**Primary recommendation:** Extend the Phase 20 engine with Spread+Sweep dual-axis apply, bipolar apply path, and reference-value storage. Define all macro group tables as static const arrays in a new `spu94_macro_controls.c`. Wire the Decay-to-Reflectivity coupling as a post-write hook in the Decay apply path. Total new/modified files: 2-3 source files, 1-2 headers, 6-8 test files.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: All multi-register macros use Spread + Sweep (proportional spacing + uniform translation)
- D-02: Preset original register values are the reference anchor; user can override in advanced mode
- D-03: Signed registers sweep into negative territory; unsigned within positive range
- D-04: No hardcoded floor values -- proportional relationships from preset/user overrides
- D-05: 4 wall controls (Left Wall, Right Wall, Left Cross, Right Cross) each with distance + echo speed pair
- D-06: Per-wall link toggle (echo speed tethered to distance proportionally when linked)
- D-07: Same/Cross link toggle for physical realism; independent by default
- D-08: Echo Speed Spread+Sweep over dLSAME, dRSAME, dLDIFF, dRDIFF
- D-09: 8 individual tap controls (mLCOMB1-4, mRCOMB1-4), independently positioned
- D-10: Tap Position Spread+Sweep macro on top of 8 individual controls
- D-11: Wall constrained/unconstrained toggle (constrained clamps within wall boundaries)
- D-12: Diffusion Amount Spread+Sweep (vAPF1, vAPF2)
- D-13: Diffusion Texture Spread+Sweep (dAPF1, dAPF2)
- D-14: Diffusion Position 4 individual controls + Spread+Sweep; constrained/unconstrained toggle
- D-15: Room Size master single knob, scales all m-prefix + linked echo speeds
- D-16: Buffer (mBASE) exposed, inverted mapping (knob up = mBASE down), snap-on-write
- D-17: Buffer inverted mapping
- D-18: Buffer snap-on-write semantics -- changes immediate, not smooth
- D-19: Decay bipolar center-detent (vIIR): left = -0x1000..0, right = 0..0x7FFF
- D-20: SAFE-03: vIIR floor at -0x1000
- D-21: SAFE-04: -0x8000 unreachable from any GUI surface
- D-22: Reflectivity bipolar center-detent (vWALL), full signed range
- D-23: Auto-rederive coupling: Decay changes -> Reflectivity knob re-derives from clamped vWALL
- D-24: Stability coupling: Reflectivity range dynamically constrained by current vIIR
- D-25: Early Reflections Spread+Sweep (vCOMB1-4); spread left = collapse, center = preset, right = exaggerated
- D-26: Individual vCOMB sliders set proportional reference; preset weights default
- D-27: Width macro removed
- D-28: Presets are register-only; macros derive positions from register state
- D-29: Macros store preset reference values alongside base values
- D-30: Bipolar engine extension needed for signed controls (Decay, Reflectivity, Early Reflections Sweep, Diffusion Amount Sweep)

### Claude's Discretion
- Concrete floor/ceiling values for each register in each group definition
- How bipolar apply path integrates (separate function vs mode flag)
- Internal storage layout for reference values
- Whether Reflectivity re-derive trigger lives in Decay apply path or post-write hook
- How link toggles are represented (bitfield, bool array, etc.)
- How constrained/unconstrained interacts with safety layer
- Test file organization and specific scenarios
- Whether SPU94_MACRO_MAX_GROUPS needs increasing (answer: yes, from 8 to at least 16)

### Deferred Ideas (OUT OF SCOPE)
- mBASE "Buffer" as sequenceable effect -- future automation/modulation target
- Naming refinement for Wall Constrained/Unconstrained toggle -- placeholder name, refine in GUI phase
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| WALL-01 | Left Wall distance + echo speed paired | Wall group definitions with link toggle mechanism |
| WALL-02 | Right Wall distance + echo speed paired | Wall group definitions with link toggle mechanism |
| WALL-03 | Left Cross distance + echo speed paired | Wall group definitions with link toggle mechanism |
| WALL-04 | Right Cross distance + echo speed paired | Wall group definitions with link toggle mechanism |
| WALL-05 | Per-wall link toggle (echo speed tethered to distance) | Link toggle boolean array in state struct; Room Size apply hooks echo speeds when linked |
| WALL-06 | Same/Cross link toggle for physical realism | Additional toggle controlling whether same-side changes propagate to cross-side |
| ECHO-SPD-01 | Echo Speed Spread+Sweep over 4 d-prefix registers | Spread+Sweep engine extension; new group type |
| TAP-01 | 8 individual tap position controls | Individual register access via existing typed setters |
| TAP-02 | Tap Position Spread+Sweep macro | Spread+Sweep group over 8 m-prefix registers |
| TAP-03 | Wall constrained/unconstrained toggle | Constrained mode dynamically adjusts member ceilings to wall boundaries |
| DIFF-AMT-01 | Diffusion Amount Spread+Sweep (vAPF1, vAPF2) | Spread+Sweep group; signed registers, needs bipolar sweep |
| DIFF-TEX-01 | Diffusion Texture Spread+Sweep (dAPF1, dAPF2) | Spread+Sweep group; unsigned registers |
| DIFF-POS-01 | Diffusion Position 4 individual + Spread+Sweep | Individual + Spread+Sweep; same pattern as taps |
| DIFF-POS-02 | Diffusion Position constrained/unconstrained toggle | Same mechanism as TAP-03 |
| ROOM-01 | Room Size master scales all m-prefix + linked echo speeds | Single-knob macro using Phase 20 proportional scaling; conditionally includes d-prefix when link toggles active |
| BUF-01 | Buffer (mBASE) inverted, snap-on-write, safety clamps | Special single-register macro; inverted mapping; triggers safety re-clamp of all m-prefix |
| BUF-02 | Safety clamps m-prefix at shrinking buffer boundary | Already implemented in Phase 20 (SAFE-02); Buffer control triggers re-evaluation |
| SS-01 | All multi-register macros use Spread+Sweep | Engine extension: dual-axis apply path |
| SS-02 | Preset register values as reference anchor | Reference values storage in state struct |
| SS-03 | Signed registers sweep into negative territory | Bipolar engine path |
| CTRL-01 | Decay bipolar center-detent (vIIR) | Bipolar apply with asymmetric half ranges |
| CTRL-02 | Reflectivity bipolar center-detent (vWALL) | Bipolar apply with stability-coupled ceiling; auto-rederive hook |
| CTRL-03 | Early Reflections Spread+Sweep (vCOMB1-4) | Spread+Sweep; signed registers; bipolar sweep |
| SAFE-03 | vIIR floor at -0x1000 | Enforced in Decay macro definition ceiling/floor |
| SAFE-04 | -0x8000 unreachable | Decay macro floor = -0x1000; no path bypasses |
| PRESET-01 | Presets register-only; macros derive positions | Derive-all-macros function called after preset load |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Spread+Sweep engine | C core library | -- | All DSP control logic lives in C core per project rule |
| Bipolar apply path | C core library | -- | Signed register math is engine-level |
| Macro group definitions | C core library | -- | Static const tables compiled into library |
| Link toggles | C core library | -- | State affects register writes; must be in engine |
| Constrained mode | C core library | -- | Dynamic ceiling adjustment interacts with safety layer |
| Decay/Reflectivity coupling | C core library | -- | Cross-macro dependency resolved at write time |
| Preset derivation | C core library | -- | Engine re-derives all macro positions after preset load |
| Buffer (mBASE) control | C core library | -- | Inverted mapping + safety re-clamp is engine logic |

## Standard Stack

This phase is pure C core -- no external dependencies. All code builds with the existing CMake toolchain (gcc/C99).

### Core (project-internal)
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Unity | vendored (tests/unit/vendor/) | Unit test framework | Already in use for all 79+ unit tests [VERIFIED: ctest -N shows 122 tests total] |
| spu94 core | in-tree | Register I/O, safety enforcement, macro engine | Phase 20 output; 4 macro tests + 2 safety tests passing [VERIFIED: ctest run] |

**No external packages required.** Phase 21 adds only C source files to the existing `spu94_obj` OBJECT library and test executables to `tests/unit/`.

## Architecture Patterns

### System Architecture Diagram

```
Preset Load (.spu94)
    |
    v
[35 Register Values] ---> reg_values[] / pending_values[]
    |                            ^
    |                            |  (safety-enforced writes)
    v                            |
[Derive All Macros] -----> [Macro Engine]
    |                         |      |
    |   +--------------------+       |
    |   |                            |
    v   v                            v
[Reference Values]    [Spread+Sweep Apply]  <-- knob input (spread, sweep)
    |                      |     |
    |                      |     +--> [Bipolar Path] (signed registers)
    |                      |     |
    |                      |     +--> [Unipolar Path] (unsigned registers)
    |                      |
    |                      v
    |             [Gang-Clamped Values]
    |                      |
    |                      v
    |        [spu94_safe_set_reg_{i16,u16}]
    |                      |
    |                      +---> SAFE-01: vIIR x vWALL stability ceiling
    |                      +---> SAFE-02: m-prefix address bounds
    |                      |
    |                      v
    |              [Register State Updated]
    |                      |
    |                      +--- [Decay apply?] ---> re-derive Reflectivity knob
    |                      +--- [Buffer apply?] --> re-clamp all m-prefix
    |
    +--- [Link Toggles] ----> Room Size propagates to echo speeds when active
    +--- [Constrain Toggle] -> Dynamic ceiling = min(member.ceiling, wall_boundary)
```

### Recommended Project Structure

New/modified files for Phase 21:

```
include/spu94/
    spu94_macro.h              # MODIFY: expand group enum, add Spread+Sweep types,
                               #   bipolar apply, reference values, link/constrain API
src/spu94/
    spu94_macro.c              # MODIFY: bipolar apply path, Spread+Sweep apply
    spu94_macro_controls.c     # NEW: all macro group definitions (static const tables),
                               #   registration, derive-all, link/constrain logic
    spu94_state_internal.h     # MODIFY: add reference values, link toggle state, constrain flags
tests/unit/macro/
    test_macro_spread_sweep.c  # NEW: Spread+Sweep dual-axis behavior
    test_macro_bipolar.c       # NEW: bipolar apply for Decay, Reflectivity, signed sweeps
    test_macro_controls.c      # NEW: group definition registration, derive-all
    test_macro_coupling.c      # NEW: Decay->Reflectivity coupling, Buffer->m-prefix re-clamp
    test_macro_link.c          # NEW: per-wall link toggles, same/cross link
    test_macro_constrain.c     # NEW: wall constrained mode for taps/diffusion
    CMakeLists.txt             # MODIFY: add new test targets
```

### Pattern 1: Spread+Sweep Dual-Axis Model

**What:** Each multi-register macro control has two axes -- Spread adjusts proportional spacing between registers, Sweep moves the entire set uniformly. Both axes operate relative to a reference snapshot (preset values or user overrides). [ASSUMED]

**When to use:** Every multi-register macro per D-01 (Echo Speed, Tap Position, Diffusion Amount/Texture/Position, Early Reflections).

**Design:**

The Spread axis controls how much the reference ratios are amplified or collapsed:
- Spread = 0.0: all registers collapse toward their group mean (or floor)
- Spread = 0.5 (center): registers at their reference proportions
- Spread = 1.0: ratios exaggerated to maximum gang-clamped extent

The Sweep axis shifts the entire set uniformly:
- For unsigned registers: Sweep [0,1] slides the set across the positive range
- For signed registers: Sweep is bipolar -- negative through zero through positive

Both axes compose: first apply Spread to get the proportional spacing, then apply Sweep to shift the result.

```c
// Source: project codebase analysis + D-01/D-25 decisions [ASSUMED design]

typedef struct {
    const char              *name;
    uint8_t                  member_count;
    spu94_macro_member_t     members[SPU94_MACRO_MAX_MEMBERS];
    uint8_t                  is_bipolar;    /* 0=unipolar, 1=bipolar sweep */
} spu94_macro_group_t;

/* Spread+Sweep apply: dual-axis control over a register group.
 * spread: [0.0, 1.0] where 0.5 = reference proportions
 * sweep:  [0.0, 1.0] for unipolar, [-1.0, 1.0] for bipolar
 *
 * Model:
 *   ref_offset[i] = reference_value[i] - floor[i]
 *   spread_factor = spread * 2.0  (so center=1.0 = reference)
 *   spread_value[i] = floor[i] + ref_offset[i] * spread_factor
 *   sweep_offset = sweep * max_sweep_headroom
 *   final[i] = spread_value[i] + sweep_offset
 *   (gang-clamped at floor/ceiling)
 */
spu94_result_t spu94_macro_apply_spread_sweep(
    spu94_state *state,
    spu94_macro_group_id_t group_id,
    float spread,
    float sweep);
```

### Pattern 2: Bipolar Apply Path

**What:** Signed registers (vIIR, vWALL, vCOMB1-4, vAPF1/2) can meaningfully traverse negative through zero through positive values. The Phase 20 unipolar [0,1] model can't represent this. [VERIFIED: D-19, D-22, D-25, D-30 in CONTEXT.md]

**When to use:** Decay (vIIR), Reflectivity (vWALL), Early Reflections Sweep (vCOMB1-4), Diffusion Amount Sweep (vAPF1/2).

**Design:**

For single-register bipolar controls (Decay, Reflectivity):
- knob_position: [-1.0, 1.0] where 0.0 = center detent (register value = 0)
- Left half [-1.0, 0.0]: maps linearly to [floor, 0]
- Right half [0.0, 1.0]: maps linearly to [0, ceiling]

For Decay specifically (D-19):
- floor = -0x1000 (SAFE-03), ceiling = 0x7FFF
- Left half: [-1.0, 0.0] -> [-0x1000, 0]
- Right half: [0.0, 1.0] -> [0, 0x7FFF]
- SAFE-04: -0x8000 unreachable because floor is -0x1000

```c
// Source: D-19 through D-24 decisions [ASSUMED design]

/* Bipolar apply for a single-register group.
 * position: [-1.0, 1.0]
 * Left half:  value = floor + (0 - floor) * (position + 1.0)
 * Right half: value = ceiling * position
 */
spu94_result_t spu94_macro_apply_bipolar(
    spu94_state *state,
    spu94_macro_group_id_t group_id,
    float position);  /* [-1.0, 1.0] */
```

### Pattern 3: Reference Values for Preset Derivation

**What:** When a preset is loaded, the register values become the "reference" for all macro computations. This is separate from the "base values" that Phase 20 snapshots during derive -- reference values persist as the proportional template, while base values are recalculated as macros are applied. [VERIFIED: D-02, D-28, D-29]

**Storage:**

```c
// Source: D-29 + existing state struct pattern [ASSUMED design]
// In spu94_state_internal.h:

int32_t macro_ref_values[SPU94_MACRO_MAX_GROUPS][SPU94_MACRO_MAX_MEMBERS];
```

At 16 groups x 16 members x 4 bytes = 1024 bytes. Headroom is 14,488 bytes. [VERIFIED: compiled sizeof check]

### Pattern 4: Decay-to-Reflectivity Coupling

**What:** When Decay changes vIIR, the stability ceiling (abs(vIIR) * abs(vWALL) <= 0x40000000) may constrain vWALL. The Reflectivity knob must immediately re-derive its position from the now-clamped vWALL. [VERIFIED: D-23, D-24; safety layer in spu94_safety.c]

**Design:** The coupling lives in the Decay apply path, not as a general post-write hook. After Decay writes vIIR through `spu94_safe_set_reg_i16`, the Reflectivity macro re-derives:

```c
// In the Decay apply path:
// 1. Write new vIIR via safe setter (may clamp vWALL via stability)
// 2. If Reflectivity group is registered:
//    a. Read actual vWALL (may have been clamped by stability enforcement)
//    b. Re-derive Reflectivity knob position from current vWALL
//    c. Update macro_knob_pos[REFLECTIVITY] to reflect truth
```

This is the ONLY cross-macro dependency (per CONTEXT.md). No other macro's apply triggers another macro's re-derivation.

### Anti-Patterns to Avoid

- **Separate "mode" flags for Spread vs Sweep:** Don't create two separate apply calls that the caller must sequence. The Spread+Sweep operation is atomic -- both axes compose in a single apply call.
- **Storing derived knob positions in preset files:** Presets are register-only (D-28). Macro positions are always derived from register state on load.
- **Float accumulation in tight loops:** The apply path iterates over group members (max 16). Using `float` for intermediate math is fine at this scale. Don't switch to integer-only Q15 math for the macro control layer -- that's for the DSP signal path, not the control surface.
- **Re-reading registers after each member write:** During a batch apply, the re-entrancy guard (`state->macro_writing = 1`) is already set. Don't read register state mid-batch; compute all values from base/reference snapshots, then write all.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Safety enforcement | Custom bounds checking in each macro apply | `spu94_safe_set_reg_{i16,u16}` (Phase 20) | Already handles stability ceiling and address bounds; all macro writes route through it |
| Register signedness | Manual type casting in group definitions | `spu94_reg_type()` lookup + `spu94_macro_member_t.type` field | Signedness is already classified for all 35 registers |
| Preset loading | Custom register iteration | `spu94_load_preset()` / `spu94_preset_load()` | Existing preset path handles write-timing policy; macro derive triggers after |

**Key insight:** Phase 20 already built the safety enforcement and basic proportional scaling. Phase 21 extends the engine (Spread+Sweep, bipolar, reference values) and defines the group tables -- it does NOT rebuild safety or proportional math.

## Macro Group Enumeration

The expanded control surface requires these distinct macro group slots. [VERIFIED: enumerated from all D-01..D-30 decisions]

### Spread+Sweep groups (2 knob axes each, 1 group slot each):

| # | Group | Registers | Signed | Notes |
|---|-------|-----------|--------|-------|
| 1 | Echo Speed | dLSAME, dRSAME, dLDIFF, dRDIFF | U16 | Unipolar sweep |
| 2 | Tap Position | mLCOMB1-4, mRCOMB1-4 | U16 | 8 members; unipolar sweep |
| 3 | Diffusion Amount | vAPF1, vAPF2 | I16 | Bipolar sweep |
| 4 | Diffusion Texture | dAPF1, dAPF2 | U16 | Unipolar sweep |
| 5 | Diffusion Position | mLAPF1, mRAPF1, mLAPF2, mRAPF2 | U16 | Unipolar sweep |
| 6 | Early Reflections | vCOMB1, vCOMB2, vCOMB3, vCOMB4 | I16 | Bipolar sweep |

### Single-knob groups (1 knob axis, 1 group slot each):

| # | Group | Registers | Type | Notes |
|---|-------|-----------|------|-------|
| 7 | Room Size | 14 m-prefix regs + conditionally 4 d-prefix when linked | U16 | Unipolar; conditional membership based on link toggles |
| 8 | Buffer | mBASE (single) | U16 | Inverted mapping; triggers m-prefix re-clamp |
| 9 | Decay | vIIR (single) | I16 | Bipolar center-detent; floor -0x1000, ceiling 0x7FFF |
| 10 | Reflectivity | vWALL (single) | I16 | Bipolar center-detent; stability-coupled ceiling |

### Not macro groups (individual register access):

| Control | Registers | Mechanism |
|---------|-----------|-----------|
| Wall distances (4) | mLSAME, mRSAME, mLDIFF, mRDIFF | Direct register setters; per-wall link propagates to echo speed |
| Individual taps (8) | mLCOMB1-4, mRCOMB1-4 | Direct register setters; override reference for Tap Position S+S |
| Individual diffusion positions (4) | mLAPF1, mRAPF1, mLAPF2, mRAPF2 | Direct register setters; override reference for Diff Position S+S |
| Individual vCOMB sliders (4) | vCOMB1-4 | Direct register setters; override reference for Early Reflections S+S |

**Total macro group slots needed: 10.** The existing `SPU94_MACRO_MAX_GROUPS = 8` must increase. Recommend bumping to 16 for headroom (Phase 22 snap integration may want its own group slots). [ASSUMED: 16 is adequate headroom]

### Group ID Enum Revision

The current enum has 8 slots with names that partially match Phase 21. It needs revision:

```c
// Current (Phase 20):
typedef enum {
    SPU94_MACRO_ROOM_SIZE       = 0,
    SPU94_MACRO_ECHO_PHYSICS    = 1,
    SPU94_MACRO_DECAY           = 2,
    SPU94_MACRO_REFLECTIVITY    = 3,
    SPU94_MACRO_WIDTH           = 4,  // D-27: REMOVED
    SPU94_MACRO_EARLY_REFL      = 5,
    SPU94_MACRO_DIFFUSION_AMT   = 6,
    SPU94_MACRO_DIFFUSION_TEX   = 7,
    SPU94_MACRO_GROUP__COUNT    = 8
} spu94_macro_group_id_t;

// Revised for Phase 21:
typedef enum {
    SPU94_MACRO_ECHO_SPEED      = 0,  // was ECHO_PHYSICS; renamed to match D-08
    SPU94_MACRO_TAP_POSITION    = 1,  // NEW (D-10)
    SPU94_MACRO_DIFF_AMOUNT     = 2,  // was DIFFUSION_AMT
    SPU94_MACRO_DIFF_TEXTURE    = 3,  // was DIFFUSION_TEX
    SPU94_MACRO_DIFF_POSITION   = 4,  // NEW (D-14)
    SPU94_MACRO_EARLY_REFL      = 5,  // unchanged
    SPU94_MACRO_ROOM_SIZE       = 6,  // was 0; renumbered
    SPU94_MACRO_BUFFER          = 7,  // NEW (D-16)
    SPU94_MACRO_DECAY           = 8,  // was 2; renumbered
    SPU94_MACRO_REFLECTIVITY    = 9,  // was 3; renumbered
    SPU94_MACRO_GROUP__COUNT    = 10
} spu94_macro_group_id_t;
```

Note: the old `SPU94_MACRO_WIDTH` (slot 4) is removed per D-27. No existing code depends on the numeric values of these enums (Phase 20 tests use the enum names, not literals). [VERIFIED: grep of test files shows enum name usage]

## State Struct Budget

Current: `sizeof(spu94_state) = 1896 bytes`, max = 16384. Headroom = 14,488 bytes. [VERIFIED: compiled sizeof check]

### New fields needed:

| Field | Size (bytes) | Purpose |
|-------|-------------|---------|
| `macro_ref_values[16][16]` | 1024 | Reference values (int32) for Spread+Sweep |
| `macro_spread_pos[16]` | 64 | Per-group spread knob position (float) |
| `macro_sweep_pos[16]` | 64 | Per-group sweep knob position (float) |
| `wall_link[4]` | 4 | Per-wall echo-speed-to-distance link toggle (uint8) |
| `same_cross_link` | 1 | Same/cross link toggle (uint8) |
| `tap_constrained` | 1 | Tap position constrained mode (uint8) |
| `diff_pos_constrained` | 1 | Diffusion position constrained mode (uint8) |

Expansion of existing arrays from [8] to [16]:

| Field | Old size | New size | Delta |
|-------|----------|----------|-------|
| `macro_group_defs[16]` | 64 | 128 | +64 |
| `macro_base_values[16][16]` | 512 | 1024 | +512 |
| `macro_knob_pos[16]` | 32 | 64 | +32 |

**Total new state: ~1767 bytes.** New total: ~3663 bytes. Still well within 16384. [ASSUMED: exact size depends on alignment padding]

## Common Pitfalls

### Pitfall 1: Spread+Sweep Interaction With Gang Clamping
**What goes wrong:** Spread exaggerates ratios, then Sweep pushes the set toward a ceiling. The most-constrained member hits its ceiling, but the gang clamp from Phase 20 was designed for single-axis scaling. With two axes, the "most constrained" member may differ between Spread and Sweep.
**Why it happens:** Phase 20's `compute_max_scale` assumes a single scale factor applied uniformly. Spread+Sweep applies two transformations in sequence.
**How to avoid:** Compute Spread-adjusted values first (new proportional spacing), then apply Sweep with its own gang-clamp calculation over the Spread-adjusted result. Two-pass within a single atomic apply.
**Warning signs:** Register values exceeding floor/ceiling; ratios drifting under combined Spread+Sweep.

### Pitfall 2: Circular Derivation on Preset Load
**What goes wrong:** Loading a preset sets registers, then derive-all is called. If derive changes any register (it shouldn't), it could trigger safety clamping, which changes the register, which makes the derive result wrong.
**Why it happens:** `spu94_macro_derive` in Phase 20 reads registers and computes knob position -- it does NOT write registers. But if a future change adds side effects to derive, this invariant breaks.
**How to avoid:** Derive must be read-only. Verify in tests: snapshot registers before derive-all, assert registers unchanged after.
**Warning signs:** Register values changing after `spu94_macro_derive` calls.

### Pitfall 3: Buffer Shrink Cascade
**What goes wrong:** User pulls Buffer knob down (mBASE increases), safety layer clamps m-prefix registers to the new boundary. All macro groups containing m-prefix registers (Room Size, Tap Position, Diffusion Position) now have different base values. Their knob positions are stale.
**Why it happens:** Buffer apply changes the safety boundary, which triggers re-clamping through the safety layer, but macro knob positions aren't automatically re-derived.
**How to avoid:** After Buffer apply, re-derive all macro groups that contain m-prefix registers. This is a Buffer-specific post-apply hook, similar to the Decay->Reflectivity coupling.
**Warning signs:** Knob positions showing incorrect values after Buffer changes; registers appearing to "jump" when a macro is next applied because base values are stale.

### Pitfall 4: vIIR Floor Enforcement Path
**What goes wrong:** The safety layer (spu94_safety.c) handles the stability ceiling (abs(vIIR) * abs(vWALL)) but does NOT enforce the -0x1000 floor on vIIR. That's a GUI/macro constraint, not a hardware safety constraint.
**Why it happens:** SAFE-03 says "vIIR GUI floor is -0x1000" -- this is a MACRO definition constraint (the Decay group's floor), not a safety-layer constraint. The safety layer only handles SAFE-01 (stability ceiling) and SAFE-02 (address bounds).
**How to avoid:** The Decay macro group definition sets `floor = -0x1000` on its vIIR member. The Phase 20 engine's `write_member_value` already clamps to member floor/ceiling before calling the safe setter. Raw mode can still set vIIR to values below -0x1000 -- SAFE-03 says "GUI floor" not "engine floor". The macro layer is the enforcement point.
**Warning signs:** Tests checking that raw register writes are clamped to -0x1000 -- they shouldn't be. Only macro writes clamp.

### Pitfall 5: Reference Values vs Base Values Confusion
**What goes wrong:** The Spread+Sweep model needs two concepts: (1) reference values (from preset, used to compute proportional ratios for Spread), and (2) base values (from last derive, used to compute current knob position). Confusing them means Spread doesn't preserve preset proportions, or derive gives wrong positions.
**Why it happens:** Phase 20 only has base values. Phase 21 adds reference values. If code accidentally uses one where the other is needed, the behavior is subtly wrong.
**How to avoid:** Name them distinctly (`macro_ref_values` vs `macro_base_values`). Document which is used where. Test with presets that have known ratios and verify Spread preserves them.
**Warning signs:** Spread at center (0.5) not reproducing the preset's original register ratios.

### Pitfall 6: Link Toggle Creates Variable Group Membership
**What goes wrong:** Room Size normally scales 14 m-prefix registers. When per-wall link toggles are active, it should also scale the corresponding echo speed d-prefix registers. This means the group's effective membership changes at runtime based on toggle state.
**Why it happens:** Phase 20's group definitions are `const` -- member_count is fixed at registration time.
**How to avoid:** Two approaches: (a) Register Room Size with all 18 registers (14 m-prefix + 4 d-prefix) and use the link toggle to zero the d-prefix members' scale factor when unlinked, or (b) use a separate helper that applies the Room Size scale factor to echo speeds conditionally after the main Room Size apply. Option (b) is simpler and doesn't require changing the engine's const group model.
**Warning signs:** Echo speed registers changing when link toggles are off; Room Size gang clamping affected by echo speed ceilings when they shouldn't be.

### Pitfall 7: Wall Constrained Mode Needs Dynamic Ceilings
**What goes wrong:** When tap constrained mode is active, each tap's ceiling should be the current value of its containing wall (mLSAME or mLDIFF for left taps, mRSAME or mRDIFF for right taps). But Phase 20's `spu94_macro_member_t.ceiling` is set at registration time and the group def is `const`.
**Why it happens:** The Phase 20 engine assumes static ceilings.
**How to avoid:** The constrained mode ceiling needs to be resolved at apply time, not at registration time. Either: (a) use a callback or function pointer for dynamic ceiling resolution, or (b) have the constrained-mode apply path read wall register values and clamp the computed result before writing. Option (b) keeps the engine simpler -- constrained mode is a post-computation clamp, not a change to the group definition.
**Warning signs:** Taps exceeding wall boundaries when constrained mode is on; constrained mode not updating when walls are moved.

## Code Examples

### Example 1: Macro Group Definition (Static Const Table)

```c
// Source: project pattern from Phase 20 test fixtures [VERIFIED: test_macro_derive.c]

/* Echo Speed group: 4 d-prefix registers, unipolar, Spread+Sweep. */
static const spu94_macro_group_t g_echo_speed_group = {
    .name = "Echo Speed",
    .member_count = 4,
    .is_bipolar = 0,
    .members = {
        { SPU94_REG_dLSAME, SPU94_REG_TYPE_U16, 0, 0xFFFF },
        { SPU94_REG_dRSAME, SPU94_REG_TYPE_U16, 0, 0xFFFF },
        { SPU94_REG_dLDIFF, SPU94_REG_TYPE_U16, 0, 0xFFFF },
        { SPU94_REG_dRDIFF, SPU94_REG_TYPE_U16, 0, 0xFFFF },
    }
};

/* Decay group: single register, bipolar, floor = -0x1000 (SAFE-03). */
static const spu94_macro_group_t g_decay_group = {
    .name = "Decay",
    .member_count = 1,
    .is_bipolar = 1,
    .members = {
        { SPU94_REG_vIIR, SPU94_REG_TYPE_I16, -0x1000, 0x7FFF },
    }
};

/* Early Reflections group: 4 vCOMB registers, bipolar sweep. */
static const spu94_macro_group_t g_early_refl_group = {
    .name = "Early Reflections",
    .member_count = 4,
    .is_bipolar = 1,
    .members = {
        { SPU94_REG_vCOMB1, SPU94_REG_TYPE_I16, -0x8000, 0x7FFF },
        { SPU94_REG_vCOMB2, SPU94_REG_TYPE_I16, -0x8000, 0x7FFF },
        { SPU94_REG_vCOMB3, SPU94_REG_TYPE_I16, -0x8000, 0x7FFF },
        { SPU94_REG_vCOMB4, SPU94_REG_TYPE_I16, -0x8000, 0x7FFF },
    }
};
```

### Example 2: Derive-All After Preset Load

```c
// Source: D-28/D-29 decisions + existing preset load pattern [ASSUMED design]

/* Called after spu94_load_preset or spu94_preset_load.
 * Snapshots register values as both reference AND base for all groups.
 * Does NOT modify any register -- read-only derivation.
 */
void spu94_macro_derive_all(spu94_state *state) {
    for (int g = 0; g < SPU94_MACRO_GROUP__COUNT; g++) {
        if (state->macro_group_defs[g] == NULL) continue;
        /* Snapshot current values as reference (for Spread ratios) */
        const spu94_macro_group_t *grp = state->macro_group_defs[g];
        for (int i = 0; i < grp->member_count; i++) {
            state->macro_ref_values[g][i] =
                read_member_value(state, &grp->members[i]);
        }
        /* Derive knob position from current state */
        spu94_macro_derive(state, (spu94_macro_group_id_t)g);
        /* Initialize spread to center (reference proportions), sweep to derive pos */
        state->macro_spread_pos[g] = 0.5f;
        /* sweep_pos derived from Phase 20's derive output */
    }
}
```

### Example 3: Buffer Apply With Re-Clamp

```c
// Source: D-16..D-18 + SAFE-02 pattern [ASSUMED design]

/* Buffer apply: inverted mapping + trigger m-prefix re-clamp.
 * position 1.0 = fully up = mBASE at minimum (0x0000, max space)
 * position 0.0 = fully down = mBASE at maximum (shrink buffer)
 */
spu94_result_t spu94_macro_apply_buffer(spu94_state *state, float position) {
    /* Invert: knob up = low mBASE value */
    uint16_t max_mbase = /* computed from work_buf_size */ 0xFFFF;
    uint16_t new_mbase = (uint16_t)((1.0f - position) * (float)max_mbase);

    /* Snap-on-write: mBASE is IMMEDIATE policy, takes effect instantly */
    spu94_result_t rc = spu94_set_reg_u16(state, SPU94_REG_mBASE, new_mbase);

    /* After mBASE changes, the effective buffer boundary changes.
     * Re-validate all m-prefix registers through the safety layer.
     * This triggers SAFE-02 clamping for any that now exceed the boundary. */
    /* ... re-write all m-prefix via safe setters ... */

    /* Re-derive all m-prefix-containing macro groups */
    /* ... */

    return rc;
}
```

## State of the Art

| Old Approach (Phase 20) | New Approach (Phase 21) | Impact |
|--------------------------|-------------------------|--------|
| Single-axis unipolar [0,1] apply | Dual-axis Spread+Sweep apply | All multi-register macros get two knobs instead of one |
| Single base_values snapshot | base_values + ref_values | Spread operates on reference, derive operates on base |
| 8 macro group slots | 16 macro group slots (10 used) | Enum revision; struct arrays grow |
| No bipolar support | Bipolar [-1,1] apply path | Decay, Reflectivity, and signed Sweeps |
| No cross-macro coupling | Decay->Reflectivity re-derive | Single cross-macro dependency |
| No link/constrain toggles | Boolean state flags + conditional apply | Wall link, same/cross link, tap/diff constrained mode |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Spread+Sweep composes as: apply Spread to get spacing, then Sweep to shift. Spread center = 0.5, not 1.0 | Architecture Patterns, Pattern 1 | Affects knob mapping; must match D-01 intent. Center behavior is well-defined in D-25 (center = preset weights), so the model fits. |
| A2 | SPU94_MACRO_MAX_GROUPS = 16 is adequate headroom | Macro Group Enumeration | If Phase 22 snap needs dedicated group slots, 16 may be tight. Easy to bump later if needed. |
| A3 | Room Size link-toggle approach uses a separate post-apply hook rather than variable membership | Pitfall 6 | If the engine is later extended for variable membership, this approach creates tech debt. But it's simpler now and avoids changing the const group model. |
| A4 | Wall constrained mode is a post-computation clamp rather than dynamic ceiling in the group def | Pitfall 7 | Same rationale as A3 -- keeps engine simpler, avoids changing const group semantics. |
| A5 | The `is_bipolar` field belongs on the group definition struct, not as a separate per-group flag in state | Architecture Patterns | Minor -- could go either way. Putting it on the group def keeps it with the other group metadata. |
| A6 | Total new state budget of ~1767 bytes is accurate within ~200 bytes | State Struct Budget | Alignment padding may increase it. Still well under 16384 cap regardless. |
| A7 | Buffer mBASE maximum value is derived from work_buf_size, not a fixed constant | Code Examples, Example 3 | Need to verify how mBASE interacts with work_buf_size. The safety layer already handles m-prefix bounds vs mBASE, but mBASE itself has its own range. |

## Open Questions

1. **How does mBASE interact with the safety layer's address bounds?**
   - What we know: SAFE-02 clamps m-prefix registers to `(work_buf_size / 2) - 1`. mBASE is explicitly excluded from the m-prefix mask in `spu94_safety.c` (line 42-65). On the real PS1, mBASE is the base address of the reverb work area; higher mBASE = less space for reverb.
   - What's unclear: What is the valid range for mBASE? Is it `[0, 0xFFFF]` or bounded by work_buf_size? The factory presets all have mBASE = 0x0000.
   - Recommendation: The Buffer macro's ceiling for mBASE should be `(work_buf_size / 2) - 1` (same formula as m-prefix bounds). After mBASE changes, the effective m-prefix ceiling becomes `(work_buf_size / 2) - 1 - mBASE` (or similar -- the reverb body's addressing is relative to mBASE). This needs verification against the reverb body's tap formulas.

2. **Does the Spread center exactly reproduce reference values?**
   - What we know: D-25 says "center = preset/user-defined proportional weights". D-01 says Spread controls "collapse <-> preset reference <-> exaggerated".
   - What's unclear: Whether Spread=0.5 means exactly `ref_value[i]` (identity) or whether the center is at Spread=1.0 with a different mapping.
   - Recommendation: Use Spread=0.5 = identity (reference proportions). This gives equal travel in both directions (collapse left, exaggerate right) and is the most intuitive center-detent mapping.

3. **How should the enum revision handle backward compatibility with Phase 20 tests?**
   - What we know: Phase 20 tests use enum names (`SPU94_MACRO_ROOM_SIZE`, etc.) not numeric values. The test group definitions are local to each test file.
   - What's unclear: Whether renumbering the enum will break any existing test.
   - Recommendation: Keep `SPU94_MACRO_ROOM_SIZE` as a valid enum name (just renumber it). Phase 20 tests don't depend on numeric values. The removed `SPU94_MACRO_WIDTH` should be deleted. Phase 20 tests that register test groups to specific IDs will need the ID to still exist -- verify they use `SPU94_MACRO_ROOM_SIZE` (slot 0 currently). Renumbering that to slot 6 is fine as long as the test still uses the enum name.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (vendored at tests/unit/vendor/Unity/) |
| Config file | tests/unit/CMakeLists.txt (add_subdirectory per module) |
| Quick run command | `ctest --test-dir build -R "test_macro" --output-on-failure` |
| Full suite command | `ctest --test-dir build --output-on-failure` |

### Phase Requirements -> Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| WALL-01..04 | Wall group definitions register correctly | unit | `ctest --test-dir build -R test_macro_controls` | Wave 0 |
| WALL-05 | Per-wall link propagates echo speed | unit | `ctest --test-dir build -R test_macro_link` | Wave 0 |
| WALL-06 | Same/cross link propagates across pairs | unit | `ctest --test-dir build -R test_macro_link` | Wave 0 |
| ECHO-SPD-01 | Echo Speed Spread+Sweep behavior | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| TAP-01 | 8 individual tap controls | unit | existing register tests | Existing |
| TAP-02 | Tap Position Spread+Sweep | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| TAP-03 | Constrained mode clamps within walls | unit | `ctest --test-dir build -R test_macro_constrain` | Wave 0 |
| DIFF-AMT-01 | Diffusion Amount S+S | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| DIFF-TEX-01 | Diffusion Texture S+S | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| DIFF-POS-01 | Diffusion Position S+S | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| DIFF-POS-02 | Diffusion Position constrained toggle | unit | `ctest --test-dir build -R test_macro_constrain` | Wave 0 |
| ROOM-01 | Room Size scales all m-prefix + linked d-prefix | unit | `ctest --test-dir build -R test_macro_controls` | Wave 0 |
| BUF-01 | Buffer inverted mapping, snap-on-write | unit | `ctest --test-dir build -R test_macro_controls` | Wave 0 |
| BUF-02 | Buffer shrink re-clamps m-prefix | unit | `ctest --test-dir build -R test_macro_coupling` | Wave 0 |
| SS-01 | Spread+Sweep dual-axis model | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| SS-02 | Preset reference as anchor | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| SS-03 | Signed registers sweep negative | unit | `ctest --test-dir build -R test_macro_bipolar` | Wave 0 |
| CTRL-01 | Decay bipolar center-detent | unit | `ctest --test-dir build -R test_macro_bipolar` | Wave 0 |
| CTRL-02 | Reflectivity bipolar + stability coupling | unit | `ctest --test-dir build -R test_macro_coupling` | Wave 0 |
| CTRL-03 | Early Reflections S+S | unit | `ctest --test-dir build -R test_macro_spread_sweep` | Wave 0 |
| SAFE-03 | vIIR floor -0x1000 | unit | `ctest --test-dir build -R test_macro_bipolar` | Wave 0 |
| SAFE-04 | -0x8000 unreachable | unit | `ctest --test-dir build -R test_macro_bipolar` | Wave 0 |
| PRESET-01 | Derive-all after preset load | unit | `ctest --test-dir build -R test_macro_controls` | Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build -R "test_macro" --output-on-failure`
- **Per wave merge:** `ctest --test-dir build --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/macro/test_macro_spread_sweep.c` -- covers SS-01, SS-02, SS-03, ECHO-SPD-01, TAP-02, DIFF-AMT-01, DIFF-TEX-01, DIFF-POS-01, CTRL-03
- [ ] `tests/unit/macro/test_macro_bipolar.c` -- covers CTRL-01, SAFE-03, SAFE-04
- [ ] `tests/unit/macro/test_macro_controls.c` -- covers WALL-01..04, ROOM-01, BUF-01, PRESET-01
- [ ] `tests/unit/macro/test_macro_coupling.c` -- covers CTRL-02, BUF-02
- [ ] `tests/unit/macro/test_macro_link.c` -- covers WALL-05, WALL-06
- [ ] `tests/unit/macro/test_macro_constrain.c` -- covers TAP-03, DIFF-POS-02
- [ ] `tests/unit/macro/CMakeLists.txt` update -- add all new test targets

## Sources

### Primary (HIGH confidence)
- `include/spu94/spu94_macro.h` -- Phase 20 engine API, group types, member struct, constants
- `src/spu94/spu94_macro.c` -- Phase 20 proportional scaling implementation, derive, apply, gang clamp
- `src/spu94/spu94_safety.c` -- Safety enforcement: stability ceiling (SAFE-01), address bounds (SAFE-02), m-prefix mask
- `include/spu94/spu94_registers.h` -- 35-register enum, signedness classification, write-timing policy
- `src/spu94/spu94_state_internal.h` -- State struct layout, macro arrays, all field sizes
- `src/spu94/spu94_presets.c` -- Factory preset register tables, mBASE = 0x0000 for all
- `.planning/phases/21-macro-controls/21-CONTEXT.md` -- All 30 implementation decisions
- `.planning/REQUIREMENTS.md` -- Requirement definitions and phase mappings
- Compiled `sizeof(spu94_state)` check: 1896 bytes of 16384 used

### Secondary (MEDIUM confidence)
- `.planning/phases/20-macro-engine-safety-core/20-CONTEXT.md` -- Phase 20 decisions (engine architecture baseline)
- `tests/unit/macro/` -- 4 existing test files confirming Phase 20 patterns
- `ctest` run confirming all 122 tests pass (4 macro + 2 safety included)

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH -- pure C core, no external deps, existing build infrastructure [VERIFIED: CMake files, test framework]
- Architecture: HIGH for group enumeration and state budget; MEDIUM for Spread+Sweep internal math (A1, A3, A4 are reasonable designs but unverified against D-01 intent)
- Pitfalls: HIGH -- identified from direct code inspection of Phase 20 engine and safety layer

**Research date:** 2026-05-04
**Valid until:** 2026-06-04 (stable C core, no external dependency drift)
