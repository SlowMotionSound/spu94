# Phase 22: Echo Speed + Diffusion Snap - Context

**Gathered:** 2026-05-04
**Status:** Ready for planning

<domain>
## Phase Boundary

Integrate the echo speed and diffusion texture macro controls with the v1.5 tempo system. Add a Sync/Free toggle that switches the echo speed section between continuous register values and discrete subdivision-based operation. In Sync mode, provide Sweep (shift all through table), Spread (unison → polyrhythmic), and Rotate (Euclidean permutation of assignments) macros operating on user-defined subdivision selections. dAPF1/dAPF2 get a simpler Sync/Free toggle with individual subdivision selection only.

</domain>

<decisions>
## Implementation Decisions

### Operating modes
- **D-01:** Binary Sync/Free toggle for the echo speed section. Free = continuous register values, no grid awareness. Sync = all 4 echo speed registers quantized to subdivision table positions.
- **D-02:** No per-register mode mixing. The entire echo speed section is either Sync or Free. Mixed state (some locked, some free) is a deferred idea.
- **D-03:** In Sync mode, user sets each register's subdivision via dropdown selectors. These selections become the reference state for macro operations.

### Sync mode macro controls
- **D-04:** Three macro controls in Sync mode: Sweep, Spread, Rotate.
- **D-05:** Sweep — shifts all 4 registers up/down the subdivision table together, maintaining their relative spacing. The whole rhythmic pattern gets faster or slower.
- **D-06:** Spread — controls how many different subdivisions are in play. All the way down = unison (all on same division). Turn up = registers move apart, each getting its own division (polyrhythmic).
- **D-07:** Rotate — Euclidean-style circular permutation of subdivision assignments across the 4 echo paths. The SET of subdivisions stays the same (defined by dropdown + Spread), but which physical echo path gets which timing rotates around.
- **D-08:** Macros operate relative to the user's dropdown selections (same reference anchor philosophy as Phase 21 D-02). Dropdowns define the rhythmic palette, macros transform it.

### Sweep range behavior
- **D-09:** Dynamic range mapping — full knob travel always maps to however much room the current Spread configuration allows. No dead zones, no wrapping.
- **D-10:** If Spread spans the full 15-position table, Sweep has no available range. This is a natural physical limit — user must narrow Spread to regain Sweep room. Accepted constraint.
- **D-11:** The subdivision table has 15 positions (1/1 through 1/16T with dotted and triplet variants). Controls are inherently discrete/stepped in Sync mode.

### Diffusion texture (dAPF1/dAPF2)
- **D-12:** dAPF1 and dAPF2 get Sync or Free toggle only. Individual dropdown subdivision selection when in Sync mode. No Sweep/Spread/Rotate macros — they're diffusion utility controls, not performance controls.

### Coexistence with v1.5 hard snap
- **D-13:** Existing GRID binding mode (hard-lock to one subdivision, auto-resnap on BPM change) remains unchanged. Sync mode is the new user-facing interface for rhythm-based echo timing. The underlying `spu94_set_subdivision()` API still works under the hood.
- **D-14:** In Sync mode, when BPM changes, all registers auto-update to their subdivision's new sample count (same behavior as existing GRID auto-resnap).

### Claude's Discretion
- Exact data structures for tracking per-register subdivision assignments in the state struct
- How Spread distributes subdivisions across registers (linear spacing in table indices, or weighted toward musically common divisions)
- Whether Rotate wraps naturally (circular buffer of indices) or needs edge handling
- How the Sync/Free toggle interacts with the Phase 21 echo speed Spread+Sweep macro (likely: Free mode uses Phase 21 continuous Spread+Sweep, Sync mode uses the new discrete Sweep/Spread/Rotate — mutually exclusive control sets)
- Test file organization

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Tempo system (v1.5 — Phases 16-19)
- `include/spu94/spu94.h` lines 525-604 — subdivision enum (15 values), tempo_reg enum (10 registers), binding state enum, set_subdivision/set_tempo/get_binding_state API
- `src/spu94/spu94_tempo.c` — auto-resnap loop, binding state tracking, re-entrancy guard pattern

