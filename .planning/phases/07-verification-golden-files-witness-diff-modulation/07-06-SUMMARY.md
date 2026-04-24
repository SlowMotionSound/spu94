---
phase: 07-verification-golden-files-witness-diff-modulation
plan: 06
subsystem: bibliography-decisions-close-out
tags: [docs-03, adr-batch, bibliography, cross-ref-gate, d-22, phase-7-close-out]
requires:
  - phase-1-ubsan-posture (ADR-0003 originator of BIB-003 / BIB-004 references)
  - phase-7-plan-01 (COVERAGE.md context for ADR-Phase-7-A; scipy / pytest / strace installed)
  - phase-7-plan-02 (goldens + Docker digest for ADR-Phase-7-C, ADR-Phase-7-D)
  - phase-7-plan-03 (witness-diff harness for ADR-Phase-7-B; BIB-014 consumer)
  - phase-7-plan-04 (modulation harness for ADR-Phase-7-E, ADR-Phase-7-F; LEVERS-CATALOG.md)
  - phase-7-plan-05 (benchmark gate split for ADR-Phase-7-G)
provides:
  - bibliography-cross-reference-validator (scripts/check_bibliography_refs.py)
  - bibliography-conformance-gate (tests/conformance/test_bibliography_crossref.py)
  - bibliography-four-tier-cluster-polish (docs/BIBLIOGRAPHY.md D-22)
  - phase-7-adr-batch (ADR-Phase-7-A..H, eight ADRs, all 22 D-XX decisions landed)
  - bib-003-bib-004-promotion-from-placeholder (ADR-0003 orphans resolved)
  - bib-014-through-bib-020-additive-entries
affects:
  - future-phase-adrs-must-cite-existing-bib-nnn-or-add-new-entry (ctest gate)
  - future-docs-03-amendments-are-additive-on-the-cluster-scheme
  - milestone-2-adpcm-primary-sources-slot-into-primary-sources-cluster
tech-stack:
  added:
    - (none; pytest + python3 already present from Plan 07-01)
  patterns:
    - additive-plus-cluster-polish (not rewrite)
    - cross-reference-ctest-gate-under-bibliography-label
    - paraphrase-discipline-as-reviewer-responsibility-not-code-check
    - eight-adr-per-work-area-batch (six minimum; planner chose full eight)
key-files:
  created:
    - scripts/check_bibliography_refs.py (~95 lines, executable)
    - scripts/test_check_bibliography_refs.py (~43 lines, 2 meta-tests)
    - tests/conformance/test_bibliography_crossref.py (~46 lines, 2 structural tests)
  modified:
    - docs/BIBLIOGRAPHY.md (146 -> 294 lines; 13 entries -> 20 entries; four-tier clustering)
    - docs/DECISIONS.md (+299 lines; eight Phase 7 ADRs prepended above ADR-Phase-6-H; zero existing ADRs modified)
    - tests/conformance/CMakeLists.txt (+30 lines; two new ctest entries labeled "bibliography")
decisions:
  - "Full eight-ADR split adopted (not the six-ADR minimum). ADR-Phase-7-F (SC-4 reinterpretation) is kept standalone from ADR-Phase-7-E (modulation harness) because the reinterpretation is semantic / communicative and the harness is operational — combining them would bury the SC-4 phrasing lock inside implementation detail. ADR-Phase-7-H (BIBLIOGRAPHY posture) is kept standalone because it governs the BIBLIOGRAPHY beyond M1 (M2/M3/M5 will extend the tier scheme) and because conflating it with the other content-area ADRs weakens the forcing-function framing."
  - "BIB-003 (Clang UBSan) and BIB-004 (GCC no_sanitize) were cited as 'future' in ADR-0003 since Phase 1 commit time; Task 1 promoted them to full entries as a Rule-3 blocking prerequisite for the cross-reference checker's exit-0 acceptance criterion. Any earlier resolution would have required a Phase 1 amendment; landing them inside Phase 7 keeps the discipline 'BIB-nnn orphans surface loudly via CI' working from commit time forward."
  - "Four-tier clustering chosen over three-tier (Primary / Secondary / Witness). Tooling References became a separate tier once pytest-benchmark, strace, LV2 spec, SHA-256 RFC, and Docker entered the bibliography — conflating them with Primary or Secondary sources would have weakened the 'this is what SPU-94 reads for algorithmic facts' signal on the Primary tier."
  - "Execute-time digest for ADR-Phase-7-D uses Plan 07-02's re-verified sha256:5a2a80d11944804c01b8619bc967e31801ec39bf3257ab80b91070eb23625644, superseding the researcher-time digest. ADR-Phase-7-D's Decision section explicitly records both digests so the revision trail stays legible."
  - "No verifier-cycle changes needed. All three tasks passed their acceptance criteria on first run after committing. Pre-existing BIB-003/004 orphans were the only surprise; auto-fixed by adding them to BIBLIOGRAPHY before Task 1's GREEN commit."
