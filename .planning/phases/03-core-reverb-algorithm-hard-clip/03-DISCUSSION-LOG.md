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
