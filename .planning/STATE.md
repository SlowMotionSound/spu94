---
gsd_state_version: 1.0
milestone: v1.12.0
milestone_name: Voice Count
status: planning
last_updated: "2026-05-30T22:09:16.245Z"
last_activity: 2026-05-30
progress:
  total_phases: 4
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-30)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** v1.12.0 Voice Count — Phase 60 (Engine Voice-Count & Allocation)

## Current Position

Phase: 60 — Engine Voice-Count & Allocation
Plan: —
Status: Roadmap created, ready to plan Phase 60
Last activity: 2026-05-30 — Roadmap for v1.12.0 created (Phases 60-63)

## Milestone History

| Milestone | Phases | Status | Shipped |
|-----------|--------|--------|---------|
| v1.11.0 Live Input Sampling | 56-59 (4 phases, 5 plans) | Archived | 2026-05-30 |
| v1.10.0 Voice Dynamics | 43-55 (13 phases, 20 plans) | Archived | 2026-05-28 |
| v1.9 Complete Voice | 33-42 (10 phases, 16 plans) | Archived | 2026-05-24 |
| v1.8 PSX Voice Engine | 27-32 (6 phases, 7 plans) | Archived | 2026-05-21 |
| v1.7 DAW Plugin Port | 21-26 (6 phases, 10 plans) | Archived | 2026-05-16 |

See `.planning/MILESTONES.md` for full history.

## Accumulated Context

### Decisions

Decisions are logged in PROJECT.md Key Decisions table.

v1.12.0 phase-structure rationale (4-phase dependency chain):
- Phase 60 (engine count + allocation) is the foundation — adds an active-voice-count concept and count-aware allocation with oldest-voice stealing + mono last-note priority. Today `allocateVoice` is a fixed `nextVoice = (nextVoice + 1) % 24` round-robin with no oldest-voice tracking (PluginProcessor.cpp ~line 2593).
- Phase 61 (coherent controls) fixes the voice-0-only scoping: the GUI apply block writes `voices[0]` / `pending_config[0]` only (PluginProcessor.cpp ~lines 863-885), and MIDI-played voices take volume from velocity and ignore the per-voice controls (note-on ~line 1426). Controls must fan out across `[0, active_voices)`.
- Phase 62 (selector GUI) is the user-facing 1–24 control + live re-sync; depends on 60 and 61 existing.
- Phase 63 (persistence) serializes the count into the existing `[voice]` INI section of plugin state (PluginProcessor.cpp ~line 1795), matching the `vol_l=` / `non=` pattern; default 24 for back-compat with pre-feature presets.

### Blockers/Concerns

None.

### Pending Todos

None for this milestone. (Project-wide to-dos live in `.planning/TODO.md`.)

## Deferred Items & Ideas

See `.planning/TODO.md` -- to-do list. Not carried in STATE.md.

## Session Continuity

Last session: 2026-05-30
Stopped at: v1.12.0 Voice Count roadmap created. 4 phases (60-63), all 10 requirements mapped (VCOUNT-01..04, VCTRL-01..03, VALLOC-01..03), 100% coverage. ROADMAP.md, REQUIREMENTS.md traceability, and STATE.md written. Phases 61/62/63 carry UI hints (sampler-window selector + control wiring).
Resume file: none.
Next action: Plan Phase 60 (Engine Voice-Count & Allocation) with /gsd:plan-phase 60.

## Operator Next Steps

- Plan Phase 60 with /gsd:plan-phase 60
