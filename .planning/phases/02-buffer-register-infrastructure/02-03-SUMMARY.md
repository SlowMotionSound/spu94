---
phase: 02-buffer-register-infrastructure
plan: 03
subsystem: api
tags: [register-io, write-policy, tick-latched, immediate, facade-layer, adr-0005]

# Dependency graph
requires:
  - phase: 01-foundation-fixed-point-math-build-infrastructure
    provides: spu94_obj OBJECT library, spu94_warnings INTERFACE flags, Unity test harness, grep-guard + verify-no-heap CI gates
  - plan: 02-01
    provides: opaque spu94_state typedef, lifecycle API, reg_values[35] + pending_values[35] + pending_mask storage already reserved
  - plan: 02-02
    provides: spu94_reg_t enum (35 + sentinel), spu94_reg_hw_offset/name accessors, spu94_snapshot_registers stub, spu94_tick stub
provides:
  - spu94_reg_type_t enum (I16/U16) + spu94_reg_type(reg) classifier
  - spu94_write_policy_t enum (IMMEDIATE/TICK_LATCHED) — the D-05 seam type
  - Engine layer (6 typed accessors) - spu94_set_reg_i16/u16, spu94_get_reg_i16/u16, spu94_get_reg_i16/u16_pending — with TYPE_MISMATCH/UNKNOWN_REG/NULL guards
  - 35-entry spu94_write_policy_table[] in src/spu94/spu94_write_policy.c (13 IMMEDIATE + 22 TICK_LATCHED)
  - spu94_apply_pending_writes(state) — pending-shadow flush; called from EXACTLY one call site (Pitfall 4)
  - spu94_tick body now flushes pending writes at tick start
  - spu94_mbase_on_write Plan-03 stub (Plan 04 lifts to snap-on-write per ADR-0006)
  - spu94_register_facade.h — 105 hand-written static inline wrappers (35 setters + 35 getters + 35 pending-getters)
  - spu94_snapshot_registers wired to read state->reg_values[] (no longer Plan-02 zero-fill stub)
  - Internal-only src/spu94/spu94_state_internal.h — single ODR home for struct spu94_state
  - ADR-0005 in docs/DECISIONS.md (line 33, prepended above ADR-0004)
affects: [02-04, 02-05, 03-reverb-algorithm, 05-public-api, 06-python-bindings]

# Tech tracking
tech-stack:
  added:
    - "Internal cross-TU header pattern (src/spu94/spu94_state_internal.h) — never installed, never under include/spu94/"
    - "Designated-initializer 35-entry policy table as runtime-replaceable seam (D-05)"
    - "64-bit packed signedness mask (one bit per spu94_reg_t) for runtime type validation"
  patterns:
    - "Engine + facade two-layer API (D-01, D-03) — engine is the linker surface; facade is static-inline-only"
    - "Pending-shadow + bitmask flush (Pitfall 4: exactly-one-call-site rule)"
    - "Stub-symbol-now / real-body-later for cross-plan integration (spu94_mbase_on_write)"
    - "Internal-header-with-_Static_assert-on-sizeof for ODR-safe struct sharing"

key-files:
  created:
    - "include/spu94/spu94_register_facade.h"
    - "src/spu94/spu94_register_io.c"
    - "src/spu94/spu94_write_policy.c"
    - "src/spu94/spu94_pending.c"
    - "src/spu94/spu94_state_internal.h"
    - "tests/unit/registers/test_register_io.c"
    - "tests/unit/registers/test_register_facade.c"
  modified:
    - "include/spu94/spu94.h (spu94_result_t reordered above spu94_registers.h include; spu94_register_facade.h added to umbrella)"
    - "include/spu94/spu94_registers.h (spu94_reg_type_t / spu94_write_policy_t enums + 6 engine accessors + spu94_reg_type/spu94_apply_pending_writes declarations)"
    - "src/spu94/spu94_registers.c (spu94_snapshot_registers reads real reg_values[])"
    - "src/spu94/spu94_state.c (drops local struct definition; includes spu94_state_internal.h)"
    - "src/spu94/spu94_tick.c (flushes pending writes at tick start)"
    - "src/spu94/CMakeLists.txt (3 new sources: register_io, write_policy, pending)"
    - "tests/unit/registers/CMakeLists.txt (2 new test executables)"
    - "docs/DECISIONS.md (ADR-0005 prepended at line 33)"