metrics:
  duration_minutes: ~9
  tasks_completed: 3
  files_created: 3
  files_modified: 3
  commits: 4 (1 RED test commit + 3 feat/docs GREEN commits)
  ctest_targets_added: 2 (bibliography_crossref, test_check_bibliography_refs; label "bibliography")
  bib_entries_final: 20 (was 13; +7 net-new Phase 7 plus BIB-003/BIB-004 promotion)
  adr_phase_7_count: 8
  bib_references_in_decisions: 20 (perfect symmetry with defined entries; 0 unused)
completed: 2026-04-23
---

# Phase 07 Plan 06: BIBLIOGRAPHY Cross-Ref Gate + ADR-Phase-7-A..H Summary

One-liner: landed the DOCS-03 BIBLIOGRAPHY cross-reference checker +
two-tier meta/conformance tests + seven net-new BIB entries + four-tier
cluster polish + all eight Phase 7 ADRs (A..H) covering the 22 D-XX
decisions from 07-CONTEXT.md — closing DOCS-03 and Phase 7's content
surface.

## What Landed

### Task 1 — `check_bibliography_refs.py` + meta-tests + conformance test (TDD)

- **`scripts/check_bibliography_refs.py`** (~95 lines, executable):
  cross-reference validator. Regex `\bBIB-(\d{3})\b` (three-digit strict
  per T-07-06-A) finds every reference in DECISIONS.md; regex
  `^### BIB-(\d{3})(?:\s*:|\s*[—-])` finds every entry header in
  BIBLIOGRAPHY.md. Exits 1 with stderr listing dangling references;
  exits 0 with pass line + unused-entry informational tail.
- **`scripts/test_check_bibliography_refs.py`** (~43 lines, 2 tests):
  positive (committed docs pass) + negative (invented `BIB-999` in tmp
  files trips the checker, stderr names the offender).
- **`tests/conformance/test_bibliography_crossref.py`** (~46 lines, 2
  tests): wraps the checker under ctest + structural gate (every
  `### BIB-nnn:` entry carries a `**URL:**` field — matches the schema
  BIB-001..BIB-013 already use).
- **`tests/conformance/CMakeLists.txt`**: two new ctest entries under
  label `bibliography` (selectable via `ctest -L bibliography`).

