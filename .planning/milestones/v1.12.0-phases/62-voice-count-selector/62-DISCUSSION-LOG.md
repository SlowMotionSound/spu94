# Phase 62: Voice-Count Selector - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-31
**Phase:** 62-voice-count-selector
**Areas discussed:** Control type, Placement, Surface, Label (Readout settled by the control choice)

---

## Control type

| Option | Description | Selected |
|--------|-------------|----------|
| Rotary knob | Matches the sampler's pitch/pan/FX knobs; sweepable to open polyphony live | |
| −/+ stepper | Two buttons + number; precise, deliberate, no fat-finger | |
| Vertical fader | Like the Level fader; sweepable, count read off position | |
| Dropdown selector | `juce::ComboBox` listing 1–24; plain discrete selection | ✓ |

**User's choice:** Dropdown selector (via "Other").
**Notes:** "This isn't a performance macro. It's simply select how many voices you need. And this is mostly for testing on the standalone anyway, so we don't need a ton of control coherence yet." Reframed the phase as a testing/utility control — clone the existing `recordModeBox` ComboBox pattern.

---

## Placement

| Option | Description | Selected |
|--------|-------------|----------|
| In the voice panel, next to the per-voice controls (pitch/level/pan) | Keeps the testing controls together; it governs those controls | ✓ |
| Set apart at the top of the sampler window | A separate, obvious "test" control | |

**User's choice:** Option 1 — in the voice panel next to the per-voice controls.
**Notes:** None.

---

## Surface

| Option | Description | Selected |
|--------|-------------|----------|
| Standalone-only | Sampler GUI lives in the standalone | |
| Both standalone and DAW plugin | Both are testing beds | ✓ |

**User's choice:** Both. "It can be in the plugin too, because that's another testing bed."
**Notes:** Corrected an earlier assumption that the control would be standalone-only. Surface-parity placement on the plugin is flagged for research (the per-voice panel carries a "standalone-only, Phase 31" comment).

---

## Label

| Option | Description | Selected |
|--------|-------------|----------|
| Voices | Plain, short | |
| Voice Count | Plain, explicit | ✓ |
| Polyphony | Synth-idiom term | |

**User's choice:** "Voice Count."
**Notes:** No Eurorack/PS1 framing on the label — keep it plain.

---

## Claude's Discretion

- Exact widget layout/sizing, 1..24 item-list construction, tooltip text, and any ComboBox/parameter plumbing needed to expose the control on the plugin surface — following the `recordModeBox` precedent.

## Deferred Ideas

- None. The user explicitly declined to track future polish of this control ("It's a given. Don't worry about it.").
