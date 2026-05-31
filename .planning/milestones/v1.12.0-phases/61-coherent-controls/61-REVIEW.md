---
phase: 61-coherent-controls
reviewed: 2026-05-31T03:26:27Z
depth: standard
files_reviewed: 4
files_reviewed_list:
  - src/plugin/PluginProcessor.cpp
  - src/plugin/PluginProcessor.h
  - tests/plugin/test_voice_controls.cpp
  - tests/plugin/CMakeLists.txt
findings:
  critical: 0
  warning: 3
  info: 4
  total: 7
status: issues_found
---

# Phase 61: Code Review Report

**Reviewed:** 2026-05-31T03:26:27Z
**Depth:** standard
**Files Reviewed:** 4
**Status:** issues_found

## Summary

Reviewed the Phase 61 "coherent-controls" diff (base `a0d046a..HEAD`): the change fans
Level / Pan / INV (via per-voice `base_vol_l/r`), NON, and PMON across every active voice
`[0, activeVoiceCount)` instead of only voice 0, plus a per-note velocity-retention array
(`noteVelocity[24]`) so the Level fader rides on top of MIDI velocity. New surface: the
`velToQ15` / `combineVoiceVol` helpers, the `applyContinuousVoiceControls()` fan-out loop
and its `processBlock` call site, MIDI/Trigger velocity capture, and a headless test target.

