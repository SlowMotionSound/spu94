---
phase: 02-buffer-register-infrastructure
verified: 2026-04-19T00:00:00Z
status: passed
score: 6/6 success criteria verified
overrides_applied: 0
re_verification: false
---

# Phase 2: Buffer + Register Infrastructure Verification Report

**Phase Goal:** A caller can allocate an SPU-94 state, write any of the 35 registers at any time, and the buffer addressing + write-policy machinery behaves identically per spec regardless of call order.

**Verified:** 2026-04-19
**Status:** passed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths (from ROADMAP Success Criteria)

| #   | Truth (ROADMAP SC)                                                                                       | Status     | Evidence                                                                                                                                              |
| --- | -------------------------------------------------------------------------------------------------------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Caller computes state size, allocates externally, init/reset/destroy heap-free (linker-symbol verified) | ✓ VERIFIED | `verify-no-heap-symbols.sh build/src/spu94/libspu94.so` returns "OK: ...heap-free..."; nm confirms `spu94_state_size`, `spu94_init`, `spu94_reset`, `spu94_destroy` defined. |
| 2   | All 35 SPU reverb registers writable/readable via typed enum identifiers; signed/unsigned preserved       | ✓ VERIFIED | `SPU94_REG__COUNT == 35` static-asserted in `src/spu94/spu94_registers.c:20`; engine layer exposes `spu94_set_reg_i16/_u16` + `_get_*` + `_get_*_pending` with `SPU94_TYPE_MISMATCH` enforcement; ctest `register_roundtrip`, `register_types`, `register_edges` all pass. |
| 3   | `BufferAddress` advance honors `MAX(mBASE, (addr+2) AND 0x7FFFE)` over 10⁶ fuzzed steps; mBASE policy as per DECISIONS | ✓ VERIFIED | `tests/python/fuzz_buffer.py` ran 10⁶ steps in 2.69s under ctest `fuzz_buffer`; `src/spu94/spu94_buffer.c:75-79` implements the formula in byte arithmetic; ADR-0006 documents the snap-on-write policy. |
| 4   | Per-register unit tests exercise each of the 35 registers in isolation with sweeps + edges + zero-meaningful | ✓ VERIFIED | 4 test TUs in `tests/unit/registers/`: roundtrip (3 RUN_TESTs across all 35), types (6, classifier + TYPE_MISMATCH per-reg), policy (4, IMMEDIATE+TICK_LATCHED per-reg), edges (4, INT16 boundaries per i16 + 5 u16 boundaries per u16 + vIIR=-0x8000 round-trip). All pass.    |
| 5   | DECISIONS.md contains entries for (a) per-register write policy and (b) mBASE side-effect on work buffer  | ✓ VERIFIED | ADR-0005 (write-timing policy table) at line 190; ADR-0006 (mBASE snap-on-write) at line 33. ADR-0004 (extensibility taps) at line 319. All present, paraphrased per licensing posture. |
| 6   | `spu94.h` compiles under `-std=c99 -pedantic` AND under an `extern "C"` C++ consumer stub                | ✓ VERIFIED | ctest `api_c99_consumer` and `api_cxx_consumer` both pass; per-target `C_STANDARD 99` / `CXX_STANDARD 11` overrides with `-pedantic -Werror -Wall -Wextra`. |

**Score:** 6/6 success criteria verified.

### Required Artifacts

