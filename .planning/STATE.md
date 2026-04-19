---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: planning
last_updated: "2026-04-19T21:05:16.482Z"
progress:
  total_phases: 8
  completed_phases: 2
  total_plans: 9
  completed_plans: 9
  percent: 100
---

# STATE: SPU-94

**Last updated:** 2026-04-19

## Project Reference

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 02 — COMPLETE; ready for Phase 03 (reverb algorithm)

## Current Position

Phase: 02 (buffer-register-infrastructure) — COMPLETE
Plan: 5 of 5 (all complete)

- **Milestone:** 1 (v1.0)
- **Phase:** 3
- **Plan:** Not started
- **Status:** Ready to plan
- **Progress:** [██████████] 100%

```
[██......] 2/8 phases complete (Phase 01 done; Phase 02 done — Plans 02-01..05 all green)
```

## Performance Metrics

- Phases completed: 2
- Plans completed: 9 (Phase 1: 4, Phase 2: 5)
- Requirements validated: 26 / 49 (Phase 1: 13, Plan 02-01: API-01/02/07/09, Plan 02-02: CORE-04, API-04, API-07 reaffirmed, Plan 02-03: CORE-04 reaffirmed, CORE-10 partial, API-04 reaffirmed, Plan 02-04: CORE-03, CORE-10 complete, Plan 02-05: TEST-02 + CORE-03/04/10 reaffirmed)

| Plan | Duration | Tasks | Files | Notes |
|------|----------|-------|-------|-------|
| 02-01 | ~5m 24s | 3 (1 TDD) | 13 | spu94_state chassis + verify-no-heap CI + API-07 consumer tests |
| 02-02 | ~7m 7s | 3 (2 TDD) | 11 | register identity (35-enum + tables) + q15 error tap + spu94_tick stub + ADR-0004 |
| 02-03 | ~11m 44s | 4 (2 TDD) | 15 | engine register I/O + 35-entry policy table + facade (105 wrappers) + ADR-0005 |
| 02-04 | ~6m 18s | 2 (1 TDD) | 9 | buffer arithmetic + mBASE snap-on-write + ADR-0006 + spu94_get_buffer_address |
| 02-05 | ~8m 30s | 4 (test-only) | 12 | per-register battery + buffer wrap/mBASE tests + Python 10^6 ctypes fuzz + structured q15_with_err table; ctest 15/15 |

## Accumulated Context

### Key Decisions (locked)

- Build from spec (nocash psx-spx), not by porting GPL emulators.
- Plain C99/C11 core, hand-rolled fixed-point, caller-allocated state, zero heap in hot path.
- ctypes Python bindings; scikit-build-core + cibuildwheel; dr_wav vendored for CLI WAV I/O.
- Linux primary; Cortex-M7 cross-compile smoke test as MCU-portability proof inside M1.
- 22.05 kHz internal reverb tick with 39-tap half-band FIR at both I/O boundaries (bit-faithful at the boundary, closing the lv2-psx-reverb gap).
- All 33 SPU reverb-affecting registers implemented (not 24; corrected during research).
- DECISIONS.md is a first-class M1 deliverable.
- LEVERS-CATALOG.md catalogued during M1, used by M4 (lever abstraction deferred).
- License pick (MIT vs Apache-2.0) deferred to end of M1.

### Phase 2 Plan 01 Decisions (locked)

- SPU94_STATE_SIZE_MAX = 16384u; actual sizeof(struct spu94_state) at end of Plan 01 = 168 bytes (97× headroom; `_Static_assert` pins it).
- SPU94_STATE_ALIGN_MAX = 16u (covers int64_t + future SIMD on every M1 target).
- spu94_init contract: NULL/undersized/misaligned state_buf → NULL; NULL+0 work_buf legal; NULL+nonzero work_buf → NULL.
- spu94_reset contract: zero work buffer + zero state, preserve work_buf pointer + size.
- No `<string.h>` in core; hand-rolled byte-loop zero-fill keeps libspu94.so heap-import-free (verified by both `nm -u` and `readelf -r`).
- `_Static_assert -> static_assert` aliased under `__cplusplus` in spu94_q15.h to satisfy API-07 C++ consumer compile.
- verify-no-heap-symbols.sh wired as its own CI job (matches grep-guard / clang-tidy / cppcheck / ubsan one-concern-per-job style).
- `pending_mask` is uint64 (35 bits used, 29 reserved) so `__builtin_ctzll` works on the full mask.

### Phase 2 Plan 02 Decisions (locked)

