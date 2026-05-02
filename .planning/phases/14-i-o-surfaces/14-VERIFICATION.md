---
phase: 14-i-o-surfaces
verified: 2026-05-02T17:15:00Z
status: passed
score: 4/4
overrides_applied: 0
---

# Phase 14: I/O Surfaces Verification Report

**Phase Goal:** Users can save and load .spu94 presets through both CLI subcommands and JUCE GUI buttons
**Verified:** 2026-05-02T17:15:00Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `spu94 preset-dump` writes the current engine state to stdout (default) or a named .spu94 file | VERIFIED | `./build/src/cli/spu94 preset-dump --preset hall` outputs valid INI text starting with `version=1`. `-o /tmp/test.spu94` writes to disk. `--name "My Hall"` sets `name=My Hall`. Missing `--preset` exits 2. Implemented in cmd_preset_dump.c (156 LOC), wired in main.c (extern + strcmp dispatch), added to CMakeLists.txt. |
| 2 | `spu94 reverb --load-preset <file.spu94>` reads a preset file and applies it before WAV processing begins | VERIFIED | `reverb --load-preset /tmp/test.spu94 sine_1khz.wav out.wav` exits 0 and produces 352KB WAV. `--load-preset + --preset` exits 2 with "mutually exclusive" message. `--load-preset + --dac` (flag layering) exits 0. Three-way mutual exclusion in cmd_reverb.c line 300-308. ROADMAP SC says `spu94 preset-load` but D-03 explicitly chose `reverb --load-preset` instead -- same intent, different surface. |
| 3 | JUCE Save button opens a native file dialog filtered to .spu94, writes the current state to the chosen path | VERIFIED | `showPresetNamePrompt()` (PluginEditor.cpp:408-455) creates FileChooser filtered to `*.spu94`, opens in saveMode. Callback calls `processorRef.savePresetToString(name, {})` which syncs all mixer/DAC atomics to SPU state before calling `spu94_preset_save`. Result written via `chosen.replaceWithText(text)`. Touch-created temp file cleaned up on cancel. Human-verified by user (save dialog simplified from two-step to one-step post-merge, per commit 4817816). |
| 4 | JUCE Load button opens a native file dialog, reads the chosen .spu94 file, and updates all GUI controls (registers, mixer faders, DAC toggles) to reflect the loaded state | VERIFIED | Load button callback (PluginEditor.cpp:50-88) opens FileChooser filtered to `*.spu94`, reads file via `loadFileAsString()`, calls `processorRef.loadPresetFromString(text)`. Processor queues pending preset buf with acquire/release atomics (PluginProcessor.cpp:281-292). Audio thread drains via `spu94_preset_load` (line 128), syncs registerBridge shadows (line 129), syncs all 11 mixer/DAC atomics (lines 134-144). Timer detects completion (PluginEditor.cpp:317-339), calls `registerPanel.updateFromShadows()` + `syncMixerKnobsFromProcessor()` (11 knobs/toggles). Custom dropdown entry with diamond prefix appears. Baseline captured for modified-state tracking. Human-verified by user. |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/cli/cmd_preset_dump.c` | preset-dump subcommand | VERIFIED | 156 LOC, contains `cmd_preset_dump`, calls `spu94_preset_save`, full getopt_long with --preset/--name/-o/--list-presets/--help |
| `src/cli/cmd_reverb.c` | --load-preset flag handling | VERIFIED | Contains `load_preset_path` variable, case 1012, fread+spu94_preset_load pipeline (lines 392-418), three-way mutual exclusion (lines 300-308), default fader guard changed to `loaded_pid > 0` |
| `src/cli/main.c` | preset-dump dispatch entry | VERIFIED | extern declaration (line 27), strcmp dispatch (line 63), help text (line 39) |
| `src/cli/CMakeLists.txt` | cmd_preset_dump.c in source list | VERIFIED | Line 13: `cmd_preset_dump.c` in add_executable |
| `src/standalone/PluginProcessor.h` | File preset load/save API | VERIFIED | `savePresetToString`, `loadPresetFromString`, `getFilePresetAppliedCount` public methods; `pendingPresetBuf`, `pendingPresetLen`, `filePresetReady`, `filePresetAppliedCount` private members |
| `src/standalone/PluginProcessor.cpp` | File read/save + spu94_preset_load/save | VERIFIED | `savePresetToString` (lines 241-279) syncs atomics then calls spu94_preset_save; `loadPresetFromString` (lines 281-292) queues to audio thread; processBlock drain (lines 125-145) applies + syncs 11 mixer/DAC atomics |
| `src/standalone/PluginEditor.h` | Save/Load buttons, modified tracking | VERIFIED | `savePresetButton`, `loadPresetButton`, `showPresetNamePrompt`, `customPresetName`, `kCustomPresetId`, `PresetSnapshot`, `captureBaseline`, `checkModified`, `updatePresetDisplayName`, `syncMixerKnobsFromProcessor` |
| `src/standalone/PluginEditor.cpp` | Button handlers, file dialogs, modified indicator | VERIFIED | Save handler (line 46 -> showPresetNamePrompt), Load handler (lines 50-88), custom dropdown entry (lines 324-338), modified asterisk (lines 501-524), syncMixerKnobsFromProcessor (lines 526-561), captureBaseline (lines 457-473), 30Hz timer integration |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| cmd_preset_dump.c | spu94_preset_save | calls C core save API | WIRED | Line 132: `spu94_preset_save(state, user_name, NULL, buf, sizeof buf)` |
| cmd_reverb.c | spu94_preset_load | fread + C core load API | WIRED | Line 410: `spu94_preset_load(state, preset_buf, nread)` |
| main.c | cmd_preset_dump.c | strcmp dispatch | WIRED | Line 27: extern, Line 63: dispatch |
| PluginEditor.cpp | PluginProcessor.cpp | calls loadPresetFromString/savePresetToString | WIRED | Line 68: `processorRef.loadPresetFromString(text)`, Line 448: `processorRef.savePresetToString(name, {})` |
| PluginProcessor.cpp | spu94_preset_save/load | C core API calls | WIRED | Line 128: `spu94_preset_load`, Line 272: `spu94_preset_save` |
| PluginEditor.cpp | registerPanel.updateFromShadows() | sync register sliders after file load | WIRED | Line 321: called in file-preset timer block |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| PluginProcessor.cpp | pendingPresetBuf | loadPresetFromString memcpy from file text | Yes -- fed to spu94_preset_load on audio thread | FLOWING |
| PluginProcessor.cpp | savePresetToString return | spu94_preset_save from live SPU state | Yes -- syncs atomics first, calls C core serializer | FLOWING |
| PluginEditor.cpp | syncMixerKnobsFromProcessor | 11 processor atomics populated by audio thread drain | Yes -- 33 references to real processor getters | FLOWING |
| PluginEditor.cpp | registerPanel.updateFromShadows | RegisterBridge shadows synced from SPU by audio thread | Yes -- registerBridge.syncShadowsFromSPU(spu) at line 129 | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| preset-dump outputs valid INI text | `spu94 preset-dump --preset hall` | Outputs `version=1` header + register/mixer/DAC sections | PASS |
| preset-dump writes to file | `spu94 preset-dump --preset hall -o /tmp/test.spu94` | File created with valid content | PASS |
| preset-dump --name sets metadata | `spu94 preset-dump --preset hall --name "My Hall" \| grep name=` | `name=My Hall` | PASS |
| preset-dump with no --preset exits 2 | `spu94 preset-dump` | Error: `--preset is required` exit 2 | PASS |
| reverb --load-preset processes audio | `spu94 reverb --load-preset file.spu94 sine.wav out.wav` | Exit 0, 352KB WAV produced | PASS |
| Three-way mutual exclusion | `spu94 reverb --load-preset f.spu94 --preset hall in.wav out.wav` | Exit 2: `mutually exclusive` | PASS |
| Flag layering works | `spu94 reverb --load-preset f.spu94 --dac in.wav out.wav` | Exit 0, WAV produced | PASS |
| CLI builds clean | `cmake --build build --target spu94_cli` | Exit 0, no warnings | PASS |
| JUCE standalone builds clean | `cmake --build build --target spu94_standalone_Standalone` | Exit 0, no warnings | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| PRE-06 | 14-01 | `spu94 preset-dump` subcommand writes current state to stdout or file | SATISFIED | cmd_preset_dump.c: full subcommand with --preset/--name/-o, dispatched from main.c |
| PRE-07 | 14-01 | `spu94 preset-load` subcommand reads .spu94 file and applies before processing | SATISFIED | Implemented as `reverb --load-preset` flag per D-03 decision; same functional intent. fread + spu94_preset_load + three-way mutual exclusion. |
| PRE-08 | 14-02 | Save button opens file dialog, writes .spu94 file | SATISFIED | showPresetNamePrompt -> FileChooser saveMode -> savePresetToString -> replaceWithText |
| PRE-09 | 14-02 | Load button opens file dialog, reads .spu94 file, updates all GUI controls | SATISFIED | Load button -> FileChooser openMode -> loadPresetFromString -> audio thread drain -> registerPanel.updateFromShadows + syncMixerKnobsFromProcessor (11 knobs/toggles) |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none found) | - | - | - | - |

No TODO/FIXME/PLACEHOLDER markers. No stub returns in phase-touched code. No console-log-only implementations. No empty handlers. All `return {}` instances are legitimate guard clauses for error conditions.

### Human Verification Required

The JUCE GUI was already human-verified by the user during development:
- Save/Load buttons visible and functional
- File dialogs filtered to .spu94
- Custom dropdown entry appears with diamond prefix after file load
- Modified-state asterisk works
- Post-merge fixes verified: save dialog simplified, volume jump resolved

No additional human verification items remain.

### Gaps Summary

No gaps found. All four success criteria are verified through codebase evidence and behavioral spot-checks. The CLI preset I/O and JUCE GUI preset I/O are fully implemented, wired, and producing real data through the complete pipeline.

**Note on SC2 wording:** The ROADMAP says `spu94 preset-load <file.spu94>` but the implementation uses `spu94 reverb --load-preset <file.spu94>`. This was an explicit design decision (D-03 in 14-CONTEXT.md: "No preset-load subcommand. Instead, reverb gains a --load-preset flag"). The functional intent -- reading a .spu94 file and applying it before WAV processing -- is fully satisfied.

---

_Verified: 2026-05-02T17:15:00Z_
_Verifier: Claude (gsd-verifier)_
