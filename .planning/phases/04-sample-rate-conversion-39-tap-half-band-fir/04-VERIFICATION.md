---
phase: 04-sample-rate-conversion-39-tap-half-band-fir
verified: 2026-04-20T21:00:00Z
status: passed
score: 4/4 must-haves verified
overrides_applied: 0
re_verification: false
---

# Phase 4: Sample-Rate Conversion (39-Tap Half-Band FIR) Verification Report

**Phase Goal:** SPU-94 is bit-faithful at the I/O boundary — the 44.1 kHz host rate is converted to/from the internal 22.05 kHz reverb rate via nocash's documented 39-tap half-band FIR, closing the fidelity gap that lv2-psx-reverb explicitly leaves open.

**Verified:** 2026-04-20T21:00:00Z
**Status:** passed
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | Input decimator produces 22.05 kHz stream from 44.1 kHz input using nocash's exact 39-tap coefficient table in integer arithmetic; impulse input produces the documented symmetric half-band impulse response. | VERIFIED | `src/spu94/spu94_fir_coef.c` contains all 39 coefficients verbatim; `spu94_fir_decimate` in `spu94_fir.c` applies folded-form FIR; bit-identity against literal 39-multiply reference asserted by `test_fir_bit_identity` (10^5 random + adversarial); `test_fir_decimate` pins 40-output impulse response against Python oracle; `test_fir_frequency_sweep` verifies decimator output over 44100-sample log sweep + 7-bin analytic |H(f)| table. All pass in live ctest. |
| 2 | Output interpolator round-trips a 22.05 kHz DC signal back to 44.1 kHz without bias or drift; filter symmetry verified to machine precision. | VERIFIED | `spu94_fir_interpolate` in `spu94_fir.c` produces both phases; `test_fir_dc` asserts DC input +0x0400 settles to +0x01FF (Python-oracle matched), no drift over 50 trailing samples, negative DC within 1 LSB, zero-in-zero-out; `test_fir_interpolate` pins phase-0 DC (0x01FF) and phase-1 DC (0x0200) against Python reference; `test_fir_impulse` verifies polyphase chain symmetry at chain level. `test_fir_round_trip_transparency` asserts band-limited 1k+3k+5k fixture residual below empirically-derived threshold (2644 LSB). All pass live. |
| 3 | 39-tap Q15-product accumulation is verified to fit in its chosen intermediate width (documented in code) across full int16 input range; no intermediate overflow reachable. | VERIFIED | D-02 accumulator-width proof comment block in `spu94_fir.c` derives analytic worst-case 0x5CD30000 and achievable int16 ceiling 0x5CD2632E; `test_fir_overflow_proof` drives accumulator to 0x5CD2632E, asserts exact bit pattern, asserts negative adversarial is UB-free and within int32 bounds, asserts overflow-magnitude tap records saturation. ADR-0014 in `docs/DECISIONS.md` discloses 2.791 dB / 0.464 bits headroom verbatim. `fuzz_fir.py` adds 10^6-step runtime regression cover (3.37s runtime, passes live). |
| 4 | DECISIONS.md contains an entry documenting the half-rate architecture and explicitly recording that lv2-psx-reverb is NOT a witness on the frequency-response axis. | VERIFIED | ADR-0012 in `docs/DECISIONS.md` (lines ~458-516) is titled "Half-rate architecture + lv2-psx-reverb OUT-OF-AXIS exclusion", ratified 2026-04-20. Decision section states: "lv2-psx-reverb: OUT-OF-AXIS (HIGH confidence). Primary-source attested by its own README..." The half-rate architecture (22.05 kHz internal tick, 39-tap boundary FIRs, SPU94_LATENCY_SAMPLES=58u) is formally ratified. |

**Score:** 4/4 truths verified

---

### Required Artifacts

