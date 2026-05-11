# Phase 22: Echo Speed + Diffusion Snap - Research

**Researched:** 2026-05-04
**Domain:** Tempo-synced musical control integration (discrete subdivision snap for echo speed and diffusion texture registers)
**Confidence:** HIGH

## Summary

Phase 22 bridges the Phase 16-19 tempo system with the Phase 20-21 macro control surface. The core challenge is implementing a Sync/Free modal toggle that switches echo speed registers (dLSAME, dRSAME, dLDIFF, dRDIFF) and diffusion texture registers (dAPF1, dAPF2) between continuous analog behavior (Phase 21 Spread+Sweep macros) and discrete subdivision-quantized operation (Phase 16 tempo table). In Sync mode, three new macro transforms (Sweep, Spread, Rotate) operate on per-register subdivision assignments rather than raw sample counts.

The existing infrastructure is robust and well-suited for extension. The tempo system already provides `spu94_set_subdivision()` which computes sample counts from BPM + subdivision enum and writes to hardware registers with re-entrancy guarding. The macro system already defines the `SPU94_MACRO_ECHO_SPEED` and `SPU94_MACRO_DIFF_TEXTURE` groups. The Phase 22 work is primarily about adding a modal layer on top: when Sync mode is active, the three discrete macros (Sweep/Spread/Rotate) replace the existing continuous Spread+Sweep, and BPM changes trigger auto-resnap of all Sync-mode registers.

The 15-position subdivision table (sorted by descending sample count) has non-uniform spacing: gaps range from 115 samples at the fast end to 5512 samples at the slow end. This means Spread and Sweep operations on table indices produce musically non-linear but aurally satisfying results -- each step up or down represents a musically meaningful rhythmic change (dotted, triplet, halving/doubling).

**Primary recommendation:** Implement as a modal state machine with per-register subdivision assignment arrays, three discrete transform functions operating on table indices, and a composite apply that calls `spu94_set_subdivision()` for each register after computing the transformed indices. Re-use the existing tempo auto-resnap infrastructure for BPM change handling.

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- D-01: Binary Sync/Free toggle for the echo speed section. Free = continuous register values, no grid awareness. Sync = all 4 echo speed registers quantized to subdivision table positions.
- D-02: No per-register mode mixing. The entire echo speed section is either Sync or Free. Mixed state (some locked, some free) is a deferred idea.
- D-03: In Sync mode, user sets each register's subdivision via dropdown selectors. These selections become the reference state for macro operations.
- D-04: Three macro controls in Sync mode: Sweep, Spread, Rotate.
- D-05: Sweep -- shifts all 4 registers up/down the subdivision table together, maintaining their relative spacing. The whole rhythmic pattern gets faster or slower.
- D-06: Spread -- controls how many different subdivisions are in play. All the way down = unison (all on same division). Turn up = registers move apart, each getting its own division (polyrhythmic).
- D-07: Rotate -- Euclidean-style circular permutation of subdivision assignments across the 4 echo paths. The SET of subdivisions stays the same (defined by dropdown + Spread), but which physical echo path gets which timing rotates around.
- D-08: Macros operate relative to the user's dropdown selections (same reference anchor philosophy as Phase 21 D-02). Dropdowns define the rhythmic palette, macros transform it.
- D-09: Dynamic range mapping -- full knob travel always maps to however much room the current Spread configuration allows. No dead zones, no wrapping.
- D-10: If Spread spans the full 15-position table, Sweep has no available range. This is a natural physical limit -- user must narrow Spread to regain Sweep room. Accepted constraint.
- D-11: The subdivision table has 15 positions (1/1 through 1/16T with dotted and triplet variants). Controls are inherently discrete/stepped in Sync mode.
- D-12: dAPF1 and dAPF2 get Sync or Free toggle only. Individual dropdown subdivision selection when in Sync mode. No Sweep/Spread/Rotate macros -- they're diffusion utility controls, not performance controls.
- D-13: Existing GRID binding mode (hard-lock to one subdivision, auto-resnap on BPM change) remains unchanged. Sync mode is the new user-facing interface for rhythm-based echo timing. The underlying `spu94_set_subdivision()` API still works under the hood.
- D-14: In Sync mode, when BPM changes, all registers auto-update to their subdivision's new sample count (same behavior as existing GRID auto-resnap).

