---
phase: 62-voice-count-selector
verified: 2026-05-31T00:00:00Z
status: human_needed
score: 2/5 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Launch Release standalone; confirm Voice Count dropdown is visible in the sampler voice panel, reads 24 on startup, and lists values 1 through 24"
    expected: "Dropdown labeled 'Voice Count' is visible, shows 24 as the default, and its menu contains every integer from 1 to 24"
    why_human: "JUCE GUI layout and widget visibility cannot be confirmed from source alone; requires launching the running standalone"
  - test: "With a sample loaded and voices triggering, set Voice Count to 1 and play multiple notes"
    expected: "Sampler becomes monophonic — each new note takes over the single active voice (last-note priority); no reload or restart occurs"
    why_human: "Audible behavior requires a working note input path (MIDI or trigger) and a human ear to confirm mono takeover; not automatable from source"
  - test: "Raise Voice Count from 1 to 8, then to 24, while triggering notes"
    expected: "Polyphony increases audibly and immediately at each step; per-voice controls (Level, Pan, ADSR, NON, PMON) reach the newly-active voices; no reload needed"
    why_human: "Polyphony increase, control fan-out to new voices, and zero-reload behavior are runtime/audible phenomena; cannot be verified by grep"
  - test: "Change Voice Count several times while notes are sounding; observe the dropdown at each step"
    expected: "The displayed dropdown value always matches the number of voices the sampler is actually using — the two never diverge"
    why_human: "Display/actual sync under live playback is a runtime behavioral check requiring a human observer"
---

# Phase 62: Voice-Count Selector Verification Report

**Phase Goal:** The player sets the active voice count from a control in the sampler window, and the sampler immediately plays — and is controlled — at that count.
**Verified:** 2026-05-31
**Status:** human_needed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

The ROADMAP defines four success criteria for Phase 62. Two are fully verifiable from source; the remaining three are audible/behavioral and require human testing.

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Sampler window shows a 1-24 "Voice Count" control (ROADMAP SC-1) | VERIFIED | `PluginEditor.h:151` declares `juce::ComboBox voiceCountBox;` + `voiceCountLabel`. Setup in `.cpp:73-81`: `panel.addAndMakeVisible(voiceCountBox)`, loop `n=1..24` with `addItem(String(n), n)`, `setSelectedId(24, dontSendNotification)`, label text "Voice Count". Standalone-only by construction via `panel` (not editor). |
| 2 | On startup the control reads 24 matching the engine default; no audible change until lowered (PLAN D-05) | VERIFIED | `.cpp:76`: `voiceCountBox.setSelectedId(24, juce::dontSendNotification)` — default is 24, `dontSendNotification` suppresses onChange at construction. Engine default is `activeVoiceCount{24}` (Phase 60). The two values are identical; no divergence is possible at startup. |
| 3 | Moving the control to 1 makes the sampler monophonic; raising adds polyphony, audibly and immediately (ROADMAP SC-2) | HUMAN NEEDED | Code wiring to `setActiveVoiceCount` is confirmed (see Key Links). Phase 60's setter handles ring-out + mono allocation. However, audible behavior under playback has not been heard; MIDI input is unavailable on Linux at time of phase completion. |
| 4 | Per-voice controls and allocation follow the new count straight away (ROADMAP SC-3) | HUMAN NEEDED | Phase 61's `applyContinuousVoiceControls()` fan-out across `[0, activeVoiceCount)` is already proven. The onChange wiring routes through `setActiveVoiceCount` which updates the count the fan-out reads. Correctness is strongly implied by construction, but audible confirmation with a working note source is pending. |
| 5 | Displayed count always matches the engine count (ROADMAP SC-4) | HUMAN NEEDED | The dropdown is the sole driver of `setActiveVoiceCount`; there is no independent code path that updates `activeVoiceCount` without going through the dropdown (or a future preset-load path not yet built). Display == actual is true by construction, but runtime verification under playback has not been performed. |

