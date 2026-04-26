---
phase: 03-core-reverb-algorithm-hard-clip
verified: 2026-04-20T00:00:00Z
status: passed
score: 5/5 success criteria verified
overrides_applied: 0
---

# Phase 3: Core Reverb Algorithm + Hard Clip Verification Report

**Phase Goal:** The per-22.05 kHz-tick reverb algorithm — SAME/DIFF IIR, 4-tap comb,
APF1, APF2 — plus the mix-bus hard clip, run correctly against every documented spec
behavior at the algorithmic level.
**Verified:** 2026-04-20
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|---------|
| 1 | Driving each isolated stage with crafted inputs produces outputs matching hand-derived nocash-pseudocode reference values bit-for-bit (SC-1) | VERIFIED | `ctest -R reverb_` passes all per-stage tests; `test_reverb_body` passes SC-1 composition equivalence at 3 seeds; Python `derive_reverb_reference.py` is the independent derivation chain |
| 2 | Mix-bus input driven past ±0x7FFF saturates to the hard-clip range independently of the reverb network (SC-2, CORE-02) | VERIFIED | `spu94_reverb_hard_clip` is its own named function with dedicated `ctest` target `reverb_hard_clip`; test table covers INT32_MIN/MAX, ±0x10000, boundary in-range, and null-overflow_out cases |
| 3 | vIIR = -0x8000 causes the final reverb result to be negated; vIIR = INT16_MIN+1 does NOT negate (SC-3, CORE-08, TEST-06) | VERIFIED | `grep -c "vIIR_snap == INT16_MIN" src/spu94/spu94_reverb.c` = 4 (L+R per SAME+DIFF IIR); `test_same_iir_anomaly_vIIR_INT16_MIN_negates` + `test_same_iir_control_vIIR_INT16_MIN_plus_1_no_negate` both present and green |
| 4 | Fixed-point saturation, truncation, and signed-overflow edge cases are exercised by dedicated tests, all passing (SC-4, TEST-07) | VERIFIED | `tests/unit/reverb/test_reverb_edges.c` has 12 sub-tests covering INT16_MIN², ADR-0001 ASR truncation direction, D-07 cascading-sat mirror case, D-10 anomaly+Pitfall-1 compound, D-11 err invariants, Pitfall-8 buffer_address invariance; `ctest -R reverb_edges` green |
| 5 | DECISIONS.md contains ADR for comb-sum intermediate accumulation precision (SC-5a) and for register-write timing between L/R tick (SC-5b) | VERIFIED | `grep -cE "^## ADR-000[789]|^## ADR-001[01]" docs/DECISIONS.md` = 5; ADR-0007 (comb-sum cascading sat) and ADR-0008 (L/R freeze-once-per-pair) both present with revert levers and M4/M5 revision triggers |

