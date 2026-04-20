# Phase 5: Public API + Presets Integration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-20
**Phase:** 05-public-api-presets-integration
**Areas discussed:** API shape, Mix-bus wiring, Preset representation + sourcing, Preset-load atomicity, RT-safety audit, Mid-stream write test strategy

---

## Area 1 — `spu94_process` block API shape

### Q1.1 — Sample layout

| Option | Description | Selected |
|--------|-------------|----------|
| Interleaved `int16_t io[2*N]` (LRLR…) | Matches WAV on disk, CLI, numpy stereo | |
| Planar `int16_t *L, *R` | Matches JUCE / plugin frameworks; per-side control surface exposure | ✓ |
| Both (two entry points) | Ship interleaved + planar wrappers | |

**User's choice:** Planar.
**User's rationale:** *"I like having control surface exposure over each side independently."*

### Q1.2 — Reverb tail / drain API

| Option | Description | Selected |
|--------|-------------|----------|
| Documented-only (no drain API) | Caller feeds zeros after input ends; knows tail-length folklore | |
| Add `spu94_flush(state, N, Lout, Rout)` named drain | Names the concept; CLI + offline render work without folklore | ✓ |
| Only `spu94_flush` (no silent-input path) | Mixes warmup + drain | |

**User's choice:** Add `spu94_flush`.

### Q1.3 — Block size constraint

| Option | Description | Selected |
|--------|-------------|----------|
| Any N ≥ 1 | Internal FIR-phase tracking handles odd blocks | ✓ |
| Even N only | Aligns phase-0 to call boundary | |

**User's choice:** Any N ≥ 1.

### Q1.4 — In-place processing

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, `Lout == Lin` allowed | Sample-at-a-time loop is alias-safe | ✓ |
| No, distinct buffers required | Reserves option for future SIMD | |

**User's choice:** In-place allowed.
**User's follow-up:** *"When would distinct buffers be a benefit?"* → Answered: no real benefit for this architecture; caller can `memcpy` if they need the original.

---

## Area 2 — Mix-bus wiring

| Option | Description | Selected |
|--------|-------------|----------|
| Mailbox on state (`mix_bus_l`, `mix_bus_r` fields) | Smallest change; existing tests unchanged; matches register pattern | ✓ |
| Refactor `spu94_reverb_body` signature to accept `(Lin, Rin)` | Cleanest signature; ~15-20 test call sites churn | |
| Public `spu94_set_mix_bus_input` accessor | Worst — exposes internal plumbing on public API | |

**User's choice:** Mailbox.
**User's follow-up:** *"What are the implications of this decision please. I still don't understand any of what you said."* → Re-explained in plain English: literal line of code change, test-file churn count, public-API pollution.

---

## Area 3 — Preset representation + sourcing

### Q3.1 — Storage shape

| Option | Description | Selected |
|--------|-------------|----------|
| One `const spu94_preset_t presets[10]` table | Data-centric; enables introspection | ✓ |
| Ten individual `const` blocks | More symbols, same data | |
| Function-per-preset | Verbose, no introspection | |

**User's choice:** One table.

### Q3.2 — Sourcing discipline

| Option | Description | Selected |
|--------|-------------|----------|
| Single source (nocash) | Fastest; silent typo risk | |
| Three-source cross-reference (nocash + hardware-readout + third independent) | Mirrors Phase 4 FIR coefficient discipline | ✓ |

**User's choice:** Three-source cross-reference.
**Contextual note:** Actual value extraction is research-pass work (Phase 5 researcher agent lands it in `05-RESEARCH.md`). Discussion locks the *discipline*, not the values.

---

## Area 4 — Preset-load atomicity

| Option | Description | Selected |
|--------|-------------|----------|
| Respect Phase 2 D-04 split policy | v* immediate, d*/m* tick-latched; one unified write path; ~45μs "half-applied" window (inaudible) | ✓ |
| Bypass D-04: force everything immediate | "Instant preset change" via special-case path; risk of delay-jump click | |
| Bypass D-04: force everything tick-latched | All 35 commit atomically at next tick; 1-tick delay on every register | |

**User's choice:** Respect D-04.

---

## Area 5 — RT-safety audit infrastructure

Four axes, each locked to the recommended approach:

| Axis | Option | Selected |
|------|--------|----------|
| No-heap | Linker-symbol check (already in CI since Phase 1) | ✓ |
| No-locks | Linker-symbol check (new `verify-no-locks.sh`) | ✓ |
| No-syscalls | `strace -c`-based loop test on 10⁵ iterations | ✓ |
| No-variable-latency | ctypes timing benchmark; `(max-median)/median ≤ 3×` target | ✓ |
| Test layout | Four test targets under `tests/rt_safety/`, per-axis diagnosis | ✓ |

**User's follow-up:** *"Is this just for the audit at the end?"* → Answered: partly end-of-phase proof (SC-4), mostly permanent CI regression gates.
**User's verdict:** *"Okay yeah I guess just go with all recommendations because I have no fucking clue what any of that means."*
**Resolution:** Claude committed to treating RT-safety plumbing as discretion-level in future phases unless a real taste call is hiding inside; flagged in this log.

---

## Area 6 — Mid-stream write test strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Writes at any time, any register | Matches "living instrument" framing — every parameter modulatable anywhere | ✓ |
| Writes only at block boundaries | More restrictive contract; less musical | |

**User's choice:** Any time, any register.

---

## Cross-Area Follow-Up: "How accurate is this?"

> *"I just feel like I am making these choices, but have no idea how accurate it is to how the original engineers must have done it."*

**Reframe recorded in CONTEXT.md `<domain>` and `<deferred>` sections:**

- Phases 1–4 decisions (register semantics, Q15 math, reverb topology, FIR coefficients, hard-clip, vIIR anomaly, mBASE snap-on-write, split write policy) carry algorithm-authenticity weight and ARE locked to spec / three-source discipline.
- Phase 5 shell decisions (D-01..D-06, D-08..D-10) serve modern C callers — there is no 1994 precedent because PS1 silicon had no library-caller concept.
- The ONE Phase 5 decision that does carry authenticity weight is D-07 (the 10 preset register values themselves), and it gets Phase 4-grade three-source cross-reference discipline.

---

## Claude's Discretion

Carried through from the locked decisions — per-decision specifics listed in `<decisions>` → *Claude's Discretion* section of CONTEXT.md. Notable items:
- Exact `spu94_preset_t` struct shape (flat array vs typed fields)
- Whether `num_samples` is `uint32_t` or `size_t`
- RT-safety benchmark threshold — `3× median` is first-pass; planner validates against host measurement
- Whether `spu94_load_preset` takes enum id or `const spu94_preset_t *`
- ADR count and split when appending to `docs/DECISIONS.md`

## Deferred Ideas

Preserved in full in `<deferred>` section of CONTEXT.md. Summary:
- M4 planted seeds: preset morph/crossfade, user-supplied preset tables, A/B compare, mix-bus mailbox exposure
- Phase 6: Python bindings + CLI
- Phase 7: golden files, witness diff, modulation harness, LEVERS-CATALOG.md
- Phase 8: MCU smoke test
- Milestone 4: named levers, smoothing, JUCE plugin
- Milestone 5: hardware-capture preset-value arbitration