**Score:** 2/5 truths verified by code inspection; 3/5 pending human testing.

Note: the 3 pending truths are not failed — the code is correctly wired by construction and the architectural risk is low. They are deferred to human UAT because the behavior is audible and requires a working note input path.

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/plugin/PluginEditor.h` | `juce::ComboBox voiceCountBox` + `voiceCountLabel` member declared in standalone voice panel group | VERIFIED | Line 151-152 contain both declarations immediately after the `noiseColorKnob`/`noiseColorLabel` group, under the comment block noting Phase 62 / VCOUNT-01/03. |
| `src/plugin/PluginEditor.cpp` | Setup block: items 1-24 (itemId == count), default id 24 with dontSendNotification, `panel.addAndMakeVisible`, "Voice Count" label, onChange lambda, setBounds inside `if (samplerWindow)` | VERIFIED | Lines 69-81 (setup), 327-330 (onChange), 1771-1772 (bounds). All four parts of the recordModeBox idiom are present. |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `PluginEditor.cpp:327-330` `voiceCountBox.onChange` | `SPU94AudioProcessor::setActiveVoiceCount` | `processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId())` | VERIFIED | `.cpp:329` reads exactly `processorRef.setActiveVoiceCount(voiceCountBox.getSelectedId());`. `getSelectedId()` returns the itemId which equals the voice count (set by the `n=1..24` loop). No ID arithmetic, no offset. |
| `PluginEditor.cpp:73` setup block | `samplerWindow->getPanel()` (standalone-only panel) | `panel.addAndMakeVisible(voiceCountBox)` | VERIFIED | `.cpp:73` and `.cpp:78` add both box and label to `panel` (the `samplerWindow->getPanel()` reference, not the editor). Bounds at lines 1771-1772 sit inside the `if (samplerWindow) {` block that opens at line 1695, confirmed by indentation and brace trace. |

---

### Data-Flow Trace (Level 4)

The voiceCountBox is a control-output widget (it drives state; it does not render dynamic data from an upstream source). Level 4 data-flow tracing applies to components that render upstream data. The applicable trace runs in reverse: user selection -> `getSelectedId()` -> `setActiveVoiceCount(n)` -> `activeVoiceCount` atomic store -> audio-thread allocator reads. That path is fully code-confirmed at the GUI→engine boundary. The downstream audio behavior (what the engine does with the count) was proven in Phase 60's CTest suite and is not re-litigated here.

---

### Behavioral Spot-Checks

Step 7b is partially applicable. The Release standalone target was confirmed to build cleanly by the executor at Task 2 (commit `26c0d9a`). A re-build was not performed during this verification session because it requires a full CMake compile (~several minutes) and the build state is known from the commit. The code-level wiring checks above serve as the automatable confirmation; runtime behavioral checks are routed to human verification.

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| voiceCountBox onChange wiring present | `grep -Eq 'processorRef\.setActiveVoiceCount\(\s*voiceCountBox\.getSelectedId\(\)\s*\)'` | Match found at `.cpp:329` | PASS |
| items 1-24 populated with itemId == count | `grep -q 'voiceCountBox.addItem(juce::String(n), n)'` | Loop at `.cpp:74-75` confirmed | PASS |
| Default id 24 with dontSendNotification | `grep -Eq 'voiceCountBox\.setSelectedId\(24'` | `.cpp:76` confirmed | PASS |
| Standalone-only guard (setBounds inside if block) | brace-depth trace at line 1695-1772 | Depth = 1 throughout; closes after the section | PASS |
| Release build clean | Commit `26c0d9a` build step | Prior executor confirmed exit 0; not re-run | PASS (prior) |
| Audible mono->poly change | Requires running standalone + note input | MIDI unavailable on Linux | SKIP (human needed) |

---

### Probe Execution

No probe scripts were declared in the PLAN or exist under `scripts/*/tests/probe-*.sh` for this phase. Step 7c: not applicable.

---

### Requirements Coverage

The PLAN declares requirements `VCOUNT-01` and `VCOUNT-03`. Both appear in REQUIREMENTS.md Phase 62 row.

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| VCOUNT-01 | 62-01-PLAN.md | User can set number of active sampler voices (1-24) from a control in the sampler window | SATISFIED | `voiceCountBox` declared, populated 1-24, added to standalone panel, labeled "Voice Count" — all four parts verified in source |
| VCOUNT-03 | 62-01-PLAN.md | Changing the voice count takes effect immediately, with controls and note allocation following the new count | PARTIAL — code satisfied; audible confirmation pending | `onChange` calls `setActiveVoiceCount(getSelectedId())` which drives Phase 60 allocator + Phase 61 fan-out; audible immediacy requires human UAT |

**Orphaned requirements check:** REQUIREMENTS.md maps VCOUNT-02 to Phase 60 (complete), VCOUNT-04 to Phase 63 (pending). Neither is claimed by Phase 62. No orphaned requirements for this phase.

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none found) | — | — | — | — |

Scanned `src/plugin/PluginEditor.h` and `src/plugin/PluginEditor.cpp` for `TBD`, `FIXME`, `XXX`, `TODO`, `HACK`, `PLACEHOLDER`, `return null`, `return {}`, `return []`, placeholder strings, console.log-only handlers. No blockers found. The `onChange` lambda body is a single real call (not a stub). The items loop is real and complete. No hardcoded empty values flow to the voice-count path.

---

### Human Verification Required

The following items cannot be confirmed from source inspection. They require launching the Release standalone with a working note-trigger path.

#### 1. Control Visible and Populated at Startup

**Test:** Launch the Release standalone (`build/src/plugin/spu94_plugin_artefacts/Release/Standalone/SPU-94`), open the sampler window, and inspect the voice panel.
**Expected:** A dropdown labeled "Voice Count" is visible in the voice panel, shows 24 as its initial value, and its menu lists every integer from 1 to 24.
**Why human:** JUCE widget visibility, panel layout, and dropdown population can only be confirmed by running the GUI.

#### 2. Count = 1 Produces Monophonic Behavior

**Test:** Load a sample so voices can be triggered. Set Voice Count to 1 and trigger multiple overlapping notes.
**Expected:** Only one voice sounds at a time; each new note takes over the single active voice with last-note priority. No reload or restart of the standalone is needed.
**Why human:** Mono takeover is an audible runtime behavior. The code paths through Phase 60's allocator, but hearing the result requires a note source (MIDI or trigger) that is currently unavailable on Linux.

#### 3. Raising Count Adds Polyphony Immediately; Controls Follow

**Test:** With sample triggering, raise Voice Count from 1 to 8, then to 24. Play chords at each step. Adjust Level and Pan controls after each change.
**Expected:** Polyphony increases audibly at each step with no reload. Per-voice controls (Level, Pan, ADSR shape, NON, PMON) audibly affect the newly-active voices (Phase 61 fan-out following the new count).
**Why human:** Polyphony headroom and control fan-out require audible verification across multiple active voices.

#### 4. Displayed Count Matches Actual Count Under Playback

**Test:** Change the Voice Count dropdown several times while notes are sounding.
**Expected:** The displayed dropdown value always matches how many voices the sampler is actually allocating — the display and the engine never diverge.
**Why human:** Display/actual sync is a runtime behavioral property; the source makes it true by construction (dropdown is the sole driver), but the assertion has not been exercised under live conditions.

---

### Gaps Summary

No code-level gaps. The two verified truths (control declared and wired) are fully satisfied. The three pending truths are not failures — they are correctly wired by construction and backed by proven Phase 60/61 engine code. They are human_needed because audible confirmation with a working note input path has not yet occurred.

The overall status is `human_needed`, not `gaps_found`. No plan revision is required. When a MIDI or trigger path is available on the Linux standalone, the four human verification steps above complete the phase.

---

_Verified: 2026-05-31_
_Verifier: Claude (gsd-verifier)_