- `spu94_reg_t` enum order: vLOUT, vROUT, mBASE, then reverb block 0x1DC0..0x1DFE in ascending hardware-offset order (vLIN/vRIN at indices 33/34). `SPU94_REG__COUNT = 35` pinned by `_Static_assert` in spu94_registers.c AND in test_register_identity.c.
- `spu94_reg_name` returns the BARE name (e.g., "vIIR", not "SPU94_REG_vIIR") per CONTEXT D-17.
- Out-of-range `spu94_reg_name` returns NULL (not "" empty string); out-of-range `spu94_reg_hw_offset` returns 0xFFFF.
- `q15_mul_truncate_with_err` remainder is PRE-saturation: `INT16_MIN^2` returns result=INT16_MAX, err=0 (the saturation discard is recoverable separately). Documented in header AND in ADR-0004 with a revision-path note.
- `q15_mul_truncate` is now a one-line wrapper passing err_out=NULL — bit-identical to Phase 1; reference test table unchanged.
- `spu94_tick(spu94_state*)` lives in src/spu94/spu94_tick.c with an empty body. NULL-safe. Plans 03/04/Phase 3 fill it in.
- `spu94_state` typedef has a SINGLE home: forward-declared in spu94_registers.h. spu94.h does NOT re-typedef it (would break -std=c99 -pedantic / API-07).
- `spu94_registers.h` does NOT include `spu94/spu94.h` — only forward-declares the opaque type. Sub-headers must never include the umbrella header (one-way include rule).
- `spu94_placeholder.c` removed in this plan; src/spu94 now contains spu94_state.c + spu94_registers.c + spu94_tick.c (three real TUs).
- ADR-0004 added at line 33 of docs/DECISIONS.md (prepended above ADR-0001) — documents q15 error tap + spu94_tick as intentional public seams per D-22/D-23/D-24.

### Phase 2 Plan 03 Decisions (locked)

- Engine layer (6 typed accessors) lives in `src/spu94/spu94_register_io.c`; signedness validated at runtime via a 64-bit packed mask (23 bits set for u16 family). TYPE_MISMATCH is a pure no-op on data — does NOT clear a previously staged pending value.
- 35-entry `spu94_write_policy_table[]` in `src/spu94/spu94_write_policy.c` is the D-05 swappable seam — 13 IMMEDIATE (12 v* + mBASE) + 22 TICK_LATCHED. Pinned for SPU-94; Controllers re-points it via re-link.
- `spu94_apply_pending_writes` in `src/spu94/spu94_pending.c` is called from EXACTLY one place (first line of `spu94_tick`). Sequential 35-bit scan, branch-predictable, portable (no `__builtin_ctzll`).
- IMMEDIATE writes mirror the new value into `pending_values[]` AND clear the mask bit, so `spu94_get_reg_*_pending` always returns a meaningful value for IMMEDIATE-policy registers (D-06 contract).
- `spu94_mbase_on_write` Plan-03 stub lives in `spu94_write_policy.c` (not a public header — internal symbol). Plan 04 lifts the body to `state->buffer_address = (uint32_t)new_mbase;` per ADR-0006.
- Internal-only header `src/spu94/spu94_state_internal.h` holds the single ODR home for `struct spu94_state` + the sizeof `_Static_assert`. NEVER under `include/spu94/`. The alignof guard stays in `spu94_state.c`.
- `spu94_result_t` enum reordered above the `<spu94/spu94_registers.h>` include in `spu94.h` so engine setter signatures (which return `spu94_result_t`) declared in the registers sub-header can refer to it without a circular umbrella include.
- Facade header `include/spu94/spu94_register_facade.h` ships 105 hand-written `static inline` wrappers (35 setters + 35 active getters + 35 pending getters). Zero linker surface (verified via `nm`).
- `spu94_snapshot_registers` now reads `state->reg_values[]` (Plan 02 zero-fill stub replaced).
- ADR-0005 added at line 33 of `docs/DECISIONS.md` (prepended above ADR-0004) — documents the split write-timing policy + per-register assignments + seam structure + Pitfall 4 protection.
- `sizeof(struct spu94_state) == 168 bytes` unchanged from end of Plan 01 (Plan 03 used reserved fields, did not add new ones; 16216 bytes headroom remaining vs `SPU94_STATE_SIZE_MAX`).

### Phase 2 Plan 05 Decisions (locked)

