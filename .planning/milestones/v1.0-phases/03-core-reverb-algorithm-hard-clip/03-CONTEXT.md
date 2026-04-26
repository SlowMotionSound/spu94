# Phase 3: Core Reverb Algorithm + Hard Clip - Context

**Gathered:** 2026-04-19
**Supplemental pass:** 2026-04-19 (post-research — D-07..D-11 locked in plain-language Q&A with user)
**Status:** Ready for planning — all gray areas locked. `03-RESEARCH.md` is the primary-source backing; this CONTEXT supersedes the preliminary leans it surfaced.

<domain>
## Phase Boundary

Phase 3 delivers **the reverb algorithm itself** — the per-22.05 kHz-tick network computation that slots into the already-shaped `spu94_tick()` body between `spu94_apply_pending_writes()` (Phase 2 Plan 03) and `spu94_buffer_advance()` (Phase 2 Plan 04). Phase 3 also delivers the mix-bus hard-clip stage that sits on the reverb's input path.

**In scope:**
- The six documented reverb stages per nocash psx-spx's processing order: input scale → SAME IIR → DIFF IIR → 4-tap comb → APF1 → APF2 → output scale
- The hard-clip / saturation stage on the mix bus feeding the reverb (CORE-02)
- The `vIIR = -0x8000` hardware anomaly (CORE-08, TEST-06)
- Fixed-point saturation, truncation, and overflow edge cases covered by dedicated tests (TEST-07)
- Hand-derived per-stage reference tables enabling SC-1 (isolated-stage bit-for-bit equality against nocash)
- Two DECISIONS.md entries mandated by ROADMAP SC-5: (a) comb-sum intermediate accumulation precision, (b) register-write timing between L-tick and R-tick within a 44.1 kHz sample pair. (These resolutions land as ADRs after research.)

**Governing directive (user, 2026-04-19):**

> *"I just want all these elements AS CLOSE TO THE ORIGINAL PS1 SPU REVERB AS POSSIBLE."*

This directive governs every research-gated resolution in Phase 3. Where nocash is explicit, SPU-94 follows it verbatim. Where nocash is silent (D-07, D-08), Phase 3 ships a principled default and every such decision is structured as a D-22 seam so Milestone 5 hardware capture can flip the pin without touching surrounding code. **Supplemental post-research resolution (2026-04-19):** D-07..D-11 were locked in a plain-language Q&A pass after `03-RESEARCH.md` landed. Two user decisions diverged from the research-recommended defaults: D-07 (chose cascading clamp over int32-accumulate for comb sum — more distortion character, richer overflow signal) and the M4-facing expansion of D-11 (added an overflow-magnitude observable to pair with the truncation err-tap). Both divergences were taste-driven and align with the user's Controllers/plugin roadmap; both remain seam-swappable if M5 capture or plugin-era user testing reveals a better answer.

**Explicitly NOT in scope:**
- 39-tap half-band FIR at the 44.1 ↔ 22.05 kHz I/O boundary — Phase 4 (CORE-06, CORE-07)
- `spu94_process` block-based public entry point — Phase 5 (API-03)
- 10 factory reverb presets — Phase 5 (CORE-09)
- Python ctypes bindings / wheel — Phase 6
- Witness-diff harness against lv2-psx-reverb / Mednafen / DuckStation — Phase 7 (TEST-03). Phase 3's audible divergence from witnesses (if any) is a research-gated gray area, not a Phase 3 blocker.
- Golden-file regression tests — Phase 7
- MCU cross-compile validation — Phase 8

</domain>

<decisions>
## Implementation Decisions

### Test Isolation + Reverb Body Factoring (Area 3 — locked)

