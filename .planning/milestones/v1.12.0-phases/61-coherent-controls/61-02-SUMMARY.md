---
phase: 61-coherent-controls
plan: 02
subsystem: plugin-audio
tags: [juce, voice-mixer, q15, fan-out, velocity, base-vol, non, pmon, rt-safe, tdd-green]

# Dependency graph
requires:
  - phase: 61-coherent-controls (plan 01)
    provides: "friend struct VoiceControlsTest seam, int16_t noteVelocity[24] array, void applyContinuousVoiceControls() no-op stub, and the 8-case RED test_voice_controls target"
  - phase: 60-engine-voice-count-allocation
    provides: "std::atomic<int> activeVoiceCount{24} — the fan-out bound, read with memory_order_acquire"
provides:
  - "applyContinuousVoiceControls() real fan-out: Level/Pan/INV (base_vol) + NON + PMON across [0, activeVoiceCount)"
  - "per-note velocity capture at MIDI note-on into noteVelocity[24] (D-01)"
  - "velToQ15 + combineVoiceVol range-reconciliation helpers unifying velocity (0x7FFF) and GUI (0x3FFF) full-scales onto base_vol (D-08)"
  - "Trigger/voice-0 audition seeds noteVelocity[0]=0x7FFF so the audition stays exactly at the Level fader"
  - "locked apply-before-duck ordering documented at the processBlock call site (Pitfall 4)"
affects: [coherent-controls, voice-dynamics, future-internal-mod-bus]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Per-block continuous-control fan-out: a bounded [0,count) loop of atomic loads + O(1) existing engine setters (no heap/lock/syscall) — RT-safe by construction"
    - "Velocity-rides-Level: base_vol recomputed every block as q15_mul_truncate(signed guiVol, noteVelocity[v]) so the fader scales on TOP of per-note velocity instead of overwriting it"

key-files:
  created: []
  modified:
    - src/plugin/PluginProcessor.cpp

key-decisions:
  - "Fan-out writes ONLY voices[v].base_vol_l/r + set_non/set_pmon — never any sweep_l/sweep_r field — so an in-flight tremolo/duck state machine survives the per-block re-base (voice_controls_sweep_interaction proves it)"
  - "Pitch stays voice-0-only (set_pitch(mx,0,...) kept OUTSIDE the method) and the global noise_gen LFSR config stays a single global write — neither is fanned out (CONTEXT scope)"
  - "apply runs BEFORE the Phase 46 sidechain-duck block: base_vol is the sweep/duck ceiling consumed on the same C-core tick, and the duck snapshots the already-Level-scaled base_vol so duck depth (a ratio) is preserved when Level moves on a ducked voice"

patterns-established:
  - "Continuous per-voice control apply as an extracted processBlock method, seam-tested via the VoiceControlsTest friend forwarder with no audio render"

requirements-completed: [VCTRL-01, VCTRL-02, VCTRL-03]

# Metrics
duration: ~16min (executor implementation) + orchestrator-finalized gate/tracking
completed: 2026-05-31
---

# Phase 61 Plan 02: Coherent Controls Fan-Out (GREEN) Summary

**GREEN half of the RED->GREEN pair: replaces the no-op stub with the real `applyContinuousVoiceControls()` fan-out so Level, Pan, INV, NON, and PMON govern every active voice `[0, activeVoiceCount)`, with per-note velocity retained so the Level fader rides on top of velocity (D-01) and the velocity/GUI full-scales reconciled onto the engine's 0x3FFF base_vol scale (D-08). All 8 `voice_controls` cases flip green; full suite + rt_safety stay green.**

## Performance

- **Duration:** ~16 min executor implementation (commits 21:51–22:07 CDT); full-suite GREEN gate (~18 min, dominated by the 1091 s `plug15_null_passthrough_48k` render) and finalization completed by the orchestrator.
- **Completed:** 2026-05-31
- **Tasks:** 3
- **Files modified:** 1 (`src/plugin/PluginProcessor.cpp`, +103 / -20) — all header declarations were pre-placed by Plan 01, so this plan is pure `.cpp`.