key-decisions:
  - "spu94_result_t enum declaration moved from below spu94_registers.h include to above — required so the engine setter signatures in spu94_registers.h can return the type without circular umbrella include"
  - "Internal-only header src/spu94/spu94_state_internal.h holds struct spu94_state and the sizeof _Static_assert; alignof guard stays in spu94_state.c"
  - "spu94_mbase_on_write defined as a no-op stub in spu94_write_policy.c (NOT in a public header) so the library links cleanly while Plan 04 owns the snap-on-write body"
  - "Forward declaration of spu94_mbase_on_write before its definition satisfies -Werror=missing-prototypes without leaking the symbol into a public header"
  - "spu94_reg_type out-of-range returns SPU94_REG_TYPE_I16 (arbitrary); set_reg_*'s SPU94_UNKNOWN_REG is the proper out-of-range channel"
  - "IMMEDIATE writes mirror new value into pending_values[] AND clear the pending_mask bit — keeps spu94_get_reg_*_pending consistent for IMMEDIATE-policy registers (D-06 contract)"
  - "Sequential bit-iteration (no __builtin_ctzll) in spu94_apply_pending_writes — at most 35 bits, branch-predictable, portable to clang-on-Windows without intrin shims"

requirements-completed:
  - CORE-04
  - CORE-10
  - API-04

# Metrics
duration: 11m 44s
completed: 2026-04-19
---

# Phase 2 Plan 03: Register Read/Write API + Write-Timing Policy Summary

**Engine-layer typed register accessors with signedness validation, the 35-entry write-policy table as the D-05 swappable seam, the pending-shadow + tick-flush plumbing that honors Pitfall 4's exactly-one-call-site rule, the 105-wrapper hand-written facade header, and ADR-0005 documenting the split write-timing policy — all on a chassis whose linker surface stays heap-free and whose public headers stay C99-pedantic + C++-pedantic clean.**

## Performance

- **Duration:** ~11 min 44 s (autonomous executor; 4 tasks; 2 architectural fixes auto-applied during build)
- **Started:** 2026-04-19T20:05:45Z
- **Completed:** 2026-04-19T20:17:29Z
- **Tasks:** 4 (Tasks 1+2 share an internal-header refactor and were committed together as a single GREEN; Task 3 TDD; Task 4 ADR)
- **Commits:** 5 (RED, GREEN-Tasks-1-and-2, RED-Task-3, GREEN-Task-3, ADR)
- **Files created:** 7 — modified: 8

## Accomplishments

