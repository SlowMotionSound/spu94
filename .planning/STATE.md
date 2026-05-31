---
gsd_state_version: 1.0
milestone: v1.12.0
milestone_name: Voice Count
status: milestone_complete
stopped_at: Milestone complete (Phase 63 was final phase)
last_updated: 2026-05-31T18:13:50.156Z
last_activity: 2026-05-31 -- Phase 63 plan 01 complete; all v1.12.0 requirements satisfied
progress:
  total_phases: 4
  completed_phases: 4
  total_plans: 5
  completed_plans: 5
  percent: 100
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-30)

**Core value:** Reproduce the PS1 SPU reverb algorithm from spec -- sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't -- in a form that ports cleanly from desktop to hardware without a rewrite.
**Current focus:** Milestone complete

## Current Position

Phase: 63
Plan: Not started
Status: Milestone complete
Last activity: 2026-05-31

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
- [Phase 62-01]: Voice Count ComboBox (1-24, default 24, dontSendNotification) added to the standalone sampler panel; onChange -> processorRef.setActiveVoiceCount(getSelectedId()) with itemId==count (no ID arithmetic). Standalone-only by construction (joins samplerWindow->getPanel() + bounded inside if(samplerWindow)), zero plugin-surface code (D-03). Code COMPLETE (9e46e4a, 26c0d9a; clean Release build).
- [Phase 62-01]: Task 3 audible UAT (mono<->poly listening test) DEFERRED / pending human verification — no working MIDI controller on standalone under Linux (out of milestone scope), so the audible behavioral criteria (ROADMAP Phase 62 criteria 2-4) are NOT yet human-confirmed. Tracked as HUMAN-UAT for later. Code/build criteria pass; audible criteria unverified.
- [Phase 63-01]: active voice count persisted into the .spu94 [voice] text section as `active_voices=N` (D-01); binary StateSerializer/getStateInformation path left untouched (D-02 — DAW/session-state persistence of the count is deferred). Restore uses seed-then-override: `restoredCount=24` seeded before the parse loop (D-03 back-compat), the `active_voices` clause captures into that local (NOT a direct atomic store), and one `setActiveVoiceCount(restoredCount)` after the loop applies it through the clamp 1-24 + ring-out (D-04). No parser-side validation — the setter's clamp absorbs 0->1, 999->24, junk->1.
- [Phase 63-01]: getActiveVoiceCount() getter reads memory_order_acquire (pairs with setActiveVoiceCount's release store); the save line reads memory_order_relaxed (message-thread snapshot, matches sibling save lines); atomic stays private. Standalone voiceCountBox snaps to the restored count via setSelectedId(getActiveVoiceCount(), juce::dontSendNotification) appended to syncMixerKnobsFromProcessor (D-05 — flag suppresses the onChange feedback store). First headless coverage of the plugin text-preset round-trip (test_voice_persist 3/3); harness needed a prepareToPlay helper since loadPresetFromString early-returns until engines[0] is live. 24/24 regression green. VCOUNT-04 complete — last open v1.12.0 requirement. Commits 413a001, 6d4a9a4, 07f271b.

### Blockers/Concerns

None.

### Pending Todos

None for this milestone. (Project-wide to-dos live in `.planning/TODO.md`.)

## Deferred Items & Ideas

See `.planning/TODO.md` -- to-do list. Not carried in STATE.md.

## Session Continuity

Last session: 2026-05-31T17:44:35Z
Stopped at: Phase 63 plan 01 complete (VCOUNT-04 — voice-count persistence)
Resume file: None
Next action: v1.12.0 Voice Count milestone — all 10 requirements (VCOUNT-01..04, VCTRL-01..03, VALLOC-01..03) satisfied. Ready for milestone-completion / ship. Non-blocking manual UAT ear-checks still pending (carried, not gating).

## Operator Next Steps

- v1.12.0 Voice Count: all phases (60-63) complete. Ready to close out / ship the milestone.
- Manual UAT (non-blocking, listening checks, carried): Phase 62 mono<->poly audible test (no Linux MIDI controller); voice-0/Trigger audition A/B vs v1.11.0 (D-06); PMON-chain character across active set with count>=3 (VCTRL-03).
- Deferred (tracked, non-blocking): binary DAW/session-state persistence of the voice count (D-02 — .spu94 file persistence only for now).