**Real-time safety: clean.** The fan-out is atomic loads + a bounded (`count <= 24`) loop of
O(1) C setters — no heap, no locks, no syscalls. The loop bound `v < count` where
`count = activeVoiceCount.load()` is clamped to `[1,24]` by `setActiveVoiceCount`, and both
`noteVelocity[24]` and `voices[24]` are fixed 24-wide, so there is **no out-of-bounds risk**
(this directly answers the scope's stated concern). The Q15 sign handling is correct:
`q15_mul_truncate(guiVol, velQ15)` keeps the sign of its first argument, so INV's negative
`base_vol` survives. The velocity math cannot overflow (`127 * 0x7FFF = 4161409` fits int32).

**No BLOCKERs found.** The fan-out is correct for the happy path and the deferred-key_on
save/restore in `spu94_voice.c` (lines 658-666, then re-set by `spu94_voice_key_on` at 677)
means a same-block note-on still wins over the stale-velocity apply-loop pass. The findings
below are correctness-adjacent edge interactions (duck + live Level move), a velocity-lifecycle
gap that is latent today but is a loaded gun for the upcoming GUI work, and documentation/
comment drift that overstates exactness.

## Warnings

### WR-01: `noteVelocity[v]` is never reset on note-off / voice-free — stale velocity governs a re-keyed or fader-swept voice

**File:** `src/plugin/PluginProcessor.cpp:1488-1493` (note-off path), `:2700-2703` (apply loop), `:559` (PluginProcessor.h declaration)

**Issue:** `noteVelocity[v]` is written only on MIDI note-on (`:1479`) and Trigger (`:867`).
It is **never cleared** when a voice is keyed off (`:1492`), stolen, or its ADSR release
finishes. `applyContinuousVoiceControls()` unconditionally rewrites `base_vol[v]` for **every**
voice in `[0, count)` every block, using whatever `noteVelocity[v]` was last left there:

```cpp
for (int v = 0; v < count; ++v) {
    mx->voices[v].base_vol_l = combineVoiceVol(guiL, noteVelocity[v]);   // stale velQ15 if v idle
    mx->voices[v].base_vol_r = combineVoiceVol(guiR, noteVelocity[v]);
    ...
}
```

Two consequences:
1. **Released-but-ringing tail:** a voice in ADSR release is still `active=1` (voice.c:97-99).
   While its tail rings, the apply loop keeps multiplying the *old* note's velocity by the live
   Level. If the user nudges the Level fader during a long release, that tail's ceiling jumps to
   `oldVelocity x newLevel`. This is the documented "base_vol is the live ceiling" design, but
   the *velocity* component being a ghost of a finished note is surprising.
2. **MIDI then Trigger on the same slot:** the Trigger path seeds `noteVelocity[0]=0x7FFF`, but
   a MIDI note that lands on voice 0 leaves a sub-max velocity behind. There is no reset, so the
   *next* thing that reads voice 0's velocity inherits the last writer. Currently safe because
   every key path overwrites before it matters — but it depends on that invariant holding.

This is latent (no current audible bug because every key_on re-stages `base_vol` at tick start,
and idle voices are silent), but it is a fragile invariant that the Phase 62 GUI voice-count
selector and any future per-voice retrigger can break.

**Fix:** Reset velocity on note release and in the allocator's steal path so an idle/just-freed
voice contributes `base_vol = 0` rather than a ghost velocity:

```cpp
// note-off (around :1490)
int voice = findVoiceForNote(msg.getNoteNumber());
if (voice >= 0) {
    spu94_voice_mixer_key_off(spu94_get_voice_mixer(), voice);
    noteVelocity[voice] = 0;   // drop the retained velocity; tail uses ADSR, not base_vol ghost
}
```
If preserving the release-tail ceiling is intentional (it may be — the tail's loudness arguably
*should* track velocity), then at minimum document that `noteVelocity` intentionally persists
through release, and clear it in `allocateVoice`'s steal branch (`:2667-2669`) so a stolen slot
never inherits the previous occupant's velocity.

### WR-02: live Level fader move during an active duck desynchronizes the duck floor/recovery target from the moved ceiling

**File:** `src/plugin/PluginProcessor.cpp:932` (apply runs every block), `:1527-1528` (duck snapshot), `:1602-1603` / `:1636-1647` (floor + recovery target)

**Issue:** The header comment at `:920-931` claims moving Level on a ducked voice "moves the
ceiling while the duck depth (a ratio it applies to the snapshot) is preserved — not
double-applied or erased." That holds only if `base_vol` is *static* during the duck. After
Phase 61, `applyContinuousVoiceControls()` rewrites `base_vol[v]` **every block** for all active
voices, including a currently-ducking one. The duck captures `duckOrigLevel_l[v] = voices[v].vol_l`
once at trigger (`:1527`) and drives a sweep whose floor (`:1602`) and recovery target (`:1636`)
are computed from that frozen snapshot. If the user moves the Level fader mid-duck:

- `base_vol[v]` changes → next tick STEP 0 recomputes `vol_l = sweep.level * base_vol >> 15`
  against the *new* ceiling, so the audible duck depth shifts under the user's hand;
- recovery still targets the *old* `duckOrigLevel_l[v]`, so the voice recovers to a level that no
  longer matches the (now different) Level setting — it can overshoot or undershoot the fader.

This requires the narrow combination of (a) ducking active on a voice and (b) the Level fader
being moved during that duck, and it degrades gracefully (no crash, no NaN, sweep state survives
per the guard test). But the in-code claim that the interaction is clean is **stronger than the
code supports** — the snapshot is not re-derived when the ceiling moves.

**Fix:** Either (preferred) re-anchor the duck snapshot when `base_vol` changes mid-duck, or
soften the comment to state the known limitation. A minimal re-anchor: in the post-process duck
state machine, when `duckState[v] != DUCK_IDLE`, rescale `duckOrigLevel_l/r[v]` by the ratio of
new-to-old `base_vol` before computing floor/recovery, so the depth ratio is preserved against the
moved ceiling. At minimum, add a regression test that fires a duck, moves Level, and asserts the
recovered level tracks the new fader rather than the stale snapshot.

### WR-03: per-block unconditional `base_vol` rewrite for *every* `[0,count)` voice can fight other per-voice volume writers

**File:** `src/plugin/PluginProcessor.cpp:2700-2703`

**Issue:** The old code wrote `base_vol` only for voice 0. The new loop writes `base_vol_l/r`
for **all** active voices every block, unconditionally, from the single global `guiVoiceVolL/R`
pair. Any current or future per-voice path that wants a voice to hold a *different* `base_vol`
than `combineVoiceVol(globalGui, noteVelocity[v])` will be silently overwritten on the very next
block. Concretely, the mod-bus and duck paths operate on `vol_l/r` (ephemeral, re-derived from
`base_vol` each tick) so they coexist, but this loop now makes the global Level/Pan fader the
**sole authority** over `base_vol` for the entire active set — there is no per-voice Level escape
hatch. This is consistent with the "coherent controls" intent (every voice shares the fader), so
it is a design constraint to flag, not a defect: it forecloses per-voice Level/Pan until a future
phase reworks the storage model.

**Fix:** No change required if the global-fader-owns-all-base_vol model is the accepted design
(it matches the phase goal). Flagging so it is a conscious, documented constraint: add a one-line
note at the loop that `base_vol` is now globally owned and any future per-voice volume must route
through a separate field (e.g., a per-voice trim folded into `combineVoiceVol`), not by writing
`base_vol` directly.

## Info

### IN-01: comment claims voice-0 Trigger stays "exactly at the Level setting" but it is 1 LSB low

**File:** `src/plugin/PluginProcessor.cpp:862-866`, `:2677-2680`

**Issue:** The comment says `combineVoiceVol(guiVol, 0x7FFF) == guiVol` ("keeps voice 0 at
exactly the Level setting"). `q15_mul_truncate(0x3FFF, 0x7FFF) = (0x3FFF*0x7FFF)>>15 = 0x3FFE`,
not `0x3FFF` — a 1-LSB (≈ -0.0005 dB) attenuation, which the `default24_regression` test itself
encodes as the expected `0x3FFE`. So full Level is now `0x3FFE`, not the pre-Phase-61 `0x3FFF`.
Inaudible and intentional, but "exactly" overstates it.

**Fix:** Reword to "≈ the Level setting (1-LSB truncation: `0x3FFF -> 0x3FFE`)" so the comment
matches the asserted test value and nobody later "fixes" the 1-LSB delta as a bug.

### IN-02: `velToQ15` / `combineVoiceVol` duplicated between production and test

**File:** `tests/plugin/test_voice_controls.cpp:111-116` vs `src/plugin/PluginProcessor.cpp:62-72`

**Issue:** `velToQ15` is copy-pasted into the test's anonymous namespace (identical body) rather
than reused from production. If the production mapping ever changes (e.g., a different velocity
curve), the test keeps asserting against the old formula and silently passes. `combineVoiceVol`
is effectively re-expressed inline in the test as `q15_mul_truncate(0x3FFF, velToQ15(...))`.

**Fix:** Low priority for a small static helper. If a shared test/impl seam is cheap, expose
`velToQ15`/`combineVoiceVol` (they are in an anonymous namespace in the .cpp, so not linkable —
moving them to a small internal header would let the test include the single source of truth).

### IN-03: `combineVoiceVol` is a one-line passthrough wrapper over `q15_mul_truncate`

**File:** `src/plugin/PluginProcessor.cpp:84-87`

**Issue:** `combineVoiceVol(guiVol, velQ15)` does nothing but `return q15_mul_truncate(guiVol, velQ15);`
— no added clamping, no reordering, no sign handling beyond what the callee already guarantees.
The wrapper earns its keep purely as a named documentation anchor (the comment block above it is
the real value). Acceptable, but it is indirection that a reader must chase to confirm there is no
hidden behavior.

**Fix:** Keep it (the name + comment aid readability and the call sites read well), or inline it
and move the explanatory comment to the call site. No correctness impact either way.

### IN-04: test `keyOnMidiLike` mirrors live `base_vol` by hand, coupling the test to engine internals

**File:** `tests/plugin/test_voice_controls.cpp:78-88`, `:364-365`

**Issue:** `keyOnMidiLike` stages a `key_on` (which only sets `pending_config`, not the live
voice — voice.c:453-458) but the test then manually writes `voices[v].base_vol_l/r` (`:364-365`)
to fake the "already keyed" live state, because no `mixer_tick` runs in the headless harness.
This is a reasonable proof-without-render technique (the file documents it), but it hardcodes the
fact that `key_on` defers to tick; if the engine ever made `key_on` apply immediately, these tests
would quietly test a contradictory state. Not a defect — noting the tight coupling so a future
engine change to key_on timing also revisits these cases.

**Fix:** None required. Optionally assert the pre-condition (`voices[v].active == 0` right after
`key_on`, before the manual mirror) to make the deferred-apply assumption explicit and
self-documenting.

---

_Reviewed: 2026-05-31T03:26:27Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