### Claude's Discretion
- Exact data structures for tracking per-register subdivision assignments in the state struct
- How Spread distributes subdivisions across registers (linear spacing in table indices, or weighted toward musically common divisions)
- Whether Rotate wraps naturally (circular buffer of indices) or needs edge handling
- How the Sync/Free toggle interacts with the Phase 21 echo speed Spread+Sweep macro (likely: Free mode uses Phase 21 continuous Spread+Sweep, Sync mode uses the new discrete Sweep/Spread/Rotate -- mutually exclusive control sets)
- Test file organization

### Deferred Ideas (OUT OF SCOPE)
- Mixed state (some echoes grid-locked, some free)
- Physics-based controller personalities (magnetic pull, inertia, spring-back)
- Interpolation between grid points (continuous sweep with grid detents)
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| SNAP-01 | Per-register magnetic snap -- each echo speed register (dLSAME, dRSAME, dLDIFF, dRDIFF) has independent snap behavior (Free / Global / specific subdivision) | Sync/Free toggle + per-register dropdown subdivision assignment array in state struct; `spu94_set_subdivision()` handles the actual snap computation |
| SNAP-02 | Smooth knob travel between snap points -- registers scale freely, snapping only within a pull zone near each subdivision grid value | Implemented via the modal Sync/Free toggle: in Free mode the existing continuous Spread+Sweep applies; in Sync mode registers are always quantized to table positions (discrete steps, not smooth-with-pull-zones). The CONTEXT.md D-01 decision clarifies this is a binary toggle, not a magnetic pull model. |
| SNAP-03 | dAPF1 and dAPF2 have independent snap behavior grouped with Diffusion controls | Separate Sync/Free toggle for diffusion texture + per-register dropdown assignment for dAPF1/dAPF2 |
</phase_requirements>

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Sync/Free toggle state | C core (state struct) | -- | All DSP state lives in C core per project decision |
| Per-register subdivision assignment | C core (state struct) | -- | Extends existing tempo_bind_sub[] pattern |
| Sweep/Spread/Rotate transforms | C core (new functions) | -- | Pure integer/index math on subdivision table |
| Auto-resnap on BPM change | C core (spu94_tempo.c) | -- | Extends existing auto-resnap loop |
| Mode-aware macro apply | C core (new composite apply) | -- | Routes to continuous or discrete path based on toggle |
| Dropdown UI selectors | Future GUI (Phase 23) | -- | Phase 22 provides the C API; Phase 23 wires to JUCE |

## Standard Stack

### Core
| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Unity (test framework) | existing | Unit testing | Already in use for all Phase 16-21 tests [VERIFIED: codebase] |
| CMake | 3.22+ | Build system | Already in use [VERIFIED: codebase] |
| C11 (no extensions) | -- | Implementation language | Project-wide standard [VERIFIED: CMakeLists.txt] |

### Supporting
No new external libraries needed. Phase 22 is purely internal C core logic extending existing subsystems.

## Architecture Patterns

### System Architecture Diagram

```
                     User Intent (GUI - Phase 23)
                              |
                    +---------v----------+
                    |  Sync/Free Toggle  |
                    +----+----------+----+
                         |          |
              +----------v--+  +---v-----------+
              | FREE MODE   |  | SYNC MODE     |
              | Phase 21    |  | Phase 22      |
              | Spread+Sweep|  | Discrete      |
              | (continuous)|  | Sweep/Spread/ |
              |             |  | Rotate        |
              +------+------+  +-------+-------+
                     |                 |
                     |    +------------v-----------+
                     |    | Per-register dropdown  |
                     |    | subdivision assignment |
                     |    | (reference state)      |
                     |    +------------+-----------+
                     |                 |
                     |    +------------v-----------+
                     |    | Transform: compute     |
                     |    | final table indices    |
                     |    +------------+-----------+
                     |                 |
                     v                 v
              +------+-----------------+------+
              |  spu94_set_reg_u16 (FREE)     |
              |  spu94_set_subdivision (SYNC) |
              +---------------+---------------+
                              |
                     +--------v--------+
                     | Hardware regs   |
                     | (state struct)  |
                     +-----------------+
                              |
              +---------------v---------------+
              | BPM Change --> Auto-resnap    |
              | (only SYNC-mode registers)    |
              +-------------------------------+
```

