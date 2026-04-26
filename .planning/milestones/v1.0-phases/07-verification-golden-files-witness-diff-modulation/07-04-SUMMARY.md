---
phase: 07-verification-golden-files-witness-diff-modulation
plan: 04
subsystem: modulation-harness-levers-catalog
tags: [modulation, d-17, d-18, d-19, levers-catalog, tdd, idempotent-writer]
requires:
  - phase-6-python-binding (SPU94 + Register + Preset)
  - phase-7-plan-01-foundation (COVERAGE.md validator lives alongside; scipy installed)
provides:
  - modulation-harness-35x3-parametrized (stability + determinism at audio-rate)
  - modulation-report-json-per-register (classifier + s2s-rms + sha256)
  - levers-catalog-md-35-rows-populated
  - idempotent-auto-column-writer (HAND-preservation proven)
  - writer-meta-tests (idempotency + hand-preservation + hand-empty-default)
  - structural-conformance-test (35-rows-enum-order + no-empty-auto-cells)
  - t-07-04-a-through-e-mitigations
affects:
  - future-m4-macro-layer (consumes levers catalog as empirical register-to-behavior map)
  - future-ci-may-add-modulation-label-to-hotpath-gate
tech-stack:
  added:
    - (none; scipy + numpy + pytest already installed by Plan 07-01)
  patterns:
    - tdd-red-then-green-per-task (two commits per task)
    - session-scoped-pytest-fixture-for-result-accumulation
    - named-group-regex-with-explicit-framing-preservation
    - closed-enum-auto-values-mitigates-pipe-injection
    - _state-attribute-as-documented-test-surface-contract (T-07-04-E)
key-files:
  created:
    - tests/python/modulation_harness.py
    - tests/python/test_modulation_harness.py
    - tests/python/modulation_report.json
    - scripts/write_levers_catalog.py
    - tests/python/test_levers_catalog_writer.py
    - tests/conformance/test_levers_catalog_complete.py
    - docs/LEVERS-CATALOG.md
  modified:
    - tests/python/CMakeLists.txt
    - tests/conformance/CMakeLists.txt
decisions:
  - "Committed tests/python/modulation_report.json in-tree (27 KB / 806 lines) rather than gitignoring it — matches the Phase 7 Plan 05 benchmark-baselines.json pattern and the Plan 02 golden-sidecar pattern. Deterministic regeneration is proven by the determinism gate; checking the file in makes scripts/write_levers_catalog.py runnable without first invoking the harness, which is what both the writer meta-test and the conformance structural test depend on at ctest-collection time."
  - "Zipper-onset sine threshold is 1% of AMP (s2s_rms_max > 160 on AMP=16000). At 500 Hz sine modulation only vAPF1 crossed this threshold (s2s_rms=166.5); the remaining 34 registers sit in the 60–140 range. The threshold is deliberately tight so the catalog captures onsets rather than missing them; 'clean through 11 kHz' is the conservative label the writer emits when sweep mode's continuous 0.1 Hz → 11 kHz walk also didn't fire."
  - "Modulation harness uses SPU94._state (Phase 6's test-surface contract) directly — no hasattr fallback. Documented as T-07-04-E mitigation; any Phase 6 rename fails LOUDLY with AttributeError rather than silently passing the SPU94 instance as a ctypes argument."
  - "Writer regex captures leading whitespace + backtick-anchored register column + four HAND/AUTO cells + trailing whitespace as separate groups, so rebuilding a rewritten row preserves the original visual framing byte-for-byte (needed for idempotency across re-runs)."
  - "AUTO-value strings are a closed set ('free' / 'sample-quantized' / 'catastrophic' / '~NNN Hz' / 'clean through 11 kHz'). No user-controlled bytes reach the markdown table; T-07-04-D pipe-injection threat mitigated by construction."
  - "CMake integration uses one ENVIRONMENT generator expression + LABELS 'modulation' (or 'levers') per test, mirroring the Phase 5 fuzz_process / Phase 7 Plan 05 hotpath-alloc-gate pattern. Selectable via ctest -L modulation and ctest -L levers."
  - "07-03's witness_determinism entry in tests/conformance/CMakeLists.txt was preserved untouched during the append — the two Wave 3 plans share the same file but wrote non-overlapping blocks. Post-commit check: ctest -L 'witness|modulation|levers' is 4/4 green in 16.3 s."
