# Phase 2: Buffer + Register Infrastructure - Context

**Gathered:** 2026-04-19
**Status:** Ready for planning

<domain>
## Phase Boundary

Phase 2 delivers the **chassis** of the SPU-94 library — the state machinery, register I/O surface, and work-buffer wrap arithmetic that the reverb algorithm (Phase 3) and public `spu94_process` path (Phase 5) build upon. It does not compute any reverb; it builds the container the reverb lives in.

**In scope:**
- Opaque state type with caller-allocated storage (no heap allocations in the library)
- All 33 SPU reverb-affecting registers (the 24 reverb-block registers `0x1F801DC0–0x1F801DFE` plus `mBASE`, `vLOUT`, `vROUT`, and other routing/control registers that participate in the reverb output)
- Typed read/write API for every register with compiler-enforced signed-vs-unsigned distinction
- Per-register mid-stream write policy (split: `v*` immediate, `d*`/`m*` tick-latched) as a swappable policy table
- Work-buffer wrap math: `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 0x7FFFE)` across 10⁶ fuzzed steps
- mBASE-write side-effect policy (preliminary: floor-only; **final answer deferred to Phase 2 research**)
- Per-register unit tests (ROADMAP Phase 2 success criterion 4; REQ TEST-02)
- `spu94.h` compiles clean under `-std=c99 -pedantic` and under an `extern "C"` C++ consumer stub (REQ API-07)
- Two extensibility taps locked in during discussion: `q15_mul_truncate_with_err` (error-remainder observability) and `spu94_tick()` (public per-tick processing entry point)
- ADR-0004 (extensibility taps) and ADR-0005 (write-timing policy table) added to `docs/DECISIONS.md`; ADR-0006 (mBASE side-effect) staged pending research

**Explicitly NOT in scope:**
- Reverb algorithm (Phase 3)
- Sample-rate conversion / 39-tap FIR (Phase 4)
- `spu94_process` block-based public entrypoint (Phase 5)
- Factory presets (Phase 5)
- Python bindings (Phase 6)
- Witness diff / golden-file harness (Phase 7)

</domain>

<decisions>
## Implementation Decisions

### Register Access Shape

- **D-01: Two-layer API.** The public register I/O surface has two layers:
  1. **Engine layer** — typed generic functions: `spu94_set_reg_i16`, `spu94_set_reg_u16`, `spu94_get_reg_i16`, `spu94_get_reg_u16`, `spu94_get_reg_i16_pending`, `spu94_get_reg_u16_pending`. Used internally, used for iteration, used for Python binding.
  2. **Facade layer** — 33 hand-written `static inline` per-register wrappers (e.g., `spu94_set_vIIR`, `spu94_get_dCOMB1`). Zero runtime cost (compiled away). Provides readable call sites.
- **D-02: Signed/unsigned distinction is structural, not documentary.** Gain-type (`v*`-prefixed) registers use the `_i16` variants; delay/address-type (`d*`/`m*`) use `_u16`. The compiler enforces the distinction. ROADMAP Phase 2 success criterion 2 is met by construction.
- **D-03: Hand-written wrappers, not macro-generated.** ~66 one-line wrappers are maintained by hand in a dedicated header file. Boring, auditable, visible to any future reader in five minutes. The ability of auditors to verify the library's bit-faithfulness claims by reading the code is worth the maintenance cost.

### Mid-Stream Write Timing

- **D-04: Split policy, implemented as a per-register policy table.**
  - `v*` (gain-type) registers: **immediate** — the next multiply that reads them sees the new value, even mid-tick.
  - `d*` / `m*` (delay / address / base-address) registers: **tick-latched** — writes go into a pending slot; the pending value becomes active at the start of the next 22.05 kHz tick.
- **D-05: The policy table is a swappable seam.** Structured as a `struct { spu94_reg_t reg; spu94_write_policy_t policy; }` table that SPU-94 ships pinned to the PS1-faithful values. The same table mechanism is what the future SPU-94 Controllers milestone re-points at alternative policies.
- **D-06: Tick-latched registers expose a pending read-back in addition to the active read-back.** `spu94_get_reg_u16(reg)` returns the active value; `spu94_get_reg_u16_pending(reg)` returns what will be applied at the next tick. For immediate-policy registers, both return the same thing. This exposes the shadow register that the split policy creates.

