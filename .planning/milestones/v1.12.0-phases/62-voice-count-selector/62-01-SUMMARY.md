---
phase: 62-voice-count-selector
plan: 01
subsystem: ui
tags: [juce, combobox, standalone, polyphony, voice-count, gui-wiring]

# Dependency graph
requires:
  - phase: 60-engine-voice-count
    provides: "setActiveVoiceCount(int) message-thread setter (clamps [1,24], RT-safe atomic store) + count-bounded allocation with oldest-voice steal + mono last-note priority"
  - phase: 61-coherent-controls
    provides: "applyContinuousVoiceControls() fan-out across [0, activeVoiceCount) so per-voice Level/Pan/NON/PMON follow the active count"
provides:
  - "User-facing 'Voice Count' ComboBox (1-24, default 24) in the standalone sampler voice panel"
  - "Live onChange wiring: voiceCountBox.getSelectedId() -> processorRef.setActiveVoiceCount(n), driving the Phase 60 allocator + Phase 61 fan-out with no reload"
affects: [63-persistence]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "ComboBox itemId == payload value (1..24) so getSelectedId() passes straight through with no ID arithmetic"
    - "Standalone-only control by construction: added to samplerWindow->getPanel() and laid out only inside the if (samplerWindow) block in resized() — zero plugin-surface code (D-03)"

key-files:
  created: []
  modified:
    - src/plugin/PluginEditor.h
    - src/plugin/PluginEditor.cpp

key-decisions:
  - "Dropdown selection IS the display — no separate readout widget; display == actual by construction since the dropdown is the sole driver (D-05)"
  - "Default selected id 24 with dontSendNotification — matches engine default activeVoiceCount{24}, so construction fires no onChange and there is no audible change until the user lowers the count (D-05)"
  - "DAW-plugin selector surface deferred to a plugin-beta milestone; this phase is standalone-only (D-03 revised)"

patterns-established:
  - "Voice-count control cloned from the existing recordModeBox four-part ComboBox idiom (declaration / setup / onChange / bounds)"
  - "itemId-as-value ComboBox: the engine count is read directly from getSelectedId(), no offset/mapping table"

requirements-completed: [VCOUNT-01, VCOUNT-03]

# Metrics
duration: ~3 min (code tasks 1-2)
completed: 2026-05-31
---

# Phase 62 Plan 01: Voice Count Selector Summary

**A standalone-only "Voice Count" ComboBox (1-24, default 24) whose onChange calls `setActiveVoiceCount(getSelectedId())`, driving the Phase 60 allocator and Phase 61 control fan-out live — code complete; the audible mono↔poly UAT is deferred pending a working MIDI controller on Linux.**

## Performance

- **Duration:** ~3 min (Task 1-2 code work; commits 10:15 → 10:18 local)
- **Started:** 2026-05-31T15:15:12Z
- **Completed (code tasks):** 2026-05-31T15:18:04Z
- **Tasks:** 2 of 3 complete; Task 3 (human UAT) deferred — see below
- **Files modified:** 2 (33 insertions)

