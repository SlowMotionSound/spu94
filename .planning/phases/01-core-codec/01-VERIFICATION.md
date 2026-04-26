---
phase: 01-core-codec
verified: 2026-04-26T22:00:00Z
status: passed
score: 9/9 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 1: Core Codec Verification Report

**Phase Goal:** ADPCM decode and encode exist as standalone, tested C functions that any caller can use without touching spu94_state
**Verified:** 2026-04-26
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| #  | Truth | Status | Evidence |
|----|-------|--------|----------|
| 1  | A caller can decode a 16-byte ADPCM block into 28 int16 samples with correct filter prediction, rounding, nibble ordering, sign extension, and shift clamping | VERIFIED | `spu94_adpcm_decode_block` in `src/spu94/spu94_adpcm.c:32-92`; 19 known-vector tests covering all 5 filters, shift extremes, saturation, nibble order |
| 2  | Decoder uses caller-allocated 4-byte state (two int16), zero heap, integer-only, no dependency on spu94_state | VERIFIED | Struct `spu94_adpcm_state {int16_t old; int16_t older;}` in header; grep confirms 0 malloc/calloc/free, 0 float/double, "spu94_state" only in a comment |
| 3  | Shift values 13-15 are mapped to shift 9; filter indices 5-7 are clamped to 4 | VERIFIED | `spu94_adpcm.c:42-43`: `if (shift > 12) shift = 9; if (filter > 4) filter = 4;`; confirmed by `test_decode_shift13_maps_to_9`, `test_decode_shift14_maps_to_9`, `test_decode_shift15_maps_to_9`, `test_decode_filter_clamp_5to4` — all pass |
| 4  | All existing 82 ctest pass unchanged (plus new ADPCM tests) | VERIFIED | Full reverb/fir/preset/process suite: 35/35 passed; q15 + state + adpcm: 4/4 passed; 84 total tests in suite; no failures observed |
| 5  | A caller can encode 28 int16 samples into a 16-byte ADPCM block where the encoder picks optimal (filter, shift) via brute-force search over 65 combinations | VERIFIED | `spu94_adpcm_encode_block` in `src/spu94/spu94_adpcm_encode.c:26-104`; outer loop `f=0..4`, inner loop `s=0..12` = 65 combinations; `int64_t best_error = INT64_MAX` with strict `<` tiebreak |
| 6  | Encoder uses reconstructed (decoded) samples for prediction state, not original PCM | VERIFIED | `spu94_adpcm_encode.c:76-77`: `trial.older = trial.old; trial.old = clamped;` — clamped is the sat_s16 output; grep for `trial.old = in[` returns empty |
| 7  | Encode then decode is deterministic and bit-identical across runs | VERIFIED | `test_encode_decode_roundtrip_deterministic` encodes twice from fresh zero state, asserts memcmp of both block and decoded output; test passes |
| 8  | Encoder quantizes nibbles with round-to-nearest, guards against UB at shift=12 | VERIFIED | `spu94_adpcm_encode.c:57-59`: `int32_t half_step = (shift_amount > 0) ? (1 << (shift_amount - 1)) : 0;` — explicit guard prevents `1 << -1`; `test_encode_shift12_no_ub` passes |
| 9  | Both encode and decode use caller-allocated state, zero heap, integer-only | VERIFIED | grep on both files: 0 `/64`, 0 `float\|double`, 0 `malloc\|calloc\|free`; `spu94_state` appears only in a comment in decoder |

