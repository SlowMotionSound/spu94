# Phase 21: Macro Controls - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-04
**Phase:** 21-macro-controls
**Areas discussed:** Decay knob feel, Width knob model, Reflectivity coupling, Preset derivation edge cases, Full control surface redesign (Room Designer, Spread+Sweep model, link toggles, Buffer control)

---

## Decay Knob Feel

User stated directly: "Middle detent, negative travel, positive travel." Bipolar center-detent.

Follow-up about travel split confused user. Clarification: linear mapping within each half, negative zone maps fully to total negative value size. Each half maps to its full range.

**Decision:** Bipolar center-detent. Left = -0x1000 to 0, right = 0 to 0x7FFF.

---

## Width Knob Model

User stated directly: "all the way left = mono, center = original spacing, right = widest stereo image."

**Decision:** Bipolar center-detent with interpolation model. Later REMOVED — wall distance controls already cover stereo width.

---

## Reflectivity Coupling

| Option | Selected |
|--------|----------|
| Auto-rederive | ✓ |
| Silent clamp | |

User also clarified Reflectivity is bipolar (negative through positive), not unipolar as initially assumed.

**Decision:** Auto-rederive when Decay changes. Bipolar center-detent. Knob always shows truth.

---

## Preset Derivation Edge Cases

Three edge cases discussed:

**All registers at floor:** User said room size of zero isn't meaningful — "zero should have some minimum proportional relationship." Initially decided non-zero floors, later evolved into the interpolation model.

**Width pairs with no separation:** User specified interpolation: "At zero (mono) should spread linearly as you rotate the knob up to the standard spacing, then scale further linearly toward the max width."

**Cross-pollination:** User asked if the interpolation model applies to Room Size and Echo Physics too. Answer: yes. All multi-register groups use preset reference values as anchor, interpolate from minimum toward reference, scale beyond.

**Decay at zero:** User pointed out this resolves naturally from the bipolar decision. Not an edge case.

---

## Spread + Sweep Model (emerged from discussion)

Triggered by user question: "What if a user wants all passes, or any other control to function inversely proportional?"

User designed a two-knob model: Spread (proportional spacing between registers) + Sweep (move the whole set across its range). Initially called "Scale" for unsigned groups, user rejected the term: "Sweep makes sense. User sets a proportional relationship between settings, then sweeps the whole setting set across a range."

Applied universally to all multi-register groups.

---

## Room Designer (emerged from discussion)

### Walls
User designed the wall control model through iterative discussion:
- Left Wall = distance (mLSAME) + echo speed (dLSAME)
- Right Wall = distance (mRSAME) + echo speed (dRSAME)
- Left Cross = distance (mLDIFF) + echo speed (dLDIFF)
- Right Cross = distance (mRDIFF) + echo speed (dRDIFF)

User asked about same/cross tethering. Claude explained SPU allows physics-breaking (cross-side reflection arriving faster than same-side). User chose: "both" — independent by default, link toggle for physical realism.

Per-wall echo speed link toggle: user specified echo speed should link to wall distance when toggled.

### Echo Speed macro
Spread + Sweep over all 4 echo speeds. User confirmed.

### Tap Positions (mCOMB)
Claude initially proposed 4 paired controls. User rejected: "It would be genuinely useful to be able to place mLCOMB1 and mRCOMB1 oddly spaced apart." Decision: 8 individual controls.

User asked about naming. Claude suggested options. User proposed "Tap Position." Locked.

Wall constrained/unconstrained toggle. User specified: "Wall Constrained Combs, or Wall Unconstrained Combs (we can think of a better name later)."

Spread + Sweep macro on top. User confirmed.

### Diffusion section
User drove the three-part breakdown: Amount (vAPF), Texture (dAPF), Position (mAPF). All three in the Diffusion section, which lives under Room Designer.

Each sub-section gets Spread + Sweep. Position also gets 4 individual controls + constrained/unconstrained toggle.

### Room Size master
Scales all m-prefix registers proportionally. Also scales echo speeds when per-wall link toggles active.

### Buffer (mBASE)
User asked about mBASE. Claude explained it's memory management on PS1 (voice samples vs reverb buffer), not a reverb design parameter. All factory presets have mBASE = 0x0000.

User decided: expose as "Buffer" control. Up = max space (default), down = crush. Inverted mapping. Noted as future sequenceable effect.

### Width removal
User asked about overlap between Width macro and wall distance controls. Decision: "If width is already covered in the wall distance controls, then lose that arbitrary width control."

---

## Corrections during discussion

- Claude incorrectly stated dCOMB1-4 registers exist. They don't. Echo Physics reduced from 8 to 4 d-prefix registers (dLSAME, dRSAME, dLDIFF, dRDIFF).
- Claude repeatedly jumped ahead or re-asked decided questions, causing user frustration. User requested: "please be more clear and concise" and "chill please... dont jump ahead."
- Claude initially assumed Reflectivity was unipolar. User corrected: "bipolar is what I meant."
- Claude used AskUserQuestion modal despite memory note about text cutoff. User requested plain-text numbered lists.

## Claude's Discretion

- Concrete floor/ceiling values for each register in each group
- Bipolar engine integration approach
- Internal storage layout for reference values
- Reflectivity re-derive trigger mechanism
- Link toggle and constrained/unconstrained representation in C core
- Test organization and scenarios
- Whether SPU94_MACRO_MAX_GROUPS needs increasing

## Deferred Ideas

- mBASE "Buffer" as sequenceable effect
- Wall Constrained/Unconstrained naming refinement (Phase 23)