### Recommended Project Structure

New files:
```
src/spu94/
    spu94_snap.c           # Sync/Free toggle, dropdown assignment, Sweep/Spread/Rotate,
                           # mode-aware composite apply, BPM change hook integration
include/spu94/
    spu94_snap.h           # Public API: toggle, assignment, transform, apply functions
tests/unit/snap/
    test_snap_toggle.c     # Sync/Free mode switching
    test_snap_sweep.c      # Discrete Sweep transform
    test_snap_spread.c     # Discrete Spread transform
    test_snap_rotate.c     # Euclidean Rotate transform
    test_snap_bpm.c        # Auto-resnap on BPM change in Sync mode
    test_snap_diffusion.c  # dAPF1/dAPF2 Sync/Free + dropdown
    CMakeLists.txt
```

### Pattern 1: Modal Apply (Sync/Free routing)

**What:** A composite apply function that checks the Sync/Free toggle and dispatches to the appropriate path -- continuous Spread+Sweep (Phase 21) in Free mode, or discrete Sweep/Spread/Rotate in Sync mode.

**When to use:** Any time the echo speed or diffusion texture macro is applied.

**Example:**
```c
// Source: Architecture design based on existing composite apply patterns
// (spu94_macro_apply_room_size, spu94_macro_apply_tap_position)
spu94_result_t spu94_snap_apply_echo_speed(spu94_state *state,
                                            float spread,
                                            float sweep) {
    if (state == NULL) return SPU94_INVALID_STATE;

    if (!state->echo_speed_sync) {
        /* FREE mode: delegate to Phase 21 continuous Spread+Sweep */
        return spu94_macro_apply_spread_sweep(
            state, SPU94_MACRO_ECHO_SPEED, spread, sweep);
    }

    /* SYNC mode: compute transformed subdivision indices, then
     * call spu94_set_subdivision for each register */
    // ... discrete transform logic ...
    return SPU94_OK;
}
```

### Pattern 2: Discrete Index Transform (Sweep)

**What:** Shift all 4 registers' subdivision indices up or down the table together, preserving relative spacing. Clamped at table boundaries.

**When to use:** Sweep knob in Sync mode.

**Example:**
```c
// Source: Design based on D-05, D-09, D-10 decisions
// sweep_offset: integer steps to shift (computed from knob position + available range)
static void compute_sweep_indices(const uint8_t *ref_indices,     /* 4 dropdown selections */
                                  const uint8_t *spread_indices,  /* after Spread applied */
                                  int sweep_offset,
                                  uint8_t *out_indices) {         /* 4 final indices */
    for (int i = 0; i < 4; i++) {
        int idx = (int)spread_indices[i] + sweep_offset;
        /* Clamp to [0, 14] (table bounds) */
        if (idx < 0) idx = 0;
        if (idx > 14) idx = 14;
        out_indices[i] = (uint8_t)idx;
    }
}
```

### Pattern 3: Discrete Index Transform (Spread)

**What:** Controls how many different subdivisions are in play. At minimum, all registers collapse to the same subdivision (unison). At maximum, registers spread apart to maximize polyrhythmic content.

**When to use:** Spread knob in Sync mode.

**Example:**
```c
// Source: Design based on D-06 decision
// spread_factor: 0.0 = unison (all same), 1.0 = maximum separation
// ref_indices[4]: user dropdown selections (the reference palette)
static void compute_spread_indices(const uint8_t *ref_indices,
                                   float spread_factor,
                                   uint8_t *out_indices) {
    /* Compute reference centroid (mean index) */
    float centroid = 0.0f;
    for (int i = 0; i < 4; i++) centroid += (float)ref_indices[i];
    centroid /= 4.0f;

    for (int i = 0; i < 4; i++) {
        float offset = (float)ref_indices[i] - centroid;
        float spread_val = centroid + offset * spread_factor;
        /* Round to nearest integer, clamp to [0, 14] */
        int idx = (int)(spread_val + 0.5f);
        if (idx < 0) idx = 0;
        if (idx > 14) idx = 14;
        out_indices[i] = (uint8_t)idx;
    }
}
```

### Pattern 4: Euclidean Rotate

