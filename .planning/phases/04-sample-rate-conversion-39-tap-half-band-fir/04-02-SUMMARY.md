---
phase: 04
plan: 02
subsystem: sample-rate-conversion
tags: [fir, folded-form, bit-identity, audit-witness, overflow-proof, accumulator-width, clamp-once, err-tap, overflow-tap]
requires:
  - Phase 4 Plan 01 (39-tap coefficient TU + internal-header prototypes + 14 new spu94_state FIR fields + Python reference oracle)
  - Phase 1 (q15_mul_truncate_with_err + sat_s16 + ADR-0001 ASR policy)
  - Phase 2 (caller-allocated spu94_state shell; spu94_init contract)
  - Phase 3 (per-stage err-tap precedent in spu94_reverb.c)
provides:
  - src/spu94/spu94_fir.c -- folded-form spu94_fir_decimate + spu94_fir_interpolate + D-01 literal-reference audit + folded-form audit companion
  - D-02 Accumulator Width Proof comment block in-source (0x5CD30000 worst case, 0xB9A6 L1 norm, 2.79 dB / 0.46 bits headroom)
  - D-04 cascade-clamp compile-time seam (#ifdef SPU94_FIR_CASCADE_CLAMP, default undefined)
  - D-05 overflow-magnitude tap + D-06 aggregate post-shift err-tap, no branches on the hot-path add
  - tests/unit/fir/test_fir_bit_identity.c -- 10^5 random + adversarial; proves folded == literal (D-01 audit witness)
  - tests/unit/fir/test_fir_overflow_proof.c -- adversarial hits acc == 0x5CD2632E + UB-free negative; SC-3 closed
  - tests/unit/fir/test_fir_decimate.c -- per-stage phase / impulse / DC / L-R independence vs Python reference
  - tests/unit/fir/test_fir_interpolate.c -- per-stage impulse / DC phase gains / L-R independence vs Python reference
  - tests/python/derive_fir_reference.py --dump-test-tables (C-syntax oracle emitter)
affects:
  - src/spu94/spu94_fir_internal.h (+2 test-visible declarations: spu94_fir_decimate_literal_reference + spu94_fir_folded_reference)
  - src/spu94/CMakeLists.txt (spu94_fir.c appended to spu94_obj)
  - tests/unit/fir/CMakeLists.txt (+4 executables with LABELS "fir")
  - sizeof(struct spu94_state): unchanged at 536 bytes (Plan 02 adds no struct fields; uses Plan 01's 14 FIR fields)
tech-stack:
  added: []
  patterns:
    - folded-form FIR via coefficient symmetry h[k]==h[38-k]; center tap first, ascending k for pairs, skip-zero optimization
    - accumulator-width proof as in-source comment block (D-02) backed by empirical overflow-proof test + UBSan-in-CI as second oracle
    - literal 39-multiply reference function alongside production folded form; bit-identity asserted by test across 10^5 random inputs
    - compile-time D-04 seam (#ifdef SPU94_FIR_CASCADE_CLAMP) with test guard (#error) ensuring bit-identity audit only runs under clamp-once
    - aggregate post-shift err-tap (04-RESEARCH Pattern 1 reconciliation): err = acc - (shifted<<15), bit-faithful to D-03 clamp-once
    - Python oracle --dump-test-tables flag emits C-syntax reference arrays + #define constants for direct paste into test TUs
key-files:
  created:
    - src/spu94/spu94_fir.c (351 lines)
    - tests/unit/fir/test_fir_bit_identity.c (75 lines)
    - tests/unit/fir/test_fir_overflow_proof.c (65 lines)
    - tests/unit/fir/test_fir_decimate.c (107 lines)
    - tests/unit/fir/test_fir_interpolate.c (72 lines)
  modified:
    - src/spu94/spu94_fir_internal.h (103 -> 134 lines; +2 test-visible audit-reference declarations)
    - src/spu94/CMakeLists.txt (+1 source entry: spu94_fir.c)
    - tests/unit/fir/CMakeLists.txt (+4 test executable blocks)
    - tests/python/derive_fir_reference.py (164 -> 215 lines; +--dump-test-tables branch)
decisions:
  - D-06 aggregate err-tap interpretation implemented as acc - (shifted<<15); matches the 04-RESEARCH Pattern 1 reconciliation. Strict per-multiply err-tap was not chosen because it would engage a D-04 cascade-clamp math regime with different output values; aggregate is bit-faithful to clamp-once.
  - fir_interp_phase0_apply pair order: (18,20) first (spanning-center pair), then {(0,38),(2,36),(4,34),(6,32),(8,30),(10,28),(12,26),(14,24),(16,22)} in ascending small-k order. Deviates from the decimator helper's center-first + k=0..18 ascending order because phase-0 has no center tap (coef[19] is an odd index belonging to phase 1). Documented inline; summation order pinned.
  - Delay-line convention cross-check: C circular buffer (delay[idx]=oldest, push-then-advance, read logical-k via (idx+38-k)%39) produced bit-identical outputs to the Python shift-register reference (delay[0]=newest) on every pasted test vector on first build. No reconciliation needed.
  - 100000-iteration bit-identity test ran in 0.04s under -O2. No need to reduce budget for CI time.
  - "Branch-free" phrase rewritten to "No branches" in the D-05 tap comment after a live grep-guard catch (the word "free" is forbidden in core sources per grep-guard.sh). Logged as an auto-fix (Rule 3) below.
  - Full-table SHA-256 pin landed as metadata in this SUMMARY rather than as a C test (Unity + C SHA-256 would pull a crypto dep). Plan 04 Python audit will pin it in-band.
metrics:
  duration: "~7m 0s"
  completed: "2026-04-20T19:44:44Z"
  tasks: 3
  tdd_tasks: 0 (planned as 3; landed as 3 atomic feat/test/test commits -- the Task 1 implementation is self-consistent with the Python oracle, and Task 2's bit-identity test acts as the GREEN check for Task 1's folded-form math)
  auto_fixes: 1
  commits:
    - fbd7998 feat(04-02): land folded-form FIR stages + D-01 literal-reference audit witness
    - 3c3f68c test(04-02): land D-01 bit-identity audit + SC-3 accumulator-width proof
    - 96247f0 test(04-02): land per-stage decimate + interpolate tests vs Python reference
---

# Phase 4 Plan 02: Folded-Form FIR + D-01 Audit Witness Summary

Landed the production folded-form sample-rate-conversion FIR arithmetic. `spu94_fir_decimate` pushes a 44.1 kHz stereo sample into per-channel circular-buffer delay lines, advances a phase counter, and on every retained phase computes a folded-form 39-tap FIR output via center-tap + 9 non-zero pairs + zero-skip. `spu94_fir_interpolate` pushes a 22.05 kHz stereo sample and emits BOTH phases of the 2x upsampled 44.1 kHz output (phase 0 via even-offset subfilter; phase 1 via center-tap passthrough). A literal 39-multiply audit reference + folded-form audit companion are test-visible (declared in the internal header) and prove folded == literal under D-03 clamp-once across 10^5 random inputs + 1 adversarial input. The D-02 accumulator-width proof lives as a comment block in `src/spu94/spu94_fir.c` and is empirically confirmed by `test_fir_overflow_proof` driving the accumulator to the achievable int16 ceiling `0x5CD2632E` without UB. D-04 cascade-clamp is wired as a compile-time `#ifdef` seam (default off). D-05 overflow-magnitude and D-06 aggregate post-shift err taps are branchless (always-add with pre-selected magnitude).

## Files Created + Line Counts

| File | Lines | Purpose |
|------|-------|---------|
| `src/spu94/spu94_fir.c` | 351 | Folded-form decimator + interpolator + literal audit reference + folded audit companion; D-02 proof comment; D-04 #ifdef seam; D-05/D-06 taps. |
| `tests/unit/fir/test_fir_bit_identity.c` | 75 | 2 sub-tests: 10^5 Xorshift64 random + adversarial input; D-01 audit witness. |
| `tests/unit/fir/test_fir_overflow_proof.c` | 65 | 3 sub-tests: positive adversarial hits 0x5CD2632E, negative adversarial UB-free + magnitude >= bound, overflow-magnitude tap records saturation. SC-3 closed. |
| `tests/unit/fir/test_fir_decimate.c` | 107 | 4 sub-tests: phase alternation (1/0/1/0 + reset), impulse response (40 retained outputs vs Python), DC steady state (+0x0400 -> 0x03FF), L/R independence. |
| `tests/unit/fir/test_fir_interpolate.c` | 72 | 3 sub-tests: impulse (P0 = -1, P1 = 0), DC phase gains (P0 = 0x01FF, P1 = 0x0200 hand-verified), L/R independence. |

## Files Modified

| File | Change | Notes |
|------|--------|-------|
| `src/spu94/spu94_fir_internal.h` | 103 -> 134 lines | +2 audit-reference declarations (spu94_fir_decimate_literal_reference + spu94_fir_folded_reference), inserted above the production stage declarations so audit-first ordering is visible. |
| `src/spu94/CMakeLists.txt` | +1 line | spu94_fir.c appended to spu94_obj source list after spu94_fir_coef.c (alphabetical). |
| `tests/unit/fir/CMakeLists.txt` | +20 lines | 4 new `add_executable` + `add_test` + `LABELS "fir"` blocks. |
| `tests/python/derive_fir_reference.py` | 164 -> 215 lines | `--dump-test-tables` CLI flag emits the decimator impulse array + DC settled #define + interpolator impulse + DC phase-0/phase-1 #defines, in C-syntax for direct paste. |

## Production FIR Stage Function Layout (src/spu94/spu94_fir.c)

| Function | Linkage | Purpose |
|----------|---------|---------|
| `fir_read_tap` | static inline | Read logical tap k (0=newest, 38=oldest) from circular buffer using `(idx+38-k)%39`. |
| `fir_push` | static inline | Write sample to `delay[idx]` and advance `idx = (idx+1)%39`. |
| `fir_folded_apply` | static | Folded-form 39-tap FIR over 9 non-zero pairs + center tap; applies D-03/D-04/D-05/D-06. |
| `spu94_fir_decimate` | extern `T` | Production decimator (CORE-06); retained-phase invokes fir_folded_apply for L + R. |
| `fir_interp_phase0_apply` | static | Even-offset subfilter over 10 non-zero pairs. |
| `fir_interp_phase1_apply` | static | Center-tap only (half-band Type I). |
| `spu94_fir_interpolate` | extern `T` | Production interpolator (CORE-07); emits phase-0 + phase-1 for L + R. |
| `spu94_fir_decimate_literal_reference` | extern `T` | D-01 literal 39-multiply audit reference; test-only use. |
| `spu94_fir_folded_reference` | extern `T` | D-01 folded-form audit companion with same semantics on history[]. |

`nm build/src/spu94/libspu94.so`:

```
T spu94_fir_decimate
T spu94_fir_decimate_literal_reference
T spu94_fir_folded_reference
T spu94_fir_interpolate
```

No `T spu94_fir_chain_step` symbol (Plan 03 owns).

## Accumulator Width Proof (as Landed in spu94_fir.c)

```
Sum of |h[k]| for the 39-tap table: 47,526 = 0xB9A6.
INT16_MAX_MAGNITUDE: 32,768 = 0x8000 (abs(INT16_MIN)).
Analytic product (unreachable by int16 inputs, requires |x|=32768 on both sides):
    47,526 * 32,768 = 1,557,331,968 = 0x5CD30000.
Achievable int16 bound (x = +32767 if coef>=0 else -32768):
    empirically 0x5CD2632E = 1,557,291,822 (test_fir_overflow_proof asserts).
INT32_MAX = 0x7FFFFFFF = 2,147,483,647. Headroom: 2.79 dB / 0.46 bits. QED.
```

Interpolator phase-0 subfilter: worst-case 0x3CD30000 = 1,020,461,056 (6.46 dB / 1.07 bits headroom).
Phase-1 subfilter (center tap only): 0x4000 * |INT16_MIN| = 0x20000000 (trivially safe).

D-02 seam: if a future composition tightens the decimator margin below zero bits, promote the local accumulator to int64. Zero caller-visible change.

## SHA-256 Coefficient Pin

Independent byte-level transcription (little-endian packed int16, matches x86/ARM little-endian memory layout):

```
24378792bec9911a7772fea623c8b0f5541f7a59ef1576a741fb5b2f8a0482f8
```

(Big-endian alternative: `0cf885a66004331c8992a0e312e1eb8dfd7d29a8c699805bd64a7af6b2d366ce`.)

Computed with:

```
python3 -c "
import struct, hashlib
coefs = [...39 values, transcribed independently...]
print(hashlib.sha256(b''.join(struct.pack('<h', c) for c in coefs)).hexdigest())
"
```

Plan 04's Python audit will pin this value in-band via `docs/BIBLIOGRAPHY.md` or a ctest Python helper.

## Test Results

| Test | Sub-tests | Status | Notes |
|------|-----------|--------|-------|
| `fir_coef_table` (Plan 01) | 5 | Pass | Unchanged from Plan 01. |
| `fir_bit_identity` (new) | 2 | Pass (0.04 s) | 10^5 random + 1 adversarial; folded == literal. |
| `fir_overflow_proof` (new) | 3 | Pass | Positive adversarial acc == 0x5CD2632E; negative magnitude >= bound; tap records saturation. SC-3 closed. |
| `fir_decimate` (new) | 4 | Pass | Every pasted reference value matched on first build. |
| `fir_interpolate` (new) | 3 | Pass | Phase-0 DC = 0x01FF, phase-1 DC = 0x0200 (hand-verified). |

Full suite:

```
ctest --test-dir build
Total Tests: 31     Passed: 31     Failed: 0
Label "fir":        5 tests
```

`bash scripts/ci/grep-guard.sh`: OK (17 files). `bash scripts/ci/verify-no-heap-symbols.sh`: OK.

## Reference Cross-Check Outcome

Every pasted reference value matched the C production model on first build. No reconciliation was needed.

The decimator impulse response (40 retained 22.05 kHz outputs for +0x7FFF impulse followed by 79 zeros) traces the coefficient table sampled at every-other 44.1 kHz step, producing the readable pattern -1 / 1 / -10 / 34 / -103 / 265 / -616 / 1331 / -2960 / 10245 / 10245 / -2960 / ... then zeros. This is the expected impulse-response shape -- the first 20 entries are the odd-indexed + even-indexed (phase-alternated) coefficients scaled by `>>15`, symmetric about the center of the 20-entry active window; the last 20 entries are zero because the impulse has left the 39-sample delay line.

Delay-line convention cross-check: the C circular-buffer convention `delay[idx]` = oldest slot / push-then-advance / read-logical-k via `(idx + 38 - k) % 39` agrees byte-for-byte with the Python shift-register reference (`history[0]` = newest). Both use the same summation order (center tap first, then `k = 0..18` non-zero pairs in ascending k), so the aggregate err-tap is identical too.

## Deviations from Plan

### Rule-Based Auto-Fixes

**1. [Rule 3 - Blocking] Replaced "Branch-free" with "No branches" in the D-05 tap comment.**

- **Found during:** Task 1 acceptance verification (`bash scripts/ci/grep-guard.sh` run after first build).
- **Issue:** The word "free" is in the grep-guard forbidden-token list (forbidden alongside float/double/malloc/calloc/realloc). The D-05 overflow-magnitude tap comment originally said "Branch-free on the hot path (ternary selects the value; we always add)", which triggered the guard.
- **Fix:** Rewrote as "No branches on the hot path (the if/else-if selects the magnitude value; we always add)". Same semantic; passes grep-guard.
- **Files modified:** `src/spu94/spu94_fir.c`
- **Commit:** `fbd7998` (Task 1 fix rolled into the same commit as the implementation).

### Other Notes

1. **Test-file header inserted `spu94_fir_folded_reference` declaration in Task 1 instead of Task 2.** Plan 02 Task 2 was specified to add this declaration + body. Because the declaration and body are trivially small (single inline function) and the Task 1 TU (`spu94_fir.c`) already contains the identical internal `fir_folded_apply` math, I landed both `spu94_fir_decimate_literal_reference` AND `spu94_fir_folded_reference` in Task 1's single commit `fbd7998`. Task 2's commit then contains only the two new test TUs + CMake wiring. This is a cosmetic reorganization of the plan's task boundaries; the total code landed is identical and every acceptance criterion for both tasks is met. Documented here for traceability.

2. **Interpolator impulse P1 == 0 explanation.** The spec in the plan text implied phase-1 would pass the impulse through scaled by `0x4000 >> 15 = 0.5`. In the circular-buffer production code (and the Python shift-register reference), the first call after `spu94_init` pushes the impulse to `delay[0]` / `history[0]` (newest slot); the phase-1 subfilter reads `history[19]` (logical tap 19 = center tap), which is still zero from the zero-initialized delay line. Hence `P1 = coef[19] * 0 = 0`. Phase 1's center-tap scaling only manifests once the impulse has propagated 19 samples through the delay line. The Python dump + C test agree; DC settled value `P1 = 0x0200 = (0x4000 * 0x0400) >> 15` is hand-verified.

### Authentication Gates

None encountered.

## Known Stubs

Plan 02 lands production math + audits. The following stubs remain intentional and are Plan 03 / 04 work:

| Stub | Location | Resolved by |
|------|----------|-------------|
| `spu94_fir_chain_step` prototype only (no body) | `src/spu94/spu94_fir_internal.h` | Plan 03 (chain composition) |
| `spu94_fir_chain_step_reverb_bypass` prototype only | `src/spu94/spu94_fir_internal.h` | Plan 03 (bypass variant for tests) |
| SHA-256 full-table coefficient pin (metadata in this SUMMARY only) | this file | Plan 04 Python audit |

No stubs block Plan 02's stated goals (folded-form arithmetic + audit witness + accumulator-width proof). The chain-wrapper stub is sealed at declaration granularity, guaranteeing Plan 03 has a stable target.

## Threat Flags

No new security-relevant surface beyond the plan's `<threat_model>` anticipation:
- T-04-INT-01 mitigated: 0x5CD2632E asserted + UBSan-in-CI.
- T-04-INT-02 accepted: summation order pinned + bit-identity test.
- T-04-BUF-01 / -02 mitigated: modular unsigned arithmetic (unsigned promotion in `(idx + 38u - k) % 39u`); verified by impulse + phase-alternation tests.
- T-04-COEF-01 mitigated: Plan 01 coef-table invariants + bit-identity surfaces any miscompile.
- T-04-STATE-01 accepted: err/overflow fields read-only (D-23).
- T-04-TAP-01 mitigated: overflow tap is always-add; no public mutator API.
- T-04-AUDIT-01 accepted: literal + folded references declared in INTERNAL header; tests are the only callers.
- T-04-CASCADE-01 mitigated: `#ifdef SPU94_FIR_CASCADE_CLAMP` + `#error` guard in test TU.

## Forward Dependencies Sealed

### For Plan 03 (chain wrapper + latency API)
- `spu94_fir_decimate(state, l, r, *out_l, *out_r, *out_valid)` ready; returns `*out_valid = 1` on retained phase, 0 on discarded.
- `spu94_fir_interpolate(state, l_22k, r_22k, *out_l_p0, *out_r_p0, *out_l_p1, *out_r_p1)` ready; emits both phases per call.
- Chain wrapper can compose: decimate -> (retained ? spu94_tick + interpolate : emit stored phase-1) without signature changes.
- Production stage functions operate on spu94_state's existing 14 Plan-01 fields; no struct extension needed for Plan 03.

### For Plan 04 (test battery + ADRs + witness classification)
- D-02 Accumulator Width Proof comment block in-source; ADR-Phase-4-II (accumulator width) can cite it verbatim.
- D-04 cascade-clamp seam present; Plan 04 can add a `test_fir_cascade_clamp` TU that builds with `-DSPU94_FIR_CASCADE_CLAMP` to witness the divergence.
- Bit-identity test runs in 0.04 s under -O2; Plan 04 can extend to 10^6 iterations without CI budget concerns.
- SHA-256 coefficient pin (little-endian: `24378792bec9911a7772fea623c8b0f5541f7a59ef1576a741fb5b2f8a0482f8`) metadata-only here; Plan 04 can promote to an in-band audit.

## Self-Check: PASSED

**Files exist:**
- FOUND: `src/spu94/spu94_fir.c`
- FOUND: `src/spu94/spu94_fir_internal.h` (modified)
- FOUND: `src/spu94/CMakeLists.txt` (modified)
- FOUND: `tests/unit/fir/test_fir_bit_identity.c`
- FOUND: `tests/unit/fir/test_fir_overflow_proof.c`
- FOUND: `tests/unit/fir/test_fir_decimate.c`
- FOUND: `tests/unit/fir/test_fir_interpolate.c`
- FOUND: `tests/unit/fir/CMakeLists.txt` (modified)
- FOUND: `tests/python/derive_fir_reference.py` (modified)

**Commits exist (in current worktree branch):**
- FOUND: fbd7998 (Task 1: folded-form stages + audit references)
- FOUND: 3c3f68c (Task 2: bit-identity + overflow-proof tests)
- FOUND: 96247f0 (Task 3: per-stage decimate + interpolate tests + Python --dump-test-tables)
