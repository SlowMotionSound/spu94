---
phase: 16-interpolation-engine
verified: 2026-05-05T19:45:00Z
status: human_needed
score: 6/6 must-haves verified
overrides_applied: 0
human_verification:
  - test: "Decide whether NaN input causing out-of-bounds array read (CR-01) requires a fix before Phase 17"
    expected: "NaN passed to spu94_interp_set_morph should clamp to 0.0 (Half Echo), not cause undefined behavior"
    why_human: "All 4 roadmap success criteria pass for valid inputs. The NaN bug contradicts the threat model claim T-16-02 but is not covered by any success criterion. Developer must decide: fix now vs accept risk and fix later."
---

# Phase 16: Interpolation Engine Verification Report

**Phase Goal:** A morph position value produces the correct interpolated register set for any point along the 9-preset continuum
**Verified:** 2026-05-05T19:45:00Z
**Status:** human_needed
**Re-verification:** No -- initial verification

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Morph position 0.0 produces Half Echo register values | VERIFIED | test_interp_clamp_below_zero passes; test_interp_waypoint_identity at w=0 passes |
| 2 | Morph position 1.0 produces Delay register values | VERIFIED | test_interp_clamp_above_one passes; test_interp_waypoint_identity at w=8 passes |
| 3 | Morph position 0.5 produces registers midway between Studio B and Studio C | VERIFIED | test_interp_midpoint_linear passes at morph 0.4375 (exact midpoint of segment 3-4) |
| 4 | At each of the 9 waypoint positions, output registers are bit-identical to the corresponding Sony factory preset | VERIFIED | test_interp_waypoint_identity iterates all 9 waypoints x 35 registers; all pass |
| 5 | vLOUT, vROUT remain 0x7FFF; vLIN, vRIN remain 0x8000; mBASE remains 0x0000 regardless of morph position | VERIFIED | test_interp_fixed_registers tests 5 positions across full range; all pass |
| 6 | Signed v-prefix registers interpolate through negative values without unsigned wraparound | VERIFIED | test_interp_signed_no_wraparound verifies vCOMB2 and vWALL at midpoint Half Echo->Room; correct negative results |

**Score:** 6/6 truths verified

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `src/spu94/spu94_interp.c` | Interpolation engine with waypoint table, position mapping, linear interpolation | VERIFIED | 147 LOC, contains spu94_interp_set_morph, waypoint table, fixed-register check, signed/unsigned dispatch |
| `include/spu94/spu94.h` | Public API declaration for spu94_interp_set_morph | VERIFIED | Declaration at line 552, SPU94_INTERP_WAYPOINT_COUNT macro at line 527 |
| `tests/unit/interp/test_interp.c` | Unit tests covering all 5 INTERP requirements (min 100 lines) | VERIFIED | 314 LOC, 7 sub-tests covering INTERP-01 through INTERP-05 plus edge cases |

### Key Link Verification

| From | To | Via | Status | Details |
|------|------|------|--------|---------|
| spu94_interp.c | spu94_presets[] | reads preset register tables through waypoint index mapping | WIRED | Line 83-84: `spu94_presets[spu94_interp_waypoints[seg]].regs` |
| spu94_interp.c | spu94_set_reg_i16/u16 | writes interpolated values through engine-layer setters | WIRED | Lines 93-144: 9 call sites for i16, 3 for u16 |
| spu94_interp.c | spu94_reg_type | queries register signedness for dispatch | WIRED | Lines 106, 114, 124: three dispatch points |

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|-------------------|--------|
| spu94_interp.c | a[], b[] (preset regs) | spu94_presets[] (static .rodata table in spu94_presets.c) | Yes -- hardcoded Sony factory values from spec | FLOWING |
| spu94_interp.c | interpolated values | float arithmetic on preset register pairs | Yes -- computed per call, written to state via setters | FLOWING |

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 7 sub-tests pass | `./build/tests/unit/interp/test_interp` | 7 Tests 0 Failures 0 Ignored OK | PASS |
| Symbol exported from shared library | `nm -D build/src/spu94/libspu94.so \| grep interp` | `T spu94_interp_set_morph` at 0x8734 | PASS |
| ctest integration works | `ctest --test-dir build -R test_interp` | 1/1 Test passed | PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-----------|-------------|--------|----------|
| INTERP-01 | 16-01 | Morph position maps to adjacent preset pair + fractional distance | SATISFIED | test_interp_clamp_below_zero (0.0), test_interp_clamp_above_one (1.0), test_interp_waypoint_identity (all 9 positions) |
| INTERP-02 | 16-01 | All 30 active registers linearly interpolate between adjacent presets | SATISFIED | test_interp_midpoint_linear verifies all 30 non-fixed registers at segment 3-4 midpoint |
| INTERP-03 | 16-01 | Fixed registers hold constant values regardless of morph | SATISFIED | test_interp_fixed_registers at 5 positions; also verified in waypoint_identity test |
| INTERP-04 | 16-01 | Waypoint positions produce bit-identical Sony preset registers | SATISFIED | test_interp_waypoint_identity: 9 waypoints x 35 registers, bit-exact comparison |
| INTERP-05 | 16-01 | Signed registers interpolate correctly through negative values | SATISFIED | test_interp_signed_no_wraparound: vCOMB2=-16936, vWALL=-24640 (correct signed results) |

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| spu94_interp.c | 62-67 | NaN passes through clamp (CR-01 from code review) | WARNING | Potential crash if NaN reaches the function; does not affect valid [0.0, 1.0] inputs |
| test_interp.c | 260 | Midpoint formula ((va+vb)/2) does not precisely match impl formula (va+trunc((vb-va)*frac)) | INFO | Currently agrees for all Studio B/C differences (all even); fragile if presets change |

### Human Verification Required

### 1. NaN Input Safety Decision

**Test:** Pass `NAN` (from `<math.h>`) to `spu94_interp_set_morph`. Observe whether it crashes or produces defined behavior.
**Expected:** Should clamp to 0.0 and produce Half Echo registers (per threat model T-16-02 claim).
**Actual:** On x86, `(int)NAN` produces `INT_MIN` (-2147483648). This causes `spu94_interp_waypoints[INT_MIN]` -- an out-of-bounds read from `.rodata`, likely segfault.
**Why human:** The four roadmap success criteria are all satisfied for valid morph positions [0.0, 1.0]. NaN is not a "morph position" -- it represents invalid/corrupted input. The developer must decide:

- **Option A: Fix now** -- change line 66 from `if (position < 0.0f)` to `if (!(position >= 0.0f))`. One-line fix that catches NaN via unordered comparison semantics. Also add a NaN test case. Low effort, eliminates UB.
- **Option B: Accept risk, fix later** -- Phase 17 (GUI) will feed the function from a JUCE slider which always produces valid floats. The NaN path is only reachable from direct C API callers passing corrupted data. Accept as known defect, track for next hardening pass.

**Impact if unfixed:** Any caller passing NaN (uninitialized float, 0.0/0.0 division, corrupted parameter) triggers undefined behavior. In a plugin context, this could crash the DAW.

### Gaps Summary

No gaps in the stated success criteria. All four roadmap success criteria pass. All five INTERP requirements are satisfied with test evidence. The NaN input validation bug (CR-01) contradicts the implementation's own threat model comment (T-16-02) but is not covered by any roadmap success criterion. Surfaced for human decision.

---

_Verified: 2026-05-05T19:45:00Z_
_Verifier: Claude (gsd-verifier)_
