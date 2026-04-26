---
phase: 02-buffer-register-infrastructure
plan: 02
subsystem: api
tags: [register-identity, q15-error-tap, tick-entry-point, adr-0004, extensibility-seams]

# Dependency graph
requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: q15_mul_truncate (refactored to wrapper this plan), spu94_q15.h (extended), umbrella header
  - plan: 02-01
    provides: opaque spu94_state typedef, lifecycle API, reg_values[35] storage slot already reserved
provides:
  - spu94_reg_t public enum with 35 sequentially numbered entries + SPU94_REG__COUNT sentinel
  - spu94_reg_hw_offset(reg) returning psx-spx hardware offset; 0xFFFF on out-of-range
  - spu94_reg_name(reg) returning bare name (e.g., "vIIR"); NULL on out-of-range
  - spu94_snapshot_registers(state, out[35]) declaration + zero-fill stub body (Plan 03 wires real storage)
  - q15_mul_truncate_with_err(a, b, err_out) — pre-saturation truncation remainder tap (D-18)
  - q15_mul_truncate refactored to a thin wrapper passing err_out=NULL; bit-identical to Phase 1
  - spu94_tick(state) public no-op stub — atomic per-22.05 kHz tick entry point (D-19)
  - ADR-0004 in docs/DECISIONS.md documenting both taps as intentional public seams
  - tests/unit/registers/ test subdirectory (9 Unity tests) wired into CTest
affects: [02-03, 02-04, 02-05, 03-reverb-algorithm, 05-public-api, 06-python-bindings]

# Tech tracking
tech-stack:
  added:
    - "designated-initializer parallel tables (uint16_t hw_offset + const char* name) keyed by enum"
  patterns:
    - "Enum + parallel-table register identity surface, _Static_assert-pinned to canonical count"
    - "Forward-declare opaque type in sub-header to break circular include with umbrella"
    - "Single-typedef-home discipline for opaque types under -std=c99 -pedantic (API-07)"
    - "Public observation-tap functions paired with thin wrappers that pass NULL"

key-files:
  created:
    - "include/spu94/spu94_registers.h"
    - "src/spu94/spu94_registers.c"
    - "src/spu94/spu94_tick.c"
    - "tests/unit/registers/CMakeLists.txt"
    - "tests/unit/registers/test_register_identity.c"
  modified:
    - "include/spu94/spu94.h (includes spu94_registers.h; declares spu94_tick; removes duplicate spu94_state typedef)"
    - "include/spu94/spu94_q15.h (adds q15_mul_truncate_with_err; refactors q15_mul_truncate to wrapper)"
    - "src/spu94/CMakeLists.txt (adds spu94_registers.c + spu94_tick.c; drops spu94_placeholder.c)"
    - "tests/unit/CMakeLists.txt (add_subdirectory(registers))"
    - "tests/unit/q15/test_q15.c (5 new tests for _with_err and spu94_tick)"
    - "docs/DECISIONS.md (ADR-0004 prepended at top, line 33)"
  deleted:
    - "src/spu94/spu94_placeholder.c (Phase 1 scaffold; superseded by spu94_state.c + spu94_registers.c + spu94_tick.c)"

key-decisions:
  - "Enum ordering: vLOUT, vROUT, mBASE first (routing/base registers, outside the reverb block), then reverb block 0x1DC0..0x1DFE in ascending hardware-offset order — matches the plan's <interfaces> table verbatim and gives a clean Python IntEnum mapping"
  - "Out-of-range spu94_reg_name returns NULL (not empty string); header documents this; tests pin it"
  - "_Static_assert((int)SPU94_REG__COUNT == 35, ...) lives in src/spu94/spu94_registers.c — guards both the hw_offsets and names tables against drift; an additional _Static_assert in the test TU pins the same invariant from the consumer side"
  - "q15_mul_truncate_with_err remainder is PRE-saturation: for INT16_MIN^2 the function returns INT16_MAX with err=0 (the +2^30 product is exactly divisible by 2^15; the saturation discard is a separate quantity callers can derive)"
  - "spu94_state typedef has a single home in spu94_registers.h (forward decl); spu94.h no longer re-typedefs it — required to keep -std=c99 -pedantic clean (API-07 surface)"
  - "spu94_placeholder.c removed in this plan — three real TUs (state, registers, tick) make the placeholder structurally redundant"