**What:** Circular permutation of subdivision assignments across the 4 echo paths. The set of active subdivisions stays fixed, but which physical path gets which timing rotates.

**When to use:** Rotate knob in Sync mode.

**Example:**
```c
// Source: Design based on D-07 decision (Euclidean permutation)
// rotation: integer 0-3 (4 positions for 4 registers)
static void compute_rotate_indices(const uint8_t *spread_indices,
                                   int rotation,
                                   uint8_t *out_indices) {
    for (int i = 0; i < 4; i++) {
        out_indices[i] = spread_indices[(i + rotation) % 4];
    }
}
```

### Pattern 5: Re-entrancy Guard for Batch Writes

**What:** When Sync mode transforms write multiple registers via `spu94_set_subdivision()`, the tempo system's write-interception hook must not trigger binding state transitions. Use the existing `tempo_writing` guard.

**When to use:** Any batch operation that calls `spu94_set_subdivision()` for multiple registers.

**Example:**
```c
// Source: Existing pattern in spu94_tempo.c (auto-resnap loop)
state->tempo_writing = 1;
for (int i = 0; i < 4; i++) {
    spu94_set_subdivision(state,
        echo_speed_tempo_regs[i],
        final_subdivisions[i]);
}
state->tempo_writing = 0;
```

Note: Actually, `spu94_set_subdivision()` already sets `tempo_writing` internally. If calling it in a loop, the guard is set/cleared per call. This is safe because the hook checks `tempo_writing` which is set at entry of each `set_subdivision` call. No additional outer guard needed unless we bypass `set_subdivision` and write registers directly.

### Anti-Patterns to Avoid

- **Operating on raw sample counts in Sync mode:** Always work with table indices (0-14) as the transform domain. Convert to sample counts only at the final `spu94_set_subdivision()` call. This keeps transforms BPM-independent.
- **Conflating binding states with Sync/Free toggle:** The existing `SPU94_BIND_GRID` state is the underlying mechanism. The Sync/Free toggle is a higher-level UI concept. Sync mode sets registers to GRID binding; Free mode leaves them at FIXED or PROPORTIONAL. Don't add a new binding state enum value.
- **Per-register mode mixing in Phase 22:** D-02 explicitly forbids this. The entire echo speed section toggles together. dAPF1/dAPF2 have their own toggle but also switch together.
- **Floating-point subdivision indices:** The table has 15 integer positions. All index math should be integer (or float rounded to int). No fractional indices -- that's the deferred "interpolation between grid points" idea.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Sample count from BPM+subdivision | Custom formula | `spu94_set_subdivision()` | Already handles integer truncation, overflow checking, register writes, binding state, re-entrancy guard |
| Auto-resnap on BPM change | New BPM-change hook | Existing `spu94_set_tempo()` resnap loop | Already iterates GRID-bound registers and resnaps. Sync-mode registers will naturally be GRID-bound. |
| Register write safety | Direct `reg_values[]` assignment | `spu94_set_reg_u16()` / `spu94_safe_set_reg_u16()` | TICK_LATCHED policy, write hooks, safety clamping |
| Circular permutation | Complex rotation logic | Simple `(i + rotation) % 4` | Only 4 registers, no edge cases beyond modular arithmetic |

**Key insight:** The tempo system already does the heavy lifting. Phase 22 is a layer of index arithmetic on top of `spu94_set_subdivision()`, not a parallel system.

## Common Pitfalls

### Pitfall 1: Sweep Range Depends on Spread State
**What goes wrong:** Sweep transform applied without considering current Spread configuration, causing registers to overshoot table boundaries.
**Why it happens:** D-09/D-10 define dynamic range mapping -- Sweep's available range depends on how much table space Spread has consumed.
**How to avoid:** Compute Sweep range AFTER Spread: find the min and max indices produced by Spread, compute remaining headroom in each direction, limit Sweep offset to that headroom.
**Warning signs:** Registers clamped at index 0 or 14 unexpectedly; knob appears to have dead zones despite D-09 promising none.

