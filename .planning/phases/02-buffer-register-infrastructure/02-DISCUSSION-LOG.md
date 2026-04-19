# Phase 2: Buffer + Register Infrastructure - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `02-CONTEXT.md` — this log preserves the alternatives considered.

**Date:** 2026-04-19
**Phase:** 02-buffer-register-infrastructure
**Areas discussed:** Register access style, Mid-stream write timing, mBASE write side effects, Error handling on register writes, State allocation shape, Register identifier numbering scheme, Controllers graduation path

---

## Pre-discussion: Error Accumulator + extensibility framing

Before the gray-area discussion began, the user raised the Error Accumulator concept (`error-accumulator.md` at repo root) — a stateful progressive-quantization effect with physical rotor controller. Reviewed against SPU-94's bit-faithfulness commitment; determined EA is incompatible as a SPU-94 core feature (breaks the bit-faithfulness promise) but compatible as a companion/consumer project.

**Outcome:** SPU-94 preserves bit-faithfulness; two specific extensibility taps added to Phase 2 scope to keep the EA door open; broader architectural principle (extensibility seams, observability) named and adopted.

**Locked decisions from this pre-discussion:**

| Decision | Captured as |
|----------|-------------|
| `q15_mul_truncate_with_err(a, b, *err_out)` added to Phase 2 | D-18 |
| `spu94_tick()` is public API — per-22.05 kHz processing entry point | D-19 |
| ADR-0004 documents both taps as intentional seams | (Phase 2 deliverable) |

---

## Area 1: Register access style

| Option | Description | Selected |
|--------|-------------|----------|
| A — One generic function + enum | `spu94_set_reg(state, SPU94_REG_vIIR, value)`. Compact, easy iteration, minimal Python binding. Loses signed/unsigned distinction. | |
| B — Per-register named functions | `spu94_set_vIIR(state, value)`. 33+ functions, reads beautifully at call site. Awkward for iteration and Python wrapping. | |
| C — Signed/unsigned generic variants | `spu94_set_reg_i16(...)` + `spu94_set_reg_u16(...)`. Preserves signed/unsigned. Clean for iteration and Python. | |
| **Hybrid: C engine + B facade (layered)** | Two-layer: C's typed engine functions internally, 33 hand-written inline wrappers for B's call-site feel. Zero runtime cost. | ✓ |

**User's choice:** Hybrid two-layer API (C engine + B facade).

**Rationale:**
- Engine layer keeps iteration and Python-binding ergonomics; compiler enforces signed/unsigned distinction structurally (ROADMAP SC2).
- Facade layer keeps call sites readable without adding maintenance to iteration code.
- Wrappers are `static inline` — compiler deletes them; zero runtime cost.
- Hand-written wrappers (vs macro-generated) chosen for audit readability given the bit-faithfulness commitment.

---

## Area 2: Mid-stream write timing

| Option | Description | Selected |
|--------|-------------|----------|
| 1 — Immediate everywhere | Next math op sees the new value. Simplest. Risk of mid-tick buffer artifacts on `d*` registers. | |
| 2 — Tick-latched everywhere | All writes buffered; applied at next 22.05 kHz tick. 45 µs max latency (inaudible). Maximally deterministic. | |
| **3 — Split by register type (Recommended)** | `v*` gains immediate, `d*`/`m*` delays tick-latched. Best of both. Structured as a per-register policy table. | ✓ |
| 4 — You decide | | |

**User's choice:** Option 3 (split policy).

**Rationale (reframed during discussion):**
- Not primarily a musical choice — chosen because **the per-register policy table is the seam** that future Controllers milestone needs. Split-policy-as-table is the shape most compatible with both bit-faithfulness and future exploration.
- Tick-latched latency (45 µs) is 300× below the threshold of human temporal hearing.
- Exposes a new observability distinction: `v*` registers have identical active+pending; `d*`/`m*` have potentially-differing shadow state between write and next tick. Phase 2 exposes both values (D-06).

---

## Area 3: mBASE write side effects

| Option | Description | Selected |
|--------|-------------|----------|
| 1 — Floor-only (literal spec) | Wrap formula is the only documented behavior; no implicit reset or clear. | (preliminary) |
| 2 — Reset position | BufferAddress immediately jumps to new mBASE. Would be our invention. | |
| 3 — Clear buffer | BufferAddress jumps + buffer zeroed. Safest but adds behavior. | |
| **4 — Defer to Phase 2 research** | Mark as to-be-researched against nocash + witnesses. | ✓ |

**User's choice:** Defer to Phase 2 research; preliminary lean is Option 1 (floor-only).

**Notes:**
- Research task added to D-10 with an explicit evidence checklist (full nocash SPU section, Mednafen/lv2-psx-reverb/DuckStation behavioral witnesses, PSX homebrew exercising mid-stream mBASE writes).
- Research output feeds ADR-0006 with evidence basis.
- Side-effect handler structured as a swappable seam (D-11) regardless of final answer.

---

## Area 4: Error handling on register writes

Pre-framed with the distinction: **data behavior (bit-faithful, locked)** vs **reporting behavior (our choice, sidecar)**.

| Option | Description | Selected |
|--------|-------------|----------|
| 1 — Silent (void return) | Matches PS1 hardware literally. No reporting. | |
| **2 — Status code return** | Returns `SPU94_OK` / `SPU94_CLAMPED` / `SPU94_UNKNOWN_REG`. Zero cost to ignore; rich signal for tests, Python, Controllers. | ✓ |
| 3 — Debug-only logging | Silent in release, stderr warnings in debug. Not useful for runtime observers. | |
| 4 — You decide | | |