metrics:
  duration_minutes: ~12
  tasks_completed: 2
  files_created: 7
  files_modified: 2
  commits: 4 (2 RED + 2 GREEN, TDD shape)
  ctest_targets_added: 3 (modulation_harness #51, levers_catalog_complete #75, levers_catalog_writer #76)
  parametrized_test_cases: 211 (105 stability + 105 determinism + 1 report writer)
  pass_rate: 211/211 (100%)
  harness_runtime: 4.0 s (vs 180 s VALIDATION budget — 45x headroom)
  stability_failures: 0
  determinism_failures: 0
  registers_reporting_zipper_at_500hz: 1 (vAPF1 only)
  classifier_buckets: "12 free + 6 sample-quantized + 17 catastrophic = 35"
completed: 2026-04-23
---

# Phase 07 Plan 04: Modulation Harness + LEVERS-CATALOG Summary

Shipped the 35-register × 3-mode modulation harness with stability +
determinism gates (105 parametrized cases × 2 gates + 1 report writer
= 211 pytest cases, all green in 4 s) and the idempotent
`docs/LEVERS-CATALOG.md` AUTO-column writer that preserves hand-written
HAND columns across regenerations. Closes TEST-05 (modulation harness)
and DOCS-02 (levers catalog foundation).

## What Landed

### Task 1 — modulation harness (TDD: RED 7f4ac5a → GREEN 1996a33)

- `tests/python/modulation_harness.py` (235 lines): module-level helpers
  — `modulation_stream(mode, rate_hz, n, seed)`, `reg_valid_range(reg)`,
  `set_reg_typed(state_handle, reg, value)`, `run_one_case(reg, mode,
  rate_hz, seed)`, `classify_modulation_cost(reg)`, plus the
  `ModulationResult` dataclass. D-17 rate axis walked continuously inside
  sweep mode via `scipy.signal.chirp(method='logarithmic')`; sine mode
  at 500 Hz (`RATES_HZ[4]`) for steady-state spot-check; random_walk
  for non-periodic D-08 write-policy stressing.
- `tests/python/test_modulation_harness.py` (109 lines): 105
  parametrized `test_stability` + 105 parametrized `test_determinism` +
  1 `test_report_written` = 211 pytest cases. Session-scoped `results`
  fixture accumulates stability outcomes into
  `tests/python/modulation_report.json` after the final case.
- `tests/python/modulation_report.json` (806 lines, 27 KB): committed
  in-tree per D-21-style pattern. 35 registers × `{modulation_cost,
  modes[sine|sweep|random_walk]{stability_ok, sample_to_sample_rms_max,
  zipper_onset_hz, out_sha256}}`.
- `tests/python/CMakeLists.txt`: new `modulation_harness` ctest entry
  (TIMEOUT 180, LABELS "modulation", SPU94_LIB generator expression
  mirroring the fuzz_*.py convention).

### Task 2 — LEVERS-CATALOG writer + conformance test (TDD: RED cc4318b → GREEN cc3b8ab)

- `docs/LEVERS-CATALOG.md` (57 lines): 35 data rows in spu94_reg_t enum
  order with four columns per row — `Musical role (HAND)`,
  `Modulation cost (AUTO)`, `Zipper onset (AUTO)`, `M4 lever (HAND)`.
- `scripts/write_levers_catalog.py` (125 lines, executable): named-group
  regex parses each data row, rebuilds it with AUTO cells from
  `modulation_report.json`, writes back only if content changed.
  Second run is a byte-identical no-op.
- `tests/python/test_levers_catalog_writer.py` (3 meta-tests):
  idempotency + HAND-preservation + HAND-empty-by-default.
- `tests/conformance/test_levers_catalog_complete.py` (2 structural
  tests): 35-rows-in-enum-order + no-empty-AUTO-cells-after-writer.
- `tests/conformance/CMakeLists.txt`: APPEND two entries
  (`levers_catalog_complete` #75, `levers_catalog_writer` #76,
  LABELS "levers"). 07-03's `witness_determinism` entry preserved
  untouched — the two Wave 3 plans shared this file and wrote
  non-overlapping blocks.

## Classifier Buckets (Pitfall 7 proposal → confirmed empirically)

| Bucket              | Count | Members (in enum order)                                                                 |
|---------------------|------:|-----------------------------------------------------------------------------------------|
| `free`              |    12 | `vLOUT`, `vROUT`, `vIIR`, `vCOMB1..4`, `vWALL`, `vAPF1`, `vAPF2`, `vLIN`, `vRIN`        |
| `sample-quantized`  |     6 | `dAPF1`, `dAPF2`, `dLSAME`, `dRSAME`, `dLDIFF`, `dRDIFF`                                 |
| `catastrophic`      |    17 | `mBASE`, `mLSAME`, `mRSAME`, `mLCOMB1..4`, `mRCOMB1..4`, `mLDIFF`, `mRDIFF`, `mLAPF1/2`, `mRAPF1/2` |

**Surprises:** none. Every register ended up where Pitfall 7 predicted.
The 12/6/17 split matches ADR-0005 (13 IMMEDIATE = 12 v-prefix + mBASE /
22 TICK_LATCHED = 6 d-prefix + 16 non-mBASE m-prefix) once you
reclassify mBASE as catastrophic-for-modulation-purposes per ADR-0006's
snap-on-write behavior.

## Stability + Determinism: Clean Sweep

**All 105 × 2 gates green.** No register failed stability (no NaN, no
range escape past int16, no crash). No register failed determinism
(same seed → byte-identical audio SHA-256 across two runs). This is
the Phase 2 D-08 split write-timing policy verification at
audio-rate modulation: it holds for every register in every mode.

Key numeric signals (from `modulation_report.json`):

- **Sine mode s2s_rms range** (35 regs @ 500 Hz): min 66.7 (`mRDIFF`)
  → max 166.5 (`vAPF1`). Only `vAPF1` crosses the 1% zipper-onset
  threshold; the rest read "clean through 11 kHz" in the catalog.
- **Sweep mode s2s_rms range**: min 66.7 (`mRDIFF`) → max 119.5 (`vAPF1`).
  Continuous 0.1 Hz → 11 kHz walk — the D-17 rate axis covered
  per-register inside a single mode.
- **Random-walk mode**: `mBASE` bottoms out at s2s_rms=3.0 (its HALL
  preset default is stable; the modulation barely perturbs the IMMEDIATE
  snap-on-write output). `vAPF1` tops at s2s_rms=119.2.

**`vAPF1` is the catalog's one ~500 Hz zipper-onset entry.** Every
other register reads "clean through 11 kHz". The threshold is tight
(1% of full-scale AMP); relaxing it in a future refresh would catch
more borderline cases — but that's M4's call, not M1's.

## Notes for the M4 Macro Designer

- The 12 `free` registers are the natural CV-control candidates.
  Anthony's HAND columns can safely map these to any musical lever
  without worrying about zipper (within the 11 kHz rate ceiling sweep
  mode walked).
- The 6 `sample-quantized` `d*` registers are the "tempo-sync"
  candidates — their 22.05 kHz tick granularity maps naturally to
  delay-time / pre-delay macros where audio-rate modulation isn't
  the goal.
- The 17 `catastrophic` `m*` registers (including `mBASE`) are the
  "structural" registers. M4's smoothing layer is what transmutes
  their click-on-write hardware character into a playable macro.
  Good candidates for `Room Size` style parameters that move slowly
  by design.
- The catalog's HAND columns are now empty and waiting. Anthony can
  start filling them in during M4 listening tests; the next writer
  run will leave his text intact.

## Authentication Gates

None. Phase 7 Plan 01's apt-get install, Plan 02's docker group, Plan
03's lv2 build — all upstream of this plan. No privileged access
needed; harness + writer run entirely under the dev user.

## Deviations from Plan

### No Auto-fixes Required

Both tasks shipped exactly as planned on first GREEN. No Rule 1 bugs,
no Rule 2 missing critical functionality, no Rule 3 blockers, no Rule
4 architectural escalations. The plan's example code compiled cleanly
against the live Phase 6 binding — the `SPU94._state` contract held
(T-07-04-E), the Register IntEnum iterated in the expected order, the
Preset.HALL enum worked as a drop-in argument to `load_preset`.

### Coordination with Sibling Plan 07-03

Both 07-03 and 07-04 touch `tests/conformance/CMakeLists.txt`. 07-03
landed first (commit `1efb701`) with its `witness_determinism` block.
07-04's append placed `levers_catalog_complete` + `levers_catalog_writer`
after 07-03's entry — zero line overlap, no merge concerns. Post-commit
verification: `ctest -L "witness|modulation|levers"` runs 4/4 green in
16.3 s, and the conformance file diff shows only a clean append.

## Commits

| # | Hash      | Type | Message                                                                              |
|---|-----------|------|--------------------------------------------------------------------------------------|
| 1 | `7f4ac5a` | test | test(07-04): add failing modulation harness stability+determinism tests (RED)        |
| 2 | `1996a33` | feat | feat(07-04): modulation harness + 105 stability/determinism cases (GREEN)            |
| 3 | `cc4318b` | test | test(07-04): add LEVERS-CATALOG scaffold + failing writer meta-tests (RED)           |
| 4 | `cc3b8ab` | feat | feat(07-04): idempotent LEVERS-CATALOG writer + conformance CMake wire (GREEN)       |

## Known Stubs

None. The catalog's HAND columns are empty by design (D-16 — the
writer will never fill them) and will be populated by Anthony during
M4 listening tests. This is not a stub in the stubs-tracker sense —
it's a documented contract boundary between M1 (empirical
machine-filled columns) and M4 (subjective hand-filled columns).