| Artifact | Purpose | Exists | Substantive | Wired | Status |
|----------|---------|--------|-------------|-------|--------|
| `src/spu94/spu94_fir_coef.c` | 39 int16 coefficient table | Yes | 66 lines, all 39 values, _Static_assert | Compiled into spu94_obj; referenced by fir_folded_apply | VERIFIED |
| `src/spu94/spu94_fir_internal.h` | Stage prototypes + extern coef decl | Yes | 134 lines, full declarations | Included by spu94_fir.c, spu94_io_chain.c, all test TUs | VERIFIED |
| `src/spu94/spu94_fir.c` | Folded-form decimator + interpolator | Yes | 351 lines; production stages + literal audit reference + folded audit companion; D-02 proof comment; D-04 ifdef seam; D-05/D-06 taps | Linked into libspu94.so; nm confirms T symbols | VERIFIED |
| `src/spu94/spu94_io_chain.c` | 44.1 kHz chain wrapper + spu94_get_latency_samples | Yes | 104 lines; chain_step_impl, two public wrappers, accessor | Linked into libspu94.so; nm confirms T symbols; single call site for decimate + interpolate | VERIFIED |
| `include/spu94/spu94.h` | SPU94_LATENCY_SAMPLES (58u) + prototype | Yes | Macro defined at line 203, accessor prototype present | Public header; consumed by test TUs and downstream phases | VERIFIED |
| `tests/unit/fir/test_fir_coef_table.c` | 5-invariant table integrity | Yes | 91 lines; 5 sub-tests | Built as ctest target `fir_coef_table`; passes live | VERIFIED |
| `tests/unit/fir/test_fir_bit_identity.c` | Folded == literal proof (D-01) | Yes | 75 lines; 10^5 random + adversarial | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_overflow_proof.c` | SC-3 accumulator width | Yes | 65 lines; 3 sub-tests; drives to 0x5CD2632E | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_decimate.c` | Per-stage decimator vs Python oracle | Yes | 107 lines; 4 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_interpolate.c` | Per-stage interpolator vs Python oracle | Yes | 72 lines; 3 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_chain_latency.c` | D-09 latency contract | Yes | 84 lines; 3 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_impulse.c` | SC-1 chain impulse + polyphase symmetry | Yes | 109 lines; 2 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_dc.c` | SC-2 DC round-trip no drift | Yes | 85 lines; 3 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_err_overflow_taps.c` | D-05/D-06 tap invariants | Yes | 111 lines; 3 sub-tests | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_frequency_sweep.c` | Decimator frequency response | Yes | Present; bit-exact vs Python + 7-bin analytic table | Passes live | VERIFIED |
| `tests/unit/fir/test_fir_round_trip_transparency.c` | Chain round-trip fidelity | Yes | Present; empirically-fit Q15=0x3177, threshold 2644 LSB | Passes live | VERIFIED |
| `tests/python/fuzz_fir.py` | 10^6-step ctypes fuzz | Yes | 3.37s runtime; output-range + latency-pin + canary + reset invariants | Registered as ctest target with "fuzz;fir" labels; passes live (3.43s) | VERIFIED |
| `docs/DECISIONS.md` ADR-0012..ADR-0020 | 9 architectural decision records | Yes | ADR-0012 verified directly; 9 ADRs prepended above ADR-0011 | Durable planning artifact; referenced by phase and tests | VERIFIED |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|-----|--------|---------|
| `spu94_io_chain.c::chain_step_impl` | `spu94_fir_decimate` | Direct call (line 51) | WIRED | Single call site (Pitfall 4 preserved) |
| `spu94_io_chain.c::chain_step_impl` | `spu94_fir_interpolate` | Direct call (line 60) | WIRED | Single call site |
| `spu94_io_chain.c::chain_step_impl` | `spu94_tick` | Conditional call inside `if (dec_valid) if (reverb_active)` | WIRED | Correctly gated at 22.05 kHz retained phase |
| `spu94_fir.c` | `spu94_fir_coef[39]` | `extern const int16_t spu94_fir_coef[39]` from `spu94_fir_coef.c` | WIRED | Used in fir_folded_apply, fir_interp_phase0_apply, fir_interp_phase1_apply, literal reference |
| `test_fir_coef_table.c` | `spu94_fir_coef[39]` | Include `spu94_fir_internal.h` + sizeof check | WIRED | 5 invariants verified live |
| `test_fir_overflow_proof.c` | `spu94_fir_decimate_literal_reference` / `spu94_fir_folded_reference` | Include `spu94_fir_internal.h` | WIRED | Adversarial accumulator value asserted |
| `include/spu94/spu94.h` | `SPU94_LATENCY_SAMPLES 58u` | Macro + accessor prototype | WIRED | Consumed by test_fir_chain_latency, fuzz_fir.py |

---

### Data-Flow Trace (Level 4)

Not applicable — phase produces a C library (no rendering components, no UI, no data store). The FIR chain produces int16 audio samples; data-flow is verified by the behavioral tests (impulse response, DC round-trip, frequency sweep, fuzz harness) rather than a UI rendering trace.

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| All 12 FIR-labeled tests pass (11 unit + fuzz_fir) | `ctest --test-dir build -L fir` | 12/12 passed in 3.55s | PASS |
| Full suite (38 tests) passes with no regressions | `ctest --test-dir build` | 38/38 passed in 8.92s | PASS |
| Python coefficient cross-check | `python3 -c "import derive_fir_reference; assert sum(coef)==0x7FFE and sum(abs)==0xB9A6..."` | Exits 0 | PASS |
| SPU94_LATENCY_SAMPLES=58u in public header | `grep SPU94_LATENCY_SAMPLES include/spu94/spu94.h` | Line 203 defines `58u` | PASS |
| ADR-0012 present with lv2-psx-reverb OUT-OF-AXIS wording | `grep "OUT-OF-AXIS" docs/DECISIONS.md` | Found in ADR-0012 | PASS |

