---
phase: 03-core-reverb-algorithm-hard-clip
plan: 04
subsystem: reverb-tests-adrs
tags: [reverb, test-07, composition-equivalence, fuzz, adr-landing, sc-4, sc-5, pitfall-8, pitfall-9]
requires:
  - spu94_reverb network feature-complete (Plans 01-03)
  - tests/python/derive_reverb_reference.py (Plans 01-03)
  - tests/python/fuzz_buffer.py pattern (Phase 2 Plan 05)
  - docs/DECISIONS.md with ADR-0006 at line 33 (Phase 2 convention)
provides:
  - tests/unit/reverb/test_reverb_edges.c (TEST-07 Q15 edge battery, 12 sub-tests)
  - tests/unit/reverb/test_reverb_body.c (SC-1 composition equivalence, 4 tests over 3 seeds)
  - tests/python/fuzz_reverb.py (10^6-step ctypes fuzz; ~2.6s runtime)
  - docs/DECISIONS.md::ADR-0007..ADR-0011 (five new ADRs above ADR-0006)
affects:
  - tests/unit/reverb/CMakeLists.txt (+2 executables: reverb_edges, reverb_body)
  - tests/python/CMakeLists.txt (+1 ctest target: fuzz_reverb)
tech-stack:
  added: []
  patterns:
    - edge-battery-per-stage (TEST-07: INT16_MIN^2, ADR-0001 truncation
      direction, Pitfall-1/7/8 guards re-asserted at stage-call level)
    - composition-equivalence-under-seeded-random (SC-1: 2 states A/B
      seeded identically, compare reverb_body(A) vs stage-by-stage(B)
      byte-for-byte across work_buf + err_* + overflow_magnitude)
    - python-ctypes-fuzz-with-generator-expression-env (Pitfall-7
      mitigation via $<TARGET_FILE:spu94_shared>; mirrors fuzz_buffer.py)
    - newest-on-top-adr-prepend (Phase 2 convention; ADR-0011 → ADR-0010
      → ADR-0009 → ADR-0008 → ADR-0007 above the preserved ADR-0006)
    - adr-paraphrase-discipline (DOCS-03: SPU-94 own wording in decision
      bodies; witnesses license-tagged in witness/Sources sections only)
key-files:
  created:
    - tests/unit/reverb/test_reverb_edges.c
    - tests/unit/reverb/test_reverb_body.c
    - tests/python/fuzz_reverb.py
    - .planning/phases/03-core-reverb-algorithm-hard-clip/03-04-SUMMARY.md
  modified:
    - tests/unit/reverb/CMakeLists.txt
    - tests/python/CMakeLists.txt
    - docs/DECISIONS.md
decisions:
  - ADR-0011 (D-11 scope + overflow-magnitude extension) landed at
    docs/DECISIONS.md line 33. Per-multiply err tap across every stage;
    hard-clip exposes overflow_magnitude out-param as the sibling
    high-bits-lost observable. Controllers M4 forward dependency seeded.
  - ADR-0010 (D-10) landed at line 138. Explicit branch at IIR memory-
    write point: `if (vIIR_snap == INT16_MIN) result = sat_s16(-(int32_t)result)`.
    Rejects emergent-from-saturation-arithmetic as speculative.
  - ADR-0009 (D-09, CORE-02) landed at line 241. Hard-clip as its own
    named function; satisfies "independently testable" SC-2 with no
    test-fixture indirection. Seam: body-caller function slot.
  - ADR-0008 (D-08, SC-5b) landed at line 335. Freeze-once-per-pair
    default; M4 Controllers seed for Extended Modulation Mode toggle
    exposing re-read-fresh-for-R (doubles modulation rate to 44.1 kHz).
  - ADR-0007 (D-07, SC-5a, user-locked 2026-04-19) landed at line 438.
    Cascading sat_s16 after each add; diverges from DuckStation/
    Mednafen-PSX witness consensus per taste-driven user decision.
    Revert lever documented; M5 hardware capture remains the ultimate
    authority.
  - test_reverb_body's seeded-random strategy skips mBASE specifically
    because setting it to a large random u16 snaps buffer_address
    outside the bounds-checked work_buf window, which reduces the data
    path's coverage (most reads/writes become no-ops). Deterministic
    choice, not a gap — reverb network is exercised far more with
    buffer_address kept in-range.
  - fuzz_reverb.py asserts structural invariants (no crash, no OOB,
    halfword-alignment, mBASE floor) rather than per-sample bit-
    exactness. Per-sample bit-exactness is test_reverb_body's job;
    fuzz_reverb's complementary mission is broad state-space coverage.
