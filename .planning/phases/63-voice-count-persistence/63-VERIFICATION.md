---
phase: 63-voice-count-persistence
verified: 2026-05-31T18:30:00Z
status: passed
score: 5/5 must-haves verified
overrides_applied: 0
---

# Phase 63: Voice-Count Persistence — Verification Report

**Phase Goal:** A saved preset remembers how many voices were active, so reopening or
reloading it restores the same voice count.
**Verified:** 2026-05-31
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | D-01: Saving a preset records the active voice count into the .spu94 [voice] text section only | VERIFIED | `active_voices=%d` at PluginProcessor.cpp:1861, inside `[voice]` block (lines 1852-1869), relaxed-load snapshot matching all 13 sibling save lines |
| 2 | D-02: Count NOT written to binary DAW/session state (getStateInformation / StateSerializer) | VERIFIED | `getStateInformation` (lines 2812-2830) and `setStateInformation` (lines 2832+) contain no references to `active_voices`, `activeVoiceCount`, or `getActiveVoiceCount`; grepped both functions, zero matches |
| 3 | D-04: Loading restores the count through setActiveVoiceCount (clamped 1-24, held notes ring out) | VERIFIED | `setActiveVoiceCount(restoredCount)` at line 2008, after parse loop and before `std::memcpy(pendingPresetBuf...)` at line 2010; no raw `activeVoiceCount.store()` present in restore path |
| 4 | D-05: After a load the standalone voiceCountBox shows the restored value (setSelectedId + dontSendNotification) | VERIFIED | `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), juce::dontSendNotification)` at PluginEditor.cpp:1990, inside `syncMixerKnobsFromProcessor()` (lines 1940-1991), last statement before closing brace; human-verify gate approved by user (set to 6, saved, changed to 18, loaded — selector snapped to 6; user replied "approved.") |
| 5 | D-03: A .spu94 with no count key restores to 24 (pre-feature back-compat) | VERIFIED | `int restoredCount = 24` seeded at line 1921 before parse loop; capture clause at line 1955 assigns to local only; absent key leaves seed intact; `voice_persist_backcompat` CTest PASSES |

**Score:** 5/5 truths verified

---

## ROADMAP Success Criteria

| # | Criterion | Status | Evidence |
|---|-----------|--------|----------|
| 1 | Saving a preset records the current active voice count (.spu94 text only; binary DAW state deferred per D-02) | VERIFIED | Save line at PluginProcessor.cpp:1861; `voice_persist_roundtrip` CTest: 7 saved, 7 restored, exit 0 |
| 2 | Loading restores the saved count AND the selector shows the restored value | VERIFIED | `setActiveVoiceCount(restoredCount)` at :2008 (headless); `voiceCountBox.setSelectedId(...)` at PluginEditor.cpp:1990 (GUI); human-verify gate APPROVED |
| 3 | Presets saved before this feature load cleanly, defaulting to 24 voices | VERIFIED | Seed-then-override pattern; `voice_persist_backcompat` CTest PASSES |

---

## Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/plugin/PluginProcessor.h` | `getActiveVoiceCount()` const acquire getter | VERIFIED | Line 286: `int getActiveVoiceCount() const { return activeVoiceCount.load(std::memory_order_acquire); }`; atomic `activeVoiceCount` stays private at line 447 |
| `src/plugin/PluginProcessor.cpp` | save line + seed-then-override parse + `setActiveVoiceCount` apply | VERIFIED | Save: :1861 (`active_voices=%d`, relaxed); seed: :1921 (`int restoredCount = 24`); capture: :1955 (into local, not atomic store); apply: :2008 (before `memcpy` at :2010) |
| `src/plugin/PluginEditor.cpp` | `voiceCountBox.setSelectedId(processorRef.getActiveVoiceCount(), ...)` in `syncMixerKnobsFromProcessor` | VERIFIED | Line 1990, inside function body (1940-1991), last statement; `juce::dontSendNotification` present |
| `tests/plugin/test_voice_persist.cpp` | Headless round-trip + back-compat + clamp cases | VERIFIED | File exists; 3 `bool test_*()` functions; `makePreparedProcessor()` helper calls `prepareToPlay(44100.0, 512)` so `engines[0]` is live before load; all cases exercise `getActiveVoiceCount()` |
| `tests/plugin/CMakeLists.txt` | `test_voice_persist` target + 3 `add_test` entries | VERIFIED | Target at line 292; 3 `add_test` entries at lines 337-342: `voice_persist_roundtrip`, `voice_persist_backcompat`, `voice_persist_clamp`; 11 occurrences of `test_voice_persist` total |

