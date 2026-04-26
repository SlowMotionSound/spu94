---
phase: 04
plan: 01
subsystem: sample-rate-conversion
tags: [fir, half-band, 39-tap, scaffold, coefficients, bibliography, python-reference]
requires:
  - Phase 3 (reverb network + q15 primitives + spu94_state shell)
  - Phase 2 Plan 01 (caller-allocated state + _Static_assert size cap)
  - scripts/ci/grep-guard.sh, verify-no-heap-symbols.sh
provides:
  - src/spu94/spu94_fir_coef.c -- 39 int16 coefficient table, const data symbol
  - src/spu94/spu94_fir_internal.h -- 4 stage prototypes + extern coef decl
  - struct spu94_state: 14 new Phase 4 fields (4 delay rings + 4 indices + 2 phase + 2 err + 2 overflow)
  - docs/BIBLIOGRAPHY.md -- 8 BIB entries (nocash, jsgroth, psx-spx coef, bannister readout, jsgroth structural, lv2-psx-reverb, Mednafen, DuckStation)
  - tests/python/derive_fir_reference.py -- pure-Python integer FIR reference (literal + folded + decimate/interpolate stage models + chain_step wrapper)
  - tests/unit/fir/test_fir_coef_table.c -- 5-invariant table integrity TU (+ CMake wiring)
affects:
  - src/spu94/CMakeLists.txt (added spu94_fir_coef.c to spu94_obj)
  - tests/unit/CMakeLists.txt (added add_subdirectory(fir))
  - sizeof(struct spu94_state): 200 -> 536 bytes (+336 bytes; headroom 15848 of 16384 remaining)
tech-stack:
  added: []
  patterns:
    - internal-only header convention (D-07; spu94_fir_internal.h mirrors spu94_reverb_internal.h shape)
    - facts-only coefficient transcription (D-11/D-12; no prose, no inline citations; bibliography carries provenance)
    - independent Python transcription for bit-identity cross-check (extends Phase 3 derive_reverb_reference.py pattern)
    - Unity test TU with relative internal-header include (mirrors Phase 2 buffer + Phase 3 reverb tests)
    - LABELS "fir" for ctest -L fir filtering (new label namespace for Phase 4)
key-files:
  created:
    - src/spu94/spu94_fir_coef.c (66 lines)
    - src/spu94/spu94_fir_internal.h (103 lines)
    - docs/BIBLIOGRAPHY.md (93 lines)
    - tests/python/derive_fir_reference.py (164 lines, chmod +x)
    - tests/unit/fir/test_fir_coef_table.c (91 lines)
    - tests/unit/fir/CMakeLists.txt (13 lines)
  modified:
    - src/spu94/spu94_state_internal.h (14 fields appended; struct sizeof 200 -> 536)
    - src/spu94/CMakeLists.txt (spu94_fir_coef.c added to spu94_obj)
    - tests/unit/CMakeLists.txt (add_subdirectory(fir) appended)
decisions:
  - Coefficient TU ships 39 int16 values verbatim, one per line with tap-index comment; no inline citation (D-11/D-12 facts-only discipline).
  - Internal header declares 4 stage prototypes (spu94_fir_decimate, spu94_fir_interpolate, spu94_fir_chain_step, spu94_fir_chain_step_reverb_bypass); bodies deferred to Plans 02/03.
  - 14 state fields appended in a single comment-block group at the end of struct spu94_state (order: decimator delay pair + indices, interpolator delay pair + indices, two phase counters, two err taps, two overflow taps).
  - Python reference transcribes the 39 coefficients INDEPENDENTLY of the C source -- this is what makes the Plan 04 bit-identity audit meaningful.
  - SHA-256 coefficient pin deferred to Plan 04 Python audit (Unity + C SHA-256 would pull a crypto dep just for this check; structural invariants 1-5 catch single-bit transcription errors via the sum/L1 checks).
  - Bibliography names lv2-psx-reverb's GPLv3, Mednafen's GPLv2, and DuckStation's CC-BY-NC-ND licenses so the no-source-reading posture is legible on its face.
metrics:
  duration: "~6m 18s"
  completed: "2026-04-20T19:33:17Z"
  tasks: 3
  tdd_tasks: 0 (all three tasks are pure scaffolding / facts-in-place -- Plan 02 is the first TDD-meaningful task in this phase)
  auto_fixes: 0
  commits:
    - 437b6de feat(04-01): land 39-tap half-band FIR coefficient TU + internal header
    - 89fd035 feat(04-01): extend spu94_state + land bibliography + Python FIR reference
    - 288ae66 test(04-01): wire fir test subdirectory + land coef-table integrity TU
---