### Pitfall 2: Unison Collapse Creates Ambiguous Rotation
**What goes wrong:** When Spread=0 (unison, all on same subdivision), Rotate has no audible effect but the system tracks rotated indices anyway, causing confusion when Spread increases again.
**Why it happens:** Rotating 4 copies of the same index produces the same array.
**How to avoid:** Accept this as correct behavior -- Rotate is a no-op at unison. When Spread increases, rotation becomes audible again. Store rotation position independently of whether it's currently audible.
**Warning signs:** None -- this is the correct behavior, but tests should verify it.

### Pitfall 3: BPM Change Auto-Resnap Conflicts with Macro State
**What goes wrong:** BPM change triggers auto-resnap via `spu94_set_tempo()`, which resnaps registers to their stored subdivision. But if Sweep/Spread/Rotate have transformed the indices away from the dropdown reference, the resnap uses the wrong subdivision.
**Why it happens:** Existing `tempo_bind_sub[]` stores one subdivision per register. Transforms produce different effective subdivisions that need to be the resnap targets.
**How to avoid:** When Sync mode transforms produce final indices, update `tempo_bind_sub[]` to reflect the CURRENT effective subdivision (not the dropdown reference). This way auto-resnap uses the correct (transformed) subdivision.
**Warning signs:** BPM change snaps registers back to dropdown values, ignoring Sweep/Spread/Rotate positions.

### Pitfall 4: Toggle Transition Loses State
**What goes wrong:** Switching from Sync to Free mode loses the register values (snaps to some continuous Spread+Sweep position). Switching from Free to Sync mode disrupts the current register values by forcing subdivision quantization.
**Why it happens:** Modal transitions without value preservation.
**How to avoid:** Sync->Free: keep current register values as-is (they become the Spread+Sweep base). Re-derive continuous knob positions from current state. Free->Sync: quantize each register to the nearest subdivision (or use the dropdown selections). Either way, minimize audible jump at transition.
**Warning signs:** Audible click or sudden value jump when toggling Sync/Free.

### Pitfall 5: Sorted vs. Enum-Order Table Indices
**What goes wrong:** Treating enum indices (0=1/1, 14=1/16T) as monotonically increasing/decreasing in sample count. They're NOT -- dotted notes are longer than the next straight value.
**Why it happens:** The table order is: straight, dotted, triplet for each rhythmic tier. Sorted by sample count at 120 BPM: 1/1d > 1/1 > 1/2d > 1/1t > 1/2 > 1/4d > 1/2t > 1/4 > 1/8d > 1/4t > 1/8 > 1/16d > 1/8t > 1/16 > 1/16t.
**How to avoid:** For Sweep (shift the pattern faster/slower), sort by sample count and operate on the sorted position, not the raw enum index. Alternatively, define a "musical order" lookup that maps position 0-14 to the sorted-by-duration sequence.
**Warning signs:** Sweep "up" doesn't always make all echoes slower; some jump unexpectedly because dotted/triplet interleaving.

### Pitfall 6: Spread Float-to-Int Rounding Creates Duplicate Indices
**What goes wrong:** At moderate Spread values, two registers round to the same table index, reducing the effective polyrhythmic diversity below what the user expects.
**Why it happens:** With only 15 table positions and 4 registers, rounding collisions are inevitable at certain Spread values.
**How to avoid:** This is fundamentally acceptable per the design (15 discrete positions). Document it and test for it. The Spread knob feels "stepped" -- each step adds one more distinct subdivision.
**Warning signs:** None -- this is expected behavior for a 15-position discrete control.

## Code Examples

### State Struct Extension

```c
// Source: Design based on existing state struct patterns
// Add to spu94_state_internal.h:

/* Phase 22: Sync/Free state for echo speed and diffusion texture */
uint8_t        echo_speed_sync;         /* 0=Free, 1=Sync */
uint8_t        diff_texture_sync;       /* 0=Free, 1=Sync */

/* Per-register dropdown subdivision assignment (reference for transforms).
 * echo_speed: 4 registers (dLSAME, dRSAME, dLDIFF, dRDIFF).
 * diff_texture: 2 registers (dAPF1, dAPF2).
 * Values: 0-14 (spu94_subdivision_t) or 0xFF (unset). */
uint8_t        snap_echo_sub[4];        /* dropdown selections */
uint8_t        snap_diff_sub[2];        /* dropdown selections */

/* Sync mode knob positions */
float          snap_sweep_pos;          /* [0.0, 1.0] */
float          snap_spread_pos;         /* [0.0, 1.0] */
float          snap_rotate_pos;         /* [0.0, 1.0] maps to 0-3 rotation */

/* State budget: 2 + 6 + 12 = 20 bytes. Trivial addition. */
```