**User's choice:** Option 2 (status code return).

**Rationale surfaced during discussion:**
- User articulated the broader principle: "I want to always expose what the bits and math are doing in the SPU core engine, because that's ultimately where the information resides for the error accumulator to operate on."
- This principle was then named formally as D-23 (SPU-94 Observability Principle).
- Controllers-tie discussion established that status-code return is what enables tactile feedback on rotor interfaces, soft-knee instability UI, preset-loading adjustment reports, and test assertions on clamping behavior.

---

## Area 5: State allocation shape (optional area, raised post-hoc)

| Option | Description | Selected |
|--------|-------------|----------|
| A — Opaque handle + size query | `spu94_state_size()` runtime query. Industry standard for this use case. | |
| B — Macro-defined max size | `SPU94_STATE_SIZE` compile-time constant. MCU-friendly, less flexible. | |
| C — Exposed struct | `spu94_state` fully public. Breaks encapsulation. | |
| **D — Hybrid (A + B)** | Opaque + `spu94_state_size()` + `SPU94_STATE_SIZE_MAX` compile-time upper bound. | ✓ |

**User's choice:** Hybrid (Option D).

**Rationale:** Covers desktop, MCU (Phase 8), and Python (Phase 6) consumers. Opacity protects internals from becoming ABI; observability is preserved through dedicated read functions rather than direct field access.

---

## Area 6: Register identifier numbering scheme (optional area)

| Option | Description | Selected |
|--------|-------------|----------|
| A — Hardware-offset values (`0xC0`, `0xC2`, ...) | Enum values are PS1 hardware register offsets. Self-documenting dumps. Non-contiguous. | |
| B — Sequential values (`0`, `1`, ...) | Canonical C enum pattern. Easy array indexing. Opaque debug dumps. | |
| **C — Sequential + hardware-offset lookup function** | Sequential enum values + `spu94_reg_hw_offset(reg)` for when the hardware mapping is needed. Best ergonomics. Hardware info available on demand. | ✓ |

**User's choice:** Option C (sequential + lookup).

**Rationale:**
- Initial recommendation was Option A (hardware-offset) under the "academic rigor" interpretation.
- User clarified "academic rigor" meant "expose all useful data for Controllers" rather than "use hardware addresses as the canonical identifier."
- Clarification reframed the question: the hardware offset is information Controllers may want available; the enum value is code-internal plumbing. Better to separate concerns: sequential for plumbing, lookup function for information.
- Also added: `spu94_reg_name(reg)` string accessor for debug output and Controllers UI labels (D-17).

---

## Area 7: Controllers graduation path (optional area)

| Option | Description | Selected |
|--------|-------------|----------|
| A — Sibling repo at M1 ship | Separate repo, separate .planning, consumes SPU-94 API cleanly. | |
| **B — Same repo, late-stage milestone** | Controllers becomes a post-M4 or M5 milestone in this project. Iterative refinement until it "feels right." | ✓ |
| C — Defer the decision | Keep the seed note; decide later. | |

**User's choice:** Option B (same repo, late-stage milestone).

**Rationale:**
- Controllers will extensively consume SPU-94 API; one repo avoids cross-repo version-pinning overhead.
- Different evaluation criteria (subjective-musical vs bit-faithful) can be handled by different milestone goals within the same project.
- Single git history tells the full story of how reverb core and control layer co-evolved.
- Error Accumulator folds in as flagship first effect of this milestone.

**Action item:** Add Controllers milestone to ROADMAP.md at M1 completion, with `error-accumulator.md` and `spu94-controllers-seed.md` attached as briefs.

---

## Architectural principles surfaced during discussion

| Principle | Captured as |
|-----------|-------------|
| Extensibility Seams — every gray-area resolution is a pinnable mechanism (policy table, function pointer slot, swappable handler); SPU-94 pins to PS1-faithful answer, Controllers unpins | D-22 |
| Observability — core engine exposes what its bits and math are doing as readable quantities, without allowing external mutation of the bit-faithful code path; polling-default, callbacks deferred | D-23 |
| Controllers as Future Consumer — all Phase 2+ API design honors the constraint that Controllers will consume this API as a thin exploration layer | D-24 |

---

## Claude's Discretion

Captured in CONTEXT.md `<decisions>` section — implementation details not worth a user decision: exact struct layout, policy-table data structure choice, `spu94_result_t` enum identifier naming, register-name string prefix convention, internal file organization under `include/spu94/`, init/reset boundary, test harness subdirectory organization, whether the mBASE side-effect seam is exposed publicly in Phase 2 or kept internal until Controllers needs it.

---

## Deferred Ideas

- **Error Accumulator as a built-in SPU-94 feature** — rejected (breaks bit-faithfulness). Routed to future Controllers milestone.
- **SPU-94 Controllers milestone** — captured as seed note; scheduled for post-M4/M5.
- **Observer/callback pattern for register changes** — deferred; polling at UI-refresh rate covers the need for now.
- **Intermediate tick-internal state exposure** — Phase 3 question, not Phase 2.
- **mBASE side-effect final answer** — deferred to Phase 2 research.
- **State allocation alignment guarantees** — planner to decide if Phase 2 or later.
- **Thread-safety stance documentation** — planner to clarify Phase 2 scope or defer to API-06 in Phase 5.
