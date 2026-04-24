---
gsd_state_version: 1.0
milestone: v1.0
milestone_name: milestone
status: planning
last_updated: "2026-04-24T01:35:50.278Z"
progress:
  total_phases: 8
  completed_phases: 7
  total_plans: 33
  completed_plans: 33
  percent: 100
---

# STATE: SPU-94

**Last updated:** 2026-04-23

## Project Reference

See: .planning/PROJECT.md (updated 2026-04-23)

**Project:** SPU-94 — bit-faithful PS1 SPU reverb DSP
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.
**Current Focus:** Phase 07 — verification-golden-files-witness-diff-modulation

## Current Position

Phase: 07 (verification-golden-files-witness-diff-modulation) — EXECUTING
Plan: 3 of 6 complete (07-01 foundation, 07-02 goldens+repro, 07-05 RT-safety gates)

- **Milestone:** 1 (v1.0)
- **Phase:** 8
- **Plan:** Not started
- **Status:** Ready to plan
- **Progress:** [██████████] 100%

```
[███████░] 30/33 plans (~91%) — Phase 7 Wave 2 complete; Wave 3 (07-03 witness-diff + 07-04 modulation) next
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
| Phase 07 P03 | ~18m | 3 tasks | 6 files |
| Phase 07 P04 | ~12m | 2 (TDD) tasks | 7 created / 2 modified files |
| Phase 07 P06 | 9m | 3 tasks | 6 files |

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

### Phase 6 Context Decisions (locked, 2026-04-21)

- **Python API (D-01..02):** Expose both layers — raw-panel module functions (state handle passed explicitly) as primary; thin `SPU94` class as sugar over the raw layer. `spu94.presets` importable as Python data.
- **CLI (D-03..05):** Native C binary via CMake; dr_wav vendored at `vendor/dr_wav/` and linked to CLI binary only (never into `libspu94`); Python `[project.scripts]` entry_point shim for pip-install users. Non-zero exit + one-line stderr on errors.
- **Register sync + drift (D-06..08):** Runtime reflection at import — `spu94_reg_name(i)` + `spu94_reg_hw_offset(i)` iteration builds the `Register` IntEnum dynamically. Drift caught via import-time asserts on `spu94_state_size()` / reg count / preset count. Struct-internal offsets (tests-only) stay hand-typed in fuzz scripts with labeled warning blocks.
- **numpy contract (D-09..11):** Strict int16 C-contiguous arrays required on `spu94.process` / `spu94.flush`; TypeError / ValueError with actionable message otherwise. Zero-copy guaranteed when contract holds. Faithful to PS1 hardware posture — the SPU had no format-conversion layer to emulate.
- **`--config` JSON (D-12..15):** Auto-detect by `"base"` key — override patch (`{"base": "hall", "overrides": {...}}`) or flat register map. Accepts integer and hex-string values; signed / unsigned range-checked per register type; unknown keys are errors.
- **Fuzz migration (D-16..18):** All four fuzz scripts (`fuzz_buffer`, `fuzz_reverb`, `fuzz_fir`, `fuzz_process`) drop hand-typed register constants and import from the new binding. Struct-internal offsets stay hand-typed. CMake / ctest wiring unchanged.
- **README (D-19..20):** Polished tone throughout; extensive scope — 11 sections (hero, status, quick install, Python walkthrough, CLI walkthrough, "For the DSP-curious" technical section, roadmap, architecture overview, licensing posture, bibliography, contributing).
- **Packaging (D-21..25):** `manylinux_2_28` Linux wheel (glibc 2.28+); Python 3.10+ minimum; one wheel per platform (pure ctypes, no Python C API); `libspu94.so` + `spu94` binary installed inside `spu94/` package dir; `pyproject.toml` with scikit-build-core + cibuildwheel config.

All decisions logged in `.planning/phases/06-python-binding-cli/06-CONTEXT.md`. Owned requirements: PYBIND-01..06, CLI-01..04, DOCS-04 (11 total). Next: `/gsd-plan-phase 6`.

### Blockers

None.

### Todos

- Phase 6 complete. Next: `/gsd-discuss-phase 7` or `/gsd-plan-phase 7`.
- Phase 7 consumes Phase 6's Python binding directly — witness-diff / golden-file / modulation harnesses all drive `spu94.process` and `spu94.flush` through the ctypes layer landed in Plans 06-01 / 06-02.
- Phase 7 has several gray areas flagged during Phase 6 discuss: witness-diff tolerance calibration per preset, golden-file input corpus (impulse / sine / noise / silence), pytest-benchmark thresholds for RT-safety regression, coverage-table shape mapping nocash behaviors to tests.
- BIBLIOGRAPHY.md citation system (BIB-NNN) lands in Phase 7 — README reference links deferred from Phase 6 will be backfilled once BIB entries exist.
- LEVERS-CATALOG.md annotates all 35 registers — modulation cost classifications (free / sample-quantized / catastrophic) come out of the Phase 7 modulation-harness empirics.

## Session Continuity

### Last Session (2026-04-22 → 2026-04-23)

- Closed out Phase 6. All 5 plans committed (06-01 through 06-05), UAT 5/5 pass, 66/66 ctest green.
- Correctness fixes landed during close-out window: CR-01 Q15 `input_scale` regression (commit `53bac5c`), CLI `work_buf` startup-burst heap residue reset (commit `9650243`), HI-01..05 code-review fixes (commits `2574c56`..`629222b`), ADR-Phase-6-H master-send default relocated into preset tables to close the `--config` override-shape silent-output trap (commit `9746fcd`).
- Human UAT SC-4 (README walkthrough) closed with README content polish explicitly deferred to post-M4 per standing guidance (`feedback_readme_not_priority.md`). Structural gates — 10 section headings in order, 11 required content tokens, polished-tone invariants — are mechanized via `tests/docs/test_readme_sections.py` + `scripts/ci/verify-readme-sections.sh` (ctest id 66).
- Transition to Phase 7 executed 2026-04-23. ROADMAP + STATE + PROJECT updated; PYBIND-01..06, CLI-01..04, DOCS-04 moved to Validated in PROJECT.md.

### Next Session

- M1 close-out (ARCHITECTURAL-AUDIT.md Part 6) is the active workstream, NOT Phase 7 planning. See `.planning/HANDOFF.json` for the 15-step migration plan and current position.
- Step 3 shipped 2026-04-24 as commit 72f2270 (feat(api): work-buf size contract + load_preset validation, ADR-0022). Three new error codes (INVALID_STATE/WORK_BUF_TOO_SMALL/INVALID_ARG); new SPU94_WORK_BUF_MAX_BYTES constant; spu94_preset_min_work_buf_size(id) accessor. 79/79 ctest green.
- Step 4 shipped 2026-04-24 as commit bce2c13 (feat(observability): OOB tap counter via spu94_get_error_counters, ADR-0023). uint64 oob_tap_count on spu94_state; public spu94_error_counters_t + snapshot accessor; Python api.get_error_counters. tests/unit/state/test_error_counters.c added. 80/80 ctest green.
- Next in close-out: Step 5 (raise Python default work_buf_size to SPU94_WORK_BUF_MAX_BYTES), Step 6 (tighten self_test + modulation harness with oob_tap_count==0 + non-silence floor assertions), Step 7 (NULL-state-on-mutation across set_reg_*), Step 8 (CLI + Python critical findings), Step 11 (first GitHub Actions CI run), Steps 12-15 (witness-diff gate, external anchor, VERIFICATION.md, /gsd-audit-milestone + /gsd-complete-milestone).
- Phase 7 planning resumes AFTER M1 close-out completes and milestone is tagged.

---
*State initialized: 2026-04-18 at roadmap completion*
