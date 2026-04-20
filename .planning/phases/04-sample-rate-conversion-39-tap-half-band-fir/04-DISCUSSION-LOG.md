# Phase 4: Sample Rate Conversion (39-tap half-band FIR) - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-20
**Phase:** 04-sample-rate-conversion-39-tap-half-band-fir
**Areas discussed:** FIR arithmetic & precision (A), Pipeline integration (B), Coefficient provenance & ADR scope (C), Witness scope & test strategy (D)

---

## Area A — FIR Arithmetic & Precision

### Q1 — How do we write the 39-tap math?

| Option | Description | Selected |
|--------|-------------|----------|
| Fast (folded ~10 mults) | Skip 19 zero coefs, fold 9 symmetric pairs. Same output as literal. MCU-friendly. | ✓ (after research) |
| Literal 39 mults | All 39 multiply-adds, including zeros. 1:1 with nocash table. | |
| Both, swap by seam | Default literal, MCU flip to fast. Double test surface. | |

**User's journey:** Initially chose "Literal 39 mults" for spec-readability. Asked whether literal was what PS1 silicon actually did. After honest answer (unknowable from outputs; 1994 transistor-budget inference favors folded), revised to Fast folded form. Literal form kept as permanent audit reference in test binary with bit-identity test.

**Notes:** Authenticity-by-output is the only testable measure. Both forms produce identical numbers with a single final `sat_s16`. Folded is best-guess silicon; literal is best-match-spec-page. Compromise: folded ships + literal is permanently compiled into tests as witness.

### Q2 — Accumulator width

| Option | Description | Selected |
|--------|-------------|----------|
| int32 + proof | Fits this filter. Worst-case-inputs test + proof comment. MCU-friendly. | ✓ |
| int64 defensive | No proof needed. Slight MCU cost. | |
| int32 fallback to int64 | Try int32, flip if proof margin tight. | |

**User's choice:** int32 + proof.

**Notes:** Discussion covered what integer overflow wrap actually sounds like (sharp discrete clicks triggered at specific amplitudes; different from clipping and aliasing; no musical utility). Stake is not "does it sound different," but "eliminate wrap-clicks by proof vs brute force."

### Q3 — FIR clamp policy

| Option | Description | Selected |
|--------|-------------|----------|
| Clamp once at end | Clean transparent FIR. Character lives in Phase 3, not the boundary. | ✓ (as default) |
| Cascade clamp every add | FIR adds its own distortion on top of reverb character. | ✓ (as opt-in seam) |
| Clamp once + record overflow | Clean math, record overflow magnitude for M4 drive-meter. | ✓ (always on) |

**User's choice:** "Both available. Clamp-once OR cascade, ALWAYS record overflow." — combined Options 1 + 2 + 3 via a seam. Default clamp-once (hardware-authentic, clean boundary). Cascade clamp as opt-in for M4 musical character toggle. Overflow magnitude unconditionally recorded regardless.

**Notes:** Phase 3 D-07 chose cascade-clamp for the comb filter (character stage). Phase 4 deliberately diverges because the FIR is an I/O boundary, not a character stage. User's instinct to preserve optionality aligns with the seam-first architecture from Phase 2 D-22.

### Q4 — Err-tap scope on FIR

| Option | Description | Selected |
|--------|-------------|----------|
| One err per output | Cheap. Two fields in state. Single boundary signal for M4. | |
| Full per-multiply parity | ~10 extra int32 adds per sample. Maximum M4 material. Consistent with Phase 3 D-11. | ✓ |
| None | FIR contributes nothing to precision-loss surface. | |

**User's choice:** Full per-multiply parity.

**Notes:** User confirmed future-plugin sonic use is real (drive meters, warmth knobs, overflow-modulated feedback, envelope followers) — precision-loss data is silent metadata in Phase 4 but audible M4 material.

---

## Area B — Pipeline Integration

### B1 — Where does the FIR sit relative to `spu94_tick`?