## Threat Flags

None. The plan's T-07-04-A..E register is mitigated as specified:

- **T-07-04-A** (u16/i16 setter range escape) — `np.clip(vals, lo, hi)`
  in `modulation_to_register_values` + signedness dispatch in
  `set_reg_typed` (no value outside `[-32768, 32767]` or `[0, 65535]`
  ever reaches the C API).
- **T-07-04-B** (`out_sha256` info disclosure) — accepted; purpose is
  determinism-gate traceability; hashes are not sensitive.
- **T-07-04-C** (regex corrupts HAND) — named-group regex +
  backtick-anchored register column + `test_hand_columns_preserved`
  meta-test proves survival.
- **T-07-04-D** (pipe injection in AUTO cells) — closed-set values
  (`free`/`sample-quantized`/`catastrophic`/`~NNN Hz`/`clean through 11 kHz`);
  no user-controlled bytes in the writer's output.
- **T-07-04-E** (`SPU94._state` test-surface stability) — attribute
  accessed directly with no `hasattr` fallback; AttributeError fires
  loudly on rename.

## Deferred Issues

None. All plan acceptance criteria satisfied:

- [x] 35 × 3 parametrized cases pass stability + determinism gates
  (211/211 green).
- [x] `tests/python/modulation_report.json` exists, valid JSON, 35
  entries with `modulation_cost` + `modes` keys.