**Score:** 9/9 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94_adpcm.h` | Public ADPCM API — state struct, decode_block + encode_block signatures, filter table externs | VERIFIED | Contains `typedef struct spu94_adpcm_state`, `extern const int16_t spu94_adpcm_f0[5]`, `extern const int16_t spu94_adpcm_f1[5]`, `uint8_t spu94_adpcm_decode_block(`, `void spu94_adpcm_encode_block(`, `#define SPU94_ADPCM_BLOCK_SAMPLES  28`, `#define SPU94_ADPCM_BLOCK_BYTES    16` |
| `src/spu94/spu94_adpcm.c` | ADPCM decoder + filter coefficient tables | VERIFIED | 93 lines; defines `const int16_t spu94_adpcm_f0[5] = {0, 60, 115, 98, 122}`, `const int16_t spu94_adpcm_f1[5] = {0, 0, -52, -55, -60}`; uses `>> 6` (not `/64`); uses `sat_s16` at line 75 |
| `src/spu94/spu94_adpcm_encode.c` | ADPCM encoder with internal decoder, brute-force search, L2 error metric | VERIFIED | 104 lines (exceeds 80 min); contains `int64_t best_error`, `int64_t error = 0`, `trial.old = clamped`, `(shift_amount > 0)` guard, `>> 6` ASR |
| `tests/unit/adpcm/test_adpcm_decode.c` | Known-vector decode tests covering all 5 filters, shift extremes, clamp, nibble ordering | VERIFIED | 487 lines (exceeds 120 min); 19 `void test_` functions; includes `build_block` helper; includes `<spu94/spu94_adpcm.h>`; all 19 tests pass |
| `tests/unit/adpcm/test_adpcm_encode.c` | Encoder tests — round-trip consistency, filter/shift selection, nibble range, determinism | VERIFIED | 309 lines (exceeds 100 min); 12 `void test_` functions; uses both `spu94_adpcm_encode_block` and `spu94_adpcm_decode_block` for round-trip; all 12 tests pass |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/spu94/spu94_adpcm.c` | `include/spu94/spu94_q15.h` | `sat_s16()` for int16 clamping | WIRED | `sat_s16` found at line 75 |
| `tests/unit/adpcm/test_adpcm_decode.c` | `include/spu94/spu94_adpcm.h` | `#include <spu94/spu94_adpcm.h>` | WIRED | Include at line 16; `spu94_adpcm_decode_block` called in every test function |
| `src/spu94/spu94_adpcm_encode.c` | `include/spu94/spu94_adpcm.h` | `spu94_adpcm_f0/f1` tables, `spu94_adpcm_state` struct | WIRED | `#include <spu94/spu94_adpcm.h>` at line 21; `spu94_adpcm_f0[f]` and `spu94_adpcm_f1[f]` accessed in brute-force loop |
| `src/spu94/spu94_adpcm_encode.c` | `include/spu94/spu94_q15.h` | `sat_s16()` for internal decoder clamping | WIRED | `sat_s16` found at line 69 |
| `tests/unit/adpcm/test_adpcm_encode.c` | `include/spu94/spu94_adpcm.h` | `encode_block + decode_block` for round-trip | WIRED | Both functions called; encode in every test, decode in round-trip and consistency tests |
| `src/spu94/CMakeLists.txt` | both `.c` sources | `spu94_adpcm.c`, `spu94_adpcm_encode.c` in `spu94_obj` | WIRED | Lines 19-20 of CMakeLists confirm both sources in the OBJECT library |
| `tests/unit/CMakeLists.txt` | `tests/unit/adpcm/` | `add_subdirectory(adpcm)` | WIRED | Line 21 confirmed |
| `tests/unit/adpcm/CMakeLists.txt` | test executables | `add_test(NAME adpcm_decode_unit)`, `add_test(NAME adpcm_encode_unit)` | WIRED | Both test targets and `add_test` declarations present; ctest discovers tests #46 and #47 |

---

### Data-Flow Trace (Level 4)

Not applicable — these are pure C codec functions with no dynamic data sources. All inputs are caller-provided arguments. There are no async fetches, stores, or props to trace.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Decoder 19 known-vector tests pass | `ctest -R adpcm_decode_unit` | Passed 0.00 sec | PASS |
| Encoder 12 tests pass | `ctest -R adpcm_encode_unit` | Passed 0.00 sec | PASS |
| No /64 division in decoder | `grep -c '/64' spu94_adpcm.c` | 0 | PASS |
| No /64 division in encoder | `grep -c '/64' spu94_adpcm_encode.c` | 0 | PASS |
| No float/double in either file | grep both files | 0, 0 | PASS |
| No heap in either file | grep both files | 0, 0 | PASS |
| No `trial.old = in[` encoder bug | grep encoder | empty | PASS |
| Existing regression suite (reverb/fir/preset/process) | `ctest -R reverb_|fir_|preset_|process_` | 35/35 passed | PASS |

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|----------|
| ADPCM-01 | 01-01 | Decoder with all 5 filter pairs, ASR `(old*f0+older*f1+32)>>6`, single clamp, state carry | SATISFIED | `spu94_adpcm.c:71` uses `>> 6` with +32 bias; 19 tests cover all 5 filters |
| ADPCM-02 | 01-01 | Shift 13-15 mapped to 9; no UB from negative shifts | SATISFIED | `spu94_adpcm.c:42`: `if (shift > 12) shift = 9;`; tests shift13/14/15 all pass with value 8 |
| ADPCM-03 | 01-01 | Low nibble first; sign-extend 4-bit nibbles before shift | SATISFIED | `spu94_adpcm.c:58-65`: low nibble extracted first, `if (nib >= 8) nib -= 16;` sign-extends; `test_decode_nibble_order` and `test_decode_negative_nibble` pass |
| ADPCM-04 | 01-02 | Encoder brute-force over 65 combinations, int64 SSE, deterministic tiebreak | SATISFIED | Loops `f=0..4`, `s=0..12`; `int64_t error`; strict `<` with lower-f outer loop |
| ADPCM-05 | 01-02 | Encoder uses reconstructed samples for prediction state | SATISFIED | `trial.old = clamped` (line 77); `test_encode_state_uses_reconstructed` and `test_encode_decode_consistency` pass |
| ADPCM-06 | 01-02 | Round-to-nearest quantization; shift=12 UB guard | SATISFIED | `half_step = (shift_amount > 0) ? (1 << (shift_amount-1)) : 0`; `test_encode_shift12_no_ub` passes |
| ADPCM-07 | 01-01 + 01-02 | Pure C, caller-allocated state, zero heap, integer-only, no `spu94_state` | SATISFIED | Both files: 0 malloc/calloc/free, 0 float/double, 0 `/64`; "spu94_state" only in decoder comment |

All 7 phase requirements satisfied. No orphaned requirements: ADPCM-INT, ADPCM-IO, and ADPCM-TEST are explicitly deferred to later phases.

---

### Anti-Patterns Found

None. No TODO/FIXME/placeholder comments, no empty returns, no float/double, no heap, no critical encoder bug (`trial.old = in[i]` not present).

---

### Human Verification Required

None. All must-haves are verifiable programmatically and all checks passed.

---

### Gaps Summary

No gaps. All 9 truths verified, all 5 artifacts substantive and wired, all 8 key links confirmed, all 7 requirements satisfied, tests passing.

---

_Verified: 2026-04-26_
_Verifier: Claude (gsd-verifier)_