| Artifact | Expected | Status | Details |
| -------- | -------- | ------ | ------- |
| `include/spu94/spu94.h` | Umbrella public header (opaque type, lifecycle, tick, get_buffer_address) | ✓ VERIFIED | Exists; declares `spu94_state` typedef, `SPU94_STATE_SIZE_MAX`, `SPU94_STATE_ALIGN_MAX`, `spu94_result_t`, lifecycle, `spu94_tick`, `spu94_get_buffer_address`. C99+C++ consumers compile. |
| `include/spu94/spu94_registers.h` | 35-entry enum + accessor declarations | ✓ VERIFIED | 35 enum entries + `SPU94_REG__COUNT` sentinel; engine layer signatures + `spu94_reg_hw_offset`/`name`/`snapshot_registers`/`reg_type`/`apply_pending_writes`. |
| `include/spu94/spu94_register_facade.h` | 35 hand-written static inline wrappers per register (set/get/get_pending = 105) | ✓ VERIFIED | `grep -cE "static inline.*spu94_(set|get)_(v\|d\|m)[A-Z]"` returns 105. Static inline so no linker symbols. |
| `include/spu94/spu94_q15.h` | `q15_mul_truncate_with_err` + thin wrapper | ✓ VERIFIED | `q15_mul_truncate_with_err` defined at line 75; `q15_mul_truncate` is wrapper passing NULL at line 101. C++ alias for `_Static_assert` present (API-07). |
| `src/spu94/spu94_state.c` | Lifecycle implementation, heap-free | ✓ VERIFIED | Includes internal struct via header; sizeof(spu94_state) = 168 bytes per Plan 01 measurement; lifecycle implementation; no `<string.h>`/`<stdlib.h>`. |
| `src/spu94/spu94_state_internal.h` | Internal struct definition + size assert | ✓ VERIFIED | `_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX)` at line 47. Not under `include/`. |
| `src/spu94/spu94_registers.c` | Hardware-offset + name tables; snapshot reads `reg_values[]` | ✓ VERIFIED | Designated-initializer 35-entry tables; `_Static_assert((int)SPU94_REG__COUNT == 35)` at line 20; snapshot reads real storage. |
| `src/spu94/spu94_register_io.c` | Engine-layer typed setters/getters with TYPE_MISMATCH | ✓ VERIFIED | `nm` shows `spu94_set_reg_i16/_u16`, `spu94_get_reg_i16/_u16`, `_pending` variants exported; `spu94_reg_type` classifier present. |
| `src/spu94/spu94_write_policy.c` | 35-entry pinned write-policy table (D-05 seam) | ✓ VERIFIED | `spu94_write_policy_table` array present; 13 IMMEDIATE + 22 TICK_LATCHED visible by grep. `spu94_mbase_on_write` symbol absent (moved to spu94_buffer.c per Plan 04 — ODR preserved). |
| `src/spu94/spu94_pending.c` | Pending shadow flush via bitmask | ✓ VERIFIED | `spu94_apply_pending_writes` exported as T-symbol; called from `spu94_tick` only. |
| `src/spu94/spu94_buffer.c` | `spu94_buffer_advance` + real `spu94_mbase_on_write` + `spu94_get_buffer_address` | ✓ VERIFIED | All three functions defined; wrap formula `(buffer_address + 2u) & 0x7FFFEu` with `(advanced > mbase) ? advanced : mbase`; snap is verbatim (no `& ~1u`). |
| `src/spu94/spu94_tick.c` | `spu94_tick` body: apply_pending_writes → buffer_advance | ✓ VERIFIED | Lines 36, 42 of `spu94_tick.c`: apply_pending first, then buffer_advance. |
| `scripts/ci/verify-no-heap-symbols.sh` | Linker-symbol heap-free CI gate | ✓ VERIFIED | Exists, executable, `set -euo pipefail`, two-pass nm + readelf; runs green against current build. |
| `tests/api/c99_consumer.c` + `cxx_consumer.cpp` | C99 + C++ extern-C compile tests | ✓ VERIFIED | Both ctest targets green. |
| `tests/unit/state/test_state_lifecycle.c` | Lifecycle Unity tests | ✓ VERIFIED | ctest `state_lifecycle` green; 12 RUN_TESTs per Plan 01 SUMMARY. |
| `tests/unit/registers/test_register_*.c` | 7 register-test TUs (identity, io, facade, roundtrip, types, policy, edges) | ✓ VERIFIED | All 7 files present; corresponding ctest targets all green. |
| `tests/unit/buffer/test_buffer_*.c` | 3 buffer test TUs (basic, wrap, mbase) | ✓ VERIFIED | All present; ctest targets `buffer_basic_unit`, `buffer_wrap`, `buffer_mbase` all green. |
| `tests/python/fuzz_buffer.py` | 10⁶-step ctypes fuzz harness | ✓ VERIFIED | Exists; ctest `fuzz_buffer` ran 10⁶ steps in 2.69s with seed 0xC0FFEE. |
| `docs/DECISIONS.md` ADR-0004/0005/0006 | Phase 2 ADRs prepended | ✓ VERIFIED | ADR-0006 line 33, ADR-0005 line 190, ADR-0004 line 319; ADR-0001/0002/0003 preserved. |