- Plan 02-05 lands tests-only -- 6 new C Unity TUs (4 register + 2 buffer) + Python ctypes fuzz harness + 2 new q15 tests. Zero production code change; `nm` confirms 19 T-symbols (unchanged from Plan 04 end state).
- 33 net-new C RUN_TEST entries: 17 across 4 register TUs (round-trip 3 + types 6 + policy 4 + edges 4), 14 across 2 buffer TUs (wrap 7 + mBASE 7), 2 in q15 (structured `_with_err` reference table + null-passthrough).
- Python fuzz harness `tests/python/fuzz_buffer.py` runs 10^6 random ops per invocation (~2.46 s on dev workstation, ~407K ops/s) with an INDEPENDENT Python state model (mirrors `(buffer_address, mBASE)` and applies `MAX(mBASE, (ba+2) & 0x7FFFE)` per tick); divergence in EITHER direction fails the test. Stronger than the wrap inequalities alone.
- Halfword-alignment exception scoped to `(ba & 1) != 0 IMPLIES ba == mBASE` (the snap-on-write or odd-MAX-result case). Persists across non-mBASE register writes; cleared by the next tick or another `set_mBASE`.
- Pre-Phase-6 ctypes design: hand-synced enum IDs in `fuzz_buffer.py`. Phase 6 replaces with ctypes IntEnum derived from the C header at import time.
- Pitfall 7 (stale library build) mitigated via `set_tests_properties(fuzz_buffer PROPERTIES ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>")` -- CMake re-evaluates the generator expression on every test invocation, so the harness loads the just-built `.so`.
- Coverage-boundary comment block in `test_buffer_wrap.c` documents why the `mBASE=0 + ba=0x7FFFE` wrap-to-zero corner is the Python fuzz harness's job (262K-tick reach from init exceeds C-test budget).
- TDD RED/GREEN split intentionally collapsed: Plan 05 is exclusively retroactive test coverage for already-correct Plans 01-04 implementations; tests pass on first compile, leaving no meaningful "RED" state. Single `test(...)` commit per task.
- Auto-fixed (3): `d*/m*` shorthand in a comment (Plans 03+04 recurrence); over-narrow odd-`ba` exception caught by the property-test mechanism it was meant to govern; boundary-comment regex needed both substrings on one line.
- Phase 2 success criteria 1-6 ALL met at end of Plan 05; phase complete; ready for Phase 3 (reverb algorithm) which builds inside `spu94_tick`'s already-shaped body.

### Phase 2 Plan 04 Decisions (locked)

- BufferAddress wrap formula `MAX(mBASE, (buffer_address + 2) AND 0x7FFFE)` implemented in byte arithmetic in `src/spu94/spu94_buffer.c::spu94_buffer_advance` using an inline ternary (`(advanced > mbase) ? advanced : mbase`) — no `max()` macro to hide intent (acceptance criterion).
- mBASE snap-on-write per ADR-0006: `spu94_mbase_on_write(state, new_mbase)` assigns `state->buffer_address = (uint32_t)new_mbase` verbatim. No bit-0 mask (bit-faithful per T-02-18); no implicit work-buffer clear; audible discontinuity accepted as hardware-accurate.
- `spu94_mbase_on_write` definition relocated from `spu94_write_policy.c` (Plan 03 stub) to `spu94_buffer.c` (Plan 04 real body). ODR preserved — `nm` confirms exactly one `T spu94_mbase_on_write` symbol, in `spu94_buffer.o`. The forward declaration in `spu94_register_io.c` (sole caller) was unchanged; satisfied at link time by the new home.
- Public observability accessor `spu94_get_buffer_address(const spu94_state *)` added to `include/spu94/spu94.h` — returns `uint32_t`; NULL-safe (returns 0). D-23 read-only observability principle.
- `spu94_tick` body now in its final Phase-2 shape: `apply_pending_writes` → `buffer_advance`. Pitfall 4 still satisfied — each helper has exactly one call site. Phase 3 inserts the reverb-network computation as the third line.
- `spu94_buffer_advance` is INTERNAL (not on public header). Forward-declared at the top of `spu94_buffer.c` (satisfies `-Werror=missing-prototypes`) and at the call site in `spu94_tick.c` (only caller). Promotion to public symbol would require a new ADR.
- ADR-0006 added at line 33 of `docs/DECISIONS.md` (prepended above ADR-0005). Snap-on-write resolution + wrap formula + D-11 seam + bit-0 pass-through pin + audible-discontinuity acceptance + three revision paths. Paraphrase discipline upheld; psx-spx URL cited; verbatim sentence absent.
- ADR-0005 left intact per "accepted ADRs not edited in place" Discipline rule. Its reference to `spu94_write_policy.c` as home of `spu94_mbase_on_write` is now historical; ADR-0006 records the relocation explicitly in its Sources.
- `sizeof(struct spu94_state) == 168 bytes` unchanged from end of Plan 03 (Plan 04 added no new struct fields; `buffer_address` was already reserved by Plan 01).
- Tests directory `tests/unit/buffer/` created with Unity suite `buffer_basic_unit` (11 sub-tests). Plan 05 owns the formal `test_buffer_wrap.c`, `test_buffer_mbase.c` (with full sentinel sweep for work-buf-unchanged), and the Python ctypes 10⁶-step fuzz harness `tests/python/fuzz_buffer.py`.