### Error Reporting on Register Writes

- **D-07: Register write functions return a status code.** Return type is `spu94_result_t` with at minimum `SPU94_OK`, `SPU94_CLAMPED`, `SPU94_UNKNOWN_REG`. Callers can ignore the return value with zero runtime cost; callers that check get rich information for tests, Python wrappers, and Controllers telemetry.
- **D-08: Data behavior is bit-faithful regardless of reporting.** Out-of-range values are clamped or wrapped per PS1 hardware behavior; unknown register IDs are no-ops. The status code is strictly a sidecar — it describes what happened to the data, it does not alter the data path.

### mBASE Write Side Effects

- **D-09: Preliminary policy is floor-only.** Writing a new value to `mBASE` updates the wrap floor; `BufferAddress` continues from its current position until it would wrap past `0x7FFFE`, at which point it snaps to the new `mBASE`. No implicit buffer clear, no implicit position reset.
- **D-10: FINAL DECISION DEFERRED TO PHASE 2 RESEARCH.** The research step must provide an evidence table covering:
  - Full nocash SPU/reverb section read for any implicit side-effect language the formula section doesn't mention
  - Behavioral witness comparison: Mednafen, lv2-psx-reverb, DuckStation (output-only per PROJECT.md witness policy)
  - PSX homebrew / demo code exercising mid-stream `mBASE` writes, if findable
  Research output feeds ADR-0006 in `docs/DECISIONS.md` with the resolution and its evidence basis.
- **D-11: The side-effect handler is a swappable seam.** Whatever the research lands on, the implementation is structured as a function slot that Controllers can later swap for alternative behaviors (clear-on-write, crossfade-on-write, etc.) without touching core SPU-94 code.

### State Allocation Shape

- **D-12: Opaque handle with caller-allocated storage.**
  - `typedef struct spu94_state spu94_state;` in the public header (type declared, internals hidden).
  - `size_t spu94_state_size(void);` — runtime query for exact storage size.
  - `#define SPU94_STATE_SIZE_MAX <N>` — compile-time upper bound for MCU / embedded callers that need `alignas(spu94_state) char buf[SPU94_STATE_SIZE_MAX]` stack/static allocation.
