---
phase: 24-state-automation-surface
plan: 01
subsystem: plugin-state
tags: [state-persistence, binary-container, daw-save-load, locale-safe]
dependency_graph:
  requires: [spu94_preset_save, spu94_preset_load, pendingPresetBuf-mechanism]
  provides: [getStateInformation, setStateInformation, StateSerializer-namespace]
  affects: [PluginProcessor.cpp, PluginProcessor.h]
tech_stack:
  added: []
  patterns: [binary-envelope-over-text, IEEE-754-float-appendix, deferred-apply-handoff]
key_files:
  created:
    - src/plugin/StateSerializer.h
    - tests/plugin/test_state_serializer.cpp
  modified:
    - src/plugin/PluginProcessor.h
    - src/plugin/PluginProcessor.cpp
    - tests/plugin/CMakeLists.txt
decisions:
  - "Container format: SPU9 magic + version 1 + uint32 LE bodyLen + text body + 6-float appendix"
  - "Float appendix captures inputGain, morphPosition, morphSpeed, morphGrit (wrapper-side atomics not in .spu94 text) + 2 reserved padding floats"
  - "Future-version rejection: version byte > 1 fails gracefully, engine stays at defaults (D-06)"
  - "textLen validated against SPU94_PRESET_BUF_SIZE in load to prevent pendingPresetBuf overflow (T-24-02)"
metrics:
  duration: "20m 30s"
  completed: "2026-05-12T15:09:48Z"
  tasks_completed: 2
  tasks_total: 2
  files_created: 2
  files_modified: 3
---

# Phase 24 Plan 01: State Persistence (Binary Container) Summary

Binary state container wrapping .spu94 text + IEEE 754 float appendix for DAW project save/load via getStateInformation/setStateInformation.

## Tasks Completed

| # | Task | Commit | Key Changes |
|---|------|--------|-------------|
| 1 | Create StateSerializer.h + fill get/setStateInformation | 52aadb6 | StateSerializer.h (save/load), PluginProcessor.cpp stubs filled, PluginProcessor.h include added |
| 2 | State round-trip unit tests | 891c0b7 | 7 tests covering save/roundtrip/rejection, fix ensureSize bug, CMakeLists.txt registration |

## Implementation Details

### StateSerializer.h (header-only namespace)

**save()**: Calls `spu94_preset_save` to get the .spu94 text body, then writes a 9-byte binary header (4-byte `SPU9` magic + 1-byte version + 4-byte LE body length) followed by the text body and a 24-byte float appendix (6 IEEE 754 floats: inputGain, morphPosition, morphSpeed, morphGrit, pad0, pad1).

**load()**: Validates magic bytes, rejects version > 1, bounds-checks bodyLen against sizeInBytes, ensures bodyLen >= 24 (float appendix minimum), validates textLen against SPU94_PRESET_BUF_SIZE. Returns a LoadResult struct with pointers into the original data (zero-copy for the text body) and extracted float values.

### getStateInformation (PluginProcessor.cpp)

Syncs wrapper-side atomics to the engine (matching savePresetToString's existing sync pattern), then calls `StateSerializer::save` with the engine state and 4 wrapper-side float values. The Input Gain engine register is pinned at 0x7FFF (Phase 23 D-03); the actual gain value is captured in the float appendix.

### setStateInformation (PluginProcessor.cpp)

Calls `StateSerializer::load`, on failure returns immediately (engine stays at defaults per D-06). On success: copies text body into `pendingPresetBuf` via memcpy (no allocation), sets `filePresetReady = true` to trigger the existing deferred-apply mechanism on the next processBlock, and stores the 4 float appendix values into the corresponding wrapper-side atomics (inputLevel, morphPosition, morphSpeed, morphGrit). All atomic stores use `memory_order_relaxed`.

### Locale Safety (PLUG-25)

Zero locale-sensitive parsing in the entire state chain:
- .spu94 text format uses `snprintf %04X` hex formatting and hand-rolled `parse_hex_u16` (no float text parsing)
- Float appendix uses raw IEEE 754 binary bytes via `memcpy` (not text serialization)
- No `juce::String::getFloatValue`, `std::stof`, or locale-dependent parsing anywhere

### Thread Safety

- `getStateInformation` runs on the message thread (JUCE contract); reads atomics, calls spu94_preset_save which only reads engine state
- `setStateInformation` runs on the message thread; writes into pre-allocated pendingPresetBuf array + stores to atomics; the audio thread picks up via the existing `filePresetReady` flag on the next processBlock
- Zero allocation on the audio thread (PLUG-23)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] MemoryBlock::ensureSize before append**
- **Found during:** Task 2 (unit testing)
- **Issue:** `dest.ensureSize(N)` set the MemoryBlock's internal size to N, causing subsequent `dest.append()` calls to write at offset N instead of 0. All container bytes were offset by the pre-sized amount.
- **Fix:** Removed the `ensureSize` call. `MemoryBlock::append()` handles growing internally; `reset()` + successive `append()` is the correct pattern.
- **Files modified:** src/plugin/StateSerializer.h
- **Commit:** 891c0b7

**2. [Rule 1 - Bug] Plan text claims .spu94 body starts with "[SPU94 Preset]"**
- **Found during:** Task 2 (test authoring)
- **Issue:** Plan acceptance criteria says text body starts with `"[SPU94 Preset]"`, but the actual spu94_preset_io.c serializer starts with `"version=1\n"`.
- **Fix:** Test checks for `"version=1\n"` (matching actual C core behavior).
- **Files modified:** tests/plugin/test_state_serializer.cpp
- **Commit:** 891c0b7

## Decisions Made

1. **Container uses reset() + append() pattern** (not ensureSize + memcpy) for MemoryBlock construction -- simpler and avoids the offset bug.
2. **textLen validated against SPU94_PRESET_BUF_SIZE** in load() to prevent overflow when copying into the fixed-size pendingPresetBuf array (T-24-02 mitigation).
3. **Float appendix read via memcpy** (not reinterpret_cast dereference) for alignment safety on all platforms.

## Test Results

7/7 tests pass via ctest:

| Test | Description | Status |
|------|-------------|--------|
| test_save_produces_valid_container | Magic bytes, version byte, bodyLen consistency | PASS |
| test_load_roundtrip_identical | Exact float equality on 4 appendix values | PASS |
| test_load_rejects_future_version | Version byte 2 returns ok=false | PASS |
| test_load_rejects_short_data | sizeInBytes < 9 returns ok=false | PASS |
| test_load_rejects_bad_magic | 'XXXX' magic returns ok=false | PASS |
| test_load_rejects_truncated_body | One byte short returns ok=false | PASS |
| test_text_body_is_spu94_format | Text body starts with "version=1\n" | PASS |

## Verification

- [x] VST3 target compiles clean with StateSerializer.h included
- [x] All 7 state serializer tests pass
- [x] No locale-sensitive parsing in the state chain (grep-verified)
- [x] No allocation in setStateInformation beyond memcpy into pre-allocated buffer

## Self-Check: PASSED

All 5 created/modified files exist on disk. Both task commits (52aadb6, 891c0b7) verified in git log.