### Gray-Area Decisions Pending (to be logged in DECISIONS.md)

- Phase 1: Q15 multiply semantics (`>> 15` direction); vIIR = -0x8000 policy.
- Phase 2: per-register mid-stream write policy (RESOLVED Plan 03 → ADR-0005); mBASE-write side effect (RESOLVED Plan 04 → ADR-0006 snap-on-write).
- Phase 3: comb-sum intermediate accumulation precision; register-write timing between L-tick and R-tick.
- Phase 4: lv2-psx-reverb witness exclusion on frequency-response axis (documented).
- Phase 7: witness-diff tolerance calibration per preset.

### Open Questions

- Comb-sum intermediate precision — nocash silent; resolve in Phase 3 with witness check.
- mBASE-write buffer behavior — RESOLVED via Phase 2 research as snap-on-write (ADR-0006); Plan 04 landed the implementation in `src/spu94/spu94_buffer.c` through the D-11 seam.
- FIR integer accumulation width — verify 32-bit intermediate suffices for 39-tap Q15 × int16 sum in Phase 4.

### Blockers

None.

### Todos

- Phase 2 complete. Next: `/gsd-transition` to transition to Phase 3.
- Phase 3 (reverb algorithm) builds inside the already-shaped `spu94_tick` body (Plan 04 left it as `apply_pending_writes -> buffer_advance -> [Phase 3 inserts here]`).
- Phase 3 will exercise the contract pinned by Plan 05's tests (any wrap-formula or mBASE-snap regression in Phase 3 will surface in the fuzz harness at a specific (seed, step) pair).
- Phase 3 will use `q15_mul_truncate_with_err` for the per-multiply error observation that the future Error Accumulator concept depends on (D-18 / ADR-0004; tests landed in Plan 02 + Plan 05).

## Session Continuity

### Last Session (2026-04-19)

- Executed Phase 2 Plan 05 -- the test battery + Python ctypes fuzz harness that closes Phase 2.
- Created 6 C Unity TUs: `test_register_roundtrip.c` (3 tests; all-35 round-trip + snapshot + facade parity), `test_register_types.c` (6 tests; classifier + TYPE_MISMATCH per i16/u16 + UNKNOWN_REG + edge preservation), `test_register_policy.c` (4 tests; IMMEDIATE per-reg + TICK_LATCHED per-reg + multi-pending atomic flush + mixed window), `test_register_edges.c` (4 tests; INT16_MIN/MAX/0 every i16 + 5 u16 boundaries every u16 + vIIR=-0x8000 round-trip + zero meaningful), `test_buffer_wrap.c` (7 tests; advance-from-zero + floor-active + mBASE=0 32-tick + 0xFFFE snap + halfword/bounded/floor invariants), `test_buffer_mbase.c` (7 tests; immediate snap + multi-snap + snap-to-zero + 4 KB sentinel work-buf-unchanged sweep + set+tick + pending readback + reset).
- Created `tests/python/fuzz_buffer.py` (10^6 random ops; independent Python state model; ~407K ops/s; 2.46 s per 10^6 run) with golden seed `0xC0FFEE`. Wired as ctest target `fuzz_buffer` via `tests/python/CMakeLists.txt` (find_package Python3 3.10; `$<TARGET_FILE:spu94_shared>` env-var generator expression for Pitfall 7 mitigation).
- Appended 2 new tests to `tests/unit/q15/test_q15.c` (structured `q15_err_case_t` reference table for `_with_err` remainder + null-passthrough across the Phase-1 mul_cases table).
- Auto-fixed: `(22 d*/m*)` shorthand in a doc comment (the recurring Plans 03/04 issue); over-narrow odd-`buffer_address` exception in fuzz_buffer.py (caught at step 5 of first smoke run; replaced with independent Python model that validates `(ba & 1) != 0 IMPLIES ba == mBASE`); boundary-comment regex needed both substrings on one line.
- 4 commits land Plan 05: `b788a28` (Task 1 register battery), `e70ba9e` (Task 2 buffer battery), `0b2dd20` (Task 3 Python fuzz), `13be09e` (Task 4 q15 structured table). ctest 15/15 green; grep-guard + verify-no-heap clean. nm: 19 T-symbols (unchanged from Plan 04 -- tests-only landing).
- Phase 2 success criteria 1-6 ALL met. Phase 2 complete.

### Next Session

- `/gsd-transition` -- transition from Phase 2 to Phase 3 (reverb algorithm). Phase 3's reverb-network computation slots into `spu94_tick`'s body as the third statement (after `apply_pending_writes` and `buffer_advance`). Plan 05's fuzz harness becomes the regression-protection mechanism that catches any Phase 3 buffer-arithmetic or write-timing drift.

---
*State initialized: 2026-04-18 at roadmap completion*
