---
phase: 63-voice-count-persistence
plan: 01
subsystem: ui
tags: [juce, preset, serialization, voice-count, atomics, ctest]

# Dependency graph
requires:
  - phase: 60-engine-voice-count
    provides: "activeVoiceCount atomic + setActiveVoiceCount (clamp 1-24, release store, ring-out)"
  - phase: 62-voice-count-selector
    provides: "voiceCountBox standalone selector (itemId == count) + onChange -> setActiveVoiceCount"
provides:
  - "Active voice count persisted into the .spu94 [voice] text section (active_voices=N)"
  - "Load-side seed-then-override restore routed through setActiveVoiceCount (clamped, back-compatible to 24)"
  - "getActiveVoiceCount() const acquire getter (atomic stays private)"
  - "Standalone Voice Count selector snaps to the restored count after every file-preset load"
  - "First automated coverage of the plugin-layer text-preset round-trip (test_voice_persist)"
affects: [voice-count, preset-format, plugin-persistence, sampler-gui]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Seed-then-override preset parse: seed a local to the back-compat default before the loop, capture the key into the local (not a direct atomic store), apply once after the loop through the clamping setter"
    - "Plugin text-preset round-trip CTest harness (two-instance save-on-A / load-on-B) requiring prepareToPlay so engines[0] is live before loadPresetFromString"

key-files:
  created:
    - tests/plugin/test_voice_persist.cpp
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - src/plugin/PluginEditor.cpp
    - tests/plugin/CMakeLists.txt

key-decisions:
  - "D-01: count persisted only in the .spu94 [voice] text section (same surface as every other per-voice control)"
  - "D-02: binary DAW/session state (getStateInformation / StateSerializer) left untouched"
  - "D-03: a .spu94 with no count key restores to 24 via a seeded local (pre-feature back-compat)"
  - "D-04: load restores through setActiveVoiceCount (jlimit 1-24, held notes ring out) — never a raw atomic store"
  - "D-05: post-load voiceCountBox.setSelectedId uses juce::dontSendNotification to suppress the onChange feedback path"

patterns-established:
  - "Seed-then-override parse: absent key => back-compat default, present key => captured-then-clamped, no parser-side validation (the setter's clamp absorbs 0/999/junk)"
  - "Save reads with memory_order_relaxed (message-thread snapshot); the matching getter reads with memory_order_acquire to pair with setActiveVoiceCount's release store"

requirements-completed: [VCOUNT-04]

# Metrics
duration: 32min
completed: 2026-05-31
---

# Phase 63 Plan 01: Voice-Count Persistence Summary

**Active sampler voice count (1-24) now saves into and restores from `.spu94` text presets via a seed-then-override parse routed through `setActiveVoiceCount`, with the standalone Voice Count selector snapping to the restored value and pre-feature presets defaulting to the full 24 voices.**

## Performance

- **Duration:** 32 min
- **Started:** 2026-05-31T17:12:36Z
- **Completed:** 2026-05-31T17:44:35Z
- **Tasks:** 4 (3 implementation + 1 human-verify checkpoint)
- **Files modified:** 5 (1 created, 4 modified)

## Accomplishments