metrics:
  duration: approx 35 min (agent-hosted; excludes read-first phase)
  tasks: 3
  files_created: 4
  files_modified: 3
  lines_added: approx 1500 (tests: ~960; ADRs: ~540)
  ctests_before: 23
  ctests_after: 26
  new_unit_tests_body: 4
  new_unit_tests_edges: 12
  fuzz_reverb_steps: 1000000
  fuzz_reverb_runtime_seconds: 2.6
  fuzz_reverb_ops_per_second: approx 381000
  adr_lines_added: 5
  adr_total_after_plan: 11
  completed: 2026-04-19
---

# Phase 3 Plan 04: Edge Battery + Composition Equivalence + ADR Landings Summary

Closed Phase 3. Three regression-protection assets plus five ADRs for
the Phase-3 design-decision corpus. The reverb network's Plans 01-03
implementation is now locked in with a TEST-07 Q15 edge battery, a
full-body SC-1 composition-equivalence test suite, a 10^6-step ctypes
fuzz harness, and five ADRs recording D-07..D-11 with paraphrase
discipline. ROADMAP Phase 3 SC-4 and SC-5 are both GREEN.

## What Shipped

### TEST-07 fixed-point edge battery (Task 1)

`tests/unit/reverb/test_reverb_edges.c` — 12 Unity sub-tests targeting
Pitfall/ADR-specific edges across every stage.

| Test | Maps to |
| --- | --- |
| test_output_scale_truncation_direction | ADR-0001 ASR re-assertion at stage |
| test_INT16_MIN_squared_saturates_in_output_scale | ADR-0001 INT16_MIN² sat |
| test_INT16_MIN_squared_saturates_in_same_iir_vWALL | INT16_MIN² at wall multiply |
| test_INT16_MIN_squared_saturates_in_comb | INT16_MIN² across all 4 comb taps |
| test_INT16_MIN_squared_saturates_in_apf1 | Pitfall-1 + Pitfall-7 compound |
| test_INT16_MIN_squared_saturates_in_apf2 | structural mirror of apf1 |
| test_comb_cascading_sat_mirror_case | D-07 mirror (v=(-0x7FFF,-0x7FFF,0x7FFF,0x7FFF)) |
| test_vIIR_anomaly_INT16_MIN_negation_guard | D-10 + Pitfall-1 at anomaly branch |
| test_INT16_MIN_tap_subtraction_pitfall1 | Pitfall-1 guard on IIR tap sub |
| test_err_zero_for_non_saturating_all_stages | D-11 err invariant (zero-in) |
| test_err_accumulates_for_saturating_input | D-11 err monotonicity |
| test_buffer_address_unchanged_across_reverb_body | Pitfall 8 |

The D-07 mirror case deserves its own note. Plan 03 Task 3 pinned the
cascading behavior with one distinguishing case
(`v=(0x7FFF,0x7FFF,-0x7FFF,-0x7FFF)` → Lout=-0x7FFF); the Plan 04
mirror case reverses the sign pattern
(`v=(-0x7FFF,-0x7FFF,0x7FFF,0x7FFF)` → Lout=0x7FFC). The int32-
accumulate variant would give -2 in both cases; any regression to that
variant fails both tests simultaneously. The two cases bracket the
cascading behavior on both the floor clamp (`sat_s16` at
INT16_MIN bound, mirror case) and the ceiling clamp (INT16_MAX bound,
original case).

### SC-1 composition equivalence (Task 2)

`tests/unit/reverb/test_reverb_body.c` — 4 Unity sub-tests.

Three of them drive the core `run_body_equivalence(seed)` helper with
seeds `0x12345678`, `0xCAFEBABE`, and `0x0BADF00D`. Each:

1. Seeds `gA_work` and `gB_work` with an identical LCG noise stream.
2. Initializes state A on `(gA_state, gA_work)` and state B on
   `(gB_state, gB_work)`.
3. Populates every register (except mBASE — see Decisions) with the
   same deterministic value on both states, then flushes pending
   writes.
4. Path A: `spu94_reverb_body(A)`.
5. Path B: re-reads every snapshot from B in the same order
   `reverb_body` does, then calls each stage function with those
   snapshots in the same order.
