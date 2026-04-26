# Phase 3: Core Reverb Algorithm + Hard Clip - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-19
**Phase:** 03-core-reverb-algorithm-hard-clip
**Areas discussed:** Comb-sum precision, L/R write timing, Stage isolation for tests, Hard-clip placement + vIIR anomaly + error-tap wiring

---

## Governing Directive (user, 2026-04-19)

> *"I just want all these elements AS CLOSE TO THE ORIGINAL PS1 SPU REVERB AS POSSIBLE."*

This directive was issued mid-Area-4 and applies retroactively to every research-gated resolution (D-07..D-10 in CONTEXT.md). It supersedes any "recommended" or "preliminary lean" option in this log: the research verdict for hardware fidelity wins over math-cleanliness, test-ergonomics, or simplicity.

---

## Area 1: Comb-sum intermediate precision

| Option | Description | Selected |
|--------|-------------|----------|
| int32 acc + single sat at end | acc32 = m1+m2+m3+m4 (int32 safe); final = sat_s16(acc32). Preserves partial information. Mathematically cleanest. | preliminary lean only |
| int16 saturate-per-add (cascading) | acc = sat_s16(...+m_i) after each of 4 multiplies. Saturation fires up to 3 times; older 16-bit DSP pattern. | — |
| int16 wrap per add, no sat until stage exit | Two's-complement wrap in the partial sums. Less common for audio. | — |

**User's choice:** Research-gated. User expressed that this is beyond their taste-based judgment; said *"Definitely want deep research on this one before committing."* CONTEXT.md D-07 records int32-accumulate-then-saturate as a preliminary lean for planner situational awareness only — governing directive subordinates any lean to the research verdict.

**Notes:** User explicitly invoked the Phase 2 mBASE research pattern (primary-source verbatim from nocash → ADR-0006) as the precedent. Milestone 5 hardware capture is the ultimate arbiter if witnesses disagree; D-22 seam preserves the flip.

---

## Area 2: L-tick vs R-tick register-write timing within a 44.1 kHz sample pair

No options table was presented. User routed this area to research on the same grounds as Area 1, immediately after the Area 1 deferral.

**User's choice:** Research-gated. User said *"yes, if area 2 is also not well documented, lets do deep research on that one too."* CONTEXT.md D-08 records "freeze v* values at tick start for both L and R" as a preliminary lean for planner situational awareness only.

**Notes:** nocash essentially silent on the intra-sample-pair ordering. The timing ambiguity matters specifically when the host writes an IMMEDIATE-policy `v*` register between the L and R halves of a tick (the D-04 split policy from Phase 2 makes this observable). Hardware truth is the arbiter; research consults nocash, secondary sources, then defers to hardware behavior.

---

## Area 3: Stage isolation for tests (SC-1)

### Q1: How do Phase 3 tests drive isolated stages?

| Option | Description | Selected |
|--------|-------------|----------|
| Internal header, one stage per function | stages declared in src-only spu94_reverb_internal.h, tests include directly, zero public-API surface | ✓ |
| Public API stage entry points | spu94_run_stage_X() on public API | — |
| Single tick + observable-only outputs | no per-stage driving; does not satisfy SC-1 literally | — |

**User's choice:** User said they didn't understand the question and asked Claude to *"choose the one that has the most robust testing vectors."* Locked as Claude's Discretion aligned with the robustness directive.

### Q2: Public read-only stage observability taps (D-23-style) in Phase 3 or defer like D-21?

| Option | Description | Selected |
|--------|-------------|----------|
| Defer like D-21 | internal header handles tests; no public taps until consumer demands | ✓ |
| Add now as read-only accessors | 6 int16 cached fields + 6 public accessors; Controllers-ready day one | — |

**User's choice:** Claude's Discretion. D-21 reasoning extended: no consumer yet justifies the cost.

### Q3: Stage function granularity — combined L+R or split?