### Sorted Table for Musical Sweep Order

```c
// Source: Computed from subdivision table at any BPM
// (sort order is BPM-independent because all values scale linearly with 1/BPM)
//
// Sorted from longest (slowest echo) to shortest (fastest echo):
// idx 1 (1/1d) > idx 0 (1/1) > idx 4 (1/2d) > idx 2 (1/1t) > idx 3 (1/2)
// > idx 7 (1/4d) > idx 5 (1/2t) > idx 6 (1/4) > idx 10 (1/8d) > idx 8 (1/4t)
// > idx 9 (1/8) > idx 13 (1/16d) > idx 11 (1/8t) > idx 12 (1/16) > idx 14 (1/16t)

static const uint8_t subdivision_sorted_by_duration[SPU94_SUBDIVISION__COUNT] = {
    SPU94_SUB_1_1_DOTTED,   /* 0: longest */
    SPU94_SUB_1_1,          /* 1 */
    SPU94_SUB_1_2_DOTTED,   /* 2 */
    SPU94_SUB_1_1_TRIPLET,  /* 3 */
    SPU94_SUB_1_2,          /* 4 */
    SPU94_SUB_1_4_DOTTED,   /* 5 */
    SPU94_SUB_1_2_TRIPLET,  /* 6 */
    SPU94_SUB_1_4,          /* 7 */
    SPU94_SUB_1_8_DOTTED,   /* 8 */
    SPU94_SUB_1_4_TRIPLET,  /* 9 */
    SPU94_SUB_1_8,          /* 10 */
    SPU94_SUB_1_16_DOTTED,  /* 11 */
    SPU94_SUB_1_8_TRIPLET,  /* 12 */
    SPU94_SUB_1_16,         /* 13 */
    SPU94_SUB_1_16_TRIPLET, /* 14: shortest */
};

/* Reverse lookup: given a subdivision enum, what's its position in sorted order? */
static const uint8_t subdivision_to_sorted_pos[SPU94_SUBDIVISION__COUNT] = {
    1,   /* SPU94_SUB_1_1          -> sorted position 1 */
    0,   /* SPU94_SUB_1_1_DOTTED   -> sorted position 0 */
    3,   /* SPU94_SUB_1_1_TRIPLET  -> sorted position 3 */
    4,   /* SPU94_SUB_1_2          -> sorted position 4 */
    2,   /* SPU94_SUB_1_2_DOTTED   -> sorted position 2 */
    6,   /* SPU94_SUB_1_2_TRIPLET  -> sorted position 6 */
    7,   /* SPU94_SUB_1_4          -> sorted position 7 */
    5,   /* SPU94_SUB_1_4_DOTTED   -> sorted position 5 */
    9,   /* SPU94_SUB_1_4_TRIPLET  -> sorted position 9 */
    10,  /* SPU94_SUB_1_8          -> sorted position 10 */
    8,   /* SPU94_SUB_1_8_DOTTED   -> sorted position 8 */
    12,  /* SPU94_SUB_1_8_TRIPLET  -> sorted position 12 */
    13,  /* SPU94_SUB_1_16         -> sorted position 13 */
    11,  /* SPU94_SUB_1_16_DOTTED  -> sorted position 11 */
    14,  /* SPU94_SUB_1_16_TRIPLET -> sorted position 14 */
};
```

### Sweep Dynamic Range Computation

```c
// Source: Design based on D-09, D-10 constraints
// After Spread produces indices, compute how far Sweep can shift
static int compute_sweep_headroom(const uint8_t *spread_sorted_pos, int n) {
    /* Find the extreme positions in the sorted table */
    int min_pos = 14, max_pos = 0;
    for (int i = 0; i < n; i++) {
        if (spread_sorted_pos[i] < min_pos) min_pos = spread_sorted_pos[i];
        if (spread_sorted_pos[i] > max_pos) max_pos = spread_sorted_pos[i];
    }
    /* Headroom: how far up (toward longer) and down (toward shorter) */
    int headroom_up = min_pos;          /* positions available above highest */
    int headroom_down = 14 - max_pos;   /* positions available below lowest */
    /* Wait -- "up" in sorted order means toward index 0 (longer).
     * Sweep knob up = slower = shift toward sorted position 0.
     * The minimum-position register limits upward shift.
     * The maximum-position register limits downward shift. */
    return headroom_up + headroom_down; /* total available range */
}
```

