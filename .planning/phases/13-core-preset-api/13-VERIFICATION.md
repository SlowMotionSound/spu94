---
phase: 13-core-preset-api
verified: 2026-05-01T22:17:30Z
status: passed
score: 10/10
overrides_applied: 0
---

# Phase 13: Core Preset API Verification Report

**Phase Goal:** The C core can serialize all SPU state (35 registers + mixer faders + DAC toggles) to a versioned key=value text buffer and restore it with bit-identical fidelity
**Verified:** 2026-05-01T22:17:30Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | spu94_preset_save writes all register, mixer, and DAC state to a caller-provided buffer in human-readable key=value format | VERIFIED | src/spu94/spu94_preset_io.c lines 43-108: EMIT macro writes version, name, description, 35 registers (loop), 6 faders + latency_comp, 4 DAC toggles. Test test_save_register_count asserts exactly 35 registers. Tests prove all 46 fields present. |
| 2 | spu94_preset_load parses a key=value buffer and restores all state -- a save/load round-trip produces bit-identical register values | VERIFIED | src/spu94/spu94_preset_io.c lines 135-258: full section-aware parser with strcmp dispatch. Tests test_roundtrip_hall_registers, test_roundtrip_custom_state prove bit-identical comparison of all 46 fields. test_roundtrip_preserves_bytes_exact proves determinism. |
| 3 | The preset text includes a version header line (version=1) so future format additions won't break existing files | VERIFIED | Line 63: `EMIT("version=1\n")` is the first write. test_save_version_header asserts it's at position 0. Parser handles version in SECTION_NONE dispatch. test_parse_no_version_line_still_works proves forward compatibility. |
| 4 | A .spu94 file saved to disk is plain text, human-readable, and hand-editable with a text editor | VERIFIED | Format uses INI-style [section] headers, named keys (spu94_reg_name), comment lines (# SPU reverb registers...), 4-digit hex values. test_save_section_headers, test_save_section_comments, test_save_hex_format_4digit all confirm. Parser handles hand-edit scenarios (D-09 unknown keys, D-08 missing keys, malformed lines, blank lines, comments). |
| 5 | spu94_preset_save writes all 46 fields (35 registers + 7 mixer + 4 DAC) to a caller-provided buffer | VERIFIED | Register loop covers SPU94_REG__COUNT=35. Mixer section: input_gain, dry_fader, patina_fader, dry_send, patina_send, reverb_fader (6) + latency_comp (1) = 7. DAC: dac_enabled, dac_fir_enabled, dac_noise_enabled, dac_true_oversample = 4. Total 46. Tests verify each. |
| 6 | Output text has version=1 as the first non-blank line | VERIFIED | test_save_version_header: `strstr(preset_buf, "version=1\n") == preset_buf` |
| 7 | Output has [registers], [mixer], [dac] section headers with comment lines | VERIFIED | test_save_section_headers and test_save_section_comments both pass |
| 8 | All 16-bit values are 4-digit hex (0xNNNN), booleans are 0/1 | VERIFIED | Format specifiers `0x%04X` for regs/faders, `%d` for booleans. test_save_hex_format_4digit verifies exactly 4 hex digits + newline. |
| 9 | Missing keys retain engine's current value (D-08) and unknown keys silently ignored (D-09) | VERIFIED | test_parse_missing_keys_retain_defaults: loads partial preset, confirms untouched fields keep defaults. test_parse_unknown_keys_ignored: fake_key=0xDEAD returns SPU94_OK with no crash. |
| 10 | Round-trip is bit-identical for both factory and custom configurations (PRE-04) | VERIFIED | test_roundtrip_hall_registers/mixer/dac: compares all 46 fields between original and save-then-load copy. test_roundtrip_custom_state: non-default mixer/DAC values verified. test_roundtrip_preserves_bytes_exact: serialization determinism proven. |

**Score:** 10/10 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94.h` | spu94_preset_save and spu94_preset_load declarations, SPU94_PRESET_BUF_SIZE constant | VERIFIED | Lines 486-516: section banner, #define SPU94_PRESET_BUF_SIZE 4096u, both function signatures with documentation comments |
| `src/spu94/spu94_preset_io.c` | Save function implementation, parse_hex_u16 helper, full load parser | VERIFIED | 260 lines. Save (lines 43-108), parse_hex_u16 (lines 22-37), parse_bool (lines 125-133), load (lines 135-258). No stubs. |
| `tests/unit/preset/test_preset_roundtrip.c` | 20 tests: 15 save format + 5 round-trip fidelity | VERIFIED | 462 lines. main() calls RUN_TEST 20 times. All pass (ctest verified). |
| `tests/unit/preset/test_preset_parse.c` | 12 parser edge-case tests (D-08, D-09, tolerance) | VERIFIED | 195 lines. main() calls RUN_TEST 12 times. All pass (ctest verified). |
| `src/spu94/CMakeLists.txt` | spu94_preset_io.c in OBJECT library | VERIFIED | Line 19: spu94_preset_io.c listed in add_library(spu94_obj OBJECT ...) |
| `tests/unit/preset/CMakeLists.txt` | test_preset_roundtrip and test_preset_parse targets | VERIFIED | Lines 46-71: both targets with LABELS "preset" |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| src/spu94/spu94_preset_io.c | include/spu94/spu94.h | #include <spu94/spu94.h> | WIRED | Line 13 |
| src/spu94/spu94_preset_io.c | include/spu94/spu94_registers.h | #include <spu94/spu94_registers.h> | WIRED | Line 14; spu94_snapshot_registers at line 74; spu94_reg_name at lines 80, 205 |
| src/spu94/spu94_preset_io.c | spu94_set_* mixer/DAC setters | strcmp dispatch chains | WIRED | Lines 221-252: spu94_set_input_gain through spu94_set_dac_true_oversample |
| src/spu94/CMakeLists.txt | src/spu94/spu94_preset_io.c | add_library(spu94_obj OBJECT ...) | WIRED | Line 19 |
| tests/unit/preset/test_preset_roundtrip.c | spu94_preset_save + spu94_preset_load | round-trip pattern | WIRED | Save at 10+ call sites, load at 5 call sites in round-trip tests |
| tests/unit/preset/test_preset_parse.c | spu94_preset_load | direct calls with crafted input | WIRED | 9 distinct spu94_preset_load calls with various edge-case inputs |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Both test targets compile | cmake --build build --target test_preset_roundtrip test_preset_parse | [100%] Built target (both) | PASS |
| All 32 tests pass | ctest --test-dir build -R "test_preset_roundtrip\|test_preset_parse" | 100% tests passed, 0 failed out of 2 | PASS |
| All 6 preset-label tests pass (no regression) | ctest --test-dir build -L preset -j4 | 100% tests passed, 0 failed out of 6 | PASS |
| No grep-guard violations in spu94_preset_io.c | grep for float/double/malloc/calloc/realloc/free/long | Exit code 1 (no matches) | PASS |
| No heap symbols in libspu94 | ctest --test-dir build -R no_heap | rt_no_heap Passed | PASS |
| No internal header usage | grep spu94_state_internal spu94_preset_io.c | Exit code 1 (no match) | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-----------|-------------|--------|----------|
| PRE-01 | 13-01 | spu94_preset_save writes all register + mixer + DAC state to buffer in key=value format | SATISFIED | Implementation covers all 46 fields; test_save_register_count=35, test_save_mixer_fields_present (7), test_save_dac_fields_present (4) |
| PRE-02 | 13-02 | spu94_preset_load reads key=value buffer and restores all state | SATISFIED | Full parser with register/mixer/DAC dispatch; test_parse_register_restoration, test_parse_dac_toggles, round-trip tests |
| PRE-03 | 13-01, 13-02 | Version header so future additions don't break old files | SATISFIED | version=1 written first (test_save_version_header); parser handles missing version (test_parse_no_version_line_still_works); D-08/D-09 tolerance proven |
| PRE-04 | 13-02 | Round-trip fidelity -- save then load produces bit-identical state | SATISFIED | test_roundtrip_hall_registers/mixer/dac + test_roundtrip_custom_state compare all 46 fields; test_roundtrip_preserves_bytes_exact proves determinism |
| PRE-05 | 13-01 | .spu94 plain text, human-readable, hand-editable | SATISFIED | INI-style format with named keys, section headers, comment lines, 4-digit hex. Parser tolerates hand-edits (unknown keys, malformed lines, comments). |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | - | - | No anti-patterns found in phase 13 files |

### Human Verification Required

None required. All truths are programmatically verifiable through the test suite and code inspection.

### Gaps Summary

No gaps found. All 10 observable truths verified, all 6 artifacts pass all levels (exists, substantive, wired), all key links wired, all 5 requirements satisfied, all 32 tests pass, no anti-patterns, no grep-guard violations, no heap symbols introduced.

---

_Verified: 2026-05-01T22:17:30Z_
_Verifier: Claude (gsd-verifier)_