| Option | Description | Selected |
|--------|-------------|----------|
| One function per stage, L+R internal | matches nocash stage boundaries; hand-derived reference targets same unit | ✓ |
| Split into L and R per stage | more granular test control; risks fake boundaries | — |
| Defer until Area 2 research | planner decides after L/R timing resolves | — |

**User's choice:** Claude's Discretion. Default to nocash-stage-granularity since that's what hand-derived references target; trivial refactor if Area 2 research reveals otherwise.

### Q4: File layout — single TU, by family, or one per stage?

| Option | Description | Selected |
|--------|-------------|----------|
| Single spu94_reverb.c + spu94_reverb_internal.h | matches Phase 2's one-concern-per-TU grain | ✓ |
| One TU per stage family (iir / comb / apf) | three TUs | — |
| One TU per stage (6 TUs) | most granular; fragmentation risk | — |

**User's choice:** Claude's Discretion. Matches Phase 2 pattern exactly.

**Notes on Area 3 as a whole:** User said *"Yeah I have no idea about any of that. Just choose the one that has the most robust testing vectors."* Claude documented the robustness-enhancing measures in CONTEXT.md `<specifics>` (per-stage hand-derived tables with INT16 MIN/MAX/0/saturation-tripping values, stage-composition equivalence tests, TEST-06 with both anomaly + control, TEST-07 fixed-point battery, Python ctypes reverb-network fuzz harness analogous to Phase 2's fuzz_buffer.py, err-tap invariant tests if D-11 lands scope (i)).

---

## Area 4: Hard-clip placement + vIIR anomaly + error-tap wiring + output-scale stage

### Q1: CORE-02 hard-clip scope

| Option | Description | Selected |
|--------|-------------|----------|
| Research-gated | add to /gsd-research-phase 3 scope | (not locked in session) |
| Pin now: input-side mix-bus clip only | one explicit clip between input-scaling and SAME IIR | — |
| Pin now: clip at every accumulation point | sat_s16 at every partial sum | — |

**User's choice:** Research-gated (same pattern as Areas 1/2). CONTEXT.md D-09 records the one-explicit-clip-stage pattern as a preliminary lean for planner situational awareness only.

### Q2: vIIR = -0x8000 anomaly — explicit branch or emergent?

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit branch + ADR | if (vIIR == INT16_MIN) negate final reverb result | (not locked in session) |
| Emergent from saturation policy | falls out of a specific Q15 multiply + sat combination | — |
| Research-gated | add to research scope | — |

**User's choice:** Research-gated. User said *"I'm not opposed to gathering extensive research on ALL these reverb design elements before deciding anything."* CONTEXT.md D-10 records explicit-branch as a preliminary lean for planner situational awareness only. Governing directive (hardware fidelity) wins.

### Q3: D-18 error-tap wiring scope

| Option | Description | Selected |
|--------|-------------|----------|
| All multiplies observable, off by default | per-stage int32 err accumulator in spu94_state | (not locked in session) |
| Only SAME/DIFF IIR feedback + comb sum | selective; simpler state footprint | — |
| None wired in Phase 3 | D-18 dormant until consumer arrives | — |

**User's choice:** Research-gated (bundled with Area 4's other sub-decisions). CONTEXT.md D-11 records scope (i) as a preliminary lean for planner situational awareness only. Note: this is SPU-94's own error-observation layer (D-18 from Phase 2), not a hardware-fidelity question — governing directive applies less directly here, but research still informs whether per-multiply error observation aligns with any documented hardware behavior.

### Q4: Output-scale stage — own function or folded?

| Option | Description | Selected |
|--------|-------------|----------|
| Own stage function | consistent with SC-1's six stages; test-vector symmetry | ✓ |
| Folded into spu94_tick | inline after 5 reverb-network stages | — |

**User's choice:** Claude's Discretion. Locked in CONTEXT.md D-03 after user confirmed *"choose the most robust testing vectors"* applies to housekeeping decisions.

---

## Claude's Discretion