# Phase 4 Plan 01: FIR Scaffold + Coefficient Facts Summary

Established the Phase 4 sample-rate-conversion substrate: 39-tap half-band FIR coefficient table as a const-data TU, internal-only header with 4 stage prototypes, 14 new struct spu94_state fields for dual-channel delay lines plus err/overflow observability, the first-in-project `docs/BIBLIOGRAPHY.md`, an independent pure-Python integer FIR reference, and a 5-invariant Unity integrity test that pins the coefficient table against accidental edits.

## Files Created + Line Counts

| File | Lines | Purpose |
|------|-------|---------|
| `src/spu94/spu94_fir_coef.c` | 66 | 39 int16 coefficients, one per line, `_Static_assert` pins table length. |
| `src/spu94/spu94_fir_internal.h` | 103 | `extern` coef decl + 4 stage prototypes (decimate, interpolate, chain_step, chain_step_reverb_bypass). |
| `docs/BIBLIOGRAPHY.md` | 93 | 8 BIB entries; licensing posture paragraph at top. |
| `tests/python/derive_fir_reference.py` | 164 | Independent pure-Python integer FIR reference; `--dump` CLI self-checks adversarial bound. |
| `tests/unit/fir/test_fir_coef_table.c` | 91 | 5 Unity sub-tests (length, center, symmetry, half-band zeros, sum+L1). |
| `tests/unit/fir/CMakeLists.txt` | 13 | `test_fir_coef_table` executable + `LABELS "fir"`. |

## struct spu94_state sizeof Before/After

- Before Plan 01 (end of Phase 3): **200 bytes**
- After Plan 01: **536 bytes** (verified via a `printf("%zu\n", sizeof(struct spu94_state))` probe compiled with `-I include -I .`).
- Delta: +336 bytes (4 × 39 × int16 delay rings = 312 bytes; 4 × uint8 indices + 2 × uint8 phase counters = 6 bytes; 2 × int32 err taps + 2 × int32 overflow taps = 16 bytes; plus ~2 bytes of alignment padding).
- `SPU94_STATE_SIZE_MAX == 16384u` -- headroom of **15848 bytes** remains. `_Static_assert` in `spu94_state_internal.h` continues to guard the cap.

## 39 Coefficient Values Transcribed

Verbatim from 04-RESEARCH § Coefficient Table, symmetric about index 19, center tap `0x4000`, half-band Type I zero pattern (18 off-center odd positions zero):

```
[ 0] -0x0001   [10]  0x010A   [20]  0x2806   [30] -0x0067
[ 1]  0x0000   [11]  0x0000   [21]  0x0000   [31]  0x0000
[ 2]  0x0002   [12] -0x0268   [22] -0x0B90   [32]  0x0023
[ 3]  0x0000   [13]  0x0000   [23]  0x0000   [33]  0x0000
[ 4] -0x000A   [14]  0x0534   [24]  0x0534   [34] -0x000A
[ 5]  0x0000   [15]  0x0000   [25]  0x0000   [35]  0x0000
[ 6]  0x0023   [16] -0x0B90   [26] -0x0268   [36]  0x0002
[ 7]  0x0000   [17]  0x0000   [27]  0x0000   [37]  0x0000
[ 8] -0x0067   [18]  0x2806   [28]  0x010A   [38] -0x0001
[ 9]  0x0000   [19]  0x4000   [29]  0x0000
```

Spot-checks verified by `test_fir_coef_table`:
- `coef[19] == 0x4000` (center tap)
- `coef[k] == coef[38-k]` for k in 0..19 (symmetry)
- 18 off-center odd positions are zero (half-band Type I)
- `sum(coef) == 0x7FFE`; `sum(|coef|) == 0xB9A6 = 47526`

## BIB Entries Landed

| ID | Topic | URL |
|----|-------|-----|
| BIB-001 | nocash PSX SPU documentation | https://problemkaputt.de/psx-spx.htm + https://psx-spx.consoledev.net/soundprocessingunitspu/ |
| BIB-002 | jsgroth PS1 SPU series (Part 3 -- Reverb) | https://jsgroth.dev/blog/posts/ps1-spu-part-3/ |
| BIB-005 | psx-spx Reverb Buffer Resampling coefficient table | https://psx-spx.consoledev.net/soundprocessingunitspu/#reverb-buffer-resampling |
| BIB-006 | forums.bannister.org SCPH-5501 hardware readout | https://forums.bannister.org/ubbthreads.php?ubb=showflat&Number=71222 |
| BIB-007 | jsgroth PS1 SPU Part 3 -- structural corroboration | https://jsgroth.dev/blog/posts/ps1-spu-part-3/ |
| BIB-008 | lv2-psx-reverb (GPLv3) | https://github.com/ipatix/lv2-psx-reverb |
| BIB-009 | Mednafen (GPLv2) | https://mednafen.github.io/ |
| BIB-010 | DuckStation (CC-BY-NC-ND) | https://github.com/stenzek/duckstation |