- **D-13: Caller provides the work buffer memory separately.** The reverb work buffer is not part of `spu94_state`; it is a second caller-allocated region passed at init time. This lets callers place the work buffer in regions of physical memory appropriate to their platform (e.g., MCU SRAM vs external RAM).
- **D-14: Init/reset/destroy lifecycle functions.** `spu94_init(buf, state_size, work_buf, work_buf_size)` returns `spu94_state*` initialized to the empty preset. `spu94_reset(state)` restores to empty-preset state without re-initializing. `spu94_destroy(state)` zeros the state; no-op if the library never allocated (which it doesn't).

### Register Identifier Numbering

- **D-15: Sequential enum values (`SPU94_REG_<name> = 0..32`).** Compact, canonical C iteration pattern via `SPU94_REG__COUNT`, native array indexing, clean Python binding.
- **D-16: Hardware-offset information is available on demand.** `uint16_t spu94_reg_hw_offset(spu94_reg_t reg)` returns the PS1 hardware register offset (e.g., `0xC2` for `SPU94_REG_vIIR`) for any caller that wants the hardware mapping (telemetry dumps, debugger scripts, witness-diff tooling).
- **D-17: Register-name string accessor is public.** `const char *spu94_reg_name(spu94_reg_t reg)` returns a static string (e.g., `"vIIR"`) for debug output, log formatting, and Controllers UI labels.

### Extensibility Taps (from Error Accumulator discussion)

- **D-18: `q15_mul_truncate_with_err(int16_t a, int16_t b, int16_t *err_out)` is added to `include/spu94/spu94_q15.h`.** Same math as `q15_mul_truncate` but also returns the discarded truncation remainder via `err_out`. `err_out == NULL` is permitted (equivalent to `q15_mul_truncate`). `q15_mul_truncate` itself becomes a thin `static inline` wrapper that passes `NULL`.
- **D-19: `spu94_tick(state)` is public API.** The atomic per-22.05 kHz-tick processing entry point. Phase 3's reverb algorithm is built inside this function. Phase 5's block-based `spu94_process` is built as a loop around this. Observers (Controllers, Error Accumulator, telemetry, test harnesses) can interleave processing between ticks.

### Register Observability

- **D-20: Atomic full-state snapshot.** `spu94_snapshot_registers(state, int16_t out[33])` grabs all 33 active register values at a consistent instant. Used for preset capture, witness diffs, debug dumps, Controllers UI.
- **D-21: Change callbacks are NOT included in Phase 2.** Observer/callback pattern deferred — realtime-audio-grade cost concerns (function-pointer dispatch, re-entrancy, allocation) outweigh the benefit when polling at UI-refresh rates (~60 Hz) catches all meaningful visual updates. If Controllers later demonstrates a need for push notifications, they are added then.

### Architectural Principles (span all decisions above)

- **D-22: Extensibility Seams Principle.** Every gray-area resolution is structured as a pinnable mechanism — a policy table, a function-pointer slot, a swappable side-effect handler. SPU-94 pins each seam to its PS1-faithful answer; the future Controllers milestone unpins them for exploration. This is what lets the library remain bit-faithful while leaving doors open.
- **D-23: SPU-94 Observability Principle.** The core engine must **expose** what its internal state is doing — registers (active + pending), buffer address, quantization errors, tick boundaries — as readable quantities available to external observers, **without** allowing those observers to alter the bit-faithful code path. Observability is read-only; mutation happens only through the public write API. Polling-based observability is the default; push-based (callbacks) is deferred.
- **D-24: Controllers as Future Consumer.** All Phase 2+ API design honors the constraint that a future milestone ("SPU-94 Controllers") will consume this API as a thin exploration layer — neither re-implementing core behavior nor requiring core-behavior changes. If Controllers ever needs something new, the ask is evaluated as either (a) a no-op on the bit-faithful path (acceptable) or (b) Controllers' own adaptor-layer responsibility.

### ADRs to Add During Phase 2

- **ADR-0004:** Extensibility taps — documents `q15_mul_truncate_with_err` and `spu94_tick()` as intentional public seams, not accidental API surface. Required Phase 2 deliverable.
- **ADR-0005:** Per-register write-timing policy table — documents the split policy, lists every register's timing assignment (v* immediate, d*/m* tick-latched, with any exceptions), explains the seam-table structure. Required Phase 2 deliverable.
- **ADR-0006:** mBASE write side-effect resolution — filled in after Phase 2 research. Preliminary lean: floor-only. Evidence table required. Required Phase 2 deliverable.

### Claude's Discretion (within the locked decisions above)

- Exact struct layout of `spu94_state` (field ordering, internal alignment decisions, any internal sub-structs)
- Exact shape of the policy-table data structure (array vs switch vs generated lookup)
- Naming of the `spu94_result_t` enum's internal identifiers beyond the three committed above
- Whether `spu94_reg_name` returns the register name as-is (`"vIIR"`) or with a prefix (`"SPU94_REG_vIIR"`)
- Internal file layout under `include/spu94/` — whether the 33 wrapper functions live in `spu94.h` directly, in a `spu94_registers.h` sub-include, or elsewhere
- Exact boundary between "what `spu94_init` does" vs "what `spu94_reset` does"
- Test-harness organization under `tests/unit/` (new subdirectories for buffer/register coverage)
- Whether the mBASE side-effect seam is exposed as a function pointer in Phase 2 or left internal until Controllers needs it (the decision here is a Phase 2 question; the rationale must be logged)

### Folded Todos

None — no pending todos matched Phase 2 scope at discussion time.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Core Value, constraints (no heap, no float, bit-faithfulness), licensing posture, key decisions table
- `.planning/REQUIREMENTS.md` — Phase 2 owns CORE-03, CORE-04, CORE-10, API-01, API-02, API-04, API-07, API-09, TEST-02
- `.planning/ROADMAP.md` § Phase 2 — success criteria verbatim; must all be TRUE to consider phase complete
- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — prior phase decisions (library layout, Q15 API shape, Unity test harness, DECISIONS.md format)
- `docs/DECISIONS.md` — ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR anomaly), ADR-0003 (UBSan no_sanitize policy) — Phase 2 appends ADR-0004, ADR-0005, ADR-0006

