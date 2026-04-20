# Phase 3: Core Reverb Algorithm + Hard Clip — Research

**Researched:** 2026-04-19
**Domain:** PS1 SPU reverb network semantics (SAME/DIFF IIR, 4-tap comb, APF1, APF2, scale stages); Q15 fixed-point saturation and truncation semantics in C; the vIIR=−0x8000 negation anomaly; hard-clip placement on the mix bus; bit-exact golden-vector test strategy.
**Confidence:** HIGH on the reverb pseudocode and stage order (direct nocash quote). HIGH on the vIIR=−0x8000 anomaly description (verbatim nocash sentence). HIGH on L-tick / R-tick alternation (verbatim nocash sentence). MEDIUM on comb-sum intermediate precision (nocash silent on accumulator width; DuckStation/Mednafen-PSX behavioral witness shows int32-accumulate-then-one-saturation). MEDIUM on hard-clip placement (nocash describes saturation only at "values written to memory"; placement of an explicit stage before SAME IIR is an SPU-94 design choice, not a spec dictate). HIGH on the licensing landscape (confirmed GPLv2/GPLv3/CC-BY-NC-ND across all major emulator witnesses).

## Summary

The nocash SPU Reverb Formula is unambiguous on structure: seven stages in a fixed order (input scale → SAME IIR → DIFF IIR → 4-tap COMB → APF1 → APF2 → output scale → buffer advance), and the L half and R half of each 22.05 kHz sample are processed on two consecutive 44.1 kHz clocks. The pseudocode is written as plain integer math with **no explicit `sat_s16()` wrappers on any line**; the sole saturation statement is a note — "The values written to memory are saturated to −8000h..+7FFFh" — appended separately. Two facts are primary-source locked: the vIIR=−0x8000 anomaly ("the multiplication by −8000h is still done correctly, but, the final result (the value written to memory) gets negated") and the L-then-R 44.1 kHz alternation ("The reverb hardware spends one 44100h cycle on left calculations, and the next 44100h cycle on right calculations"). [VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/]

Three questions remain research-gated because nocash does not resolve them at the level Phase 3 must commit:

- **Comb-sum precision (D-07):** nocash shows `Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2] + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]` with no intermediate saturation. jsgroth's Part 3 writeup explicitly flags this as unresolved: "all values are saturated to signed 16-bit before being written to RAM (and possibly also during some of the intermediate calculations — unclear to me)." [CITED: jsgroth.dev/blog/posts/ps1-spu-part-3/] The behavioral witness (DuckStation's port of Mednafen-PSX's reverb) accumulates all four products in `s32` and saturates once at the end — a single-pass saturation pattern. [CITED: github.com/stenzek/duckstation/commit/809b9f89] Phase 3's preliminary lean (int32 accumulate + final sat) matches this witness.

- **L/R register-write timing (D-08):** nocash establishes the L-tick precedes the R-tick by one 44.1 kHz cycle, but does not state whether registers are latched at the start of the L-tick for the subsequent R-tick, or read fresh by the R-tick. Real hardware behavior here is undocumented by nocash. The governing user directive ("AS CLOSE TO THE ORIGINAL PS1 SPU REVERB AS POSSIBLE") combined with the absence of a definitive hardware observation in accessible secondary sources means SPU-94 must pick a principled policy, document it, and mark it as a D-22 seam. Phase 3's preliminary lean (freeze v\* at tick start for both L and R) is defensible as the simplest mental model but is NOT primary-source-locked.

- **Hard-clip stage placement (D-09):** nocash places saturation only at "values written to memory." It does not prescribe an explicit clip between input-scale and SAME IIR. CORE-02 requires hard-clip behavior on the mix bus. Phase 3's preliminary lean (one explicit clip stage between input-scaling and SAME IIR, satisfying SC-2's "independently testable" requirement) is the SPU-94 design choice; it is consistent with, but not dictated by, the spec.

The vIIR=−0x8000 anomaly mechanism (D-10) resolves in favor of **an explicit branch** — nocash documents the observable effect (negation of the final memory-written result), not an emergent arithmetic mechanism. The DuckStation witness also uses explicit branching for the −0x8000 case. [CITED: github.com/stenzek/duckstation/commit/809b9f89]

No MIT/BSD/Apache-licensed C reference implementation of the PS1 SPU reverb was found in the public ecosystem. Every major emulator with visible reverb code is either GPLv2 (Mednafen, UPSE, PCSX-Redux), GPLv3 (lv2-psx-reverb — confirmed via the COPYING file), or recently-relicensed CC-BY-NC-ND (DuckStation, as of Sept 2024). This validates the PROJECT.md licensing posture: SPU-94 is built from the nocash spec with these emulators used only as behavioral witnesses.

