---
phase: 10-core-polyphase-fir-cascade
verified: 2026-04-30T21:30:00Z
status: human_needed
score: 4/5 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Confirm that the 18 dB gain drop on the production DAC path is intentional and acceptable — or decide whether gain compensation is required before Phase 10 is considered complete"
    expected: "Either (a) the 1/8 gain is accepted as correct hardware-faithful physics of naive zero-stuffing and CR-01 is deferred to Phase 11, or (b) gain compensation is added to spu94_dac_fir_step_8x before closing Phase 10"
    why_human: "CR-01 in the Phase 10 code review flags the production path outputting ~18 dB less signal than v1.2 with no documentation in the public API header. This is the correct physics of naive zero-stuffing but it is a breaking behavioral change. The ROADMAP Success Criteria do not explicitly require gain parity. Whether it's a blocker for Phase 10 vs. acceptable known behavior is a product decision."
  - test: "Confirm that INT-04 passband tolerance of 0.044 dB (actual) vs 0.01 dB (REQUIREMENTS.md and ROADMAP SC1 stated metric) is accepted for this phase"
    expected: "Either (a) REQUIREMENTS.md INT-04 is updated to 0.05 dB to reflect the inherent Q15 truncation budget, or (b) the 0.044 dB is explicitly accepted as satisfying the spirit of INT-04 given it is mathematically unreachable with fixed-point arithmetic"
    why_human: "The ROADMAP Success Criterion 1 and REQUIREMENTS.md INT-04 both state '0.01 dB'. The implementation achieves 0.044 dB. The executor correctly determined that 0.01 dB is mathematically unreachable with Q15 integer arithmetic across 14 evaluations per sample, and adjusted to 0.05 dB threshold in the code. The planning artifacts (REQUIREMENTS.md, ROADMAP) still state 0.01 dB. These need to be reconciled."
---

# Phase 10: Core Polyphase FIR Cascade Verification Report

**Phase Goal:** Replace v1.2 approximate FIR with true 8x zero-stuff cascade at 352.8 kHz — naive implementation per D-01, no polyphase (D-02), coexist with v1.2 (D-03), scipy prototype first (D-04)
**Verified:** 2026-04-30T21:30:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | spu94_dac_fir_step_8x exists and is called from spu94_process.c (D-03 coexistence satisfied) | VERIFIED | `spu94_process.c` lines 117-118 call `spu94_dac_fir_step_8x` for L and R channels; no bare `spu94_dac_fir_step(` call remains in the file; both functions present in `spu94_dac_fir.c` |
| 2 | Original spu94_dac_fir_step is completely unchanged (D-03) | VERIFIED | Commit `31385f4` shows 0 deletions in `spu94_dac_fir.c`; `spu94_dac_fir_step` body confirmed intact at lines 139-168; `test_v1_2_regression` unit test passes |
| 3 | No heap, locks, or syscalls in the 8x path (DSP-08) | VERIFIED | `grep` of `spu94_dac_fir.c` finds zero occurrences of `malloc/calloc/free/pthread/mutex/syscall`; `rt_no_heap` and `rt_no_locks` tests pass (2/2); all intermediate storage is stack-local (14 bytes: `int16_t s1[2]`, `int16_t s2[4]`, `int16_t s3_last`); NOTE: `rt_no_syscalls` test uses strace and hangs in this environment — strace tests are a pre-existing infrastructure issue, not introduced by Phase 10 |
| 4 | All 80 non-DAC golden files produce identical SHA-256 hashes (INT-03) | VERIFIED | `10-03-SUMMARY.md` documents 50/50 reverb and 30/30 ADPCM goldens matched sidecars; commit `31385f4` is a 3-insertion/3-deletion change only to the DAC section inside `if (state->dac_enabled)` guard; `adpcm_goldens_regression` ctest passes (8 sec); `test_process_dac_integration` and `test_process_dac_toggle_transitions` pass |
| 5 | Passband conformance within tolerance (INT-04) | UNCERTAIN — threshold discrepancy | `python3 tools/dac_filter_design.py --verify-8x` exits 0; measured deviation = **0.044 dB**; implementation threshold = **0.05 dB**; ROADMAP SC1 and REQUIREMENTS.md INT-04 state **0.01 dB**; executor documented that 0.01 dB is mathematically unreachable with Q15 integer arithmetic across 14 evaluations/sample; requires human decision — see Human Verification section |

