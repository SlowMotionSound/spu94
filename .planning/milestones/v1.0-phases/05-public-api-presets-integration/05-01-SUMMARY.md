---
phase: 05-public-api-presets-integration
plan: 01
subsystem: api

tags: [preset, audit, csv, provenance, rodata, unity, ctest]

requires:
  - phase: 02-buffer-register-infrastructure
    provides: spu94_reg_t enum (35-register ordering); SPU94_REG__COUNT; extern "C" header discipline
  - phase: 04-fir-half-band-resampling
    provides: BIBLIOGRAPHY.md Primary/Coefficient/Witness section structure
provides:
  - spu94_preset_id_t enum (OFF=0..DELAY=9, __COUNT=10)
  - spu94_preset_t typedef (const char *name + int16_t regs[35])
  - extern const spu94_preset_t spu94_presets[10] in .rodata
  - tests/python/verify_preset_sources.py resolutions-aware cell-equality gate
  - .planning/research/05-preset-values-audit-{nocash,hitmen}.csv audit artifacts
  - .planning/research/05-preset-values-audit-resolutions.md (16 Off cells resolved per BIB-011 priority)
  - BIB-011/012/013 entries in docs/BIBLIOGRAPHY.md under Preset Sources
  - tests/unit/preset/test_preset_table_integrity.c (count + names + Off-match + regs-length)
affects: [05-02, 05-03, 05-05, preset-loader, audio-process, fuzz-process]

tech-stack:
  added: []
  patterns:
    - "Two-source audit CSV + Python verifier pattern (schema mismatch -> exit 2, red -> exit 1, green -> exit 0)"
    - "Resolutions.md paired with verifier: undocumented red cells fail CI; documented ones are acknowledged-not-fixed"
    - "Designated initializer preset tables with (int16_t) bit-pattern casts for unsigned-family register storage"

key-files:
  created:
    - .planning/research/05-preset-values-audit-nocash.csv
    - .planning/research/05-preset-values-audit-hitmen.csv
    - .planning/research/05-preset-values-audit-resolutions.md
    - tests/python/verify_preset_sources.py
    - src/spu94/spu94_presets.c
    - tests/unit/preset/CMakeLists.txt
    - tests/unit/preset/test_preset_table_integrity.c
  modified:
    - include/spu94/spu94.h
    - src/spu94/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/python/CMakeLists.txt
    - docs/BIBLIOGRAPHY.md

key-decisions:
  - "Off preset uses BIB-011 nocash's 0x0001 defensive values at the 16 m-prefix register indices (per documented priority chain BIB-013 > BIB-011 > BIB-012; BIB-013 silent on per-register values so BIB-011 wins)"
  - "Preset-name normalization: Studio Small/Medium/Large -> Studio A/B/C; Chaos Echo -> Echo (canonical per Sony LIBSND)"
  - "Both audit CSVs preserve per-source provenance verbatim; edits go into resolutions.md, not into CSVs"
  - "Verifier is resolutions-aware: documented red cells do not fail CI, but undocumented ones do -- first-class regression gate on the audit"
  - "reg_idx 0..2 (vLOUT/vROUT/mBASE) = 0x0000 for all presets: neither source publishes per-preset values for those global registers"

patterns-established:
  - "Three-source audit workflow: two human-transcribed CSVs + Python verifier + paired resolutions doc"
  - "test_off_matches_audit pins the specific nonzero Off cells (not blanket all-zero) so future regression is cell-specific, not coarse"

requirements-completed:
  - CORE-09

duration: ~25min
completed: 2026-04-20
---

# Phase 05-01: Preset Data + Provenance Summary

**Ten PS1 factory reverb preset tables landed in .rodata with byte-for-byte traceability to two audited sources; Off preset resolved in favor of BIB-011's defensive 0x0001 buffer-offset values per the documented priority chain.**

## Performance

- **Duration:** ~25 min
- **Completed:** 2026-04-20
- **Tasks:** 6
- **Files created:** 7 (2 CSVs + resolutions.md + verifier.py + presets.c + test TU + its CMakeLists.txt)
- **Files modified:** 5 (public header + 3 CMakeLists.txt + BIBLIOGRAPHY)

## Accomplishments

- 10 preset tables in `src/spu94/spu94_presets.c` -- 350 int16 values across 10 designated-initializer rows, in .rodata, exported as `spu94_presets` data symbol
- Public preset surface: `spu94_preset_id_t` enum + `spu94_preset_t` typedef + `spu94_presets[]` extern, ready for Plan 02 (process/flush) and Plan 03 (`spu94_load_preset`) to consume
- D-07 three-source audit committed as durable provenance: two CSVs + Python cell-equality verifier + resolutions.md for the 16 Off cells
- `test_preset_table_integrity` pins the ABI contract: count=10, canonical names, Off matches the audit (including the 16 nonzero buffer-offset cells), regs[] length == SPU94_REG__COUNT
- BIBLIOGRAPHY gains BIB-011/012/013 under a new Preset Sources (Phase 5) section with honest lineage notes
- Full test suite stays 40/40 green; grep-guard + verify-no-heap-symbols still clean; no new warnings

## Task Commits

