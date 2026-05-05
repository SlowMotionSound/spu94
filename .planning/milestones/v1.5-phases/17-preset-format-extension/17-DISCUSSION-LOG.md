# Phase 17: Preset Format Extension - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-03
**Phase:** 17-preset-format-extension
**Areas discussed:** What gets saved, Subdivision format, Section layout, Load-time behavior

---

## What Gets Saved

| Option | Description | Selected |
|--------|-------------|----------|
| Full fidelity | BPM + sync toggles + all 10 binding states + subdivisions + proportional ref BPMs. Preset restores the exact tempo setup. ~15 extra lines in the file. | ✓ |
| Musical essentials only | BPM + sync toggles + subdivisions for grid-bound registers only. Proportional/fixed registers just keep their raw register values. Simpler, but proportional scaling behavior is lost. | |
| Bare minimum per success criteria | BPM + subdivision names only. No sync toggles, no binding states. Meets the letter of TEMPO-05/06 but loses setup context. | |

**User's choice:** Full fidelity
**Notes:** None — straightforward selection.

---

## Subdivision Format

| Option | Description | Selected |
|--------|-------------|----------|
| Compact musical | 1/4, 1/8d, 1/16t — 'd' suffix for dotted, 't' for triplet. Matches how musicians talk. Short, unambiguous, easy to parse. | ✓ |
| Spelled out | 1/4, 1/8_dotted, 1/16_triplet — fully explicit, no abbreviation ambiguity. Longer lines but self-documenting. | |
| Numeric enum | Integer index (0-14) matching the C enum. Compact but meaningless to a human reading the file. | |

**User's choice:** Compact musical
**Notes:** Selected with preview showing the file format.

---

## Section Layout

| Option | Description | Selected |
|--------|-------------|----------|
| New [tempo] section | Dedicated section after [dac]. Groups all tempo state together. Clean separation. Existing v1.4 parser skips the unknown section header and all its keys. | ✓ |
| Flat keys in header area | Put tempo fields before any section. Simple, but mixes metadata with engine state. | |
| Inline in [registers] | Add _sub= keys right after each register's hex value. Keeps register and subdivision together, but breaks the clean one-value-per-register pattern. | |

**User's choice:** New [tempo] section
**Notes:** Selected with preview showing the file format.

---

## Load-time Behavior

| Option | Description | Selected |
|--------|-------------|----------|
| Auto-snap on load | Restore BPM + bindings, then recompute all grid-bound registers via spu94_set_subdivision. Guarantees BPM-to-register consistency even for hand-edited presets. | ✓ |
| Restore state only | Trust the file's hex values. BPM and bindings are metadata-only until user changes BPM. Hand-edited presets can have mismatched state. | |
| Snap to host BPM | Ignore saved BPM, keep engine's current BPM, resnap. Better suited for plugin milestone. | |

**User's choice:** Auto-snap on load
**Notes:** User identified that auto-snap is the true state reload — guarantees consistency. Initially recommended "Restore state only" which was the weaker option; user's clarifying question corrected the recommendation.

---

## Claude's Discretion

- Binding state string format in file (e.g. `grid`, `proportional`, `fixed`)
- Field ordering within [tempo] section
- Whether to add comment header in section
- Parser implementation details (extend section state machine)

## Deferred Ideas

None — discussion stayed within phase scope.
