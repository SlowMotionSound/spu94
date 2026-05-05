---
phase: 18-i-o-surfaces
verified: 2026-05-04T00:54:55Z
status: human_needed
score: 4/4
overrides_applied: 0
human_verification:
  - test: "Launch standalone, load WAV, play with Hall preset. Switch to INT mode, type BPM 120. Verify audible change in reverb timing. Change global subdivision from 1/4 to 1/8 -- listen for shorter delay taps."
    expected: "Reverb character audibly changes when BPM or subdivision changes. Delay taps lock to musical grid."
    why_human: "Audible change in reverb character cannot be verified programmatically."
  - test: "In INT mode, pull one per-register dropdown (e.g. dCOMB1) from Global to 1/2. Verify it stays at 1/2 while other registers follow Global. Drag a register slider manually -- verify its dropdown transitions to Free."
    expected: "Per-register subdivision overrides work independently. Manual slider drag triggers C core write-interception and transitions dropdown to Free."
    why_human: "GUI interaction and visual state transition requires human observation."
  - test: "Switch to EXT mode. Open Options -> Audio/MIDI Settings. Verify MIDI device list is accessible. If MIDI clock source available, verify BPM display tracks external tempo."
    expected: "EXT mode selectable without crash. BPM field read-only. MIDI device selection available. With MIDI clock: BPM display tracks source within 1-2 beats."
    why_human: "Requires MIDI hardware or virtual MIDI setup. Real-time external clock behavior cannot be verified statically."
  - test: "Save a .spu94 preset with tempo state (INT mode, BPM 120, custom subdivisions). Load a different preset. Re-load the saved file. Verify BPM, mode, and subdivision dropdowns restore."
    expected: "Full tempo state round-trips through file preset save/load."
    why_human: "GUI state restoration after file load requires visual verification."
---

# Phase 18: I/O Surfaces Verification Report

**Phase Goal:** Users can set BPM and select subdivisions through the CLI and the JUCE standalone GUI
**Verified:** 2026-05-04T00:54:55Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `spu94 reverb --tempo 120 input.wav output.wav` processes audio with delay registers snapped to the specified BPM | VERIFIED | CLI has `case 1013` parsing --tempo, calls `spu94_set_tempo(state, tempo_bpm)` at cmd_reverb.c:470 after preset loading. 9/9 pytest tests pass including `test_tempo_with_factory_preset` (exit 0, valid WAV produced). |
| 2 | The JUCE GUI has a BPM field where the user can type a tempo value, and delay registers update when BPM changes | VERIFIED | `bpmField` declared in PluginEditor.h:74, constructed with `setRange(1.0, 999.0, 1.0)` at PluginEditor.cpp:348. `onValueChange` stores to `processorRef.getTempoBpm()` at line 354. processBlock reads atomic and calls `spu94_set_tempo(spu, bpm)` at PluginProcessor.cpp:334 when BPM changes. Full data path wired. |
| 3 | The JUCE GUI provides subdivision selectors (per-register or global mode) that snap delay registers to musical divisions | VERIFIED | `globalSubSelector` (ComboBox with 15 subdivisions from `spu94_subdivision_to_string`) at PluginEditor.cpp:364-369. 10 `perRegDropdowns` with Free/Global/Individual tiers at lines 375-392. processBlock calls `spu94_set_subdivision` for changed registers at PluginProcessor.cpp:318,343,358. |
| 4 | Changing BPM or subdivision in the GUI produces an audible change in the reverb character | VERIFIED (code path) | Data flows: GUI widget -> atomic store -> processBlock reads -> `spu94_set_tempo`/`spu94_set_subdivision` -> C core resnaps delay registers -> `spu94_process` uses new values. Code path is complete. **Audible verification requires human testing.** |

