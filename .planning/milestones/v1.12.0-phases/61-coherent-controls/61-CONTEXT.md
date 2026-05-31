# Phase 61: Coherent Controls - Context

**Gathered:** 2026-05-30
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the **sampler's** per-voice GUI controls govern every *active* voice, not
just voice 0. Today the control strip (Level, Pan, INV, NON, PMON) writes only to
`voices[0]` / `pending_config[0]` — the voice the on-screen Trigger button plays —
and MIDI-played notes take their loudness from velocity and otherwise ignore the
GUI controls. This phase fans the controls out across `[0, activeVoiceCount)`
(the active set defined by Phase 60) so the whole rig sounds the way the controls
are set.

**In scope (VCTRL-01/02/03):** Level, Pan, INV (phase-invert), and the NON/PMON
toggles reach every active voice; the ADSR envelope shape applies to every
triggered voice (verified already true — see below).

**Out of scope:** the user-facing voice-count selector (Phase 62 / VCOUNT-01),
voice-count persistence (Phase 63), per-voice independent control values (every
active voice shares one setting — a chord lands at one Pan, one Level, etc.), and
any new signal-flow topology. Pitch is intentionally NOT fanned out: each MIDI
note keeps its own pitch from the note number; the GUI pitch knob stays a
voice-0/Trigger-audition control.

</domain>

<decisions>
## Implementation Decisions

### Level vs. velocity — Level rides on top of velocity
- **D-01:** MIDI note loudness stays velocity-sensitive; the Level fader is a
  master trim that scales the whole rig on top of velocity. Hitting harder is
  still louder; Level raises/lowers everything. (Chosen over "Level replaces
  velocity / flat knob-set loudness." Neither is more hardware-faithful — the SPU
  has a per-voice volume register and no velocity concept; this is a feel call.)
- **D-01a:** Level fans out as per-voice `base_vol_l/r` (the same signal point
  voice 0 uses today), NOT on `master_vol`. Per the established mixer signal flow
  (reverb send tapped after per-voice volume, before master volume), this means
  Level governs each voice's reverb send as well as its dry level — pulling Level
  down ducks the rig's reverb feed too, identical to voice 0's current behavior.
  Planner: confirm the send-tap point in `src/spu94/spu94_voice.c` mixer tick.

### Pan — one shared stereo position for the whole rig
- **D-02:** Every active voice shares the single Pan knob → a chord lands all its
  notes at the *same* stereo position. No automatic left/right spread across
  voices. This is exactly VCTRL-02 ("same stereo position") and matches the
  one-module-one-pan model. (Per-voice spread is a future feature — see Deferred.)

### INV (phase-invert) — rides the volume sign
- **D-03:** INV negates the sign of `base_vol_l/r` (Phase 39 design). Because Pan,
  Level, and INV all resolve into the one `base_vol` value, fanning `base_vol` out
  to every active voice carries all three together — no separate INV plumbing.

### NON / PMON — fan out as per-voice flags; PMON forms the hardware chain
- **D-04:** The NON (sample→noise) and PMON (pitch-mod-from-previous-voice)
  toggles set the per-voice flag across `[0, activeVoiceCount)` via
  `spu94_voice_mixer_set_non` / `..._set_pmon`. Enabling PMON across the active
  set produces the faithful SPU **chain** (voice N's pitch bent by voice N-1's
  output; voice 0 has no predecessor) — a characterful effect, not per-voice
  self-wobble. This is the default because it's the hardware behavior. The noise
  generator (LFSR) is already global/shared; only the per-voice NON enable fans out.

### Live application vs. trigger-time
- **D-05:** Continuous controls (Level, Pan, INV, NON, PMON) apply **live** to all
  currently-sounding voices the instant the knob/toggle moves — not just to
  newly-played notes. The envelope is the one exception: its shape is latched at
  each note's key-on and rides out from there (can't reshape a sounding note —
  PS1-faithful). Matches success criteria 1–2 ("every *sounding* voice") and the
  existing voice-0 continuous apply block, which already writes `base_vol`/NON/PMON
  every block.

### Fan-out bound — the Phase 60 active count
- **D-06:** The fan-out loop runs over `[0, activeVoiceCount)` (Phase 60's
  `std::atomic<int> activeVoiceCount{24}`). At the default 24 this reaches all
  voices = no audible change vs. today (regression safety). A voice that falls out
  of range when the count later drops (Phase 62) simply stops receiving control
  updates and rings out / gets reused per Phase 60's allocation rules.

### Already coherent — no work needed (verified in code)
- **D-07:** ADSR is already fanned out — `buildAdsrConfig()` is applied at every
  MIDI key-on (`PluginProcessor.cpp:1427`), so every triggered voice already
  shares the GUI envelope. VCTRL-03's ADSR clause is satisfied; planner should add
  a regression check, not new wiring. The shared noise generator is likewise
  already global.

### Trigger button / voice-0 audition
- **D-08:** The on-screen Trigger button (no velocity) plays voice 0 at the Level
  setting (treat as full velocity × Level) so the audition button matches the
  fader. Minor consistency detail.