- **D-01: Each reverb stage is an internal-linkage function, declared in a new src-only header.** File: `src/spu94/spu94_reverb_internal.h`. Tests include it directly (analogous to how Phase 2 tests consume `spu94_state_internal.h`). The stage functions never appear on any public header under `include/spu94/`. This is the literal implementation of SC-1's "driving the reverb tick with isolated stage inputs" — tests call each stage with crafted state and assert its output bit-for-bit against the hand-derived reference.
- **D-02: One function per documented stage, L and R handled internally per function.** Six stage functions total: `spu94_reverb_input_scale`, `spu94_reverb_same_iir`, `spu94_reverb_diff_iir`, `spu94_reverb_comb`, `spu94_reverb_apf1`, `spu94_reverb_apf2`, `spu94_reverb_output_scale`. Each runs both the L and the R pass internally per nocash's pseudocode structure. This aligns stage granularity with the nocash primary source — the unit the hand-derived references target — rather than inventing a finer split SPU-94 would have to justify from scratch. Note: 7 functions, because input-scale and output-scale are separate documented stages bracketing the reverb network.
- **D-03: Output-scale is its own stage function (not folded into `spu94_tick`).** SC-1 lists "output scale" among the six stages the hand-derived reference targets; factoring it as its own function keeps stage-granularity uniform, preserves test-vector symmetry with the other stages, and mirrors the factoring of `spu94_apply_pending_writes` / `spu94_buffer_advance` in Phase 2 (no inline logic in the top-level tick body).
- **D-04: Phase 3 does not add public read-only stage-output observers (D-23 style).** Defer per D-21 reasoning: tests reach stage outputs via the internal header; no external consumer yet demonstrates a need for mid-tick stage observability through the public API. If Controllers / the Error Accumulator / any M4+ consumer later demonstrates a need, public accessors are added then — read-only, adhering to D-23.
- **D-05: Single `src/spu94/spu94_reverb.c` TU holds the stage function bodies + the top-level reverb-body caller that `spu94_tick` invokes as its third statement.** Matches Phase 2's grain (one concern per TU: `spu94_buffer.c`, `spu94_pending.c`, `spu94_register_io.c`, etc.). Single-concern here = "the reverb network computation."
- **D-06: The Phase 3 reverb body is inserted into `spu94_tick` between `spu94_apply_pending_writes(state)` and `spu94_buffer_advance(state)`.** The comment placeholder in `src/spu94/spu94_tick.c` ("Phase 3 will add: the reverb-network computation.") is the literal insertion point. Pitfall 4 (single-call-site discipline from ADR-0005) still holds — the reverb body's top-level caller is called from exactly one location, the `spu94_tick` body.

### Post-Research Decisions (LOCKED — supplemental plain-language Q&A pass, 2026-04-19)

`03-RESEARCH.md` landed 2026-04-19 and surfaced primary-source evidence plus witness comparisons for each research-gated gray area. The user then worked through each in a supplemental discuss pass with plain-language framing (analogies over jargon). Final locks:

