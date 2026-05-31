# Phase 63: Voice-Count Persistence - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning

<domain>
## Phase Boundary

Make the active voice count (the 1–24 value behind `setActiveVoiceCount` /
`activeVoiceCount`, from Phases 60/62) **save into and restore from `.spu94`
preset files**, so a reloaded patch plays at the count it was saved with and the
standalone **Voice Count** selector snaps to the restored value. Pre-feature
patches (no count stored) load as the full 24.

**In scope (VCOUNT-04):**
- Serialize `activeVoiceCount` into the plugin-layer `[voice]` section of the
  `.spu94` text format (`savePresetToString`), beside the existing per-voice keys
  (`vol_l`, `non`, …).
- Parse it back in `loadPresetFromString`, routing the restored value through
  `setActiveVoiceCount` (clamp 1–24, RT-safe, held notes ring out).
- Refresh the standalone `voiceCountBox` to the restored value after a load
  (criterion 2), via the existing post-load GUI resync path.
- Back-compat: a `.spu94` with no count key restores to 24.

**Out of scope:**
- Binary DAW/session state (`getStateInformation` / `setStateInformation` →
  `StateSerializer`). The count is **not** added to the binary container this
  phase (see D-02 + Deferred).
- The DAW-plugin Voice Count control (still deferred from Phase 62).
- Any change to allocation / fan-out / mono-poly behavior (shipped Phases 60/61)
  or to the live selector wiring (shipped Phase 62).

</domain>

<decisions>
## Implementation Decisions

### Persistence surface
- **D-01:** Persist the voice count to **`.spu94` preset files only** — the
  plugin-layer `[voice]` section written by `savePresetToString` / read by
  `loadPresetFromString`. This is exactly where every other per-voice control
  already lives (level, pan, NON, PMON, noise, drive, ADSR, …), so the count
  behaves like the rest of the patch. Rationale (user pick, 2026-05-31): keep it
  consistent; don't make voice count the lone setting that *also* auto-recalls.
- **D-02:** **Do NOT** add the count to the binary DAW/session state
  (`StateSerializer` / `getStateInformation`). That container today serializes
  only the C-core engine (35 reverb regs + mixer + DAC + a 6-float morph
  appendix) and carries **none** of the sampler `[voice]`/`[adsr]`/`[effects]`/
  `[mod_bus]` GUI sections. Adding voice count alone would break that pattern,
  and the plugin surface has no visible Voice Count control yet (Phase 62
  deferral). Deferred until the broader "all sampler state in the session blob"
  question is revisited.

### Back-compat
- **D-03:** A `.spu94` with **no count key restores to 24** (engine default / full
  rig). Load semantics: default the restored count to 24, then override only if
  the key is present — so pre-feature patches (and any future no-count patch)
  land on 24. Satisfies ROADMAP criterion 3. NOTE: the existing parser is lenient
  (absent keys leave state untouched), so this needs an explicit "seed 24 then
  override" — relying on the lenient skip would leave the user's *current* count
  instead of forcing 24.

### Restore routing & re-sync
- **D-04:** On load, push the restored count through **`setActiveVoiceCount(n)`**
  (NOT a raw atomic store) so it clamps to 1–24 and inherits Phase 60's graceful
  behavior — lowering lets held notes ring out and shrinks future allocation.
- **D-05:** After a load, the standalone **`voiceCountBox` must update to the
  restored value** (`setSelectedId(n, dontSendNotification)`) so the selector
  shows what is actually playing (criterion 2). Hook into the existing post-load
  resync path (editor `timerCallback` watches `getFilePresetAppliedCount()` →
  `syncMixerKnobsFromProcessor()`). Use `dontSendNotification` so the refresh does
  not re-fire `onChange` back into `setActiveVoiceCount`.

### Claude's Discretion
- Exact key name in the `[voice]` section (e.g. `active_voices=` / `voice_count=`),
  its format (small decimal int per the `non=%d` precedent), and placement within
  the section — implementer's call.
- Whether the GUI resync reads the count via a new message-thread-safe accessor or
  the existing atomic — implementer's call, as long as it is read-safe from the
  message thread.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### The save/load text path to extend (where the work lands)
- `src/plugin/PluginProcessor.cpp:1831-1906` — `savePresetToString`; the `[voice]`
  section begins at line 1852 (`text += "\n[voice]\n"`). Add the count key here,
  next to `vol_l=` / `non=`.