| Option | Description | Selected |
|--------|-------------|----------|
| Internal 44.1 kHz wrapper (Option A, internal-only) | Complete chain, src-only header, not on public include path. Phase 5 composes. | ✓ |
| Public 44.1 kHz wrapper (Option A, public) | Same chain, exposed to external callers. Locks in API surface. | |
| Primitives only (Option B) | Ship decimate + interpolate separately. Phase 5 assembles. | |

**User's choice:** Option A, internal-only.

**Notes:** User asked whether Option A paints us into a corner. Distinguished public vs internal: public = forever API commitment + lock-in risk; internal = testable/ear-checkable chain at Phase 4 end with no API commitment. Phase 5 reuses the internal wrapper to compose block-based `spu94_process` without API conflict.

### B2 — Stereo delay-line shape

| Option | Description | Selected |
|--------|-------------|----------|
| Separate per-channel buffers | Clear "L's memory, R's memory." Independent updates. Standard DSP. | ✓ |
| Interleaved single buffer | L, R, L, R... in one 78-slot buffer. Sometimes cache-friendlier on modern CPU. | |

**User's choice:** Separate per-channel buffers.

**Notes:** User recalled a possible PS1 behavior of "only calculate L, invert for R." Research agent (2026-04-20) spawned to verify; confirmed no such behavior in the FIR — user's recollection is conflation with either the reverb engine's L/R time-multiplex (same silicon alternates L one tick, R the next; values not derived from each other) or the SAME/DIFF cross-feed inside the reverb network. FIR has no L/R coupling; separate per-channel state is bit-faithful.

### B3 — Latency disclosure

| Option | Description | Selected |
|--------|-------------|----------|
| Document only (ADR) | Explain group delay in DECISIONS.md. Future plugin hard-codes. | |
| Document + expose programmatically | ADR + `spu94_get_latency_samples()` returning 38. Free DAW PDC at M4. | ✓ |
| Neither | Silent about latency. Users figure it out. | |

**User's choice:** Document + expose programmatically.

**Notes:** Total FIR round-trip group delay ~860 µs (38 samples at 44.1 kHz). Inaudible to humans but load-bearing for DAW plugin delay compensation and Eurorack sync.

---

## Area C — Coefficient Provenance & ADR Scope

### C1 — Coefficient sourcing

| Option | Description | Selected |
|--------|-------------|----------|
| Single source | Pick jsgroth, verbatim, move on. | |
| Two-source cross-reference | jsgroth + bannister forum. Compare byte-for-byte. | |
| Three-source cross-reference | + hunt for hitmen c02 / psxdev / academic teardown. Max confidence. | ✓ |
| Defer to M5 hardware capture | Ship provisional, lock at M5. | |

**User's choice:** Three-source cross-reference.

**Notes:** Research agent (2026-04-20) surfaced the critical finding: **nocash does not publish the FIR coefficients.** Coefficients are community reverse-engineering (jsgroth writeup + bannister.org SCPH-5501 hardware readout). Three-source cross-reference matches the project's epistemic-honesty ethic and avoids inheriting a single source's typo.

### C2 — Coefficient storage form (locked as plumbing)

**Locked at Claude's discretion:** Q15 native `int16_t`, verbatim from verified sources, dedicated `.c` translation unit (not a header), one coefficient per line with tap-index comment. No sonic impact, no design tradeoff.

### C3 — Transcription posture (locked as plumbing)

**Locked at Claude's discretion:** Integer literal values only. No prose, tables, or commentary copied from nocash/jsgroth/bannister. Bibliography cites all sources with URL + post/section identifier. Matches PROJECT.md licensing posture.

### C4 — ADR granularity

| Option | Description | Selected |
|--------|-------------|----------|
| Fewer big ADRs (~3) | Bundled "FIR arithmetic contract" + coefficient ADR + SC-4 mandate. | |
| My proposal (6 ADRs) | One ADR per major locked decision. | |
| Maximum granularity (~8–12) | Every distinct decision gets its own ADR. Most citable. | ✓ |

**User's choice:** Maximum granularity. Planner decides exact count during plan breakdown.