requirements-completed:
  - CORE-04
  - API-04
  - API-07

# Metrics
duration: 7m 7s
completed: 2026-04-19
---

# Phase 2 Plan 02: Register Identity + Extensibility Taps Summary

**The 35-register identity surface (enum + hw_offset + name + atomic snapshot declaration), the Q15 error-observation tap with pre-saturation remainder semantics, the public spu94_tick per-tick entry point stub, and ADR-0004 documenting both taps as intentional seams — all on a chassis that compiles clean under C99-pedantic and through an extern "C" C++ consumer.**

## Performance

- **Duration:** ~7 min 7 s (autonomous executor; baseline build green at start)
- **Started:** 2026-04-19T19:52:41Z
- **Completed:** 2026-04-19T19:59:48Z
- **Tasks:** 3 (Tasks 1 and 2 TDD-split into RED + GREEN commits)
- **Commits:** 5 (RED, GREEN, RED, GREEN, ADR doc)
- **Files created:** 5 — modified: 5 — deleted: 1

## Accomplishments

- **Register identity surface (Task 1):** `include/spu94/spu94_registers.h` declares the `spu94_reg_t` enum with 35 sequentially numbered entries plus `SPU94_REG__COUNT` sentinel = 35. `src/spu94/spu94_registers.c` implements two parallel designated-initializer tables (`spu94_reg_hw_offsets[]` and `spu94_reg_names[]`) keyed by enum value, plus the three accessors (`spu94_reg_hw_offset`, `spu94_reg_name`, `spu94_snapshot_registers`). `_Static_assert((int)SPU94_REG__COUNT == 35, ...)` pins the count.
- **Q15 error-observation tap (Task 2):** `include/spu94/spu94_q15.h` now defines `q15_mul_truncate_with_err(a, b, err_out)` as the source-of-truth Q15 multiply. `q15_mul_truncate` is refactored to a one-line wrapper that passes `err_out = NULL` — the Phase 1 reference test table continues to pass bit-exactly without modification. The remainder is documented (header + ADR) as pre-saturation.
- **`spu94_tick` public stub (Task 2):** `void spu94_tick(spu94_state *state)` declared in `include/spu94/spu94.h`; empty-body implementation in `src/spu94/spu94_tick.c`. NULL-safe. Two new tests cover NULL and valid-state paths.
- **`spu94_placeholder.c` removed (Task 2):** Three real source files (state, registers, tick) make the Phase 1 scaffold structurally redundant. The build's source list now reflects Phase 2 work directly.
- **ADR-0004 (Task 3):** Prepended to `docs/DECISIONS.md` above ADR-0001. Records q15 error tap + spu94_tick as intentional extensibility taps; names the future Controllers milestone and Error Accumulator project as the consumers; calls out the bit-faithfulness invariant; lists three revision paths.
- **Test surface:** 9 new Unity tests in `tests/unit/registers/test_register_identity.c` (count, hw_offset spot checks + out-of-range, name spot checks + out-of-range, full-range non-NULL/non-0xFFFF coverage, offset uniqueness, snapshot zero-fill, NULL-out tolerance) plus 5 new tests in `tests/unit/q15/test_q15.c` (`_with_err(NULL)` matches base across the Phase 1 table, remainder table, INT16_MIN^2 pre-saturation case, two `spu94_tick` null-safety tests). All tests pass.
- **Full ctest suite green:** 5/5 (`q15_unit` with 8 sub-tests, `state_lifecycle`, `register_identity_unit`, `api_c99_consumer`, `api_cxx_consumer`).
- **CI invariants green:** `grep-guard.sh` (6 core files now), `verify-no-heap-symbols.sh build/src/spu94/libspu94.so`. The library has all four expected exported text symbols: `spu94_reg_hw_offset`, `spu94_reg_name`, `spu94_snapshot_registers`, `spu94_tick`.

## Specific Numbers (per `<output>` requirements)

### Final enum ordering (deviations from proposed)

**No deviation.** The enum ships exactly in the order proposed in the plan's `<interfaces>` block:

```
0  vLOUT     1  vROUT     2  mBASE     3  dAPF1     4  dAPF2
5  vIIR      6  vCOMB1    7  vCOMB2    8  vCOMB3    9  vCOMB4
10 vWALL    11 vAPF1     12 vAPF2     13 mLSAME    14 mRSAME
15 mLCOMB1  16 mRCOMB1   17 mLCOMB2   18 mRCOMB2   19 dLSAME
20 dRSAME   21 mLDIFF    22 mRDIFF    23 mLCOMB3   24 mRCOMB3
25 mLCOMB4  26 mRCOMB4   27 dLDIFF    28 dRDIFF    29 mLAPF1
30 mRAPF1   31 mLAPF2    32 mRAPF2    33 vLIN      34 vRIN
SPU94_REG__COUNT = 35
```

Routing/base registers (vLOUT, vROUT, mBASE) front the enum because their hardware offsets (0x1D84, 0x1D86, 0x1DA2) sit *outside* the reverb block proper (0x1DC0..0x1DFE). Within the reverb block, ordering follows ascending hardware offset — making the table easy to audit against psx-spx by reading top-to-bottom.

### Hardware offsets (each confirmed against psx-spx; no discrepancies)

| Enum | Offset | Enum | Offset | Enum | Offset |
|------|--------|------|--------|------|--------|
| vLOUT | 0x1D84 | mLSAME | 0x1DD4 | mRCOMB3 | 0x1DEA |
| vROUT | 0x1D86 | mRSAME | 0x1DD6 | mLCOMB4 | 0x1DEC |
| mBASE | 0x1DA2 | mLCOMB1 | 0x1DD8 | mRCOMB4 | 0x1DEE |
| dAPF1 | 0x1DC0 | mRCOMB1 | 0x1DDA | dLDIFF | 0x1DF0 |
| dAPF2 | 0x1DC2 | mLCOMB2 | 0x1DDC | dRDIFF | 0x1DF2 |
| vIIR | 0x1DC4 | mRCOMB2 | 0x1DDE | mLAPF1 | 0x1DF4 |
| vCOMB1 | 0x1DC6 | dLSAME | 0x1DE0 | mRAPF1 | 0x1DF6 |
| vCOMB2 | 0x1DC8 | dRSAME | 0x1DE2 | mLAPF2 | 0x1DF8 |
| vCOMB3 | 0x1DCA | mLDIFF | 0x1DE4 | mRAPF2 | 0x1DFA |
| vCOMB4 | 0x1DCC | mRDIFF | 0x1DE6 | vLIN | 0x1DFC |
| vWALL | 0x1DCE | mLCOMB3 | 0x1DE8 | vRIN | 0x1DFE |
| vAPF1 | 0x1DD0 | | | | |
| vAPF2 | 0x1DD2 | | | | |

The `test_offsets_are_unique` Unity test scans all `C(35,2) = 595` pairs and confirms no two registers share an offset; passes.

### `q15_mul_truncate_with_err` remainder semantics chosen

**Pre-saturation remainder.** Header text and ADR-0004 both pin this. The math is:

```
product   = (int32_t)a * (int32_t)b
shifted   = product >> 15           // ASR
result    = sat_s16(shifted)
remainder = product - (shifted << 15)   // independent of saturation
```

For `INT16_MIN * INT16_MIN`:
- `product   = +2^30 = 1073741824`
- `shifted   = +2^15 = 32768`
- `result    = INT16_MAX = 32767` (saturated discard = 1)
- `remainder = 1073741824 - (32768 << 15) = 1073741824 - 1073741824 = 0`

Test `test_q15_mul_truncate_with_err_saturation_remainder_is_pre_saturation` pins this. The header explains how callers can derive the additional saturation discard (`shifted - INT16_MAX` when `result == INT16_MAX && shifted > INT16_MAX`).

### ADR-0004 line placement in docs/DECISIONS.md

| ADR | Line |
|-----|------|
| ADR-0004 | 33 |
| ADR-0001 | 131 |
| ADR-0002 | 217 |
| ADR-0003 | 274 |

ADR-0004 prepended above ADR-0001 per the file's "New entries are prepended at the top of this file" discipline.