**Primary recommendation:** Lock Phase 3 around four primary-source facts (stage order; L-then-R 44.1 kHz alternation; vIIR=−0x8000 negation of the memory-written result; saturation applied at memory write). Resolve D-07 in favor of int32 accumulate + single final saturation (MEDIUM-confidence behavioral alignment with DuckStation/Mednafen-PSX; seam-swappable via D-22 if M5 hardware capture diverges). Resolve D-09 as an explicit clip stage between input-scale and SAME IIR (design choice, seam-swappable). Resolve D-10 as an explicit branch on `vIIR == INT16_MIN` (matches what the spec documents — the observable effect). D-08 remains the weakest-supported decision; the lean (tick-latched freeze) should be marked in ADR-0008 with a "revisit under M5 hardware capture" note.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Test Isolation + Reverb Body Factoring (Area 3)**
- **D-01:** Each reverb stage is an internal-linkage function, declared in `src/spu94/spu94_reverb_internal.h`. Tests include this header directly (analogous to Phase 2's `spu94_state_internal.h`).
- **D-02:** One function per documented stage, L and R handled internally. Seven stage functions total: `spu94_reverb_input_scale`, `spu94_reverb_same_iir`, `spu94_reverb_diff_iir`, `spu94_reverb_comb`, `spu94_reverb_apf1`, `spu94_reverb_apf2`, `spu94_reverb_output_scale`.
- **D-03:** Output-scale is its own stage function (not folded into `spu94_tick`).
- **D-04:** Phase 3 does not add public read-only stage-output observers (deferred per D-21 reasoning).
- **D-05:** Single `src/spu94/spu94_reverb.c` TU holds all stage bodies + the top-level reverb-body caller invoked by `spu94_tick`.
- **D-06:** Reverb body inserted into `spu94_tick` between `spu94_apply_pending_writes` and `spu94_buffer_advance` (the "Phase 3 will add" comment placeholder in `src/spu94/spu94_tick.c`).

**Architectural Principles (carried forward from Phase 2)**
- **D-22 Extensibility Seams:** every gray-area resolution in Phase 3 is a pinnable mechanism.
- **D-23 Observability:** stage outputs are test-observable via internal header; no public mutation path.
- **D-24 Controllers as Future Consumer:** Phase 3 API design assumes a future Controllers layer consumes the reverb surface.
- **ADR-0001 (Q15 truncation direction):** every multiply in Phase 3 uses `q15_mul_truncate` / `q15_mul_truncate_with_err` — truncation toward −∞, bit-identical to Phase 1's locked semantic.
- **ADR-0002 (vIIR anomaly):** accepted as a reproduce-target.

### Claude's Discretion (within the locked decisions above)

- Exact C prototypes of the seven stage functions.
- Internal naming inside `spu94_reverb_internal.h`.
- Whether the reverb body caller is named `spu94_reverb_tick` or `spu94_reverb_body`.
- Whether input-scale applies `vLIN`/`vRIN` before or fused with the hard-clip stage.
- Test-file granularity under `tests/unit/reverb/`.
- Internal register-read convenience helpers inside the reverb body.

### Research-Gated Gray Areas (NOT yet locked — resolutions recommended below)

- **D-07:** Comb-sum intermediate accumulation precision → **RECOMMEND: int32 accumulate + single final saturation** (matches DuckStation/Mednafen-PSX witness; simplest to test; seam-swappable via D-22). See § Decisions Recommended.
- **D-08:** Register-write timing between L-tick and R-tick → **RECOMMEND: freeze v\* at tick start for both L and R** (primary-source-silent; principled default; seam-swappable; revisit under M5 hardware capture). See § Decisions Recommended.
- **D-09:** Hard-clip stage placement → **RECOMMEND: one explicit clip stage between input-scale and SAME IIR**, implemented as its own stage function (satisfies SC-2 "independently testable"; implicit `sat_s16` from Q15 multiplies still applies everywhere). See § Decisions Recommended.
- **D-10:** vIIR=−0x8000 anomaly mechanism → **RECOMMEND: explicit branch** `if (vIIR == INT16_MIN) result = -result;` at the memory-write point of each IIR stage (matches nocash's "final result gets negated" wording verbatim; matches DuckStation witness). See § Decisions Recommended.
- **D-11:** Per-multiply error-tap wiring scope → **RECOMMEND: scope (i) — all multiplies observable**, per-stage `int32` error accumulator in `spu94_state`. Justification: CONTEXT's test-vector-robustness directive + Phase 2's D-18 first-real-consumer framing; the per-multiply overhead is one int32 add; enables error-tap invariant tests that strengthen the TEST-07 battery.

### Deferred Ideas (OUT OF SCOPE)

- 39-tap half-band FIR at the 44.1 ↔ 22.05 kHz I/O boundary — Phase 4.
- `spu94_process` block-based public entry point — Phase 5.
- 10 factory reverb presets — Phase 5.
- Python ctypes bindings / wheel — Phase 6.
- Witness-diff harness against Mednafen/DuckStation — Phase 7.
- Golden-file regression tests — Phase 7.
- MCU cross-compile validation — Phase 8.
- Public read-only stage-output observability accessors — deferred per D-04.
- L/R granular split at the function level — deferred pending research.
- Frequency-response witness against lv2-psx-reverb — PROJECT.md Key Decisions explicitly excludes.
- Reverb work-buffer clear on power-on — Phase 5 concern.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CORE-02 | Hard clip / saturation behavior on the mix bus (-0x8000..+0x7FFF), matching hardware | § Nocash Primary Evidence confirms "The values written to memory are saturated to −8000h..+7FFFh" as the only explicit saturation statement. § Decisions Recommended D-09 proposes an explicit clip stage between input-scale and SAME IIR, with the implicit Q15 saturation everywhere else (from `q15_mul_truncate` + `sat_s16` helpers already in Phase 1). |
| CORE-05 | All-pass + comb filter network topology matching nocash's documented processing order | § Nocash Primary Evidence captures the processing order verbatim: input-scale → SAME IIR → DIFF IIR → 4-tap COMB → APF1 → APF2 → output-scale → buffer advance. The seven stage functions (D-02) map 1-to-1. |
| CORE-08 | Reproduce the documented vIIR=-0x8000 hardware anomaly (negates the final reverb result) | § Nocash Primary Evidence captures the verbatim sentence: "When set to −8000h, the multiplication by −8000h is still done correctly, but, the final result (the value written to memory) gets negated." § Decisions Recommended D-10 resolves via explicit branch at the memory-write point. |
| TEST-06 | vIIR=-0x8000 hardware anomaly specifically tested | § Test Strategy details the anomaly path + non-anomaly control (vIIR=INT16_MIN+1 proves the anomaly is specifically −0x8000 and nothing else). |
| TEST-07 | Fixed-point saturation, truncation, and overflow edge cases specifically tested | § Common Pitfalls enumerates the edges; § Test Strategy details the per-stage INT16_MIN / INT16_MAX / 0 / saturation-tripping battery plus the Q15 truncation re-assertion from ADR-0001. |

</phase_requirements>

## Project Constraints (from CLAUDE.md / PROJECT.md)

1. **User execution style (global CLAUDE.md):** the user is the hands-on operator. Phase 3 is pure-code-in-repo — no deployed systems — so the hands-on-walkthrough directive is a no-op here, but the planner should structure plans so the user can execute and verify each plan (compile, run tests, inspect failures) independently.
2. **No heap in core.** Phase 3 adds no heap symbols. All reverb state (per-stage error accumulators, work-buffer taps) lives in `struct spu94_state` or on the stack of the stage function. Verified by Phase 1's `verify-no-heap-symbols.sh`.
3. **No float/double in core.** Enforced by the Phase 1 grep guard. All reverb math is `int16_t` / `int32_t` via `q15_mul_truncate*` / `sat_s16` / `q15_add_sat`.
4. **No unqualified `long`.** Use `<stdint.h>` widths.
5. **Bit-faithful from spec, not from port.** Primary source: nocash psx-spx. Mednafen / DuckStation / lv2-psx-reverb / UPSE / PCSX-Redux source code is NOT read as a primary research input. Their outputs may be used as behavioral witnesses (deferred to Phase 7). A single high-level algorithmic comparison against DuckStation's Mednafen-PSX-derived reverb — at the level of "int32 accumulate vs cascading saturate" — is documented in this research as a MEDIUM-confidence cross-reference for D-07, not as source-transcription.
6. **nocash paraphrase discipline.** Facts (pseudocode formulas, variable names, register addresses, the verbatim vIIR−0x8000 and L/R-cycle sentences) are free to use and cited here. ADR-0007..ADR-0010 (the Phase 3 decision records) must paraphrase nocash's prose in SPU-94's own words per DOCS-03. Code comments likewise paraphrase.
7. **C99/C11 freestanding conformance.** Phase 3 adds no new public headers. The internal `spu94_reverb_internal.h` is src-only, never installed.
8. **Determinism flags in force.** `-ffp-contract=off`, `-fno-fast-math`, `-Werror` inherit via `spu94_warnings` INTERFACE target. No new flag surface.
9. **UBSan + `no_sanitize("integer")` policy (ADR-0003).** Functions where SPU two's-complement wrap is the intended SPU behavior get the attribute. Phase 3's reverb stages use saturation everywhere (no intentional wrap); no new `no_sanitize` attributes should be needed.
10. **Epistemic honesty (user feedback):** this research explicitly flags what is primary-source-locked vs witness-corroborated vs SPU-94-design-choice. "Here's what I know vs don't know" rather than false certainty.
11. **Announce official writes (user feedback):** the planner, when it lands ADR-0007..ADR-0010 in `docs/DECISIONS.md`, must announce intent before editing the durable artifact per the user's "no preview-then-approve dance" preference.

## Standard Stack

Phase 3 is pure fixed-point integer DSP in C. **There is no external library that should be added in this phase.** All primitives are already landed by Phase 1 and Phase 2.

### Core (already landed — reused)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `spu94_q15.h` Q15 helpers | Phase 1 (vendored in-repo) | `q15_mul_truncate`, `q15_mul_truncate_with_err`, `sat_s16`, `q15_add_sat` | [VERIFIED: `include/spu94/spu94_q15.h`, ADR-0001] Already implements the ADR-0001 truncation-toward-−∞ semantic and the `_with_err` pre-saturation remainder tap (ADR-0004). Phase 3 uses `q15_mul_truncate_with_err` for every multiply per D-11 scope (i). |
| `spu94_registers.h` + facade | Phase 2 | 35-register enum, hw_offset, typed I/O | [VERIFIED: `include/spu94/spu94_register_facade.h`] 105 `static inline` wrappers. Phase 3 reverb body reads via engine layer (`spu94_get_reg_i16`, `spu94_get_reg_u16`) for explicit register-intent readability. |
| `spu94_state_internal.h` | Phase 2 | Single ODR home for `struct spu94_state` | [VERIFIED: `src/spu94/spu94_state_internal.h`] Phase 3 adds per-stage `int32_t` error accumulator fields here if D-11 lands scope (i). Current sizeof = 168 B; headroom to `SPU94_STATE_SIZE_MAX` = 16216 B. Any reasonable Phase 3 additions fit. |
| Unity C test framework | Phase 1 (vendored) | Per-stage unit tests with inline reference tables | [VERIFIED: Phase 1 01-02-PLAN] Phase 3's stage tests follow the same inline-reference-table pattern as `tests/unit/q15/test_q15.c`. |
| CMake `spu94_obj` OBJECT library + determinism flags | Phase 1 | Flag-identical build of `.so` and `.a` | [VERIFIED: `src/spu94/CMakeLists.txt`] New `src/spu94/spu94_reverb.c` TU auto-picked up by existing source globs / explicit list. |
| CI grep guard / verify-no-heap / clang-tidy / cppcheck / UBSan | Phase 1 | Determinism + posture enforcement | [VERIFIED: `scripts/ci/*`] Phase 3 code passes all unchanged. |

### Core (new in Phase 3)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| None (zero new runtime deps) | — | — | [VERIFIED: CONTEXT Decisions + PROJECT.md "no external DSP libs"] Phase 3 is hand-written integer DSP. The seven stage functions, the reverb body caller, and the hard-clip stage are all implemented with the Phase 1/2 primitives. No third-party C code needed. The "right answer" is affirmative: **pure C int math is the correct stack choice here.** |

### Supporting (test-side only, non-shipped)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Python 3.10+ (ctypes) | 3.10+ floor (already in use by Phase 2 fuzz harness) | `tests/python/fuzz_reverb.py` — 10⁶ random register configurations + input samples | [VERIFIED: Phase 2 Plan 05 established the pattern, ctypes in stdlib since 2.5] Replicates `fuzz_buffer.py`'s shape. Invariants: no crashes, no unbounded growth, full-tick equals sequential stage composition. |
| `numpy` (Python, test-side only) | OPTIONAL — recommend ≥ 1.26 | Optional: derive expected stage outputs in Python to cross-check against the hand-derived C reference tables | [VERIFIED: Python 3.13.7 local env has numpy] NOT required; pure-Python-integer `fuzz_reverb.py` can compute the independent reference. Use numpy only if it measurably improves harness clarity. |

**Alternatives considered:**

| Instead of | Could use | Tradeoff |
|------------|-----------|----------|
| Hand-written integer DSP | CMSIS-DSP Q15 primitives (ARM) | CMSIS-DSP is Apache-2.0 — license-compatible, BUT: (a) targets Cortex-M SIMD; (b) its saturation/truncation semantics (rounding vs truncating) differ from nocash's documented SPU behavior; (c) would contaminate the MCU-portable claim with a toolchain-conditional dependency. **Reject.** Hand-roll Q15 to match spec exactly — already done in Phase 1. |
| Explicit clip stage (D-09 rec) | Implicit clip via `sat_s16` in every multiply | CORE-02 requires the clip stage be "independently testable." The implicit-only approach makes it inseparable from the stage functions. **Keep explicit stage.** |
| Int32 accumulate for comb (D-07 rec) | Cascading `sat_s16` after each add | Cascading saturation is equally valid per the spec's silence but diverges from the Mednafen-PSX/DuckStation behavioral witness. If M5 hardware capture prefers cascading, swap via the D-22 seam. **Default to int32 accumulate** for M1. |
| Explicit vIIR=−0x8000 branch (D-10 rec) | Emergent from IIR saturation arithmetic | Nocash describes the **observable effect** (final result negated), not an emergent mechanism. Attempting to reproduce it emergently requires extra constraints on every IIR operand that nocash does not specify — risks drift. **Default to explicit branch.** Auditable; TEST-06 is one line. |

**Installation:** none — no new packages. Phase 3 code is new files under `src/spu94/` and `tests/unit/reverb/` + one file under `tests/python/`.

**Version verification:** N/A — no new packages. Existing Phase 1/2 primitive versions are pinned by the vendored copies in `third_party/unity/` and the CMake build graph. Python 3.13.7 confirmed via `python3 --version` in prior-phase research.

## Architecture Patterns

### Recommended Project Structure (new files under existing dirs)

```
src/spu94/
├── spu94_reverb.c                  # NEW. The seven stage function bodies +
│                                   # the top-level reverb body caller invoked
│                                   # from spu94_tick. One-concern TU.
└── spu94_reverb_internal.h         # NEW. Src-only header. Declares the seven
                                    # stage functions so tests can include it
                                    # without exposing them on a public header.

tests/unit/reverb/                  # NEW directory.
├── test_reverb_input_scale.c       # Per-stage unit test. Inline reference
├── test_reverb_hard_clip.c         # table: {register_config, input,
├── test_reverb_same_iir.c          # expected_output}. INT16_MIN / INT16_MAX /
├── test_reverb_diff_iir.c          # 0 / saturation-tripping. Plus vIIR=-0x8000
├── test_reverb_comb.c              # anomaly + non-anomaly control in
├── test_reverb_apf1.c              # test_reverb_same_iir.c (and/or
├── test_reverb_apf2.c              # test_reverb_diff_iir.c).
├── test_reverb_output_scale.c
├── test_reverb_body.c              # Full-tick-vs-stage-composition
│                                   # equivalence test.
└── test_reverb_edges.c             # TEST-07 Q15 saturation / truncation /
                                    # signed-overflow battery, every stage.

tests/python/
└── fuzz_reverb.py                  # 10^6 random register configs + input
                                    # samples. Follows fuzz_buffer.py's pattern
                                    # ($<TARGET_FILE:spu94_shared> env var +
                                    # independent Python state model).

docs/
└── DECISIONS.md                    # Phase 3 APPENDS (prepends at line 33 per
                                    # established style): ADR-0007 (comb-sum
                                    # precision), ADR-0008 (L/R write-timing),
                                    # ADR-0009 (hard-clip placement), ADR-0010
                                    # (vIIR anomaly mechanism). Some may
                                    # collapse into fewer ADRs at planner
                                    # discretion.
```

### Pattern 1: Seven Stage Functions, Each with L and R Internal

**What:** Each documented nocash stage is one C function. Inside, it runs the L calculation and then the R calculation per nocash's pseudocode line order. The function mutates `spu94_state` (writes to the reverb work buffer, updates per-stage error accumulators if D-11 scope (i)). It reads registers via the engine layer, not the facade.

**When to use:** Every reverb stage, Phase 3. This is D-02's locked shape.

**Example (paraphrased from nocash, SPU-94 variable naming):**

```c
// Source: psx-spx.consoledev.net/soundprocessingunitspu/
// Nocash pseudocode line (verbatim):
//   [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]  ;L-to-L
//   [mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]  ;R-to-R
//
// SPU-94 implementation (integer math, Q15 truncation per ADR-0001,
// register reads via engine layer, explicit comments on every multiply):
static void spu94_reverb_same_iir(spu94_state *state,
                                  int16_t Lin, int16_t Rin)
{
    // L side:
    int16_t vIIR  = spu94_get_reg_i16(state, SPU94_REG_vIIR);
    int16_t vWALL = spu94_get_reg_i16(state, SPU94_REG_vWALL);
    uint16_t dLSAME = spu94_get_reg_u16(state, SPU94_REG_dLSAME);
    uint16_t mLSAME = spu94_get_reg_u16(state, SPU94_REG_mLSAME);
    int16_t tap_dLSAME   = reverb_buf_read(state, dLSAME);
    int16_t tap_mLSAME_2 = reverb_buf_read(state, mLSAME - 2);
    int16_t wall_prod = q15_mul_truncate_with_err(
        tap_dLSAME, vWALL, &state->err_same_iir);
    int16_t acc = q15_add_sat(Lin, wall_prod);
    acc = q15_add_sat(acc, -tap_mLSAME_2); // subtract
    int16_t iir_prod = q15_mul_truncate_with_err(
        acc, vIIR, &state->err_same_iir);
    int16_t result = q15_add_sat(iir_prod, tap_mLSAME_2);
    // D-10: explicit vIIR=INT16_MIN anomaly branch (nocash: "final result gets negated")
    if (vIIR == INT16_MIN) result = -result; // beware INT16_MIN negation: sat_s16 clamps
    reverb_buf_write(state, mLSAME, sat_s16(result));
    // ...R side mirrors the above with dRSAME / mRSAME / Rin...
}
```

Note: `reverb_buf_read` / `reverb_buf_write` are Phase 3 static inline helpers in `spu94_reverb.c` that wrap the Phase 2 buffer-address arithmetic with the correct `BufferAddress + offset` computation and 0x7FFFE mask. The `-tap_mLSAME_2` negation itself can hit INT16_MIN (another Q15 edge); use `(int16_t)-(int32_t)tap_mLSAME_2` guarded by `sat_s16` or equivalent to avoid UB. Flag in the edge battery (TEST-07).

### Pattern 2: Hard-Clip Stage Between Input-Scale and SAME IIR

**What:** A dedicated `spu94_reverb_hard_clip` stage function that takes `(Lin, Rin)` from `spu94_reverb_input_scale` and returns clipped `(Lin', Rin')` into the SAME IIR stage. Body is trivial — `sat_s16(Lin)` — but exists as its own function for independent testability (SC-2).

**When to use:** Every reverb tick, between input-scale and SAME IIR. This is D-09's recommended resolution.

**Example:**

```c
static void spu94_reverb_hard_clip(int32_t Lin_wide, int32_t Rin_wide,
                                   int16_t *Lin_out, int16_t *Rin_out)
{
    *Lin_out = sat_s16(Lin_wide);
    *Rin_out = sat_s16(Rin_wide);
}
```

Rationale: input-scale multiplies `vLIN * LeftInput` can produce a product wider than int16; the clip at this boundary is the "mix bus hard clip" CORE-02 calls out. The implicit clips inside every Q15 multiply elsewhere in the pipeline are covered by `q15_mul_truncate`'s own saturation (ADR-0001).

### Pattern 3: Int32 Accumulator for Comb Sum, Single Final Saturation

**What:** The 4-tap comb sum `Lout = vCOMB1*[mLCOMB1] + ... + vCOMB4*[mLCOMB4]` uses an `int32_t` accumulator across all four products, with one `sat_s16` at the end before storing `Lout` as the input to APF1.

**When to use:** Inside `spu94_reverb_comb`. This is D-07's recommended resolution.

**Example:**

```c
static void spu94_reverb_comb(spu94_state *state,
                              int16_t *Lout_out, int16_t *Rout_out)
{
    // Read four coefficients:
    int16_t v1 = spu94_get_reg_i16(state, SPU94_REG_vCOMB1);
    int16_t v2 = spu94_get_reg_i16(state, SPU94_REG_vCOMB2);
    int16_t v3 = spu94_get_reg_i16(state, SPU94_REG_vCOMB3);
    int16_t v4 = spu94_get_reg_i16(state, SPU94_REG_vCOMB4);

    // Read four L taps (similar for R):
    int16_t tL1 = reverb_buf_read(state, mLCOMB1);
    /* ... */

    // Four Q15 products, each saturates individually (bit-faithful to the
    // "every multiply implies /8000h with saturation" note in nocash):
    int32_t p1 = (int32_t)q15_mul_truncate_with_err(v1, tL1, &state->err_comb);
    int32_t p2 = (int32_t)q15_mul_truncate_with_err(v2, tL2, &state->err_comb);
    int32_t p3 = (int32_t)q15_mul_truncate_with_err(v3, tL3, &state->err_comb);
    int32_t p4 = (int32_t)q15_mul_truncate_with_err(v4, tL4, &state->err_comb);

    // Sum in int32 — no overflow possible (4 * INT16_range fits in int32):
    int32_t sumL = p1 + p2 + p3 + p4;

    // Single final saturation (D-07):
    *Lout_out = sat_s16(sumL);
    // ...R side mirrors...
}
```

This is the seam-structured shape. If M5 hardware capture later shows the SPU saturates between adds, replace the single `sat_s16(sumL)` with cascading `sat_s16(p1 + p2)` → `sat_s16(prev + p3)` → `sat_s16(prev + p4)` without touching the calling code. The D-22 seam is the function body.

### Pattern 4: Tick-Latched v* Register Snapshot (D-08 recommendation)

**What:** At the start of the full 22.05 kHz tick body (which covers the L-tick and R-tick together in SPU-94's `spu94_tick` model), read all `v*` registers once into local variables. Use those snapshots for both the L-half and R-half calculations. Do NOT re-read registers between the SAME IIR L-side and R-side.

**When to use:** Every reverb tick body. This matches D-08's recommended "freeze v* at tick start" policy.

**Example:**

```c
void spu94_reverb_body(spu94_state *state)
{
    // Freeze v* snapshot for the whole tick (D-08):
    const int16_t vIIR_snap  = spu94_get_reg_i16(state, SPU94_REG_vIIR);
    const int16_t vWALL_snap = spu94_get_reg_i16(state, SPU94_REG_vWALL);
    /* ... all v* registers ... */

    // Stage calls receive snapshots, not live-read registers:
    int32_t Lin_wide, Rin_wide;
    spu94_reverb_input_scale(state, &Lin_wide, &Rin_wide);
    int16_t Lin, Rin;
    spu94_reverb_hard_clip(Lin_wide, Rin_wide, &Lin, &Rin);
    spu94_reverb_same_iir(state, Lin, Rin, vIIR_snap, vWALL_snap);
    /* ... */
}
```

This seam is swappable: if M5 reveals the hardware reads `v*` fresh for R after L, the `spu94_reverb_body` caller re-reads them between L-phase and R-phase subroutines. The stage functions themselves don't change signature — they take `v*` as parameters either way.

### Anti-Patterns to Avoid

- **Floating-point anywhere in the reverb stages.** Grep guard will catch it; reject at code review before CI does.
- **Branchless-clever tricks that change saturation semantics.** The spec is clear — saturate. Implementations like `(x ^ 0x8000) - 0x8000` or `(x > 0x7FFF) ? 0x7FFF : x` are acceptable only if they produce bit-identical output to `sat_s16`; test them in the edge battery.
- **Re-reading `v*` registers between the L-half and R-half of a tick** — unless this is explicitly the policy (it's not, per D-08 recommendation). The seam is at the body-caller level; stage functions take snapshots as parameters.
- **Inlining the reverb body into `spu94_tick`.** D-05 locks one TU, one top-level caller. Preserves Phase 2's Pitfall 4 (single-call-site) discipline.
- **Depending on compiler-specific `__builtin_add_overflow`** — Phase 1's `sat_s16` already handles this portably. Use existing primitives.
- **Hand-coding the COMB sum as `sat_s16(sat_s16(sat_s16(p1+p2)+p3)+p4)` when D-07's resolution is int32 accumulate.** Match the ADR exactly; document the seam.
- **Using `q15_mul_truncate` (NULL err_out) in Phase 3.** Per D-11 scope (i) recommendation, all Phase 3 multiplies use `q15_mul_truncate_with_err` wired to a per-stage error accumulator. This is the seam that makes the Error Accumulator concept (D-18) testable in Phase 3.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Q15 multiply with saturation + truncation | A new `mul_q15` function | `q15_mul_truncate_with_err` from `include/spu94/spu94_q15.h` | Already lands ADR-0001's truncation direction and ADR-0004's pre-sat remainder. Any new multiply helper would have to be audited against the identical test table. |
| 16-bit saturation | A new `clamp16` function | `sat_s16` from the same header | Already vetted; one ODR home. |
| Saturating add | A new `add_s16_sat` | `q15_add_sat` | Same reason. |
| Register I/O | Direct `state->reg_values[]` access | `spu94_get_reg_i16` / `spu94_get_reg_u16` engine accessors | Phase 2 Plan 03's typed accessors enforce signed/unsigned discipline; direct array access bypasses the type guard. Facade wrappers OK for uniform read-shorthand if preferred — planner decides. |
| Buffer wrap arithmetic | A new wrap function | Phase 2's `spu94_buffer_advance` semantics + inline helper `reverb_buf_read/write` in `spu94_reverb.c` that uses `(BufferAddress + offset) & 0x7FFFE` with the mBASE floor as Plan 02-04 documented | Phase 2 Plan 05's 10⁶-step fuzz harness is the regression protection; a new implementation here would have to prove equivalence under that fuzz. |
| Test harness | A new test framework | Unity (already vendored) + the inline-reference-table pattern from `tests/unit/q15/test_q15.c` | Zero net new deps; pattern is established. |
| Python ctypes stateful-fuzz harness | A new harness | `tests/python/fuzz_buffer.py`'s template | Pattern established in Phase 2 Plan 05. Independent Python state model validates against the C core; divergence in either direction fails. |
| SPU reverb algorithm | Your own algorithm | **Nocash pseudocode — paraphrased into C using SPU-94's own variable naming** | This is the whole Core Value. Phase 3 does not invent; it transcribes facts from spec while paraphrasing prose. |
| vIIR=−0x8000 anomaly | Emergent saturation hack | Explicit branch `if (vIIR == INT16_MIN) result = -result;` guarded by `sat_s16` to handle the INT16_MIN → +INT16_MAX clamp | Matches what nocash describes (observable effect, not mechanism). Matches DuckStation witness. Auditable; one line; TEST-06 trivial. |
| Hard-clip semantics | A custom asymmetric clip | `sat_s16` (produces [−0x8000, +0x7FFF] range per nocash) | Confirmed symmetric per spec: "saturated to −8000h..+7FFFh." `sat_s16` is the vetted primitive. |

**Key insight:** Phase 3's custom-code budget is **zero new primitive functions**. Everything is composition of Phase 1/2 primitives per nocash's pseudocode. The novel code is in the *ordering* (seven stage functions that match nocash's line order), not in any new fixed-point utility. Any temptation to write a new multiply/saturate helper is a smell — audit it against the existing primitive.

## Runtime State Inventory

> Phase 3 is NOT a rename / refactor / migration — it adds new code. Section skipped per the section's own trigger condition. No stored data, live service config, OS-registered state, secrets, or build artifacts carry embedded strings that need updating as a result of Phase 3.

**Nothing found in any category:** verified — this is a greenfield phase. Only exception: `docs/DECISIONS.md` gets new ADR entries (ADR-0007..ADR-0010) prepended at line 33 per the established Phase 2 style. That is an ordinary commit, not a "state migration."

## Common Pitfalls

### Pitfall 1: INT16_MIN negation is UB if done naively

**What goes wrong:** `-(int16_t)-0x8000` → signed overflow, undefined behavior under C11 §6.5p5. Applies to:
- The `- [mLSAME-2]` subtract in the SAME IIR pseudocode if `[mLSAME-2] == INT16_MIN`.
- The `Lout - vAPF1*...` subtract in APF1 if the product is INT16_MIN.
- The `result = -result` explicit branch for vIIR=−0x8000 if `result == INT16_MIN`.

**Why it happens:** Two's-complement range is asymmetric; +0x8000 is not representable in int16. The C standard makes the overflow UB (not implementation-defined) for signed types. UBSan will flag it at runtime under ADR-0003's policy.

**How to avoid:** Do the negation in int32 and then `sat_s16` back: `(int16_t)sat_s16(-(int32_t)x)`. This maps −0x8000 → +0x7FFF (the documented saturation). Use `q15_add_sat(a, sat_s16(-(int32_t)b))` for subtraction.

**Warning signs:** UBSan signed-integer-overflow report. Divergence from nocash-derived reference values at inputs where a sample equals INT16_MIN. Test with explicit input = INT16_MIN at every subtract and negate site.

### Pitfall 2: C signed-overflow in the COMB int32 accumulator is unreachable — but prove it

**What goes wrong:** The 4-tap sum `int32_t sumL = p1 + p2 + p3 + p4` where each `p` is a Q15 product saturated to ±INT16_MAX. Maximum absolute value ≈ 4 × 0x7FFF ≈ 0x1FFFC — fits comfortably in int32. No overflow. BUT: if a future refactor changes the product range (e.g., removing the per-multiply `sat_s16`), the bound breaks silently.

**Why it happens:** Silent invariant. Nothing at the language level enforces "these four products are pre-saturated."

**How to avoid:** Add a `_Static_assert` or runtime `assert` in `spu94_reverb_comb` that documents the bound: `/* each p fits in [-0x7FFF, +0x7FFF]; sum fits in int32 */`. Better: use `q15_mul_truncate_with_err` (already saturates to int16) and widen explicitly: `int32_t p = (int32_t)q15_mul_truncate_with_err(v, t, &err);`. The explicit cast documents intent.

**Warning signs:** Any change to the comb sum that introduces a pre-saturation gap. UBSan will catch actual overflow; the grep for `int32_t sumL` in code review catches intent drift.

### Pitfall 3: Arithmetic right-shift vs integer division for Q15 multiply

**What goes wrong:** `(int32_t)a * b >> 15` in C — for negative products, `>>` is implementation-defined pre-C23, and integer division rounds toward zero. These give different results for negative half-LSB cases. ADR-0001 already pinned arithmetic-right-shift truncation-toward-−∞.

**Why it happens:** Naive port from pseudocode or from a different emulator that chose the opposite convention.

**How to avoid:** Use `q15_mul_truncate` / `q15_mul_truncate_with_err`. NEVER use `* b >> 15` directly in reverb code. The `_Static_assert` in `spu94_q15.h` (per Phase 1) guarantees arithmetic-right-shift on every toolchain; reverb code inherits it for free.

**Warning signs:** `grep -rn '>> 15' src/spu94/` should return zero hits outside `spu94_q15.h` after Phase 3 lands. Add this to the code-review checklist.

### Pitfall 4: Re-reading `v*` registers between L-tick and R-tick

**What goes wrong:** If `spu94_reverb_same_iir` calls `spu94_get_reg_i16(SPU94_REG_vIIR)` once for L and again for R, and the host writes vIIR via the engine's IMMEDIATE policy between the two reads (e.g., from a non-SPU-94 callback), L and R see different vIIR values. This is outside the D-08 policy envelope and not reproducible.

**Why it happens:** Engine IMMEDIATE writes land synchronously (Phase 2 Plan 03 / ADR-0005). Between any two consecutive `get_reg` calls, a concurrent write could intervene.

**How to avoid:** Pattern 4 above. Read `v*` once at the top of `spu94_reverb_body`; pass snapshots down to stage functions as parameters. Stage functions do not call `get_reg` for `v*` registers. They MAY call `get_reg` for `d*` / `m*` (address-type) registers, which are TICK_LATCHED per ADR-0005 and therefore frozen for the duration of the tick flush anyway.

**Warning signs:** Any stage function signature that omits the `v*` snapshot parameter and instead calls `spu94_get_reg_i16(state, SPU94_REG_v*)` internally. Flag at code review.

### Pitfall 5: The vIIR=−0x8000 branch must run AFTER all intermediate saturation, not before

**What goes wrong:** If the anomaly branch runs before the final `sat_s16(result)`, the negation can land in the saturated range and appear to work but produce a different value than hardware produces. Nocash says "the final result (the value written to memory) gets negated" — emphasis on "written to memory." The negation is the LAST thing before the store.

**Why it happens:** Misreading the spec; placing the branch at the wrong point in the stage.

**How to avoid:** In every IIR stage, the order is: compute `result` (including the final `q15_add_sat`), then `if (vIIR_snap == INT16_MIN) result = sat_s16(-(int32_t)result);`, then `reverb_buf_write(..., result)`. TEST-06 must exercise both the anomaly path (vIIR = INT16_MIN) and a non-anomaly control (vIIR = INT16_MIN + 1) to prove the anomaly is specifically INT16_MIN and nothing else.

**Warning signs:** Divergence from a non-anomaly control test. UBSan flag on the negation.

### Pitfall 6: Mid-tick register flush ordering

**What goes wrong:** Phase 2 ADR-0005's tick-flush order is: `spu94_apply_pending_writes` (first line of `spu94_tick`) → then the reverb body. TICK_LATCHED registers are visible to the reverb body with their NEW values. If Phase 3 accidentally re-triggers a flush mid-body (e.g., via `spu94_set_reg_*` inside a stage function — which should never happen), a second flush corrupts the tick's atomicity.

**Why it happens:** A stage function calling a setter as a side effect. There's no reason for this in normal reverb code, but defensive programming matters.

**How to avoid:** Stage functions are read-only on register state. They write only to the work buffer and to `state->err_*`. Code review: grep for `spu94_set_reg` in `src/spu94/spu94_reverb.c` — should return zero hits.

**Warning signs:** Any setter call in a reverb stage. Zero-tolerance — fails code review.

### Pitfall 7: Q15 edge cases in the APF1/APF2 feedback loop

**What goes wrong:** The APF structure `Lout = Lout - vAPF1*[tap]; [store] = Lout; Lout = Lout*vAPF1 + [tap]` has two Q15 multiplies with the same coefficient, and the intermediate `Lout` (written to memory AND fed forward) can be near-INT16_MIN in edge cases. The second multiply `Lout*vAPF1` compounds the saturation.

**Why it happens:** APF is a recursive structure; feedback amplifies edge-case drift.

**How to avoid:** Test each APF stage with `Lin = INT16_MIN`, `vAPF1 = INT16_MIN`, and a pre-seeded tap buffer at INT16_MIN. Compare output bit-for-bit to the hand-derived reference. This is one row in the `test_reverb_apf1.c` table.

**Warning signs:** Any APF test failure at INT16_MIN. The `_with_err` remainder stream should light up on APF edges — zero err is an invariant for non-saturating input.

### Pitfall 8: Buffer-tap read before `spu94_buffer_advance`

**What goes wrong:** D-06 pins the insertion order: `apply_pending_writes → reverb_body → buffer_advance`. The reverb body's tap reads and writes use `state->buffer_address` as it was at the START of the tick, not after the advance. If a future refactor swaps `buffer_advance` before `reverb_body`, every address shifts by +2 and every tap is wrong.

**Why it happens:** Refactor drift. Someone moves statements in `spu94_tick.c` "for clarity."

**How to avoid:** `spu94_tick.c` body has comments pinning the invariant. Add a test in `test_reverb_body.c` that explicitly asserts `state->buffer_address` is unchanged across the reverb body (i.e., the body reads/writes but does not advance). Phase 2 Plan 05's fuzz harness will also catch it at a specific (seed, step) pair if the order drifts.

**Warning signs:** Phase 2 fuzz harness divergence. Plan-03's tick-flush test seeing mysterious off-by-2 errors.

### Pitfall 9: Hand-derived reference tables derived from GPL-emulator output

**What goes wrong:** The planner or executor, unable to easily derive expected stage outputs from nocash pseudocode by hand, runs Mednafen or DuckStation with crafted inputs and captures the outputs as "reference values." This contaminates the test with GPL-derived data. Per PROJECT.md licensing posture, this is NOT acceptable.

**Why it happens:** Shortcut when hand-derivation is tedious. Especially tempting for APF1/APF2 which have feedback.

**How to avoid:** Every reference value in `tests/unit/reverb/` is derived in one of two ways — (a) by hand from nocash pseudocode with the derivation shown in a comment or a linked `docs/derivations/` markdown file, (b) by a Python script `tests/python/derive_reverb_reference.py` that implements nocash pseudocode in Python independently (not a port of any C emulator). The Python script is version-controlled; its output is the source of truth for the test tables. No GPL emulator may be in the derivation chain.

**Warning signs:** A test value with no derivation comment. A test value that matches Mednafen output to the bit but does not match a hand-derived computation. Code review must challenge any table entry that lacks derivation provenance.

## Code Examples

Verified-from-nocash patterns, paraphrased into SPU-94 idiom. All nocash prose is paraphrased per DOCS-03; pseudocode lines are transcribed as facts per nocash's bibliographic convention.

### Example 1: The SAME IIR Line (Primary Source, Paraphrased)

```c
// NOCASH PSEUDOCODE (source: psx-spx.consoledev.net/soundprocessingunitspu/):
//   [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]  ;L-to-L
//
// Paraphrase: compute a weighted input (current Lin plus previous same-side
// buffer tap scaled by vWALL), subtract the 2-samples-ago value at mLSAME,
// multiply by vIIR, add the 2-samples-ago value back, store at mLSAME.

// SPU-94 implementation (see Pattern 1 for full body).
// Uses q15_mul_truncate_with_err (ADR-0004), q15_add_sat (Phase 1), sat_s16.
// vIIR=INT16_MIN branch at memory-write point (D-10, nocash: "final result gets negated").
```

### Example 2: Hard-Clip Stage (D-09 resolution)

```c
// CORE-02 independently testable hard-clip on the mix-bus input path.
// No saturation wrapper appears on this line in nocash pseudocode, BUT
// nocash's saturation note ("values written to memory are saturated to
// -8000h..+7FFFh") and the physical requirement that Lin/Rin be int16
// before feeding SAME IIR justify an explicit stage here.
//
// Source: nocash "Input from Mixer" section (Lin = vLIN*LeftInput, Rin = vRIN*RightInput).

static void spu94_reverb_hard_clip(int32_t Lin_wide, int32_t Rin_wide,
                                   int16_t *Lin_out, int16_t *Rin_out)
{
    *Lin_out = sat_s16(Lin_wide);
    *Rin_out = sat_s16(Rin_wide);
}
```

### Example 3: 4-Tap Comb (D-07 resolution — int32 accumulate, single final sat)

See Pattern 3 above.

### Example 4: The vIIR = INT16_MIN Anomaly Branch (D-10 resolution)

```c
// NOCASH (verbatim):
//   "vIIR works only in range -7FFFh..+7FFFh. When set to -8000h, the
//    multiplication by -8000h is still done correctly, but, the final
//    result (the value written to memory) gets negated"
//
// Source: psx-spx.consoledev.net/soundprocessingunitspu/
//
// D-10 resolution: explicit branch at the memory-write point of every IIR
// stage. Matches nocash's wording ("final result gets negated") verbatim.
// Matches DuckStation/Mednafen-PSX behavioral witness (explicit branch).
//
// The int32 widening for the negation avoids INT16_MIN-negation UB
// (Pitfall 1); sat_s16 clamps the result to valid Q15 range.

if (vIIR_snap == INT16_MIN) {
    result = (int16_t)sat_s16(-(int32_t)result);
}
reverb_buf_write(state, mLSAME, result);
```

### Example 5: Tick-Latched v* Snapshot (D-08 resolution)

See Pattern 4 above.

## Nocash Primary Evidence (verbatim)

This section records the primary-source evidence on which Phase 3's decisions rest. **These are verbatim quotes from psx-spx for traceability and ADR provenance only.** Per DOCS-03, the ADRs themselves must paraphrase.

### E1: Full SPU Reverb Formula (verbatim from psx-spx / nocash)

```
;Input from Mixer (Input volume multiplied with incoming data)
Lin = vLIN * LeftInput    ;from any channels that have Reverb enabled
Rin = vRIN * RightInput   ;from any channels that have Reverb enabled

;Same Side Reflection (left-to-left and right-to-right)
[mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-2])*vIIR + [mLSAME-2]  ;L-to-L
[mRSAME] = (Rin + [dRSAME]*vWALL - [mRSAME-2])*vIIR + [mRSAME-2]  ;R-to-R

;Different Side Reflection (left-to-right and right-to-left)
[mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-2])*vIIR + [mLDIFF-2]  ;R-to-L
[mRDIFF] = (Rin + [dLDIFF]*vWALL - [mRDIFF-2])*vIIR + [mRDIFF-2]  ;L-to-R

;Early Echo (Comb Filter, with input from buffer)
Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2] + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
Rout = vCOMB1*[mRCOMB1] + vCOMB2*[mRCOMB2] + vCOMB3*[mRCOMB3] + vCOMB4*[mRCOMB4]

;Late Reverb APF1 (All Pass Filter 1, with input from COMB)
Lout = Lout - vAPF1*[mLAPF1-dAPF1], [mLAPF1] = Lout, Lout = Lout*vAPF1 + [mLAPF1-dAPF1]
Rout = Rout - vAPF1*[mRAPF1-dAPF1], [mRAPF1] = Rout, Rout = Rout*vAPF1 + [mRAPF1-dAPF1]

;Late Reverb APF2 (All Pass Filter 2, with input from APF1)
Lout = Lout - vAPF2*[mLAPF2-dAPF2], [mLAPF2] = Lout, Lout = Lout*vAPF2 + [mLAPF2-dAPF2]
Rout = Rout - vAPF2*[mRAPF2-dAPF2], [mRAPF2] = Rout, Rout = Rout*vAPF2 + [mRAPF2-dAPF2]

;Output to Mixer (Output volume multiplied with input from APF2)
LeftOutput  = Lout*vLOUT
RightOutput = Rout*vROUT

;Saturate to -8000h..+7FFFh and advance buffer
BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)
```

[VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/ — SPU Reverb Formula section, fetched 2026-04-19]

### E2: vIIR = −0x8000 anomaly (verbatim)

> "vIIR works only in range -7FFFh..+7FFFh. When set to -8000h, the multiplication by -8000h is still done correctly, but, the final result (the value written to memory) gets negated"

[VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/ — same page]

### E3: Saturation note (verbatim)

> "The values written to memory are saturated to -8000h..+7FFFh."

[VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/ — same page]

**Critical note on E3:** The pseudocode in E1 does **NOT** show explicit `sat_s16()` wrappers on individual lines. Saturation is mentioned only as a separate note. This is the textual basis for D-09's open question — an explicit hard-clip stage between input-scale and SAME IIR is an SPU-94 design choice to satisfy CORE-02's "independently testable" requirement, not a direct quote from the pseudocode.

### E4: L/R alternating 44.1 kHz cycles (verbatim)

> "The reverb hardware spends one 44100h cycle on left calculations, and the next 44100h cycle on right calculations"

[VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/ — same page]

**Implication:** SPU-94's `spu94_tick` models a full L+R 22.05 kHz stereo step. The internal L-then-R ordering within the tick matches nocash's L-then-R cycle ordering. Nocash is silent on whether registers are re-read between the two 44.1 kHz cycles — this is the D-08 gray area.

### E5: Output to Mixer (verbatim)

> "LeftOutput = Lout*vLOUT"
> "RightOutput = Rout*vROUT"

[VERIFIED: psx-spx.consoledev.net/soundprocessingunitspu/ — same page]

### E6: Nocash author on COMB precision: SILENT

Nocash does not specify: (a) whether the four products in the COMB sum are individually saturated before summing, (b) whether the sum is done in int16 (with cascading saturation) or int32 (with one final saturation), (c) whether the final sat happens before or after APF1 reads `Lout`. This is the D-07 gray area. [VERIFIED: by exhaustive string search of the psx-spx SPU page for "COMB", "accumulator", "intermediate", "int32", "precision" — no such discussion found.]

### E7: Jsgroth independent corroboration on COMB precision ambiguity

> "all values are saturated to signed 16-bit before being written to RAM (and possibly also during some of the intermediate calculations - unclear to me)."

[CITED: jsgroth.dev/blog/posts/ps1-spu-part-3/ — published May 26, 2024 per search result metadata.] Confirms: even an independent reverse-engineering writeup flags intermediate-saturation precision as unresolved. Strong evidence that D-07 cannot be resolved from spec alone; SPU-94 must pick a principled default and seam-structure it.

### E8: DuckStation/Mednafen-PSX behavioral witness (ALGORITHMIC APPROACH ONLY, not source transcription)

The DuckStation commit 809b9f8 (2019-era, pre-license-change) ported the reverb formula from Mednafen-PSX. Per the commit's diff description: saturation is applied after individual multiply operations (the Q15 `>> 15` shift), the 4-tap COMB products are summed as s32 and saturated once at the end (single-pass, not cascading), and vIIR = INT16_MIN is handled with explicit branching — not via emergent saturation arithmetic. [CITED: github.com/stenzek/duckstation/commit/809b9f89ca0a24934ffa13c7901345ed0aa82eeb — described via WebFetch summary, not code-transcribed.]

**License posture:** DuckStation was GPLv3 in 2019 (when the commit landed); relicensed to CC-BY-NC-ND in September 2024. Mednafen-PSX is GPLv2. SPU-94 does **not** read either as a primary source. The high-level algorithmic observation above (int32 accumulate + single sat; explicit vIIR branch) is behavioral — it corresponds to the D-07 and D-10 implementation choices — not a source port. DOCS-03's bibliography records "DuckStation SPU reverb (commit 809b9f8)" as a secondary cross-reference with its license status explicit. [VERIFIED: license change dated Sept 2024; GamingOnLinux report.]

## Decisions Recommended (to land as ADR-0007..ADR-0010)

Planner writes these into DECISIONS.md after user confirms. Each is seam-structured per D-22.

### ADR-0007: Comb-Sum Accumulation Precision (resolves D-07)

**Resolution:** int32 accumulate + single final `sat_s16`. The 4-tap sum stays in `int32_t` across all four additions; one `sat_s16` applies to the final sum before APF1 reads it.

**Confidence:** MEDIUM. Nocash silent; jsgroth flags the ambiguity explicitly; DuckStation/Mednafen-PSX behavioral witness uses this pattern. The int32 intermediate is mathematically safe (max |sum| ≈ 4 × 0x7FFF fits in int32).

**Alternative (cascading sat_s16 after each add):** Equally valid per the spec's silence. M5 hardware capture may prefer this variant. The D-22 seam is the `spu94_reverb_comb` function body — swap int32 accumulate for cascading sat_s16 without changing the calling code. ADR-0007 documents the alternative and the swap procedure.

**Test:** `test_reverb_comb.c` includes inputs where int32 sum and cascading-sat produce different outputs; the hand-derived reference uses the int32 formulation. If M5 flips the policy, the reference table and the function body swap together.

### ADR-0008: L/R Register-Write Timing Within a Sample Pair (resolves D-08)

**Resolution:** Freeze `v*` snapshot at the start of the reverb body; L-half and R-half use the same snapshot. `d*`/`m*` registers are TICK_LATCHED per ADR-0005 and therefore already frozen for the tick's duration.

**Confidence:** MEDIUM-LOW. Nocash silent on whether the hardware re-reads `v*` between the L and R 44.1 kHz cycles. The frozen-snapshot policy is a principled default — it matches the spirit of ADR-0005 (tick atomicity) and produces the simplest mental model.

**Alternative (re-read v* for R-half):** Equally valid per the spec's silence. If M5 hardware capture shows the R-half sees new v* values when a host write lands between the two 44.1 kHz cycles, the `spu94_reverb_body` caller re-reads between calling the L-phase and R-phase stage subroutines (or, if D-02's single-function-per-stage grain needs to be revisited, stage functions split L and R at that point). The D-22 seam is at the body-caller level.

**Test:** `test_reverb_body.c` includes a test where a hypothetical mid-tick v* change (simulated by the test setting up a specific `pending_mask` state) produces identical outputs to the frozen-snapshot case. The test documents the semantic.

**Revisit trigger:** M5 Phase 8 hardware capture. Until then, this ADR is the pin.

### ADR-0009: Hard-Clip Stage Placement (resolves D-09 and CORE-02)

**Resolution:** An explicit `spu94_reverb_hard_clip` stage function sits between `spu94_reverb_input_scale` and `spu94_reverb_same_iir` on the input path. The stage body is a trivial `sat_s16` pair. Its existence as a separate function satisfies CORE-02's "independently testable" requirement (SC-2). Implicit per-multiply saturation inside every `q15_mul_truncate*` call continues to apply everywhere else.

**Confidence:** MEDIUM. Nocash's saturation note ("values written to memory are saturated to −8000h..+7FFFh") does not explicitly require a separate clip stage before SAME IIR. The stage is an SPU-94 design choice, not a spec dictate.

**Alternative (fold into input-scale with an implicit `sat_s16` on `Lin_wide`):** Equally bit-correct. Makes CORE-02 testing harder — the planner would need to add a test scaffold that isolates the clip from the input-scale. The D-22 seam is the function slot: the body caller can be routed through `spu94_reverb_hard_clip` or directly to `spu94_reverb_same_iir`.

**Test:** `test_reverb_hard_clip.c` drives the stage with int32 inputs at ±0x10000, ±INT16_MAX+1, INT32_MIN, INT32_MAX, and asserts sat_s16-matching outputs bit-for-bit. This is the "independently testable" acceptance criterion.

### ADR-0010: vIIR = −0x8000 Anomaly Mechanism (resolves D-10 and CORE-08)

**Resolution:** Explicit branch `if (vIIR_snap == INT16_MIN) result = sat_s16(-(int32_t)result);` at the memory-write point of EACH IIR stage (SAME and DIFF). Applied to the final memory-written result per nocash's "final result (the value written to memory) gets negated" wording.

**Confidence:** HIGH. Nocash describes the observable effect, not an emergent mechanism. The explicit branch matches the spec wording verbatim and matches the DuckStation witness. Emergent-from-saturation approaches would require reverse-engineering a mechanism nocash does not specify — risks drift.

**Alternative (emergent from saturation arithmetic):** Rejected. Would require constraints on every multiply in the IIR chain that nocash does not document. If M5 hardware capture reveals a specific emergent mechanism, the ADR's D-22 seam is the branch itself: replace with the specific arithmetic pattern.

**Test:** `test_reverb_same_iir.c` + `test_reverb_diff_iir.c` include:
- Anomaly path: vIIR = INT16_MIN, non-zero input, assert result is negated vs non-anomaly control.
- Non-anomaly control: vIIR = INT16_MIN + 1, same input, assert NO negation (proves the anomaly is specifically INT16_MIN).
- Non-anomaly control: vIIR = INT16_MAX, same input, assert normal multiplication.
- Edge: vIIR = INT16_MIN and `result` before anomaly branch is itself INT16_MIN — the branch-then-sat_s16 produces INT16_MAX (the documented clamp), not an UB-producing `-INT16_MIN`.

### ADR on D-11 (per-multiply err-tap wiring scope — Phase 3 first real consumer)

**Resolution:** Scope (i) — all multiplies observable, per-stage `int32` error accumulator in `spu94_state`. Each reverb stage gets one accumulator field (`err_input_scale`, `err_hard_clip` [likely zero], `err_same_iir`, `err_diff_iir`, `err_comb`, `err_apf1`, `err_apf2`, `err_output_scale`). Runtime cost: one int32 add per multiply. Accessible via internal-only getter for tests; not on public header (per D-04).

**Confidence:** HIGH. D-11 is an SPU-94 design decision; scope (i) maximizes test coverage for the TEST-07 edge battery and sets up the Error Accumulator concept (D-18) for M4+ Controllers consumption.

**Test:** `test_reverb_edges.c` asserts:
- Per-stage err accumulator is zero for non-saturating input (invariant).
- Per-stage err accumulator increments monotonically under saturating input (invariant).
- The sum of all stage err values over a tick equals the total Q15 truncation loss of that tick (tautology, but verifies plumbing).

Note: This may or may not be a standalone ADR — planner decides whether to fold into ADR-0007 (as "per-multiply observability for the comb") or create ADR-0011.

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| Float-based PSX reverb VSTs (lv2-psx-reverb, pre-2020 plugins) | Integer Q15 bit-exact emulation | Circa 2018–2019 when DuckStation ported Mednafen-PSX's integer formula | Bit-exactness against hardware capture is the new standard; float approximations are acceptable only for audio-tool purposes. SPU-94 joins the bit-exact camp. |
| GPL-only reference implementations (Mednafen, UPSE, lv2-psx-reverb) | Permissive-licensed original work derived from nocash spec | Ongoing — SPU-94 is attempting this | Enables embedding in commercial products (future M4 plugin), Eurorack hardware (M5), JUCE wrappers. Requires strict nocash-paraphrase discipline per DOCS-03. |
| DuckStation as a reference emulator | DuckStation as witness only (license restricted) | September 2024 (CC-BY-NC-ND relicense) | SPU-94's licensing posture was correct before the relicense; now even more so. |

**Deprecated/outdated:**
- **lv2-psx-reverb as a bit-exact witness.** It's float-based; PROJECT.md correctly excludes it from the frequency-response axis and its bit-exactness is undefined. Use only for loose audible-similarity comparison in Phase 7.
- **"Read Mednafen source to understand PSX reverb."** Never the right call per PROJECT.md licensing posture; the nocash spec plus jsgroth's independent analysis covers everything needed at the spec level; DuckStation/Mednafen behavior is observable as black-box output.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | Int32 accumulate + final sat_s16 for COMB matches hardware | ADR-0007, Pattern 3 | Low-MEDIUM. Divergence from hardware in extreme-coefficient regimes (unlikely in real presets). Seam-swappable. M5 capture is the fallback. |
| A2 | Frozen v* snapshot for both L and R halves matches hardware | ADR-0008, Pattern 4 | MEDIUM. If hardware re-reads v*, real-world divergence only under mid-tick host-write regimes — rare in game audio, common in M4 lever modulation. Seam-swappable. M5 capture is the fallback. |
| A3 | Explicit clip stage before SAME IIR matches CORE-02 spirit | ADR-0009, Pattern 2 | LOW. Semantically equivalent to implicit saturation at the same point; the explicit stage purely improves testability. No audible-behavior risk. |
| A4 | Explicit vIIR=INT16_MIN branch at memory-write point matches hardware | ADR-0010, Example 4 | LOW. Nocash wording is direct; DuckStation witness agrees. If emergent mechanism exists, behavior is still identical from the host's perspective. |
| A5 | Per-multiply err-tap with int32 accumulator has negligible hot-path cost | D-11 scope (i) recommendation | LOW. One int32 add per multiply; benchmark harness in Phase 7 will confirm. If pathological, scope reduces to (ii) [feedback multiplies only] via the D-22 seam. |
| A6 | DuckStation's 2019-era reverb approach reflects Mednafen-PSX's int32+sat pattern accurately | § Nocash Primary Evidence E8 | LOW-MEDIUM. The WebFetch summary described the pattern; not code-transcribed. A planner-level cross-check (reading the commit description, NOT the code) could confirm without transcription. |
| A7 | nocash text at psx-spx.consoledev.net matches the original problemkaputt.de specs document bit-for-bit for the reverb section | § Nocash Primary Evidence | LOW. Both sources returned identical wording for the vIIR anomaly and the pseudocode; psx-spx is explicitly a converted edition of Martin Korth's document (confirmed by search). |
| A8 | `q15_add_sat` is correctly implemented for subtraction via `q15_add_sat(a, -b)` with int32 widening on b | Pattern 1, Pitfall 1 | LOW. Phase 1's test table covers add-saturate edges; the subtraction pattern is standard. Any failure would surface in `test_reverb_edges.c`. Worth a dedicated row in the edge battery nonetheless. |

**If any of A1/A2 turn out wrong under M5 hardware capture:** the D-22 seams (ADR-0007 and ADR-0008) make the swap a one-TU change with a re-derivation of the test reference tables. Phase 3's plans should keep the reference-derivation scripts under `tests/python/derive_reverb_reference.py` so that re-deriving after a seam swap is one command.

## Open Questions

1. **Should the hard-clip stage's `int32_t Lin_wide` input type be `int32_t` (allowing future input-scale to produce wider values) or `int16_t` (constraining it to the immediate post-input-scale range)?**
   - What we know: Nocash shows `Lin = vLIN * LeftInput` — the product of two int16 is ≤ 31 bits, so int32 is the minimum safe width.
   - What's unclear: Whether SPU-94 wants input-scale to saturate to int16 immediately and have hard-clip be a no-op on the positive path, OR keep the wide int32 through the hard-clip stage so that the clip is the explicit saturation point.
   - Recommendation: Keep int32 through hard-clip. Makes the clip the single documented saturation point on the input path; input-scale becomes a pure widening multiply. Planner's decision; either is bit-correct.

2. **Should the reverb body caller be named `spu94_reverb_tick` or `spu94_reverb_body`?**
   - CONTEXT D-06 leaves this at Claude's discretion.
   - Recommendation: `spu94_reverb_body`. The word "tick" is already spoken for by `spu94_tick` (the public API). Calling the reverb subroutine "the reverb body" disambiguates and matches Phase 2's pattern (`spu94_apply_pending_writes` and `spu94_buffer_advance` are both sub-bodies of `spu94_tick`).

3. **Should `spu94_reverb_input_scale` return int32 pairs via out-parameters or a struct?**
   - Recommendation: int32 out-parameters. Matches C-idiom of the rest of the codebase; no new struct types needed.

4. **Is there any risk that the `q15_mul_truncate_with_err` remainder semantic (pre-saturation per Plan 02-02 decision) interacts badly with the vIIR=−0x8000 anomaly branch?**
   - What we know: `_with_err` returns `result = INT16_MAX, err = 0` for `INT16_MIN * INT16_MIN` (per STATE.md Plan 02-02). The branch `if (vIIR == INT16_MIN) result = -result` runs AFTER the multiply.
   - What's unclear: Whether `err` for the vIIR=INT16_MIN multiply should be negated too. Likely not — `err` represents the truncation loss, which is a magnitude; the sign flip applies to `result` alone.
   - Recommendation: Leave `err` unsigned-in-spirit. Document in code comment. Test: error accumulator sum with vIIR=INT16_MIN matches the sum with vIIR=+0x7FFF when other operands are identical (proves the negation does not leak into `err`).

5. **Should `tests/python/fuzz_reverb.py` be one harness or three (by stage-group: scale/clip, IIR, COMB/APF)?**
   - Recommendation: Start with one harness (mirrors `fuzz_buffer.py`). Split only if the 10⁶-step run gets too slow (it won't — the Phase 2 harness is ~2.46 s for 10⁶ steps). Planner decides.

## Environment Availability

> Phase 3 is pure-code-in-repo, no new external dependencies beyond what Phase 1/2 already require. This table documents the continuity.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| gcc or clang | Core library compile | ✓ (assumed; Phase 1 set up the toolchain) | project-locked | — |
| CMake ≥ 3.20 | Build graph | ✓ (Phase 1) | — | — |
| Unity C test framework | Per-stage unit tests | ✓ (vendored, Phase 1) | pinned | — |
| Python 3.10+ | `fuzz_reverb.py` | ✓ (local 3.13.7 per Phase 2 Plan 05) | ≥ 3.10 floor | — |
| UBSan (runtime, via `-fsanitize=undefined` in the `test-ubsan` build) | Catch INT16_MIN-negation UB etc. | ✓ (Phase 1 ADR-0003) | — | — |
| numpy (test-side only) | Optional — derive expected outputs | ✓ (local; not required in CI) | ≥ 1.26 OK | Pure-Python-integer derivation |

**Missing dependencies:** none.

**Missing dependencies with no fallback:** none.

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework | Unity (C, vendored, Phase 1) + Python 3 ctypes (stdlib) |
| Config file | `tests/unit/CMakeLists.txt` (phase-3 adds `tests/unit/reverb/` subdir) + `tests/python/CMakeLists.txt` (adds `fuzz_reverb` ctest target) |
| Quick run command | `ctest --output-on-failure --test-dir build -R reverb` |
| Full suite command | `ctest --output-on-failure --test-dir build` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CORE-02 | Hard-clip on mix-bus saturates to ±INT16 range | unit | `ctest -R test_reverb_hard_clip` | ❌ Wave 0 — new file `tests/unit/reverb/test_reverb_hard_clip.c` |
| CORE-05 | Stage order produces correct nocash-derived outputs per stage | unit | `ctest -R test_reverb_same_iir` et al. | ❌ Wave 0 — new files in `tests/unit/reverb/` |
| CORE-05 | Full body equals sequential stage composition | unit | `ctest -R test_reverb_body` | ❌ Wave 0 |
| CORE-08 | vIIR = INT16_MIN negates final result | unit | `ctest -R test_reverb_same_iir -V 2>&1 \| grep anomaly` | ❌ Wave 0 |
| CORE-08 | vIIR = INT16_MIN + 1 does NOT negate (control) | unit | same TU, different table row | ❌ Wave 0 |
| TEST-06 | Dedicated anomaly test | unit | `ctest -R test_reverb_same_iir` | ❌ Wave 0 |
| TEST-07 | Q15 saturation edges per stage | unit | `ctest -R test_reverb_edges` | ❌ Wave 0 — new file |
| TEST-07 | Q15 truncation direction re-assertion | unit | reuses Phase 1 test_q15 + `test_reverb_edges` applies ADR-0001 to reverb context | ❌ Wave 0 |
| (fuzz) | 10⁶ random register configs + input samples; invariants hold | property | `ctest -R fuzz_reverb` | ❌ Wave 0 — new file `tests/python/fuzz_reverb.py` |
| (fuzz) | Full-tick vs stage-composition equivalence | property | same harness | ❌ Wave 0 |

### Sampling Rate

- **Per task commit:** `ctest --output-on-failure --test-dir build -R reverb` (fast — unit tests only)
- **Per wave merge:** `ctest --output-on-failure --test-dir build` (full suite including Python fuzz — ~2.5s per 10⁶ fuzz run; Phase 3 adds its own similar duration)
- **Phase gate:** Full suite green on clean build before `/gsd-verify-work`. Grep-guard + verify-no-heap clean. UBSan build clean.

### Wave 0 Gaps

- [ ] `tests/unit/reverb/` directory + CMakeLists.txt scaffold
- [ ] `tests/unit/reverb/test_reverb_input_scale.c`
- [ ] `tests/unit/reverb/test_reverb_hard_clip.c`
- [ ] `tests/unit/reverb/test_reverb_same_iir.c` (includes vIIR=-0x8000 anomaly test + non-anomaly control)
- [ ] `tests/unit/reverb/test_reverb_diff_iir.c` (includes vIIR=-0x8000 anomaly test)
- [ ] `tests/unit/reverb/test_reverb_comb.c` (includes int32-accumulate-vs-cascading-sat boundary case)
- [ ] `tests/unit/reverb/test_reverb_apf1.c`
- [ ] `tests/unit/reverb/test_reverb_apf2.c`
- [ ] `tests/unit/reverb/test_reverb_output_scale.c`
- [ ] `tests/unit/reverb/test_reverb_body.c` (full-body vs stage-composition equivalence + buffer_address-unchanged invariant)
- [ ] `tests/unit/reverb/test_reverb_edges.c` (TEST-07 battery)
- [ ] `tests/python/fuzz_reverb.py` + CMakeLists registration
- [ ] `tests/python/derive_reverb_reference.py` (hand-derivation of reference values per Pitfall 9)

*(No framework install needed — Unity + Python 3 + ctypes are already present.)*

## Security Domain

> Phase 3 is a pure-integer DSP inner loop in a C library with no network, file, or user-input surface of its own. CLAUDE.md's security constraints (no heap, no float, no syscalls, no unqualified `long`) are already enforced by Phase 1's CI. ASVS categories are not directly applicable at this layer.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A |
| V3 Session Management | no | N/A |
| V4 Access Control | no | N/A |
| V5 Input Validation | yes (indirectly) | The reverb body takes `struct spu94_state *` — NULL-safety is a Phase 2 contract. Integer inputs are `int16_t` / `int32_t` and cannot produce memory unsafety. Buffer reads use Phase 2's wrap-masked `(addr + offset) & 0x7FFFE` — verified memory-safe by Phase 2 Plan 05's 10⁶-step fuzz. |
| V6 Cryptography | no | N/A |

### Known Threat Patterns for This Stack

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Out-of-bounds write to work buffer via corrupted register values | Tampering | Phase 2 wrap mask `& 0x7FFFE` enforced at every read/write; mBASE snap-on-write (ADR-0006) keeps `buffer_address >= mBASE`; 10⁶-step fuzz covers the corner. Phase 3 inherits. |
| Integer overflow in accumulator causing wraparound to malicious value | Tampering | All adds use `q15_add_sat`; COMB int32 accumulator has documented bound; UBSan catches any violation. |
| Denial-of-service via pathologically slow input | Availability | Every reverb stage is O(1) with a fixed op count. No data-dependent loops. Benchmark harness in Phase 7 confirms. |
| Undefined behavior enabling compiler to miscompile saturation logic | Tampering | `-fno-strict-overflow` via determinism flags + UBSan + `no_sanitize("integer")` annotations where saturation wrap is intentional (ADR-0003). |

## Sources

### Primary (HIGH confidence)

- **psx-spx.consoledev.net/soundprocessingunitspu/** — SPU Reverb Formula (verbatim E1), vIIR anomaly (E2), saturation note (E3), L/R 44.1 kHz cycle (E4), output stage (E5). Fetched 2026-04-19. [License note: psx-spx is a conversion of Martin "nocash" Korth's PSXSPX Specifications; nocash's copyright status is ambiguous per PROJECT.md, so SPU-94 paraphrases prose and treats facts/formulas as technical-fact material.]
- **problemkaputt.de/psx-spx.htm** — original nocash specification page, cross-reference for E1–E5. Conversion source for psx-spx.consoledev.net.

### Secondary (MEDIUM confidence — independent corroboration)

- **jsgroth.dev/blog/posts/ps1-spu-part-3/** — independent PS1 SPU Part 3 blog post (May 2024). Confirms the 6-stage algorithm structure, L/R 22.05 kHz clocking, and explicitly flags the intermediate-saturation precision as unresolved ("unclear to me") — primary corroboration for D-07's gray-area status. Author: jsgroth. [License: personal blog; no code transcribed; high-level algorithmic overlap only.]

### Tertiary (behavioral witness, LICENSE-AWARE — cross-reference for algorithmic approach, NOT source transcription)

- **github.com/stenzek/duckstation commit 809b9f8** — "SPU: Use reverb formula from Mednafen-PSX" (2019). Behavioral cross-reference for D-07 (int32 accumulate + single sat) and D-10 (explicit vIIR=−0x8000 branch). [License at commit time: GPLv3. Current license: CC-BY-NC-ND (Sept 2024). SPU-94 does NOT read the code; the behavioral observation above is paraphrased from the commit's diff description via WebFetch summary. Bibliography entry required per DOCS-03.]
- **github.com/ipatix/lv2-psx-reverb** — the existing LV2 plugin. Confirmed float-based, not bit-accurate; excluded from frequency-response axis per PROJECT.md. [License: GPLv3 per COPYING. Cross-reference only — not useful for bit-accuracy questions.]
- **github.com/kaniini/upse `libupse/upse-ps1-spu-reverb.h`** — UPSE project. [License: GPLv2. Not consulted as source.]

### Not consulted as source (listed for explicit exclusion per PROJECT.md)

- Mednafen PSX source (GPLv2).
- PCSX-Redux source (GPLv2).
- MiSTer FPGA PS1 core (mixed licenses, not read).

### Ecosystem references (context only)

- **hitmen.c02.at/files/docs/psx/spu.txt** — doomed@c64.org SPU document (1999). Contains reverb preset register values but no reverb algorithm pseudocode. Useful for CORE-09 (presets — Phase 5), not Phase 3.
- **copetti.org/writings/consoles/playstation/** — high-level PlayStation architecture overview. Context; not a primary reverb source.
- **microchip.com / sestevenson.wordpress.com / embeddedrelated.com** — Q15 fixed-point tutorials. Confirmed standard practice for INT16_MIN² edge case (saturate to INT16_MAX) — already implemented in Phase 1's `q15_mul_truncate_with_err`.

## Metadata

**Confidence breakdown:**
- Standard stack (pure C int math + Phase 1/2 primitives): HIGH — the absence of new external libraries is the correct answer per PROJECT.md constraints.
- Architecture patterns (seven stage functions, tick-latched v* snapshot, explicit hard-clip stage): HIGH on the stage order and function count (locked by D-02); MEDIUM on the tick-latched v* snapshot (D-08 gray area); HIGH on the explicit hard-clip stage as a design choice for CORE-02 testability.
- Pitfalls (INT16_MIN negation, arithmetic-right-shift, v* re-read, vIIR anomaly branch placement, buffer-advance ordering): HIGH — all eight pitfalls are either language-level facts (INT16_MIN UB) or policy-level consequences of locked decisions.
- Decisions recommended (ADR-0007..ADR-0010): MEDIUM on D-07 (behavioral witness, not spec); MEDIUM-LOW on D-08 (spec silent, principled default); MEDIUM on D-09 (design choice); HIGH on D-10 (spec wording direct).

**Research date:** 2026-04-19
**Valid until:** 2026-05-19 (30 days for stable spec material; the nocash document has been stable for decades, but secondary-witness landscape [DuckStation license, emulator updates] evolves).

---

*Phase: 03-core-reverb-algorithm-hard-clip*
*Research artifact. Planner consumes this + CONTEXT.md to produce the PLAN.md files.*
*Next step: `/gsd-plan-phase 3` (researcher returns to orchestrator; orchestrator launches planner).*