### Key Link Verification

| From                                         | To                                  | Via                                                  | Status     | Details                                                                                                  |
| -------------------------------------------- | ----------------------------------- | ---------------------------------------------------- | ---------- | -------------------------------------------------------------------------------------------------------- |
| `spu94_register_io.c` → mBASE write          | `spu94_buffer.c::spu94_mbase_on_write` | Engine setter dispatches to internal handler         | ✓ WIRED    | Forward decl in `spu94_buffer.c:65`; implementation at line 82 sets `state->buffer_address = (uint32_t)new_mbase`. ODR: nm shows exactly one definition (in spu94_buffer.o). |
| `spu94_tick.c`                                | `spu94_pending.c::spu94_apply_pending_writes` | Tick start flush                                     | ✓ WIRED    | `spu94_tick.c:36` invocation; defined-in `spu94_pending.c`; Pitfall 4 enforced (one call site).          |
| `spu94_tick.c`                                | `spu94_buffer.c::spu94_buffer_advance` | Per-tick advance                                    | ✓ WIRED    | `spu94_tick.c:42` invocation after apply_pending. Tick order asserted by `test_apply_pending_runs_before_buffer_advance`. |
| `spu94_register_io.c::spu94_set_reg_*`        | `spu94_write_policy.c::spu94_write_policy_table` | Policy lookup decides immediate vs latched          | ✓ WIRED    | Engine setters consult the table; pending shadow + bitmask honors split policy. Test `register_policy` exercises every register's policy. |
| `spu94_register_facade.h`                     | `spu94_registers.h` engine functions | Static inline wrappers call engine setters/getters   | ✓ WIRED    | 105 wrappers; `nm` shows no facade T-symbols (compile-time only). Parity tests in `test_register_facade.c`. |
| CI workflow                                   | `scripts/ci/verify-no-heap-symbols.sh` | Dedicated `verify-no-heap` job                      | ✓ WIRED    | Per Plan 01 SUMMARY: job pinned to actions/checkout v4.2.2 SHA; runs nm + readelf passes.               |
| Public umbrella header `spu94.h`              | `spu94_registers.h` + `spu94_register_facade.h` + `spu94_q15.h` | `#include` inside extern "C"                       | ✓ WIRED    | C99 + C++ consumer compile tests pass.                                                                  |

### Data-Flow Trace (Level 4)

| Artifact                  | Data Variable          | Source                                                 | Produces Real Data            | Status     |
| ------------------------- | ---------------------- | ------------------------------------------------------ | ----------------------------- | ---------- |
| `spu94_buffer_advance`    | `state->buffer_address` | Mutated each tick by `(addr+2) & 0x7FFFE`, floored by mBASE | YES — fuzz exercises 10⁶ ops with independent Python model match | ✓ FLOWING  |
| `spu94_get_buffer_address` | `state->buffer_address` | Read of state field                                    | YES — round-tripped vs Python model | ✓ FLOWING  |
| `spu94_set_reg_i16/_u16`  | `state->reg_values[]` + `state->pending_values[]` + `pending_mask` | Engine writes from caller arguments                    | YES — `register_roundtrip` + `register_policy` confirm | ✓ FLOWING  |
| `spu94_snapshot_registers` | `state->reg_values[]`  | Iterates all 35 active values                          | YES — wired in Plan 03 (no longer zero-fill stub) | ✓ FLOWING  |
| `spu94_apply_pending_writes` | `pending_mask` → `reg_values[]` | Bitmask scan; copies pending to active per set bit       | YES — `register_policy` confirms latched writes flush at tick | ✓ FLOWING  |
| `spu94_mbase_on_write`    | `state->buffer_address` | Direct assignment from u16 argument (snap-on-write)    | YES — `test_buffer_mbase` + fuzz confirm | ✓ FLOWING  |