- `src/plugin/PluginProcessor.cpp:1908-1950` — `loadPresetFromString`; the
  `SEC_VOICE` parse block (~1935-1950). Parse the count key here, then call the
  setter (D-04).
- `src/plugin/PluginProcessor.cpp:~2650` — `setActiveVoiceCount` definition
  (clamp [1,24], release-store at line 2655) — the restore target.
- `src/plugin/PluginProcessor.h:281` — `void setActiveVoiceCount(int n);`
- `src/plugin/PluginProcessor.h:442` — `std::atomic<int> activeVoiceCount{24}`.

### The GUI selector to re-sync (criterion 2)
- `src/plugin/PluginEditor.cpp:71-81` — `voiceCountBox` setup (default
  `setSelectedId(24, dontSendNotification)`).
- `src/plugin/PluginEditor.cpp:327-330` — `voiceCountBox.onChange` →
  `setActiveVoiceCount` (Phase 62 live wiring; the feedback-loop to avoid on resync).
- `src/plugin/PluginEditor.cpp:1327-1333` — post-load resync hook: `timerCallback`
  watches `getFilePresetAppliedCount()` → `syncMixerKnobsFromProcessor()`. The
  voice-count box refresh joins here.

### The binary state path NOT touched this phase (context for D-02)
- `src/plugin/PluginProcessor.cpp:2794-2831` — `getStateInformation` /
  `setStateInformation`.
- `src/plugin/StateSerializer.h` — binary container; `save()` serializes only the
  `spu94_preset_save` C-core text + a 6-float appendix (3 reserved slots). Confirms
  the plugin `[voice]` section is absent from DAW session state.

### Prior-phase decisions (locked inputs — do not re-open)
- `.planning/phases/62-voice-count-selector/62-CONTEXT.md` — the selector,
  standalone-only surface, plugin deferral.
- `.planning/phases/60-engine-voice-count-allocation/60-CONTEXT.md` — count state,
  clamp, ring-out-on-decrease.
- `.planning/ROADMAP.md` (Phase 63 section) — goal + 3 success criteria; VCOUNT-04.

No external specs/ADRs beyond the above.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `savePresetToString` / `loadPresetFromString` `[voice]` section — a working
  serialize/parse pair for plugin-layer per-voice state; the count key is a
  one-line addition on each side (mirror the `non=%d` save + `key == "non"` parse).
- `setActiveVoiceCount` — existing clamped, RT-safe setter; reuse as the restore
  entry point (D-04).
- Post-load resync (`timerCallback` + `getFilePresetAppliedCount` +
  `syncMixerKnobsFromProcessor`) — the established "preset loaded → refresh GUI"
  mechanism; the `voiceCountBox` refresh slots in here (D-05).

### Established Patterns
- `.spu94` text keys: small ints as `%d` (e.g. `non=1`), Q15 hex as `0x%04X`. The
  count is a decimal int.
- Parser is lenient: absent keys leave state untouched — so back-compat (D-03)
  needs an explicit "seed 24 then override if present", not reliance on the skip.
- Processor→GUI control updates use `dontSendNotification` to avoid re-triggering
  `onChange` handlers.

### Integration Points
- Save: `savePresetToString` `[voice]` section ← `activeVoiceCount`.
- Load: `loadPresetFromString` `SEC_VOICE` → `setActiveVoiceCount(restored)`.
- Display: editor post-load resync → `voiceCountBox.setSelectedId(restored, dontSendNotification)`.

</code_context>

<specifics>
## Specific Ideas

- User framing (2026-05-31): voice count should "behave like everything else" —
  saved inside the patch, not a special setting that also auto-recalls across
  sessions.
- The graceful side-effect matters: restoring a *lower* count lets held notes ring
  out (Phase 60), so loading a low-count patch mid-performance won't hard-cut
  sounding voices.

</specifics>

<deferred>
## Deferred Ideas

- **Voice count in binary DAW/session state** (the alternative weighed this phase)
  — deferred until the broader "should all sampler `[voice]`/`[adsr]`/`[effects]`/
  `[mod_bus]` state ride in the binary session blob?" question is taken up. Today
  none of the sampler GUI state is in that container; voice count follows the herd.
  Revisit alongside the deferred DAW-plugin Voice Count control.
- **DAW-plugin Voice Count control** — still deferred from Phase 62 (plugin-beta
  milestone).

None other — discussion stayed within phase scope.

</deferred>

---

*Phase: 63-voice-count-persistence*
*Context gathered: 2026-05-31*