**Score: 5/5 truths verified**

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/spu94/spu94_reverb_internal.h` | Internal declarations for 7 stage functions + reverb body | VERIFIED | Present at `src/spu94/`, not at `include/spu94/`; 9 function declarations (body + 8 stages); header guard `SPU94_REVERB_INTERNAL_H` |
| `src/spu94/spu94_reverb.c` | Stage function bodies + reverb body caller | VERIFIED | 605 lines; all 8 stages + body fully implemented; no stubs |
| `tests/unit/reverb/test_reverb_hard_clip.c` | CORE-02 independently testable saturation on the mix bus | VERIFIED | Contains `TEST_ASSERT_EQUAL_INT16`, null-overflow_out test, INT32 extreme boundaries |
| `tests/python/derive_reverb_reference.py` | Python reference implementation, no GPL provenance | VERIFIED | Has `ref_input_scale`, `ref_hard_clip`, `ref_output_scale`, `ref_same_iir`, `ref_diff_iir`, `ref_comb`, `ref_apf1`, `ref_apf2`; mentions of DuckStation/Mednafen are in docstring commentary context only (not in derivation chain); C test files are GPL-free |
| `tests/unit/reverb/test_reverb_same_iir.c` | CORE-05 same-side IIR + CORE-08 anomaly + TEST-06 control | VERIFIED | `test_same_iir_anomaly_vIIR_INT16_MIN_negates`, `test_same_iir_control_vIIR_INT16_MIN_plus_1_no_negate`, second control, Pitfall-1 edge, err invariants all present |
| `tests/unit/reverb/test_reverb_diff_iir.c` | CORE-05 diff-side IIR + anomaly + control | VERIFIED | Mirrors same_iir structure; cross-side register pairing exercised |
| `tests/unit/reverb/test_reverb_comb.c` | 4-tap comb bit-exactness + D-07 cascading-sat characterization | VERIFIED | `test_comb_cascading_distinguishes_int32_accumulate` present; result `-0x7FFD` pins Variant B vs 0 for Variant A |
| `tests/unit/reverb/test_reverb_apf1.c` | APF1 bit-exactness; Pitfall-7 feedback loop edges | VERIFIED | "Pitfall 7" documented in test file; all 5 sub-tests green |
| `tests/unit/reverb/test_reverb_apf2.c` | APF2 bit-exactness; Pitfall-7 edges | VERIFIED | Mirror of APF1; green |
| `tests/unit/reverb/test_reverb_edges.c` | TEST-07 fixed-point saturation + truncation + overflow battery | VERIFIED | 12 `RUN_TEST` sub-tests; covers every stage at INT16_MIN² |
| `tests/unit/reverb/test_reverb_body.c` | SC-1 full-tick equivalence + Pitfall-8 buffer_address invariance | VERIFIED | 3 seeded equivalence tests + standalone Pitfall-8 test; `TEST_ASSERT_EQUAL_MEMORY(gA_work, gB_work, sizeof(gA_work))` present |
| `tests/python/fuzz_reverb.py` | 10^6-step random-input fuzz harness | VERIFIED | `N_STEPS` default `"1000000"`; 2.6s runtime confirmed; structural invariants checked (ba bounds, alignment) |
| `docs/DECISIONS.md` | ADR-0007..ADR-0011 (5 new entries above ADR-0006) | VERIFIED | `grep -cE "^## ADR-000[789]|^## ADR-001[01]"` = 5; ADR-0006 preserved; cascading/revert lever/Extended Modulation Mode/overflow-magnitude all present |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `src/spu94/spu94_tick.c` | `spu94_reverb_body` | third statement between `apply_pending_writes` and exit | WIRED | `grep -c "spu94_reverb_body(state);" src/spu94/spu94_tick.c` = 1 (single call site, Pitfall 4 preserved) |
| `tests/unit/reverb/*.c` | `src/spu94/spu94_reverb_internal.h` | `#include "../../../src/spu94/spu94_reverb_internal.h"` | WIRED | All test TUs include the internal header via relative path |
| `spu94_reverb.c::spu94_reverb_same_iir` | Phase 2 buffer arithmetic | `reverb_buf_read/write` + `(buffer_address + halfword_offset*2) & 0x7FFFE` | WIRED | Static inline helpers present; pattern `0x7FFFE` confirmed in source |
| `spu94_reverb_body` | all 7 stage functions + hard_clip | sequential call order matching nocash E1 | WIRED | Call order confirmed: input_scale → hard_clip → same_iir → diff_iir → comb → apf1 → apf2 → output_scale |
| `tests/python/fuzz_reverb.py` | `build/src/spu94/libspu94.so` | `SPU94_LIB` env var + CMake `$<TARGET_FILE:spu94_shared>` | WIRED | `SPU94_LIB` present in fuzz harness; `$<TARGET_FILE:spu94_shared>` in `tests/python/CMakeLists.txt` |
| `docs/DECISIONS.md` | ROADMAP Phase 3 SC-5 | ADR-0007 (comb-sum precision) + ADR-0008 (L/R write timing) | WIRED | Both SC-5a and SC-5b ADRs confirmed present |

---

### Decision Compliance (D-01..D-11 Audit)

| Decision | Requirement | Status | Evidence |
|----------|-------------|--------|---------|
| D-01 | Internal header never on public include path | HONORED | `ls include/spu94/spu94_reverb_internal.h` → NOT FOUND |
| D-02 | One function per documented stage; L+R handled internally | HONORED | 7 stage functions + 1 body = 8 declarations in header |
| D-05/D-06 | Single TU `spu94_reverb.c`; body called from `spu94_tick` as third statement | HONORED | Confirmed in `spu94_tick.c` |
| D-07 | Comb-sum: cascading `sat_s16` after each add (NOT int32 accumulate) | HONORED | `grep -cE "int32_t\\s+sum[LR]?\\s*=" src/spu94/spu94_reverb.c` = 0; distinguishing test pins the behavior |
| D-08 | v* registers frozen as snapshots at start of `spu94_reverb_body`; all stages receive values as parameters | HONORED | All `const int16_t v*_snap` declarations at top of `spu94_reverb_body`; no stage re-reads v* from state |
| D-09 | Hard-clip as its own named stage function with overflow_magnitude out-param | HONORED | `spu94_reverb_hard_clip` exists as independent function accepting int32 inputs, emitting int16 + int32 overflow |
| D-10 | vIIR = INT16_MIN anomaly: explicit branch at memory-write point, `sat_s16(-(int32_t)result)` | HONORED | `grep -c "vIIR_snap == INT16_MIN"` = 4 (L+R per SAME+DIFF IIR); `sat_s16(-(int32_t)result)` present at each branch |
| D-11 | Per-multiply err-tap scope (i): all multiplies in all stages feed per-stage int32 accumulators; overflow_magnitude on hard-clip | HONORED | `err_same_iir` (+5), `err_diff_iir` (+4), `err_comb` (+9), `err_apf1` (+5), `err_apf2` (+5), `err_output_scale` (+1) accumulation sites confirmed; `overflow_magnitude` accumulated in `spu94_reverb_body` |

---

### Data-Flow Trace (Level 4)

`spu94_reverb_body` currently drives the reverb network with `left_in = 0, right_in = 0`
(silent input). This is documented in the source as the correct no-op behavior until
Phase 5 (`spu94_process`) populates the mix-bus feed. This is not a data-flow gap — it
is an intentional, documented, and planned interim state. The reverb network is
algorithmically complete and exercises real buffer state (the work buffer is populated by
`reverb_buf_write` calls in the IIR, APF stages, and read by subsequent calls). The data
flow from register state → buffer taps → stage outputs is fully wired and exercised by
the composition-equivalence tests and fuzz harness.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 26 ctests pass | `ctest --test-dir build --output-on-failure` | 26/26 passed in 5.61s | PASS |
| D-10 anomaly branch count | `grep -c "vIIR_snap == INT16_MIN" src/spu94/spu94_reverb.c` | 4 | PASS |
| D-07 no int32 accumulator | `grep -cE "int32_t\s+sum[LR]?\s*=" src/spu94/spu94_reverb.c` | 0 | PASS |
| 5 ADRs in DECISIONS.md | `grep -cE "^## ADR-000[789]|^## ADR-001[01]" docs/DECISIONS.md` | 5 | PASS |
| D-01 internal header NOT public | `ls include/spu94/spu94_reverb_internal.h` | NOT FOUND | PASS |
| 9 exported reverb symbols | `nm -D build/src/spu94/libspu94.so \| grep -cE "T spu94_reverb_"` | 9 | PASS |
| Single `spu94_reverb_body` call site | `grep -c "spu94_reverb_body(state);" src/spu94/spu94_tick.c` | 1 | PASS |
| grep-guard (no float/double/malloc) | `bash scripts/ci/grep-guard.sh` | OK (14 files) | PASS |
| verify-no-heap-symbols | `bash scripts/ci/verify-no-heap-symbols.sh` | OK | PASS |
| No setter calls in reverb stages | `! grep -E "spu94_set_reg_" src/spu94/spu94_reverb.c` | Clean | PASS |
| SC-5a comb-sum ADR | `grep -q "comb-sum" docs/DECISIONS.md` | Present | PASS |
| SC-5b L/R timing ADR | `grep -qE "L/R.*timing|register-write timing" docs/DECISIONS.md` | Present | PASS |

---

### Requirements Coverage

| Requirement | Source Plans | Description | Status | Evidence |
|-------------|-------------|-------------|--------|---------|
| CORE-02 | 03-01, 03-04 | Hard clip / saturation behavior on the mix bus | SATISFIED | `spu94_reverb_hard_clip` is its own function; `test_reverb_hard_clip.c` has dedicated ctest target; ADR-0009 documents the placement decision |
| CORE-05 | 03-01, 03-02, 03-03 | All-pass + comb filter network topology matching nocash processing order | SATISFIED | Full 7-stage network implemented in nocash E1 order; `test_reverb_body` asserts composition equivalence; `ctest -R reverb_comb`, `reverb_apf1`, `reverb_apf2` green |
| CORE-08 | 03-02 | Reproduce the documented vIIR=-0x8000 hardware anomaly | SATISFIED | Explicit branch at all 4 memory-write points; ADR-0010 records mechanism; `test_reverb_same_iir` + `test_reverb_diff_iir` both pass anomaly test |
| TEST-06 | 03-02, 03-04 | vIIR=-0x8000 hardware anomaly specifically tested | SATISFIED | Anomaly test + non-anomaly control case at vIIR=INT16_MIN+1 present in both `test_reverb_same_iir.c` and `test_reverb_diff_iir.c`; re-asserted in `test_reverb_edges.c` |
| TEST-07 | 03-04 | Fixed-point saturation, truncation, and overflow edge cases specifically tested | SATISFIED | `test_reverb_edges.c` has 12 sub-tests; covers INT16_MIN², ASR truncation direction, cascading-sat distinguishing case, APF Pitfall-7 edge, IIR tap-subtraction Pitfall-1, err invariants, Pitfall-8 |

---

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `tests/python/derive_reverb_reference.py` | 217 | "DuckStation / Mednafen-PSX witnesses" mention | INFO | The mention is in a docstring of `ref_comb` explaining the D-07 divergence rationale. It is not GPL code provenance — it is a behavioral reference citation in a comment. The C test files are completely GPL-free. No impact on algorithmic correctness or derivation chain. |
| `src/spu94/spu94_reverb.c` | 565-569 | `left_in = 0; right_in = 0` silent input | INFO | Intentional documented placeholder — Phase 5 will wire the public mix-bus feed. The comment explicitly names Phase 5 (`spu94_process`) as the successor. Not a stub; reverb network exercises real buffer state via IIR/comb/APF tap reads/writes regardless of input scale output. |

No BLOCKER or WARNING anti-patterns found.

---

### Human Verification Required

None. All phase-3 behavioral requirements are exercised by automated ctests. Visual
appearance, real-time behavior, and external service integration are not in scope for
Phase 3 (algorithmic level only). Phase 7 (witness-diff harness) will be the first phase
requiring human perceptual judgment.

---

## Gaps Summary

No gaps. All 5 ROADMAP success criteria verified, all 5 requirement IDs (CORE-02,
CORE-05, CORE-08, TEST-06, TEST-07) satisfied, all 11 locked decisions (D-01..D-11)
honored in code, 26/26 ctests green including the 10^6-step fuzz harness.

The two INFO-level items noted in Anti-Patterns are expected: the GPL-witness docstring
citation in the Python reference script is documentation of the D-07 divergence
rationale (not a derivation dependency), and the silent-input placeholder is explicitly
deferred to Phase 5 per the documented plan.

Phase 3 is complete and ready for Phase 4 (39-tap half-band FIR).

---

_Verified: 2026-04-20_
_Verifier: Claude (gsd-verifier)_