## Accomplishments
- Added `voiceCountBox` ComboBox + paired `voiceCountLabel` ("Voice Count") to the standalone sampler voice panel, populated with items 1-24 (itemId == count), defaulted to 24 without firing onChange.
- Control is standalone-only by construction — added to `samplerWindow->getPanel()` and bounded inside the `if (samplerWindow)` block in `resized()` (no plugin-surface code, D-03).
- Wired `voiceCountBox.onChange` to `processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId())` — the count passes straight through (no ID arithmetic), reaching the Phase 60 setter (which clamps [1,24], handles ring-out on decrease + count-bounded allocation) and the Phase 61 fan-out (per-voice controls follow the new count).
- Standalone Release target (`spu94_plugin_Standalone`) compiled and linked clean during Task 2 (prior executor's verified build; not re-run — wiring confirmed present in source).

## Task Commits

Each code task was committed atomically:

1. **Task 1: Declare + set up voiceCountBox in the standalone voice panel** - `9e46e4a` (feat)
2. **Task 2: Wire voiceCountBox.onChange → setActiveVoiceCount + Release build** - `26c0d9a` (feat)
3. **Task 3: Human UAT — audible polyphony change** - **DEFERRED / pending human verification** (no commit; see Deferred section)

**Plan metadata:** committed separately (docs: complete plan).

## Files Created/Modified
- `src/plugin/PluginEditor.h` - Declared `juce::ComboBox voiceCountBox;` + paired `voiceCountLabel` member (line 151).
- `src/plugin/PluginEditor.cpp` - Setup block (items 1-24, default id 24 `dontSendNotification`, tooltip, "Voice Count" label, lines 73-81); onChange lambda → `setActiveVoiceCount(getSelectedId())` (lines 327-330); layout bounds inside `if (samplerWindow)` (line 1772).

## Decisions Made
None beyond the plan's pre-decided D-01..D-07. Implementation followed the `recordModeBox` clone idiom and itemId-as-count convention exactly as specified.

## Deviations from Plan

None - plan executed exactly as written. (Two source files modified, no auto-fixes required; the only departure from the planned task sequence is the deferral of the Task 3 human UAT, documented below — that is a verification deferral, not a code deviation.)

## Issues Encountered

None during code execution (Tasks 1-2). The Task 3 audible verification could not be performed — see Deferred Verification.

## Deferred Verification (Task 3 — Human UAT)

**Status: DEFERRED / pending human verification. NOT approved, NOT confirmed.**

Task 3 is a `checkpoint:human-verify` step requiring a human to launch the Release standalone, trigger samples, and audibly confirm that changing the Voice Count dropdown changes polyphony immediately and that the displayed count matches the number of voices actually sounding.

**Why deferred:** The standalone currently has no working MIDI controller on Linux, so notes cannot be played to exercise the mono↔poly behavior. Troubleshooting Linux MIDI input is out of scope for this milestone. The audible behavior therefore could **not** be tested and is **not** confirmed.

**What this means for the success criteria:**
- The *code/build* criteria (VCOUNT-01 control present 1-24; onChange → setActiveVoiceCount; Release standalone builds clean; no plugin-surface code) are met and verified via source assertions + the prior clean Release build.
- The *audible behavioral* criteria — ROADMAP Phase 62 criteria 2-4 (immediate mono↔poly with no reload; per-voice controls follow the new active set; displayed count == voices actually sounding under playback) — are **NOT yet human-confirmed**. They are wired correctly by construction (the dropdown is the sole driver of `setActiveVoiceCount`, and Phases 60/61 are already proven), but they have not been heard.

**Tracked for later:** Pending human UAT once a working MIDI input path exists on the standalone (or via an alternative note source). Logged as HUMAN-UAT for follow-up.

## Next Phase Readiness
- Code surface for VCOUNT-01 / VCOUNT-03 is delivered; Phase 63 (persistence) can serialize the active voice count into the existing `[voice]` INI section, defaulting to 24 for back-compat.
- **Open item:** the audible mono↔poly UAT (Task 3) remains pending human verification — it does not block the code work or Phase 63, but the behavioral criteria should not be reported as confirmed until a human has heard the polyphony change.

## Self-Check: PARTIAL (code/build PASSED; audible UAT NOT confirmed)

**Code & build criteria — PASSED:**
- `src/plugin/PluginEditor.h:151` contains `juce::ComboBox voiceCountBox;` — FOUND.
- `src/plugin/PluginEditor.cpp` contains `voiceCountBox.addItem` (1-24 loop), `voiceCountBox.setSelectedId(24, juce::dontSendNotification)`, `panel.addAndMakeVisible(voiceCountBox)`, "Voice Count" label, and `voiceCountBox.setBounds` inside the `if (samplerWindow)` block — all FOUND (lines 73-81, 1772).
- `src/plugin/PluginEditor.cpp:327-330` contains `voiceCountBox.onChange` → `processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId())` — FOUND.
- Commit `9e46e4a` (Task 1) — FOUND in git log.
- Commit `26c0d9a` (Task 2) — FOUND in git log.
- Standalone Release build: confirmed clean by the prior executor at Task 2; not re-run this session (wiring verified present in source).

**Audible behavioral criteria — NOT CONFIRMED (deferred):**
- ROADMAP Phase 62 criteria 2 (immediate mono↔poly), 3 (controls follow active set), 4 (displayed == actual under playback) require human listening and are **pending human verification** — MIDI controller unavailable on Linux (out of milestone scope). These are explicitly NOT claimed as passed.

---
*Phase: 62-voice-count-selector*
*Completed (code): 2026-05-31 — Task 3 audible UAT deferred/pending human verification*
