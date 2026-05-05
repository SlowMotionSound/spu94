---
phase: 16-core-tempo-api
verified: 2026-05-03T17:15:00Z
status: passed
score: 4/4 must-haves verified
overrides_applied: 0
---

# Phase 16: Core Tempo API Verification Report

**Phase Goal:** The C core stores BPM state, knows all musical subdivisions, and can snap any of the 10 delay registers to the nearest subdivision sample count at 22,050 Hz
**Verified:** 2026-05-03T17:15:00Z
**Status:** passed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Calling spu94_set_tempo stores a BPM value retrievable via spu94_get_tempo | VERIFIED | spu94_tempo.c lines 135-213: set stores to state->tempo_bpm, get reads it. 9 tests in test_tempo_basic.c exercise roundtrip at BPM 1/120/65535, null/zero guards. ctest: 9/9 pass. |
| 2 | Calling spu94_set_subdivision on any delay register computes the correct sample count and writes the snapped value to that register | VERIFIED | spu94_tempo.c lines 240-294: computes (60*22050*num)/(bpm*den), writes via spu94_set_reg_u16 for d-prefix (6 regs) and mCOMB offset for virtual combs (4 regs). test_auto_resnap_on_bpm_change verifies actual register values (2756 at 120BPM, 5512 at 60BPM). test_tempo_comb verifies mCOMB offsets against real Hall preset geometry. |
| 3 | All 15 subdivision variants (1/1, 1/2, 1/4, 1/8, 1/16, plus dotted and triplet for each) are supported and produce mathematically correct sample counts | VERIFIED | Subdivision table at spu94_tempo.c lines 42-58 with 15 entries. test_all_subs_valid_at_120 and test_all_subs_valid_at_300 iterate all 15. Independent python computation confirms all ratios are mathematically correct. test_comb_all_subs_at_120 exercises all 15 against real buffer geometry. |
| 4 | The API follows existing C core discipline: no heap, no floats in stored state, rt_safety clean | VERIFIED | Zero matches for float/double/malloc/calloc/realloc/free in spu94_tempo.c. All computation is integer-only (uint32 intermediates). Subdivision table is static const in .rodata. No stdio/stdlib includes. |