### Discovered register-inventory discrepancies for Plan 03 planning

**None.** The plan's `<interfaces>` register table matched the psx-spx primary source on every offset. The Plan 01 SUMMARY's note that the chassis reserves `int16_t reg_values[35]` is consistent with the enum count this plan establishes; Plan 03 can index `state->reg_values[reg]` directly.

## Task Commits

1. **Task 1 (TDD RED): failing tests for register identity surface** — `7072bee` (test)
2. **Task 1 (TDD GREEN): register identity surface — enum + hw_offset + name + snapshot stub** — `243fc20` (feat)
3. **Task 2 (TDD RED): failing tests for q15 error tap + spu94_tick stub** — `4e01604` (test)
4. **Task 2 (TDD GREEN): q15 error tap + spu94_tick public stub; drop spu94_placeholder.c** — `9b2c827` (feat)
5. **Task 3: ADR-0004 — extensibility taps (q15 error tap + spu94_tick)** — `9ef4827` (docs)

**Plan metadata commit:** _added next, includes SUMMARY.md + STATE.md + ROADMAP.md + REQUIREMENTS.md_

## Decisions Made

- **Enum ordering pinned to the plan's proposed order** (no rearrangement). Routing/base first; reverb block in ascending hardware-offset order. Justification documented inline in the enum's leading comment so future readers see the intent.
- **`spu94_reg_name` returns NULL** for out-of-range ids (not empty string). Header documents this; tests pin it. Rationale: NULL is unambiguous for callers using `if (name) printf("%s", name);` patterns; an empty string would silently print nothing.
- **Pre-saturation remainder** for `q15_mul_truncate_with_err`. Documented in header; tested explicitly for the `INT16_MIN^2` saturation edge case; recorded in ADR-0004 with the alternative (post-saturation) noted as a `revision path`. Rationale: the Error Accumulator concept (RESEARCH.md) needs the *truncation* discard, not the *saturation* discard — the latter is recoverable from the difference between `shifted` and `INT16_MAX`.
- **Single typedef home for `spu94_state`.** Forward declaration lives in `spu94_registers.h`; `spu94.h` includes it and does not re-typedef. Required to satisfy `-std=c99 -pedantic` (duplicate typedefs of the same name are C11+ only). API-07 surface preserved.
- **`spu94_placeholder.c` removed in this plan**, not deferred. The three real TUs (state, registers, tick) plus the documented removal note ("Phase 2 will replace with a real version macro" was the placeholder's own roadmap) made keeping it pure scaffold debt.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking issue] Circular include between spu94.h and spu94_registers.h**
- **Found during:** Task 1 GREEN build
- **Issue:** The plan instructed `spu94_registers.h` to `#include <spu94/spu94.h>` (for the `spu94_state` typedef and `spu94_result_t`), and *also* instructed `spu94.h` to `#include <spu94/spu94_registers.h>`. That is a circular include — `spu94.h` includes `spu94_registers.h`, which itself includes `spu94.h`, but the `spu94_state` typedef in `spu94.h` lives *after* the new `#include <spu94/spu94_registers.h>` line, so `spu94_registers.h` sees an `unknown type name 'spu94_state'` when compiled standalone (or transitively). Plan 01's TUs that include `spu94.h` (and now transitively `spu94_registers.h`) failed to compile.
- **Fix:** Replaced the `#include <spu94/spu94.h>` in `spu94_registers.h` with a direct forward declaration `typedef struct spu94_state spu94_state;`. Kept `spu94_result_t` out of the registers header (Plan 02 doesn't need it; Plan 03 will pull it in via the umbrella). This breaks the cycle cleanly.
- **Files modified:** `include/spu94/spu94_registers.h` (replaced umbrella include with forward decl).
- **Verification:** `cmake --build build` clean; library links; all symbols present.
- **Committed in:** `243fc20` (Task 1 GREEN commit) — the fix and the new header land together so the architectural choice is auditable in one diff.

**2. [Rule 3 — Blocking issue] Duplicate typedef of `spu94_state` breaks -std=c99 -pedantic (API-07)**
- **Found during:** Task 1 GREEN build (same iteration as Deviation 1; surfaced when the C99 consumer compile-test was rebuilt)
- **Issue:** With Deviation 1's forward declaration in `spu94_registers.h`, the original `typedef struct spu94_state spu94_state;` in `spu94.h` becomes a *second* typedef of the same name. C11 §6.7/3 allows redundant typedefs of identical types; C99 does not. The `api_c99_consumer` target — explicitly compiled with `-std=c99 -pedantic -Werror` per Plan 01's API-07 surface — failed with `error: redefinition of typedef 'spu94_state' [-Werror=pedantic]`.
- **Fix:** Removed the duplicate `typedef struct spu94_state spu94_state;` line from `include/spu94/spu94.h`. Replaced with a comment block explaining that the forward declaration now lives in `spu94_registers.h` (which is `#include`d above), and that this single-typedef-home discipline is required to keep the umbrella header C99-pedantic clean. The umbrella header still exposes the type name to callers (transitively via the `spu94_registers.h` include) — no public API change.
- **Files modified:** `include/spu94/spu94.h` (removed duplicate typedef + added explanatory comment).
- **Verification:** `api_c99_consumer` and `api_cxx_consumer` ctests both pass; `state_lifecycle` test (which still includes only `spu94/spu94.h`) compiles unchanged.
- **Committed in:** `243fc20` (same Task 1 GREEN commit as Deviation 1) — both fixes stem from the same architectural decision (where `spu94_state`'s typedef lives) so they're committed together.

**3. [Rule 1 — Bug] test_register_identity.c missing `<stdalign.h>` and `<stdio.h>`**
- **Found during:** Task 1 GREEN build (after Deviations 1 + 2 cleared)
- **Issue:** The new test TU used `alignas(SPU94_STATE_ALIGN_MAX)` and `snprintf`, but only included `<spu94/spu94.h>`, `<spu94/spu94_registers.h>`, `<stdint.h>`, `<stddef.h>`, `<string.h>`. Build failed with `implicit declaration of function 'alignas'` and `expected ';' before 'unsigned'`. (The state-lifecycle test from Plan 01 includes `<stdalign.h>` for the same reason; I missed it in the new TU.)
- **Fix:** Added `<stdalign.h>` and `<stdio.h>` to the new test's includes.
- **Files modified:** `tests/unit/registers/test_register_identity.c`.
- **Verification:** `register_identity_unit` ctest passes; all 9 sub-tests green.
- **Committed in:** `243fc20` (Task 1 GREEN commit).

### Architectural notes (not deviations, but worth flagging for Plans 03+)

- The single-typedef-home discipline (Deviation 2) is a Phase 2-wide invariant now: any new public sub-header must either forward-declare the opaque types it uses *or* include the umbrella header inside an `#include` guard that prevents re-entry. Plan 03's policy-table header (and any header it adds) must respect this.
- `spu94_registers.h` deliberately does NOT include the umbrella header. This is a one-way include relationship: umbrella includes sub-headers, never the reverse. Future sub-headers should follow the same rule.

---

**Total deviations:** 3 auto-fixed (1× Rule 3 architectural-mechanical, 1× Rule 3 C99 conformance, 1× Rule 1 missing include). All landed in the Task 1 GREEN commit so the architectural relationship is auditable in one diff.
**Impact on plan:** No scope creep. The two Rule 3 fixes are essentially planner oversight on the include topology; the Rule 1 fix is a missed include. None changed the public API contract or the acceptance behavior; all enabled the plan's own success criteria.

## Issues Encountered

- **Planner acceptance-criteria grep counts off-by-one in two places.** The criterion `grep -c '    SPU94_REG_' include/spu94/spu94_registers.h returns 35 (every enum entry, excluding the sentinel line)` actually returns 36 because the sentinel `SPU94_REG__COUNT` line also matches. The substantive intent (35 enum entries) is met; the regex needed `[a-z]` after `SPU94_REG_` to exclude the sentinel. Similarly the `grep -c '\[SPU94_REG_' src/spu94/spu94_registers.c returns 70` criterion actually returns 73 (70 designated-initializer entries + 3 references in code/comments). Tightened greps in the SUMMARY metadata-commit acceptance pass; planner intent honored.
- **Planner status-line regex assumed plain `Status: Accepted (...)` but the actual ADR uses `**Status:** Accepted (...)`** (bold). The substantive content matches; the regex needed to escape the `**` markdown. Already-existing ADRs (0001-0003) follow the same `**Status:**` convention; ADR-0004 was correctly modeled on them.

Both issues are reporting-side, not implementation-side. No code changes needed.

## Known Stubs

| Stub | File | Reason | Resolved by |
|------|------|--------|-------------|
| `spu94_snapshot_registers` zero-fills `out` (does not read register state) | `src/spu94/spu94_registers.c:101-110` | Plan 02 lands the declaration + parameter contract; Plan 03 will wire it to read `state->reg_values[]` (the Plan 01 chassis already reserves the storage slot). Header documents the stub status explicitly. | Plan 03 (per CONTEXT D-20) |
| `spu94_tick` is an empty function body | `src/spu94/spu94_tick.c:21-24` | Plan 02 commits the public symbol so downstream consumers (Controllers, Error Accumulator) can compile-link against the contract. The reverb algorithm fills it in over Phase 3; Plan 03 routes apply_pending_writes through it; Plan 04 routes spu94_buffer_advance through it. | Phase 3 (algorithm); Plan 03 (apply_pending_writes); Plan 04 (buffer_advance). All disclosed in the function's leading doc comment and in ADR-0004. |

Both stubs are intentional, plan-disclosed, and explicitly documented in their own header/source comments. They are NOT silent stubs that prevent the plan from achieving its stated goal — Plan 02's goal is to land the *identity surface*, not the *behavior*.

## User Setup Required

None — `user_setup: []` in the plan's frontmatter. No external services, env vars, or new tooling introduced. Existing CI gates (grep-guard, verify-no-heap, ctest) cover the new code.

## Next Phase Readiness

- **Plan 03 (per-register read/write API):** Can immediately key the engine layer on `spu94_reg_t` (sequential 0..34 + `_COUNT`). Storage already exists in `state->reg_values[35]` (Plan 01) and `state->pending_values[35]` + `state->pending_mask` (Plan 01). Snapshot stub will lift to a real read of `reg_values[]`.
- **Plan 04 (buffer arithmetic + mBASE snap-on-write):** Will dispatch on `reg == SPU94_REG_mBASE`. The enum entry exists; the hardware offset (0x1DA2) is in the table; ADR-0006 (snap-on-write) is still owed by Plan 04 Task 2.
- **Plan 05 (per-register test battery + Python fuzz):** Can iterate `for (int i = 0; i < (int)SPU94_REG__COUNT; ++i)` directly. Per-register name available for failure messages via `spu94_reg_name((spu94_reg_t)i)`.
- **Phase 3 (reverb algorithm):** `q15_mul_truncate_with_err` is available for the per-multiply error observation that the Error Accumulator concept depends on. `spu94_tick` is the entry point; algorithm fills the body.
- **No blockers.**

## Self-Check: PASSED

Verified after summary write:
- FOUND: `include/spu94/spu94_registers.h` (created)
- FOUND: `src/spu94/spu94_registers.c` (created)
- FOUND: `src/spu94/spu94_tick.c` (created)
- FOUND: `tests/unit/registers/CMakeLists.txt` (created)
- FOUND: `tests/unit/registers/test_register_identity.c` (created)
- FOUND: `include/spu94/spu94.h` (modified)
- FOUND: `include/spu94/spu94_q15.h` (modified)
- FOUND: `src/spu94/CMakeLists.txt` (modified)
- FOUND: `tests/unit/CMakeLists.txt` (modified)
- FOUND: `tests/unit/q15/test_q15.c` (modified)
- FOUND: `docs/DECISIONS.md` (modified — ADR-0004 at line 33)
- CONFIRMED REMOVED: `src/spu94/spu94_placeholder.c`
- FOUND commit: `7072bee` (Task 1 RED)
- FOUND commit: `243fc20` (Task 1 GREEN)
- FOUND commit: `4e01604` (Task 2 RED)
- FOUND commit: `9b2c827` (Task 2 GREEN)
- FOUND commit: `9ef4827` (Task 3 ADR)

---
*Phase: 02-buffer-register-infrastructure*
*Completed: 2026-04-19*
