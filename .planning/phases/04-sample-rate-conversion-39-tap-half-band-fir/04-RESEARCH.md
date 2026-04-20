# Phase 4: Sample Rate Conversion (39-tap half-band FIR) — Research

**Researched:** 2026-04-20
**Domain:** PS1 SPU I/O-boundary 44.1 ↔ 22.05 kHz sample-rate conversion via a 39-tap symmetric half-band FIR; Q15 integer accumulator-width proof; bit-identity between folded and literal FIR arithmetic; empirical witness classification (lv2-psx-reverb / Mednafen / DuckStation) on the frequency-response axis.
**Confidence:** HIGH on the coefficient values (consistent across the published sources surveyed; identical hex table in every source that publishes the values). HIGH on the half-band structure and symmetry (verified by direct computation — see § Coefficient table). HIGH on lv2-psx-reverb's OUT-OF-AXIS classification (the project's own README states it does not downsample to 22050 Hz). HIGH on the 22.05 kHz reverb rate + L/R time-multiplex (primary nocash fact, reconfirmed by jsgroth). HIGH on the accumulator-width proof — derived by direct summation, margin is **2.79 dB (0.46 bits)** for the decimator and **6.46 dB (1.08 bits)** for the interpolator phase-0 subfilter (SAFE in int32 but the decimator is tight enough to warrant flagging the D-02 int64 seam). MEDIUM on the number of **truly independent** coefficient sources — see § Coefficient provenance audit: in practice every published source traces to the same SCPH-5501 hardware readout. MEDIUM on Mednafen / DuckStation FIR-implementation status — empirical audio capture was not executed in this research pass; an exact protocol is specified for the Phase 4 execution pass to run.

## Summary

The PS1 SPU reverb is a 22.05 kHz engine wrapped by a 39-tap half-band FIR at each I/O boundary. The coefficient table is available in published sources in a single consistent form — a symmetric half-band integer table with center tap 0x4000 (= 16384 = 0.5 in Q15), zeros at every odd distance from center, and eleven distinct coefficient magnitudes (including the center). The table's primary published home is the psx-spx community-maintained render (https://psx-spx.consoledev.net/soundprocessingunitspu/ § Reverb Buffer Resampling), which in turn attributes the values to a hardware readout of a SCPH-5501 console originally posted to forums.bannister.org. **Every published source this research surveyed traces back to that single SCPH-5501 readout.** This collapses what CONTEXT.md D-10 framed as a three-source cross-reference into a one-source-with-multiple-mirrors situation, which the research surfaces as a finding the planner and user need to reconcile before the plan locks.

The accumulator-width question resolves cleanly in int32: worst-case pre-shift accumulator magnitude for the decimator is 1,557,331,968 = 0x5CD30000 (31 bits of int32 used, 2.79 dB / 0.46 bits of headroom). The interpolator's phase-0 subfilter is more relaxed at 1,020,461,056 = 0x3CD30000 (30 bits, 6.46 dB headroom). The decimator is **safe but tight** — both paths stay inside int32 under arbitrary adversarial int16 input, but the decimator consumes all but one high bit of an int32. The D-02 seam to int64 is warranted as a hedge if any future seam composition (for example, a future `err_fir` accumulator aggregated over N ticks, or a change to Q30 coefficients, or cascading clamp + inner re-accumulate) would tighten the margin further. For Phase 4 as scoped, int32 is correct.

Folded-form bit-identity holds exactly under the D-03 clamp-once regime: direct algebraic distribution of the symmetric-pair sum produces the identical pre-shift accumulator across all inputs. Verified empirically by 100 random trials plus adversarial worst-case input — bit-for-bit equality. Under the D-04 cascade-clamp variant, bit-identity breaks by construction (demonstrated: identical input produces literal = −12, folded = −7 under cascading saturation). The bit-identity test must therefore be compiled out when the D-04 seam is engaged; this is a planner task to document in the cascade-clamp ADR.

lv2-psx-reverb is a confirmed OUT-OF-AXIS witness on the frequency-response axis: the project's own README states it "doesn't downsample the reverb to 22050 Hz" and that this produces "additional brightness of the higher frequencies." Mednafen and DuckStation were NOT captured in this research pass. A reproducible empirical protocol is specified below so the Phase 4 execution pass can classify each as IN-AXIS or OUT-OF-AXIS using output audio alone (per the PROJECT.md licensing posture: their audio output is fair witness material; their GPL source is not a primary source).

**Primary recommendation:** Land Phase 4 plans around eight locked facts — the 39-coefficient table verbatim (§ Coefficient table), the decimator accumulator-width proof (§ Accumulator width proof), the interpolator proof (same section), the folded-equals-literal bit-identity argument under clamp-once (§ Folded-vs-literal bit-identity), the 38-sample total latency at 44.1 kHz (19 decimator + 19 interpolator; § Latency), lv2-psx-reverb's OUT-OF-AXIS status (§ Witness analysis), circular-buffer delay-line recommendation (§ Circular-buffer vs shift-register), and the complete test-vector library (§ Test-vector library). Flag three items for user confirmation before the plan locks: (1) the coefficient-source provenance audit finding that the three-source cross-reference collapses to one underlying hardware readout — D-10 is still honored as "three citations in bibliography" but the planner should know they are mirrors, (2) the decimator accumulator margin is tight enough (0.46 bits) to warrant the D-02 int64 seam note in the accumulator-width ADR, (3) Mednafen / DuckStation classification deferred to the Phase 4 execution pass using the protocol specified here.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Area A — FIR arithmetic & precision**
- **D-01:** FIR math uses the folded-multiply form (~10 multiplies per output sample) with the literal 39-tap coefficient table stored verbatim and used as an audit reference. Best-guess-1994-silicon authenticity. Verified bit-identical to a literal 39-multiply reference implementation via a bit-identity equivalence test. The literal form never ships in production code paths but remains permanently compiled in the test binary as an audit witness.
- **D-02:** FIR accumulator is `int32_t` with a derived-from-coefficients no-overflow proof. The proof lives in a comment block adjacent to the accumulator declaration, backed by a worst-case-inputs test case. MCU-friendly (no 64-bit adds on Cortex-M). If the proof's margin narrows uncomfortably, the seam allows promotion to `int64_t` without touching callers.
- **D-03:** Default clamp policy is "clamp once at final stage output." All 39 weighted products sum in the int32 accumulator; the final right-shift + `sat_s16` at the output stage is the only saturation point inside the FIR. Diverges from Phase 3 D-07's cascade-clamp choice — deliberately, because Phase 3's comb is a character stage and Phase 4's FIR is not.
- **D-04:** Opt-in cascade-clamp seam. The FIR stages ship a policy-variant seam that flips from clamp-once to cascade-clamp after every add. Default: clamp-once. M4 plugin toggle: cascade-clamp = "FIR boundary adds distortion character on top of reverb character."
- **D-05:** Overflow-magnitude tap on the FIR clamp is always on. Feeds `state->fir_overflow_decimator` / `state->fir_overflow_interpolator` (int32 fields). Zero conditional logic on the hot path.
- **D-06:** Per-multiply err-tap parity with Phase 3. Every one of the ~10 folded multiplies calls `q15_mul_truncate_with_err` and feeds its truncation remainder into per-boundary int32 accumulators in `spu94_state` (`err_fir_decimator`, `err_fir_interpolator`).

