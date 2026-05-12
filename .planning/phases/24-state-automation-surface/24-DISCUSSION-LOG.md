# Phase 24: State & Automation Surface - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-12
**Phase:** 24-state-automation-surface
**Areas discussed:** Automation display units, State version policy, Morph position automation behavior

---

## Automation display units — Mix levels and sends

| Option | Description | Selected |
|--------|-------------|----------|
| Percent 0–100 | Simple, universal | ✓ |
| Decibels | Matches engineer mental model for levels | |

**User's choice:** Percent for all five mix knobs (Dry Level, ADPCM Level, Reverb Level, Dry Send, ADPCM Send).
**Notes:** Quick decision, no deliberation needed.

---

## Automation display units — Input Gain

| Option | Description | Selected |
|--------|-------------|----------|
| Raw number 0–16 | Matches internal range, no units | |
| Percent 0–100 with >100% as drive | Unity at 100%, drive territory above | |
| Decibels -∞ to +24 dB | Matches gain staging mental model | ✓ |

**User's choice:** Decibels.
**Notes:** Recording engineer thinks in dB for gain controls.

---

## Automation display units — Morph Speed and Morph Grit

| Option | Description | Selected |
|--------|-------------|----------|
| Percent 0–100 | Industry standard | ✓ |
| Raw internal number | Matches code range | |

**User's choice:** Percent. "Just do industry standard here please."

---

## Morph Position — automation behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Standard percent 0–100 | Absolute position, matches most plugins | ✓ (default) |
| Bipolar ±100 offset from dial position | Dial is anchor, automation swings around it | deferred |
| Waypoint names (stepped mode) | Show "Hall 2", "User Slot 3" etc. | discussed, scope TBD |

**User's choice:** Explored bipolar offset concept (dial as anchor, automation swings ±100 around current position). Discussion was cut short due to over-explanation. Defaulted to percent 0–100 for planning; bipolar offset captured as deferred idea.
**Notes:** User's original vision: "For smooth automation curves, it should be -100 to 0 to 100. Bipolar from where the user has placed the dial. They can draw curves and reach below their current setting, as well as to the top of the range from their current setting." Waypoint names only relevant for stepped automation. This is a creative direction worth revisiting.

---

## State container version policy

**User's choice:** "Whatever standard practice is" — Claude locked as refuse-and-default.

---

## Claude's Discretion

- Parameter ID naming convention (internal strings, never user-visible)
- State container binary format details (magic bytes, endianness)

## Deferred Ideas

- Bipolar morph offset automation — user's creative concept, deferred mid-discussion
- Visual clip indicator for int16 boundary (carried from Phase 23)
- Hide preset Save/Load buttons in plugin formats (carried from STATE.md)