**Score:** 4/4 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94.h` | Tempo API declarations (types, enums, function prototypes) | VERIFIED | Lines 527-603: 3 enums (spu94_subdivision_t with 15+sentinel, spu94_tempo_reg_t with 10+sentinel, spu94_binding_state_t with 3 values), 10 function prototypes |
| `src/spu94/spu94_tempo.c` | Subdivision table, set_tempo, get_tempo, subdivision_valid, set_subdivision, binding state, write-interception | VERIFIED | 375 lines. Subdivision table (15 entries), compute_delay_samples, set_tempo with auto-resnap, get_tempo, subdivision_valid, set_subdivision with register writes, binding state functions, sync toggles, write-interception hook. |
| `src/spu94/spu94_state_internal.h` | Tempo fields in spu94_state struct | VERIFIED | Lines 207-214: tempo_bpm (uint16), reflection_sync (uint8), comb_sync (uint8), tempo_bind_state[10], tempo_bind_sub[10], tempo_bind_ref_bpm[10]. 44 bytes total. |
| `src/spu94/spu94_register_io.c` | Write-interception hook for binding state transition | VERIFIED | Line 91: extern declaration of spu94_tempo_on_reg_write. Line 152: hook call after register write completion. |
| `tests/unit/tempo/test_tempo_basic.c` | TEMPO-01 unit tests | VERIFIED | 106 lines, 9 RUN_TEST calls: set/get roundtrip at 120/1/65535, zero rejection, null guards, sync toggles, initial state. All pass. |
| `tests/unit/tempo/test_tempo_snap.c` | TEMPO-03/04 unit tests | VERIFIED | 144 lines, 11 RUN_TEST calls: subdivision validity at 120/300, overflow detection, BPM=0, bad subdivision, 4 known-vector formula tests, binding state, no-BPM error. All pass. |
| `tests/unit/tempo/test_tempo_binding.c` | Binding state transition tests (D-04 through D-07) | VERIFIED | 190 lines, 10 RUN_TEST calls: auto-resnap on BPM change (with numeric register value verification), sync group gating, initial FIXED state, GRID binding, manual write to PROPORTIONAL, FIXED stays FIXED, re-bind paths, explicit FIXED, overflow to FIXED. All pass. |
| `tests/unit/tempo/test_tempo_comb.c` | Virtual comb-delay computation tests | VERIFIED | 215 lines, 8 RUN_TEST calls: dCOMB1-4 offset correctness against Hall preset geometry, geometry overflow rejection, comb sync toggle (off=no resnap, on=resnap), all 15 subs enumerated (10 valid, 5 rejected at 120BPM with Hall). All pass. |
| `tests/unit/tempo/CMakeLists.txt` | Build config for 4 test executables | VERIFIED | 16 lines: all 4 test binaries registered with add_test |
| `src/spu94/CMakeLists.txt` | spu94_tempo.c registered | VERIFIED | Line 26: spu94_tempo.c in spu94_obj OBJECT library |
| `tests/unit/CMakeLists.txt` | tempo subdirectory registered | VERIFIED | Line 25: add_subdirectory(tempo) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| src/spu94/spu94_tempo.c | src/spu94/spu94_state_internal.h | includes and accesses state->tempo_bpm | WIRED | Line 21: #include "spu94_state_internal.h". Lines 141, 142, 150, etc. access state->tempo_bpm and other tempo fields. |
| src/spu94/spu94_tempo.c | src/spu94/spu94_register_io.c | calls spu94_set_reg_u16 to write snapped values | WIRED | Lines 174, 197-198, 262, 286-287: (void)spu94_set_reg_u16(...) calls with computed sample values. |
| src/spu94/spu94_register_io.c | src/spu94/spu94_tempo.c | calls spu94_tempo_on_reg_write for binding state transition | WIRED | Line 91: extern declaration. Line 152: spu94_tempo_on_reg_write(state, reg) called after register write. |
| tests/unit/tempo/test_tempo_basic.c | include/spu94/spu94.h | calls spu94_set_tempo/spu94_get_tempo | WIRED | Lines 44, 45: spu94_set_tempo(s, 120), spu94_get_tempo(s). Linked via spu94_static. |
| tests/unit/tempo/test_tempo_comb.c | include/spu94/spu94.h | calls spu94_set_subdivision with dCOMB registers | WIRED | Lines 51-52, 73, 92, 113: spu94_set_subdivision(s, SPU94_TEMPO_REG_dCOMB*, ...) |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 4 tempo test binaries pass | ctest -R test_tempo --output-on-failure | 4/4 passed, 0 failed | PASS |
| No regressions in C unit tests | ctest -R "test_tempo\|test_register\|test_reverb\|test_process\|test_dac" | 18/18 passed, 0 failed | PASS |
| Project builds cleanly | cmake --build . | No errors from spu94_tempo.c | PASS |
| No floats or heap in tempo.c | grep -E "float\|double\|malloc\|calloc" spu94_tempo.c | Zero matches | PASS |
| 22050 Hz rate used exclusively | grep "22050" spu94_tempo.c | Line 126: 60u * 22050u * (uint32_t)numerator | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| TEMPO-01 | 16-01 | spu94_set_tempo stores BPM in engine state | SATISFIED | set_tempo/get_tempo implemented and tested (9 tests in test_tempo_basic) |
| TEMPO-02 | 16-02, 16-03 | spu94_set_subdivision snaps delay register to subdivision at current BPM | SATISFIED | Full register writes via spu94_set_reg_u16, virtual comb offset computation, binding state machine. 10 binding tests + 8 comb tests. |
| TEMPO-03 | 16-01, 16-03 | 15 subdivisions: 1/1 through 1/16 with dotted and triplet variants | SATISFIED | Compile-time table with all 15 entries. Validity tested at BPM 120 and 300 (all 15 valid). Comb tests enumerate all 15 against real geometry. |
| TEMPO-04 | 16-01, 16-02 | Conversion formula uses 22,050 Hz sample rate | SATISFIED | Formula: (60*22050*num)/(bpm*den). Known-vector tests verify 120BPM/1/4=2756, 60BPM/1/4=5512. Integer truncation matches PS1 MIPS R3000A behavior. |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| (none) | - | - | - | Zero TODO/FIXME/placeholder markers. Zero stub functions. Zero console.log patterns. All functions have substantive implementations. |

### Human Verification Required

None. All behaviors are testable programmatically and verified by the unit test suite.

### Gaps Summary

No gaps found. All 4 roadmap success criteria are verified against the actual codebase with multiple levels of evidence: source code inspection, wiring verification, numerical correctness checks, and passing test suite.

---

_Verified: 2026-05-03T17:15:00Z_
_Verifier: Claude (gsd-verifier)_