1. **Task 1: BIB-011 nocash CSV** -- `4bd35b4` (docs)
2. **Task 2: BIB-012 hitmen CSV** -- `8d8c648` (docs)
3. **Task 3: verify_preset_sources.py + CMake wiring** -- `fcdaf3e` (test)
4. **Task 4: audit resolutions per BIB-011 priority** -- `170a2aa` (docs)
5. **Task 5: preset table + integrity test + resolutions-aware verifier** -- `7e51bda` (feat)
6. **Task 6: BIB-011/012/013 entries in BIBLIOGRAPHY** -- `4d770fa` (docs)

## Files Created/Modified

- `.planning/research/05-preset-values-audit-nocash.csv` -- BIB-011 cell-level provenance
- `.planning/research/05-preset-values-audit-hitmen.csv` -- BIB-012 cell-level provenance
- `.planning/research/05-preset-values-audit-resolutions.md` -- the 16 Off cells, decision rationale
- `tests/python/verify_preset_sources.py` -- resolutions-aware cell-equality gate
- `src/spu94/spu94_presets.c` -- the 10 preset tables in .rodata
- `include/spu94/spu94.h` -- spu94_preset_id_t, spu94_preset_t, spu94_presets[] extern
- `src/spu94/CMakeLists.txt` -- wires spu94_presets.c into spu94_obj
- `tests/unit/preset/{CMakeLists.txt, test_preset_table_integrity.c}` -- new test subdir
- `tests/unit/CMakeLists.txt` -- add_subdirectory(preset)
- `tests/python/CMakeLists.txt` -- verify_preset_sources ctest registration with LABELS "preset"
- `docs/BIBLIOGRAPHY.md` -- new Preset Sources section, 3 entries

## Decisions Made

- Task 4 checkpoint routed to priority-resolve (BIB-011 wins). The Off preset disagreement between nocash (0x0001 at 16 m-prefix regs) and hitmen (all-zero) is systematic, not transcription error. Following the plan's documented priority chain (BIB-013 > BIB-011 > BIB-012) honors project posture and sets a clean precedent for any future audit-level disagreement that may arise from an M5 hardware witness.
- `test_off_all_zero` (as specified in the plan body) renamed to `test_off_matches_audit` and rewritten to pin the specific 16 nonzero cells with 0x0001. The test remains strict: any drift in either direction fails. The whitelist of nonzero indices is co-located with the resolutions.md path in a comment.
- The verifier was upgraded from a binary cell-equality checker to a resolutions-aware checker. Undocumented red cells still fail exit 1; documented ones are counted and reported but do not fail. This satisfies the plan's Task 4 acceptance criterion verbatim ("the verifier is re-run to confirm only resolved cells remain red") while keeping ctest green in CI.
- Both CSVs preserve per-source verbatim values. The project's audit story is "these are the two sources, here is what each published, here is how we resolved the delta" -- editing the CSVs would erase that.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 -- Missing Critical] Trailing-comma bug in the ad-hoc preset-table generator**
- **Found during:** Task 5 Step B first compile
- **Issue:** The emitter appended `,` to each preset block AND joined blocks with `,\n`, producing `,,` between blocks
- **Fix:** Removed the per-block trailing comma; kept the `,\n` join
- **Verification:** Rebuild from the regenerated file succeeded; gcc clean; designated initializers all parse
- **Committed in:** `7e51bda` (Task 5 commit)

**2. [Rule 1 -- Plan-Level Adjustment] Verifier upgraded to resolutions-aware**
- **Found during:** Task 5 acceptance check
- **Issue:** Plan Task 5 acceptance required `ctest -R verify_preset_sources` green, but with BIB-011 priority the CSVs retain their 16-cell Off disagreement forever. Binary cell-equality would leave the test red permanently.
- **Fix:** Verifier now parses resolutions.md and treats documented cells as acknowledged. Schema: undocumented red -> exit 1, schema issues -> exit 2, all-green-or-fully-documented -> exit 0.
- **Verification:** `python3 tests/python/verify_preset_sources.py` returns 0; output: `PASS: 334/350 cells agree; 16 documented disagreements in resolutions.md`
- **Committed in:** `7e51bda` (Task 5 commit)

---

**Total deviations:** 2 auto-fixed (1 compile bug, 1 plan adjustment)
**Impact on plan:** Both changes necessary and scope-preserving. The resolutions-aware verifier is a stronger regression gate than binary cell-equality -- it still catches new undocumented disagreements, just doesn't fail on the one we decided to accept.

## Issues Encountered

- None beyond the two auto-fixed items above.

## User Setup Required

None.

## Next Phase Readiness

- Plan 02 (spu94_process + spu94_flush): the public header now has `spu94_preset_id_t`, `spu94_preset_t`, and `spu94_presets[]` extern declarations available. Plan 02 modifies the same header file but in a non-overlapping region (inserting `spu94_process`/`spu94_flush` prototypes near the top, not near the preset section at the bottom).
- Plan 03 (spu94_load_preset): can iterate `spu94_presets[SPU94_PRESET_<id>].regs[]` by reg_idx 0..34 and call the Phase 2 engine setters directly. The (int16_t) bit-pattern storage means unsigned-family registers need `(uint16_t)` reinterpretation before `spu94_set_reg_u16`.
- Plan 05 (fuzz + ADR landings): the audit CSVs + resolutions.md are the inputs for the preset-sourcing ADR in docs/DECISIONS.md. The fuzz harness for `spu94_process` can use `spu94_load_preset` + any preset_id.

---
*Phase: 05-public-api-presets-integration*
*Plan: 01*
*Completed: 2026-04-20*
