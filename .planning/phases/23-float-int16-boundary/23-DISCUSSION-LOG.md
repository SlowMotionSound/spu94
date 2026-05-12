# Phase 23: Float↔int16 Boundary - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-11
**Phase:** 23-float-int16-boundary
**Areas discussed:** Input Gain placement, Input Gain range

---

## Input Gain placement

| Option | Description | Selected |
|--------|-------------|----------|
| Inside the door (current) | Input Gain applied as an int16 register inside the SPU engine, *after* the float→int16 clamp. Turning the knob down scales an already-clipped signal — no real headroom. | |
| Outside the door (new) | Input Gain applied as a float multiply *before* clamp + truncation. Below unity actually attenuates before clip; the -6 dB default finally means -6 dB of headroom. | ✓ |

**User's choice:** Outside.
**Notes:** Recording-engineer framing — the knob becomes a real preamp-style trim, not just a "scale what survived" volume.

---

## Input Gain range

| Option | Description | Selected |
|--------|-------------|----------|
| Cap at 1.0 (current) | Knob is purely an attenuator. Below 1.0 creates headroom; at 1.0 the signal hits the door at whatever level the DAW sends. Safe, attenuate-only. | |
| +6 dB drive (knob → 2.0) | Gentle drive. Slamming it fully just barely tickles the ceiling on hot signals. Subtle. | |
| +12 dB drive (knob → 4.0) | Preamp-style range. Adds clear saturation character on hot material, clean on quiet material. Familiar console feel. | |
| +24 dB drive (knob → ~16.0) | Fuzz-pedal territory. Even quiet signals can be slammed into the ceiling. Turns the chip's clip into a distortion stage. | ✓ |

**User's choice:** +24 dB.
**Notes:** Aggressive, character-forward. Matches the project's North Star — fixed-point quirks ARE the product. Treats the int16 boundary as a creative effect, not a defect to engineer around.

---

## Claude's Discretion

- **Module home / file location** — sibling under `src/plugin/`, header-only, or shared into `c_core/spu94/include/`. Zero sound/feel impact. Planner picks.
- **Module shape** — stateless free functions vs class with `prepare()/process()` lifecycle mirroring `SrcChain`. The converter has no state; class would be ceremony. Planner's call.
- **Standalone-testbed parity for pre-clamp Input-Gain** — standalone is internal dev-only per v1.7; planner decides whether to keep parity or let the plugin path diverge.

## Deferred Ideas

- Visual clip indicator (meter / LED) that lights when the boundary is clamping — Phase 23 makes the int16 boundary the dominant input-side distortion source, so visual feedback would be informative. UI surface change; revisit during the next UI-touching phase.
- Move Save/Load preset actions into a top-left panel dropdown — captured in `STATE.md` deferred ideas (2026-05-11), unrelated to Phase 23 but came up during the same session.