- Area 3 Q1–Q4 (test isolation, observability taps, stage granularity, file layout)
- Area 4 Q4 (output-scale as own stage)
- Exact C prototypes of the seven stage functions
- Internal naming inside spu94_reverb_internal.h
- Test-file granularity under tests/unit/reverb/ (as long as each stage has its hand-derived reference table)

---

## Deferred Ideas

- Public read-only stage-output observability accessors — deferred per D-04/D-21 reasoning.
- L/R granular split at the function level — deferred to post-Area-2-research.
- Frequency-response witness comparison against lv2-psx-reverb — Phase 7 deliverable; lv2-psx-reverb excluded from this axis per PROJECT.md Key Decisions.
- Milestone 5 hardware capture divergence → seam flip — captured in CONTEXT.md governing-directive paragraph; D-22 seam preserves the option.

---

## User Interaction Notes

- User corrected Claude for parsing a "Recommended" option in a rejection payload as an actual answer ("Huh? why are you referencing Q1?"). Claude acknowledged and re-offered the question fresh.
- User expressed confusion at developer-plumbing questions in Areas 3 and 4. Claude re-framed in plain terms and offered "You decide, Claude" as an explicit option.
- User's final directive — *"I just want all these elements AS CLOSE TO THE ORIGINAL PS1 SPU REVERB AS POSSIBLE"* — was issued at the end of Area 4 and is now the governing constraint recorded at the top of CONTEXT.md's `<domain>` section.

---

# Supplemental Pass — Post-Research (2026-04-19)

After `03-RESEARCH.md` landed, the user requested a second discuss pass with plain-language framing: *"go through a discussion again with plain language style questions, and light technical jargon folded in where necessary."* The user noted that the research document "came off as gibberish" when written in its full technical register. All 5 previously research-gated gray areas (D-07..D-11) were walked through one at a time with analogies before options, and the 5 open questions surfaced by research were resolved as downstream consequences or Claude's-discretion items.

**Pacing:** one gray area per AskUserQuestion turn. User lock-ins came quickly once plain-language framing landed. Claude was reminded mid-pass to keep responses short enough to fit on one screen (captured as feedback memory).

---

## Gray Area 1: vIIR = -0x8000 anomaly mechanism (D-10)

**Plain-language framing:** The PS1 SPU has a documented quirk — when `vIIR` (the reverb decay coefficient) is set to its most-negative value (-0x8000), the final reverb output gets flipped upside-down. Nocash documents the **observable effect**, not the hardware **mechanism**. Question: how do we reproduce it?

| Option | Description | Selected |
|--------|-------------|----------|
| Explicit if-check | One line: negate the final result when vIIR == -0x8000. Matches what nocash documents verbatim. Auditable, easy to test, easy to swap. | ✓ |
| Emergent from math | Reverse-engineer a hidden hardware mechanism that produces the negation as a side-effect of saturation arithmetic. | — |

**User's choice:** Explicit if-check.

**Claude's follow-up** to user's "how difficult would Option B be?" question: emergent approach would require guessing at a mechanism nocash doesn't describe, with no way to verify correctness (both options produce identical observable output). Classified as speculative rabbit hole with no upside over Option A. User agreed and locked Option A.

**Notes:** Research confidence for Option A was HIGH — nocash wording is direct, DuckStation behavioral witness agrees. The user confirmed by intuition after plain-language explanation.

---

## Gray Area 2: Hard-clip stage placement (D-09)

**Plain-language framing:** The input multiply can overflow 16-bit range; hardware clamps it (the "hard clip"). Question: is the clamp its own named function in code, or folded into the input-scale step?

| Option | Description | Selected |
|--------|-------------|----------|
| Own named step | Tiny dedicated function between input-scale and SAME IIR. Satisfies CORE-02 "independently testable" with zero extra scaffolding. | ✓ |
| Folded into input-scale | One less function. Requires test scaffold to isolate the clip from the multiply. | — |

**User's mid-question prompt:** *"Could bit overflow variability be useful for the error accumulator, controller experimentation?"*