---

## Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `savePresetToString` [voice] block | `activeVoiceCount` | `snprintf active_voices=%d` relaxed load | VERIFIED | PluginProcessor.cpp:1861; key literal `active_voices`; reuses `char line[128]` from :1850 |
| `loadPresetFromString` SEC_VOICE | `setActiveVoiceCount(restoredCount)` | Seeded local (24) captured in parse, applied after loop | VERIFIED | Seed :1921; capture :1955 into `restoredCount`; apply :2008 through the clamping setter |
| `syncMixerKnobsFromProcessor` | `voiceCountBox` | `setSelectedId(getActiveVoiceCount(), dontSendNotification)` | VERIFIED | PluginEditor.cpp:1990; `dontSendNotification` breaks the `onChange` -> `setActiveVoiceCount` feedback path (lines 327-330) |

---

## Data-Flow Trace (Level 4)

`voiceCountBox` at PluginEditor.cpp:1990 renders the result of `processorRef.getActiveVoiceCount()`, which reads the `activeVoiceCount` atomic (PluginProcessor.h:447). That atomic is populated by `setActiveVoiceCount(restoredCount)` at :2008 where `restoredCount` is the seed-then-override value parsed from the `active_voices=N` preset line. The save side writes `activeVoiceCount.load(relaxed)` into the text. No hardcoded empty props; no static return in the API path.

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `PluginEditor.cpp:1990` voiceCountBox | `processorRef.getActiveVoiceCount()` | `activeVoiceCount` atomic, set by `setActiveVoiceCount(restoredCount)` at :2008 with value parsed from preset text | Yes — real int from preset, clamped 1-24 | FLOWING |

---

## Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Round-trip: save count 7 (instance A), load into B, getter returns 7 | `ctest --test-dir build -R voice_persist_roundtrip` | exit 0, 1/1 PASSED | PASS |
| Back-compat: [voice] section with no count key restores to 24 | `ctest --test-dir build -R voice_persist_backcompat` | exit 0, 1/1 PASSED | PASS |
| Clamp: `active_voices=0` -> 1; `active_voices=999` -> 24 | `ctest --test-dir build -R voice_persist_clamp` | exit 0, 1/1 PASSED | PASS |
| Regression: state/preset/voice-alloc/voice-controls/bus/mono suites | `ctest --test-dir build -R "state|preset|voice|bus|mono|controls|alloc"` | 34/34 PASSED | PASS |

---

## Probe Execution

No `scripts/*/tests/probe-*.sh` probes defined for this phase. Phase used inline `<automated>` CTest blocks, all run above.

---

## Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| VCOUNT-04 | 63-01-PLAN.md | Active voice count saved to and restored from presets/system state | SATISFIED | Save :1861, seed-then-override parse, `setActiveVoiceCount` restore, GUI snap, 3/3 CTest PASS, human-verify approved |

REQUIREMENTS.md traceability: VCOUNT-04 -> Phase 63 -> Complete. No other requirements mapped to Phase 63. No orphaned requirements.

---

## Anti-Patterns Found

Scanned `PluginProcessor.h`, `PluginProcessor.cpp`, `PluginEditor.cpp`, `test_voice_persist.cpp`, `tests/plugin/CMakeLists.txt` for `TBD`, `FIXME`, `XXX` on lines touching phase-63 code paths. Zero matches. No stubs, no placeholder returns, no hardcoded empty props in the data path.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | None | — | — |

---

## Human Verification

The GUI snap (ROADMAP criterion 2's visual portion) was verified during plan execution at the `checkpoint:human-verify` gate:

- User launched Release standalone, set Voice Count to 6, saved a `.spu94`, changed selector to 18, loaded the `.spu94`.
- Voice Count selector snapped back to 6.
- User response: **"approved."**

No additional human verification items remain. All automated checks passed. GUI snap is confirmed human-approved.

---

## Gaps Summary

None. All five must-haves verified, all three ROADMAP success criteria satisfied, 3/3 voice-persist CTests pass, 34/34 regression tests pass, human-verify gate approved.

---

_Verified: 2026-05-31_
_Verifier: Claude (gsd-verifier)_