- **D-07: Comb-sum intermediate accumulation precision — LOCKED: cascading `sat_s16` after each add.** The 4-tap comb sum clamps after every intermediate add rather than accumulating in int32 and clamping once at the end. **Rationale (user, taste-driven):** more distortion character at input extremes, richer overflow signal feeding the D-11 err accumulator (each clamp event produces saturation-magnitude material for future Controllers use). Diverges from the DuckStation/Mednafen-PSX behavioral witness (int32 accumulate) but consistent with nocash's silence on intermediate precision. **Seam:** the body of `spu94_reverb_comb` — swap to 32-bit intermediate + single final `sat_s16` without touching callers. **Revert lever (see Deferred Ideas → Planted Seeds for M4):** if plugin-era user testing finds the cascading distortion too aggressive or unmusical on realistic preset material, flip to the int32-accumulate shape. M5 hardware capture remains the authority if it ever reveals which option actual PS1 silicon uses. **ADR landing:** ADR-0007 documents the lock + the alternative + the revert lever.
- **D-08: L/R register-write timing — LOCKED: freeze v* snapshot at pair start (default mode).** Both the L and R half-steps within a single 22.05 kHz sample pair read v-register values once at the start of each pair. Mid-pair writes land but take effect on the next pair. **Rationale:** atomic, predictable, matches the tick-atomicity principle carried forward from Phase 2 (ADR-0005). **M4 Controllers seed planted (see Deferred Ideas):** "Extended Modulation Mode" exposes re-read-fresh-for-R as an opt-in Controllers toggle — doubles modulation temporal resolution from pair rate (22.05 kHz) to half-step rate (44.1 kHz), enabling audio-rate LFOs/envelopes/FM-style modulation/cross-rate tricks. **Seam:** body-caller level — stage functions accept v-register values as parameters, so swapping "snapshot once" vs "re-read for R" is a one-file change in the body caller. M5 hardware capture remains the authority on which should be the default once real silicon is observed. **ADR landing:** ADR-0008 documents the lock + the Extended Modulation Mode seed + the M5 revisit trigger.
- **D-09: Hard-clip stage placement (CORE-02) — LOCKED: its own named stage function.** `spu94_reverb_hard_clip` sits between `spu94_reverb_input_scale` and `spu94_reverb_same_iir` on the input path. **Rationale:** satisfies CORE-02's "independently testable" success criterion with zero extra test scaffolding — a dedicated TU can drive the clip directly with int32 boundary inputs and assert `sat_s16` behavior bit-for-bit. The stage accepts int32 input (necessary to feed the D-11 overflow-magnitude out-param) and emits int16 output plus the overflow-magnitude. **Seam:** the function slot in the body caller. **ADR landing:** ADR-0009 documents the lock + the alternative (fold into input-scale with implicit `sat_s16`) + the testability rationale.
- **D-10: vIIR = -0x8000 anomaly implementation (CORE-08, TEST-06) — LOCKED: explicit branch at memory-write point.** At the memory-write point of each IIR stage (SAME and DIFF): `if (vIIR_snap == INT16_MIN) { result = sat_s16(-(int32_t)result); }`. The `int32` widening avoids INT16_MIN-negation UB (Pitfall 1 from research); `sat_s16` clamps the `+0x8000` overflow to `+0x7FFF`. **Rationale:** nocash documents the observable effect ("final result gets negated"), not a hardware mechanism — reproducing the observable effect matches what the spec tells us; speculating at an undocumented mechanism risks drift. Research confidence: HIGH. Matches the DuckStation behavioral witness. **Seam:** the branch itself — if M5 capture ever reveals an emergent mechanism, replace with the specific arithmetic pattern. **ADR landing:** ADR-0010 documents the lock + the rejected "emergent from saturation arithmetic" alternative + why explicit is more honest.
- **D-11: Per-multiply error-tap wiring scope — LOCKED: all multiplies (scope i) + overflow-magnitude observable on hard-clip stage.** Every Q15 multiply in every reverb stage calls `q15_mul_truncate_with_err` and feeds its truncation remainder to a per-stage int32 accumulator in `spu94_state` (fields: `err_input_scale`, `err_same_iir`, `err_diff_iir`, `err_comb`, `err_apf1`, `err_apf2`, `err_output_scale`). **Phase 3 expansion (user, post-research):** the hard-clip stage additionally exposes an overflow-magnitude out-param — the high bits lost to saturation — as the sibling observable to the truncation-remainder stream. Together these describe the **complete precision-loss surface** of the reverb: low bits lost to shift (err taps) + high bits lost to clamp (overflow tap). **Rationale (user, Controllers roadmap):** provides the raw material for future M4 features that are impossible to fake after the fact — drive meters, soft-clip warmth levers, overflow-modulated feedback, err-stream envelopes. **Runtime cost:** one int32 add per multiply + one int32 write in the clip. No runtime branches, no conditional logic. **Tests:** per-stage invariants (err is zero for non-saturating input, monotonic-increasing under saturating input; sum over a tick equals total lost-precision bits of that tick — tautology, but verifies plumbing). **Overflow-tap tests:** clip output saturates iff overflow-magnitude is non-zero; overflow-magnitude equals `|input| - INT16_MAX` (in magnitude) for inputs outside ±INT16_MAX. **Seam:** the accumulator fields live in `spu94_state` — Controllers can later add read-only accessors without mutating any reverb code. **ADR landing:** ADR-0011 (new — planner discretion to fold into ADR-0007/ADR-0009 or standalone) documents the scope + the overflow-tap extension + the Controllers motivation.

### Architectural Principles (carried forward from Phase 2)