### Behavioral Spot-Checks

| Behavior                                          | Command                                                                 | Result                              | Status |
| ------------------------------------------------- | ----------------------------------------------------------------------- | ----------------------------------- | ------ |
| libspu94.so exports expected symbol surface       | `nm build/src/spu94/libspu94.so \| grep -cE ' T spu94_'`                | 19                                  | ✓ PASS |
| No heap imports in linked library                 | `bash scripts/ci/verify-no-heap-symbols.sh build/src/spu94/libspu94.so` | "OK: ...heap-free..."               | ✓ PASS |
| No forbidden tokens in core sources               | `bash scripts/ci/grep-guard.sh`                                         | "OK (scanned 12 files)"             | ✓ PASS |
| Full ctest suite green                            | `ctest --test-dir build --output-on-failure`                            | 15/15 passed (incl. 10⁶-step fuzz)  | ✓ PASS |
| Wrap formula present + correct mask                | `grep -E '0x7FFFE' src/spu94/spu94_buffer.c`                            | Match at line 76 with `(state->buffer_address + 2u) & 0x7FFFEu` | ✓ PASS |
| Tick order: apply_pending before buffer_advance   | `grep -nE 'spu94_(apply_pending_writes\|buffer_advance)' src/spu94/spu94_tick.c` | apply_pending@36, buffer_advance@42 | ✓ PASS |
| ODR for spu94_mbase_on_write                       | `nm build/src/spu94/libspu94.so \| grep -c ' T spu94_mbase_on_write$'` | 1 (only in spu94_buffer.o)          | ✓ PASS |
| Verbatim psx-spx sentence absent (paraphrase discipline) | `! grep -qF 'Writing a value to mBASE does additionally set the current buffer address to that value' docs/DECISIONS.md` | Sentence not present                | ✓ PASS |
| 10⁶-step fuzz harness runs to completion          | ctest `fuzz_buffer` (seed 0xC0FFEE)                                     | "OK: 1000000 steps passed", 2.69s   | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan(s) | Description | Status | Evidence |
| ----------- | -------------- | ----------- | ------ | -------- |
| CORE-03 | 02-04, 02-05 | Reverb work buffer with wrap math + mBASE side effects | ✓ SATISFIED | `spu94_buffer.c` implements `MAX(mBASE,(addr+2)&0x7FFFE)`; ADR-0006 documents snap-on-write; 10⁶-step fuzz validates invariant. |
| CORE-04 | 02-02, 02-03, 02-05 | All 35 SPU registers writable/readable with documented behavior | ✓ SATISFIED | `spu94_reg_t` enum has 35 entries (static-asserted); engine + facade cover every register; `register_roundtrip` + `register_types` + `register_edges` exercise each. |
| CORE-10 | 02-03, 02-04, 02-05 | Per-register mid-stream write policy decided + documented + implemented | ✓ SATISFIED | ADR-0005 documents split policy; `spu94_write_policy_table` (13 IMMEDIATE + 22 TICK_LATCHED + mBASE special-case); ADR-0006 + Plan 04 implement mBASE side-effect; `register_policy` + `buffer_mbase` tests confirm. |
| API-01 | 02-01 | Opaque handle + caller-allocated state, no heap | ✓ SATISFIED | `typedef struct spu94_state spu94_state;` opaque; `verify-no-heap-symbols.sh` clean; `spu94_state_size()` runtime query. |
| API-02 | 02-01 | Init/reset/destroy lifecycle; caller provides work buffer memory | ✓ SATISFIED | `spu94_init/reset/destroy` defined; dual-buffer pattern (state_buf + work_buf); 12 lifecycle tests green. |
| API-04 | 02-02, 02-03 | Typed register read/write covering all 35 registers via enum identifiers | ✓ SATISFIED | Engine layer (6 typed accessors) + 105-wrapper facade; `spu94_reg_t` enum identifiers throughout. |
| API-07 | 02-01, 02-02 | Public header C99/C11 compliant + extern "C" wrapping | ✓ SATISFIED | `api_c99_consumer` (`-std=c99 -pedantic -Werror`) + `api_cxx_consumer` (`-std=c++11 -pedantic -Werror`) ctest targets green. |
| API-09 | 02-01 | Core depends only on freestanding C subset (no malloc/stdio/pthreads) | ✓ SATISFIED | `verify-no-heap-symbols.sh` two-pass check (nm + readelf); no `<string.h>` (hand-rolled byte loops); grep-guard prohibits malloc/calloc/realloc/free. |
| TEST-02 | 02-05 | Register-level unit tests — each of 35 registers exercised in isolation | ✓ SATISFIED | 4 register test TUs in `tests/unit/registers/` (17 RUN_TESTs); every register touched per-dimension (roundtrip/types/policy/edges). |