## Accomplishments
- **VCTRL-01/02/03 delivered.** Moving Level changes the loudness of every active voice; moving Pan places every active voice at the same stereo position; toggling NON/PMON reaches every active voice (ADSR confirmed already shared, D-07). Previously all five controls touched voice 0 only.
- **Velocity rides on top of Level (D-01).** MIDI note-on now stores `noteVelocity[voice] = velToQ15(vel)` and keys on via `combineVoiceVol(guiVol, velQ15)` instead of centered `vol,vol`; the per-block apply loop recomputes `base_vol = q15_mul_truncate(guiVol, noteVelocity[v])` so a harder hit stays louder while the fader scales the whole set.
- **Range reconciliation (D-08).** `velToQ15` maps velocity 0..127 → Q15 [0,0x7FFF]; `combineVoiceVol` = `q15_mul_truncate(guiVol, velQ15)`. Full velocity (0x7FFF) × full Level (0x3FFF) = 0x3FFE — the GUI's own max — so a velocity-127 MIDI note and the Trigger audition at Level=100% land on the same `base_vol`. The old `int16_t vol = (vel*0x7FFF)/127;` line that fed base_vol on the wrong full-scale is removed.
- **INV with no separate plumbing (D-03).** `q15_mul_truncate` preserves the sign of its first argument, so passing the signed `guiVol` first carries phase-invert's negative base_vol through the multiply for every active voice.
- **Regression-safe (D-06).** At count=24 the loop reaches all 24 voices, and the voice-0/Trigger audition (seeded `noteVelocity[0]=0x7FFF`) is bit-identical to the v1.11.0 voice-0 path. A voice outside `[0, count)` receives no control update.

## Final form of `applyContinuousVoiceControls()`

```cpp
void SPU94AudioProcessor::applyContinuousVoiceControls()
{
    auto* mx = spu94_get_voice_mixer();
    const int count = activeVoiceCount.load(std::memory_order_acquire);   // Phase 60 bound
    const int16_t guiL = guiVoiceVolL.load(std::memory_order_relaxed);
    const int16_t guiR = guiVoiceVolR.load(std::memory_order_relaxed);
    const bool nonOn   = guiVoiceNon.load(std::memory_order_relaxed);
    const bool pmonOn  = guiVoicePmon.load(std::memory_order_relaxed);

    for (int v = 0; v < count; ++v)
    {
        mx->voices[v].base_vol_l = combineVoiceVol(guiL, noteVelocity[v]);
        mx->voices[v].base_vol_r = combineVoiceVol(guiR, noteVelocity[v]);
        spu94_voice_mixer_set_non(mx, v, nonOn ? 1 : 0);
        spu94_voice_mixer_set_pmon(mx, v, pmonOn ? 1 : 0);
    }
}
```

- **Loop bound:** `count = activeVoiceCount.load(acquire)`, clamped to [1,24] by Phase 60's `setActiveVoiceCount`; `noteVelocity[24]` and `voices[24]` are fixed-size — strictly `v < count <= 24`.
- **Writes only** `base_vol_l/r` + `set_non`/`set_pmon`. Never any sweep field.
- **RT-safe:** atomic loads + a bounded loop of O(1) existing setters; no heap, lock, or syscall.

## Locked apply-vs-sweep/duck ordering (Pitfall 4)

`applyContinuousVoiceControls()` is called in `processBlock` at the site of the old voice-0-only block, **before** the Phase 46 sidechain-duck block. The call-site comment records the rationale:
- `base_vol` is the sweep/duck **ceiling** the C-core consumes on the same tick (`spu94_voice.c` STEP 0: `vol = sweep.level * base_vol >> 15`, or `vol = base_vol` when no sweep).
- Running the fan-out here means (a) the sweep reads the fresh Level-scaled ceiling, and (b) when the duck fires it snapshots `duckOrigLevel` from the already-Level-scaled `base_vol`, so moving Level on a ducked voice moves the ceiling while the duck **depth** (a ratio it applies to the snapshot) is preserved — not double-applied or erased.
- The loop never touches `sweep_l`/`sweep_r` `.active` or phase, so an in-flight tremolo/duck state machine survives the re-base — proven by `voice_controls_sweep_interaction`.

`set_pitch(mx, 0, guiVoicePitch)` (voice-0-only pitch) and the global `noise_gen` LFSR shift/step config remain outside the loop — single writes, not fanned out (CONTEXT scope).

## Task Commits

1. **Task 1: velocity range-reconciliation helpers + per-note velocity capture** — `6ea6256` (feat). Adds `velToQ15`/`combineVoiceVol`; note-on stores `noteVelocity[voice]` and keys on via `combineVoiceVol`; Trigger seeds `noteVelocity[0]=0x7FFF`; removes the old 0x7FFF-scale line.
2. **Task 2: implement `applyContinuousVoiceControls()` fan-out + call from processBlock** — `58f0092` (feat). Replaces the no-op stub body with the loop; deletes the three voice-0-only writes and calls the method at that site.
3. **Task 3: lock apply-vs-sweep/duck ordering at the call site** — `60192e0` (docs). Refines the ordering rationale comment (STEP 0 mechanism + duck snapshot); confirms the loop writes no sweep field.

