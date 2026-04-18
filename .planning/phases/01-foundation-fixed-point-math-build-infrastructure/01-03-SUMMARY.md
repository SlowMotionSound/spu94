---
phase: 01-foundation-fixed-point-math-build-infrastructure
plan: 03
subsystem: docs
tags: [adr, decisions, license, ubsan, q15, no-sanitize, bibliography]

# Dependency graph
requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: "CONTEXT.md decisions D-12, D-13, D-14 fixing ADR format/location; RESEARCH.md ADR templates for Q15 semantics, vIIR anomaly, and SPU94_NO_SANITIZE_INTEGER macro"
provides:
  - "docs/DECISIONS.md — ADR log seeded with ADR-0001 (Q15 multiply), ADR-0002 (vIIR = -0x8000), ADR-0003 (UBSan no_sanitize policy + SPU94_NO_SANITIZE_INTEGER macro)"
  - "LICENSE placeholder with defensive deferred-pick stance (neither MIT nor Apache-2.0 granted)"
  - "Pre-authorized SPU94_NO_SANITIZE_INTEGER macro template that Phase 3 can adopt verbatim"
  - "Empty Annotated Functions registry table in ADR-0003 ready for Phase 3 and later to append rows"
affects: [phase-03-reverb-network, phase-07-golden-files-witness-diff, milestone-1-completion]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "ADR Nygard-style format with added Sources section (Status / Context / Decision / Consequences / Sources)"
    - "Bibliography placeholder citations (BIB-NNN) for facts sourced from external docs; prose is original SPU-94 wording"
    - "Surgical, enumerated UBSan no_sanitize discipline (function-scoped, registry-tracked)"
    - "Defensive license placeholder — explicitly neither grants nor withholds rights; names deferral point"

key-files:
  created:
    - "docs/DECISIONS.md"
    - "LICENSE"
  modified: []

key-decisions:
  - "Q15 multiply uses arithmetic shift right (ASR) with sat_s16 saturation; INT16_MIN^2 saturates to INT16_MAX (ADR-0001)"
  - "vIIR = -0x8000 anomaly reproduced faithfully; negation applied at vIIR application site, NOT inside q15_mul_truncate (ADR-0002)"
  - "UBSan no_sanitize is surgical (function-scoped only), enumerated in an ADR-0003 registry table, with a cross-compiler SPU94_NO_SANITIZE_INTEGER macro pre-authorized for Phase 3 (ADR-0003)"
  - "LICENSE placeholder is explicitly defensive: names MIT and Apache-2.0 as candidates, grants neither, defers pick to end of Milestone 1, omits 'All rights reserved' and copyright line (T-01-06 mitigation)"

patterns-established:
  - "ADR structure: `## ADR-NNNN: <title>` heading + five bold sections in order (Status, Context, Decision, Consequences, Sources) — grep-able for future CI checks"
  - "Source citations use BIB-NNN placeholders pointing to docs/BIBLIOGRAPHY.md (Phase 7 deliverable)"
  - "Nocash discipline: paraphrase facts, never transcribe prose — preserves licensing flexibility"
  - "ADRs are not edited in place to reverse decisions; supersession is a new ADR with Status: Superseded-by"

requirements-completed:
  - DOCS-01
  - DOCS-05

# Metrics
duration: 3min
completed: 2026-04-18
---

# Phase 01 Plan 03: Decisions Log + License Placeholder Summary

**Seeded docs/DECISIONS.md with three Phase 1 ADRs (Q15 multiply, vIIR = -0x8000 anomaly, UBSan no_sanitize policy + macro template) and added a defensive LICENSE placeholder deferring the MIT/Apache-2.0 pick to end of Milestone 1.**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-04-18T20:23:54Z
- **Completed:** 2026-04-18T20:26:26Z
- **Tasks:** 2
- **Files modified:** 2 (both newly created)

## Accomplishments