### Extensibility-Related (must read before planning register I/O and tick entry point)

- `error-accumulator.md` (repo root) — algorithm + hardware-interface brief that triggered the extensibility taps + observability principle. Phase 2 must design in a way compatible with this project eventually consuming SPU-94's API.
- `.planning/notes/2026-04-19-spu94-controllers-seed.md` — future-milestone brief for the SPU-94 Controllers exploration layer. Informs D-22/D-23/D-24 framing.

### External References (paraphrased only — do NOT transcribe)

- **nocash PSX SPU documentation** (problemkaputt.de / psx-spx.consoledev.net) — the primary authority for the 33 register addresses, the wrap formula `BufferAddress = MAX(mBASE, (addr+2) AND 0x7FFFE)`, and any implicit mBASE-write side-effect language. Phase 2 researcher must read the full SPU/reverb section, not just the register table.
- **nocash + any findable PSX homebrew** — for the mBASE research (D-10).

### Not to be read as primary source

- Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer source — per PROJECT.md licensing posture. Outputs may be used as behavioral witnesses for the mBASE research; source code is not a primary input.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phase 1)

- `include/spu94/spu94_q15.h` — header-only Q15 helpers (`q15_mul_truncate`, `sat_s16`, `q15_add_sat`) + `_Static_assert` ASR guard. Phase 2 will extend this with `q15_mul_truncate_with_err` (D-18).
- `include/spu94/spu94.h` — public API umbrella header. Currently exposes only the Q15 surface. Phase 2 expands it with state types, register I/O, tick entry point, lifecycle functions.
- `src/spu94/spu94_placeholder.c` — single TU currently exporting only `spu94_internal_version`. Phase 2 will add state, register-table, and tick-handler TUs.
- `tests/unit/q15/` — Unity-based test pattern with inline hand-computed reference tables. Phase 2 adds `tests/unit/buffer/` and `tests/unit/registers/` following the same pattern.
- CMake scaffold with `spu94_obj` OBJECT library → `spu94_shared` + `spu94_static` — already wired with determinism flags. Phase 2 additions link through the existing flow.
- CI: grep-guard, verify-flags, UBSan, clang-tidy, cppcheck — Phase 2 code must pass all of them unchanged.

### Established Patterns

- **Header-only static inline for hot-path Q15 ops** (from Phase 1 D-05). Phase 2 continues this for the per-register wrapper facade layer (D-01).
- **Inline hand-computed reference tables in test `.c` files** (from Phase 1 D-10). Phase 2 adopts this for per-register value-sweep tables.
- **ADR-style DECISIONS.md entries** (from Phase 1 D-12). Phase 2 appends ADR-0004, ADR-0005, ADR-0006 in the same format.
- **OBJECT-library-as-flag-source-of-truth** (from Phase 1). All new source files under `src/spu94/` link into `spu94_obj`; flags and includes propagate automatically.

### Integration Points

- **Phase 3 (reverb algorithm)** consumes `spu94_tick(state)` as the entry point and reads registers via `spu94_get_reg_i16` / `spu94_get_reg_u16`. Phase 2's opaque state layout must accommodate the work-buffer references and internal accumulators Phase 3 will add.
- **Phase 5 (public API + presets)** wraps `spu94_tick()` in a block-based `spu94_process` entry point. Phase 5's `spu94_load_preset` uses the per-register write API to atomically update all 33 registers.
- **Phase 6 (Python binding)** consumes the engine layer (D-01) directly via ctypes; uses `spu94_state_size()` to size its buffer; uses `spu94_reg_name()` and `spu94_reg_hw_offset()` for Python-side reflection.
- **Phase 7 (verification)** uses `spu94_snapshot_registers` (D-20) for golden-file generation and witness-diff state capture.
- **Phase 8 (MCU cross-compile)** uses `SPU94_STATE_SIZE_MAX` (D-12) to stack-allocate on Cortex-M7 where the SRAM budget is tight.
- **Future Controllers milestone** consumes every seam locked in by D-22/D-23/D-24 — register write policy table, mBASE side-effect handler, error-observation tap, tick entry point, observability read-backs.