**Claude's answer:** Yes — the overflow magnitude (high bits lost to clamping) is a sibling observable to the truncation err-tap (low bits lost to shift). Together they form the complete precision-loss surface. Use cases for M4: drive meter, soft-clip warmth lever, overflow-modulated feedback, err-stream envelopes. Hardware fidelity unaffected (we still clip). Suggestion: lock D-09 Option A now, fold the overflow-tap extension into D-11 when we get to it.

**User's choice:** Own named step (Option A). Overflow-magnitude extension deferred to D-11 discussion.

**Notes:** This was the moment the user's M4 Controllers roadmap became an explicit driver of Phase 3 design decisions. From this point forward every decision was framed through the lens of "what does this enable for Controllers later?"

---

## Gray Area 3: Comb-sum intermediate precision (D-07)

**Plain-language framing:** The comb filter sums 4 delay taps. When to clamp — once at the end, or cascading after every add?

| Option | Description | Selected |
|--------|-------------|----------|
| Clamp once at the end (Recommended) | 32-bit intermediate sum, single clamp when done. Matches DuckStation/Mednafen behavioral witness. Cleaner math, less distortion character at extremes. | — |
| Clamp after every add | Cascade saturates through each intermediate sum. More saturation character at input extremes. No witness supports this but nocash doesn't rule it out. | ✓ |
| You decide | Go with the recommendation; both are seam-swappable at M5. | — |

**User's choice:** Clamp after every add.

**Claude's note after user choice:** this complements the overflow-magnitude observable from D-09/D-11 — Option B generates more clip events per tick, giving the err/overflow stream more signal for Controllers experimentation. Diverges from the DuckStation/Mednafen witness (int32 accumulate) but nocash silent, so not contradicted. M5 hardware capture remains the seam trigger if it turns out silicon does Option A.

**User's follow-up directive:** *"Please make a note on D-07 Option B... In the event that the distortion accumulation is too much, too extreme, or not musical, whatever it may be... This is a viable lever to pull, and maybe revert to 32-bit wrapper."*

**Claude's action:** captured as "D-07 revert lever" in the Planted Seeds for M4 subsection of CONTEXT.md. If plugin-era preset testing finds the cascading distortion unmusical, flip `spu94_reverb_comb` body to the int32-accumulate shape.

**Notes:** Taste-driven, Controllers-roadmap-informed decision. User's musical ear took precedence over the behavioral witness because (a) the witness isn't primary source, (b) the seam is one-TU-swappable, (c) the M5 hardware capture is the ultimate authority.

---

## Gray Area 4: L/R register-write timing within a sample pair (D-08)

**Plain-language framing:** Stereo reverb is processed as L-then-R in two consecutive 44.1 kHz cycles. If a game writes to a v-register between the two halves, does R see the new value or the L-frozen value? Research flagged this as the MEDIUM-LOW-confidence decision (most speculative).

**User's mid-question prompt** on Claude's own research summary sentence — *"in a future Controllers/M4 scenario where you're modulating reverb parameters at audio rate ... it happens constantly and the answer changes your sound"*: *"So which option supports this more? Option B? Obviously musical controllers are in future scope."*

**Claude's answer:** Yes — Option B (re-read fresh for R) supports audio-rate modulation meaningfully better because L and R see different v-register values within a single pair, effectively doubling modulation resolution from 22.05 kHz to 44.1 kHz. Caveats: (1) possible L/R coefficient divergence (feature or artifact depending on context); (2) if real hardware does Option A, Option B gives us *more* modulation headroom than the original chip had, deviating from the "as close to PS1 as possible" directive. Suggestion: expose both as a Controllers toggle ("Extended Modulation Mode"), and choose which is the Phase 3 default.

| Option | Description | Selected |
|--------|-------------|----------|
| Default A, expose B later | Bit-faithful-first. Freeze v* at pair start. M4 Controllers layer exposes Extended Modulation Mode (B) as opt-in. | ✓ |
| Default B, expose A later | Musical-first. Re-read v* fresh for R. M4 exposes strict-hardware mode (A) as opt-in. | — |
| Default A, revisit at M5 | Skip the toggle; pick safer default, flip via seam if M5 shows hardware does B. | — |
| Default B, revisit at M5 | Skip the toggle; lean musical, flip back if M5 shows hardware does A. | — |