**Notes:** User asked what ADR means mid-discussion; explanation given (Architecture Decision Record, numbered entry in docs/DECISIONS.md, first-class project deliverable per PROJECT.md). Max granularity matches the project's "DECISIONS.md as standalone PSX reverse-engineering contribution" ethic.

---

## Area D — Witness Scope & Test Strategy

### D1 — Mednafen/DuckStation FIR-implementation investigation

| Option | Description | Selected |
|--------|-------------|----------|
| Do it here in Phase 4 | Investigate in Phase 4 research. Classify as IN-AXIS or OUT-OF-AXIS witnesses before locking our own FIR. | ✓ |
| Defer to Phase 7 | Save empirical test for the dedicated witness-diff phase. | |

**User's choice:** Do it in Phase 4.

**Notes:** Per PROJECT.md Key Decision, Mednafen and DuckStation were committed to Phase 4 empirical testing. Classification happens in `04-RESEARCH.md`; Phase 7 builds the automated harness that uses the classification.

### D2 — Test vector scope

| Option | Description | Selected |
|--------|-------------|----------|
| Impulse + DC + overflow only | Strict to success criteria. | |
| + frequency sweep | Validate half-band lowpass shape. | |
| + round-trip transparency | Measure FIR chain transparency for band-limited input. | |
| All three | Most rigorous. | ✓ |

**User's choice:** All three.

**Notes:** Plus a Python ctypes fuzz harness (`fuzz_fir.py`) following the Phase 2/3 pattern. All test vectors documented in CONTEXT.md specifics section.

---

## Claude's Discretion

- Exact C prototypes and names of FIR helper/wrapper functions
- Exact name of internal 44.1 kHz wrapper
- Circular-buffer-index vs shift-register for 39-sample delay lines (bit-identical; circular index recommended)
- Cascade-clamp seam mechanism (compile-time vs runtime)
- Exact ADR split/count within max-granularity rule
- Exact round-trip transparency threshold
- Test-file granularity under `tests/unit/fir/`
- `spu94_get_latency_samples()` as inline constant-returning function vs `.c`-defined

## Deferred Ideas

### Planted Seeds for M4

- **FIR-bypass toggle (aliasing on/off)** — user's explicit framing: aliasing is a legitimate musical tool, not garbage (chiptune, vaporwave, bitcrusher-adjacent). Expose as M4 character switch. Default FIR-on (hardware-authentic); opt-in FIR-off (lo-fi character). Phase 4 structures D-07 wrapper such that bypass is trivial to wire at M4.
- **Cascade-clamp toggle** (from D-04) — M4 user-facing character switch: clean FIR boundary vs FIR-adds-distortion-on-top-of-reverb.
- **FIR overflow + err-stream as M4 material** (from D-05/D-06) — drive meters, warmth knobs, overflow-modulated modulation sources, envelope followers.
- **Programmatic latency for DAW PDC** (from D-09) — `spu94_get_latency_samples()` paid for in Phase 4, consumed at M4 JUCE `prepareToPlay`.

### Deferred to Phase 7

- Automated witness-diff harness regression tests (TEST-03)
- Golden-file regression snapshots (TEST-04)

### Deferred to Milestone 5

- Hardware-capture arbitration of clamp policy (D-03/D-04)
- Hardware-capture arbitration of folded-vs-literal form (D-01)
- Coefficient table authoritative ratification (D-10)

### Raised in Discussion, Routed Elsewhere

- User's "R = -L" recollection — routed to research pass, resolved as conflation with reverb L/R time-multiplex or SAME/DIFF cross-feed; FIR is unchanged
- Public block-based `spu94_process` — Phase 5
- Mid-stream register writes across FIR boundary — Phase 5

## Session Meta-Notes

- User requested plain-English framing throughout, analogies over jargon, short responses (UI cuts long output). Session adapted mid-flow.
- Research agent (`general-purpose`) spawned once: to verify PS1 SPU stereo/FIR architecture. Returned: FIR has no L/R coupling; nocash does not publish FIR coefficients; coefficients come from jsgroth + bannister hardware readout; silicon likely time-multiplexes one MAC unit across L/R.
