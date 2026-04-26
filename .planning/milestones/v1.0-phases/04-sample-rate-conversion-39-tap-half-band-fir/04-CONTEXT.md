# Phase 4: Sample Rate Conversion (39-tap half-band FIR) - Context

**Gathered:** 2026-04-20
**Status:** Ready for planning — all gray areas locked. Research gating required in Phase 4 itself (coefficient sourcing + witness empirics); planner sequences research artifact before implementation plans.

<domain>
## Phase Boundary

Phase 4 delivers **the 44.1 ↔ 22.05 kHz sample-rate conversion I/O boundary** using nocash's 39-tap half-band FIR (coefficients sourced from community reverse-engineering; see C1). The FIR is what closes the fidelity gap that lv2-psx-reverb explicitly leaves open — without it, the reverb engine inherits whatever aliasing/imaging the host's rate conversion produces and the top-end character drifts from hardware.

**In scope:**
- Input decimator (44.1 → 22.05 kHz) as a 39-tap half-band FIR using community-verified coefficient table
- Output interpolator (22.05 → 44.1 kHz) symmetric to the decimator
- Bit-faithful integer arithmetic across the FIR chain with proven-no-overflow accumulator (SC-3)
- Half-rate architecture documented in DECISIONS.md with lv2-psx-reverb's freq-response-axis exclusion (SC-4; pre-established in PROJECT.md Key Decisions)
- Internal (not public) 44.1 kHz per-sample wrapper chaining decimate → `spu94_tick` → interpolate
- Per-channel FIR state (independent L and R delay lines, shared coefficient table)
- Programmatically-exposed latency contract: `spu94_get_latency_samples()` returns 38
- Precision-loss surface continued from Phase 3: per-multiply err-taps on the FIR + overflow-magnitude tap on the clamp stage
- Clamp-policy seam: default clamp-once (hardware-authentic clean resampling boundary), opt-in cascade clamp for future M4 character toggle
- Empirical Mednafen / DuckStation FIR-implementation investigation in Phase 4 research (PROJECT.md Key Decision commitment)
- Test-vector battery: impulse-response shape, DC round-trip bias/drift check, worst-case overflow proof, frequency sweep (half-band lowpass shape validation), round-trip transparency measurement

**Governing directive (user, 2026-04-20):**