### Toggle Transition (Sync->Free)

```c
// Source: Design based on Pitfall 4 prevention
spu94_result_t spu94_snap_set_echo_speed_sync(spu94_state *state, int sync) {
    if (state == NULL) return SPU94_INVALID_STATE;

    int was_sync = state->echo_speed_sync;
    state->echo_speed_sync = sync ? 1 : 0;

    if (was_sync && !sync) {
        /* Sync -> Free: current register values preserved.
         * Re-derive Phase 21 continuous Spread+Sweep positions from
         * current register state so knobs show truth. */
        spu94_macro_set_reference(state, SPU94_MACRO_ECHO_SPEED);
        float sp, sw;
        spu94_macro_derive_spread_sweep(state, SPU94_MACRO_ECHO_SPEED, &sp, &sw);

        /* Unbind from GRID so tempo changes don't resnap */
        for (int i = 0; i < 4; i++) {
            spu94_set_binding_fixed(state, echo_speed_tempo_regs[i]);
        }
    } else if (!was_sync && sync) {
        /* Free -> Sync: snap each register to its dropdown selection.
         * If no dropdown set (0xFF), snap to nearest subdivision. */
        state->tempo_writing = 1;
        for (int i = 0; i < 4; i++) {
            uint8_t sub = state->snap_echo_sub[i];
            if (sub < SPU94_SUBDIVISION__COUNT) {
                spu94_set_subdivision(state, echo_speed_tempo_regs[i],
                                     (spu94_subdivision_t)sub);
            }
        }
        state->tempo_writing = 0;
    }
    return SPU94_OK;
}
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Global GRID binding (Phase 16) | Per-register Sync mode with transforms (Phase 22) | Phase 22 (current) | GRID binding becomes the implementation detail; Sync mode is the user-facing concept |
| Single subdivision per register | Dropdown reference + transform (Sweep/Spread/Rotate) | Phase 22 (current) | Much richer rhythmic control from same 15-position table |
| Continuous-only echo speed control | Modal Sync/Free with seamless transitions | Phase 22 (current) | Users get both analog feel AND musical precision |

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Sweep should operate on duration-sorted table positions (not raw enum indices) | Architecture Patterns, Pitfall 5 | Sweep would produce non-intuitive jumps between musically unrelated subdivisions if using enum order |
| A2 | Spread uses linear spacing in sorted table indices (centroid + offset * factor) | Pattern 3 (Spread) | Could produce less musically interesting distributions; weighted alternatives may sound better |
| A3 | Rotate maps knob [0,1] to 4 discrete rotation positions (0,1,2,3) | Pattern 4 (Rotate) | With only 4 positions, continuous knob may feel awkward; stepped detent behavior may be needed |
| A4 | When Sync mode transforms produce final indices, updating `tempo_bind_sub[]` is the correct integration with auto-resnap | Pitfall 3 | If tempo system expects bind_sub to always match dropdown, auto-resnap could fight transforms |

## Open Questions (RESOLVED)

1. **Sweep sorted-table direction convention**
   - RESOLVED: Sweep=0 = slowest (sorted pos 0, longest delays), Sweep=1 = fastest (sorted pos 14, shortest delays), Sweep=0.5 = no shift from reference. Spatial convention chosen (higher knob = faster echoes) for intuitive "echo speed" naming.

2. **Spread behavior when dropdown selections are already spread out**
   - RESOLVED: Spread=0.5 reproduces dropdown reference exactly (spread_factor = spread * 2.0, matching Phase 21 convention). Spread=0.0 collapses to centroid (unison). Spread=1.0 doubles spacing from centroid where table allows.

3. **Rotate knob granularity**
   - RESOLVED: [0,1] maps to 4 rotation positions at 0.25 thresholds (0.0-0.249=rot0, 0.25-0.499=rot1, 0.5-0.749=rot2, 0.75-1.0=rot3). Stepped detent from continuous input.

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (ThrowTheSwitch) |
| Config file | tests/unit/snap/CMakeLists.txt (Wave 0 -- needs creation) |
| Quick run command | `cd build_test && make test_snap_toggle test_snap_sweep test_snap_spread test_snap_rotate test_snap_bpm test_snap_diffusion && ctest -R snap` |
| Full suite command | `cd build_test && make && ctest --output-on-failure` |

### Phase Requirements to Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| SNAP-01 | Each echo speed register independently configured (Free/Sync/specific subdivision) | unit | `ctest -R test_snap_toggle` | Wave 0 |
| SNAP-01 | Setting dLSAME=1/16, dRSAME=1/8T at 120 BPM produces different snap targets | unit | `ctest -R test_snap_sweep` | Wave 0 |
| SNAP-02 | Sweeping echo speed in Free mode moves registers continuously (Phase 21 Spread+Sweep) | unit | `ctest -R test_snap_toggle` (Free mode path) | Wave 0 |
| SNAP-02 | Sweeping echo speed in Sync mode quantizes to table positions | unit | `ctest -R test_snap_sweep` | Wave 0 |
| SNAP-03 | dAPF1/dAPF2 have independent snap via Sync/Free toggle + dropdown | unit | `ctest -R test_snap_diffusion` | Wave 0 |

### Sampling Rate
- **Per task commit:** `cd build_test && make test_snap_toggle test_snap_sweep test_snap_spread test_snap_rotate test_snap_bpm test_snap_diffusion && ctest -R snap --output-on-failure`
- **Per wave merge:** `cd build_test && make && ctest --output-on-failure`
- **Phase gate:** Full suite green before `/gsd-verify-work`

### Wave 0 Gaps
- [ ] `tests/unit/snap/CMakeLists.txt` -- test target definitions
- [ ] `tests/unit/snap/test_snap_toggle.c` -- covers SNAP-01 (mode switching, state preservation)
- [ ] `tests/unit/snap/test_snap_sweep.c` -- covers SNAP-01, SNAP-02 (discrete Sweep transform)
- [ ] `tests/unit/snap/test_snap_spread.c` -- covers SNAP-01 (discrete Spread, unison to polyrhythmic)
- [ ] `tests/unit/snap/test_snap_rotate.c` -- covers SNAP-01 (Euclidean rotation)
- [ ] `tests/unit/snap/test_snap_bpm.c` -- covers D-14 (auto-resnap in Sync mode)
- [ ] `tests/unit/snap/test_snap_diffusion.c` -- covers SNAP-03 (dAPF1/dAPF2 toggle + dropdown)
- [ ] `tests/unit/CMakeLists.txt` -- add `add_subdirectory(snap)` entry

## Sources

### Primary (HIGH confidence)
- `include/spu94/spu94.h` lines 525-604 -- subdivision enum, tempo_reg enum, binding state, API
- `src/spu94/spu94_tempo.c` -- full tempo implementation (auto-resnap, re-entrancy guard, set_subdivision)
- `include/spu94/spu94_macro.h` -- macro engine API (Spread+Sweep, bipolar, reference values)
- `src/spu94/spu94_macro.c` -- macro engine implementation (apply functions, derive)
- `src/spu94/spu94_macro_controls.c` -- echo speed group definition, diffusion texture group, composite apply patterns
- `src/spu94/spu94_state_internal.h` -- state struct layout, all tempo and macro fields
- `.planning/phases/22-echo-speed-diffusion-snap/22-CONTEXT.md` -- all 14 locked decisions

### Secondary (MEDIUM confidence)
- Subdivision table sample counts computed at 120 BPM (verified via Python formula matching `spu94_compute_delay_samples`)
- Duration-sorted table order derived from relative ratios (numerator/denominator comparison, BPM-independent)

### Tertiary (LOW confidence)
- None -- all findings verified against codebase sources.

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH - no new external dependencies, pure C core extension
- Architecture: HIGH - extends well-established existing patterns (tempo + macro systems)
- Pitfalls: HIGH - identified from actual codebase analysis (table ordering, auto-resnap interaction, mode transitions)

**Research date:** 2026-05-04
**Valid until:** Indefinite (internal C codebase, no external dependency drift)