6. Asserts `gA_work == gB_work` bytewise, each `err_*` field matches,
   `overflow_magnitude` matches, `buffer_address` matches, AND that
   `buffer_address` did not change across `reverb_body` (Pitfall 8).

The fourth test (`test_reverb_body_does_not_advance_buffer`) is a
standalone Pitfall-8 re-assertion on a fresh zero-register state.

### 10^6-step Python ctypes fuzz (Task 2)

`tests/python/fuzz_reverb.py` — 1M random ops, ~2.6s runtime, ~381k
ops/s. Op mix: 25% i16-reg-write, 25% u16-reg-write, 50% tick. After
every op asserts `buffer_address <= 0x7FFFE`, `>= mBASE`, and halfword
alignment (with the ADR-0006 odd-mBASE-snap exception).

Follows `fuzz_buffer.py`'s structure exactly:
- Aligned ctypes buffers via `aligned_buffer` helper.
- `SPU94_LIB` env var + `$<TARGET_FILE:spu94_shared>` CMake generator
  expression (Pitfall-7 mitigation).
- Ring buffer of last 10 ops for failure diagnostics.
- `SPU94_FUZZ_SEED` + `SPU94_FUZZ_STEPS` overrides for reproducibility.

Pitfall 9 discipline: no GPL emulator in the harness's dependency
chain — pure `ctypes` + SPU-94 shared library.

### ADR-0007..0011 landings (Task 3)

Five ADRs prepended above ADR-0006 in docs/DECISIONS.md. Each follows
the Michael Nygard template extended with Sources / Seam / Revision
Path sections (Phase 2 convention from ADR-0004/0005/0006).

Line numbers (post-plan):

| ADR | Line | Title | Resolves |
| --- | --- | --- | --- |
| ADR-0011 | 33 | Per-multiply err-tap + overflow-magnitude observable | D-11 |
| ADR-0010 | 138 | vIIR = INT16_MIN anomaly mechanism | D-10, CORE-08 |
| ADR-0009 | 241 | Hard-clip stage placement | D-09, CORE-02 |
| ADR-0008 | 335 | L/R register-write timing within a 22.05 kHz tick | D-08, SC-5b |
| ADR-0007 | 438 | comb-sum accumulation precision — cascading sat_s16 after each add | D-07, SC-5a |
| ADR-0006 | 583 | mBASE write side effect — snap-on-write (preserved) | D-10 (Phase 2) |

ADR-0007's user-lock date (2026-04-19) is recorded explicitly in its
Status block; the revert lever (flip to int32-accumulate if M4 plugin-
era testing rejects the cascading distortion character) is documented
in its own subsection. ADR-0008 records the M4 Controllers seed for
Extended Modulation Mode. ADR-0011 records the overflow-magnitude
extension that pairs with the err accumulator stream for future
Controllers consumers.

**Paraphrase discipline:** every ADR's Context + Decision + Consequences
sections are SPU-94's own wording. The acceptance grep
`! grep -iq "The values written to memory are saturated to" docs/DECISIONS.md`
passes — no verbatim nocash prose in decision bodies. GPL-witness
mentions (DuckStation, Mednafen-PSX) are confined to witness-tagged
lines and Sources sections, satisfying the license-posture discipline
from PROJECT.md.

## Verification

All acceptance criteria green:

- `grep -cE "^## ADR-000[789]" docs/DECISIONS.md` = **3**
- `grep -cE "^## ADR-001[01]" docs/DECISIONS.md` = **2**
- `grep -q "^## ADR-0006" docs/DECISIONS.md` = **PASS** (not displaced)
- `grep -c "RUN_TEST(" tests/unit/reverb/test_reverb_edges.c` = **12**
- `grep -c "RUN_TEST(test_body_equivalence_seed_" tests/unit/reverb/test_reverb_body.c` = **3**
- `grep -q "test_buffer_address_unchanged_across_reverb_body" tests/unit/reverb/test_reverb_edges.c` = **PASS** (Pitfall 8)
- `grep -q "TEST_ASSERT_EQUAL_MEMORY(gA_work, gB_work, sizeof(gA_work))" tests/unit/reverb/test_reverb_body.c` = **PASS**
- `grep -q "N_STEPS" tests/python/fuzz_reverb.py` + default `"1000000"` = **PASS**
- `grep -q "fuzz_reverb seed=" tests/python/fuzz_reverb.py` = **PASS**
- `grep -q "comb-sum" docs/DECISIONS.md` = **PASS** (ROADMAP SC-5a)
- `grep -qE "L/R.*timing|register-write timing|L.*R.*tick.*timing" docs/DECISIONS.md` = **PASS** (ROADMAP SC-5b)
- `! grep -iq "The values written to memory are saturated to" docs/DECISIONS.md` = **PASS** (DOCS-03 paraphrase)
- GPL-witness guard (pipeline `grep -vE witness | grep -v Sources | grep .`) = **PASS**
- `ctest --test-dir build --output-on-failure` = **26/26 GREEN**
- `bash scripts/ci/grep-guard.sh` = **PASS**
- `bash scripts/ci/verify-no-heap-symbols.sh` = **PASS**
- ROADMAP SC-5a (comb-sum precision ADR) = **GREEN** (ADR-0007)
- ROADMAP SC-5b (L/R write timing ADR) = **GREEN** (ADR-0008)