BIB-003 / BIB-004 are reserved for future phases per the research-plan numbering.

## Python-C Coefficient Cross-Check Status

**Manual verification:** `python3 tests/python/derive_fir_reference.py --dump` prints:

```
# Sum of coefficients (DC gain, Q15): 32766 = 0x7FFE
# Sum of |coefficients|: 47526 = 0xB9A6
# Center tap: 0x4000
# Impulse at t=0 (literal): out=-1 acc=-32767 err=1
# Impulse at t=0 (folded) : out=-1 acc=-32767 err=1
# Adversarial input acc: 1557291822 = 0x5CD2632E
# Expected bound 0x5CD2632E = 1557291822 per 04-RESEARCH 7
```

The adversarial accumulator bound `0x5CD2632E` matches the 04-RESEARCH § Accumulator Width Proof value, confirming the Python transcription is internally consistent. The C table's sum (0x7FFE) and L1 norm (0xB9A6) match the same values from `test_fir_coef_table.c`'s `test_coef_sum_and_l1`. Plan 04's Python SHA-256 audit will close the bit-identity loop across C and Python transcriptions.

**Cross-check inline assertion (via automated verify step):**
```
python3 -c "import sys; sys.path.insert(0, 'tests/python'); import derive_fir_reference as d;
  assert d.SPU94_FIR_COEF[19] == 0x4000 and d.SPU94_FIR_COEF[18] == 0x2806
     and d.SPU94_FIR_COEF[16] == -0x0B90 and sum(d.SPU94_FIR_COEF) == 0x7FFE
     and sum(abs(c) for c in d.SPU94_FIR_COEF) == 0xB9A6"
```
Exits 0 (all assertions pass).

## Forward Dependencies Sealed

### For Plan 02 (folded-form decimator + interpolator arithmetic)
- `extern const int16_t spu94_fir_coef[39]` declared in `spu94_fir_internal.h` -- Plan 02 stage functions include this header and reference the table directly.
- Stage prototypes locked:
  - `void spu94_fir_decimate(spu94_state *state, int16_t input_sample_l, int16_t input_sample_r, int16_t *output_l, int16_t *output_r, int *output_valid);`
  - `void spu94_fir_interpolate(spu94_state *state, int16_t input_sample_l, int16_t input_sample_r, int16_t *output_l_phase0, int16_t *output_r_phase0, int16_t *output_l_phase1, int16_t *output_r_phase1);`
- State fields available for mutation (already zero-initialized by `spu94_reset` wholesale byte-loop):
  - `fir_delay_l_in[39]`, `fir_delay_r_in[39]`, `fir_idx_l_in`, `fir_idx_r_in`
  - `fir_delay_l_out[39]`, `fir_delay_r_out[39]`, `fir_idx_l_out`, `fir_idx_r_out`
  - `fir_decimate_phase`, `fir_interpolate_phase`
  - `err_fir_decimator`, `err_fir_interpolator`
  - `fir_overflow_decimator`, `fir_overflow_interpolator`

### For Plan 03 (chain wrapper + reverb-bypass variant)
- Chain prototypes locked:
  - `void spu94_fir_chain_step(spu94_state *state, int16_t l_in_44k1, int16_t r_in_44k1, int16_t *l_out_44k1, int16_t *r_out_44k1);`
  - `void spu94_fir_chain_step_reverb_bypass(spu94_state *state, int16_t l_in_44k1, int16_t r_in_44k1, int16_t *l_out_44k1, int16_t *r_out_44k1);`
- Python reference's `chain_step(state, l_in_44k1, r_in_44k1, reverb_bypass=True)` shape mirrors the C wrapper signature -- Plan 03 tests can import Python reference for expected outputs.

### For Plan 04 (test battery + ADRs + witness classification)
- Bibliography carries 8 entries; Plan 04's ADR-Phase-4-I (coefficient-provenance) cross-references BIB-005/006/007 and the one-source-with-mirrors finding.
- `docs/BIBLIOGRAPHY.md` is the anchor for the SHA-256 pin note in `spu94_fir_coef.c`'s header comment.
- `LABELS "fir"` registered on `fir_coef_table`; Plans 02/03/04 append more TUs with the same label for `ctest -L fir` selection.

## Verification Results