### Macro engine (Phase 20-21)
- `include/spu94/spu94_macro.h` — macro group types, Spread+Sweep apply, derive API, SPU94_MACRO_MAX_GROUPS
- `src/spu94/spu94_macro.c` — Spread+Sweep apply, bipolar apply, reference value management
- `src/spu94/spu94_macro_controls.c` — echo speed group definition (g_echo_speed: dLSAME/dRSAME/dLDIFF/dRDIFF), diffusion texture group (g_diff_texture: dAPF1/dAPF2)

### State struct
- `src/spu94/spu94_state_internal.h` — existing tempo fields (tempo_bpm, tempo_subdivisions[], tempo_binding[]), macro fields (macro_group_defs[], macro_knob_pos[], macro_ref_values[])

### Planning
- `.planning/phases/21-macro-controls/21-CONTEXT.md` — Phase 21 decisions (reference anchor model D-02, echo speed group ownership D-08)
- `.planning/REQUIREMENTS.md` — SNAP-01, SNAP-02, SNAP-03 definitions

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `spu94_set_subdivision()`: already computes sample counts from BPM + subdivision enum. Phase 22 can call this for each register when in Sync mode.
- `spu94_tempo_reg_t` enum: already includes dLSAME, dRSAME, dLDIFF, dRDIFF, dAPF1, dAPF2 — the exact registers Phase 22 targets.
- `spu94_get_binding_state()`: per-register binding state tracking already exists.
- Auto-resnap loop in `spu94_tempo.c`: already iterates grid-bound registers on BPM change. Sync mode registers can hook into this.

### Established Patterns
- Per-register state arrays indexed by `spu94_tempo_reg_t` (tempo system): closest analog to per-register subdivision assignment tracking.
- Re-entrancy guard (`tempo_writing`): prevents hooks from firing during batch writes — Phase 22 batch operations (Sweep/Spread/Rotate all write multiple registers) should use the same guard.
- Reference anchor model (Phase 21): user-set values define the reference, macros transform relative to it. Phase 22 applies this to subdivision indices.

### Integration Points
- Echo speed group (`SPU94_MACRO_ECHO_SPEED`) in Phase 21: Free mode uses the existing continuous Spread+Sweep from Phase 21. Sync mode replaces it with discrete Sweep/Spread/Rotate.
- Diffusion texture group (`SPU94_MACRO_DIFF_TEXTURE`): same — Free mode uses Phase 21 continuous, Sync mode uses individual dropdown selection.
- The Sync/Free toggle likely adds a boolean to the state struct that controls which apply path is used.

</code_context>

<specifics>
## Specific Ideas

- Rotate is explicitly Euclidean-inspired — circular permutation of subdivision assignments, same concept as Euclidean rhythm rotation but applied to spatial echo timing instead of drum hits.
- In Sync mode, the dropdown selectors are the primary interface for defining rhythmic palette (not a testing UI — they're the "preset" that Sweep/Spread/Rotate transform).
- Spread as polyrhythm control is a potential selling point: one knob to go from "all echoes on the same beat" to "complex interlocking rhythm."
- The 15-position subdivision table creates inherently coarse/stepped behavior — this is accepted as a characteristic feel, not a limitation.

</specifics>

<deferred>
## Deferred Ideas

- **Mixed state (some echoes grid-locked, some free)** — viable creative territory. Revisit if Sync-only feels too vanilla. One echo anchored to beat while others drift freely.
- **Physics-based controller personalities** — future design identity element. Magnetic pull, inertia, spring-back behaviors as unique SPU-94 identity. Don't engineer now, but don't foreclose the architecture.
- **Interpolation between grid points** — if the discrete 15-position table feels too coarse/limited in practice, the fallback is: Sweep moves continuously between subdivision sample counts, grid points become detent landmarks on a smooth sweep rather than the only available positions. Revisit if controls feel choppy or unsatisfying.

</deferred>

---

*Phase: 22-echo-speed-diffusion-snap*
*Context gathered: 2026-05-04*
