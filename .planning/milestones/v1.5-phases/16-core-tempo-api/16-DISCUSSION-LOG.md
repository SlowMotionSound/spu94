# Phase 16: Core Tempo API - Discussion Log

**Date:** 2026-05-02
**Areas discussed:** 4 of 4 selected

## Area 1: Which registers to snap

**Options presented:**
1. 6 real d-prefix registers only (simpler API)
2. 10 with virtual comb layer (creative extension) [RECOMMENDED]
3. All delay-like registers (20+, maximum flexibility)

**User asked:** What difference could a user expect between 6 vs 10?

**Explanation given:** 6 = rhythmic echo pattern (early reflections lock to beat). 10 = echoes AND reverb tail resonance sync to tempo (the room itself becomes rhythmic). Analogy: bathroom slap echo vs. the room's ring both reinforcing the groove.

**Decision:** 10 registers with virtual comb layer, BUT switchable — two independent group toggles (reflection sync, comb sync) so user can exclude comb resonances from tempo sync if desired.

**Follow-up — comb switch design:**
- Options: global toggle, per-register opt-in, two-group mode
- Decision: Two-group mode — "reflection sync" and "comb sync" each independently on/off

## Area 2: Snap behavior & binding

**Options presented:**
1. Persistent binding (BPM change auto-resnaps) [RECOMMENDED]
2. One-shot calculation (manual re-snap required)
3. Frozen until explicit recalculate

**User asked:** What behavior would the user experience from each choice?

**Explanation given:** Persistent = like a synced delay pedal, follows tempo. One-shot = like setting delay in milliseconds, stays put. Frozen = remembers binding but waits for commit.

**Decision:** Persistent binding.

**Follow-up — manual override behavior:**
- User asked: Can a manual register write move proportionally with BPM?
- This led to three register states: grid-bound, proportional (scales with tempo), fully-fixed
- Decision: Three states. Manual write → proportional (default). Explicit call → fully-fixed.

## Area 3: Overflow handling

**User asked:** Does this mean fewer options at slow BPMs?

**Explanation given:** Only affects extreme edge cases (sub-30 BPM + dotted whole notes). All useful subdivisions available at any realistic musical tempo (40-300 BPM).

**User suggested:** Build it so invalid subdivisions are simply unavailable at the given BPM, rather than clamping.

**Decision:** Reject invalid combos (register unchanged, error returned). Query function lets callers check what's valid at current BPM.

## Area 4: Float-free computation

**User asked:** How would each computation method affect the feel of sweeping BPM with a CC?

**Explanation given:** All three produce identical integer results. BUT the rounding behavior differs:
- Truncation = every delay is at or slightly shorter than true value (tight/forward, drummer on top of beat)
- Rounding = delays scatter evenly around true value (centered/neutral)

**User asked:** Could all three be available with smooth interpolation via an encoder?

**Explanation given:** Can't interpolate rounding modes (output is always integer). The concept is real but needs a proper timing offset parameter (±1-5ms range) to be perceptible. That's a lever layer feature.

**User asked:** How would the PS1 have done it?

**Answer:** PS1 MIPS R3000A has no FPU — integer division only, which truncates. Tight/forward character by necessity.

**Decision:** Pure integer math, truncation (PS1-faithful). Groove-feel offset deferred to lever layer.

## Deferred Ideas

- Groove-feel offset (timing bias ±1-5ms) — lever layer
- DAW host tempo sync — v1.6 plugin milestone
- Tempo-modulated delays with smooth transitions — lever layer

## Claude's Discretion Items

- Virtual comb-delay address computation internals
- Per-register binding state struct layout
- Error code choice for overflow
- Proportional state internal representation

---
*Discussion completed: 2026-05-02*