fuzz_reverb local timing: 10^6 ops in 2.62 s, 381k ops/s (10-core x86_64).

### Commits

| Task | Commit | Scope |
| --- | --- | --- |
| 1 | `b30eaa2` | test_reverb_edges.c + CMakeLists.txt (12 Unity sub-tests; TEST-07) |
| 2 | `b6920a2` | test_reverb_body.c + fuzz_reverb.py + both CMakeLists (SC-1 + SC-4 fuzz) |
| 3 | `d1c321e` | docs/DECISIONS.md ADR-0007..0011 (SC-5) |

## Deviations from Plan

**None.** All three tasks executed exactly as planned. No Rule-1 bugs,
no Rule-2 missing critical functionality, no Rule-3 blocking issues, no
Rule-4 architectural escalations. No auth gates. No deferred items.

Two small clarifications where plan guidance left discretion:

1. **Seeded-state range:** plan sketch suggested randomizing every
   register. Executed as "every register except mBASE" — see the
   file-level comment in `test_reverb_body.c`. Randomizing mBASE would
   snap `buffer_address` to a large offset outside the 8 KB work_buf,
   causing most `reverb_buf_read/write` calls to fall outside the
   bounds check. Skipping mBASE keeps the reverb network actually
   running on the work_buf. This is a straightforward reading of the
   plan's intent, not a deviation.

2. **ADR content grep adjustments during Task 3:** after the initial
   landing, the strict GPL-witness guard
   (`! grep -E "Mednafen|DuckStation.*GPLv..." | grep -v witness | grep -v Sources | grep .`)
   required each line mentioning a witness project to contain the word
   "witness" (or "Witness") OR "Sources" on the same line. Two
   mentions in ADR-0007's Consequences section and one in ADR-0008's
   Context were rewritten to include `(witness: ...)` tagging on the
   matching line. These rewrites preserved the decision rationale
   verbatim; they were pure line-shape changes, not content changes.
   Recording here for audit because the strict grep enforced a
   line-level discipline beyond what the paraphrase policy specifies.

## Pitfall Encounters

- **Pitfall 1 (INT16_MIN negation UB):** re-asserted at the TEST-07
  level in `test_INT16_MIN_tap_subtraction_pitfall1` and
  `test_vIIR_anomaly_INT16_MIN_negation_guard`. The reverb source's
  four `sat_s16(-(int32_t)...)` guard sites from Plans 02-03 now have
  redundant stage-level verification.
- **Pitfall 7 (APF feedback edge at INT16_MIN):** re-asserted in
  `test_INT16_MIN_squared_saturates_in_apf1` and `_apf2`. Complements
  Plan 03 Task 3's primary APF tests.
- **Pitfall 8 (buffer_address unchanged across reverb_body):** pinned
  twice — once in `test_reverb_edges.c`
  (test_buffer_address_unchanged_across_reverb_body), once in
  `test_reverb_body.c` (test_reverb_body_does_not_advance_buffer AND
  as an assertion inside `run_body_equivalence`). The double pin is
  intentional because Pitfall 8 is the most likely future regression
  site if someone ever moves the buffer_advance call back into the
  reverb body by mistake.
- **Pitfall 9 (GPL provenance):** `fuzz_reverb.py` imports only the
  Python stdlib + SPU-94 shared lib. `test_reverb_edges.c` +
  `test_reverb_body.c` have zero GPL emulator names. The ADR witness
  mentions (DuckStation, Mednafen-PSX) are tagged as witness-only
  behavioral references, not read as source. `grep -rE
  "Mednafen|DuckStation|lv2-psx-reverb|UPSE|PCSX|MiSTer"` on all
  tests/ files is clean.