- **Save:** one `active_voices=N` line added to the `[voice]` block of `savePresetToString`, mirroring the plain-int `noise_shift=%d` form (relaxed snapshot read, no `? 1 : 0` ternary).
- **Restore (seed-then-override):** `int restoredCount = 24;` seeded before the parse loop; the `active_voices` clause in the `SEC_VOICE` else-if chain *captures into that local* (deliberately NOT a direct atomic store); after the loop, a single `setActiveVoiceCount(restoredCount)` applies it through the clamp (jlimit 1-24) and ring-out — so an absent key falls back to 24 and every malformed value (0→1, 999→24, non-numeric→1) lands safely with no parser-side validation.
- **Getter:** `getActiveVoiceCount() const` acquire-load getter added to `PluginProcessor.h` (pairs with `setActiveVoiceCount`'s release store); the `activeVoiceCount` atomic itself stays private.
- **GUI snap:** one resync line appended to `syncMixerKnobsFromProcessor` — `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), juce::dontSendNotification)` — so the standalone selector reflects the restored count on every file-preset load. `dontSendNotification` suppresses the `voiceCountBox.onChange` feedback path (avoids a redundant store). It rides the existing `timerCallback` `getFilePresetAppliedCount` watcher (untouched).
- **First plugin-layer round-trip coverage:** new `test_voice_persist` CTest target (3 cases) — the binary `StateSerializer` path had tests, but the `.spu94` text round-trip never did before this plan.

## Task Commits

Each task was committed atomically (TDD RED → GREEN flow):

1. **Task 1: getActiveVoiceCount getter + RED test scaffold** — `413a001` (test)
   - Public acquire getter, `test_voice_persist.cpp` (3 cases), `test_voice_persist` target in CMakeLists. Baseline RED by design.
2. **Task 2: save + seed-then-override parse + restore (GREEN)** — `6d4a9a4` (feat)
   - Save line, seeded local, capture clause, `setActiveVoiceCount` apply. Includes a test-harness fix (see Deviations). 3/3 GREEN.
3. **Task 3: voiceCountBox post-load resync** — `07f271b` (feat)
   - Selector snap line in `syncMixerKnobsFromProcessor` with `dontSendNotification`.

**Task 4** was a `checkpoint:human-verify` gate (no code) — see Human Verification below.

**Plan metadata:** _(this commit)_ — docs: complete plan.

_Note: TDD tasks 1-2 form the RED→GREEN pair; Task 3 is a pure GUI-reflection feat verified by build + the human-verify gate (the headless test cannot exercise the selector)._

## Files Created/Modified

- `tests/plugin/test_voice_persist.cpp` *(created)* — headless two-instance round-trip (A saves at 7, B loads, asserts B getter == 7), back-compat (`[voice]` with no count key → 24), and clamp (`active_voices=0` → 1, `active_voices=999` → 24) cases; constructs the processor via `make_unique<SPU94AudioProcessor>()` and calls `prepareToPlay` so the restore is live.
- `src/plugin/PluginProcessor.h` *(modified)* — `getActiveVoiceCount() const` acquire getter added beside the setter; atomic stays in the private section (line 447).
- `src/plugin/PluginProcessor.cpp` *(modified)* — `active_voices=%d` save line in the `[voice]` block; `restoredCount = 24` seed before the parse loop; `key == "active_voices"` capture clause; `setActiveVoiceCount(restoredCount)` apply after the loop, before the `std::memcpy(pendingPresetBuf...)` handoff.
- `src/plugin/PluginEditor.cpp` *(modified)* — `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), juce::dontSendNotification)` appended to `syncMixerKnobsFromProcessor`.
- `tests/plugin/CMakeLists.txt` *(modified)* — `test_voice_persist` target (copied from the `test_voice_alloc` block) with 3 `add_test` cases.

## Decisions Made

Followed the plan's locked decisions (D-01..D-05) exactly:

- **D-01 / D-02:** count lives only in the `.spu94` `[voice]` text section; the binary `getStateInformation` / `StateSerializer` path was not touched. (DAW/session-state persistence of the count is deferred per D-02 — see Next Phase Readiness.)
- **D-03:** back-compat via a seeded `restoredCount = 24` rather than leaving the instance's current count.
- **D-04:** restore routed through `setActiveVoiceCount` (clamp + ring-out), never a raw `activeVoiceCount.store()`.
- **D-05:** `juce::dontSendNotification` on the selector resync to break the `onChange` feedback loop.
- **Atomic ordering:** save reads `relaxed` (message-thread snapshot, matches the 13 sibling save lines); the getter reads `acquire` to pair with the setter's release store (audio-thread allocator handoff).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Added a prepareToPlay test-harness helper**
- **Found during:** Task 2 (turning the RED cases GREEN)
- **Issue:** `loadPresetFromString` early-returns until `engines[0]` is live, and `engines[0]` is created in `prepareToPlay` (not the constructor). A freshly `make_unique`'d processor therefore made the restore a silent no-op — which is why the Task 1 RED cases failed *identically* even once the implementation was present (the load path never ran).
- **Fix:** Added a `makePreparedProcessor()` helper in `test_voice_persist.cpp` that calls `prepareToPlay(44100.0, 512)` on each instance before save/load, so `engines[0]` exists and `loadPresetFromString` proceeds.
- **Files modified:** `tests/plugin/test_voice_persist.cpp`
- **Verification:** All 3 `voice_persist` cases turned GREEN after the helper was added (roundtrip 7→7, back-compat →24, clamp 0→1 & 999→24).
- **Committed in:** `6d4a9a4` (part of the Task 2 commit)

---

**Total deviations:** 1 auto-fixed (1 blocking, test-harness only — no production-code behavior changed)
**Impact on plan:** The fix was confined to the test scaffold and was necessary to exercise the restore path at all. No scope creep; production save/parse/restore matches the plan byte-for-byte.

## Issues Encountered

- **Pre-existing, out of scope (NOT a regression from this phase):** `test_packaging_editable_install` and `test_packaging_wheel_tag` time out while building a wheel. These targets were last touched in Phase 06 and contain no voice-count references. They were already failing independently of this work and are excluded from this plan's scope.

## Test Results

- **`ctest -R voice_persist` → 3/3 PASSED** — roundtrip 7→7, back-compat (no key) →24, clamp 0→1 & 999→24. The save key literal and the parse `key ==` literal are byte-identical (`active_voices`), proven by the round-trip passing.
- **Full regression suite → 24/24 PASSED** — binary `StateSerializer` path and C-core preset goldens untouched per D-02; Phase 60 `voice_alloc` and Phase 61 `voice_controls` all green. Adding a `[voice]` text key changed preset text but no golden snapshots the plugin `[voice]` text (zero test refs to `savePresetToString` / `loadPresetFromString`), so no golden update was needed.

## Human Verification

**Task 4 (`checkpoint:human-verify`, gate=blocking): APPROVED.**

The user launched the Release standalone, set Voice Count to 6, saved a `.spu94`, changed the selector to 18, then loaded the `.spu94` and confirmed the Voice Count selector snapped back to 6. User response: **"approved."** This is the GUI-snap proof that headless tests cannot exercise (ROADMAP success criterion 2 — "the selector shows the restored value").

## Success Criteria — VCOUNT-04

All three ROADMAP success criteria for Phase 63 are satisfied:

1. **Saving records the current count** — ✅ `active_voices=N` written to the `[voice]` text section (`voice_persist_roundtrip`, criterion 1).
2. **Loading restores the count AND the selector shows it** — ✅ restore via `setActiveVoiceCount` (round-trip test) + selector snap (human-verify approval, criterion 2).
3. **Pre-feature presets load cleanly, defaulting to 24** — ✅ seeded `restoredCount = 24` (`voice_persist_backcompat`, criterion 3).
4. **Full suite green — no regression** to the binary state path or C-core goldens — ✅ 24/24.

**VCOUNT-04 is complete** — the last open requirement of the v1.12.0 Voice Count milestone.

## Next Phase Readiness

- v1.12.0 Voice Count milestone requirements are all complete (VCOUNT-01..04, VCTRL-01..03, VALLOC-01..03).
- **Carried (non-blocking, tracked):** binary DAW/session-state persistence of the voice count is explicitly deferred per D-02 — if/when the count needs to survive a DAW session reload (not just a `.spu94` file), that is a future addition to the `StateSerializer` path.
- **Carried from Phase 62 (HUMAN-UAT, non-blocking):** the audible mono↔poly listening check remains pending (no working MIDI controller on the standalone under Linux). Unrelated to persistence.

## Self-Check: PASSED

- `63-01-SUMMARY.md` — FOUND
- Task commit `413a001` — FOUND
- Task commit `6d4a9a4` — FOUND
- Task commit `07f271b` — FOUND
- Source anchors confirmed in committed source (getter acquire @ PluginProcessor.h:286, save line @ :1861, seed @ :1921, capture clause @ :1955, apply @ :2008, editor resync @ PluginEditor.cpp:1990, atomic still private @ PluginProcessor.h:447)

---
*Phase: 63-voice-count-persistence*
*Completed: 2026-05-31*