> *"Most authentic"* — where "authenticity by output" is the only testable measure. Where silicon-internal structure is unknowable from outputs, best-guess-1994-silicon inference governs (D-01 folded form, per era's transistor-budget pressure).

**Explicitly NOT in scope:**
- `spu94_process` block-based public entry point — Phase 5 (API-03). Phase 4's internal 44.1 kHz wrapper is the building block Phase 5 composes; it does NOT appear on any public header.
- 10 factory reverb presets — Phase 5 (CORE-09)
- Python ctypes bindings / wheel — Phase 6
- Witness-diff harness regression infrastructure — Phase 7 (TEST-03). Phase 4 does a one-shot Mednafen/DuckStation empirical investigation to determine their FIR status; the automated diff harness is Phase 7.
- Golden-file regression tests — Phase 7
- MCU cross-compile validation — Phase 8
- FIR-bypass toggle (aliasing on/off as musical choice) — Milestone 4 plugin layer; Phase 4 ensures the FIR is structured such that bypass is trivial to wire later

</domain>

<decisions>
## Implementation Decisions

### Area A — FIR arithmetic & precision (LOCKED)

- **D-01: FIR math uses the folded-multiply form (~10 multiplies per output sample) with the literal 39-tap coefficient table stored verbatim and used as an audit reference.** Best-guess-1994-silicon authenticity (transistor budget era pressured every design toward the folded trick); verified bit-identical to a literal 39-multiply reference implementation via a bit-identity equivalence test. The literal form never ships in production code paths but remains permanently compiled in the test binary as an audit witness.
- **D-02: FIR accumulator is `int32_t` with a derived-from-coefficients no-overflow proof.** The proof lives in a comment block adjacent to the accumulator declaration, backed by a worst-case-inputs test case that drives the accumulator to its peak magnitude. MCU-friendly (no 64-bit adds on Cortex-M). If the proof's margin narrows uncomfortably, the seam allows promotion to `int64_t` without touching callers.
- **D-03: Default clamp policy is "clamp once at final stage output."** All 39 weighted products sum in the int32 accumulator; the final right-shift + `sat_s16` at the output stage is the only saturation point inside the FIR. FIR stays a transparent resampling boundary; all reverb character lives in the Phase 3 stages, not here. Diverges from Phase 3 D-07's cascade-clamp choice — deliberately, because Phase 3's comb is a character stage and Phase 4's FIR is not.
- **D-04: Opt-in cascade-clamp seam.** The FIR stages ship a policy-variant seam (function pointer or compile-time switch — planner decides) that flips from clamp-once to cascade-clamp after every add. Default: clamp-once. M4 plugin toggle: cascade-clamp = "FIR boundary adds distortion character on top of reverb character." M5 hardware capture is the authority if silicon ever reveals which PS1 actually did.
- **D-05: Overflow-magnitude tap on the FIR clamp is always on.** When the final `sat_s16` trips, the high bits lost to saturation are recorded into `state->fir_overflow_decimator` / `state->fir_overflow_interpolator` (int32 fields). Feeds the M4 drive-meter / warmth / overflow-modulation surface established in Phase 3 D-11. Zero conditional logic on the hot path (unconditional write; zero when not saturating).
- **D-06: Per-multiply err-tap parity with Phase 3.** Every one of the ~10 folded multiplies calls `q15_mul_truncate_with_err` and feeds its truncation remainder into per-boundary int32 accumulators in `spu94_state` (`err_fir_decimator`, `err_fir_interpolator`). Maximum M4 raw material; costs ~10 int32 adds per channel per output sample. Consistent with Phase 3's precision-loss surface.

### Area B — Pipeline integration (LOCKED)

- **D-07: Phase 4 ships an internal 44.1 kHz per-sample wrapper, not a public one.** A new internal function (name at planner's discretion — e.g., `spu94_sample_44k1` or `spu94_fir_chain_step`) lives in a src-only header and chains `spu94_fir_decimate` → `spu94_tick` → `spu94_fir_interpolate`. Not exposed via `include/spu94/`. Phase 5's public `spu94_process` composes this internal wrapper into block-based processing. Phase 4 ends with a working, testable, ear-checkable end-to-end chain — without committing to an API shape Phase 5 hasn't designed yet.
- **D-08: Separate per-channel FIR state.** Two independent 39-sample int16 delay-line buffers (`fir_delay_l[39]`, `fir_delay_r[39]`) per direction (decimator input side, interpolator input side), each with its own circular-buffer index. Coefficient table is shared (one `static const int16_t fir_coef[39]`). Clean, bit-faithful, matches jsgroth's independent-deques description of the silicon's L/R behavior. Confirmed by 2026-04-20 research pass (FIR has no L/R coupling; user's "R = -L" recollection was likely conflated with the reverb engine's L/R time-multiplex or the SAME/DIFF cross-feed — neither lives in the FIR).
- **D-09: Latency contract is documented AND programmatically exposed.** A new public function `spu94_get_latency_samples(void)` returns the total round-trip FIR group delay (38 samples = 19 decimator + 19 interpolator, at the 44.1 kHz reference rate). Documented in DECISIONS.md as an inherent property of the 39-tap linear-phase half-band structure. Pays off at M4 when the JUCE plugin reports accurate latency to the DAW for plugin delay compensation; also serves Eurorack sync use cases at M5+.

### Area C — Coefficient provenance & ADR scope (LOCKED)

- **D-10: Coefficient table sourced via three-source cross-reference.** Phase 4 research pulls the 39 coefficient values from (a) jsgroth's "PlayStation: The SPU, Part 3 — Reverb" writeup, (b) the bannister.org forum thread where coefficients were read from SCPH-5501 hardware, and (c) at least one additional independent source (candidates: hitmen c02 SPU docs, psxdev forum archives, academic PS1 teardown papers, or another hardware-readout report). Byte-for-byte comparison across all three; any disagreement flagged and resolved with a documented rationale. Nocash is NOT a source for the coefficients — nocash documents the reverb at 22.05 kHz but does not publish the FIR coefficient table itself (2026-04-20 research finding). All three sources cited in `docs/BIBLIOGRAPHY.md`.
- **D-11: Coefficient storage is Q15 native `int16_t`, verbatim from verified sources, in a dedicated `.c` translation unit (not a header).** One coefficient per line with tap-index comment. Locked as implementation plumbing — no gray area. (Claude's discretion ratified.)
- **D-12: Coefficient transcription is facts-only.** Integer literal values only; no prose, tables, or commentary copied from nocash/jsgroth/bannister/etc. Bibliography cites every source with URL and (where applicable) post/section identifier. Matches PROJECT.md licensing posture. Locked as implementation plumbing — no gray area. (Claude's discretion ratified.)
- **D-13: Phase 4 produces ADRs at maximum granularity.** Every distinct locked decision above (D-01 through D-12 + coefficient-sourcing and latency contract) gets its own numbered ADR in `docs/DECISIONS.md`, plus the SC-4-mandated half-rate architecture / lv2-psx-reverb-frequency-axis-exclusion ADR. Planner decides the exact numbering split (~8–12 ADRs) when laying out the implementation plans. Narrow ADRs = easier to cite individually from code comments, bug reports, and future-milestone research.

### Area D — Witness scope & test-vector strategy (LOCKED)

- **D-14: Mednafen and DuckStation FIR-implementation status is investigated empirically in Phase 4 research.** The Phase 4 research artifact (`04-RESEARCH.md`) runs a controlled signal through each emulator (e.g., an impulse, a near-Nyquist sine, or a band-limited sweep) using their standalone CLI modes where available, measures the output, and determines whether they implement the half-band FIR or skip it like lv2-psx-reverb. Outputs only — no source reading (licensing posture). Findings are logged as facts in the research document and feed the SC-4-mandated ADR: lv2-psx-reverb is definitively excluded from the frequency-response witness axis; Mednafen and DuckStation are individually classified as IN-AXIS or OUT-OF-AXIS witnesses per empirical result. The automated diff harness remains Phase 7 (TEST-03) work.
- **D-15: Test vectors go beyond SC-1/2/3's mandated minimum.** Every Phase 4 test target must cover:
  - **Impulse response** (SC-1): feed a unit impulse through each boundary; assert the 39-tap symmetric half-band shape at the output. Per-channel.
  - **DC round-trip** (SC-2): feed a DC (constant-amplitude) signal through decimate→interpolate bypassing the reverb; assert no bias, no drift, filter symmetry to machine precision.
  - **Worst-case overflow proof** (SC-3): drive the accumulator with inputs chosen to hit its peak magnitude; assert the int32 accumulator never overflows and the derived bound is accurate.
  - **Frequency sweep**: sweep a sine from 20 Hz to 22 kHz through the decimator alone (or decimate→interpolate bypass); measure the filter's frequency response; assert the shape matches expected half-band lowpass (passband flat to ~10 kHz, transition through 11.025 kHz, stopband floor).
  - **Round-trip transparency**: feed band-limited audio (no content above 10 kHz) through decimate→interpolate (reverb bypassed); measure residual error vs delayed-by-38-samples original; assert below a threshold (planner sets the threshold — a reasonable starting point is the theoretical Q15 quantization floor).
- **D-16: Python ctypes fuzz harness for the FIR.** Following the Phase 2 / Phase 3 pattern (`fuzz_buffer.py`, `fuzz_reverb.py`): a new `tests/python/fuzz_fir.py` drives 10⁶ random inputs through the decimator → interpolator chain and checks invariants (bounded output, no buffer corruption, latency matches `spu94_get_latency_samples()`, err/overflow tap fields within expected ranges).

### Architectural Principles (carried forward from Phases 1–3)

- **D-22 Extensibility Seams (Phase 2):** every gray-area resolution in Phase 4 is a pinnable mechanism. Clamp policy (D-03/D-04), folded-vs-literal form (D-01), and accumulator width (D-02) each ship as seams so Controllers / M4 / M5 can flip without touching surrounding code.
- **D-23 Observability (Phase 2):** Phase 4 preserves read-only observability. Err-tap and overflow-tap fields are `spu94_state` members readable via future internal accessors; no mutation path exposed. `spu94_get_latency_samples()` is the only Phase 4 public addition and is pure read.
- **D-24 Controllers as Future Consumer (Phase 2):** Phase 4 API design assumes M4 Controllers consumes the FIR surface. Seams from D-04, D-05, D-06 are the specific hooks; the latency contract (D-09) is the JUCE/host-integration hook.
- **ADR-0001 (Q15 truncation direction):** every multiply in Phase 4 uses `q15_mul_truncate_with_err` — truncation toward −∞, bit-identical to Phase 1's locked semantic.
- **Phase 3 D-07 / D-11 precedents explicitly diverged and extended:** D-03 diverges from D-07's cascade-clamp default (FIR is boundary, not character); D-06 extends D-11's per-multiply err-tap pattern to the FIR; D-05 extends D-11's overflow-magnitude tap pattern from the hard-clip stage to the FIR's clamp stages.

### Claude's Discretion (within the locked decisions above)

- Exact C prototypes and names of the internal FIR functions (`spu94_fir_decimate_step`? `spu94_fir_push_and_decimate`? etc.)
- Exact name of the internal 44.1 kHz wrapper (`spu94_sample_44k1`? `spu94_fir_chain_step`? `spu94_io_step`?)
- Circular buffer index vs shift register for the 39-sample delay line (both are bit-identical; circular-buffer index is free of per-sample memmove cost on MCU and is the recommended default — but planner decides)
- Whether the cascade-clamp seam (D-04) is a compile-time `#ifdef` switch, a runtime function pointer, or a direct caller-selectable variant — depends on what matches the surrounding code style best
- Split vs merger of ADRs (D-13 says max granularity; planner decides where natural ADR boundaries fall — e.g., whether coefficient storage Q15 choice gets its own ADR or is folded into the coefficient-sourcing ADR)
- Exact threshold for round-trip transparency (D-15 last bullet) — a reasonable starting point is a small multiple of the theoretical Q15 quantization noise floor; planner derives from test-vector analysis
- Test-file granularity under `tests/unit/fir/` — single TU per stage vs combined
- Whether `spu94_get_latency_samples()` is a `static inline` returning a constant `#define`, or a `.c`-defined function returning a `#define` constant — both satisfy the public-API contract

### Folded Todos

None — no pending todos matched Phase 4 scope at discussion time.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Core Value, bit-faithfulness directive, licensing posture (nocash facts usable verbatim, nocash prose paraphrased, GPL emulator source off-limits as primary source), Key Decisions table — specifically the two Phase-4-relevant locks: "Implement 22.05kHz half-rate processing with nocash's 39-tap half-band FIR at both I/O boundaries" and "lv2-psx-reverb explicitly excluded as a witness for frequency-response / sample-rate-accuracy."
- `.planning/REQUIREMENTS.md` — Phase 4 owns CORE-06 (input-side FIR) and CORE-07 (output-side FIR).
- `.planning/ROADMAP.md` § Phase 4 — the four success criteria must all be TRUE. SC-1 (impulse response shape), SC-2 (DC round-trip + symmetry), SC-3 (accumulator-width proof), SC-4 (DECISIONS.md entry for half-rate architecture + lv2-psx-reverb-frequency-axis exclusion).

### Prior Phase CONTEXT.md files (must read for consistency)

- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — Q15 API shape, truncation direction, inline-reference-table test pattern, grep-guard + UBSan CI.
- `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md` — D-01 through D-24 (architectural principles + seam philosophy).
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md` — D-01 through D-11: stage-function shape, internal-header pattern, err-tap per-multiply precedent (D-11), overflow-magnitude tap precedent (from hard-clip stage), cascade-clamp precedent (D-07 — Phase 4 deliberately diverges).

### Prior Phase Research (shape precedent for Phase 4 research)

- `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md` — research-artifact structure precedent.
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-RESEARCH.md` — primary-source evidence + witness comparison + decision proposal pattern. Phase 4's `04-RESEARCH.md` mirrors, with additional empirical-witness-investigation section (D-14).

### ADR Log (appended in Phase 4)

- `docs/DECISIONS.md` — existing ADRs: ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR anomaly), ADR-0003 (UBSan no_sanitize policy), ADR-0004 (extensibility taps), ADR-0005 (per-register write-timing policy), ADR-0006 (mBASE snap-on-write), ADR-0007 (comb-sum cascading sat), ADR-0008 (L/R write timing), ADR-0009 (hard-clip placement), ADR-0010 (vIIR anomaly branch), ADR-0011 (per-multiply err-tap scope + overflow-magnitude tap). Phase 4 appends (~8–12 new ADRs per D-13 max-granularity rule — exact count at planner's discretion): half-rate architecture + lv2-psx-reverb exclusion (SC-4 mandate), FIR math form (D-01), FIR accumulator width (D-02), FIR clamp policy + seam (D-03 + D-04), FIR overflow-magnitude tap (D-05), FIR err-tap scope (D-06), internal wrapper shape (D-07), per-channel state (D-08), latency contract (D-09), coefficient sourcing + bibliography (D-10), witness empirics + Mednafen/DuckStation classification (D-14).

### External References (paraphrased only — do NOT transcribe per PROJECT.md licensing posture)

- **nocash PSX SPU documentation** (problemkaputt.de / psx-spx.consoledev.net) — primary authority for the 22.05 kHz reverb processing rate, the reverb-engine L/R time-multiplex, the vIIR = -0x8000 anomaly. **Critical finding from 2026-04-20 research pass:** nocash does NOT publish the 39-tap FIR coefficient table. Coefficient sourcing defers to community reverse-engineering (D-10).
- **jsgroth "PlayStation: The SPU, Part 3 — Reverb"** (https://jsgroth.dev/blog/posts/ps1-spu-part-3/) — documents the FIR's two-independent-deques L/R architecture + confirms the reverb clock alternates L/R per 44.1 kHz tick. One of the three coefficient sources per D-10.
- **bannister.org forum thread "PS1 SPU FIR Coefficients"** — SCPH-5501 hardware readout of the 39-tap coefficient values. Second of the three coefficient sources per D-10.
- **hitmen c02 PSX SPU documentation** — secondary corroborating source. Candidate third coefficient source per D-10.
- **Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer source** — their **output audio** is used as a witness in Phase 4 research (D-14, empirical FIR-implementation investigation); their **source code** is not read. Any consultation to resolve a specific ambiguity is logged in DECISIONS.md.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phases 1–3)

- `include/spu94/spu94_q15.h` — `q15_mul_truncate`, `q15_mul_truncate_with_err`, `sat_s16`, `q15_add_sat`, `_Static_assert` ASR guard. Every Phase 4 FIR multiply uses `q15_mul_truncate_with_err` per D-06 scope.
- `src/spu94/spu94_state_internal.h` — single ODR home for `struct spu94_state`. Phase 4 adds: `fir_delay_l_in[39]`, `fir_delay_r_in[39]`, `fir_delay_l_out[39]`, `fir_delay_r_out[39]` (int16), per-channel delay indices (small ints), `err_fir_decimator` (int32), `err_fir_interpolator` (int32), `fir_overflow_decimator` (int32), `fir_overflow_interpolator` (int32). Current `sizeof(spu94_state)` is 168 bytes with 16216 bytes of `SPU94_STATE_SIZE_MAX` headroom — Phase 4 additions (~340 bytes for delay lines + handful for indices/accumulators) comfortably fit.
- `src/spu94/spu94_tick.c` — per-22.05-kHz-tick entry point. Phase 4 does NOT modify `spu94_tick` itself. The new internal 44.1 kHz wrapper (D-07) calls `spu94_tick` as its middle step.
- `src/spu94/spu94_reverb.c` — Phase 3's reverb-body TU; shape precedent for `spu94_fir.c` (Phase 4's FIR stage functions) and for a potential `spu94_io_chain.c` (the 44.1 kHz wrapper glue).
- `src/spu94/spu94_reverb_internal.h` — Phase 3's src-only header pattern. Phase 4 adds `spu94_fir_internal.h` for FIR stage declarations and the internal 44.1 kHz wrapper declaration.
- `tests/unit/reverb/` — Unity test harness with inline-reference-table pattern. Phase 4 adds `tests/unit/fir/` mirroring the pattern.
- `tests/python/fuzz_buffer.py` + `tests/python/fuzz_reverb.py` — template for `tests/python/fuzz_fir.py` (D-16).
- CMake `spu94_obj` OBJECT library + `spu94_shared` + `spu94_static` — Phase 4's new `.c` files under `src/spu94/` picked up automatically.
- CI: grep-guard (no `float`/`double`/`malloc` in core), verify-no-heap-symbols, clang-tidy, cppcheck, UBSan — Phase 4 code passes all unchanged. The FIR is integer-only by construction.

### Established Patterns

- **One-concern-per-TU grain** (Phase 2): `spu94_fir.c` holds "the FIR stages" concern. The internal 44.1 kHz wrapper may live in its own TU (`spu94_io_chain.c`) or as a stub inside `spu94_fir.c` — planner decides.
- **Internal header for internal symbols** (Phases 2, 3): `spu94_fir_internal.h` follows `spu94_reverb_internal.h`'s pattern. Not installed. Tests include directly.
- **Inline hand-computed reference tables in test `.c` files** (Phase 1): every Phase 4 test TU carries a `{input, expected_output}` table with INT16_MIN / INT16_MAX / 0 / saturation-tripping / impulse / near-Nyquist values.
- **Pitfall 4 single-call-site discipline** (ADR-0005): every Phase 4 FIR helper has exactly one call site. Internal wrapper is the single caller of the decimate and interpolate functions.
- **ADR-style DECISIONS.md entries prepended at top** (Phase 1 D-12, Phase 2 style, Phase 3 D-07..D-11): Phase 4 appends ~8–12 new ADRs at top per D-13 max-granularity.
- **Research artifact structure** (Phase 2, 3 `*-RESEARCH.md`): primary-source evidence verbatim (where fact, per licensing), secondary corroboration, witness comparison — Phase 4's research additionally includes the empirical Mednafen/DuckStation FIR-implementation investigation (D-14).

### Integration Points

- **Phase 5 (`spu94_process` + presets)** wraps Phase 4's internal 44.1 kHz wrapper in the public block-based entry point. Mid-stream register writes at block boundaries compose cleanly because the FIR is stateless relative to register state (it only has delay-line state, which is tick-invariant).
- **Phase 6 (Python bindings)** wraps the C API unchanged. `spu94_get_latency_samples()` is the only new public symbol Phase 4 adds; Phase 6 surfaces it as a Python function.
- **Phase 7 (witness diff + golden files)** consumes Phase 4's FIR output. The Phase 4 research pre-classifies Mednafen and DuckStation as IN-AXIS or OUT-OF-AXIS frequency-response witnesses, so Phase 7's diff harness knows which witnesses to trust on which axes.
- **Phase 8 (MCU cross-compile)** inherits the int32-accumulator + no-64-bit-adds discipline of Phase 4. The accumulator-width proof from D-02 is what keeps `arm-none-eabi-gcc` lean on Cortex-M7.
- **Future Milestone 4 (Controllers / JUCE plugin)** consumes four specific Phase 4 seams: the cascade-clamp toggle (D-04, becomes a user-facing character switch), the FIR-bypass toggle (planted seed, aliasing on/off as musical choice), the err-tap + overflow-tap surface (D-05 + D-06, drive meters / warmth / overflow-modulation), and the latency contract (D-09, for DAW PDC).
- **Future Milestone 5 (Hardware validation)** flips the pin on D-01 (folded vs literal form — unobservable from outputs so doesn't matter either way) and D-03/D-04 (clamp policy — hardware capture reveals which PS1 silicon actually does, which becomes the authoritative default). Phase 4's seams preserve the ability to switch without rewriting.

</code_context>

<specifics>
## Specific Ideas

### Test-Vector Robustness (continuing Phase 3's directive)

The planner should build these test measures into Phase 4's plans:

- **Per-boundary hand-derived reference tables** (decimator, interpolator): each test TU carries a `{input_samples, expected_output_samples}` table with coverage cases:
  - Unit impulse — verifies the 39-tap symmetric half-band shape
  - DC (constant-amplitude) — verifies no bias, no drift, filter symmetry
  - INT16_MIN and INT16_MAX sustained — verifies worst-case overflow behavior of the int32 accumulator
  - Saturation-tripping pulse sequence — verifies the final `sat_s16` trips correctly and the overflow-magnitude tap records the right magnitude
  - Alternating ±INT16_MAX — designed to hit peak accumulator magnitude (the overflow-proof test case per D-02)
- **Frequency-domain shape test**: drive the decimator with a chirp or a sine-sweep; FFT the output; assert the half-band lowpass frequency response matches the expected shape (passband to ~10 kHz, transition through 11.025 kHz, stopband floor at least -70 dB or similar nocash-derived target).
- **Round-trip transparency test**: `abs(audio_out - delayed_by_38(audio_in)) < threshold` for band-limited input below 10 kHz, reverb bypassed.
- **Bit-identity test**: fuzz the folded implementation (D-01) against the literal-39-tap reference across 10⁵ random inputs; assert bit-for-bit equality.
- **Err-tap and overflow-tap invariants** (D-05, D-06): err accumulator is zero for non-truncating input; overflow accumulator is zero for input that stays within the accumulator bound; both monotonically non-decreasing under stress.
- **Latency assertion test**: feed a unit impulse at t=0, measure the sample index of peak output response, assert `peak_index == spu94_get_latency_samples() == 38` (per D-09).

### Research Artifact (`04-RESEARCH.md`) Required Contents

- **Section on coefficient sourcing**: verbatim extraction from all three sources, side-by-side comparison table, any disagreement flagged with proposed resolution. Bibliography entries for each source.
- **Section on Mednafen/DuckStation empirical investigation** (D-14): methodology (input signal choice, capture path, measurement technique), results per emulator, classification as IN-AXIS or OUT-OF-AXIS frequency-response witness, confidence level.
- **Section on accumulator-width proof** (D-02): derived worst-case magnitude of the 39-product sum given the verified coefficient table; comparison to INT32_MAX; margin of safety. Provides the proof text Phase 4 code comments can reference.
- **Section on bit-identity between folded and literal forms** (D-01): formal argument that the two forms produce identical outputs given a single final `sat_s16` (i.e., given D-03's default clamp policy). If the cascade-clamp seam (D-04) is engaged, bit-identity no longer holds — the research document notes this explicitly.

### Reverb-Bypass Hook for Testing (recommended, planner decides)

Some of D-15's test vectors (DC round-trip, round-trip transparency) require bypassing the reverb in the middle of the decimate→tick→interpolate chain. Options: (a) a test-only build-time flag that replaces the reverb body with a pass-through, (b) a test-only wrapper that chains `decimate → interpolate` directly without `spu94_tick`, (c) running the full chain with all register values zeroed such that the reverb is a mathematical no-op. Option (b) is the cleanest and matches the "per-stage isolation" test ethic from Phase 3. No Controllers seam needed — this is purely test scaffolding.

</specifics>

<deferred>
## Deferred Ideas

### Planted Seeds for M4 (Controllers / Plugin Era)

Forward-looking ideas captured during Phase 4 discussion. Not scoped for Phase 4 itself; each surfaces when the M4 Controllers layer / JUCE plugin work begins.

- **FIR-bypass toggle (aliasing on/off)** — expose a musical switch: bit-faithful = FIR on (clean top end, PS1 hardware behavior), raw/crunchy = FIR off (aliasing preserved as lo-fi character à la chiptune, vaporwave, bitcrusher-adjacent). User's explicit framing (2026-04-20): aliasing is a legitimate musical tool, not garbage. Default remains FIR-on at M4 for hardware authenticity; the toggle is opt-in character. Implementation: the internal 44.1 kHz wrapper (D-07) dispatches between the FIR chain and a pass-through chain. Phase 4's job is to structure D-07 such that this bypass is trivial to wire at M4 — specifically, keep the FIR chain composable rather than monolithic.

- **Cascade-clamp toggle (FIR character switch)** — from D-04. Expose the cascade-clamp seam as an M4 user-facing character switch: clean FIR boundary (default, hardware-authentic) vs FIR-adds-distortion-character-on-top-of-reverb. Sits alongside the FIR-bypass toggle as a second axis of boundary character control. Implementation seam is D-04's policy-variant mechanism.

- **FIR overflow-magnitude + err-stream as M4 material** — from D-05 + D-06. The boundary precision-loss surface mirrors Phase 3's reverb precision-loss surface. M4 Controllers can route these into drive meters, warmth knobs, overflow-modulated modulation sources, envelope followers, etc. — same shape as the Phase 3 planted-seed list, now applied at the I/O boundary as well.

- **Programmatic latency for DAW PDC** (from D-09) — the `spu94_get_latency_samples()` contract is what lets the M4 JUCE plugin report accurate latency to the host. No M4 work needed beyond calling it at `prepareToPlay` time and passing the result to `AudioProcessor::setLatencySamples()`. Cheap payoff from a contract we paid the cost of in Phase 4.

### Deferred to Phase 7

- **Automated witness-diff harness regression tests**. Phase 4 does a one-shot empirical Mednafen/DuckStation investigation (D-14) to classify them as witnesses; Phase 7 builds the continuous-integration diff harness (TEST-03) that uses the classification.
- **Golden-file regression snapshots**. Phase 4 ships hand-derived reference tables for its own tests; Phase 7 ships the preset × input grid of golden-file snapshots (TEST-04).

### Deferred to Milestone 5

- **Hardware capture arbitration of clamp policy**. D-03 (clamp-once default) is a principled guess, not a hardware observation. If M5 hardware capture ever reveals that PS1 silicon does cascade-clamp inside its FIR (unlikely, based on 1994-era engineering priorities, but unknowable from outputs), flip D-03/D-04 accordingly.
- **Hardware capture arbitration of folded-vs-literal form**. D-01 is best-guess 1994 silicon. Output is identical either way, so M5 only matters if hardware capture reveals a bit-level discrepancy we didn't predict.
- **Coefficient table authoritative ratification**. D-10's three-source community cross-reference is our best pre-M5 answer. If any coefficient disagreement cannot be resolved between the three community sources, Phase 4 picks a working value (documented rationale) and M5 is the final authority.

### Raised in Discussion, Routed Elsewhere

- **The user's "R = -L" recollection** — routed to the Phase 4 research pass, which confirmed it's a conflation with either (a) the reverb engine's L/R time-multiplex (same silicon processes L one tick, R the next) or (b) the SAME/DIFF cross-feed inside the reverb. Neither lives in the FIR. D-08 is unaffected: separate per-channel FIR state remains correct.
- **Public block-based `spu94_process`** — Phase 5, not Phase 4. D-07's internal wrapper is the building block; the public API shape is Phase 5's call.
- **Mid-stream register writes across the FIR boundary** — Phase 5's concern. Phase 4's FIR state is orthogonal to register state; mid-stream writes to registers don't touch the FIR delay lines, and the delay lines are untouched by `spu94_reset` logic in Phase 2 (Phase 2 Plan 01 zeros `spu94_state` wholesale, which includes the Phase 4 delay lines for free).

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time.

### Scope Creep Rejections

None raised. Discussion stayed within the Phase 4 domain boundary.

</deferred>

---

*Phase: 04-sample-rate-conversion-39-tap-half-band-fir*
*Context gathered: 2026-04-20*
*Next step: `/gsd-plan-phase 4` — consumes CONTEXT.md. Planner will first invoke research (for coefficient cross-reference + empirical Mednafen/DuckStation investigation + accumulator-width proof derivation), then task breakdown.*
