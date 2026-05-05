# Phase 18: I/O Surfaces - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-03
**Phase:** 18-i-o-surfaces
**Areas discussed:** CLI tempo flags, GUI tempo layout, Subdivision selector design, Sync group toggles

---

## CLI Tempo Flags

| Option | Description | Selected |
|--------|-------------|----------|
| BPM only | --tempo 120 sets BPM but doesn't snap anything | |
| BPM + global snap | --tempo 120 sets BPM AND snaps all registers to default subdivision | |
| BPM + explicit sub flag | --tempo 120 --subdivision 1/8 requires both flags | |

**User's choice:** None of the above exactly — user introduced the FREE/INT/EXT mode concept. Entering INT or EXT mode automatically snaps all sync-enabled registers to their assigned subdivisions. `--tempo 120` implies INT mode. No extra flags needed.

**Notes:** User clarified that "nearest subdivision" means snapping to the subdivision already assigned to each register (from preset or user selection), not searching all 15 subdivisions for the closest match. This is exactly what the existing auto-resnap machinery already does.

---

## GUI Tempo Layout

**User's choice:** Deferred to Claude's discretion. User clarified that this discussion is about what controls exist, not pixel layout — that's the planner/researcher's job.

---

## Subdivision Selector Design

**User's choice:** Per-register dropdown with three tiers:
- **Free** — unclocked, ignores tempo
- **Global** — follows the global subdivision setting (default when entering a synced mode)
- **Individual divisions** — 1/1 through 1/16, straight/dotted/triplet

**Notes:** User specified that when SPU94 enters a synced mode (INT or EXT), all registers default to Global. Individual overrides pull a register out of Global without affecting others.

---

## Sync Group Toggles

| Option | Description | Selected |
|--------|-------------|----------|
| Surface as GUI toggles | reflection_sync and comb_sync as toggle buttons | |
| Defer — per-register provides equivalent flexibility | Per-register dropdowns already let each register opt in/out | ✓ |

**User's choice:** Per-register dropdowns replace the need for group toggles in this phase. Group toggles can come later.

---

## EXT Mode / MIDI Clock

**Notes:** User specified that EXT mode should respond to MIDI clock input, not just future DAW host transport. MIDI clock works in standalone via USB-MIDI. User confirmed this is not a separate architecture — it's another source feeding the same `spu94_set_tempo` pipe. Folded into Phase 18 scope.

---

## Claude's Discretion

- GUI layout and zone placement
- MIDI device selection UI approach
- MIDI clock jitter smoothing strategy
- BPM field widget style
- Visual distinction between Free/Global/Individual in dropdowns
- Global subdivision control placement

## Deferred Ideas

- Sync group toggles (reflection_sync / comb_sync) — convenience feature for later
- DAW host tempo sync (AudioPlayHead) — v1.6 plugin milestone
- Tempo-modulated delays — lever layer feature
