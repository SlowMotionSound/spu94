---
gsd_state_version: 1.0
milestone: v1.12.0
milestone_name: Voice Count
status: executing
stopped_at: Phase 62 context gathered
last_updated: "2026-05-31T15:07:32.595Z"
last_activity: 2026-05-31 -- Phase 62 planning complete
progress:
  total_phases: 4
  completed_phases: 2
  total_plans: 4
  completed_plans: 3
  percent: 50
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-30)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Phase 62 — voice count selector

## Current Position

Phase: 62
Plan: Not started
Status: Ready to execute
Last activity: 2026-05-31 -- Phase 62 planning complete

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
- [Phase 60]: active voice count stored as std::atomic<int> activeVoiceCount{24} (release/acquire), clamped [1,24] in setActiveVoiceCount; lazy % count bounding in allocateVoice (no nextVoice re-base) so a count decrease self-heals on the next allocation.
- [Phase 60]: anti-click fade on voice steal DEFERRED (hard cut only); steal-click listening session not yet performed.
- [Phase 61-01]: noteVelocity[24] declared non-atomic audio-thread-only (duckOrigLevel precedent). Plan 02 recomputes base_vol = q15_mul_truncate(guiVol, noteVelocity[v]) every block so the Level fader rides on TOP of velocity (D-01), not overwriting it.
- [Phase 61-01]: spu94_voice_init seeds base_vol to 0x3FFF (NOT 0) -- this is the "not-yet-fanned-out" RED sentinel in test_voice_controls. Plan 02 GREEN target = flip the 6 count-sensitive cases (level/pan/non_pmon/velocity_rides/default24/out_of_range) while keeping guards adsr_shared + sweep_interaction green.
- [Phase 61-01]: adsr_shared + sweep_interaction are regression GUARDS (already-correct behavior per D-07 / Pitfall 4); they pass pre-impl and must stay green.
- [Phase 61-02]: applyContinuousVoiceControls() fan-out is a bounded [0,count) loop writing voices[v].base_vol_l/r = combineVoiceVol(guiVol, noteVelocity[v]) + set_non/set_pmon — atomic loads + O(1) setters only (RT-safe). Pitch stays voice-0-only; global noise_gen LFSR stays a single write. INV rides the sign of base_vol via q15_mul_truncate (D-03). Apply runs BEFORE the Phase 46 duck so the duck snapshots the Level-scaled ceiling (depth preserved); loop never touches sweep fields so in-flight sweep/duck survive.
- [Phase 61-02]: velocity unified onto the 0x3FFF scale — velToQ15 (0..127 -> 0..0x7FFF) then combineVoiceVol = q15_mul_truncate(guiVol, velQ15); full vel x full Level = 0x3FFE, so velocity-127 == Trigger-at-100% (D-08). Trigger seeds noteVelocity[0]=0x7FFF.

### Blockers/Concerns

None.

### Pending Todos

None for this milestone. (Project-wide to-dos live in `.planning/TODO.md`.)

## Deferred Items & Ideas

See `.planning/TODO.md` -- to-do list. Not carried in STATE.md.

## Session Continuity

Last session: 2026-05-31T14:39:52.847Z
Stopped at: Phase 62 context gathered
Resume file: .planning/phases/62-voice-count-selector/62-CONTEXT.md
Next action: Phase 61 verification (gsd-verifier) then phase completion; 2 non-blocking manual UAT ear-checks pending (D-06 voice-0 audition bit-identity; VCTRL-03 PMON-chain character).

## Operator Next Steps

- Phase 61 verification + completion in progress; then Phase 62 (selector GUI — user-facing 1-24 control + live re-sync)
- Manual UAT (non-blocking, listening checks): voice-0/Trigger audition A/B vs v1.11.0 (D-06); PMON-chain character across active set with count>=3 (VCTRL-03)