**Score:** 4/4 truths verified (code-level)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/cli/cmd_reverb.c` | --tempo flag parsing and tempo application | VERIFIED | case 1013 at line 226, strtol validation, spu94_set_tempo call at line 470, help text at line 123 |
| `tests/cli/test_cli_tempo.py` | CLI integration tests for --tempo (min 60 lines) | VERIFIED | 109 lines, 9 test functions covering valid BPM, invalid inputs, missing preset, help text |
| `tests/cli/CMakeLists.txt` | ctest registration for tempo tests | VERIFIED | Contains `test_cli_tempo` and `test_cli_mixer_dac` in _cli_tests list |
| `src/standalone/PluginProcessor.h` | Tempo atomic declarations | VERIFIED | SyncMode enum at line 14, tempoBpm/syncMode/globalSubdivision/perRegSub atomics at lines 133-140, binding shadow arrays at lines 149-150, MIDI clock state at lines 153-157, acceptsMidi returns true at line 33 |
| `src/standalone/PluginProcessor.cpp` | processBlock tempo state push to C core | VERIFIED | Tempo push block at lines 262-374, MIDI clock processing at 268-300, file preset tempo sync at 167-190, save preset tempo push at 446-466 |
| `src/standalone/PluginEditor.h` | Tempo GUI widget declarations | VERIFIED | syncModeSelector at line 72, bpmField at line 74, globalSubSelector at line 76, perRegDropdowns[10] at line 80, sentinel IDs at lines 84-86, PresetSnapshot tempo fields at lines 103-106 |
| `src/standalone/PluginEditor.cpp` | Tempo GUI construction, layout, timer sync, preset interaction | VERIFIED | syncModeSelector with FREE/INT/EXT at lines 312-341, bpmField at 345-358, globalSubSelector at 364-372, perRegDropdowns at 375-399, resized layout, timerCallback EXT sync at 551-560, captureBaseline/checkModified with tempo at 786-819 |
| `src/standalone/CMakeLists.txt` | NEEDS_MIDI_INPUT TRUE | VERIFIED | Line 13: `NEEDS_MIDI_INPUT            TRUE` |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| cmd_reverb.c | spu94_set_tempo | C function call after preset loading | WIRED | `spu94_set_tempo(state, tempo_bpm)` at line 470, after spu94_tick and factory preset subdivision setup |
| cmd_reverb.c | spu94_set_subdivision | Default subdivision loop for factory presets | WIRED | `spu94_set_subdivision(state, r, SPU94_SUB_1_4)` at line 465 for FIXED registers when preset_name is set |
| PluginEditor.cpp | PluginProcessor.h | Atomic stores from GUI widgets | WIRED | `processorRef.getTempoBpm().store()` at line 354, `processorRef.getSyncMode().store()` at line 320, `processorRef.getGlobalSubdivision().store()` at line 567, `processorRef.getPerRegSub(r).store()` in dropdown callbacks |
| PluginProcessor.cpp | spu94_set_tempo | processBlock reads atomics and calls C core | WIRED | `spu94_set_tempo(spu, bpm)` at lines 326, 334 gated on BPM change detection |
| PluginProcessor.cpp | spu94_set_subdivision | processBlock applies per-register subdivisions | WIRED | `spu94_set_subdivision(spu, r, sub)` at lines 318, 343, 358 for mode transition and steady-state changes |
| CMakeLists.txt | JUCE standalone wrapper | NEEDS_MIDI_INPUT enables MIDI routing | WIRED | `NEEDS_MIDI_INPUT TRUE` at line 13, `acceptsMidi()` returns true at PluginProcessor.h:33 |
| PluginProcessor.cpp | tempoBpm atomic | MIDI clock BPM derivation stores to atomic | WIRED | `tempoBpm.store(derivedBpm, ...)` at line 293 inside MIDI clock processing block |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| PluginEditor.cpp (bpmField) | processorRef.getTempoBpm() | User types BPM in INT, MIDI clock in EXT | Yes -- stores to atomic, read by processBlock | FLOWING |
| PluginEditor.cpp (syncModeSelector) | processorRef.getSyncMode() | User selects FREE/INT/EXT | Yes -- stores to atomic, controls mode transitions in processBlock | FLOWING |
| PluginEditor.cpp (globalSubSelector) | processorRef.getGlobalSubdivision() | User selects subdivision | Yes -- stores to atomic, processBlock resnaps Global registers | FLOWING |
| PluginEditor.cpp (perRegDropdowns) | processorRef.getPerRegSub(r) | User selects Free/Global/Individual | Yes -- stores to atomic, processBlock calls spu94_set_subdivision per register | FLOWING |
| PluginProcessor.cpp (MIDI clock) | tempoBpm atomic | MIDI clock 0xF8 intervals | Yes -- moving average BPM derivation at lines 282-293 | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| CLI --tempo 120 with hall preset exits 0 | `spu94 reverb --preset hall --tempo 120 input.wav output.wav` | exit 0, valid WAV | PASS |
| CLI --tempo 0 rejected | `spu94 reverb --preset hall --tempo 0 ...` | exit 2, "invalid" in stderr | PASS |
| CLI --tempo abc rejected | `spu94 reverb --preset hall --tempo abc ...` | exit 2, "invalid" in stderr | PASS |
| CLI --help shows --tempo | `spu94 reverb --help` | "--tempo" in stdout | PASS |
| All 9 CLI tempo tests pass | `pytest tests/cli/test_cli_tempo.py -v` | 9 passed in 0.14s | PASS |
| CLI binary builds clean | `cmake --build . --target spu94_cli` | [100%] Built target spu94_cli | PASS |
| Standalone binary builds clean | `cmake --build . --target spu94_standalone` | [100%] Built target spu94_standalone | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| TEMPO-07 | 18-01 | `--tempo` flag sets BPM before processing | SATISFIED | cmd_reverb.c case 1013 parses --tempo, calls spu94_set_tempo after preset loading. 9/9 tests pass. |
| TEMPO-08 | 18-02, 18-03 | BPM field in the standalone GUI | SATISFIED | bpmField widget in PluginEditor.cpp with range 1-999, atomic bridge to processBlock, EXT mode MIDI clock BPM derivation. |
| TEMPO-09 | 18-02 | Subdivision selectors for delay registers (or a global subdivision mode) | SATISFIED | globalSubSelector + 10 perRegDropdowns with Free/Global/Individual tiers, atomic bridge to processBlock calling spu94_set_subdivision. |

No orphaned requirements found. TEMPO-10 (round-trip test) is Phase 19 scope.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | -- | No TODO, FIXME, placeholder, stub, or debug logging found | -- | -- |

No anti-patterns detected in any of the 7 modified files.

### Human Verification Required

### 1. Audible Tempo-Locked Reverb

**Test:** Launch standalone, load WAV, play with Hall preset. Switch to INT mode, type BPM 120. Change global subdivision from 1/4 to 1/8.
**Expected:** Reverb character audibly changes when BPM or subdivision changes. Shorter subdivisions produce tighter, more rapid delay taps.
**Why human:** Audible change in reverb character cannot be verified programmatically.

### 2. Per-Register Subdivision Independence

**Test:** In INT mode, pull one per-register dropdown (e.g. dCOMB1) from Global to 1/2. Drag a register slider manually.
**Expected:** Individual register keeps its assigned subdivision while others follow Global. Manual slider drag transitions that register's dropdown to "Free" (write-interception detection).
**Why human:** GUI interaction and visual state transition requires human observation.

### 3. MIDI Clock EXT Mode

**Test:** Switch to EXT mode. Open Options -> Audio/MIDI Settings. If MIDI clock source available, verify BPM display tracks external tempo.
**Expected:** EXT mode selectable without crash. BPM field read-only. MIDI device selection available. With MIDI clock: BPM display tracks source within 1-2 beats.
**Why human:** Requires MIDI hardware or virtual MIDI setup. Real-time external clock behavior cannot be verified statically.

### 4. File Preset Tempo Round-Trip

**Test:** Save a .spu94 preset with tempo state (INT mode, BPM 120, custom subdivisions). Load a different preset. Re-load the saved file.
**Expected:** BPM, mode, and subdivision dropdowns restore to saved values.
**Why human:** GUI state restoration after file load requires visual verification.

### Gaps Summary

No code-level gaps found. All 4 ROADMAP success criteria are supported by implemented code with complete data-flow paths. All 3 requirements (TEMPO-07, TEMPO-08, TEMPO-09) have implementation evidence.

4 items require human verification: audible reverb change, per-register subdivision independence, MIDI clock EXT mode, and file preset tempo round-trip. These are GUI/audio behaviors that cannot be verified through static code analysis or automated tests.

---

_Verified: 2026-05-04T00:54:55Z_
_Verifier: Claude (gsd-verifier)_