- **D-22 Extensibility Seams (Phase 2):** every gray-area resolution in Phase 3 is structured as a pinnable mechanism. Comb-sum accumulator, L/R write-timing policy, and hard-clip placement each get a seam (function slot, policy variant, stage-function pointer) that Controllers can re-point without touching the reverb math.
- **D-23 Observability (Phase 2):** Phase 3 preserves read-only observability. Stage outputs are test-observable via internal header; the err-tap accumulator (if D-11 lands scope (i)) is observable via an internal accessor. Nothing exposes a mutation path.
- **D-24 Controllers as Future Consumer (Phase 2):** Phase 3 API design assumes a future Controllers layer consumes the reverb surface. Seams from D-07, D-08, D-09, D-11 are the specific hooks.
- **ADR-0001 (Q15 truncation direction):** every multiply in Phase 3 uses `q15_mul_truncate` / `q15_mul_truncate_with_err` — truncation toward −∞, bit-identical to Phase 1's locked semantic.
- **ADR-0002 (vIIR anomaly):** accepted as a reproduce-target; D-10 pins the mechanism after research.

### Claude's Discretion (within the locked decisions above)

- Exact C prototypes of the seven stage functions (what state/inputs each takes explicitly vs reads from `spu94_state`).
- Internal naming inside `spu94_reverb_internal.h` for stage helpers / sub-helpers.
- Whether the reverb body caller invoked by `spu94_tick` is named `spu94_reverb_tick` or `spu94_reverb_body` or similar.
- Whether the input-scale stage applies `vLIN`/`vRIN` before or fused with the hard-clip stage (both are on the input path; factoring can go either way within D-01/D-02's one-function-per-stage rule).
- Test-file granularity under `tests/unit/reverb/` — single TU per stage vs one TU covering multiple stages — as long as each stage has its hand-derived reference table with INT16_MIN/MAX/0 + saturation-tripping inputs (from the test-vector robustness directive).
- Internal register-read convenience helpers inside the reverb body (e.g., whether `spu94_reverb.c` calls the engine layer directly or uses local shorthand wrappers).

### Folded Todos

None — no pending todos matched Phase 3 scope at discussion time.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Core Value ("sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't"), constraints (no heap, no float, bit-faithfulness, no reading GPL sources as primary), licensing posture, key decisions table.
- `.planning/REQUIREMENTS.md` — Phase 3 owns CORE-02, CORE-05, CORE-08, TEST-06, TEST-07.
- `.planning/ROADMAP.md` § Phase 3 — the five success criteria must all be TRUE to consider Phase 3 complete. SC-5 specifically mandates DECISIONS.md entries for comb-sum precision and L/R write timing — those are D-07 and D-08.

### Prior Phase CONTEXT.md files (must read for consistency)

- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — Q15 API shape, truncation direction, inline-reference-table test pattern, grep-guard + UBSan CI.
- `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md` — D-01 through D-24. Phase 3 extends the pattern.

### Prior Phase Research (shape precedent for Phase 3 research)

- `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md` — the research artifact that resolved mBASE side-effect (D-10 → ADR-0006). Phase 3's `03-RESEARCH.md` follows the same structure: primary-source evidence verbatim (nocash / psx-spx), secondary-source corroboration (hitmen c02, jsgroth, findable PSX homebrew), behavioral-witness comparison deferred to Phase 7.

### ADR Log (appended in Phase 3)

- `docs/DECISIONS.md` — ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR anomaly), ADR-0003 (UBSan no_sanitize policy), ADR-0004 (extensibility taps: `q15_mul_truncate_with_err` + `spu94_tick`), ADR-0005 (per-register write-timing policy table), ADR-0006 (mBASE snap-on-write). Phase 3 appends: ADR-0007 (comb-sum precision, from D-07 research), ADR-0008 (L/R write timing, from D-08 research), ADR-0009 (hard-clip placement, from D-09 research), ADR-0010 (vIIR anomaly mechanism, from D-10 research). Exact ADR count depends on what research resolves — some may collapse into single ADRs.

### External References (paraphrased only — do NOT transcribe per PROJECT.md licensing posture)

- **nocash PSX SPU documentation** (problemkaputt.de / psx-spx.consoledev.net) — primary authority for the reverb processing order (input scale → SAME IIR → DIFF IIR → comb → APF1 → APF2 → output scale), stage-by-stage pseudocode, register semantics, vIIR anomaly behavior, and saturation / hard-clip semantics. Research captured in `03-RESEARCH.md`.
- **hitmen c02 PSX SPU documentation** — secondary corroborating source for reverb-network behavior.
- **jsgroth PS1 SPU Part 3 writeup** — independent analysis; useful for cross-checking nocash where nocash is ambiguous.