- `docs/DECISIONS.md` (253 lines) with three well-formed ADRs — each carries the full Status / Context / Decision / Consequences / Sources section set in order.
- ADR-0003 carries the `SPU94_NO_SANITIZE_INTEGER` cross-compiler macro definition verbatim, pre-authorized for Phase 3 adoption, plus an "Annotated Functions" registry table (empty in Phase 1 per plan).
- ADR-0001 and ADR-0002 close out the two Phase 1 gray-area decisions tracked in STATE.md (Q15 `>> 15` direction; vIIR = -0x8000 policy).
- `LICENSE` placeholder (23 lines) defensively worded per T-01-06: explicitly states no permissive license is granted, names MIT and Apache-2.0 as candidates, defers pick to end of M1, omits both a copyright line and "All rights reserved."
- All nocash references use paraphrase + `BIB-001` / `BIB-002` placeholder citations, honoring the PROJECT.md licensing posture (prose is SPU-94's own; facts cited for Phase 7 bibliography resolution).

## Task Commits

Each task was committed atomically:

1. **Task 1: Author docs/DECISIONS.md with ADR-0001, ADR-0002, ADR-0003** — `c54e600` (docs)
2. **Task 2: Write LICENSE placeholder with deferred-pick stance** — `2c60e0d` (docs)

_Plan metadata commit (SUMMARY.md) is owned by the orchestrator per parallel-wave protocol._

## Files Created/Modified

- `docs/DECISIONS.md` — new. ADR log with three Phase 1 seed entries: ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR = -0x8000 anomaly), ADR-0003 (UBSan no_sanitize policy with `SPU94_NO_SANITIZE_INTEGER` macro template and empty Annotated Functions registry).
- `LICENSE` — new. Defensive placeholder explicitly stating no permissive license is granted by this file; MIT and Apache-2.0 named as candidates; final pick deferred to end of Milestone 1.

## Decisions Made

Three ADR decisions were recorded this plan (these ARE the plan's output); additionally, during authoring:

- **Kept the plan's numerical ordering of ADRs (0001 → 0002 → 0003) in the seed commit rather than reversing to most-recent-first (0003 → 0002 → 0001).** D-13 says new entries are prepended over time; the file header explicitly documents that Phase 1's seed entries are presented in numerical order for readability of the initial commit. Future phases prepend per D-13. This matches the plan's Task 1 instruction verbatim.
- **LICENSE uses the plan's exact wording.** No paraphrasing or embellishment — the plan's defensive language was already tuned to avoid accidental grants or denials, and any edits would be net-negative for the T-01-06 mitigation posture.

No other phrasing refinements were made versus the plan template; the ADR prose and LICENSE text are exactly as specified.

## Deviations from Plan

None — plan executed exactly as written.

Authoring discipline honored (no nocash prose transcribed; all references are paraphrases that cite `BIB-001` / `BIB-002` for future bibliography resolution). Human-review checkbox for "no nocash verbatim text" is self-confirmed: every nocash citation in DECISIONS.md is of the form "nocash documents that…" or "nocash explicitly describes this as…" — reporting the existence and nature of the documentation, not quoting its prose, tables, or phrasing.

## Issues Encountered

None.

Cross-reference verification for Plan 01's `include/spu94/spu94_q15.h` citing ADR-0001 could not be run from this worktree because Plan 01 is being executed in a parallel worktree. That cross-check is deferred to the orchestrator's post-wave verification step — both sides of the reference (header comment in q15.h, ADR-0001 heading in DECISIONS.md) are specified identically in their respective plans.

## User Setup Required

None — no external service configuration required.

## Next Phase Readiness

- Phase 1 success criterion 5 satisfied: DECISIONS.md contains resolved entries for (a) Q15 multiply semantics and (b) vIIR = -0x8000 policy; LICENSE placeholder notes the deferred pick. ADR-0003 is a bonus operational pre-authorization for Phase 3.
- Phase 3 can now adopt `SPU94_NO_SANITIZE_INTEGER` verbatim from ADR-0003 and is pre-authorized to annotate specific functions that model documented SPU hardware saturation or wraparound (mix-bus hard clip, vIIR negation), provided each annotated function adds a row to the ADR-0003 Annotated Functions registry.
- Phase 7 (BIBLIOGRAPHY.md creation, DOCS-03) has concrete placeholder keys to resolve: `BIB-001` (nocash PSX SPU reverb formula section), `BIB-002` (jsgroth.dev PS1 SPU series), `BIB-003` (Clang UBSan reference), `BIB-004` (GCC `no_sanitize_undefined` attribute).
- Milestone 1 closure task: replace LICENSE placeholder with the verbatim chosen license (MIT or Apache-2.0).

## Self-Check: PASSED

- `docs/DECISIONS.md` — FOUND (253 lines, 3 `## ADR-` headings, all five sections present in each ADR, `SPU94_NO_SANITIZE_INTEGER` macro present, Accepted dates all `2026-04-18, Phase 1`)
- `LICENSE` — FOUND (23 lines, contains "deferred", "MIT", "Apache", "Milestone 1"; does NOT contain "All rights reserved" or a copyright line)
- Commit `c54e600` — FOUND in git log (Task 1: DECISIONS.md seed)
- Commit `2c60e0d` — FOUND in git log (Task 2: LICENSE placeholder)

---
*Phase: 01-foundation-fixed-point-math-build-infrastructure*
*Completed: 2026-04-18*