### Claude's Discretion (developer details — not user-facing)
- Exact Q15 math for combining velocity × Level × Pan × INV into `base_vol_l/r`.
- Storing each active voice's velocity so `base_vol` can be recomputed live each
  block when Level/Pan move (the continuous apply block currently overwrites
  voice-0 `base_vol` every block; extending that to N voices needs the per-note
  velocity retained somewhere realtime-safe).
- Realtime-safe read of `activeVoiceCount` (acquire) inside the message→audio
  apply path; reuse the Phase 60 atomic pattern.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### GUI control apply path (the voice-0-only writes to fan out)
- `src/plugin/PluginProcessor.cpp:863-885` — continuous apply block: pitch,
  `voices[0].base_vol_l/r` (Pan/Level/INV), NON, PMON all written to voice 0 only.
  This is the block to fan out across the active set.
- `src/plugin/PluginProcessor.cpp:1414-1438` — MIDI note-on/off dispatch: loudness
  from velocity (`vol = vel*0x7FFF/127`), key-on uses `vol, vol` (hardcoded center
  pan), `buildAdsrConfig()` already applied. Must apply Level/Pan/INV here instead
  of centered velocity-only.
- `src/plugin/PluginProcessor.cpp:2487` — `buildAdsrConfig()` (proves ADSR is
  already shared across MIDI voices).
- `src/plugin/PluginProcessor.h:444-457` — `guiVoicePitch`, `guiVoiceVolL/R`,
  `guiVoiceNon`, `guiVoicePmon` source-of-truth atomics.

### Pan/Level/INV → base_vol combination (the GUI side)
- `src/plugin/PluginEditor.cpp:536-577` — Pan rotary (-100..+100), Level fader
  (0..100%), INV toggle; `updateVoiceVolumes()` combines them into `guiVoiceVolL/R`.
- `src/plugin/PluginEditor.cpp:1592-1602` — INV negates both channels (sign flip).

### Active voice count (Phase 60 — the fan-out bound)
- `src/plugin/PluginProcessor.h:437` — `std::atomic<int> activeVoiceCount{24}`.
- `src/plugin/PluginProcessor.cpp:2593` — `setActiveVoiceCount` (clamp [1,24]).
- `src/plugin/PluginProcessor.cpp:2601-2609` — `allocateVoice` reads count as the
  round-robin modulus.
- `.planning/phases/60-engine-voice-count-allocation/60-CONTEXT.md` — Phase 60
  decisions (allocation, mono last-note priority, held-notes-ring-out).

### Voice mixer engine (setters + signal flow)
- `include/spu94/spu94_voice.h:57-58` — `base_vol_l/r` (per-voice pan/level, the
  fan-out target); `:146-147` `master_vol_l/r` (post-sum, dry-only, pinned full);
  `:179` `set_pmon`, `:186` `set_non`, `:216` `set_pitch`.
- `src/spu94/spu94_voice.c` — mixer tick; confirm the reverb-send tap is after
  per-voice `base_vol`, before `master_vol` (governs D-01a).

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **Voice-0 continuous apply block** (`PluginProcessor.cpp:863-885`) is the exact
  template for the fan-out — change "write voice 0" to "loop `[0, activeVoiceCount)`".
- **`buildAdsrConfig()`** already fans ADSR to every MIDI note — pattern to mirror.
- **Phase 60 `allocateVoice`** already bounds note allocation to the active count;
  Phase 61 makes the *controls* honor the same bound.

### Established Patterns
- Pan + Level + INV are pre-combined into one signed `base_vol_l/r` per Phase 39 —
  fan out the combined value, don't re-derive Pan/Level/INV per voice on the engine
  side.
- Signal flow: per-voice `base_vol` → reverb send tap → `master_vol` (dry only).
  Level on `base_vol` therefore governs each voice's reverb send (D-01a).
- Message-thread → audio-thread state crosses via `std::atomic` with the Phase 60
  release/acquire pattern.

### Integration Points
- The fan-out loop bound is Phase 60's `activeVoiceCount`; the Phase 62 selector
  will move that count live, and these controls must already be reaching exactly
  the active set for Phase 62 to feel right.
- MIDI note-on (`:1426-1430`) is where per-note velocity meets the GUI Level/Pan/
  INV — the main behavioral change point.

</code_context>

<specifics>
## Specific Ideas

- Default count 24 must reproduce current 24-voice behavior with no audible change
  (regression safety vs. Phase 42 verification).
- PMON-on across the active set = the faithful per-voice chain (voice N bent by
  N-1). Surfaced to Anthony as a known sound consequence; kept as the default.
- "Level rides on top of velocity" framed as a mixer channel fader: touch dynamics
  still come through, the fader sets the overall level.

</specifics>

<deferred>
## Deferred Ideas

- **Per-voice pan spread** — spreading a chord's notes across the stereo field
  (each voice its own pan, auto-distributed). Came up while confirming the single
  shared Pan; it's a new capability and belongs in its own future phase, not here.

</deferred>

---

*Phase: 61-coherent-controls*
*Context gathered: 2026-05-30*