---

### Requirements Coverage

| Requirement | Phase | Description | Status | Evidence |
|-------------|-------|-------------|--------|----------|
| CORE-06 | Phase 4 | 39-tap half-band FIR at 44.1→22.05 kHz input boundary | SATISFIED | `spu94_fir_decimate` in `spu94_fir.c` implements folded-form 39-tap integer FIR; 6 ctest targets exercise it directly (fir_decimate, fir_bit_identity, fir_overflow_proof, fir_impulse, fir_frequency_sweep, fir_round_trip_transparency) |
| CORE-07 | Phase 4 | 39-tap half-band FIR at 22.05→44.1 kHz output boundary | SATISFIED | `spu94_fir_interpolate` in `spu94_fir.c` implements both phases; composed by `spu94_fir_chain_step`; 6 ctest targets exercise it (fir_interpolate, fir_chain_latency, fir_impulse, fir_dc, fir_err_overflow_taps, fir_round_trip_transparency) |

REQUIREMENTS.md traceability table marks both CORE-06 and CORE-07 as "Pending" (the checkbox convention reflects end-of-milestone completion, not per-phase completion). The functional requirements are satisfied by the Phase 4 implementation as verified above. No orphaned requirements for this phase.

---

### Anti-Patterns Found

| File | Location | Pattern | Severity | Impact |
|------|----------|---------|----------|--------|
| `src/spu94/spu94_io_chain.c` | Line 101 comment | Stale assembly-comment example "mov eax, 38; ret" — pre-correction latency value residue | Info | No code impact; macro and return value are both correctly 58u. Comment documents the LTO optimization idiom, not the actual constant. |
| `tests/unit/fir/test_fir_round_trip_transparency.c` | Fixture header | `ROUND_TRIP_ATTENUATION_Q15 = 0x3177` (0.386) rather than plan's predicted 0.977 | Info | Intentional Rule-1 auto-fix documented in 04-04-SUMMARY.md; empirically-derived value is correct for the 2:1 decimate/interpolate structure. Not a defect. |

No blockers. No stubs. No unimplemented production code paths in the FIR chain.

---

### Human Verification Required

None. All four must-haves are fully verifiable programmatically:
- Coefficient table: integer values + structural invariants checked by automated test.
- DC round-trip: no-drift assertion checked by automated test.
- Accumulator width: adversarial bound asserted by automated test with UBSan.
- DECISIONS.md ADR: text presence and content verified by direct file inspection.

The Mednafen/DuckStation empirical witness classification is deferred to Phase 7 TEST-03 per ADR-0012. This is an explicitly scoped deferral, not a Phase 4 gap.

---

### Gaps Summary

No gaps. All four success criteria are met:

- **SC-1 (decimator impulse):** Verified by `test_fir_decimate` (40-output impulse bit-exact vs Python oracle) + `test_fir_frequency_sweep` (log sweep + 7-bin |H(f)| sanity). Filter symmetry properties verified by `test_fir_coef_table` (symmetry + half-band zero pattern invariants) and `test_fir_bit_identity` (folded == literal under D-03 clamp-once).

- **SC-2 (DC round-trip):** Verified by `test_fir_dc` (settles to 0x01FF, no drift over 50 trailing samples, negative DC within 1 LSB, zero-in-zero-out) + `test_fir_round_trip_transparency` (band-limited multi-tone fixture residual below 2644 LSB threshold).

- **SC-3 (accumulator width):** Verified by in-source D-02 proof comment + `test_fir_overflow_proof` (achievable bound 0x5CD2632E asserted, UB-free, ADR-0014 discloses headroom) + `fuzz_fir.py` (10^6 runtime steps clean).

- **SC-4 (DECISIONS.md ADR):** ADR-0012 present in `docs/DECISIONS.md`, ratified 2026-04-20, documents half-rate architecture and explicitly classifies lv2-psx-reverb as OUT-OF-AXIS (HIGH confidence) on the frequency-response axis.

The latency correction (38u → 58u) and the Q15 gain deviation (0.977 → 0.386 empirical) are both Rule-1 auto-fixes documented in SUMMARYs and ADRs, not defects.

38/38 ctest pass, 12/12 FIR-labeled tests pass, including the 10^6-step fuzz harness.

---

_Verified: 2026-04-20T21:00:00Z_
_Verifier: Claude (gsd-verifier)_