### Not to be read as primary source (per PROJECT.md licensing posture)

- Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer source — their **output audio** may be used as behavioral witnesses (deferred to Phase 7); their **source code** is not read as a primary research input. Any consultation to resolve a specific ambiguity is logged in DECISIONS.md.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phases 1 + 2)

- `include/spu94/spu94_q15.h` — `q15_mul_truncate`, `q15_mul_truncate_with_err`, `sat_s16`, `q15_add_sat` + `_Static_assert` ASR guard. Every Phase 3 multiply uses `q15_mul_truncate_with_err` per D-11 scope decision.
- `include/spu94/spu94_register_facade.h` — 105 hand-written `static inline` per-register wrappers (35 setters + 35 active getters + 35 pending getters). Phase 3 stage functions read via the engine layer directly (`spu94_get_reg_i16`, `spu94_get_reg_u16`) for explicit-register-intent readability, not via facade wrappers.
- `include/spu94/spu94.h` — public umbrella. Phase 3 adds nothing to it (D-04 defers stage observability; reverb body has no public surface beyond `spu94_tick` which is already there).
- `src/spu94/spu94_tick.c` — line-for-line insertion point at the "Phase 3 will add" comment (D-06). No signature change to `spu94_tick`.
- `src/spu94/spu94_state_internal.h` — single ODR home for `struct spu94_state`. If D-11 lands scope (i), Phase 3 adds per-stage int32 error accumulator fields here. `sizeof(spu94_state)` is currently 168 bytes with 16216 bytes of `SPU94_STATE_SIZE_MAX` headroom — any Phase 3 additions comfortably fit.
- `src/spu94/spu94_buffer.c` — `spu94_buffer_advance` + `spu94_mbase_on_write`. Phase 3's stage functions read `state->buffer_address` but do not modify it (buffer advance stays in Phase 2 Plan 04's hands).
- `tests/unit/` — Unity test harness with Phase 1/2 inline-reference-table pattern. Phase 3 adds `tests/unit/reverb/` following the same pattern. Phase 2's `tests/python/fuzz_buffer.py` is the template for the Phase 3 ctypes reverb-network fuzz harness.
- CMake: `spu94_obj` OBJECT library + `spu94_shared` + `spu94_static` already handle new TUs under `src/spu94/` automatically. Determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) carry forward.
- CI: grep-guard (no `float`/`double`/`malloc` in core), verify-no-heap-symbols, clang-tidy, cppcheck, UBSan — Phase 3 code passes all unchanged.

### Established Patterns

