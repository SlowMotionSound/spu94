---
phase: 13-core-preset-api
plan: 01
subsystem: api
tags: [serialization, ini-format, preset, c99, snprintf]

requires:
  - phase: 05-presets
    provides: "Factory preset loader pattern, register infrastructure (spu94_snapshot_registers, spu94_reg_name, spu94_reg_type)"
provides:
  - "spu94_preset_save function -- serializes 46 fields to INI-style text"
  - "spu94_preset_load stub (ready for Plan 02 parser implementation)"
  - "SPU94_PRESET_BUF_SIZE constant (4096u)"
  - "parse_hex_u16 helper (hand-rolled, grep-guard safe)"
  - "15-test save-format validation suite"
affects: [13-02-PLAN, 14-cli-preset-commands, 14-juce-preset-ui]

tech-stack:
  added: []
  patterns: ["INI-style key=value serialization with [section] headers", "snprintf-based buffer-safe formatting with overflow detection", "EMIT macro pattern for sequential buffer writes"]

key-files:
  created:
    - "src/spu94/spu94_preset_io.c"
    - "tests/unit/preset/test_preset_roundtrip.c"
  modified:
    - "include/spu94/spu94.h"
    - "src/spu94/CMakeLists.txt"
    - "tests/unit/preset/CMakeLists.txt"

key-decisions:
  - "Used EMIT macro for overflow-checking snprintf pattern rather than separate check-after-each-call"
  - "parse_hex_u16 referenced via (void) cast in load stub to suppress -Wunused-function without __attribute__"
  - "Comments avoid grep-guard banned tokens by paraphrasing (no literal float/double/malloc/long in source text)"

patterns-established:
  - "EMIT macro: overflow-safe snprintf wrapper with early return -2 on buffer full"
  - "parse_hex_u16: hand-rolled hex parser avoiding strtol to satisfy grep-guard long ban"

requirements-completed: [PRE-01, PRE-03, PRE-05]

duration: 37min
completed: 2026-05-01
---

# Phase 13 Plan 01: Preset Save API Summary

**spu94_preset_save serializes 46 engine fields (35 registers + 7 mixer + 4 DAC) to versioned INI-style text with overflow protection, validated by 15 unit tests**

## Performance

- **Duration:** 37 min
- **Started:** 2026-05-01T20:49:03Z
- **Completed:** 2026-05-01T21:26:31Z
- **Tasks:** 2
- **Files modified:** 5

## Accomplishments
- Implemented spu94_preset_save: full 46-field serialization with version header, 3 sections, section comments
- Declared complete public API (save + load signatures + buffer size constant) in spu94.h
- Created parse_hex_u16 hand-rolled hex parser (ready for Plan 02's load function)
- 15 unit tests proving: error handling, format structure, field coverage, hex format, edge cases

## Task Commits

Each task was committed atomically:

1. **Task 1: Declare public API and implement spu94_preset_save** - `07414d3` (feat)
2. **Task 2: Test save output format and field coverage** - `7cbaa7f` (test)

## Files Created/Modified
- `src/spu94/spu94_preset_io.c` - Save function + parse_hex_u16 helper + load stub (124 LOC)
- `tests/unit/preset/test_preset_roundtrip.c` - 15 save-format validation tests (250 LOC)
- `include/spu94/spu94.h` - SPU94_PRESET_BUF_SIZE, spu94_preset_save, spu94_preset_load declarations
- `src/spu94/CMakeLists.txt` - Added spu94_preset_io.c to OBJECT library
- `tests/unit/preset/CMakeLists.txt` - Added test_preset_roundtrip target

## Decisions Made
- Used EMIT macro pattern for sequential snprintf calls with overflow checking (cleaner than inline checks)
- Suppressed unused-function warning on parse_hex_u16 via `(void)parse_hex_u16;` rather than compiler attributes
- Comments rewritten to avoid grep-guard banned literal tokens (float/double/malloc/long)
- Confirmed pre-existing grep-guard violations in other files are out of scope (logged below)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] Fixed -Wunused-function error on parse_hex_u16**
- **Found during:** Task 1 (build verification)
- **Issue:** parse_hex_u16 is defined for Plan 02's use but unused in Plan 01, triggering -Werror=unused-function
- **Fix:** Added `(void)parse_hex_u16;` reference in the spu94_preset_load stub
- **Files modified:** src/spu94/spu94_preset_io.c
- **Verification:** cmake --build build --target spu94_obj exits 0 with no warnings
- **Committed in:** 07414d3 (part of Task 1 commit)

**2. [Rule 1 - Bug] Fixed grep-guard violations in comments**
- **Found during:** Task 1 (verification)
- **Issue:** Source comments contained literal banned tokens (float, double, malloc, long) which grep-guard scans line-by-line
- **Fix:** Rewrote comments to paraphrase without using banned tokens directly
- **Files modified:** src/spu94/spu94_preset_io.c
- **Verification:** grep -nE forbidden pattern finds zero matches in spu94_preset_io.c
- **Committed in:** 07414d3 (part of Task 1 commit)

**3. [Rule 1 - Bug] Fixed -Wmissing-prototypes in test file**
- **Found during:** Task 2 (build)
- **Issue:** Test functions declared as `void test_*()` but project uses -Wmissing-prototypes -Werror
- **Fix:** Made all test functions `static void` (matching test_preset_load_all.c pattern)
- **Files modified:** tests/unit/preset/test_preset_roundtrip.c
- **Verification:** cmake --build build --target test_preset_roundtrip exits 0
- **Committed in:** 7cbaa7f (part of Task 2 commit)

---

**Total deviations:** 3 auto-fixed (all Rule 1 - build compliance bugs)
**Impact on plan:** All fixes were necessary for compilation under the project's strict -Werror policy. No scope creep.

## Known Stubs

| File | Line | Stub | Reason |
|------|------|------|--------|
| src/spu94/spu94_preset_io.c | spu94_preset_load() | Returns SPU94_OK without parsing | Plan 02 implements the full parser |

This is an intentional stub per the plan design (Plan 01 = save, Plan 02 = load).

## Issues Encountered
- /tmp filesystem filled to 100% during full ctest suite run (7.8GB tmpfs consumed by prior pytest sessions). Cleared old pytest/tmp files to resolve. Not related to this plan's changes.

## Next Phase Readiness
- spu94_preset_save is fully functional and tested
- Plan 02 can implement the load parser using the already-defined parse_hex_u16 helper
- All acceptance criteria met: 15 tests pass, no grep-guard violations in new code, no heap symbols introduced

---
*Phase: 13-core-preset-api*
*Completed: 2026-05-01*