## Phase 3 Closing Checklist

All five ROADMAP Phase 3 success criteria are GREEN:

- [x] **SC-1** bit-for-bit per-stage against hand-derived reference.
  Plans 01-03 provide per-stage tests; Plan 04 Task 2 adds the full-
  body composition equivalence test pinning the glue.
- [x] **SC-2** independently testable hard-clip. Plan 01 ships
  `spu94_reverb_hard_clip` as its own function with dedicated test TU;
  ADR-0009 records the rationale.
- [x] **SC-3** vIIR = INT16_MIN anomaly + control. Plan 02 ships the
  anomaly branch; Plan 04 Task 1 re-asserts it at TEST-07 level;
  ADR-0010 records the mechanism.
- [x] **SC-4** fixed-point saturation/truncation/overflow edges.
  Plan 04 Task 1 ships the edge battery; fuzz_reverb.py adds broad
  state-space coverage. UBSan clean per existing CI wiring (phase-1
  foundation).
- [x] **SC-5** DECISIONS.md entries for comb-sum precision + L/R write
  timing. Plan 04 Task 3 lands ADR-0007 (SC-5a) + ADR-0008 (SC-5b)
  plus three additional ADRs for D-09/D-10/D-11.

Requirements fulfilled:

- [x] **CORE-02** hard-clip as its own testable stage (ADR-0009 +
  test_reverb_hard_clip.c from Plan 01).
- [x] **CORE-05** full 7-stage reverb network (input_scale →
  hard_clip → same_iir → diff_iir → comb → apf1 → apf2 →
  output_scale) (Plans 01-03).
- [x] **CORE-08** vIIR anomaly reproduced faithfully (Plan 02 +
  ADR-0010).
- [x] **TEST-06** vIIR anomaly test with control case (Plan 02 Task 3
  test_reverb_same_iir.c + test_reverb_diff_iir.c).
- [x] **TEST-07** Q15 fixed-point edge battery (Plan 04 Task 1).

Ready for Phase 4 (39-tap half-band FIR at the 44.1 ↔ 22.05 kHz I/O
boundary). No Phase 3 work deferred to Phase 4. Reverb network is
feature-complete and regression-protected.

## Known Stubs

None. Every function in `src/spu94/spu94_reverb.c` has a real body;
`spu94_reverb_body` invokes all 7 nocash stages plus hard_clip. All
Phase-3-owned requirements have landed tests and ADR rationale.

## Deferred Items

None. Plan 04 completed cleanly; no out-of-scope issues were
discovered that needed deferring.

## Self-Check: PASSED

Files claimed as created:
- `tests/unit/reverb/test_reverb_edges.c` — FOUND
- `tests/unit/reverb/test_reverb_body.c` — FOUND
- `tests/python/fuzz_reverb.py` — FOUND
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-04-SUMMARY.md` — FOUND (this file)

Files claimed as modified:
- `tests/unit/reverb/CMakeLists.txt` — FOUND (modified in b30eaa2 + b6920a2)
- `tests/python/CMakeLists.txt` — FOUND (modified in b6920a2)
- `docs/DECISIONS.md` — FOUND (modified in d1c321e)

Commits claimed exist:
- `b30eaa2` — FOUND (Task 1: TEST-07 edge battery)
- `b6920a2` — FOUND (Task 2: composition equivalence + fuzz)
- `d1c321e` — FOUND (Task 3: ADR-0007..0011 landings)

Plan success criteria:
- All 3 tasks executed — **PASSED**
- Each task committed individually with --no-verify — **PASSED**
- SUMMARY.md created in plan directory — **PASSED** (this file)
- test_reverb_edges Q15 battery passes (TEST-07) — **PASSED**
- test_reverb_body composition test passes (SC-1) — **PASSED**
  (3 seeds; reverb_body == compose(stages) byte-for-byte)
- fuzz_reverb.py runs 10^6 steps clean — **PASSED** (2.62s, 381k ops/s)
- docs/DECISIONS.md contains ADR-0007..0011 above ADR-0006 — **PASSED**
- Paraphrase discipline held — **PASSED** (no nocash verbatim;
  witnesses license-tagged)