- **One-concern-per-TU grain** (Phase 2): `spu94_reverb.c` holds "the reverb network" concern. Do not split prematurely; 6-7 stage functions in one TU matches the grain.
- **Internal header for internal symbols** (Phase 2): `spu94_reverb_internal.h` follows `spu94_state_internal.h`'s pattern exactly — not installed, never on the public include path, src-only, single home for stage-function declarations.
- **Inline hand-computed reference tables in test `.c` files** (Phase 1): every Phase 3 stage test TU carries a `{register_config, input, expected_output}` table with INT16_MIN / INT16_MAX / 0 / saturation-tripping values.
- **Pitfall 4 single-call-site discipline** (ADR-0005): every Phase 3 stage helper has exactly one call site. The top-level reverb body (called from `spu94_tick`) calls each stage exactly once.
- **ADR-style DECISIONS.md entries prepended at top** (Phase 1 D-12, Phase 2 style): Phase 3 appends ADR-0007 (or similar) at top after research resolves D-07/D-08/D-09/D-10.
- **Research artifact structure** (Phase 2's 02-RESEARCH.md): primary-source evidence verbatim, secondary corroboration, witness-comparison deferral, decision proposal with explicit alternatives. `03-RESEARCH.md` mirrors.

### Integration Points

- **Phase 4 (39-tap FIR)** consumes Phase 3's reverb body unchanged. The FIR layer wraps `spu94_tick` — Phase 3 exposes no new interface for this.
- **Phase 5 (`spu94_process` + presets)** wraps Phase 3's reverb body in a block-based public entry point. Presets (CORE-09) drive the 35 registers Phase 2 already exposed; Phase 3 doesn't add preset infrastructure.
- **Phase 6 (Python bindings)** wraps the C API Phase 3 leaves unchanged (`spu94_tick` is the per-sample entry point exposed since Phase 2 Plan 02).
- **Phase 7 (witness diff + golden files)** consumes Phase 3's reverb body. The Phase 3 research may document expected divergence thresholds with lv2-psx-reverb on the frequency-response axis (excluded per PROJECT.md) and with Mednafen/DuckStation on algorithmic axes.
- **Future Controllers milestone** consumes the seams Phase 3 locks: comb-sum accumulator variant (D-07), L/R write-timing policy (D-08), hard-clip-stage-as-function-pointer (D-09), per-stage error-accumulator readout (D-11 if scope (i)).

</code_context>

<specifics>
## Specific Ideas

### Test-Vector Robustness (anchored in the user's "most robust testing vectors" directive)

The planner should build these test measures into Phase 3's plans:

- **Per-stage hand-derived reference tables**: each stage's test TU (`test_reverb_input_scale.c`, `test_reverb_same_iir.c`, ..., `test_reverb_output_scale.c`) carries a `{register_config, input_state, expected_output_state}` table with at minimum these coverage cases per stage:
  - INT16_MIN inputs (every signed register field)
  - INT16_MAX inputs
  - 0 inputs (zero-meaningful per ADR-0002 / D-02 of Phase 2)
  - Saturation-tripping inputs (values chosen to force the stage's saturation path)
  - vIIR = -0x8000 specifically (TEST-06 target; anomaly path verified whether implementation lands D-10 branch or emergent)
- **Stage-composition equivalence test**: `reverb_body(state) ≡ output_scale(apf2(apf1(comb(diff_iir(same_iir(input_scale(hard_clip(in))))))))` — verifies the full tick body equals sequential stage application with no hidden side effects.
- **vIIR anomaly control test**: TEST-06 tests the anomaly path AND a non-anomaly control case (vIIR = INT16_MIN+1) to prove the anomaly is specifically vIIR = INT16_MIN and nothing else.
- **Fixed-point edge battery (TEST-07)**: dedicated TU `test_reverb_edges.c` covers Q15 saturation (every stage that saturates), truncation direction (ADR-0001 re-asserted in the reverb context), signed-overflow behavior in each accumulation. Every stage exercised at its Q15 edges.
- **Python ctypes reverb-network fuzz harness**: `tests/python/fuzz_reverb.py` — 10⁶ random register configurations + input samples. Invariants: L/R stability (no unbounded growth under bounded input), no crashes / no wrap-off-the-buffer, full-tick-vs-stage-composition equivalence. Follows `tests/python/fuzz_buffer.py`'s pattern exactly: `$<TARGET_FILE:spu94_shared>` env-var + independent Python state model.
- **Error-tap invariant tests** (if D-11 lands scope (i)): per-stage err accumulator increments monotonically under saturating input; err stream is zero for non-saturating input; err stream's sum over a tick equals the total lost-precision bits of that tick (tautology, but verifies the plumbing).

### Reverb Body Implementation Hints

- **Stage function signatures**: lean toward `void spu94_reverb_<stage>(spu94_state *state)` for every stage — state carries every input (registers) and every output (intermediate tap values cached in state if D-04 is ever reversed; direct mutation of state buffers otherwise). Tests drive by setting up `spu94_state` + crafted work-buffer contents before calling the stage.
- **Internal helpers**: local `static inline` helpers inside `spu94_reverb.c` for recurring operations (typed register reads, buffer-tap fetch with wrap-bound check, per-stage accumulator flush). Phase 2's facade pattern proves this is compiler-optimizable to zero cost.
- **Register convenience**: consider local `static inline int16_t rd_i16(const spu94_state *s, spu94_reg_t r)` + `static inline uint16_t rd_u16(const spu94_state *s, spu94_reg_t r)` wrappers inside `spu94_reverb.c` to keep the stage body readable without noise. Planner decides.

</specifics>

<deferred>
## Deferred Ideas

### Planted Seeds for M4 (Controllers / Plugin Era)

Forward-looking ideas captured during the post-research supplemental discussion (2026-04-19). Not scoped for Phase 3 itself; each surfaces when the M4 Controllers layer / JUCE plugin work begins. All three are natural consequences of the D-07 and D-11 locks and the seam structure Phase 3 ships with.

- **Extended Modulation Mode toggle** (from D-08 resolution). Expose re-read-fresh-for-R v-register behavior as an opt-in Controllers mode on top of the default "freeze once per pair" behavior. Doubles modulation temporal resolution from 22.05 kHz (pair rate) to 44.1 kHz (half-step rate). Use cases: audio-rate LFOs, fast envelopes, FM-style parameter modulation, cross-rate tricks that pair-rate snapshots can't reach. The user keeps the strict-hardware default; musical-expressiveness users opt in. Implementation seam: body-caller dispatch — stage functions already take v-register values as parameters, so the Controllers layer picks between "one shared snapshot" and "fresh read for R" without touching stage code.

- **Overflow-magnitude as musical material** (from D-09 + D-11 resolution). The overflow-magnitude out-param on the hard-clip stage and the per-stage truncation err accumulators together describe the complete precision-loss surface of the reverb. Use cases for Controllers:
  - **Drive meter** — VU-style indicator reflecting aggregate overflow intensity, so users see when they're pushing the input into the rails.
  - **Soft-clip warmth lever** — mix a fractional amount of overflow magnitude back into the signal as a user-controllable "saturation"/"warmth" knob layered on top of the bit-faithful core. The core stays hardware-faithful; the knob is pure M4 extension.
  - **Overflow-modulated feedback** — overflow magnitude modulates IIR feedback depth, producing input-dependent tonal shifts (more drive → more character).
  - **Err-stream envelopes** — per-stage err accumulators drive automatic modulation; gives reverb character that's responsive to program material rather than static.
  Implementation seam: the accumulator and overflow fields live in `spu94_state`; Controllers adds read-only accessors at M4 time.

- **D-07 revert lever (comb-precision fallback)** — if plugin-era user testing on realistic preset material finds the cascading-clamp distortion too aggressive or unmusical, flip D-07 from Option B (cascade after each add) to Option A (int32 intermediate + single final `sat_s16`). One-TU swap of `spu94_reverb_comb` body + re-derive test reference tables. M5 hardware capture takes precedence if it ever reveals which option actual PS1 silicon uses; until then, musical judgment governs.

### Raised in Discussion, Routed Elsewhere

- **Public read-only stage-output observability accessors** — deferred per D-04 / D-21 reasoning. Added when a consumer (Controllers, EA, future plugin) demonstrates need.
- **L/R granular split at the function level** (option: `spu94_reverb_same_iir_l` + `spu94_reverb_same_iir_r`) — deferred to post-Area-2-research. If research reveals L/R must be interleaved at sub-stage granularity, refactor then; the simpler L-R-combined shape is the default.
- **Frequency-response witness comparison against lv2-psx-reverb** — PROJECT.md Key Decisions explicitly exclude lv2-psx-reverb from the frequency-response axis (it skips the 39-tap FIR by design). Phase 7 deliverable, not a Phase 3 concern.
- **Milestone 5 hardware validation of comb-sum and L/R timing choices** — captured in ROADMAP Phase 8 scope + PROJECT.md M5. Phase 3's seams preserve the ability to flip the pin if hardware capture in M5 shows divergence.

### Not Raised, but Potentially Relevant

- **Reverb work-buffer clear on power-on vs preserve-between-presets** — Phase 5's `spu94_load_preset` concern. Phase 3 assumes the buffer is in a well-defined state at tick start (caller's responsibility / Phase 2 init zeros it).
- **Stereo widening / spatial enhancement** — explicitly out of scope per PROJECT.md. Not a Phase 3 discussion.

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time.

### Scope Creep Rejections

None raised in Area 3 or Area 4 discussion. Areas 1, 2, 4 all deferred to research rather than expanded.

</deferred>

---

*Phase: 03-core-reverb-algorithm-hard-clip*
*Context gathered: 2026-04-19*
*Supplemental post-research discuss: 2026-04-19 — D-07..D-11 locked*
*Next step: `/gsd-plan-phase 3` — consumes CONTEXT.md + 03-RESEARCH.md together. No more research gating; all decisions ready for task breakdown.*