**Area B — Pipeline integration**
- **D-07:** Phase 4 ships an internal 44.1 kHz per-sample wrapper, not a public one. A new internal function (name at planner's discretion) lives in a src-only header and chains `spu94_fir_decimate` → `spu94_tick` → `spu94_fir_interpolate`. Phase 5's public `spu94_process` composes this internal wrapper into block-based processing.
- **D-08:** Separate per-channel FIR state. Two independent 39-sample int16 delay-line buffers (`fir_delay_l[39]`, `fir_delay_r[39]`) per direction (decimator input side, interpolator input side), each with its own circular-buffer index. Coefficient table is shared (one `static const int16_t fir_coef[39]`).
- **D-09:** Latency contract is documented AND programmatically exposed. `spu94_get_latency_samples(void)` returns 38 samples = 19 decimator + 19 interpolator, at the 44.1 kHz reference rate.

**Area C — Coefficient provenance & ADR scope**
- **D-10:** Coefficient table sourced via three-source cross-reference. Phase 4 research pulls the 39 coefficient values from (a) jsgroth's "PlayStation: The SPU, Part 3 — Reverb" writeup, (b) the bannister.org forum thread where coefficients were read from SCPH-5501 hardware, and (c) at least one additional independent source. Byte-for-byte comparison across all three; any disagreement flagged and resolved with a documented rationale. Nocash is NOT a source for the coefficients per the 2026-04-20 CONTEXT note. All three sources cited in `docs/BIBLIOGRAPHY.md`.
- **D-11:** Coefficient storage is Q15 native `int16_t`, verbatim from verified sources, in a dedicated `.c` translation unit (not a header). One coefficient per line with tap-index comment.
- **D-12:** Coefficient transcription is facts-only. Integer literal values only; no prose, tables, or commentary copied from nocash/jsgroth/bannister/etc. Matches PROJECT.md licensing posture.
- **D-13:** Phase 4 produces ADRs at maximum granularity. Every distinct locked decision above gets its own numbered ADR, plus the SC-4-mandated half-rate architecture / lv2-psx-reverb-frequency-axis-exclusion ADR. ~8–12 ADRs.

**Area D — Witness scope & test-vector strategy**
- **D-14:** Mednafen and DuckStation FIR-implementation status is investigated empirically in Phase 4 research. Output audio only, no source reading. lv2-psx-reverb is definitively excluded from the frequency-response witness axis.
- **D-15:** Test vectors go beyond the SC-1/2/3 minimum — impulse response, DC round-trip, worst-case overflow proof, frequency sweep, round-trip transparency.
- **D-16:** Python ctypes fuzz harness for the FIR (`tests/python/fuzz_fir.py`, 10⁶ random inputs, invariant checks).

**Architectural Principles (carried from Phases 1–3)**
- **D-22** Extensibility Seams · **D-23** Observability · **D-24** Controllers as Future Consumer.
- **ADR-0001** (Q15 truncation direction toward −∞) — every Phase 4 multiply uses `q15_mul_truncate_with_err`.
- **Phase 3 D-07 / D-11 precedents explicitly diverged and extended.**

### Claude's Discretion

- Exact C prototypes and names of the internal FIR functions (`spu94_fir_decimate_step`? `spu94_fir_push_and_decimate`? etc.)
- Exact name of the internal 44.1 kHz wrapper (`spu94_sample_44k1`? `spu94_fir_chain_step`? `spu94_io_step`?)
- Circular buffer index vs shift register for the 39-sample delay line (both are bit-identical; recommended default is circular-buffer — but planner decides).
- Whether the cascade-clamp seam (D-04) is a compile-time `#ifdef` switch, a runtime function pointer, or a direct caller-selectable variant.
- Split vs merger of ADRs (D-13 says max granularity; planner decides).
- Exact threshold for round-trip transparency — planner derives from test-vector analysis.
- Test-file granularity under `tests/unit/fir/`.
- Whether `spu94_get_latency_samples()` is `static inline` + constant `#define` or a `.c`-defined function returning a constant.

### Deferred Ideas (OUT OF SCOPE)

- `spu94_process` public block entry point — Phase 5.
- 10 factory reverb presets — Phase 5.
- Python ctypes bindings / wheel — Phase 6.
- Witness-diff harness regression infrastructure — Phase 7.
- Golden-file regression snapshots — Phase 7.
- MCU cross-compile validation — Phase 8.
- FIR-bypass toggle (aliasing on/off as musical choice) — Milestone 4.
- Cascade-clamp toggle as a user-facing character switch — Milestone 4.
- Hardware capture arbitration of clamp policy, folded form, coefficient values — Milestone 5.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CORE-06 | 39-tap half-band FIR at the 44.1→22.05 kHz input boundary — documented coefficients verbatim | § Coefficient table provides the 39 int16 values, verified half-band-symmetric and matching the structure documented by the published sources. § Accumulator width proof derives the decimator's int32 headroom. § Folded-vs-literal bit-identity validates the folded form's bit-exactness vs the literal 39-multiply form (which is preserved as an audit witness per D-01). |
| CORE-07 | 39-tap half-band FIR at the 22.05→44.1 kHz output boundary — documented coefficients verbatim | Same coefficient table (shared), used symmetrically per D-08. § Accumulator width proof covers the interpolator phase-0 subfilter (the more tightly-loaded phase) and phase-1 (center-tap passthrough). § Latency documents the 19-sample interpolator contribution of the 38-sample round-trip. |

</phase_requirements>

## Project Constraints (from CLAUDE.md / PROJECT.md)

Extracted and honored:

1. **User execution style (global CLAUDE.md):** user is the hands-on operator. Phase 4 is pure-code-in-repo — no deployed systems. The hands-on-walkthrough directive is a no-op. The planner should structure each plan so the user can execute and verify (compile, run tests, inspect failures) independently.
2. **No heap in core.** Phase 4 adds no heap symbols. All FIR state (delay lines, err accumulators, overflow taps, indices) lives in `struct spu94_state`. Verified-no-heap-symbols CI unchanged.
3. **No float/double in core.** Enforced by the Phase 1 grep guard. All FIR math is `int16_t` / `int32_t` via `q15_mul_truncate_with_err` / `sat_s16` / `q15_add_sat`.
4. **No unqualified `long`.** Use `<stdint.h>` widths.
5. **Bit-faithful from spec, not from port.** Primary source: the published coefficient table (see § Coefficient provenance audit). Mednafen / DuckStation / lv2-psx-reverb source code is NOT read as a primary research input. Their **output audio** is the witness material (§ Witness analysis).
6. **nocash / coefficient-publisher paraphrase discipline.** The 39 integer values are uncopyrightable facts and are transcribed as numbers (per D-12). Any prose surrounding them in the published sources is paraphrased in SPU-94's own words; no cells or tables are copied.
7. **C99/C11 freestanding conformance.** Phase 4 adds one new public symbol (`spu94_get_latency_samples`); the rest lives in internal headers.
8. **Determinism flags in force.** `-ffp-contract=off`, `-fno-fast-math`, `-Werror` inherit via `spu94_warnings` INTERFACE target.
9. **UBSan + `no_sanitize("integer")` policy (ADR-0003).** The FIR is integer-only by construction and uses saturation everywhere; no new `no_sanitize` attributes anticipated.
10. **Epistemic honesty (user feedback).** This research explicitly flags what is primary-source-locked vs witness-corroborated vs SPU-94-design-choice, and surfaces the D-10 provenance-audit finding honestly rather than claiming three independent sources exist when in fact they are mirrors.
11. **Announce official writes (user feedback).** The planner, when it lands the Phase-4 ADRs in `docs/DECISIONS.md`, must announce intent before editing the durable artifact.
12. **Plain-language + short responses (user feedback).** This research document is dense by necessity but structures each load-bearing section with a TL;DR first line; long prose is only where the argument requires it.

---

## Standard Stack

Phase 4 is pure fixed-point integer DSP in C. **There is no external library that should be added in this phase.** All primitives were landed by Phases 1–3.

### Core (already landed — reused)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `spu94_q15.h` Q15 helpers | Phase 1 (vendored in-repo) | `q15_mul_truncate`, `q15_mul_truncate_with_err`, `sat_s16`, `q15_add_sat` | [VERIFIED: `include/spu94/spu94_q15.h`, ADR-0001, ADR-0004] Already implements the ADR-0001 truncation-toward-−∞ semantic and the `_with_err` pre-saturation remainder tap (ADR-0004). Phase 4 uses `q15_mul_truncate_with_err` for every folded multiply per D-06. |
| `spu94_state_internal.h` | Phase 2 | Single ODR home for `struct spu94_state` | [VERIFIED: `src/spu94/spu94_state_internal.h`] Phase 4 adds per-direction `int16_t fir_delay_*[39]` delay lines + indices + `int32_t err_fir_*` / `fir_overflow_*` accumulator fields. Current `sizeof(struct spu94_state) == 168 B` with 16216 B headroom to `SPU94_STATE_SIZE_MAX`; Phase 4 additions (~320 B of int16 delay-line storage + ~32 B of indices/accumulators) fit comfortably. |
| `spu94_tick()` | Phase 2 (body from Phase 3) | The 22.05 kHz-rate reverb entry point | [VERIFIED: `src/spu94/spu94_tick.c`] Phase 4 does NOT modify `spu94_tick`. The new internal 44.1 kHz wrapper calls `spu94_tick` as the middle step of its chain (D-07). |
| `spu94_registers.h` + facade | Phase 2 | Register I/O surface | [VERIFIED: `include/spu94/spu94_register_facade.h`] Phase 4 does not touch register semantics. The FIR state lives on the SAME `struct spu94_state` instance but on distinct fields; register writes and FIR state are orthogonal. |
| Unity C test framework | Phase 1 (vendored) | Per-TU unit tests with inline reference tables | [VERIFIED: Phase 1 01-02-PLAN] Phase 4's FIR tests follow the inline-reference-table pattern. |
| Python 3.10+ (ctypes) | Phase 2 Plan 05 (`fuzz_buffer.py`), Phase 3 Plan 04 (`fuzz_reverb.py`) | `tests/python/fuzz_fir.py` — 10⁶ random input samples (D-16) | [VERIFIED: Phase 3 pattern] Independent Python FIR reference model cross-checks the C core; divergence in either direction fails the test. |
| CMake `spu94_obj` OBJECT library | Phase 1 | Flag-identical build of `.so` and `.a` | [VERIFIED: `src/spu94/CMakeLists.txt`] New `src/spu94/spu94_fir.c` + `src/spu94/spu94_fir_coef.c` + (optionally) `src/spu94/spu94_io_chain.c` TUs auto-picked up by existing source globs / explicit list. |
| CI grep guard / verify-no-heap / clang-tidy / cppcheck / UBSan | Phase 1 | Determinism + posture enforcement | [VERIFIED: `scripts/ci/*`] Phase 4 code passes all unchanged. |

### Core (new in Phase 4)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| None (zero new runtime deps) | — | — | [VERIFIED: CONTEXT Decisions + PROJECT.md "no external DSP libs"] Phase 4 is hand-written integer DSP. The FIR stages + coefficient table + delay lines + internal 44.1 kHz wrapper are all implemented with the Phase 1/2/3 primitives. No third-party C code needed. |

### Supporting (test-side only, non-shipped)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Python 3.10+ (ctypes + numpy optional) | Phase 2 Plan 05 / Phase 3 Plan 04 precedent | `tests/python/fuzz_fir.py` (D-16) + `tests/python/derive_fir_reference.py` (hand-derived impulse / DC / sweep references, independent of any C emulator) | [VERIFIED: precedent] Replicates `fuzz_reverb.py`'s shape. numpy is optional but recommended for the frequency-sweep test which benefits from `numpy.fft` to validate the half-band lowpass transition-band shape. Pure-Python-integer for the unit-impulse / DC / worst-case-overflow derivations; numpy only for FFT-based validation. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Hand-written integer FIR | CMSIS-DSP Q15 FIR primitives (ARM) | CMSIS-DSP is Apache-2.0 — license-compatible, BUT: (a) targets Cortex-M SIMD; (b) its saturation / truncation / rounding semantics differ from the SPU's truncation-toward-−∞ convention (ADR-0001); (c) introduces a toolchain-conditional dependency that contaminates the MCU-portable claim. **Reject.** Hand-rolled Q15 FIR in `src/spu94/spu94_fir.c` matches spec exactly. |
| Polyphase decomposition of the half-band filter | Direct FIR with zero-skipping | Polyphase decomposition gives a 2× speedup on decimation (only compute outputs at retained phase) and on interpolation (two sub-filters, one computes the passthrough, one computes the interpolated output). BUT: polyphase decomposition rearranges arithmetic in a way that makes the bit-identity-to-literal-39-multiply argument (D-01) harder to audit. **Defer to M4 or M8 optimization pass.** Phase 4 ships the folded-but-not-polyphase form that preserves the bit-identity audit. |
| Folded form (10–11 multiplies) | Literal 39-multiply form | Folded form is half the multiplies (exploits symmetry). Bit-identical to literal form under D-03 clamp-once (proof § Folded-vs-literal bit-identity). Literal form preserved as audit witness in test binary per D-01. **Keep folded as production, literal as test-only witness.** |
| Shift-register delay line | Circular-buffer delay line | Both produce bit-identical output. Shift register is trivially auditable but costs a 39-int16 memmove per input sample (78 bytes × 44.1 kHz = ~3.4 MB/s memory-copy traffic). Circular buffer has a per-sample cost of one write + one index increment + one AND-mask. On a Cortex-M7 with tight D-cache this matters. **Recommend circular buffer** for production (§ Circular-buffer vs shift-register). |
| `int32_t` accumulator (D-02) | `int64_t` accumulator | int32 has 0.46 bits of headroom on the decimator worst case — safe but tight. int64 would give room for composed future accumulators (e.g., multi-tick averages, finer err taps). Cost: `int64_t` adds on Cortex-M4 are 2–3 cycles instead of 1 — measurable in a 44.1 kHz inner loop. **Keep int32 for Phase 4 with the D-02 seam ready.** The D-02 ADR should document the 0.46-bit margin and make the int64 promotion path explicit so future composed seams don't blow the budget silently. |

**Installation:** none — no new packages.

**Version verification:** N/A — no new packages. Python 3.10+ confirmed by Phase 2/3 environment probes (`python3 --version` → `Python 3.13.7` locally).

---

## Architecture Patterns

### Recommended Project Structure (additions under existing dirs)

```
src/spu94/
├── spu94_fir.c                     # NEW. The two FIR stage functions
│                                   # (spu94_fir_decimate, spu94_fir_interpolate)
│                                   # + circular-buffer delay-line helpers.
│                                   # Folded-form (production) + literal-form
│                                   # (audit-witness, behind a test-only
│                                   # compile flag, or always present but
│                                   # not called from hot path).
├── spu94_fir_coef.c                # NEW. The 39 int16 coefficients verbatim.
│                                   # One coefficient per line with a tap-index
│                                   # comment. No prose, no citations in the
│                                   # file — the bibliography entry carries
│                                   # the citation. (D-11, D-12.)
├── spu94_fir_internal.h            # NEW. Src-only header. Declares the
│                                   # decimator/interpolator stage prototypes
│                                   # + the internal 44.1 kHz wrapper +
│                                   # (optionally) the circular-buffer helper
│                                   # inlines. Tests include directly. Not
│                                   # under include/spu94/.
└── spu94_io_chain.c                # OPTIONAL NEW. The internal 44.1 kHz
                                    # wrapper (D-07) — chains decimate →
                                    # tick → interpolate. Alternatively
                                    # lives inside spu94_fir.c. Planner
                                    # decides based on one-concern-per-TU
                                    # aesthetics.

include/spu94/
└── spu94.h                         # EXTEND. Add one new public symbol:
                                    # uint32_t spu94_get_latency_samples(void);
                                    # (D-09). Implementation in
                                    # spu94_io_chain.c or spu94_fir.c.

tests/unit/fir/                     # NEW directory.
├── test_fir_coef_table.c           # Verify the 39-coefficient table:
│                                   # length is 39; symmetric about index 19;
│                                   # center tap = 0x4000; zero-pattern
│                                   # matches half-band Type I (odd-offset
│                                   # from center zeros); SHA-256 sidecar to
│                                   # detect accidental table edits.
├── test_fir_decimate.c             # Per-stage unit tests for decimator.
├── test_fir_interpolate.c          # Per-stage unit tests for interpolator.
├── test_fir_bit_identity.c         # Folded-form vs literal-39-multiply
│                                   # form: bit-for-bit across 10^5 random
│                                   # inputs + adversarial worst-case input.
│                                   # (D-01 audit witness test.)
├── test_fir_overflow_proof.c       # Worst-case adversarial input drives
│                                   # accumulator to 0x5CD30000 (decimator)
│                                   # and 0x3CD30000 (interpolator phase-0);
│                                   # asserts no int32 overflow and validates
│                                   # the § Accumulator width proof bounds.
├── test_fir_impulse.c              # Unit impulse in; 39-tap half-band
│                                   # impulse response out; center-latency
│                                   # peak check (ASPU center at 19 samples).
├── test_fir_dc.c                   # DC signal in; DC-exact out (no bias, no
│                                   # drift); L and R symmetry to the last
│                                   # bit.
├── test_fir_freq_sweep.c           # Sine sweep 20 Hz → 22 kHz;
│                                   # assert passband flat to ~10 kHz;
│                                   # transition through 11.025 kHz; stopband
│                                   # floor below Phase-4-chosen dB target.
│                                   # (FFT assertion driven in numpy in the
│                                   # Python fuzz harness; this C TU carries
│                                   # the spot-check at fixed points.)
├── test_fir_round_trip.c           # Band-limited input (no content > 10 kHz)
│                                   # through decimate → interpolate (reverb
│                                   # bypassed via per-tick zero-state trick
│                                   # — see § Reverb-bypass hook for testing);
│                                   # residual error vs delayed-by-38-samples
│                                   # original < planner-chosen threshold.
├── test_fir_latency.c              # Feed unit impulse at t=0 through the
│                                   # full internal 44.1 kHz wrapper; assert
│                                   # peak-output index == 38; assert
│                                   # spu94_get_latency_samples() == 38.
└── test_fir_err_overflow_taps.c    # Invariants on err_fir_decimator /
                                    # err_fir_interpolator /
                                    # fir_overflow_decimator /
                                    # fir_overflow_interpolator: zero for
                                    # non-truncating / non-saturating input;
                                    # monotonic-nondecreasing under stress;
                                    # matches the Python reference remainder
                                    # computation bit-for-bit.

tests/python/
├── derive_fir_reference.py         # Hand-derived expected outputs for the
│                                   # impulse, DC, worst-case-overflow,
│                                   # and bit-identity tests (pure-Python
│                                   # integer; independent of any C
│                                   # emulator). Follows Phase 3's
│                                   # derive_reverb_reference.py pattern.
└── fuzz_fir.py                     # 10^6 random-input fuzz harness
                                    # (D-16). Invariants: bounded output,
                                    # no buffer corruption, latency ==
                                    # spu94_get_latency_samples(), err/
                                    # overflow fields within expected
                                    # ranges. Follows fuzz_reverb.py's
                                    # pattern.

docs/
├── DECISIONS.md                    # Phase 4 APPENDS (prepended at top per
│                                   # established style): ~8–12 ADRs —
│                                   # planner decides split per D-13. At
│                                   # minimum: half-rate architecture + lv2-
│                                   # psx-reverb exclusion (SC-4 mandate),
│                                   # FIR math form (D-01), accumulator
│                                   # width + int64 seam (D-02), clamp-once
│                                   # policy + cascade-clamp seam (D-03 +
│                                   # D-04), overflow-magnitude tap (D-05),
│                                   # err-tap scope (D-06), internal wrapper
│                                   # shape (D-07), per-channel state (D-08),
│                                   # latency contract (D-09), coefficient
│                                   # sourcing + bibliography (D-10), witness
│                                   # empirics (D-14).
└── BIBLIOGRAPHY.md                 # NEW FILE (does not exist yet in this
                                    # repo — previous ADRs reference it as
                                    # a placeholder). Phase 4 creates it
                                    # and adds the coefficient-source
                                    # entries (see § Bibliography additions).
```

### Pattern 1: Two FIR stage functions with shared coefficient table

**What:** `spu94_fir_decimate` and `spu94_fir_interpolate` are the two public-inside-Phase-4 FIR stage functions, declared in `spu94_fir_internal.h`. Both read the shared `static const int16_t fir_coef[39]` table defined in `spu94_fir_coef.c`. Each takes the `spu94_state *` (for the per-channel delay-line state + err-tap + overflow-tap fields) plus input/output samples. L and R are handled in independent calls per D-08.

**When to use:** The decimator is called twice per 44.1 kHz host sample (once per channel) at the input boundary; the interpolator is called twice per 22.05 kHz `spu94_tick` (once per channel; produces two output samples per call for the 2× upsampling) at the output boundary.

**Example (folded-form decimator sketch):**

```c
// Source: paraphrased from the published coefficient table in
// § Coefficient table of this research document; folded-form derivation is
// SPU-94's own work.
// Source (licensing): 39 int16 values are uncopyrightable facts. Prose
// around them in any source is NOT transcribed. See docs/BIBLIOGRAPHY.md
// entry BIB-005 for the coefficient-source citations (D-10).

// File: src/spu94/spu94_fir_coef.c
//
// 39-tap half-band FIR coefficients for the PS1 SPU reverb resampler.
// See docs/BIBLIOGRAPHY.md BIB-005, BIB-006, BIB-007 for source citations.
// Values are Q15 native int16. Table is symmetric about index 19 (center).
// Odd-offset-from-center entries are zero (half-band Type I structure).
// SHA-256 of this table is pinned in tests/unit/fir/test_fir_coef_table.c.

#include <stdint.h>
#include "spu94_fir_internal.h"

const int16_t spu94_fir_coef[39] = {
    -0x0001,  //  0
     0x0000,  //  1
     0x0002,  //  2
     0x0000,  //  3
    -0x000A,  //  4
     0x0000,  //  5
     0x0023,  //  6
     0x0000,  //  7
    -0x0067,  //  8
     0x0000,  //  9
     0x010A,  // 10
     0x0000,  // 11
    -0x0268,  // 12
     0x0000,  // 13
     0x0534,  // 14
     0x0000,  // 15
    -0x0B90,  // 16
     0x0000,  // 17
     0x2806,  // 18
     0x4000,  // 19   <-- center tap, Q15 0.5
     0x2806,  // 20
     0x0000,  // 21
    -0x0B90,  // 22
     0x0000,  // 23
     0x0534,  // 24
     0x0000,  // 25
    -0x0268,  // 26
     0x0000,  // 27
     0x010A,  // 28
     0x0000,  // 29
    -0x0067,  // 30
     0x0000,  // 31
     0x0023,  // 32
     0x0000,  // 33
    -0x000A,  // 34
     0x0000,  // 35
     0x0002,  // 36
     0x0000,  // 37
    -0x0001,  // 38
};
_Static_assert(sizeof(spu94_fir_coef) / sizeof(spu94_fir_coef[0]) == 39,
               "FIR coefficient table must be exactly 39 entries");
```

```c
// File: src/spu94/spu94_fir.c  (folded-form decimator sketch)
//
// Clamp-once policy (D-03). Folded-form math (D-01) with literal-form audit
// witness compiled separately in test_fir_bit_identity.c.
// Every multiply uses q15_mul_truncate_with_err per D-06.
// Overflow-magnitude tap on the final sat_s16 per D-05.

static int16_t spu94_fir_decimate_fold(
    const int16_t delay[39],   // circular view: delay[0]=newest, delay[38]=oldest
    int32_t *err_out,          // D-06 err-tap accumulator (one per channel)
    int32_t *overflow_out)     // D-05 overflow-magnitude tap
{
    // Folded form: sum symmetric pairs before multiplying.
    // For symmetric h[k] = h[38-k], distributive law gives:
    //   Σ h[k] * x[n-k] for k in [0,38]
    // = h[19] * x[n-19] + Σ h[k] * (x[n-k] + x[n-(38-k)]) for k in [0,18]
    // which exactly matches the literal 39-multiply form (proof in
    // § Folded-vs-literal bit-identity of this research document).
    //
    // Ten non-zero folded multiplies + one center multiply = 11 total.
    // Zeros are skipped statically — this is the D-01 "~10 multiplies" form.

    // Pre-compute the 38-sample-latency positions once; these are the
    // reverse-index positions into the circular delay line.
    // x_mid = delay[19] (center tap of the delay line = x[n-19])
    // x_k / x_Nk (k=0..18) read from delay[k] / delay[38-k].

    // Pair sums are computed in int32 to avoid the intermediate int16
    // overflow that could happen on two-INT16_MAX operands. Each element
    // of the sum is int16; the pair-sum is int17, fits in int32.
    // Multiplication: int16 coefficient * int32 pair-sum -> int32 product.
    // (Note: this pre-shift product can be up to |coef| * 2 * INT16_MAX,
    // which still fits in int32 for all non-center coefs — max is
    // |0x2806| * 2 * 0x7FFF = 0x27FE_B3FA, comfortably inside int32.)

    int32_t acc = 0;

    // Center tap: multiply once.
    acc += spu94_fir_coef[19] * (int32_t)delay[19];

    // Nineteen pair sums. Zero-coefficient pairs skipped in the unrolled form
    // (planner decides: loop with `if (spu94_fir_coef[k] == 0) continue;` OR
    // fully-unrolled with the zero entries elided — both give bit-identical
    // results; the unrolled form is more auditable, the loop is more
    // compact. Planner's call; bit-identity to the literal form is preserved
    // in either form because integer * 0 == 0).
    for (int k = 0; k < 19; ++k) {
        const int16_t c = spu94_fir_coef[k];
        if (c == 0) continue;
        const int32_t pair = (int32_t)delay[k] + (int32_t)delay[38 - k];
        acc += c * pair;
    }

    // Single right-shift by 15 (Q15 to int range) + single sat_s16 to
    // int16 output. Clamp-once per D-03.
    // The shift direction is arithmetic (toward -infinity) per ADR-0001;
    // this is compile-time-asserted in spu94_q15.h.
    int32_t shifted = acc >> 15;

    // Overflow-magnitude tap (D-05): how far outside int16 range is the
    // shifted accumulator? Zero when it fits.
    int32_t magnitude = 0;
    if (shifted >  INT16_MAX) magnitude = shifted - INT16_MAX;
    else if (shifted < INT16_MIN) magnitude = INT16_MIN - shifted;
    *overflow_out += magnitude;  // accumulate per-call; zero when no saturation

    // sat_s16 returns int16; we return it.
    int16_t out = sat_s16(shifted);

    // Per-multiply err-tap (D-06): the q15_mul_truncate_with_err helper
    // captures the shift-truncation remainder on each per-multiply call.
    // In this folded form, we want the err-tap at the MULTIPLY level,
    // not at the single-final-shift level. One option: do the multiplies
    // individually with q15_mul_truncate_with_err and accumulate the
    // per-multiply shifted results into acc as int16s (then sum), at the
    // cost of changing the clamp-once regime. Second option (simpler, and
    // bit-identical to clamp-once): compute the full int32 accumulator as
    // above, then compute the aggregate err in one shot as
    // (acc - (shifted << 15)) — this is the pre-shift remainder of the
    // single final shift. Second option is what fits D-03 + D-06 together.
    *err_out += (acc - ((int32_t)shifted << 15));

    return out;
}
```

Note the tension between D-06 (per-multiply err-tap) and D-03 (clamp-once):
- Reading D-06 strictly, "Every one of the ~10 folded multiplies calls `q15_mul_truncate_with_err`" implies per-multiply shift-and-saturate — but that is cascade-clamp, which is D-04's seam-opt-in, not the default.
- Reading D-06 intent-ally (its purpose is "maximum M4 raw material — Phase 3's precision-loss surface extended to the FIR boundary"), the aggregate err tap above captures the same discarded bits at lower per-call cost.
- **Recommendation:** the aggregate err-tap (one per direction, as in the sketch) is the bit-faithful interpretation under D-03. If the planner prefers per-multiply err at the cost of diverging from clamp-once, that is effectively engaging D-04's cascade-clamp seam and should be called out as such in the ADR.
- **This is a Claude-discretion reconciliation point** — the planner picks the reading and documents it in the ADR for D-03/D-04/D-06 (a combined ADR or three separate ones).

### Pattern 2: Circular-buffer delay line per channel

**What:** Each of the four delay lines (decimator L, decimator R, interpolator L, interpolator R) is a `int16_t ring[39]` with an `unsigned index` (mod 39) pointing at the oldest slot. A write operation places the new sample at `ring[index]` and advances `index = (index + 1) % 39`. A read at "logical position k samples ago" is `ring[(index + k) % 39]` (or equivalent — planner's exact convention).

**When to use:** Production FIR path. Both stages. No memmove cost, no shift-register overhead, MCU-friendly. See § Circular-buffer vs shift-register for the full rationale.

**Example:**

```c
// Inside src/spu94/spu94_fir.c, static inline helpers:

static inline void spu94_fir_delay_push(int16_t ring[39], uint8_t *index,
                                        int16_t sample)
{
    ring[*index] = sample;
    *index = (*index + 1) % 39;
}

// Read at logical position k (0 = newest, 38 = oldest).
static inline int16_t spu94_fir_delay_at(const int16_t ring[39],
                                         uint8_t index, unsigned k)
{
    // newest is at (index - 1) mod 39; the "k samples ago" is
    // (index - 1 - k) mod 39 = (index + 38 - k) mod 39.
    unsigned pos = (index + 38u - k) % 39u;
    return ring[pos];
}
```

Planner may swap the convention (e.g., use 64-entry power-of-two ring with `& 0x3F` mask at the cost of wasted entries; or use an index increment that pre-decrements instead of post-increments) — any convention is fine as long as the reference model in `tests/python/derive_fir_reference.py` matches and the bit-identity test passes.

### Pattern 3: Internal 44.1 kHz chain wrapper

**What:** The per-44.1-kHz-sample function, internal only. Takes a stereo int16 input, calls `spu94_fir_decimate` for L and R, feeds the resulting 22.05 kHz pair into `spu94_tick`, then calls `spu94_fir_interpolate` for L and R to produce two 44.1 kHz output samples per call. Lives in `spu94_io_chain.c` or `spu94_fir.c` (planner decides).

**When to use:** The building block Phase 5's public `spu94_process` composes into block-based processing. Never exposed on a public header.

**Example sketch (names at planner's discretion):**

```c
// File: src/spu94/spu94_io_chain.c  OR  bottom of src/spu94/spu94_fir.c
//
// Phase 4 internal 44.1 kHz wrapper. Not in include/spu94/.
// Phase 5's public spu94_process() composes this in a block-based loop.

void spu94_fir_chain_step(spu94_state *state,
                          int16_t L_in_44k1,  int16_t R_in_44k1,
                          int16_t *L_out_44k1, int16_t *R_out_44k1)
{
    // DECIMATE side: push each 44.1 kHz input sample into the per-channel
    // delay lines. The FIR produces ONE 22.05 kHz output every two pushes;
    // half the calls the FIR output is discarded (the non-retained phase).
    // Tick counter state->fir_decimate_phase alternates 0/1.
    //
    // (Implementation detail: the decimator can compute the FIR output on
    // every push and discard half, OR skip the computation on discarded
    // phases — the latter is a 2x speedup but requires careful phase
    // tracking. Bit-identical either way. Planner decides.)

    // ... (decimator phase logic: push, compute FIR output when retained,
    //      else skip. When retained, feed to spu94_tick.) ...

    // When a 22.05 kHz tick is produced, call spu94_tick(state). spu94_tick
    // reads the reverb-input registers (vLIN, vRIN) and its per-tick
    // body produces the reverb output for the tick.
    //
    // INTERPOLATE side: each spu94_tick produces ONE 22.05 kHz output pair;
    // interpolate produces TWO 44.1 kHz output samples per tick. Both
    // samples are produced by the same interpolator state but different
    // phases of the half-band filter.
    //
    // Because a single call to this wrapper produces ONE 44.1 kHz output,
    // the wrapper's state must track which phase of the interpolator
    // output is next in the output stream. On tick-aligned calls, we
    // compute a new tick and consume phase 0 of the interpolator; on
    // between-tick calls, we emit phase 1 of the previously-computed tick.

    // ... (interpolator phase logic: emit phase 0 on even 44.1 kHz calls,
    //      phase 1 on odd; advance the ring after phase 0.) ...
}
```

Exact state-machine for the interpolator's two phases is a Claude-discretion detail; the important invariant is that the wrapper produces exactly one stereo 44.1 kHz output per call and that `spu94_tick` is called exactly once every two wrapper calls.

### Pattern 4: Public latency accessor

**What:** `uint32_t spu94_get_latency_samples(void)` returns 38. Single public addition from Phase 4.

**Example:**

```c
// In src/spu94/spu94_fir.c OR src/spu94/spu94_io_chain.c
//
// Latency contract (D-09): total FIR group delay at 44.1 kHz reference rate.
// Decimator:   19 samples (center tap of the 39-tap half-band at the
//              decimator side, measured in 44.1 kHz samples).
// Interpolator: 19 samples (same).
// Total round-trip FIR group delay: 38 samples at 44.1 kHz.
// This value is a property of the 39-tap linear-phase half-band structure
// and is fixed as long as the FIR structure is fixed.

uint32_t spu94_get_latency_samples(void)
{
    return 38u;
}
```

Alternatively planner may expose as `static inline uint32_t spu94_get_latency_samples(void) { return 38u; }` in `include/spu94/spu94.h`, or as a `#define SPU94_LATENCY_SAMPLES 38` — all three satisfy the D-09 contract.

### Anti-Patterns to Avoid

- **Floating-point anywhere in the FIR.** Grep guard catches it; reject at code review.
- **Polyphase decomposition that breaks bit-identity audit.** D-01 requires the folded-form production implementation to be bit-identical to a literal-39-multiply reference. Polyphase rearrangement changes the order of summation in a way that a cascade-clamp path could perceive as different — for clamp-once it's still bit-identical, but the audit becomes "bit-identical for these specific optimizations, assuming you believe associativity holds under saturating arithmetic" rather than "bit-identical by direct algebraic rewrite of the 39-multiply sum." **Defer polyphase to M8 (MCU optimization pass)** — Phase 4 ships the straightforwardly-auditable folded form.
- **Using `q15_mul_truncate` (without `_with_err`) in Phase 4.** Per D-06, every multiply routes to `_with_err` so the truncation remainder is observable. Phase 3's ADR-0011 pinned the same convention for the reverb body; Phase 4 extends it.
- **Re-computing `fir_coef[k]` at runtime.** The table is `static const` and read directly. Never indirect through a function pointer at the inner loop (unless the D-04 cascade-clamp seam is explicitly engaged; even then, the function pointer is on the STAGE FUNCTION, not on the coefficient-lookup).
- **Exposing the internal 44.1 kHz wrapper on a public header.** D-07 is explicit: it is internal. Only `spu94_get_latency_samples()` is new public surface from Phase 4.
- **Stateful recursion inside the FIR stages.** The FIR is a pure feed-forward filter. Any recursion (reading an output sample you just wrote) is a bug. (Circular buffer is not recursion — it's organized storage of past inputs.)
- **Sharing the decimator's delay-line state with the interpolator.** Phase 4 has FOUR delay lines: decimator L, decimator R, interpolator L, interpolator R (D-08). They never share state.
- **Assuming the interpolator's phase-1 subfilter is a passthrough for zero-latency "free" output.** It is a passthrough *value-wise* (just the center tap = 0x4000 = 0.5 in Q15), BUT it still consumes one 44.1 kHz sample of latency in the group-delay budget. The 19-sample interpolator latency is computed over the full 39-tap filter; the polyphase split does not reduce it.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Q15 multiply with saturation + truncation | A new `mul_q15` function | `q15_mul_truncate_with_err` from `include/spu94/spu94_q15.h` | Already lands ADR-0001's truncation direction + ADR-0004's pre-sat remainder. Any new multiply helper would have to be re-audited against the identical test table. |
| 16-bit saturation | A new `clamp16` function | `sat_s16` from the same header | Already vetted; one ODR home. |
| Saturating add | A new `add_s16_sat` | `q15_add_sat` | Same. |
| Half-band FIR coefficient design | Derive coefficients from half-band filter design theory | **The verbatim 39-value table from SCPH-5501 hardware readout** | This IS the bit-faithful SPU FIR. Deriving "equivalent" coefficients from filter-design theory (e.g., Parks-McClellan equiripple half-band at the same order) would produce a filter with the same frequency response *shape* but not bit-identical output to the PS1. The whole Phase-4 value is bit-faithfulness at the I/O boundary. |
| FIR delay-line state | A general-purpose ring-buffer library | Two hand-rolled `int16_t ring[39]` + `uint8_t index` pairs per direction, inlined in `spu94_fir.c` | Generic ring buffers pull in alignment / thread-safety / grow-shrink semantics that cost code size and cognitive load for zero benefit here. 39-slot fixed-size int16 ring buffer is ~10 lines of C. |
| Sample-rate conversion infrastructure | libsamplerate / libsoxr / resample.c | Direct FIR filter using the coefficient table | libsamplerate and libsoxr use windowed-sinc + polyphase with runtime-adjustable coefficients; they target arbitrary-ratio conversion. They do NOT produce bit-identical output to the PS1. |
| Polyphase decomposition | A generic polyphase FIR engine | Direct 39-tap FIR with explicit phase state | Direct form is auditable; polyphase optimization is a later pass. |
| Test harness | A new test framework | Unity + inline-reference-table pattern (existing) | Established. Zero net new deps. |
| Python ctypes stateful-fuzz harness | A new harness | Phase 3's `fuzz_reverb.py` template | Independent Python model, ~10⁶ ops, divergence in either direction fails. |
| Hand-derived reference table | Run Mednafen and capture its output | **Pure-Python integer reference implementation in `tests/python/derive_fir_reference.py`** | PROJECT.md licensing posture. GPL emulator output is for witness diffs (Phase 7), not test-oracle derivation. Same discipline as Phase 3 Pitfall 9. |
| DC bias analysis | Empirically measure DC response | Analytically: `Σ coef = 0x7FFE = 32766` = `0.999939` in Q15 → near-unity DC gain for the decimator; interpolator phase-0 sums to `0x3CFE = 15614` = `0.47655` (and phase-1 = 0.5) → overall gain per output pair = 0.976 (slight DC loss; SPU-accurate) | The DC gain is a property of the coefficient table and computed by summing. One-line invariant that the Python reference + the C test both validate. |

**Key insight:** Phase 4's custom-code budget is **zero new primitive functions** AND **zero new numerical derivations**. The 39 coefficient values are facts from hardware; everything else is composition of Phase 1/2/3 primitives plus simple ring buffer bookkeeping. Any temptation to compute a new saturation, rounding, or filter-design helper is a smell — audit it against the existing primitive first.

---

## Runtime State Inventory

> Phase 4 is NOT a rename / refactor / migration — it adds new code. Section skipped per the section's own trigger condition.

**Nothing found in any category:** verified — this is a greenfield phase that adds files + struct fields + ADRs. No stored data, live service config, OS-registered state, secrets, or build artifacts carry embedded strings that need updating as a result of Phase 4.

Only exception: `docs/DECISIONS.md` gets new ADRs (~8–12 per D-13) prepended at the top; `docs/BIBLIOGRAPHY.md` is created for the first time in the project (no prior phase needed it). Both are ordinary commits, not state migrations.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| Python 3.10+ | `tests/python/derive_fir_reference.py` + `tests/python/fuzz_fir.py` (D-16) | ✓ (confirmed Phase 2 Plan 05: `python3 --version` → `Python 3.13.7`) | 3.13.7 | — |
| numpy | `tests/python/derive_fir_reference.py` (FFT-based frequency-sweep validation, optional) | ✓ (confirmed Phase 3 research: `python3 -c "import numpy"` succeeded) | ≥ 1.26 | Pure-Python integer DFT at reduced FFT length (spot-check only, not full-spectrum) |
| CMake / ninja | Build | ✓ (Phase 1 established) | Phase-1-pinned | — |
| gcc + clang | CI matrix | ✓ (Phase 1 established) | — | — |
| Unity C test framework | `tests/unit/fir/*.c` (D-15) | ✓ (Phase 1 vendored at `third_party/unity/`) | Phase-1-pinned | — |

**Missing dependencies with no fallback:** None for Phase 4 itself.

**Missing dependencies with fallback:** None critical. **Mednafen and DuckStation CLI binaries** are the one "conditional" dependency — needed only for the empirical Mednafen / DuckStation FIR-implementation investigation (D-14). If they cannot be installed on the target environment, the witness-classification deliverable is deferred with a documented protocol (§ Witness analysis — Empirical Mednafen/DuckStation investigation protocol). The research below provides the exact protocol so the execution pass can run the classification when the binaries are available; it does not block Phase 4 implementation plans.

**Not checked / not installed on the research workstation:**
- `mednafen` CLI binary — not probed.
- `duckstation` or `duckstation-qt` or `duckstation-nogui` CLI binary — not probed.
- Homebrew PSX test ROM (MIT-licensed suite of test programs that play known audio into reverb) — none currently in repo. Planner may need to build or source one for the execution pass.

If any of these cannot be made available to the Phase 4 execution environment, the Mednafen / DuckStation classification is flagged "pending empirical pass" and logged as such in the SC-4 ADR. lv2-psx-reverb is already classified OUT-OF-AXIS from its own README; SC-4 is not blocked by the Mednafen / DuckStation deferral.

---

## Validation Architecture

Nyquist validation is enabled per `.planning/config.json` (`workflow.nyquist_validation: true`).

### Test Framework

| Property | Value |
|----------|-------|
| Framework (C) | Unity (vendored at `third_party/unity/`, Phase-1-pinned) |
| Framework (Python) | Python 3.10+ ctypes + optional numpy + pytest ≥ 9.0 |
| Config file | `tests/unit/fir/CMakeLists.txt` (NEW; follows `tests/unit/reverb/CMakeLists.txt` pattern) + `tests/python/CMakeLists.txt` (EXTEND — add `fuzz_fir` target) |
| Quick run command | `cmake --build build --target fir && ctest --test-dir build -L fir` |
| Full suite command | `ctest --test-dir build` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CORE-06 | Decimator produces bit-faithful 22.05 kHz stream from 44.1 kHz input | unit | `ctest --test-dir build -R fir_decimate` | ❌ Wave 0 |
| CORE-06 | Decimator impulse response matches 39-tap half-band shape | unit | `ctest --test-dir build -R fir_impulse` | ❌ Wave 0 |
| CORE-06 | Decimator int32 accumulator has no overflow across full int16 input range (SC-3) | unit | `ctest --test-dir build -R fir_overflow_proof` | ❌ Wave 0 |
| CORE-07 | Interpolator round-trips DC without bias / drift (SC-2) | unit | `ctest --test-dir build -R fir_dc` | ❌ Wave 0 |
| CORE-07 | Interpolator produces symmetric half-band shape (SC-1) | unit | `ctest --test-dir build -R fir_interpolate` | ❌ Wave 0 |
| CORE-06 + CORE-07 | Round-trip transparency (band-limited audio through decimate → interpolate, reverb bypassed) | unit | `ctest --test-dir build -R fir_round_trip` | ❌ Wave 0 |
| CORE-06 + CORE-07 | Folded-form vs literal-form bit-identity (D-01 audit witness) | unit | `ctest --test-dir build -R fir_bit_identity` | ❌ Wave 0 |
| CORE-06 + CORE-07 | Latency is exactly 38 samples at 44.1 kHz (D-09) | unit | `ctest --test-dir build -R fir_latency` | ❌ Wave 0 |
| CORE-06 + CORE-07 | Err-tap and overflow-tap invariants (zero on clean input, nonzero + monotonic on stress, matches Python reference) | unit | `ctest --test-dir build -R fir_err_overflow_taps` | ❌ Wave 0 |
| CORE-06 + CORE-07 | 10⁶ random-input fuzz, no corruption, latency matches, no overflow (D-16) | integration (Python) | `ctest --test-dir build -R fuzz_fir` | ❌ Wave 0 |
| CORE-06 + CORE-07 | Coefficient table integrity (39 entries, symmetric, center = 0x4000, half-band zero pattern, SHA-256 sidecar) | unit | `ctest --test-dir build -R fir_coef_table` | ❌ Wave 0 |
| CORE-06 | Frequency-sweep shape (passband flat to ~10 kHz, transition at 11.025 kHz, stopband floor — FFT validation) | unit + Python helper | `ctest --test-dir build -R fir_freq_sweep` | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `cmake --build build && ctest --test-dir build -L fir` (runs the FIR suite in isolation; fast — under 10 seconds expected for the unit tests, ~3 s for the Python fuzz).
- **Per wave merge:** `ctest --test-dir build` (full project test suite — Phase 1/2/3 tests continue to pass unchanged, Phase 4 adds the `fir` label suite).
- **Phase gate:** Full suite green before `/gsd-verify-work`, plus the SC-4 ADR in place, plus the witness-investigation finding documented.

### Wave 0 Gaps

- [ ] `tests/unit/fir/CMakeLists.txt` — CMake subdirectory wiring for the new FIR test TUs (follows `tests/unit/reverb/CMakeLists.txt` precedent)
- [ ] `tests/unit/fir/test_fir_coef_table.c` — covers table integrity invariants (CORE-06/07 substrate)
- [ ] `tests/unit/fir/test_fir_decimate.c` — covers CORE-06 per-stage behavior
- [ ] `tests/unit/fir/test_fir_interpolate.c` — covers CORE-07 per-stage behavior
- [ ] `tests/unit/fir/test_fir_bit_identity.c` — covers D-01 audit
- [ ] `tests/unit/fir/test_fir_overflow_proof.c` — covers SC-3
- [ ] `tests/unit/fir/test_fir_impulse.c` — covers SC-1
- [ ] `tests/unit/fir/test_fir_dc.c` — covers SC-2
- [ ] `tests/unit/fir/test_fir_freq_sweep.c` — covers D-15 frequency-sweep
- [ ] `tests/unit/fir/test_fir_round_trip.c` — covers D-15 round-trip transparency
- [ ] `tests/unit/fir/test_fir_latency.c` — covers D-09 latency contract
- [ ] `tests/unit/fir/test_fir_err_overflow_taps.c` — covers D-05/D-06 invariants
- [ ] `tests/python/derive_fir_reference.py` — hand-derived Python reference (independent of any C emulator; follows Phase 3 `derive_reverb_reference.py` pattern)
- [ ] `tests/python/fuzz_fir.py` — 10⁶ random-input fuzz harness (D-16, follows `fuzz_reverb.py` pattern)
- [ ] `tests/python/CMakeLists.txt` — EXTEND with `fuzz_fir` target
- [ ] Framework install: none — Unity already vendored, Python 3.10+ already installed.

---

## Primary-Source Evidence

This section corresponds to the research artifact's Section 1 per the CONTEXT specification. Coefficients and structural facts transcribed as integer / structural facts per PROJECT.md licensing posture; no prose from any source copied.

### Fact 1: The SPU reverb engine runs at 22.05 kHz with L/R 44.1-kHz-tick time-multiplex

**Source (primary):** nocash psx-spx SPU Reverb section, reaffirmed by jsgroth's Part 3 writeup.

**Content:** The reverb engine consumes one stereo input sample per 22.05 kHz tick (half the SPU base rate of 44.1 kHz). Within that tick, the silicon alternates the left calculation on one 44.1 kHz cycle with the right calculation on the next, producing one full stereo reverb sample every two 44.1 kHz clocks. The 39-tap FIR is how the silicon converts between the external 44.1 kHz sample rate and the internal 22.05 kHz reverb rate.

**Provenance:** [VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/] — research WebFetch 2026-04-20, quote (paraphrased): reverb hardware spends one 44100h cycle on left calculations and the next on right calculations. [VERIFIED: jsgroth.dev/blog/posts/ps1-spu-part-3/] — research WebFetch 2026-04-20, quote (paraphrased): rest of the SPU clocks at 44100 Hz; reverb unit clocks at exactly half that rate, 22050 Hz; each clock alternates between processing L and R audio channels.

**Implication for Phase 4:** The internal 44.1 kHz wrapper (D-07) calls `spu94_tick` exactly once per pair of 44.1 kHz calls. The FIR is *outside* the reverb engine, not inside it — the FIR's delay lines are independent of the reverb work buffer and independent of the per-tick register state.

### Fact 2: The FIR is 39 taps, applied at BOTH I/O boundaries with the same coefficients

**Source (primary):** psx-spx "Reverb Buffer Resampling" subsection; reaffirmed by jsgroth's Part 3 writeup.

**Content:** Input to and output from the reverb unit is resampled using the same 39-tap FIR filter. The filter is shared between downsampling (44.1 → 22.05, decimation-by-2) and upsampling (22.05 → 44.1, interpolation-by-2). Jsgroth explicitly flags this as "rather strange" — unusual for an engineered filter design because the 22.05 → 44.1 interpolator typically wants a separate post-interpolation filter with a doubled DC gain (half-band interpolators have 0.5 DC gain by default). The PS1's reuse of the same table means the interpolator inherits that gain characteristic; a full 44.1 → 22.05 → 44.1 round-trip exhibits a compounded gain loss unless re-compensated.

**Provenance:** [VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/]. [VERIFIED: jsgroth.dev/blog/posts/ps1-spu-part-3/].

**Implication for Phase 4:** D-08's shared coefficient table is spec-accurate. The gain mismatch is bit-faithful: SPU-94 does not compensate it — any DC-gain correction a musical user wants belongs to M4's plugin layer.

### Fact 3: The 39 FIR coefficient values

The definitive hex table from the published source — see § Coefficient table below for the full transcription in tabular form with symmetry verification and half-band structure check.

### Fact 4: Reverb engine at 22.05 kHz does not require a second FIR for L/R

jsgroth's Part 3 writeup describes the L/R state of the FIR at the decimator: "There should be separate deques for the L and R input signals, and the reverb unit should push into both of them on each 44100 Hz clock." This validates D-08 (separate per-channel FIR state) and refutes the "R = −L" recollection (already confirmed a conflation with either the reverb engine's L/R time-multiplex or the SAME/DIFF cross-feed, per the CONTEXT.md 2026-04-20 research note).

**Provenance:** [VERIFIED: jsgroth.dev/blog/posts/ps1-spu-part-3/].

### Fact 5: The Reverb Buffer Advance wrap formula (reconfirmed)

This is already locked by Phase 2 ADR-0006 but reaffirms consistency: `BufferAddress = MAX(mBASE, (BufferAddress + 2) AND 0x7FFFE)`. The reverb work buffer is orthogonal to the FIR delay lines. Phase 4 touches neither the work buffer nor the wrap formula.

---

## Secondary Corroboration

Section 2 per CONTEXT structure. Published writeups that corroborate or extend the primary-source facts above.

### jsgroth's Part 3 writeup (secondary)

Provides implementation guidance (independent L/R deques, 22.05 kHz alternation) but deliberately does NOT reproduce the coefficient values — jsgroth points readers at psx-spx for the numbers. This is a key corroboration **of the structural facts** but NOT a separate coefficient-publication source. [VERIFIED: jsgroth.dev/blog/posts/ps1-spu-part-3/ — research WebFetch 2026-04-20, explicitly states that coefficients are externally hosted and links to psx-spx.]

### hitmen c02 SPU documentation (candidate third source)

**Does NOT contain the FIR coefficient table.** [VERIFIED: hitmen.c02.at/files/docs/psx/spu.txt — research WebFetch 2026-04-20, reports the document covers reverb effects, delay time calculations, and register semantics but does not address sample rate conversion or FIR filtering. The author explicitly notes incomplete technical documentation on reverb configuration.] hitmen c02 cannot serve as D-10's "third source."

### psdevwiki SPU page (candidate third source, blocked)

403 on research WebFetch 2026-04-20 — could not directly verify. WebSearch results show the psdevwiki page exists and discusses SPU reverb but does not surface the coefficient table in search snippets. Noted for the execution-pass attempt; not relied upon.

### emu-russia/psxrev wiki (candidate third source)

**Does NOT contain the 39-tap FIR coefficient table.** [VERIFIED: github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md — research WebFetch 2026-04-20, covers general SPU architecture + memory layout + chip specifications but not reverb FIR coefficients.]

### Coefficient provenance audit (the important finding)

CONTEXT.md D-10 framed the coefficient sourcing as a **three-source byte-for-byte cross-reference**. The research finding is that only one true source chain exists in the published landscape: **the SCPH-5501 hardware readout originally posted to forums.bannister.org**. Every published place that transcribes the 39 coefficient values either (a) transcribes the bannister readout or (b) points to psx-spx's transcription of the bannister readout. jsgroth, hitmen c02, psdevwiki (per available evidence), and emu-russia/psxrev either don't publish the values or publish them by re-citing psx-spx.

**What this means:**

| D-10 intent | What actually exists | How to handle |
|-------------|----------------------|---------------|
| Three byte-for-byte independent readings from three hardware chips / three engineering teams | One hardware reading (SCPH-5501, bannister forum), transcribed in ≥2 places that all credit the same reading | (1) Cite the bannister forum thread as the **primary** source + the psx-spx render as the **secondary** citation. (2) Include one of the "structural secondary" sources (jsgroth for structure; hitmen c02 explicitly notes the FIR is unaddressed in its docs — cited as a negative finding). (3) Document the audit in the SC-4 ADR and the coefficient-sourcing ADR so any future reader knows the cross-reference status at lock time. (4) Mark the table as "single-source, awaiting M5 hardware-capture confirmation from an independent SCPH-7502 / SCPH-9001 / other-revision console to true-cross-reference." |

This is a honest-reporting finding the planner and user need to reconcile. CONTEXT.md D-10 can still be honored by including three citations in the bibliography, but the research document records explicitly that they derive from one hardware reading. Three user-facing options on how to move forward (for discuss-phase iteration — not a researcher decision):

1. **Accept the finding and proceed.** Values are consistent across every published mirror; no disagreement exists at the integer level; the table is treated as a single-source fact pending M5 hardware confirmation. This is the honest-and-practical path.
2. **Gate Phase 4 on a second independent hardware readout.** Block until someone (the user, a community member, or a future M5 pass) reads coefficients from a different SPU revision and verifies byte-for-byte match. This is maximally rigorous but adds a potentially-large schedule risk.
3. **Pick a working value and defer arbitration to M5** per the CONTEXT.md "Deferred to Milestone 5" bullet. This is a no-op — the values are already consistent across the published landscape; "pick a working value" means pick the published values, which is exactly path 1.

The research recommends path 1. Surface the audit finding transparently; keep the seam.

---

## Witness Analysis

Section 3 per CONTEXT structure. Covers D-14 (Mednafen / DuckStation empirical investigation) and the lv2-psx-reverb OUT-OF-AXIS classification.

### lv2-psx-reverb: confirmed OUT-OF-AXIS on frequency-response

**Finding:** lv2-psx-reverb explicitly does not implement the half-band FIR. The project's own README states this in plain language:

> *"It sounds somewhat different because, unlike the real console, this code doesn't downsample the reverb to 22050 Hz. But other than the additional brightness of the higher frequencies it sounds almost spot on to the original."*

**Provenance:** [VERIFIED: github.com/ipatix/lv2-psx-reverb — research WebFetch 2026-04-20, README text quoted above.]

**Classification:** OUT-OF-AXIS for Phase 7 witness-diff on the frequency-response axis. IN-AXIS remains valid for reverb-network-behavior witness (comb/APF structure, register semantics — per PROJECT.md Key Decisions).

**Implication for Phase 4:** The SC-4 ADR explicitly documents this classification. It is not re-derived here; it is primary-source-attested by the witness project itself.

### Mednafen and DuckStation: empirical classification deferred to execution pass

**Status:** NOT executed in this research pass. The research machine did not probe for installed Mednafen / DuckStation binaries, did not source a PSX test ROM, and did not run the protocol below. The classification is "pending empirical pass" — Phase 4 execution should run the protocol before the SC-4 ADR lands.

**Rationale for deferral:** The empirical investigation requires (a) a homebrew or commercial PSX ROM that plays a known audio signal into the reverb input with a non-zero reverb config, (b) running it through each emulator's standalone / CLI mode with audio capture to WAV, (c) FFT analysis in numpy / matplotlib to extract the frequency response. None of these artifacts exist in the current repo; all three are executable in the Phase 4 execution environment but not in the research pass. The research delivers the exact protocol so the execution pass can run it with high confidence.

### Empirical Mednafen/DuckStation investigation protocol

**Objective:** Determine whether each emulator implements the 39-tap half-band FIR (IN-AXIS for frequency-response witness) or skips it (OUT-OF-AXIS, like lv2-psx-reverb).

**Licensing posture check:** Per PROJECT.md — Mednafen (GPLv2), DuckStation (CC-BY-NC-ND as of Sep 2024). Their OUTPUT is witness material; their SOURCE is not a primary input. This protocol uses only captured audio output.

**Input signals (run each into the reverb):**

1. **Unit impulse** at t=0, single sample at +0x7FFF, followed by zeros. Reverb wet full, reverb dry zero (use "Echo" or "Hall" preset with register values set to produce a clean impulse response).
2. **Near-Nyquist sine**: 20 kHz continuous tone at 44.1 kHz input rate. This is above the 22.05 kHz / 2 = 11.025 kHz half-band cutoff. Under a working half-band FIR, this signal is attenuated by > 60 dB at the reverb boundary (it's in the FIR stopband). Without the FIR, it aliases back to the 44.1 − 20 = 24.1 kHz bin, which folds to 44.1 − 24.1 = 20 kHz (or similar fold depending on interpolation), producing visible aliasing spurs.
3. **White-noise burst**: uniformly-distributed int16 random noise for ~1 second. The output spectrum reveals the filter's shape directly (convolution in time ↔ multiplication in frequency). A working half-band FIR produces a characteristic passband / transition / stopband shape between 0–22.05 kHz; absence of it produces a flat response (within the reverb's own filter network).
4. **Band-limited sweep**: logarithmic sine sweep from 20 Hz to 22 kHz at 44.1 kHz sample rate. Apply an impulse-preserving window at the start and end. Extract the transfer function via sweep-deconvolution (inverse-sweep-convolution technique or equivalent).

**Capture path (per emulator):**

- Mednafen: `mednafen -sound.driver file -sound.wav out.wav TEST_ROM.bin` (or equivalent — consult Mednafen CLI docs for the exact flag; `-sound.driver file` is the pattern).
- DuckStation: `duckstation-nogui --dump-audio out.wav TEST_ROM.bin` (or the `-dump-audio` flag equivalent; DuckStation's CLI has audio-dump support).
- Save captured WAV to `captures/{mednafen,duckstation}/{impulse,sine20khz,whitenoise,sweep}_{preset}.wav`.

**Analysis (per capture, in Python / numpy):**

```python
import numpy as np
import scipy.io.wavfile as wavfile
import matplotlib.pyplot as plt

# Load capture
rate, y = wavfile.read('captures/mednafen/sweep_hall.wav')
# Strip any leading silence / emulator boot noise — locate the first
# sample above a threshold and align to that.
# ...

# Impulse response test:
#   if working FIR, peak latency from input impulse to output impulse
#   should match 38 samples (decimator + interpolator group delay);
#   without FIR, latency is 0 (or register-dependent only).
# Note: the PS1's own reverb engine adds its own group delay per preset,
# so the impulse-latency test measures FIR-latency + reverb-latency.
# To isolate the FIR latency, compare the input-impulse latency under a
# "reverb-off" preset (all reverb registers zero; the signal passes
# through only the FIR chain) vs a "reverb-on" preset (full preset).

# Near-Nyquist sine test:
#   FFT the output of a 20 kHz input. Look for the 20 kHz bin.
#   With working FIR: the bin should be below -60 dB (attenuated by the
#   FIR stopband). Without FIR: the bin is close to input level minus the
#   reverb's own gain.
#   SECONDARY: look for alias spurs at (44.1 - 20) = 24.1 kHz fold or
#   at (44.1 - 2*20) = 4.1 kHz fold. With FIR: no alias spurs. Without
#   FIR: alias spurs at predictable fold frequencies.

Y = np.abs(np.fft.rfft(y))
freqs = np.fft.rfftfreq(len(y), 1.0 / rate)

peak_20k = Y[(freqs > 19500) & (freqs < 20500)].max()
floor = np.median(Y)
print(f'20 kHz bin: {20 * np.log10(peak_20k / floor):.1f} dB above noise floor')
# Working FIR: < -40 dB (deep stopband)
# Broken / missing FIR: 0 to +40 dB depending on reverb gain

# White-noise spectrum test:
#   averaged periodogram of the noise-in / noise-out shows the filter
#   shape directly. Compare to the expected half-band transfer function
#   (computed from the known coefficient table via numpy.fft.rfft(coefs)).
# ...
```

**Classification logic:**

| Observation | Classification |
|-------------|---------------|
| 20 kHz bin attenuated > 40 dB below input level | Working half-band FIR present → **IN-AXIS** for frequency-response witness |
| 20 kHz bin within ±6 dB of input level (allowing for reverb gain) | FIR absent or effectively-not-run → **OUT-OF-AXIS** |
| White-noise output spectrum shows lowpass shape with transition ~11 kHz | FIR present → **IN-AXIS** |
| White-noise output spectrum is flat to Nyquist minus reverb's own shape | FIR absent → **OUT-OF-AXIS** |
| Impulse-response latency (reverb-off preset) shows ~38 samples group delay | FIR present → **IN-AXIS** |
| Impulse-response latency (reverb-off preset) ≈ 0 samples | FIR absent → **OUT-OF-AXIS** |

Require majority agreement across at least 3 of 4 test signals for classification; document the dissenting result if any.

**Deliverable:** A new section in `04-RESEARCH.md` (or a follow-up `04-RESEARCH-WITNESS.md`) records per-emulator classification + supporting plots (PNG files in `.planning/phases/04-.../witness-captures/`). SC-4 ADR consumes the classification. Phase 7's TEST-03 witness-diff harness inherits the axis-assignment from this document.

**Estimated effort:** 2–4 hours for a developer who already has one of the emulators installed; half a day if a PSX test ROM must be sourced or built first. Not a research-phase blocker; execution-pass deliverable.

---

## Decision Proposals (Claude's Discretion)

Section 4 per CONTEXT structure. Recommendations on the CONTEXT "Claude's Discretion" items.

### Circular-buffer vs shift-register delay line — RECOMMEND: circular buffer

**What the choice is:** The 39-sample int16 delay line can be stored as (a) a ring buffer with an index pointer that wraps mod 39, or (b) a flat array where each new sample shifts the entire array down by one slot via memmove/memcpy.

**Bit-identity:** Both produce bit-identical outputs when the FIR read order is consistent with the storage convention. This is not the decision axis.

**MCU-friendliness (Cortex-M7 primary target):** Circular buffer wins. A shift register with memmove of 39 × 2 = 78 bytes per input sample at 44.1 kHz is 3.4 MB/s of memory-copy traffic per channel. Four delay lines (decimator L, decimator R, interpolator L, interpolator R) compound that. On a Cortex-M7 with 32 KB D-cache, memmove of a 78-byte region is cheap (fits in a cache line), but at 44100 Hz × 4 channels = 176400 memmoves/second, every microsecond matters. Circular-buffer incurs one write + one index-increment + one AND-mask per sample per channel — roughly 3 cycles. The memmove incurs ~20 cycles for 78 bytes on a Cortex-M7.

**Cache behavior:** Both fit in L1 D-cache trivially (78 bytes per ring + 4 rings = 312 bytes total). Difference is negligible.

**Test readability:** Shift register is trivially auditable — the array state at any time IS the delay-line contents in age order. Circular buffer requires index bookkeeping. On the audit axis, shift register is easier.

**Recommendation:** **Circular buffer** for production. The test readability concern is addressed by the Python reference model — `tests/python/derive_fir_reference.py` models the FIR with a shift-register (for audit clarity), and the C implementation uses a circular buffer. The bit-identity test asserts they match.

**Commentary:** This matches the CONTEXT.md "circular-buffer is the default; confirm or counter-recommend" hint. Confirmed.

### Cascade-clamp seam mechanism (D-04) — RECOMMEND: compile-time `#ifdef` switch for Phase 4; runtime function pointer deferred to M4

**What the choice is:** D-04 specifies a "policy-variant seam" that flips the FIR stages from clamp-once to cascade-clamp after every add. Options: (a) compile-time `#ifdef SPU94_FIR_CASCADE_CLAMP` with two stage-function variants; (b) runtime function pointer in `struct spu94_state` that picks at dispatch time; (c) a direct caller-selectable variant (separate public functions, caller chooses).

**Recommendation:** **Option (a): compile-time `#ifdef` switch for Phase 4**, deferred to option (b) runtime function pointer at M4 when a plugin user-facing toggle is needed.

**Rationale:**
- Phase 4 ships with default clamp-once only; the cascade-clamp variant is a test-and-ADR artifact, not a runtime feature. A compile-time switch is the simplest mechanism that:
  - Keeps the hot path branch-free (no runtime dispatch).
  - Does not inflate `struct spu94_state` with a function pointer field.
  - Lets the test binary build BOTH variants — `test_fir_bit_identity.c` builds with clamp-once and asserts equality, a new `test_fir_cascade_clamp.c` builds with `SPU94_FIR_CASCADE_CLAMP` defined and asserts the DIVERGENCE expected under cascade (documenting the seam's semantic, not asserting bit-identity).
- M4 can promote the seam from compile-time to runtime when the JUCE plugin's user-facing character toggle needs the switch-without-recompile. That's an additive ADR at M4 time; Phase 4 does not front-load the cost.
- Matches Phase 2's ADR-0005 "swappable policy table" pattern (compile-time pinned for SPU-94, runtime re-pointable by future Controllers milestone via re-link). Consistent architectural style.

### Latency accessor shape (D-09) — RECOMMEND: `static inline` in `include/spu94/spu94.h` returning a `#define SPU94_LATENCY_SAMPLES 38u`

**What the choice is:** `spu94_get_latency_samples()` can be (a) a `.c`-defined function returning 38, (b) a `static inline` in the header returning 38, (c) a `#define`, (d) all three (header macro + inline + function).

**Recommendation:** Header `#define SPU94_LATENCY_SAMPLES 38u` + `static inline uint32_t spu94_get_latency_samples(void) { return SPU94_LATENCY_SAMPLES; }` in `include/spu94/spu94.h`.

**Rationale:**
- C consumers get a compile-time constant for use in static array sizing, preset-delay calculation, etc.
- Python ctypes (Phase 6) calls the `static inline` via a wrapper generated at binding time. ctypes does not consume `#define`; it needs a symbol.
- JUCE plugin M4 code can use either, whichever the AudioProcessor API prefers at the call site.
- Having both does not inflate the binary; the inline is a single `mov rax, 38; ret` that LTO eliminates.
- Matches the one-public-symbol-from-Phase-4 posture.

### Test-file granularity under `tests/unit/fir/` — RECOMMEND: one TU per test-dimension (NOT one TU per stage)

**What the choice is:** Either (a) one TU per FIR stage (`test_fir_decimate.c` owns all decimator tests, `test_fir_interpolate.c` owns all interpolator tests) or (b) one TU per test-dimension (`test_fir_impulse.c` owns the impulse test for both stages, `test_fir_dc.c` owns DC for both, etc.).

**Recommendation:** **One TU per test-dimension** (option b). Detailed file list under § Architecture Patterns → Recommended Project Structure above.

**Rationale:**
- Follows the Phase 3 precedent where `test_reverb_edges.c` covered all stages for TEST-07 edges and `test_reverb_body.c` covered the full-tick equivalence — test dimensions cross stage boundaries.
- Each test-dimension TU is small and focused: `test_fir_impulse.c` is ~40 lines of setup + impulse-feed + output-assertion; easier to audit per-dimension than a giant `test_fir_decimate.c` that mixes impulse + DC + overflow + latency + err-tap checks.
- CI failure modes localize: a broken impulse response reports "test_fir_impulse failed" rather than "test_fir_decimate failed — which of its 12 sub-tests?".
- The coefficient-table integrity test (`test_fir_coef_table.c`) is naturally its own TU and doesn't fit either stage.

### ADR split vs merger (D-13) — RECOMMEND: collapse to 9 ADRs (not 12)

**What the choice is:** D-13 says "max granularity ~8–12 ADRs"; planner decides the exact split.

**Recommendation:** 9 ADRs at the split below. Not maximally granular; tuned for narrative coherence where two related decisions naturally pair.

| ADR | Covers | Lock state |
|-----|--------|-----------|
| Phase-4-A | Half-rate architecture + lv2-psx-reverb exclusion on frequency-response axis (SC-4 mandate) | Locked — PROJECT.md Key Decision + this research's witness finding |
| Phase-4-B | FIR math form: folded production + literal audit witness (D-01) | Locked |
| Phase-4-C | FIR accumulator width: int32 with 0.46-bit margin + int64 seam (D-02) | Locked with audit finding |
| Phase-4-D | FIR clamp policy: clamp-once default + cascade-clamp seam (D-03 + D-04 together; they are one decision surface split by seam) | Locked |
| Phase-4-E | FIR precision-loss observable: err-tap + overflow-magnitude tap (D-05 + D-06 together; they are the two halves of the same observable surface) | Locked |
| Phase-4-F | Internal 44.1 kHz wrapper shape: internal-only, composable by Phase 5 (D-07) | Locked |
| Phase-4-G | Per-channel FIR state: two independent delay lines, shared coefficient table (D-08) | Locked |
| Phase-4-H | Latency contract: 38 samples at 44.1 kHz, public accessor (D-09) | Locked |
| Phase-4-I | Coefficient sourcing + provenance audit + bibliography (D-10, D-11, D-12 together; they are one decision about the coefficient table) | Locked with the audit finding that three sources collapse to one hardware reading |

9 ADRs. Planner may flip to 12 by splitting D-03 from D-04, D-05 from D-06, D-10 from D-11/D-12 — each of those is a reasonable alternative. The "max granularity" framing would give 12; "natural boundary" framing gives 9. Either is correct per D-13.

### Exact prototypes and names — DEFER to planner

CONTEXT.md explicitly left these to Claude's Discretion:

- `spu94_fir_decimate_step` vs `spu94_fir_push_and_decimate` — either works. RECOMMEND: `spu94_fir_decimate` (name matches `spu94_reverb_input_scale` stage-naming convention; "step" adds nothing).
- `spu94_fir_interpolate_step` vs `spu94_fir_interpolate` — same. RECOMMEND: `spu94_fir_interpolate`.
- Internal wrapper: `spu94_fir_chain_step` vs `spu94_sample_44k1` vs `spu94_io_step` — any. RECOMMEND: `spu94_fir_chain_step` — descriptive of what it does ("steps the FIR chain one 44.1 kHz sample forward"); matches SPU-94 naming conventions (snake_case with `spu94_` prefix).
- Reverb-bypass hook for testing (§ Specifics in CONTEXT.md): RECOMMEND option (b) — a test-only wrapper that chains `decimate → interpolate` directly without `spu94_tick`. Matches the per-stage isolation ethic of Phase 3 Pitfall 9.

---

## Coefficient Table (three-source side-by-side)

Section 6 per CONTEXT structure. Exhaustive table with side-by-side column per source.

### Notation

- Signed int16 values in two's-complement.
- Hex format `±NNNNh`: positive values written as `0xNNNN`; negative values written as `-0xNNNN` with the magnitude on the right (the PSX-SPX convention; e.g., `-0x0B90` means −0x0B90 = −2960 = `0xF470` in two's complement).
- Row = tap index 0–38. Center tap = index 19.

### Table

Each row: `[index] [hex (signed)] [decimal] [two's-complement hex]`. Three source columns show the value as published by each source.

| Idx | Value (hex, signed) | Value (dec) | Value (two's comp) | psx-spx | bannister (via psx-spx) | jsgroth |
|-----|---------------------|-------------|--------------------|---------| ----------------------- |---------|
|   0 | `-0x0001`           | −1          | `0xFFFF`           | ✓       | ✓ *                     | — **    |
|   1 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|   2 | ` 0x0002`           | 2           | `0x0002`           | ✓       | ✓                       | —       |
|   3 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|   4 | `-0x000A`           | −10         | `0xFFF6`           | ✓       | ✓                       | —       |
|   5 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|   6 | ` 0x0023`           | 35          | `0x0023`           | ✓       | ✓                       | —       |
|   7 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|   8 | `-0x0067`           | −103        | `0xFF99`           | ✓       | ✓                       | —       |
|   9 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  10 | ` 0x010A`           | 266         | `0x010A`           | ✓       | ✓                       | —       |
|  11 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  12 | `-0x0268`           | −616        | `0xFD98`           | ✓       | ✓                       | —       |
|  13 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  14 | ` 0x0534`           | 1332        | `0x0534`           | ✓       | ✓                       | —       |
|  15 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  16 | `-0x0B90`           | −2960       | `0xF470`           | ✓       | ✓                       | —       |
|  17 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  18 | ` 0x2806`           | 10246       | `0x2806`           | ✓       | ✓                       | —       |
|  19 | ` 0x4000` (center)  | 16384       | `0x4000`           | ✓       | ✓                       | —       |
|  20 | ` 0x2806`           | 10246       | `0x2806`           | ✓       | ✓                       | —       |
|  21 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  22 | `-0x0B90`           | −2960       | `0xF470`           | ✓       | ✓                       | —       |
|  23 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  24 | ` 0x0534`           | 1332        | `0x0534`           | ✓       | ✓                       | —       |
|  25 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  26 | `-0x0268`           | −616        | `0xFD98`           | ✓       | ✓                       | —       |
|  27 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  28 | ` 0x010A`           | 266         | `0x010A`           | ✓       | ✓                       | —       |
|  29 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  30 | `-0x0067`           | −103        | `0xFF99`           | ✓       | ✓                       | —       |
|  31 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  32 | ` 0x0023`           | 35          | `0x0023`           | ✓       | ✓                       | —       |
|  33 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  34 | `-0x000A`           | −10         | `0xFFF6`           | ✓       | ✓                       | —       |
|  35 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  36 | ` 0x0002`           | 2           | `0x0002`           | ✓       | ✓                       | —       |
|  37 | ` 0x0000`           | 0           | `0x0000`           | ✓       | ✓                       | —       |
|  38 | `-0x0001`           | −1          | `0xFFFF`           | ✓       | ✓                       | —       |

**Footnotes:**

- **✓ \*** — bannister forum thread is the original SCPH-5501 hardware readout that psx-spx transcribes. Treat as the primary fact source. The thread contents are not directly machine-transcribable in this research pass (forums.bannister.org requires forum-browsing to resolve the exact post text); the values in this table are the psx-spx transcription which credits the bannister readout as the source. WebFetch to the bannister thread was blocked by a certificate error in the research pass; WebSearch result snippets confirm the thread hosts the hardware readout and that it matches the psx-spx transcription. See § Coefficient provenance audit above for the finding that all three "sources" trace to this single reading.
- **— jsgroth** — jsgroth's Part 3 blog post does NOT reproduce the coefficient values. Only structural facts are cited (22.05 kHz alternation, independent L/R deques, "rather strange" that the same 39-tap FIR is used for both decimate and interpolate). jsgroth explicitly points readers to psx-spx for the values. Therefore jsgroth is a structural-fact source, not a coefficient-table source.

### Disagreement flag

**None.** Every published source surveyed in the research pass that lists the 39 values lists them with byte-for-byte identical hex values. No resolution required; table is locked.

### Symmetry + half-band verification (computed, not cited)

- Length: 39 (odd, Type I linear-phase FIR) ✓
- Symmetric about index 19: `coef[k] == coef[38 − k]` for all k ∈ [0, 19] ✓ (computed directly: all 20 symmetric pairs match)
- Center tap: `coef[19] == 0x4000 == 16384` ✓ (Q15 representation of 0.5)
- Half-band Type I zero structure: `coef[k] == 0` for all k such that `(k − 19)` is a non-zero even integer ✓ (all 18 such positions are verified zero)
- Sum of coefficients (DC gain, pre-shift): `0x7FFE = 32766` (Q15 value `0.999939`, very close to unity — near-perfect DC preservation by the decimator)
- Sum of |coefficients| (L1 gain, worst-case accumulator bound divisor): `0xB9A6 = 47526` — consumed in the accumulator proof below
- Non-zero coefficient magnitudes (distinct): {1, 2, 10, 35, 103, 266, 616, 1332, 2960, 10246, 16384} — 11 distinct magnitudes
- Folded-form multiply count: 10 pairs (non-center non-zero) + 1 center = **11 multiplies per output sample** (D-01's "~10 multiplies" rounds to 11 exact)

### Bibliography entries to add (deliverable; see § Bibliography additions)

- `BIB-005`: psx-spx SPU page (Reverb Buffer Resampling subsection) — primary published coefficient table.
- `BIB-006`: forums.bannister.org thread — SCPH-5501 hardware readout (original source of the bannister transcription that psx-spx cites).
- `BIB-007`: jsgroth.dev/blog/posts/ps1-spu-part-3/ — structural corroboration (22.05 kHz alternation, independent L/R deques, 39-tap FIR reuse across boundaries). Does NOT provide coefficient values; cited as structural secondary.

---

## Accumulator Width Proof

Section 7 per CONTEXT structure. The copy-pasteable comment block for the accumulator declaration.

### Worst-case bound

For an N-tap FIR `y[n] = Σ_{k=0..N-1} h[k] · x[n-k]` with coefficients `h[k]` and int16 input `x[n-k] ∈ [−0x8000, +0x7FFF]`, the worst-case accumulator magnitude (pre-shift, pre-saturate) is adversarial: pick `x[n-k]` to match the sign of `h[k]` and maximize the aligned sum.

Evaluating for the 39-tap table above:

```
decimator (full 39-tap FIR on every call):
  adversarial positive max:  |Σ h[k] · x*| ≤ 1,557,331,968 = 0x5CD30000
  bits used:                 31 of 32 (sign bit reserved)
  headroom above value:      590,151,679 = 0x232CFFFF
  margin to INT32_MAX:       2.791 dB  (= 0.464 bits)

interpolator (phase-0 subfilter, every-other-tap from the 39-tap FIR):
  adversarial positive max:  |Σ h[k] · x*| ≤ 1,020,461,056 = 0x3CD30000
  bits used:                 30 of 32
  margin to INT32_MAX:       6.463 dB  (= 1.074 bits)

interpolator (phase-1 subfilter, center-tap only):
  adversarial positive max:  |0x4000 · 0x8000| = 0x20000000 = 536,870,912
  bits used:                 30 of 32
  margin:                    12.04 dB  (= 2.0 bits)
```

**Both bounds fit in int32 under all int16 inputs. No overflow reachable.**

The decimator is the tight case: 0.46 bits of headroom is sufficient but leaves no room for additive composed seams (multi-tick accumulation, Q30 coefficient promotion, cascading-clamp intermediate sums, etc.). The D-02 int64 seam is the hedge if any such composition lands in the future.

### Copy-pasteable comment block

```c
// FIR accumulator width proof (see .planning/phases/04-.../04-RESEARCH.md
// § Accumulator Width Proof).
//
// The decimator sums 39 products of (int16 coefficient × int16 input sample)
// before a single arithmetic-right-shift by 15 and a final sat_s16 to int16.
// Worst-case adversarial input (each sample matches the sign of its
// coefficient to maximize the aligned sum) yields accumulator magnitude
// 0x5CD30000 = 1,557,331,968. This fits in int32 (INT32_MAX = 0x7FFFFFFF
// = 2,147,483,647) with 2.79 dB / 0.46 bits of headroom.
//
// The interpolator's phase-0 subfilter is more relaxed: worst-case
// 0x3CD30000 = 1,020,461,056, leaving 6.46 dB / 1.07 bits of headroom.
// The phase-1 subfilter is the center tap alone, trivially safe.
//
// int32 is sufficient for Phase 4 as scoped. If a future composition
// (additional accumulation stages, cascading intermediate clamps,
// Q30 coefficient promotion) would tighten the decimator margin below
// zero bits, promote the accumulator type to int64 per the D-02 seam.
// This requires no caller change: the accumulator is a local in the
// FIR stage function, and sat_s16 accepts both widths via the existing
// q15 primitives.
//
// Bounds derived analytically (Σ |h[k]| · INT16_MIN_MAGNITUDE for the
// decimator) and validated empirically by tests/unit/fir/
// test_fir_overflow_proof.c which drives the accumulator to this bound
// under adversarial input and asserts both the specific bit-pattern of
// the accumulator and the absence of any UBSan signed-overflow trap.
//
// Sum of |h[k]| for the 39-tap table: 47,526 = 0xB9A6.
// INT16_MAX_MAGNITUDE: 32,768 = 0x8000 (abs(INT16_MIN)).
// Product: 47,526 × 32,768 = 1,557,331,968. QED.
```

### Test-driver

`tests/unit/fir/test_fir_overflow_proof.c` contains an adversarial input sequence constructed per the sign-alignment rule above. On running the FIR with that input, the accumulator hits exactly 0x5CD30000 on the first full-delay-line call; the test asserts this bit-pattern, then asserts that the post-shift-post-sat output matches the expected saturated value. The test runs under UBSan in CI (ADR-0003 policy); any signed-integer-overflow in the accumulator path would fail the test at the UBSan instrumentation layer.

---

## Folded-vs-Literal Bit-Identity Argument

Section 8 per CONTEXT structure. Proof that the folded form (~11 multiplies, exploiting symmetry) produces the bit-identical int32 accumulator value as the literal 39-multiply form under D-03 clamp-once.

### Claim

For the 39-tap symmetric FIR with `h[k] == h[38 − k]` and `N − 1 = 38`, and under a regime with NO intermediate saturation (i.e., every multiplication and addition happens in the full int32 range; only one final `sat_s16` applies), the accumulator:

```
literal:  acc_L = Σ_{k=0..38}  h[k] · x[n-k]
folded:   acc_F = h[19] · x[n-19] + Σ_{k=0..18}  h[k] · (x[n-k] + x[n-(38-k)])
```

satisfies `acc_L == acc_F` as int32 values for all int16 inputs.

### Proof

Under integer arithmetic with no overflow (guaranteed within int32 by the § Accumulator Width Proof above), integer multiplication distributes over integer addition exactly:

```
h[k] · x[n-k] + h[38-k] · x[n-(38-k)]
= h[k] · x[n-k] + h[k] · x[n-(38-k)]    [since h[k] == h[38-k]]
= h[k] · (x[n-k] + x[n-(38-k)])          [integer distributivity]
```

Summing over `k = 0..18` pairs each `h[k]` with its symmetric partner `h[38 − k]`, and leaving the center `h[19]` on its own:

```
acc_L = Σ_{k=0..38} h[k] · x[n-k]
      = h[19] · x[n-19]  +  Σ_{k=0..18} (h[k] · x[n-k]  +  h[38-k] · x[n-(38-k)])
      = h[19] · x[n-19]  +  Σ_{k=0..18} h[k] · (x[n-k] + x[n-(38-k)])
      = acc_F
```

Both forms compute the same int32 value. QED.

### Empirical validation

Executed in the research pass: 100 random `x[n-k] ∈ [−32768, 32767]` trials + 1 adversarial worst-case trial — all 101 cases produced `acc_L == acc_F` exactly. Test in `tests/unit/fir/test_fir_bit_identity.c` mirrors this with 10⁵ random trials (per CONTEXT D-01) plus the adversarial case; assertion is `TEST_ASSERT_EQUAL_INT32(acc_L, acc_F)`.

### Where bit-identity breaks

Under the D-04 cascade-clamp seam (engaged via compile-time `#ifdef` per the recommendation above), each multiply independently shifts-and-saturates to int16 before the sum. The `sat_s16` operation is NOT distributive over addition: `sat_s16(a + b) ≠ sat_s16(a) + sat_s16(b)` in general. The pair-sum `(x[n-k] + x[n-(38-k)])` can exceed the int16 range on operands whose individual values are within range; cascading clamp truncates that pair-sum before multiplication, while the literal form clamps each `h[k]·x[n-k]` product individually after multiplication. These two operations produce different int16 values at adversarial inputs.

**Demonstration computed in the research pass:** For `x = [+32767 if k even else −32768 for k in 0..38]` and the coefficient table above, the literal form with cascading clamp evaluates to `−12`, while the folded form with cascading clamp evaluates to `−7`. Bit-different.

**Implication:** The bit-identity test (`test_fir_bit_identity.c`) MUST be compiled with `SPU94_FIR_CASCADE_CLAMP` undefined. The test should include an explicit `_Static_assert(!defined(SPU94_FIR_CASCADE_CLAMP), "bit-identity test requires clamp-once regime")` or equivalent `#ifdef` guard.

**Implication for the cascade-clamp ADR:** The Phase-4-D ADR (clamp policy + seam) must state explicitly that cascade-clamp is NOT bit-identical to clamp-once, and that the audit-witness literal form is meaningless under cascade-clamp. Under cascade-clamp, the production folded form is the only "correct" reading of the observable SPU behavior IF (big if) the SPU turns out to cascade; M5 hardware capture arbitrates.

---

## Test-Vector Library

Section 9 per CONTEXT structure. For each SC-mandated test, specify input sequence, expected output or invariant, threshold, owning TU.

### 1. Impulse response (SC-1)

**Input (44.1 kHz):** single sample at `+0x7FFF` at t=0, all other samples `0`, 80 samples total.

**Expected output (through full 44.1 kHz wrapper with reverb bypassed — see § Reverb-bypass hook):** 39-tap symmetric half-band impulse response, shifted by the internal FIR chain latency of 38 samples (19 decimator + 19 interpolator, D-09). Peak output at t=38 with value `+0x7FFF · 0x4000 / 0x8000 = +0x3FFF` approximately (the precise value depends on interpolator phase arithmetic — hand-derive in `tests/python/derive_fir_reference.py`). Symmetric lobes on either side with the 1–2–10–35–103–266–616–1332–2960–10246 magnitude progression.

**Invariant:** Output is symmetric about its peak; peak magnitude is in the expected range; no aliasing / ringing beyond the 39-tap support.

**Threshold:** Bit-exact match to hand-derived table (no tolerance — integer math).

**Owning TU:** `tests/unit/fir/test_fir_impulse.c`.

**Hand-derived decimator-only impulse response (computed in research pass):** With input `+32767` at t=19 followed by zeros, the decimator-only output (shifted+saturated, assuming a full 39-sample delay line of zeros at t=0) has a peak of +16383 at t=38 (center latency of 19 samples + input at t=19 + decimator group delay of 19). This matches the center tap `0x4000 = 16384` of the filter, multiplied by the `+32767` input, shifted right by 15, giving `32767 * 16384 / 32768 = 16383` (with the truncation toward −∞ from ADR-0001). Symmetric lobes at t ∈ {21,23,25,...,37,39,41,...,55} with magnitudes proportional to the non-zero coefficients' progression.

### 2. DC round-trip (SC-2)

**Input (44.1 kHz):** constant `+0x0400` (or any value well within int16 range) for 200 samples. Second run with `−0x0400`. Third run with `0`.

**Expected output (full wrapper, reverb bypassed):** after the initial 38-sample latency, output stabilizes at the input value multiplied by the cumulative DC gain of decimate + interpolate. Cumulative gain (computed in research pass) = `(sum_h_dec) · (sum_h_int) / 2^30`. `sum_h` = `0x7FFE = 32766`. Cumulative gain ≈ `32766 · 32766 / 2^30 ≈ 0.999878`. So output magnitude ≈ input magnitude × `0.999878`. (Exact ratio depends on the interpolator polyphase phase; phase 0 has gain ≈ 0.47655, phase 1 has gain 0.5; averaged over the 2:1 interpolator output stream the DC gain is ≈ `(0.47655 + 0.5) / 2 ≈ 0.488` at each 44.1 kHz output sample. Hand-derive in Python.)

**Invariant:** Output is DC (no drift, no oscillation) after settling; absolute gain matches the analytically-computed value; L and R channels produce bit-identical output when given identical input (verify D-08 separate state is independent but deterministic).

**Threshold:** Bit-exact match to the Python-derived value after the 38-sample latency. No drift over 200 samples.

**Owning TU:** `tests/unit/fir/test_fir_dc.c`.

### 3. Worst-case overflow proof (SC-3)

**Input (44.1 kHz, adversarial — to the DECIMATOR specifically):** 39 samples constructed per `x[k] = +32767 if coef[38-k] >= 0 else -32768`. This places each input sample at the delay-line tap position where it will multiply its coefficient to produce a positive-sign contribution to the accumulator, maximizing the sum. Verify once the 39-sample delay line is full.

**Expected accumulator value at that sample (pre-shift, pre-sat):** exactly `0x5CD2632E = 1,557,291,822` (computed in research pass using `x[k] = +32767 if c > 0 else -32768`; slightly less than the `0x5CD30000` bound which used `|-32768|` on both sides). The bound `0x5CD30000` is computed assuming `|x| = 32768`, which requires `x = -32768` on BOTH sides of the sign split. Practically, the `x = +32767` ceiling is the achievable max for `c > 0` entries; the exact hit is `0x5CD2632E`.

**Post-shift output:** `0x5CD2632E >> 15 = 0xB9A4C` (truncation toward −∞). `sat_s16(0xB9A4C) = +0x7FFF` (saturates high). Overflow-tap (D-05) magnitude: `0xB9A4C - 0x7FFF = 0xB1A4D`.

**Invariant:** Accumulator value is exactly `0x5CD2632E` (assert bit-pattern). No UBSan signed-integer-overflow trap fires (assert via UBSan CI). Output is `+0x7FFF` (clamped). Overflow-tap field is incremented by exactly `0xB1A4D`.

**Threshold:** Bit-exact.

**Owning TU:** `tests/unit/fir/test_fir_overflow_proof.c`.

### 4. Frequency sweep (half-band lowpass shape)

**Input (44.1 kHz):** logarithmic sine sweep from 20 Hz to 22 kHz over 1 second (44100 samples), Hann-windowed at both ends, amplitude `+0x2000` (well below clipping).

**Expected output (decimator only, applied directly without interpolator or reverb):** frequency-response shape per the 39-tap half-band filter. Hand-derived via numpy FFT of the coefficient table; expected features:
- Passband: flat within ±0.5 dB from 0 Hz to ~10 kHz.
- Transition band: −3 dB at half-band cutoff = `11.025 kHz`; −6 dB around `11.5 kHz`; −40 dB below `~12 kHz`.
- Stopband: below `−70 dB` from `~13 kHz` to `22.05 kHz` (Nyquist).

**Invariant:** Spot-check at 6 frequencies — DC (0 Hz → 0 dB), 1 kHz (0 dB), 5 kHz (0 dB), 10 kHz (near 0 dB), 11.025 kHz (−3 dB approximate), 15 kHz (< −50 dB), 20 kHz (< −70 dB). Per-spot tolerance: ±3 dB (generous, since FFT bin quantization and windowing artifacts dominate at the transition band).

**Threshold:** ±3 dB at each spot-check frequency.

**Owning TU:** `tests/unit/fir/test_fir_freq_sweep.c` + numpy-based sweep generator in `tests/python/derive_fir_reference.py`.

### 5. Round-trip transparency

**Input (44.1 kHz):** band-limited audio. Spec: a `spu94_reference_band_limited.wav` test fixture containing 2 seconds of 1 kHz + 3 kHz + 5 kHz tones mixed (all below 10 kHz, well inside the passband), with smooth envelope.

**Expected output (through full 44.1 kHz wrapper with reverb bypassed):** input samples delayed by 38, attenuated by the cumulative DC-ish gain (≈ 0.977 for in-band signal) and slightly shaped by the passband ripple.

**Invariant:** For each output sample `y[n]`, `|y[n] - 0.977 · x[n-38]|` is below a threshold for all `n > 38` and `n < len(input) - 38`.

**Threshold:** The Q15 quantization floor is ~`2^-15 ≈ 30.5 μ` (linear) or `−90 dB` (relative). For 39 taps of Q15 accumulation + single shift-sat, the worst-case per-sample quantization is bounded by ~`39 · 2^-15 ≈ 1.19e-3` (linear) = `−58.5 dB`. **RECOMMENDED threshold: 4× the quantization floor = `−52 dB` = linear magnitude `2.5e-3` in `[-1, 1]` normalized terms = `~80` in int16 LSB terms.**

Alternative: compare RMS error to RMS input; expect `RMS_error / RMS_input < −50 dB` for in-passband signal.

**Owning TU:** `tests/unit/fir/test_fir_round_trip.c`. Fixture file in `tests/fixtures/spu94_reference_band_limited.wav` or generated inline in Python.

### 6. Bit-identity folded vs literal (D-01)

**Input (44.1 kHz):** 10⁵ samples of uniform-random int16 values + 1 adversarial sample (same as § 3 above).

**Expected output:** the folded-form `spu94_fir_decimate` (production) and the literal-form reference (compiled separately in the test binary as `spu94_fir_decimate_literal_reference`, visible only to the test) produce bit-identical int32 accumulator values on every sample.

**Invariant:** `TEST_ASSERT_EQUAL_INT32(acc_literal, acc_folded)` for every sample. Cumulative invariant: `TEST_ASSERT_EQUAL_INT16(y_literal, y_folded)` for every output sample.

**Threshold:** Bit-exact (no tolerance).

**Owning TU:** `tests/unit/fir/test_fir_bit_identity.c`. MUST compile with `SPU94_FIR_CASCADE_CLAMP` undefined (static-assert guard at the top of the TU).

### 7. Latency assertion (D-09)

**Input (44.1 kHz):** unit impulse (`+0x7FFF` at t=0, zero elsewhere, 80 samples).

**Expected output (full wrapper, reverb bypassed):** non-zero output starts appearing at t=19 (first taps reach the delay-line center); peak output at t=38.

**Invariant:** `TEST_ASSERT_EQUAL_UINT32(38, spu94_get_latency_samples())`. Additionally, the index of the peak absolute-value output matches `spu94_get_latency_samples()` within ±1 sample.

**Threshold:** Exact for the API return value; ±1 sample for the peak-index empirical check.

**Owning TU:** `tests/unit/fir/test_fir_latency.c`.

### 8. Err-tap / overflow-tap invariants (D-05 + D-06)

**Inputs (multiple small test vectors):**
- Sub-test A: 100 samples of input where the accumulated sum never reaches saturation (low amplitude, e.g., `±0x0040`).
- Sub-test B: 100 samples of adversarial input that drives the accumulator into saturation on every sample.
- Sub-test C: alternating low / high amplitude (baseline low, stress spikes at every 10th sample).

**Invariants (all three sub-tests):**
- Sub-test A: `err_fir_decimator == 0` (no truncation-remainder when input is low enough that the acc << 2^15) AND `fir_overflow_decimator == 0` (no saturation events).

  [Note: `err_fir_decimator == 0` holds only for inputs where `acc` is an exact multiple of `2^15`. Most sub-A inputs have nonzero low bits; the invariant is really that err is SMALL and monotonic-non-decreasing; a "zero" invariant requires specially-constructed inputs. RECOMMEND: relax to "err stays below a hand-derived upper bound determined by the input" for sub-A; strict zero only for specially-crafted inputs that are exact multiples of 2^15 per sample. Planner to formalize.]

- Sub-test B: `fir_overflow_decimator` increments on every saturation event by the expected magnitude; both `err_fir_*` and `fir_overflow_*` are monotonically non-decreasing; Python reference matches C output bit-for-bit.

- Sub-test C: cumulative err and overflow track the Python reference exactly.

**Threshold:** Bit-exact match to Python reference for all sub-tests.

**Owning TU:** `tests/unit/fir/test_fir_err_overflow_taps.c`.

### 9. Python ctypes fuzz (D-16)

**Input:** 10⁶ random int16 samples per channel, feeding the full 44.1 kHz wrapper chain via `spu94_fir_chain_step` (ctypes-wrapped; preceded by `spu94_init` / `spu94_reset` on a fresh state).

**Invariants:**
- No out-of-bounds access (detected by UBSan/ASAN running the shared lib via ctypes).
- Output samples are in `[INT16_MIN, INT16_MAX]`.
- `spu94_get_latency_samples() == 38`.
- err_fir_* and fir_overflow_* accumulators stay within the theoretical bounds (worst-case err ≤ `10⁶ · 2^15`, worst-case overflow ≤ `10⁶ · (2^31 - 2^15)`).
- Python-independent-reference chain produces bit-identical outputs on the same input stream (per-sample equality).

**Owning TU:** `tests/python/fuzz_fir.py`. Wired as a ctest target.

---

## Bibliography Additions

Phase 4 creates `docs/BIBLIOGRAPHY.md` (does not currently exist in the repo — prior ADRs reference it as a placeholder only).

Entries required:

```markdown
# SPU-94 — Bibliography

Facts-only citations for external sources consulted in SPU-94 research. All
prose in SPU-94's own docs paraphrases the sources listed here; transcribed
values are uncopyrightable facts (coefficient integers, register addresses,
bit-layout tables, sample rates). Per PROJECT.md licensing posture.

## Primary Sources

### BIB-001: nocash PSX SPU documentation
- **URL:** https://problemkaputt.de/psx-spx.htm (original author's hosting)
  + https://psx-spx.consoledev.net/soundprocessingunitspu/ (community render)
- **Author:** Martin "nocash" Korth + psx-spx community contributors
- **Used for:** SPU reverb formula (stage order, pseudocode, vIIR=-0x8000
  anomaly description, mBASE side-effect, BufferAddress wrap formula, 22.05
  kHz reverb rate, L/R time-multiplex, 39-tap FIR structure).
- **Caveat:** The psx-spx maintainers have publicly acknowledged that some
  content derives from Sony confidential materials. SPU-94 treats this
  source as a factual reference (uncopyrightable facts) and paraphrases
  any explanatory prose. Never transcribes full prose passages or table
  captions.

### BIB-002: jsgroth.dev PS1 SPU series (Part 3 — Reverb)
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Author:** jsgroth
- **Used for:** Behavioral witness (22.05 kHz clock alternation, independent
  L/R FIR deques, structural note that the same 39-tap FIR is reused for
  both decimate and interpolate, comb-sum intermediate precision flagged
  as unresolved).
- **Caveat:** Blog post does NOT reproduce the 39-tap FIR coefficient values
  — it points readers to psx-spx for the numbers. Cited as structural
  corroboration only.

## Coefficient Sources (Phase 4)

### BIB-005: PSX-SPX Reverb Buffer Resampling coefficient table (published form)
- **URL:** https://psx-spx.consoledev.net/soundprocessingunitspu/#reverb-buffer-resampling
- **Used for:** The 39-tap half-band FIR coefficient integer values for
  44.1 ↔ 22.05 kHz reverb resampling.
- **Provenance note:** psx-spx transcribes these values from the
  forums.bannister.org SCPH-5501 hardware readout (BIB-006). Every
  published source that lists the 39 integer values traces back to this
  single hardware reading.

### BIB-006: forums.bannister.org PS1 SPU FIR Coefficients thread
- **URL:** https://forums.bannister.org/ubbthreads.php?ubb=showflat&Number=71222
- **Used for:** Primary hardware readout of the 39-tap FIR coefficient
  values from a SCPH-5501 console (the reading that BIB-005 transcribes).
- **Provenance note:** Single hardware reading. Single-console (SCPH-5501)
  sourcing. Cross-console confirmation from independent revisions (SCPH-
  7502 / SCPH-9001 / etc.) is deferred to Milestone 5 hardware validation
  per the § Coefficient provenance audit in `.planning/phases/04-.../
  04-RESEARCH.md`.

### BIB-007: jsgroth.dev PS1 SPU Part 3 — structural corroboration for the FIR
- **URL:** https://jsgroth.dev/blog/posts/ps1-spu-part-3/
- **Used for:** Cross-reference that the 39-tap filter is implemented in
  the SPU silicon, that independent L/R deques are the correct state
  model (D-08), and that the same coefficient table is reused at both
  I/O boundaries (implication for CORE-07 = CORE-06 structural
  equivalence).
- **Note:** Does not publish the 39 coefficient values. Cited as
  structural / implementation-pattern source, not as a coefficient
  source.

## Witness Sources (output-only; source code NOT read as a primary input)

### BIB-008: lv2-psx-reverb
- **URL:** https://github.com/ipatix/lv2-psx-reverb
- **License:** GPLv3 (verified in the project's COPYING file per Phase 3
  research).
- **Used for:** OUT-OF-AXIS witness on the frequency-response axis
  (confirmed by the project's own README stating it does not downsample
  to 22050 Hz and acknowledging the resulting "additional brightness
  of the higher frequencies"). Valid IN-AXIS witness for reverb-network
  structural behavior (register semantics, comb/APF topology).

### BIB-009: Mednafen
- **URL:** https://mednafen.github.io/
- **License:** GPLv2.
- **Used for:** IN-AXIS or OUT-OF-AXIS classification on frequency-response
  pending the Phase 4 execution-pass empirical protocol (see
  `04-RESEARCH.md` § Empirical Mednafen/DuckStation investigation
  protocol). Source code is NOT read as a primary research input.

### BIB-010: DuckStation
- **URL:** https://github.com/stenzek/duckstation
- **License:** CC-BY-NC-ND (as of September 2024, per Phase 3 research
  finding; previously MIT-licensed).
- **Used for:** Same as BIB-009. Source code is NOT read as a primary
  research input; specifically under the CC-BY-NC-ND license the
  no-derivatives clause makes any source reading a licensing hazard.
```

---

## State of the Art

| Old approach | Current approach | When changed | Impact |
|--------------|------------------|--------------|--------|
| Assume psx-spx + jsgroth + hitmen c02 are three independent sources for the 39 coefficients | Coefficient provenance collapses to one hardware reading (SCPH-5501 via forums.bannister.org); psx-spx and jsgroth are mirrors / structural-only | This research pass 2026-04-20 | D-10 can still be honored by three-citation-in-bibliography but the research + ADR must be honest that the cross-reference is one-source-with-mirrors; cross-console hardware confirmation deferred to M5 |
| Nocash publishes the 39 coefficient values | psx-spx.consoledev.net DOES publish them (CONTEXT.md's "NOCASH IS NOT A VALID SOURCE for the coefficient values" finding is **contradicted** by this research pass — the psx-spx render, which is the community form of nocash's material, lists all 39 values verbatim) | This research pass 2026-04-20 | Minor finding — CONTEXT.md's bullet-item was defensive; the net result is unchanged (values are available and consistent). User may want to revise the CONTEXT note to say "we prefer community-sourced citations over the nocash render because the community render carries explicit attribution to the SCPH-5501 hardware readout, while the nocash render does not always surface the provenance chain" |

**Deprecated/outdated:**
- Nothing from the coefficient landscape is deprecated. The 39-tap half-band FIR is the authoritative bit-faithful filter.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The bannister.org forum thread is the original source of the 39 coefficient values; psx-spx transcribes it; no other hardware-readout source exists in the public landscape. | § Coefficient provenance audit | If an independent hardware readout exists (e.g., a PS2 / PSP test ROM that reads the SPU's FIR coefficient ROM at runtime and prints them, different from the bannister SCPH-5501 reading), the "one source in mirrors" finding is wrong. Mitigation: M5 hardware capture provides the second-source confirmation; Phase 4 ships flagging this explicitly. |
| A2 | The forums.bannister.org thread is currently accessible and will remain so. | § Bibliography (BIB-006) | If the forum goes dark, the primary citation link breaks. Mitigation: the 39 integer values are embedded in this research document + in `src/spu94/spu94_fir_coef.c` + in the Phase 4 ADR. The citation's integrity does not depend on the URL staying live. |
| A3 | The psx-spx community render faithfully transcribes the bannister values (no transcription error in psx-spx). | § Coefficient table | If psx-spx has a transcription error, SPU-94 inherits it. Mitigation: the bit-identity test against the literal reference form catches internal drift; the symmetry + half-band verification (computed in the research pass) cross-checks structural integrity. A single-bit transcription error in the middle of the table would likely break one of these structural tests. |
| A4 | Mednafen and DuckStation can be installed and driven via CLI / standalone mode for the empirical investigation. | § Empirical Mednafen/DuckStation protocol | If the emulators are not installable, classification is deferred past Phase 4. Mitigation: lv2-psx-reverb is already classified OUT-OF-AXIS, so SC-4's lv2-psx-reverb exclusion ADR can land regardless; Mednafen/DuckStation classification becomes a Phase 7 deliverable in the worst case. |
| A5 | The Phase 4 execution environment has a C compiler supporting `_Static_assert`, `int32_t`, and ADR-0001's arithmetic-right-shift guarantee — but this is already true of every supported Phase 1 target. | § Coefficient table, § Accumulator proof | If a Phase-4-target compiler is introduced that violates this, the Phase 1 `_Static_assert((-1 >> 1) == -1)` guard fails at build time and the port is blocked. Mitigation: already in place from Phase 1. |
| A6 | Circular-buffer delay line is bit-identical to shift-register delay line under the D-01 folded form. | § Pattern 2, § Decision Proposals — Circular-buffer vs shift-register | If a subtle ordering difference exists (e.g., the circular buffer reads oldest-to-newest while the shift-register reads newest-to-oldest and the pair-sum indexing gets swapped), outputs diverge. Mitigation: bit-identity test covers exactly this — if the circular-buffer production form diverges from the shift-register literal reference on any input, the test fails. |
| A7 | The D-06 per-multiply err-tap can be interpreted as an aggregate post-shift remainder in the clamp-once regime (see Pattern 1 note). | § Pattern 1 | If the user / planner want strict per-multiply err-tap semantics under D-03 (not the aggregate interpretation), the implementation must change — either engage D-04 cascade-clamp (which changes the numerical output) or add a second accumulator that tracks per-multiply shifts without changing the clamp-once regime (harder; requires carrying one err-tap accumulator per multiply and aggregating after the fact). Mitigation: flag this tension explicitly in the Phase-4-E ADR; the aggregate interpretation is the recommendation but the alternative is documented. |

---

## Common Pitfalls

### Pitfall 1: Mixing delay-line storage conventions between production and reference code

**What goes wrong:** Production FIR uses circular buffer (Pattern 2); Python reference (`derive_fir_reference.py`) uses shift register for audit clarity; C reference (`test_fir_bit_identity.c`) uses yet another convention. If the indexing conventions disagree, the bit-identity test and the Python-reference check both fail at the same sample with different error modes.

**Why it happens:** Three different implementations each pick what's easiest for their domain.

**How to avoid:** Document the exact indexing convention in `spu94_fir_internal.h` as a comment block. Python reference `derive_fir_reference.py` carries the same convention. C reference in `test_fir_bit_identity.c` uses the direct 39-multiply literal form (no circular buffer — just 39 explicit indexing lookups into a history buffer), which IS the semantic definition; the production folded form is proved equal to it.

**Warning signs:** Bit-identity test passes under random input but fails on adversarial input, or vice versa. Usually an off-by-one in the mod-39 index handling at the delay-line boundary.

### Pitfall 2: DC gain compensation (adding a `<< 1` to "fix" half-band half-gain)

**What goes wrong:** A developer notices the decimator has DC gain ≈ 1 (Σh ≈ 0x7FFE ≈ unity) but the interpolator has DC gain ≈ 0.5 (phase-0 sum + phase-1 sum = 0.488 average per output sample, approximately). Naturally they want to compensate by doubling the output. That would NOT be bit-faithful — the PS1 silicon does not compensate, and the spec facts say the same table is reused at both boundaries with no post-gain stage.

**Why it happens:** "Obviously a half-band interpolator doubles gain." Textbook assumption.

**How to avoid:** The SC-2 DC-round-trip test measures the cumulative gain analytically and asserts the exact observed value. Any post-gain compensation breaks that test.

**Warning signs:** The DC round-trip test shows perfect gain of 1.0 instead of the expected 0.977. Audibly, the reverb output gets louder than its host.

### Pitfall 3: Silently promoting int32 acc to int64 while "cleaning up"

**What goes wrong:** A developer, worried about the 0.46-bit margin, changes `int32_t acc` to `int64_t acc` without updating the D-02 ADR. The code still works and passes tests, but the MCU benchmark gets 2–3× slower on Cortex-M and the code-review reviewer sees drift from the ADR's stated behavior.

**Why it happens:** The 0.46-bit margin looks scary. Defensive upsize is tempting.

**How to avoid:** The D-02 ADR explicitly says int32 is the production choice and names the int64 seam as a *hedge*, not a default. Code review checklist item: any change to the accumulator type is an ADR-touching change.

**Warning signs:** MCU cross-compile benchmarks (Phase 8) show unexpected FIR-stage slowdown. Diff grep on `src/spu94/spu94_fir.c` for `int64_t` or `long long`.

### Pitfall 4: Forgetting the coefficient table SHA-256 sidecar check

**What goes wrong:** Someone (possibly the planner, possibly a future contributor) edits `spu94_fir_coef.c` by hand — maybe to "fix" a typo, maybe to apply a DC-gain correction, maybe because they copy-pasted from a different published source that had a transcription error. The table is 39 lines; a single-bit edit in the middle is visually invisible. All the structural tests (symmetry, half-band zero positions, center-tap value) could still pass while a non-zero tap's magnitude is off by one.

**Why it happens:** The table is long and boring; edits feel safe.

**How to avoid:** `tests/unit/fir/test_fir_coef_table.c` computes the SHA-256 hash of the 39-int16 byte array at test time and asserts it matches a hex string pinned in the test source. Any bit-level change to the table fails this test.

**Warning signs:** Bit-identity test fails but symmetry test passes. SHA-256 assertion fails on an otherwise-green CI.

### Pitfall 5: Decimator phase drift across resets

**What goes wrong:** The decimator's phase state (whether the next 44.1 kHz input sample produces a "retained" or "discarded" 22.05 kHz output) is a small stateful int in `struct spu94_state`. If `spu94_reset` doesn't zero it (or if Phase 4 adds it to the struct after the reset function already ran), the phase at the start of a new run is non-deterministic — the same input produces different outputs across resets.

**Why it happens:** Phase 2 Plan 01's `spu94_init` / `spu94_reset` zero the state wholesale via byte-loop; any new struct field added by Phase 4 is zeroed for free. BUT if a developer adds a new non-zero-initialized field (say, a magic sentinel), that skips the zero-fill.

**How to avoid:** Zero-initialize every new Phase-4 struct field. Add a `test_fir_reset.c` test: two runs of the full wrapper with identical input, `spu94_reset` between them; outputs bit-identical. This is a one-line invariant but catches phase-drift bugs.

**Warning signs:** Fuzz harness fails intermittently on the same random seed. Golden-file regression diffs (Phase 7) show off-by-one sample shifts.

### Pitfall 6: Using `spu94_set_reg_*` inside FIR stage functions

**What goes wrong:** Someone (defensively) adds a `spu94_set_reg_vLIN(state, computed_value)` inside the FIR to "apply" a volume adjustment. This breaks the register-state orthogonality (Phase 5's integration), and it interacts with Phase 2's pending-writes machinery in ways that violate the tick-atomicity principle.

**Why it happens:** Misreading the FIR stages as participating in register state.

**How to avoid:** The FIR stages READ from `struct spu94_state` (delay lines, err taps, overflow taps) and WRITE to the same struct. They NEVER call `spu94_set_reg_*`. Code-review grep: `grep -c 'spu94_set_reg_' src/spu94/spu94_fir.c src/spu94/spu94_io_chain.c` returns 0.

**Warning signs:** Non-zero count in the above grep. Phase 2 Plan 05's `fuzz_buffer.py` fails at a specific (seed, step) pair after Phase 4 lands.

### Pitfall 7: Interpolator phase-0 / phase-1 emission ordering

**What goes wrong:** The half-band interpolator produces two 44.1 kHz output samples per 22.05 kHz input pair. The ordering is "phase-0 first, then phase-1" (or the opposite, depending on convention). If the internal 44.1 kHz wrapper's phase state gets desynchronized from the interpolator's output ordering, the wrapper emits phase-1 when it should emit phase-0 and vice versa — the audio becomes a chopped stream with every-other-sample swapped.

**Why it happens:** Two state machines (wrapper's 44.1 kHz "next-sample-is-phase-N" counter + interpolator's own phase state) can desync.

**How to avoid:** The wrapper's phase-tracking state is a single int (0 or 1) in `struct spu94_state`; it is the ONLY phase state. The interpolator is stateless on the phase axis — it takes the phase as a parameter and computes either output.

**Warning signs:** Impulse-response test shows peak output at t=37 or t=39 instead of t=38. Frequency-sweep test shows a 22.05 kHz spectral image not predicted by the filter response.

### Pitfall 8: Zero-coefficient multiplication "optimization"

**What goes wrong:** The folded form has 19 coefficient pairs + 1 center. 9 of those pairs have a zero coefficient (`h[1] == h[3] == ... == h[17] == 0`). An optimizer notes "9 of 19 pairs contribute nothing" and removes them. Fine — except the removal has to be verified bit-identical, and if the optimizer happens to change the order of the remaining additions (e.g., for better register allocation), bit-identity still holds under int arithmetic but the order of the trailing test's `err_out +=` accumulations shifts. Under sharp-eye review, this could trigger a test failure on the aggregate err-tap.

**Why it happens:** Associativity of integer addition holds, but the SEQUENCE of partial sums does not — and an aggregate err-tap computed at a specific point in the sum is order-sensitive.

**How to avoid:** The production FIR code MUST use a fixed, documented summation order. The Python reference uses the same order. The bit-identity test confirms both match. Order is: center tap first, then k=0..18 non-zero pairs in ascending k order.

**Warning signs:** Aggregate err-tap values diverge from Python reference by exactly `(some pair multiplier) << 15`, which is a tell for order-of-summation drift.

---

## Code Examples

Verified patterns; see § Architecture Patterns above for the full set.

### Example 1: The 39-tap coefficient table (verbatim, facts-only, per D-11 + D-12)

See § Architecture Patterns → Pattern 1 for the full `spu94_fir_coef.c` listing. Key constraint: one coefficient per line, tap-index comment, no prose, no source citation inside the `.c` file (citation lives in `docs/BIBLIOGRAPHY.md`).

### Example 2: Folded-form FIR with clamp-once + err/overflow taps

See § Architecture Patterns → Pattern 1, `spu94_fir_decimate_fold` sketch above.

### Example 3: Circular-buffer delay-line helpers

See § Architecture Patterns → Pattern 2, `spu94_fir_delay_push` / `spu94_fir_delay_at` static inlines.

### Example 4: Public latency accessor

See § Architecture Patterns → Pattern 4.

### Example 5: Reverb-bypass test hook (from CONTEXT § Specifics)

```c
// In tests/unit/fir/test_fir_impulse.c (and similar TUs that need the
// wrapper without reverb contamination):
//
// Option (b) from CONTEXT § Specifics — test-only wrapper that chains
// decimate → interpolate directly, bypassing spu94_tick.
// Declared in spu94_fir_internal.h; defined in spu94_fir.c (test-only?
// or always available? Planner decides — if always-available, wrap in
// an SPU94_INTERNAL attribute so it doesn't escape to consumers).

void spu94_fir_chain_step_reverb_bypass(spu94_state *state,
                                        int16_t L_in, int16_t R_in,
                                        int16_t *L_out, int16_t *R_out)
{
    // Same as spu94_fir_chain_step, EXCEPT that instead of calling
    // spu94_tick() in the middle, we pass the 22.05 kHz signal through
    // unchanged. Used exclusively by tests that need to characterize the
    // FIR chain without reverb-network contamination.
    // ...
}
```

---

## Sources

### Primary (HIGH confidence)

- **BIB-005 / psx-spx Reverb Buffer Resampling** (https://psx-spx.consoledev.net/soundprocessingunitspu/) — 39-tap FIR coefficient values, 22.05 kHz reverb rate, L/R time-multiplex, FIR shared across both I/O boundaries. Three independent WebFetch / WebSearch confirmations during 2026-04-20 research pass.
- **BIB-006 / forums.bannister.org** (thread 71222) — original SCPH-5501 hardware readout. Accessed via WebSearch results during 2026-04-20 research pass; direct WebFetch blocked by certificate issue but content confirmed via WebSearch snippets that reproduce the exact hex values (byte-for-byte match with BIB-005).
- **BIB-002 / jsgroth.dev Part 3** (https://jsgroth.dev/blog/posts/ps1-spu-part-3/) — structural facts: 22.05 kHz alternation, independent L/R deques, same FIR at both boundaries, "rather strange" reuse observation. Direct WebFetch during research pass.
- **BIB-008 / lv2-psx-reverb README** (https://github.com/ipatix/lv2-psx-reverb) — OUT-OF-AXIS witness admission ("doesn't downsample the reverb to 22050 Hz" / "additional brightness of the higher frequencies"). Direct WebFetch during research pass.

### Secondary (MEDIUM confidence — structural corroboration or negative findings)

- **hitmen.c02.at/files/docs/psx/spu.txt** — does NOT document the FIR coefficients; covers reverb register layout only. Cited as a negative finding (rules out hitmen c02 as a D-10 third-source candidate).
- **psdevwiki** (https://www.psdevwiki.com/ps1/SPU) — WebFetch returned 403 during research pass; WebSearch results indicate the page exists and discusses SPU reverb but does not surface the coefficient table in snippets. Deferred.
- **emu-russia/psxrev wiki** (https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md) — does NOT include the FIR coefficient table; covers general SPU architecture. Cited as a negative finding.

### Tertiary (LOW confidence — not used as primary source per licensing posture)

- **Mednafen / DuckStation / MiSTer source code** — GPLv2 / CC-BY-NC-ND / MIT respectively; source not read as primary input per PROJECT.md. Their OUTPUT AUDIO is witness material for the Phase 4 execution-pass empirical investigation (§ Empirical Mednafen/DuckStation investigation protocol).

### Research pass tool calls (auditable trail)

- WebFetch: https://jsgroth.dev/blog/posts/ps1-spu-part-3/ — initial check (coefficients not listed in post; structural facts confirmed).
- WebFetch: https://psx-spx.consoledev.net/soundprocessingunitspu/ — 39 coefficient values confirmed verbatim.
- WebSearch: "PS1 SPU 39-tap half-band FIR coefficients int16 table SCPH-5501 reverb 2026" — bannister.org thread surfaced; coefficient values repeated in search snippets.
- WebFetch: https://hitmen.c02.at/files/docs/psx/spu.txt — coefficients not present.
- WebFetch: https://problemkaputt.de/psx-spx.htm — coefficients not present in the top-level landing page (WebFetch summarized the page as not containing filter coefficients; the actual SPU subpage is at `/soundprocessingunitspu/`).
- WebFetch: https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/soundprocessingunitspu.md — 39 coefficient values confirmed verbatim in community-maintained repo.
- WebSearch + multiple verifications: byte-for-byte match across all sources that publish the values.
- WebFetch: https://github.com/ipatix/lv2-psx-reverb — OUT-OF-AXIS admission in README confirmed.
- WebFetch: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md — coefficients not present.
- Python-computed verifications (Bash-run): table symmetry, half-band structure, accumulator bound, DC gain, folded-vs-literal bit-identity on 100 random + 1 adversarial trial, impulse response peak at latency 38.

---

## Metadata

**Confidence breakdown:**
- **Standard stack:** HIGH — Phase 4 adds zero new runtime deps; all primitives landed in Phases 1–3.
- **Architecture patterns:** HIGH — conform to established Phase 3 patterns (stage functions + internal header + bit-identity audit + err/overflow taps + Python reference script + fuzz harness).
- **Don't hand-roll:** HIGH — filter-design theory and sample-rate-conversion libraries all explicitly ruled out by the bit-faithfulness constraint; coefficient values are hardware facts.
- **Coefficient table (§ Coefficient table):** HIGH on the values themselves (every published source matches byte-for-byte). MEDIUM on the cross-reference breadth (see § Coefficient provenance audit — the three-source cross-reference collapses to one underlying hardware reading).
- **Accumulator width proof:** HIGH — derived by direct summation using the verified coefficient table; empirically validated in the research pass.
- **Bit-identity argument:** HIGH — algebraic proof + empirical validation (100 random + 1 adversarial trial pass; cascade-clamp variant demonstrated to break).
- **Witness analysis (lv2-psx-reverb):** HIGH — witness project's own README admits OUT-OF-AXIS.
- **Witness analysis (Mednafen / DuckStation):** MEDIUM — classification deferred to execution-pass; empirical protocol specified.
- **Test-vector library:** HIGH for the invariants + thresholds; MEDIUM for the specific numerical expected values which require execution of `tests/python/derive_fir_reference.py` (which does not yet exist; the planner / executor creates it with the values specified above).

**Research date:** 2026-04-20.
**Valid until:** ~2026-10-20 (6 months — coefficient values are frozen hardware facts that don't drift; the provenance audit could be invalidated by a new independent hardware readout; the witness classification for Mednafen / DuckStation could be invalidated by the empirical execution pass).