**All 9 declared requirement IDs accounted for in code + tests. No orphaned requirements.** The traceability table in REQUIREMENTS.md marks all 9 as `[x] Complete`.

### Anti-Patterns Found

| File | Line | Pattern | Severity | Impact |
| ---- | ---- | ------- | -------- | ------ |
| (none in implementation files) | - | - | - | - |

Plan 02 SUMMARY discloses two intentional Plan-02-stage stubs (`spu94_snapshot_registers` zero-fill, `spu94_tick` empty body) — both **resolved by later plans in Phase 2** (Plan 03 wires snapshot to `reg_values[]`; Plans 03 + 04 fill `spu94_tick`'s body with apply_pending then buffer_advance). Plan 03 SUMMARY discloses one stub (`spu94_mbase_on_write` no-op) — **resolved by Plan 04** (real body in `spu94_buffer.c`). All Phase-2-internal stubs are closed before phase exit.

The Phase 2 REVIEW.md (advisory pass) flagged two warnings (WR-01: misleading test name; WR-02: pipefail edge case in verify-no-heap script) and six info-level items. Per orchestrator instruction these are noted but not failing — none compromise goal achievement and all have been previously surfaced.

### Human Verification Required

None. Phase 2 is infrastructure (state chassis, register I/O, buffer arithmetic, write policy) with no UI, no real-time audio output, no external service integration. Every truth is verifiable programmatically via:
- ctest (15/15 green including 10⁶-step Python fuzz)
- nm (symbol surface + ODR)
- grep-guard (forbidden tokens)
- verify-no-heap-symbols.sh (linker-level heap-free)
- C99 + C++ consumer compile gates

The reverb-network audio behavior that would need human listening tests is Phase 3+ scope.

### Gaps Summary

None. All 6 ROADMAP success criteria are met; all 9 declared requirements are satisfied with code + tests; all key links wired; tick order correct; ADRs (0004/0005/0006) prepended in DECISIONS.md with paraphrase discipline upheld; bit-faithfulness invariants honored (Q15 ASR semantics preserved through `q15_mul_truncate_with_err`; halfword-aligned `0x7FFFE` mask; verbatim mBASE snap including odd-value pass-through documented in ADR-0006 + T-02-18); ODR enforced (`spu94_mbase_on_write` exists exactly once, in `spu94_buffer.o`); freestanding-C subset proven at the linker level.

The build is healthy (15/15 ctest green this run), and the chassis is ready for Phase 3 to insert the reverb-network computation as the third statement in `spu94_tick`.

---

_Verified: 2026-04-19_
_Verifier: Claude (gsd-verifier)_
