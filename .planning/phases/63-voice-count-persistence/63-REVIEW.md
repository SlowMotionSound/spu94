---
phase: 63-voice-count-persistence
reviewed: 2026-05-31T13:05:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - src/plugin/PluginProcessor.h
  - src/plugin/PluginProcessor.cpp
  - src/plugin/PluginEditor.cpp
  - tests/plugin/test_voice_persist.cpp
  - tests/plugin/CMakeLists.txt
findings:
  critical: 0
  warning: 1
  info: 2
  total: 3
status: issues_found
---

# Phase 63: Code Review Report

**Reviewed:** 2026-05-31T13:05:00Z
**Depth:** standard
**Files Reviewed:** 5
**Status:** issues_found

## Summary

Phase 63 adds voice-count persistence to the plugin-layer `.spu94` text-preset path: a
`getActiveVoiceCount()` acquire-load getter, an `active_voices=%d` save line, a
seed-24-then-override parse clause + `setActiveVoiceCount(restoredCount)` restore call,
a `voiceCountBox` resync line in the editor, and a new headless round-trip test.

I traced all four thread-safety concerns called out in the brief and found the
implementation correct on each:

- **Atomic memory ordering.** `getActiveVoiceCount()` (acquire) pairs with
  `setActiveVoiceCount()`'s release store at `PluginProcessor.cpp:2673`. The restore
  call at line 2008 (release) is sequenced before `filePresetReady.store(true, release)`
  at line 2014; the audio thread then bumps `filePresetAppliedCount` (release, line 547),
  which the GUI timer polls (acquire, `PluginEditor.cpp:1328`) before invoking the
  resync. The GUI therefore observes the updated count — no torn read, no stale value.
- **Message-thread vs audio-thread access.** `setActiveVoiceCount`,
  `savePresetToString`, `loadPresetFromString`, and the editor resync all run on the
  message thread. The only audio-thread reader of `activeVoiceCount` is
  `allocateVoice`/`applyContinuousVoiceControls` via acquire loads — unchanged and
  still correct. The restore does NOT touch `engines[0]` directly (consistent with the
  CR-02 discipline elsewhere in the file).
- **Parse / buffer bounds.** The save buffer is `char line[128]`; `active_voices=%d\n`
  with a 32-bit int maxes at ~25 chars — no `snprintf` truncation or overflow. The
  load path keeps the existing `len >= sizeof(pendingPresetBuf)` guard before the
  `memcpy`. No new unbounded copy is introduced.
- **Back-compat default.** The seed-then-override is correct: `restoredCount`
  defaults to 24 and is only overwritten when the `active_voices` key is present, so a
  pre-feature preset forces the full rig rather than leaving the live count. The restore
  routes through `setActiveVoiceCount`, whose `juce::jlimit(1,24,n)` clamp means
  malformed values (`getIntValue()` yields 0 on junk) land safely in `[1,24]`.

The save literal and the parse `key ==` literal are byte-identical (`"active_voices"`),
so the round-trip cannot silently break on a key-string drift. The `voiceCountBox`
holds item IDs 1-24 and `getActiveVoiceCount()` is clamped to that same range, so
`setSelectedId(..., dontSendNotification)` always resolves to a real item and the
`dontSendNotification` flag correctly suppresses the `onChange -> setActiveVoiceCount`
feedback loop.

One real defect remains: the clamp-high test sub-case is vacuous and cannot fail for
its stated purpose (Warning, WR-01). The two Info items document a deliberately-deferred
scope boundary and a verified-correct shared-sync interaction, recorded so a future
reviewer does not re-flag them.

## Warnings

### WR-01: Clamp-high test sub-case is vacuous — cannot catch an upper-bound regression

**File:** `tests/plugin/test_voice_persist.cpp:116-124`
**Issue:** The `active_voices=999` clamp sub-case constructs a fresh processor (default
`activeVoiceCount{24}`), loads a preset, and asserts the count equals 24. Because the
starting state already equals the expected result, this assertion passes *vacuously*:
it would still pass if the parser never read the `active_voices` key, if the
seed-then-override left `restoredCount` at its default 24, or if the upper bound of
`juce::jlimit(1, 24, n)` were broken (e.g. raised to 999). The test therefore provides
no protection for the high-clamp behavior it claims to cover ("`active_voices=999 -> 24`"
per the comment at line 100). Contrast the low sub-case (`active_voices=0 -> 1`), which
is sound because 1 differs from the default 24 and so genuinely exercises the parse +
clamp.
**Fix:** Force the instance away from 24 before loading so the expected result is only
reachable through a working parse + clamp:
```cpp
{
    auto p = makePreparedProcessor();
    p->setActiveVoiceCount(7);   // move off the default so 24 is only reachable via parse+clamp
    const juce::String high = "[preset]\nname=hi\n[voice]\nactive_voices=999\n";
    p->loadPresetFromString(high);
    if (p->getActiveVoiceCount() != 24)
    {
        std::printf("\n  FAIL: clamp high got %d expected 24", p->getActiveVoiceCount());
        ok = false;
    }
}
```
(Optionally also assert an in-range overshoot like `active_voices=25 -> 24`, which a
fresh-instance default can never accidentally satisfy.)

## Info

### IN-01: Voice count is not persisted in DAW binary session state (deliberate deferral, not a defect)

**File:** `src/plugin/PluginProcessor.cpp:2812-2861` (`getStateInformation` /
`setStateInformation`)
**Issue:** `activeVoiceCount` is a plugin-layer atomic that lives outside the C core.
The binary DAW-session path serializes via `StateSerializer::save`, which calls the
C-core `spu94_preset_save` directly (`StateSerializer.h:62`) — it never calls the
plugin's `savePresetToString`, so the `[voice] active_voices` key is absent from
session blobs. On `setStateInformation`, the text body is fed to the C-core
`spu94_preset_load`, which skips the unknown plugin section. Consequently a DAW
project save/reload resets the voice count to the engine default (24); only the manual
Save/Load `.spu94` file buttons persist it. This is recorded as Info, not a finding,
because decision **D-02** (`63-CONTEXT.md:45`, `63-RESEARCH.md:12`) explicitly scopes
this phase to the `.spu94` text path only and defers binary-session persistence to the
future "all sampler state in the session blob" work. Flagging it would contradict a
documented scope decision. Noted so it is not mistaken for an oversight, and so the
shared-sync interaction in IN-02 is understood in context.
**Fix:** None required for Phase 63. Revisit when the deferred DAW-plugin Voice Count
control and the broader session-blob question are taken up.

### IN-02: Editor resync is shared by the DAW-restore path — verified benign

**File:** `src/plugin/PluginEditor.cpp:1327-1333`, `1990`
**Issue:** `syncMixerKnobsFromProcessor()` (which now contains the `voiceCountBox`
resync) is triggered whenever `getFilePresetAppliedCount()` changes. That counter is
bumped by BOTH the text-preset path AND the binary DAW-restore path
(`setStateInformation` -> audio thread -> `filePresetAppliedCount.fetch_add`). After a
DAW restore the resync therefore reads `getActiveVoiceCount()`, which per IN-01 returns
the live (un-restored, default 24) value. I traced this to confirm it is *not* a UI
inconsistency: the box is set to whatever the engine count actually is, so it stays
truthful about the running state — it simply reflects the (deliberately) un-persisted
default rather than a phantom value. No fix needed; documented so the IN-01 deferral and
this shared-path behavior are not later misread as a desync bug.
**Fix:** None. Behavior is consistent with the engine state and with D-02.

---

_Reviewed: 2026-05-31T13:05:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
