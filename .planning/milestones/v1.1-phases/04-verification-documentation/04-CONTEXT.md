# Phase 4: Verification + Documentation - Context

**Gathered:** 2026-04-27
**Status:** Ready for planning

<domain>
## Phase Boundary

Prove the ADPCM codec is correct and document every gray-area resolution. No new DSP, no new features — this phase produces tests, golden files, and ADRs that formalize decisions already made in Phases 1-3.

</domain>

<decisions>
## Implementation Decisions

### Known-Vector Test Scope
- **D-01:** Phase 1 already has 19 decode + 8 encode unit tests covering every vector TEST-01 asks for (all-zero, impulse, each filter 0-4, shift 0/6/12, shift 13/14/15, clamp positive/negative, state carry, filter clamp 5→4). Phase 4 audits existing coverage against TEST-01's checklist, fills any small gaps, and produces a coverage map — no redundant test duplication.
- **D-02:** TEST-02 round-trip determinism is already covered by `test_encode_decode_roundtrip_deterministic` in Phase 1. Phase 4 verifies and documents this coverage.

### Golden File Strategy
- **D-03:** ADPCM golden files live as `adpcm/` subdirectories inside existing preset directories (e.g., `tests/golden/hall/adpcm/impulse.wav`). Same SHA-256 sidecar pattern, same `regenerate_goldens.py` infrastructure.
- **D-04:** All 10 presets get ADPCM goldens, with 3 inputs each: impulse, sine (1kHz), and chirp (20Hz→20kHz logarithmic sweep). Total: 30 ADPCM golden files + 30 SHA-256 sidecars.
- **D-05:** Chirp input is a new test signal — a 20Hz→20kHz logarithmic sweep showing how ADPCM coloration varies across the frequency range. Generated programmatically (not a recorded file).

### ADR Numbering + Scope
- **D-06:** ADPCM ADRs continue the existing sequence: ADR-0047 through ADR-0053. One continuous decision log across the whole project.
- **D-07:** Standard ADR format matching existing entries: title, context, decision, rationale, alternatives considered. 10-20 lines each. Decisions are already made — Phase 4 formalizes them.
- **D-08:** The 7 ADPCM ADRs cover: (1) rounding vs truncation, (2) shift 13-15 policy, (3) filter 5-7 policy, (4) division semantics (>>6 vs /64), (5) encoder error metric, (6) encoder tiebreaking, (7) tail block padding.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Existing Test Infrastructure
- `tests/unit/adpcm/test_adpcm_decode.c` — 19 existing decode tests (coverage baseline for TEST-01)
- `tests/unit/adpcm/test_adpcm_encode.c` — 8 existing encode tests (coverage baseline for TEST-02)
- `tests/unit/adpcm/CMakeLists.txt` — Test registration pattern

### Golden File Infrastructure
- `scripts/regenerate_goldens.py` — Golden file regeneration script (extend for ADPCM variants)
- `tests/golden/hall/` — Example preset golden directory structure (pattern for adpcm/ subdirs)
- `config/witness_diff_thresholds.json` — Per-preset thresholds (ADR-0024)

### Decision Records
- `docs/DECISIONS.md` — 46 existing ADRs (continue sequence from ADR-0047)
- `.planning/STATE.md` § Accumulated Context > Decisions — ADPCM gray-area resolutions to formalize

### ADPCM Implementation (source of truth for ADR content)
- `src/spu94/adpcm.c` — Codec implementation with inline comments on gray-area choices
- `include/spu94/spu94_adpcm.h` — Public API documenting shift/filter behavior
- `.planning/research/m2-adpcm/` — Research documents with cross-reference evidence

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- `regenerate_goldens.py`: Extends naturally — add `--adpcm` flag to generate ADPCM-on variants in `adpcm/` subdirectories
- `tests/golden/*/` directory structure: Pattern for adpcm/ subdirectories
- `docs/DECISIONS.md` format: 46 existing ADRs establish the template for new entries
- Phase 1 test files: Most TEST-01/02 vectors already implemented

### Established Patterns
- Golden files use SHA-256 sidecars with `--check` mode in `regenerate_goldens.py`
- ADRs follow: `## ADR-NNNN: Title` / Context / Decision / Rationale / Alternatives Considered
- CTest registration via `add_test()` in CMakeLists.txt with label grouping

### Integration Points
- `regenerate_goldens.py` needs `--adpcm` flag and chirp signal generation
- `tests/golden/CMakeLists.txt` (or equivalent) needs ADPCM golden regression gates
- `docs/DECISIONS.md` gets 7 new ADR entries appended

</code_context>

<specifics>
## Specific Ideas

- Chirp input: 20Hz→20kHz logarithmic sweep, same sample rate and duration as existing test inputs, generated programmatically
- ADPCM goldens show the coloration delta: same input through same preset, with and without ADPCM — the existing reverb-only goldens serve as the "off" baseline

</specifics>

<deferred>
## Deferred Ideas

- **ADPCM solo mode in JUCE standalone** — preview ADPCM coloration without reverb, directly in the GUI (noted during Phase 3 human verification, belongs in M4 plugin UI)

</deferred>

---

*Phase: 4-Verification + Documentation*
*Context gathered: 2026-04-27*