TDD shape: RED commit first (tests fail because checker doesn't exist),
then GREEN commit lands the checker + CMakeLists wiring + the Rule-3
BIB-003/BIB-004 promotion that makes the committed docs pass.

### Task 2 — BIBLIOGRAPHY.md additive + cluster-polish (D-22)

- Seven net-new Phase 7 entries added on top of Task 1's BIB-003/BIB-004
  promotion: BIB-014 (lv2-psx-reverb witness binary, pinned SHA
  `424e1e8ee7f780106b005011b036386513c61db3`, GPLv3, source-never-read
  posture), BIB-015 (pinned psx-spx wayback snapshot for D-04),
  BIB-016 (pytest-benchmark 5.2.3), BIB-017 (strace with the
  `brk,mmap,mmap2,munmap,mremap` filter), BIB-018 (LV2 plugin
  specification — host-side only, M1 is not an LV2 plugin),
  BIB-019 (SHA-256 RFC 6234), BIB-020 (Docker image-digest pinning).
- Four-tier restructure: **Primary Sources** (BIB-001, BIB-005, BIB-006,
  BIB-011, BIB-013, BIB-015), **Secondary Sources** (BIB-002, BIB-007,
  BIB-012), **Witness Binaries** (BIB-008, BIB-009, BIB-010, BIB-014),
  **Tooling References** (BIB-003, BIB-004, BIB-016, BIB-017, BIB-018,
  BIB-019, BIB-020).
- Preamble rewritten to describe the four-tier scheme + the
  consumption-role framing. README tone match throughout — confident,
  factual, no apologetic early-stage language. Paraphrase discipline
  upheld per PROJECT.md.

File growth: 146 lines → 294 lines; 13 entries → 20 entries.

### Task 3 — ADR-Phase-7-A..H in DECISIONS.md

Eight ADRs prepended above ADR-Phase-6-H per the top-of-file convention.
Existing ADRs untouched (`git diff` removed-line count: 0).

#### D-XX → ADR mapping

| ADR | Title (short) | D-XX covered | Landed in |
|---|---|---|---|
| **ADR-Phase-7-A** | COVERAGE.md three-section + CI validator + pinned wayback | D-01, D-02, D-03, D-04 | Plan 07-01 |
| **ADR-Phase-7-B** | Witness-diff split-band measurement-only; lv2 pinned SHA | D-05, D-06, D-07, D-08 | Plan 07-03 |
| **ADR-Phase-7-C** | Goldens format + input set + regeneration discipline | D-09, D-10, D-11, D-12 | Plan 07-02 |
| **ADR-Phase-7-D** | Docker base-image digest pin, no per-package apt pins | D-13, D-14, D-15 | Plan 07-02 |
| **ADR-Phase-7-E** | Modulation harness + determinism gate + LEVERS-CATALOG writer | D-16, D-17, D-19 | Plan 07-04 |
| **ADR-Phase-7-F** | SC-4 reinterpretation — no internal-tick zipper, not smoothness | D-18 | Phase 7 close-out (this plan) |
| **ADR-Phase-7-G** | Benchmark gate split — alloc hard, timing report-only | D-20, D-21 | Plan 07-05 |
| **ADR-Phase-7-H** | BIBLIOGRAPHY additive + cluster-polish + CI cross-ref gate | D-22 | Plan 07-06 (this plan) |

All 22 D-XX decisions from `07-CONTEXT.md` accounted for.

Every ADR's `**Sources:**` section cites `BIB-nnn` entries that resolve
in BIBLIOGRAPHY.md after Task 2 landed. 18 `BIB-01[4-9]|BIB-020`
citations across the eight ADRs.

## New BIB IDs (Full List)

- **BIB-003** — Clang UBSan reference (promoted from Phase-1 placeholder)
- **BIB-004** — GCC `no_sanitize` attribute (promoted from Phase-1 placeholder)
- **BIB-014** — lv2-psx-reverb (Phase 7 witness binary, pinned SHA)
- **BIB-015** — Pinned psx-spx wayback snapshot (2026-01-14)
- **BIB-016** — pytest-benchmark 5.2.3
- **BIB-017** — strace
- **BIB-018** — LV2 plugin specification
- **BIB-019** — SHA-256 (RFC 6234)
- **BIB-020** — Docker (image digest pinning)

## Pre-existing Orphans Flagged / Fixed

Two pre-existing orphan citations were discovered during Task 1's first
checker run against committed docs:

- `BIB-003` cited in ADR-0003 Sources as "`BIB-003` future" since Phase 1.
- `BIB-004` cited in ADR-0003 Sources as "`BIB-004` future" since Phase 1.

Both resolved by adding full entries to BIBLIOGRAPHY.md before Task 1's
GREEN commit (Rule-3 deviation — the checker's exit-0 acceptance
criterion could not hold without them). Documented in ADR-Phase-7-H as
the "promoted from Phase-1-era placeholder status" note.

Zero other orphans surfaced.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking] BIB-003 and BIB-004 orphaned in DECISIONS.md since Phase 1**

- **Found during:** Task 1 first run of `python3 scripts/check_bibliography_refs.py` against committed docs.
- **Issue:** ADR-0003 Sources has cited these as "future" BIB entries since the Phase 1 commit; the cross-reference checker exits 1 on their absence, blocking Task 1's acceptance criterion (`python3 scripts/check_bibliography_refs.py` exits 0).
- **Fix:** Added BIB-003 (Clang UBSan reference) and BIB-004 (GCC `no_sanitize` attribute) to BIBLIOGRAPHY.md as a new "Tooling References (Phase 1 — UBSan policy anchors)" cluster before Task 1 GREEN commit; Task 2's cluster-polish pass later folded both into the consolidated Tooling References tier.
- **Files modified:** `docs/BIBLIOGRAPHY.md`.
- **Commit:** `53fa883` (Task 1 GREEN) — folded into the `feat(07-06): ship check_bibliography_refs.py + wire ctest (GREEN)` commit so the acceptance criterion verification in the commit message is honest on-disk.

### No Rule 4 escalations

No architectural changes. No auth gates. No verifier cycles. All
acceptance criteria met on first run after each commit.

## Phase 7 Close-Out Checklist

Mapping each ROADMAP Milestone 1 success criterion to its landing plan
and closing evidence:

| SC | Description | Closing Plan | Closing Evidence |
|---|---|---|---|
| **SC-1** | Every nocash-documented reverb behavior has a passing test with a coverage map | Plan 07-01 | `docs/COVERAGE.md` (3 sections, 77 `test:` rows, 35 registers); `scripts/ci/check_coverage.py` CI gate; `coverage-map-check` GitHub job |
| **SC-2** | Witness-diff harness ships divergence numbers against lv2-psx-reverb | Plan 07-03 | `scripts/ci/witness_diff.py` 50-pair split-band harness; `.artifacts/witness_report.json`; `witness-diff` CI job with artifact upload; tolerance policy deferred to post-Phase-7 ADR per ADR-Phase-7-B |
| **SC-3** | Golden-file byte-identity across Docker-pinned CI and host dev | Plan 07-02 | 50 committed goldens + 50 SHA-256 sidecars under `tests/golden/<preset>/<input>.{wav,sha256}`; `Dockerfile.repro` pinned to sha256:5a2a80d...23625644; `reproducibility` CI job; human smoke-test PASS on 2026-04-23 |
| **SC-4** | Register modulation free of internal-tick zipper (reinterpreted per ADR-Phase-7-F) | Plan 07-04 | `tests/python/test_modulation_harness.py` 105 parametrized cases × 2 gates (stability + determinism); `modulation_report.json`; `docs/LEVERS-CATALOG.md` populated (12 free + 6 sample-quantized + 17 catastrophic = 35); ADR-Phase-7-F semantic lock |
| **SC-5** | RT-safety gates catch allocation + timing regressions | Plan 07-05 | `tests/rt_safety/hotpath_alloc_gate.sh` hard-fail on `brk/mmap/mmap2/munmap/mremap`; paired negative meta-test proves gate fires; `tests/benchmarks/test_benchmark.py` report-only with human-endorsed `benchmark_baselines.json`; two CI jobs (hotpath-alloc-gate hard, benchmark-report report-only) |
| **SC-6** | DECISIONS.md + BIBLIOGRAPHY.md close the first-class-deliverable loop | Plan 07-06 (this plan) | 8 Phase 7 ADRs land; BIBLIOGRAPHY.md gains 7 new entries + four-tier clustering; `scripts/check_bibliography_refs.py` CI gate; ADR-Phase-7-H codifies the ongoing posture |

All six SC-1..SC-6 closed. Phase 7 complete.

## Known Stubs

None. All three scripts are live; all three tests pass; all eight ADRs
carry complete sections (Status / Context / Decision / Consequences /
Sources); all 20 BIB-nnn references resolve.

## Threat Flags

None. Plan 07-06 is pure documentation + validator work; introduces no
new network surface, auth path, file access pattern, or schema change
at a trust boundary. `scripts/check_bibliography_refs.py` reads two
committed markdown files; its only external-input edge is the
`--decisions` / `--bibliography` CLI flags, both argparse-typed and
constrained to local file paths.

T-07-06-A (citation scope creep) mitigated by the three-digit-strict
regex in the checker. T-07-06-B (transcribed nocash prose leak)
mitigated by reviewer attention per ADR-Phase-7-H (same posture as
existing ADRs; not code-enforceable).

## Deferred Issues

None for this plan. Two items explicitly deferred by the ADR batch
itself:

1. **Tolerance policy for witness-diff divergence** (ADR-Phase-7-B) —
   deferred to a post-Phase-7 ADR once the first `witness_report.json`
   informs per-preset thresholds. SILENCE / OFF degenerate rows will
   need special-case scoping.
2. **M2 ADPCM bibliography additions** — ADR-Phase-7-H's four-tier
   clustering is designed to extend naturally; M2 Primary Sources will
   slot in above BIB-015.

## Commits

| # | Hash | Type | Message |
|---|---|---|---|
| 1 | `db73f0e` | test | `test(07-06): add failing meta-tests for check_bibliography_refs.py (RED)` |
| 2 | `53fa883` | feat | `feat(07-06): ship check_bibliography_refs.py + wire ctest (GREEN)` |
| 3 | `dfabe0e` | docs | `docs(07-06): additive BIBLIOGRAPHY entries + cluster-polish pass (D-22)` |
| 4 | `d2e8d06` | docs | `docs(07-06): land ADR-Phase-7-A..H (eight Phase 7 ADRs)` |

## Self-Check

- [x] `scripts/check_bibliography_refs.py` exists and executable — FOUND.
- [x] `scripts/test_check_bibliography_refs.py` exists — FOUND.
- [x] `tests/conformance/test_bibliography_crossref.py` exists — FOUND.
- [x] `docs/BIBLIOGRAPHY.md` — 294 lines / 20 BIB entries / four-tier clustering — FOUND.
- [x] `docs/DECISIONS.md` — 8 ADR-Phase-7-[A-H] headers — FOUND.
- [x] Commit `db73f0e` (TDD RED) in git log — FOUND.
- [x] Commit `53fa883` (TDD GREEN + BIB-003/BIB-004 promotion) in git log — FOUND.
- [x] Commit `dfabe0e` (BIBLIOGRAPHY additive + cluster polish) in git log — FOUND.
- [x] Commit `d2e8d06` (ADR-Phase-7-A..H batch) in git log — FOUND.

## Self-Check: PASSED