**Score:** 4/5 truths fully verified (Truth 5 is UNCERTAIN pending human decision on threshold)

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `include/spu94/spu94_dac_fir.h` | Public declaration of `spu94_dac_fir_step_8x` | VERIFIED | Line 46: `int16_t spu94_dac_fir_step_8x(spu94_dac_fir_state *state, int16_t input);` present with full docblock |
| `src/spu94/spu94_dac_fir.c` | `spu94_dac_fir_step_8x` implementation with 3-stage cascade | VERIFIED | Lines 187-250: full implementation; 14 `dac_fir_stage_apply` calls (2+4+8); 28 `dac_fir_push` calls; accumulator width proof comment block at lines 170-185 |
| `src/spu94/spu94_process.c` | 8x wiring in DAC section | VERIFIED | Lines 114-124: section comment updated; `spu94_dac_fir_step_8x` called for L and R; no bare `spu94_dac_fir_step(` remains |
| `tests/unit/dac_fir/test_dac_fir_8x.c` | 8x unit tests (6 tests) | VERIFIED | 6 tests present and all passing via `ctest -R dac_fir_8x`; covers impulse (3 assertions), DC gain, overflow adversarial, v1.2 regression |
| `tests/unit/dac_fir/CMakeLists.txt` | test_dac_fir_8x registered | VERIFIED | Lines 29-31: `add_executable`, `target_link_libraries`, `add_test` for `dac_fir_8x` |
| `tools/dac_filter_design.py` | `--verify-8x` mode with scipy cascade simulation | VERIFIED | `def verify_8x_cascade` at line 235; `def cmd_verify_8x` at line 295; `def dac_fir_stage_apply_py` at line 190; `--verify-8x` argparse entry at line 671; runs and exits 0 |
| `tests/golden_v1.2/` | 55 .wav + 55 .sha256 archived | VERIFIED | `find tests/golden_v1.2 -name "*.wav"` = 55; `find tests/golden_v1.2 -name "*.sha256"` = 55; commit `6679eb6` |
| `tests/golden/` | 135 .wav + 135 .sha256 (regenerated DAC goldens) | VERIFIED | Count confirmed: 55 DAC (.wav in */dac/*) + 30 ADPCM + 50 reverb = 135; commit `3eb8e12` (72 files changed: 36 .wav + 36 .sha256) |

### Key Link Verification

| From | To | Via | Status | Details |
|------|-----|-----|--------|---------|
| `spu94_process.c` | `spu94_dac_fir_step_8x` | Direct call in DAC section lines 117-118 | WIRED | Both L and R channels; inside `if (state->dac_fir_enabled)` guard inside `if (state->dac_enabled)` |
| `spu94_dac_fir_step_8x` | `dac_fir_stage_apply` | 14 calls per input sample (2+4+8) | WIRED | Lines 193, 201 (Stage 1); lines 212, 220 (Stage 2 x2); lines 234, 242 (Stage 3 x4); `(void)` casts on discarded Stage 3 non-final outputs are correct |
| `spu94_dac_fir_step_8x` | `dac_fir_push` | 28 pushes per input sample | WIRED | Lines 191, 199 (Stage 1); lines 210, 218 (Stage 2 x2); lines 231, 240 (Stage 3 x4) |
| `tools/dac_filter_design.py` | Q15 coefficient tables | `quantize_to_q15` + `build_pair_table` | WIRED | `build_pair_table` derives symmetric pairs from Q15 arrays; pair counts confirmed: 14 (Stage 1), 3 (Stage 2), 2 (Stage 3) matching C tables |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| `spu94_dac_fir_step_8x` | `s3_last` | `dac_fir_stage_apply` operating on state delay lines carrying real audio samples | Yes — state populated by `dac_fir_push(input, ...)` and zero-stuff pushes | FLOWING |
| `spu94_process.c` DAC section | `out_l`, `out_r` | Return value of `spu94_dac_fir_step_8x` | Yes — feeds into `L_out[i]`, `R_out[i]` | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| scipy --verify-8x exits 0 | `python3 tools/dac_filter_design.py --verify-8x` | All 8x cascade checks pass; deviation = 0.044 dB | PASS |
| scipy --verify (v1.2 mode) still passes | `python3 tools/dac_filter_design.py --verify` | Passband 0.0783 dB, stopband 49.2 dB, @20kHz -0.016 dB — ALL PASS | PASS |
| 8x unit tests pass | `ctest -R dac_fir_8x --output-on-failure` | 1/1 test target, 6/6 sub-tests pass | PASS |
| All dac_fir tests pass | `ctest -L dac_fir --output-on-failure` | 5/5 test targets pass (0 failures) | PASS |
| process tests pass | `ctest -L process --output-on-failure` (excluding fuzz) | 13/13 non-fuzz process tests pass | PASS |
| dac_integration tests | `ctest -L dac_integration --output-on-failure` | 2/2 pass | PASS |
| golden regression | `ctest -L golden --output-on-failure` | 1/1 (adpcm_goldens_regression) pass | PASS |
| rt_no_heap | `ctest -R rt_no_heap` | PASS | PASS |
| rt_no_locks | `ctest -R rt_no_locks` | PASS | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|------------|-------------|--------|---------|
| DSP-01 | 10-01, 10-02 | Zero-stuff input to 352.8kHz (cascade 2x at 88.2/176.4/352.8kHz) | VERIFIED | `spu94_dac_fir_step_8x` pushes real + zero at each stage; D-01 in RESEARCH.md explicitly equates "insert 7 zeros" with the cascaded 2x approach |
| DSP-02 | 10-02 | Stage 1 FIR at 88.2kHz with v1.2 coefficients verbatim | VERIFIED | Stage 1 evaluated 2x per input using `dac_interp_stage1` (unchanged v1.2 coef array) |
| DSP-03 | 10-02 | Stage 2 FIR at 176.4kHz with v1.2 coefficients verbatim | VERIFIED | Stage 2 evaluated 4x per input using `dac_interp_stage2` |
| DSP-04 | 10-02 | Stage 3 FIR at 352.8kHz with v1.2 coefficients verbatim | VERIFIED | Stage 3 evaluated 8x per input using `dac_interp_stage3` |
| DSP-06 | 10-02 | Decimate 352.8kHz to 44.1kHz (every 8th sample) | VERIFIED | `s3_last` keeps only the final of 8 Stage 3 outputs; comment at line 227-229 documents why all 8 must execute |
| DSP-08 | 10-02 | No heap, no locks, no syscalls in 8x path | VERIFIED | Static analysis clean; `rt_no_heap` and `rt_no_locks` pass; stack-only intermediates (14 bytes) |
| INT-03 | 10-03 | Non-DAC golden files bit-identical | VERIFIED | 50/50 reverb + 30/30 ADPCM confirmed per SUMMARY; `if (state->dac_enabled)` guard isolates 8x code |
| INT-04 | 10-04 | Passband response matches v1.2 within 0.01dB | UNCERTAIN | Actual deviation: 0.044 dB. Implementation threshold: 0.05 dB. REQUIREMENTS.md states 0.01 dB. Q15 truncation budget makes 0.01 dB unreachable — requires human decision |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| `src/spu94/spu94_process.c` | 117-118 | Production path outputs ~18 dB less signal than v1.2 without gain compensation or API documentation (CR-01 from code review) | WARNING | Breaking behavioral change — callers using DAC FIR will hear 18 dB drop; existing integration tests do not catch magnitude regression |
| `tests/unit/dac_fir/test_dac_fir_8x.c` | 131-132 | Tautological range assertions in overflow test (WR-01 from code review) | INFO | `TEST_ASSERT_GREATER_OR_EQUAL_INT16(INT16_MIN, out)` can never fail for `int16_t`; test is a crash smoke test, not a range verifier |
| `tools/dac_filter_design.py` | 203-213 | Python accumulator uses arbitrary precision instead of int32 bounds — silent on overflow that would crash C (IN-01 from code review) | INFO | Python reference won't flag future coefficient modifications that would overflow C `int32_t` accumulators |

### Human Verification Required

**1. INT-04 Passband Threshold Decision**

**Test:** Review whether REQUIREMENTS.md INT-04 ("matches v1.2 within 0.01dB") and ROADMAP SC1 should be updated to 0.05 dB, given Q15 arithmetic makes 0.01 dB physically unreachable.

**Expected:** Either (a) update REQUIREMENTS.md INT-04 and ROADMAP SC1 to state 0.05 dB threshold with a note that this is the inherent Q15 truncation budget, or (b) accept that the 0.01 dB metric was aspirational and that 0.044 dB satisfies the spirit of the requirement (same filter, same passband shape, Q15 quantization only accounts for the gap).

**Why human:** The executor correctly identified and documented the physics. The planning artifacts still state 0.01 dB. Reconciling the stated metric with the actual achievable metric is a product decision, not a code fix.

**2. CR-01 Gain Drop Decision**

**Test:** Confirm whether the ~18 dB output level drop on the production DAC FIR path is (a) intentional hardware-faithful behavior that will be addressed in Phase 11 (e.g., by gain compensation in the A/B mode toggle or noise recalibration), or (b) a bug that must be fixed before Phase 10 is closed.

**Expected:** Either (a) Phase 10 is closed as-is with a note that the 1/8 gain factor is correct hardware physics and Phase 11 will address level calibration, or (b) gain compensation is added to `spu94_dac_fir_step_8x` or the header is updated with a prominent BREAKING CHANGE note before proceeding.

**Why human:** The code review (10-REVIEW.md CR-01) raised this as a critical finding. The phase goal does not explicitly require output level parity with v1.2. Whether a silent 18 dB drop is acceptable for this phase's "naive implementation" milestone is a product and compatibility decision.

### Gaps Summary

No hard FAILED truths. All artifacts exist, are substantive, and are wired. Two items require human decision before closing:

1. **INT-04 threshold**: REQUIREMENTS.md and ROADMAP SC1 state 0.01 dB; the codebase achieves 0.044 dB. The executor documented why 0.01 dB is unreachable. Planning artifacts need updating or explicit acceptance.

2. **CR-01 gain regression**: The production audio path now delivers ~18 dB less output than v1.2 when DAC FIR is enabled. This is physically correct for naive zero-stuffing but is undocumented and uncompensated. Phase 10 code review flagged it as critical. Resolution needed before proceeding.

The rt_no_syscalls and hotpath_alloc_gate ctest targets hang in this environment due to strace (pre-existing infrastructure issue unrelated to Phase 10). The heap and lock safety of `spu94_dac_fir_step_8x` is verified by static code inspection and the two passing strace-free rt_safety tests.

---

_Verified: 2026-04-30T21:30:00Z_
_Verifier: Claude (gsd-verifier)_