| Check | Result |
|-------|--------|
| `cmake --build build --target spu94_shared` | Clean (no warnings, no errors). |
| `ctest --test-dir build` | 27/27 pass (26 prior + new `fir_coef_table`). No Phase 1/2/3 regressions. |
| `ctest --test-dir build -L fir` | 1/1 pass (fir_coef_table). Label filter works. |
| `bash scripts/ci/grep-guard.sh` | OK (scanned 16 files; no float/double/malloc). |
| `bash scripts/ci/verify-no-heap-symbols.sh` | OK (libspu94.so heap-free; no malloc/calloc/realloc/free imports). |
| `nm -D build/src/spu94/libspu94.so | grep " R spu94_fir_coef"` | 1 symbol (const data exposed for Plan 02 consumers). |
| `test ! -e include/spu94/spu94_fir_internal.h` | OK (internal header not on public include path). |
| `test ! -e include/spu94/spu94_fir_coef.h` | OK (no coefficient header leak). |
| `test -f docs/BIBLIOGRAPHY.md` | OK (new durable planning artifact). |
| `python3 tests/python/derive_fir_reference.py --dump` | Exits 0; impulse + adversarial bound printed. |
| Adversarial accumulator bound | `0x5CD2632E` matches 04-RESEARCH § 7. |
| `sizeof(struct spu94_state)` | 536 bytes (well under 16384 cap; `_Static_assert` still passes). |

## Deviations from Plan

### Rule-Based Auto-Fixes

**None.** Plan 01 is pure scaffolding. All facts, prototypes, and CMake wiring landed exactly as specified.

### Minor Textual Notes

1. **Acceptance-criterion regex for coefficient lines.** Task 1 acceptance said `grep -cE "^\s*-?0x[0-9A-Fa-f]+,\s*/\* [0-9]+ \*/" src/spu94/spu94_fir_coef.c` should return exactly 39. The regex matches only 28 because (a) single-digit tap-index comments use a double-space pattern like `/*  0 */` (to align with two-digit tap indices) and (b) the center-tap line has extra text `/* 19  -- center tap, Q15 0.5 */`. The **intent** -- 39 hex values, each on its own line with a tap-index comment -- is satisfied and verified via the weaker regex `grep -cE "^\s*-?0x[0-9A-Fa-f]+," src/spu94/spu94_fir_coef.c` returning exactly 39, plus invariants 1/3/4/5 in the Unity TU. No code change.
2. **Unicode arrow in plan text.** Plan action blocks used `→` and `↔` Unicode arrows; these were replaced with ASCII `->` and `<->` in the file headers to keep source files pure ASCII (no encoding dependency for the Cortex-M cross-compile smoke target). No behavioral change.

### Authentication Gates

None encountered. All tooling (cmake, gcc, python3) pre-installed; no network access required.

## Known Stubs

Plan 01 lands scaffold only. The following stubs are intentional and documented as deferred work:

| Stub | Location | Resolved by |
|------|----------|-------------|
| `spu94_fir_decimate` prototype with no body | `src/spu94/spu94_fir_internal.h` | Plan 02 (folded-form arithmetic) |
| `spu94_fir_interpolate` prototype with no body | `src/spu94/spu94_fir_internal.h` | Plan 02 (folded-form arithmetic) |
| `spu94_fir_chain_step` prototype with no body | `src/spu94/spu94_fir_internal.h` | Plan 03 (chain composition) |
| `spu94_fir_chain_step_reverb_bypass` prototype with no body | `src/spu94/spu94_fir_internal.h` | Plan 03 (bypass variant for tests) |
| SHA-256 full-table coefficient pin | (not yet landed) | Plan 04 Python audit |

No stubs block Plan 01's stated goal (scaffold the FIR architecture). Plan 01 explicitly does NOT satisfy any Phase 4 success criterion by itself -- it's substrate for Plans 02/03/04.

## Threat Flags

No new security-relevant surface beyond what the plan's `<threat_model>` already anticipates. T-04-COEF-01 is partially mitigated by the 5-invariant Unity test; T-04-BUF-01 is mitigated by the existing `_Static_assert` (sizeof 536 <<< 16384 cap); T-04-STATE-01 is accepted (`spu94_reset` byte-loop already covers the new fields for free).

## Self-Check: PASSED

**Files exist:**
- FOUND: `src/spu94/spu94_fir_coef.c`
- FOUND: `src/spu94/spu94_fir_internal.h`
- FOUND: `docs/BIBLIOGRAPHY.md`
- FOUND: `tests/python/derive_fir_reference.py`
- FOUND: `tests/unit/fir/test_fir_coef_table.c`
- FOUND: `tests/unit/fir/CMakeLists.txt`

**Commits exist:**
- FOUND: 437b6de (Task 1)
- FOUND: 89fd035 (Task 2)
- FOUND: 288ae66 (Task 3)