- [x] `grep -q "list(Register)" tests/python/test_modulation_harness.py`
  passes.
- [x] `grep -q "out_sha256" tests/python/modulation_harness.py` passes.
- [x] `grep -qE "IMMEDIATE_REG_NAMES|classify_modulation_cost"
  tests/python/modulation_harness.py` passes.
- [x] Harness runtime 4.0 s ≪ 180 s VALIDATION budget.
- [x] `docs/LEVERS-CATALOG.md` has exactly 35 data rows in
  spu94_reg_t enum order.
- [x] `pytest tests/python/test_levers_catalog_writer.py -q` passes 3
  tests (idempotency + HAND-preservation + HAND-empty-by-default).
- [x] `pytest tests/conformance/test_levers_catalog_complete.py -q`
  passes 2 tests (row order + no-empty-AUTO).
- [x] `grep -c "| free |" docs/LEVERS-CATALOG.md` == 12; `| catastrophic |`
  == 17; `| sample-quantized |` == 6.
- [x] Two back-to-back `python3 scripts/write_levers_catalog.py` runs
  produce zero `git diff`.
- [x] `ctest -L "modulation|levers"` passes 3/3 in ~5.1 s.

## Self-Check

- [x] `tests/python/modulation_harness.py` exists — FOUND.
- [x] `tests/python/test_modulation_harness.py` exists — FOUND.
- [x] `tests/python/modulation_report.json` exists — FOUND.
- [x] `scripts/write_levers_catalog.py` exists, executable — FOUND.
- [x] `tests/python/test_levers_catalog_writer.py` exists — FOUND.
- [x] `tests/conformance/test_levers_catalog_complete.py` exists — FOUND.
- [x] `docs/LEVERS-CATALOG.md` exists, 35 rows populated — FOUND.
- [x] Commit `7f4ac5a` in git log — FOUND.
- [x] Commit `1996a33` in git log — FOUND.
- [x] Commit `cc4318b` in git log — FOUND.
- [x] Commit `cc3b8ab` in git log — FOUND.

## Self-Check: PASSED
