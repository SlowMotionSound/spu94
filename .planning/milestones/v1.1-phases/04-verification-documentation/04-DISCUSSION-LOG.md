# Phase 4: Verification + Documentation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-27
**Phase:** 04-verification-documentation
**Areas discussed:** Known-vector test scope, Golden file strategy, ADR numbering + scope

---

## Known-Vector Test Scope

### Q1: Phase 1 already covers TEST-01 vectors. Add more or verify existing?

| Option | Description | Selected |
|--------|-------------|----------|
| Verify existing + document | Audit against TEST-01 checklist, fill gaps, produce coverage map. No redundant duplication. | ✓ |
| Fresh test suite | Separate test file with all vectors even if redundant. | |
| You decide | Claude picks. | |

**User's choice:** Verify existing + document

---

## Golden File Strategy

### Q1: Where should ADPCM golden files live?

| Option | Description | Selected |
|--------|-------------|----------|
| Subdirectory per preset | adpcm/ inside existing preset dirs (tests/golden/hall/adpcm/). Same infra. | ✓ |
| Separate top-level directory | tests/golden-adpcm/ with own preset dirs. | |
| You decide | Claude picks. | |

**User's choice:** Subdirectory per preset

### Q2: Which presets and inputs?

| Option | Description | Selected |
|--------|-------------|----------|
| Hall + Room + Echo, impulse + sine | 3 presets x 2 inputs = 6 goldens. | |
| All 10 presets, impulse + sine | Maximum coverage, 20 goldens. | ✓ |
| You decide | Claude picks. | |

**User's choice:** All 10 presets
**Notes:** User asked about chirp/frequency sweep tests — added as follow-up question.

### Q3: Add chirp as third input?

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, add chirp | 20Hz→20kHz log sweep as third input. All 10 presets x 3 inputs = 30 goldens. | ✓ |
| No, keep impulse + sine | Two inputs enough for regression gating. | |

**User's choice:** Yes, add chirp

---

## ADR Numbering + Scope

### Q1: Numbering scheme?

| Option | Description | Selected |
|--------|-------------|----------|
| Continue sequence | ADR-0047 through ADR-0053. One continuous log. | ✓ |
| M2 prefix | ADR-M2-01 through ADR-M2-07. Milestone separation. | |
| You decide | Claude picks. | |

**User's choice:** Continue sequence

### Q2: ADR detail level?

| Option | Description | Selected |
|--------|-------------|----------|
| Standard | Same format as existing: title, context, decision, rationale, alternatives. 10-20 lines. | ✓ |
| Deep with code references | Include file:line refs, test names, numeric examples. | |
| You decide | Claude picks. | |

**User's choice:** Standard

---

## Claude's Discretion

None — user made all decisions directly.

## Deferred Ideas

- ADPCM solo mode in JUCE standalone — preview ADPCM coloration without reverb (from Phase 3 checkpoint, belongs in M4)