## Test Results — GREEN gate

- **`ctest -R voice_controls`** → **8/8 passed** (the 6 count-sensitive RED cases flipped: level/pan/non_pmon/velocity_rides/default24/out_of_range; guards adsr_shared + sweep_interaction held green).
- **`ctest -L rt_safety`** → **6/6 passed** (alloc_gate, negative_meta, rt_safety labels) — the new host-side glue adds no heap/lock/syscall, so the C-core nm-gates are unaffected.
- **Full suite** (`ctest -E packaging_editable_install|packaging_wheel_tag`) → **131/131 passed, 0 failures** — no pre-existing plugin or C-core test regressed, including the full-render `plug15_null_passthrough_48k`.
- The 2 excluded tests (`test_packaging_editable_install` #101, `test_packaging_wheel_tag` #102) are pre-existing Python-wheel timeouts unrelated to this phase, logged in `deferred-items.md`.

## Files Modified
- `src/plugin/PluginProcessor.cpp` (+103 / -20) — `velToQ15`/`combineVoiceVol` helpers, note-on velocity capture, Trigger seed, the real `applyContinuousVoiceControls()` body, the processBlock call replacing the voice-0-only block, and the ordering comment.

## Decisions Made
- **Loop writes base_vol + flags only.** Keeping the apply loop off all sweep/duck state is what lets the continuous controls re-base the ceiling every block without fighting the L/R sweep state machines or the sidechain-duck restore.
- **Apply before duck.** Chosen so the duck reads the freshly Level-scaled ceiling as its pre-duck reference; documented at the call site rather than left implicit.
- **Pitch + noise_gen not fanned out.** Pitch stays voice-0-only and the global LFSR stays a single write, matching the CONTEXT scope decision — only Level/Pan/INV/NON/PMON are coherent across voices.

## Issues Encountered
- **Executor handed off before its finalization step.** The implementation agent committed Tasks 1–2 and the Task-3 working-tree change, kicked off the ~18-min full-suite regression run in the background, then returned control rather than blocking on it. The orchestrator finalized: committed Task 3 (`60192e0`), ran the authoritative GREEN gate (voice_controls 8/8, rt_safety 6/6, full suite 131/131), wrote this SUMMARY, and updated tracking. No work was lost — all source changes were intact in the working tree and verified against the plan's acceptance criteria before finalization.
- **Full-suite runtime far exceeds the VALIDATION.md ~90 s estimate** because of the `plug15_null_passthrough_48k` full render (~18 min). Not a defect; the estimate predated that case. Handled by backgrounding the suite and gating on its completion.

## Manual UAT (non-blocking — for the listening check)
Two ear-confirmations from VALIDATION.md, whose *mechanism* is already covered by automated mixer-state assertions:
1. **D-06:** voice-0/Trigger audition at Level=100% sounds indistinguishable from a v1.11.0 build (bit-identity is auto-checked; this is the ear A/B).
2. **VCTRL-03:** the PMON-chain character (voice-N bent by N−1) is audible across the active set when playing a chord with count≥3 and PMON enabled.

## User Setup Required
None — no external service configuration required.

## Self-Check: PASSED

- File exists: `src/plugin/PluginProcessor.cpp` (modified), `.planning/phases/61-coherent-controls/61-02-SUMMARY.md`.
- `applyContinuousVoiceControls()` body is the real fan-out loop over `[0, count)` writing `base_vol_l/r` + `set_non`/`set_pmon` only; reads `activeVoiceCount.load(acquire)`; called from processBlock; the voice-0-only `voices[0].base_vol_l = guiVoiceVolL.load` write is gone; `set_pitch(...,0,...)` (voice-0-only pitch) is still present and outside the method.
- `velToQ15` + `combineVoiceVol` defined; note-on assigns `noteVelocity[voice]`; Trigger seeds `noteVelocity[0]=0x7FFF`; the old `(vel*0x7FFF)/127` base_vol line is removed.
- Commits exist on the branch: `6ea6256`, `58f0092`, `60192e0`.
- All 8 `voice_controls` cases GREEN; full suite 131/131 (excluding the 2 pre-existing/unrelated packaging timeouts); rt_safety 6/6. No pre-existing test regressed.

---
*Phase: 61-coherent-controls*
*Completed: 2026-05-31*
