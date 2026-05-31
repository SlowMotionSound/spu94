# Phase 63: Voice-Count Persistence - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-31
**Phase:** 63-voice-count-persistence
**Areas discussed:** Persistence surface (where the voice count is remembered)

---

## Persistence surface

Codebase scout found two independent persistence paths holding *different* state:
the `.spu94` text preset (`savePresetToString`/`loadPresetFromString`) carries the
full plugin-layer `[voice]` section (every per-voice control), while the binary
DAW/session container (`StateSerializer` / `getStateInformation`) carries only the
C-core engine + a 6-float morph appendix and **none** of the sampler GUI sections.

| Option | Description | Selected |
|--------|-------------|----------|
| 1. `.spu94` patches only | Save/restore the count in the `[voice]` section beside level/pan/NON/PMON/ADSR. Reload restores the count + updates the selector; old patches load as 24. Consistent with every other voice control. | ✓ |
| 2. That + auto-recall across DAW/app sessions | Also wire the count into the binary session state. More work; makes voice count the only sampler control that auto-recalls while the others don't; plugin has no visible control yet (Phase 62 deferral). | |

**User's choice:** Option 1 — `.spu94` patches only.
**Notes:** User picked the consistent path ("behave like everything else"). Binary
DAW/session-state persistence deferred until the broader "all sampler state in the
session blob" question is revisited. User accepted the two locked defaults below.

---

## Claude's Discretion

- Exact `[voice]` key name (`active_voices=` / `voice_count=`), int format, and
  placement within the section — following the `non=%d` precedent.
- Whether the GUI resync reads via a new accessor or the existing atomic, provided
  the read is message-thread-safe.

## Locked defaults (user confirmed, not contested)

- Back-compat: a patch with no count key restores to **24** (full rig).
- Restore routes through `setActiveVoiceCount` → clamps 1–24 and lets held notes
  ring out gracefully (Phase 60 behavior) rather than hard-cutting.

## Deferred Ideas

- Voice count in binary DAW/session state — revisit with the "all sampler state in
  session blob" question.
- DAW-plugin Voice Count control — still deferred from Phase 62 (plugin-beta milestone).