</code_context>

<specifics>
## Specific Ideas

- **Buffer wrap test strategy.** The 10⁶ fuzzed-step requirement (ROADMAP SC 3) is a stateful property test. Implement as a Python harness driving the C library via ctypes — Python generates random mBASE values and write sequences, calls `spu94_write_reg_u16` + `spu94_tick` in a loop, asserts the wrap formula holds at every step. Reserve the test harness pattern under `tests/unit/buffer/`; the ctypes driver lives in `tests/python/` (pre-Phase-6 — a single `fuzz_buffer.py` is fine, no full wheel needed yet).
- **Per-register unit test structure.** Following Phase 1's inline-reference-table pattern: each register gets its own test TU with a hand-derived `{register, write_value, expected_read_value}` table covering signed/unsigned preservation, edge cases (0, MIN, MAX, wraparound values), and the split-policy latching behavior (active vs pending).
- **Policy table structure hint.** The write-timing policy table (D-04, D-05) is a candidate for a `static const struct { spu94_reg_t reg; spu94_write_policy_t policy; } spu94_write_policy_table[SPU94_REG__COUNT]`. Iteration over the table handles the "apply all pending writes at tick start" step cleanly. The table is the exported seam.
- **Pending register storage.** Tick-latched registers need a shadow storage slot. Consider a flat `int16_t pending_values[SPU94_REG__COUNT]` paired with a bitmask `pending_mask` indicating which slots have unapplied writes. At tick start, iterate the bitmask; for each set bit, copy pending→active and clear the bit. Simple, branch-free, cache-friendly.
- **State size determinism.** `spu94_state_size()` must return a deterministic value across reproducible builds for the golden-file / witness-diff guarantees in later phases. Use `sizeof(spu94_state)` where `spu94_state`'s layout is fully specified internally (not dependent on compiler padding choices that change between invocations).

</specifics>

<deferred>
## Deferred Ideas

### Raised in Discussion, Routed Elsewhere

- **Error Accumulator algorithm and hardware** — out of scope for SPU-94 core; captured in `error-accumulator.md` at repo root. Phase 2's extensibility taps (D-18, D-19) and the observability principle (D-23) keep the door open. The EA itself lands as a flagship deliverable in the future SPU-94 Controllers milestone.
- **SPU-94 Controllers milestone** — captured in `.planning/notes/2026-04-19-spu94-controllers-seed.md`. Late-stage milestone in this repo (post-M4 or M5). Iterative refinement to find what "feels right." Explicit action item: add to `ROADMAP.md` at M1 completion.
- **Observer/callback pattern for register changes** (D-21) — deferred until Controllers demonstrates a polling-based UI cannot serve. 60 Hz polling covers visual refresh; realtime-audio-grade callback infrastructure costs aren't yet justified.
- **Exposing intermediate tick-internal state** (comb tap outputs pre-sum, APF outputs, IIR accumulators) — Phase 3 question, not Phase 2. Phase 2 structures `spu94_tick()` so Phase 3 can expose these if needed, but adds nothing itself.
- **mBASE side-effect policy final answer** — deferred to Phase 2 research per D-10. Preliminary floor-only.

### Not Raised, but Potentially Relevant

- **State allocation alignment guarantees.** `spu94_state` may contain members that require specific alignment (e.g., SIMD-aligned buffers in future optimization phases). `spu94_state_size()` should return a size rounded up to a safe alignment, and documentation should specify the alignment requirement for caller-provided buffers. Planner to decide if this is a Phase 2 issue or deferred.
- **Thread-safety stance.** PROJECT.md says no locks in the hot path. Does that mean the library is single-threaded-per-instance (caller's responsibility to not share one state across threads)? Is there a Phase 2 doc-comment commitment or is this deferred to API-06 in Phase 5? Planner to clarify.

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time.

### Scope Creep Rejections

- **Error Accumulator as a built-in SPU-94 feature.** Rejected. Breaks bit-faithfulness. Routed to future Controllers milestone. Architectural seams added so it lands cleanly.

</deferred>

---

*Phase: 02-buffer-register-infrastructure*
*Context gathered: 2026-04-19*