- **Engine layer (Task 1):** `include/spu94/spu94_registers.h` extended with the `spu94_reg_type_t` and `spu94_write_policy_t` enums, the `spu94_reg_type(reg)` classifier, and 6 typed accessors (`spu94_set_reg_i16/u16`, `spu94_get_reg_i16/u16`, `spu94_get_reg_i16/u16_pending`). Implementation in the new `src/spu94/spu94_register_io.c` with NULL-state, out-of-range, and TYPE_MISMATCH guards on every entry point — without mutating active or pending state on a rejected call (D-08).
- **Internal-header refactor (Task 1):** `struct spu94_state` moved out of `src/spu94/spu94_state.c` into a new internal header `src/spu94/spu94_state_internal.h` (NOT under `include/spu94/`) so every Phase-2 internal TU shares one ODR-safe definition. The `_Static_assert(sizeof <= SPU94_STATE_SIZE_MAX)` guard moves with it; the `alignof` guard stays in `spu94_state.c` next to the lifecycle code.
- **Snapshot wiring (Task 1):** `spu94_snapshot_registers` now reads `state->reg_values[]` instead of the Plan-02 zero-fill stub; NULL-out + NULL-state edge cases preserved.
- **Policy table (Task 2):** `src/spu94/spu94_write_policy.c` ships the 35-entry `spu94_write_policy_table[SPU94_REG__COUNT]` pinned to the PS1-faithful split — 13 IMMEDIATE + 22 TICK_LATCHED, audited via grep counts. The `spu94_mbase_on_write` Plan-03 stub also lives here so the library links cleanly while Plan 04 lifts the body.
- **Pending flush (Task 2):** `src/spu94/spu94_pending.c` implements `spu94_apply_pending_writes` — sequential 35-bit scan, branch-predictable, portable. `src/spu94/spu94_tick.c`'s body now calls it at tick start. Pitfall 4 enforced: only `spu94_pending.c` (definition) and `spu94_tick.c` (call site) reference the symbol — verified by `grep -lrn 'spu94_apply_pending_writes(' src/spu94/`.
- **Facade layer (Task 3):** `include/spu94/spu94_register_facade.h` — 105 hand-written `static inline` wrappers, three per register (set / get / get_pending). The umbrella header pulls them in. Zero linker-surface cost: `nm` shows no `spu94_set_vIIR` symbol — wrappers compile away.
- **ADR-0005 (Task 4):** Prepended at line 33 of `docs/DECISIONS.md`, above ADR-0004. Documents the structural argument, the 35-row split, the seam structure, the pending-readback contract, the Pitfall 4 protection, the test obligation owed by Plan 05, and three revision paths. Paraphrase discipline honored.
- **Test surface:** 2 new Unity TUs (12 sub-tests in `test_register_io.c`, 7 in `test_register_facade.c`); CTest now runs 7/7 green (`q15_unit`, `state_lifecycle`, `register_identity_unit`, `register_io_unit`, `register_facade_unit`, `api_c99_consumer`, `api_cxx_consumer`).
- **CI invariants green:** `grep-guard.sh` (11 core files now), `verify-no-heap-symbols.sh build/src/spu94/libspu94.so`. Library exposes 17 T-symbols; the policy table sits in `.rodata` (`R spu94_write_policy_table`).

## Specific Numbers (per `<output>` requirements)

### Final per-register policy table (35 rows for future audit)

| # | Enum | Policy | Type |
|---|------|--------|------|
| 0 | SPU94_REG_vLOUT | IMMEDIATE | i16 |
| 1 | SPU94_REG_vROUT | IMMEDIATE | i16 |
| 2 | SPU94_REG_mBASE | IMMEDIATE (+ snap side-effect) | u16 |
| 3 | SPU94_REG_dAPF1 | TICK_LATCHED | u16 |
| 4 | SPU94_REG_dAPF2 | TICK_LATCHED | u16 |
| 5 | SPU94_REG_vIIR | IMMEDIATE | i16 |
| 6 | SPU94_REG_vCOMB1 | IMMEDIATE | i16 |
| 7 | SPU94_REG_vCOMB2 | IMMEDIATE | i16 |
| 8 | SPU94_REG_vCOMB3 | IMMEDIATE | i16 |
| 9 | SPU94_REG_vCOMB4 | IMMEDIATE | i16 |
| 10 | SPU94_REG_vWALL | IMMEDIATE | i16 |
| 11 | SPU94_REG_vAPF1 | IMMEDIATE | i16 |
| 12 | SPU94_REG_vAPF2 | IMMEDIATE | i16 |
| 13 | SPU94_REG_mLSAME | TICK_LATCHED | u16 |
| 14 | SPU94_REG_mRSAME | TICK_LATCHED | u16 |
| 15 | SPU94_REG_mLCOMB1 | TICK_LATCHED | u16 |
| 16 | SPU94_REG_mRCOMB1 | TICK_LATCHED | u16 |
| 17 | SPU94_REG_mLCOMB2 | TICK_LATCHED | u16 |
| 18 | SPU94_REG_mRCOMB2 | TICK_LATCHED | u16 |
| 19 | SPU94_REG_dLSAME | TICK_LATCHED | u16 |
| 20 | SPU94_REG_dRSAME | TICK_LATCHED | u16 |
| 21 | SPU94_REG_mLDIFF | TICK_LATCHED | u16 |
| 22 | SPU94_REG_mRDIFF | TICK_LATCHED | u16 |
| 23 | SPU94_REG_mLCOMB3 | TICK_LATCHED | u16 |
| 24 | SPU94_REG_mRCOMB3 | TICK_LATCHED | u16 |
| 25 | SPU94_REG_mLCOMB4 | TICK_LATCHED | u16 |
| 26 | SPU94_REG_mRCOMB4 | TICK_LATCHED | u16 |
| 27 | SPU94_REG_dLDIFF | TICK_LATCHED | u16 |
| 28 | SPU94_REG_dRDIFF | TICK_LATCHED | u16 |
| 29 | SPU94_REG_mLAPF1 | TICK_LATCHED | u16 |
| 30 | SPU94_REG_mRAPF1 | TICK_LATCHED | u16 |
| 31 | SPU94_REG_mLAPF2 | TICK_LATCHED | u16 |
| 32 | SPU94_REG_mRAPF2 | TICK_LATCHED | u16 |
| 33 | SPU94_REG_vLIN | IMMEDIATE | i16 |
| 34 | SPU94_REG_vRIN | IMMEDIATE | i16 |

**Totals:** 13 IMMEDIATE + 22 TICK_LATCHED = 35. **i16:** 12 (vLOUT, vROUT, vIIR, vCOMB1..4, vWALL, vAPF1, vAPF2, vLIN, vRIN). **u16:** 23 (mBASE + 22 d-prefix/m-prefix).

### Final `sizeof(struct spu94_state)` after Plan 03

**168 bytes** — unchanged from end of Plan 01 (Plan 03 used the reserved fields, did not add new ones). The `_Static_assert` continues to confirm `168 <= SPU94_STATE_SIZE_MAX (16384)`. Headroom remaining: 16216 bytes — Plans 04 and 05 have generous slack.

### TYPE_MISMATCH behavior nuances discovered

- **Wrong-typed setter is a pure no-op on data, even on edge values.** `spu94_set_reg_i16(s, SPU94_REG_mBASE, INT16_MIN)` returns `SPU94_TYPE_MISMATCH` without writing INT16_MIN-as-u16 (which would be 0x8000) into either active or pending storage. Same for `spu94_set_reg_u16(s, SPU94_REG_vIIR, 0xFFFFu)` — the would-be `int16_t -1` value is rejected, not silently stored.
- **NULL state and out-of-range share a single result code (`SPU94_UNKNOWN_REG`) on setters.** Distinguishing the two would force callers to check NULL up front (which they should anyway). The shared code keeps the contract minimal; tests in `test_register_io.c` exercise both paths.
- **Wrong-typed setter does NOT clear a previously-set pending value.** Confirmed by reading the engine setter code paths: the type check returns early before any pending_mask manipulation. So a stale pending value from an earlier (correctly typed) write survives a rejected wrong-typed write — the rejected call is purely transparent to subsequent ticks.
- **Out-of-range getter returns 0 (not `(int16_t)0xFFFF` or any other sentinel).** Callers cannot distinguish "register has value 0" from "out-of-range reg id" via the getter alone; pair with `spu94_reg_type(reg)` if the distinction matters.

### Facade-header line count

**203 lines** for `include/spu94/spu94_register_facade.h`. Composition: ~30 lines of doc-comment header + 105 wrapper definitions (one line each, three blank-separator lines per register) + extern-C scaffolding + include guards.

### Symbol surface confirmation

- `nm build/src/spu94/libspu94.so | grep ' T spu94_'` returns 17 text symbols.
- `nm build/src/spu94/libspu94.so | grep ' R spu94_write_policy_table'` shows the table in `.rodata`.
- `nm build/src/spu94/libspu94.so | grep ' T spu94_set_vIIR$'` returns EMPTY — facade wrappers are `static inline`, no linker symbol.

## Task Commits

1. **Task 1 (TDD RED): failing tests for engine-layer register I/O** — `dff4163` (test)
2. **Task 1 + Task 2 (GREEN): engine layer + write-policy table + tick flush + internal-header refactor** — `dd6e696` (feat)
3. **Task 3 (TDD RED): failing tests for register facade wrappers** — `3488fa3` (test)
4. **Task 3 (GREEN): 35-register facade header (105 static inline wrappers)** — `da4ec5a` (feat)
5. **Task 4: ADR-0005 — split write-timing policy + swappable table** — `b80a589` (docs)

**Plan metadata commit:** _added next, includes SUMMARY.md + STATE.md + ROADMAP.md + REQUIREMENTS.md_

**Note on Tasks 1+2 GREEN bundling.** The plan's executor-note says "If context pressure is felt before Task 4, commit the code changes from Tasks 1-3 and spawn a fresh executor for Task 4". Tasks 1 and 2 are mechanically inseparable — the engine setters in spu94_register_io.c reference symbols defined in spu94_write_policy.c (the policy table) and spu94_pending.c (apply function), so an independent Task 1 commit would leave the build in an unlinkable state. They were committed together as a single GREEN landing; the RED commit for Task 1 still stands separately so the test-first discipline is auditable. Task 3 was then committed as its own RED+GREEN pair. Task 4 (ADR) committed atomically.

## Decisions Made

- **spu94_result_t enum reordered above spu94_registers.h include in spu94.h.** The engine setter signatures declared in `spu94_registers.h` return `spu94_result_t`. Without the reorder, `spu94_registers.h` would either need to `#include <spu94/spu94.h>` (the circular include explicitly forbidden by Plan 02's architectural note) or duplicate-typedef the enum. The reorder is a 30-line pure relocation with no API change. Documented in spu94.h with an explanatory comment.
- **Internal cross-TU header in src/, not include/.** `src/spu94/spu94_state_internal.h` is reachable only from sources in `src/spu94/`. CMake does not install it; `include/spu94/` stays purely public-API. This preserves the opaque-handle contract (D-12) — public callers still see only the forward declaration in `spu94_registers.h`.
- **spu94_mbase_on_write Plan-03 stub stays in spu94_write_policy.c (not a separate file).** Plan 04's `<output>` flags that the body may move to `src/spu94/spu94_buffer.c` if that better isolates the snap-on-write logic with the buffer-arithmetic surface. Plan 03 deliberately doesn't pre-commit a location for Plan 04's choice; the stub is co-located with the policy table because that's where the IMMEDIATE-with-side-effect dispatch decision is made. Plan 04 can move the definition without touching engine code.
- **IMMEDIATE writes mirror to pending_values[].** D-06 says the `_pending` accessor returns "what will be applied at the next tick". For IMMEDIATE-policy registers, "what will be applied" is the same as "what is already applied" — so the engine writes the new value to BOTH active and pending storage and clears the mask bit. Without this mirror, `spu94_get_reg_*_pending` on an IMMEDIATE register would return whatever stale value was last written through the TICK_LATCHED path (in practice 0, but the contract should not depend on init-zeroing).
- **Facade-header static inline returns the result code from the engine setter.** Some facade designs swallow the return code for ergonomics; SPU-94 propagates it because Plan 05's per-register test battery and Phase 6's Python bindings both want to assert on the SPU94_OK / SPU94_TYPE_MISMATCH outcomes from facade calls. The runtime cost is zero (the wrapper inlines).

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 — Blocking issue] C-comment-glob conflict: literal `d*/m*` in `/* ... */` comments was parsed as comment-end + glob.**
- **Found during:** First Task-1 GREEN build.
- **Issue:** Several source files (and the test TU) carried the human-readable shorthand `d*/m*` for "delay/address registers (d-prefix and m-prefix)" inside C-style block comments. The `*/` token closed the comment early, leaving `m* ...rest...` to be parsed as code. Compiler then errored on "unknown type name 'm'" and similar. The plan itself uses `d*/m*` heavily in its prose; lifting any of that prose verbatim into source comments triggers the issue.
- **Fix:** Reworded every occurrence in source comments (3 source files + 2 test files) to use either "d-prefix/m-prefix" or "d-prefix and m-prefix delay/address registers". The plan's prose is not modified — it's documentation, not C code.
- **Files modified:** `include/spu94/spu94_registers.h`, `src/spu94/spu94_register_io.c`, `src/spu94/spu94_write_policy.c`, `tests/unit/registers/test_register_io.c`, `tests/unit/registers/test_register_facade.c`.
- **Verification:** Clean build; ctest 7/7 green.
- **Committed in:** `dd6e696` (Task 1+2 GREEN) and `da4ec5a` (Task 3 GREEN) — fixed in each file as that file landed.
- **Worth flagging for Plans 04+:** The phase-context `02-CONTEXT.md` and the plans use this shorthand frequently. Any source comment that paraphrases context language must transliterate `d*/m*` to a comment-safe spelling. A future grep-guard rule (e.g., warn on `d\*/m\*` in `.c`/`.h` files) could catch this mechanically.

**2. [Rule 3 — Blocking issue] Circular type dependency: spu94_registers.h needs spu94_result_t but spu94.h declares it after the registers include.**
- **Found during:** First Task-1 GREEN build.
- **Issue:** Plan 03 added engine setter declarations (`spu94_set_reg_i16/u16` returning `spu94_result_t`) to `include/spu94/spu94_registers.h`. The umbrella `spu94.h` declared `spu94_result_t` AFTER `#include <spu94/spu94_registers.h>` — so when the umbrella was processed, the registers header saw an undefined `spu94_result_t`. Plan 02's architectural note explicitly forbids `spu94_registers.h` from `#include`-ing `spu94.h` (the circular include the forward declaration was meant to avoid).
- **Fix:** Moved the `spu94_result_t` typedef in `spu94.h` to a position above the `#include <spu94/spu94_registers.h>` line. Replaced the original definition site with an explanatory comment block. No public API change — the type is still in the same translation unit; only its source-line position moved.
- **Files modified:** `include/spu94/spu94.h`.
- **Verification:** Clean build; api_c99_consumer + api_cxx_consumer tests still pass (the consumers see the same enum at the same point).
- **Committed in:** `dd6e696` (Task 1+2 GREEN) — surfaced and resolved together with the rest of the engine-layer landing.
- **Worth flagging for Plans 04+:** The single-typedef-home discipline established in Plan 02 (forward-decl in sub-headers; full decl in one umbrella spot) needs a corollary: any TYPE that a sub-header's signatures reference must also have a single home, declared before the sub-header is included. Plan 03's `spu94_write_policy_t` and `spu94_reg_type_t` enums were placed directly in `spu94_registers.h` to avoid the same trap; future plans should follow that pattern (declare consumer-facing enums in the sub-header that owns them, not in the umbrella).

**3. [Rule 1 — Bug] -Werror=missing-prototypes on spu94_mbase_on_write.**
- **Found during:** Task 2 build.
- **Issue:** `spu94_mbase_on_write` is defined in `spu94_write_policy.c` but has no public-header declaration (intentional — it's an internal symbol). Phase 1's strict warning set includes `-Werror=missing-prototypes` which fires on any non-static function definition without a prior prototype.
- **Fix:** Added an explicit forward declaration immediately above the definition in `spu94_write_policy.c`. The forward decl is internal to the TU; no public-header impact. The same forward declaration also exists in `spu94_register_io.c` (the only caller).
- **Files modified:** `src/spu94/spu94_write_policy.c`.
- **Verification:** Build clean; ctest 7/7.
- **Committed in:** `dd6e696` (Task 1+2 GREEN).

**4. [Rule 1 — Bug] grep-guard hit: "double" in spu94_pending.c doc comment.**
- **Found during:** Post-Task-2 CI gate run.
- **Issue:** The Pitfall 4 doc comment in `spu94_pending.c` contained the phrase "risks double-applying the same pending value", and `grep-guard.sh` matches the literal token `double` regardless of context.
- **Fix:** Reworded to "risks re-applying the same pending value twice".
- **Files modified:** `src/spu94/spu94_pending.c`.
- **Verification:** `bash scripts/ci/grep-guard.sh` exits 0.
- **Committed in:** `dd6e696` (Task 1+2 GREEN) — caught and fixed before the commit landed.

---

**Total deviations:** 4 auto-fixed (3× Rule 3 architectural-mechanical; 1× Rule 1 bug; 0× Rule 4 architectural). All landed in the bundled Task 1+2 GREEN commit so the architectural relationships are auditable in one diff. **No scope creep.** All four fixes were essential corrections to make Plan 03's stated acceptance criteria pass.

## Issues Encountered

- **Plan acceptance-criteria grep counts are partly intent-only.** The `grep -o 'spu94_set_[a-zA-Z0-9_]*' include/spu94/spu94_register_facade.h | sort -u | wc -l returns 35` criterion actually returns 38 because the regex matches `spu94_set_reg_i16` and `spu94_set_reg_u16` (the engine-layer functions referenced inside the facade-wrapper bodies) in addition to the 35 facade names. The substantive intent (35 unique facade-wrapper names) is met; the regex needed an exclusion for `_reg_`. Same for the pending-getter grep (returns 37 = 35 facade pending getters + `spu94_get_reg_i16_pending` + `spu94_get_reg_u16_pending`). Reporting-side, not implementation-side. No code changes needed.
- **Plan acceptance-criteria grep `grep -c '_Static_assert(sizeof(struct spu94_state)'` chained with `&&` exits early on grep returning 0 lines** — a compound shell pipeline bailed at the first `0` count even though the count was correct (the assert moved out of `spu94_state.c` into the new internal header, by design). Verified separately with explicit echo statements. Reporting issue, not a real failure.

Both issues are reporting-side. No code changes needed.

## Known Stubs

| Stub | File | Reason | Resolved by |
|------|------|--------|-------------|
| `spu94_mbase_on_write` is an empty function body | `src/spu94/spu94_write_policy.c:91-95` | Plan 03 lands the symbol so `spu94_register_io.c` links cleanly with the IMMEDIATE-with-side-effect dispatch already in place. Plan 04 lifts the body to `state->buffer_address = (uint32_t)new_mbase;` per ADR-0006 (snap-on-write). The function's lead comment documents the stub status. | Plan 04 (CONTEXT D-09/D-11; ADR-0006) |

This stub is intentional, plan-disclosed in 02-03-PLAN.md Task 2's `<action>` block, ADR-0005's Decision section, and the function's own header comment. It does NOT prevent Plan 03's stated goal — Plan 03's goal is to land the routing surface; Plan 04 lands the buffer-address snap behavior.

## User Setup Required

None — `user_setup: []` in the plan frontmatter. No external services, env vars, or new tooling. Existing CI gates (grep-guard, verify-no-heap, ctest) cover the new code without modification.

## Next Phase Readiness

- **Plan 04 (buffer arithmetic + mBASE snap-on-write):** All Plan 04 dispatch sites are in place. `spu94_mbase_on_write` is a one-function edit away from the snap implementation. The `state->buffer_address` field exists (Plan 01) and is already routed through `spu94_init`/`spu94_reset`. Plan 04 Task 2 also writes ADR-0006; Plan 03 left the spot above ADR-0005 in `docs/DECISIONS.md` for it.
- **Plan 05 (per-register test battery + Python fuzz):** Can now iterate `for (int r = 0; r < (int)SPU94_REG__COUNT; ++r) { ... spu94_set_reg_i16/u16(...) }` directly. The `spu94_reg_type(reg)` classifier picks the right setter; `spu94_write_policy_table[reg]` predicts the expected get/get_pending behavior; `spu94_reg_name(reg)` gives a failure message label.
- **Phase 3 (reverb algorithm):** The reverb core can read register values via `spu94_get_reg_i16/u16` (or the facade), without worrying about pending-write timing — `spu94_tick` flushes pending writes BEFORE the algorithm body runs.
- **Phase 6 (Python bindings):** `spu94_set_reg_i16/u16` is the binding target for IntEnum-keyed register access. The TYPE_MISMATCH return code surfaces as a Python exception cleanly. `spu94_snapshot_registers` gives Python a one-call atomic state dump.
- **Future Controllers milestone:** The seam is in place. Re-pointing `spu94_write_policy_table` requires defining the symbol in a translation unit that links before/instead of `spu94_write_policy.c`. ADR-0005 documents this explicitly.
- **No blockers.**

## Self-Check: PASSED

Verified after summary write:
- FOUND: `include/spu94/spu94_register_facade.h` (created)
- FOUND: `src/spu94/spu94_register_io.c` (created)
- FOUND: `src/spu94/spu94_write_policy.c` (created)
- FOUND: `src/spu94/spu94_pending.c` (created)
- FOUND: `src/spu94/spu94_state_internal.h` (created)
- FOUND: `tests/unit/registers/test_register_io.c` (created)
- FOUND: `tests/unit/registers/test_register_facade.c` (created)
- FOUND: `include/spu94/spu94.h` (modified)
- FOUND: `include/spu94/spu94_registers.h` (modified)
- FOUND: `src/spu94/spu94_registers.c` (modified)
- FOUND: `src/spu94/spu94_state.c` (modified)
- FOUND: `src/spu94/spu94_tick.c` (modified)
- FOUND: `src/spu94/CMakeLists.txt` (modified)
- FOUND: `tests/unit/registers/CMakeLists.txt` (modified)
- FOUND: `docs/DECISIONS.md` (modified — ADR-0005 at line 33)
- FOUND commit: `dff4163` (Task 1 RED)
- FOUND commit: `dd6e696` (Task 1+2 GREEN)
- FOUND commit: `3488fa3` (Task 3 RED)
- FOUND commit: `da4ec5a` (Task 3 GREEN)
- FOUND commit: `b80a589` (Task 4 ADR)

---
*Phase: 02-buffer-register-infrastructure*
*Completed: 2026-04-19*