**User's choice:** Default Option A, expose B later. Keeps Phase 3 bit-faithful by default; Extended Modulation Mode becomes an explicit Controllers feature at M4.

**Notes:** This was the decision the user engaged most actively with — responding to the musical implications rather than the spec-silent implementation detail. The planted-seed mechanism let the user commit to a strict default today while preserving the expressive capability for future work. Captured in Planted Seeds for M4.

---

## Gray Area 5: Error-tap wiring scope (D-11)

**Plain-language framing:** Every Q15 multiply loses some precision (low bits chopped off). The `_with_err` pattern from Phase 1 can expose those lost bits as a separate stream. Phase 3 is the first real consumer — how much do we wire up?

**Expanded by Claude:** given the D-09 discussion's overflow-magnitude thread, the expanded Option (i) should also extend the hard-clip function to hand back an overflow-magnitude out-param (high bits lost to saturation) — matching the err-tap shape for a complete precision-loss surface.

| Option | Description | Selected |
|--------|-------------|----------|
| All + overflow-tap (Recommended, expanded) | Per-stage err accumulator on every multiply, PLUS overflow-magnitude out-param on the hard-clip. Full precision-loss surface area. | ✓ |
| All multiplies only | Every multiply gets an err tap. Skip the overflow-magnitude on the clip. | — |
| Feedback multiplies only | Only IIR feedback and comb sum get err taps. | — |
| None, defer to Controllers need | No err-tap wiring; add when M4 demonstrates concrete need. | — |

**User's choice:** All + overflow-tap.

**Notes:** Most richly-scoped option. Runtime cost is trivial (one int32 add per multiply, one int32 write in the clip, no branches). Sets up M4 Controllers with the full precision-loss surface to draw from — drive meters, soft-clip warmth levers, overflow-modulated feedback, err-stream envelopes. Captured in both D-11 lock and Planted Seeds for M4.

---

## Derived / Claude's-Discretion (Research's 5 Open Questions)

All 5 fall out of the above locks; none required user input:

1. **Hard-clip input type** — MUST be int32 to feed the D-11 overflow-magnitude out-param. Resolved as int32 by D-11 expansion.
2. **Reverb body caller name** — Claude's discretion. Recommend `spu94_reverb_body` per research (avoids collision with public `spu94_tick`).
3. **Input-scale return shape** — int32 out-parameters (consistent with hard-clip's int32 output shape).
4. **`err` remainder sign treatment for vIIR=INT16_MIN multiply** — leave unsigned-in-spirit; `err` represents magnitude of truncation loss, not a signed value. Document inline.
5. **Fuzz harness granularity** — single `fuzz_reverb.py` mirroring Phase 2's `fuzz_buffer.py`. Split only if ~10⁶-step duration becomes painful (it won't; Phase 2 is ~2.5s).

---

## User Interaction Notes (Supplemental Pass)

- User said after Option A framing for D-10: *"The plain english was very helpful. THAT makes sense to me, when stated in that way. Please always try to use that methodology moving forward."* Captured as durable feedback memory (`feedback_plain_language_short.md`) — plain-English first, analogies over jargon, keep responses short enough to fit on one screen.
- User flagged long responses getting cut off in their UI, causing them to select "clarify" options just to advance. Same feedback memory captures the length target.
- User's M4 Controllers roadmap became the lens through which every gray area got evaluated after D-09. The planted-seed mechanism (revert levers, opt-in Controllers toggles) was the bridge between "bit-faithful default for M1 shipping" and "expressive headroom for M4."
- Two user decisions diverged from the research-recommended defaults: D-07 (chose cascading clamp over int32 accumulate — taste-driven) and D-11 (expanded scope (i) with overflow-magnitude tap — Controllers-roadmap-driven). Both are seam-swappable; both remain bit-faithful to the extent spec demands it.
