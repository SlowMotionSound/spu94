# SPU-94 — Architecture Decision Records

This log records gray-area resolutions for the SPU-94 project. It is a first-class
deliverable per PROJECT.md: the value of a bit-faithful PS1 SPU reverb reimplementation
is in part the decisions themselves — what was ambiguous, what was chosen, and why.

## Format

Each entry is an ADR in the Michael Nygard style, with an added **Sources** section:

- **Status**: Proposed / Accepted (date, phase) / Superseded-by (ADR-NNNN).
- **Context**: What ambiguity existed and why it had to be resolved.
- **Decision**: What SPU-94 does.
- **Consequences**: Tradeoffs, test obligations, known revision paths.
- **Sources**: Standards, documentation, witness emulators, internal refs
  (paraphrased; see `docs/BIBLIOGRAPHY.md` once Phase 7 creates it).

## Discipline

- Accepted ADRs are **not edited in place** to change the decision. A decision
  reversal requires a new ADR with `Status: Accepted` that references the
  superseded ADR via `Status: Superseded-by ADR-NNNN` on the old entry.
- Prose in ADRs is original SPU-94 wording. Facts (register names, shift
  semantics, coefficient values) are cited via `BIB-NNN` keys pointing to
  `docs/BIBLIOGRAPHY.md` entries (Phase 7 deliverable; placeholder refs are
  acceptable in earlier phases).
- New entries are prepended at the top of this file. Phase 1's seed entries
  (ADR-0001, ADR-0002, ADR-0003) are presented in numerical order below for
  readability of this initial commit.

---

## ADR-0006: mBASE write side effect — snap-on-write

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

Phase 2's discussion identified the mBASE-write side-effect question as a
gray area worth resolving with primary-source research before locking a
behavior. The preliminary CONTEXT.md lean (D-09) was "floor-only — writing
mBASE updates the wrap floor but does not reset BufferAddress." The
research step (see `02-RESEARCH.md` § "mBASE Side-Effect Evidence")
contradicted the preliminary lean: the nocash psx-spx SPU documentation
states in plain language that an mBASE write additionally sets the current
buffer address to the written value. The wrap formula itself (see this
ADR's Decision section) uses `max(mBASE, ...)` so mBASE already acts as a
floor on every subsequent tick — the write-time snap is an additional,
stateful side effect that the formula alone does not produce.

Secondary and tertiary sources (hitmen c02 SPU doc, jsgroth PS1 SPU Part 3
writeup, findable PSX homebrew) are silent on the mid-stream mBASE-write
case, consistent with the Sony BIOS reverb-setup procedure that disables
reverb before changing registers. GPL witnesses (Mednafen, lv2-psx-reverb,
DuckStation) were not read per PROJECT.md licensing posture; their output
audio remains a candidate witness for Milestone 5 hardware-comparison
work, but their source code is not a primary input to this resolution.

**Decision:**

Writing `mBASE` snaps the reverb work-buffer pointer:

```
On any write to mBASE with value N:
    state->reg_values[SPU94_REG_mBASE] = (int16_t)N      (engine layer)
    state->buffer_address = (uint32_t)N                  (this ADR)
```

No implicit work-buffer clear. No crossfade. No tick-alignment delay. The
jump can produce an audible discontinuity in the reverb tail; that is
hardware-accurate behavior and SPU-94's default.

BufferAddress advance (once per stereo tick, from `spu94_buffer_advance`
called inside `spu94_tick` after `spu94_apply_pending_writes`):

```
buffer_address = MAX(mBASE, (buffer_address + 2) AND 0x7FFFE)
```

(Mathematical `MAX` — the implementation in `spu94_buffer.c` uses an
inline ternary, not a `max()` macro, to keep the operation visible at
the call site.)

- Byte addressing. The `+2` advances by one 16-bit halfword per stereo
  tick.
- `0x7FFFE` masks to the 512 KB-2 SPU RAM region with halfword alignment
  (bit 0 always clear after an advance).
- The `max(mBASE, ...)` clause floors the advance to mBASE even without
  the write-time snap; the snap is for the mid-stream-write case
  specifically.

Implemented in `src/spu94/spu94_buffer.c`:
- `spu94_buffer_advance(state)` — the wrap formula.
- `spu94_mbase_on_write(state, new_mbase)` — the snap side effect.
- `spu94_get_buffer_address(state)` — read-only observability accessor
  (D-23) on the running address.

`spu94_mbase_on_write` is the D-11 seam. SPU-94 keeps it internal (not
runtime-swappable) in Phase 2. The future Controllers milestone may
re-point it at alternative behaviors (floor-only, crossfade-on-write,
clear-and-snap) by re-linking an alternative translation unit, or by
promoting it to a runtime function pointer via an additive ADR.

**Bit-faithfulness note (T-02-18):** The snap passes the written `u16`
value through verbatim. An odd mBASE produces an odd `buffer_address`
for exactly one step, until the next `spu94_buffer_advance` clears bit 0
through the `AND 0x7FFFE` mask. The primary source is silent on bit-0
masking at write time; SPU-94 defaults to verbatim pass-through and
documents this exception in the threat register and in Plan 05's
T-02-28 fuzz invariant. The snap MUST NOT be patched to add `& ~1u` —
that would diverge from the bit-faithful interpretation.

**Consequences:**

- **Bit-faithful to primary source.** SPU-94's mBASE behavior matches the
  plain-language nocash statement. No invented side effect (buffer clear,
  crossfade, tick-alignment) that the spec does not describe.

- **D-09 revised.** The preliminary D-09 "floor-only" lean is superseded
  by this ADR. The D-11 seam structure is preserved so a future reversal
  is cheap if hardware-witness evidence (Milestone 5) contradicts the
  plain-language reading.

- **Audible discontinuity accepted.** Because the snap is instantaneous,
  writing mBASE during active reverb jumps the reverb's work-buffer
  read pointer, which manifests as a click or phase jump in the audio
  tail. This is accepted as hardware-accurate. Controllers may later
  add a smoothing layer for musical use cases — but that is the
  Controllers layer, not core SPU-94.

- **Test obligations:**
  - Plan 04 ships `tests/unit/buffer/test_buffer_basic.c` with eleven
    Unity cases covering null-safety, init/reset zeroing, single-tick
    advance, 100-tick cumulative advance, the wrap corner at the top of
    the address window, the mBASE-floor case, the snap-on-write
    immediate effect, the odd-mBASE pass-through, the work-buffer
    untouched invariant, and the tick-order observability check
    (apply_pending_writes runs before buffer_advance).
  - Plan 05 will add `tests/unit/buffer/test_buffer_wrap.c` for the
    formula corners at finer resolution and `tests/unit/buffer/test_buffer_mbase.c`
    with a full sentinel-pattern check that the snap leaves work_buf
    bytewise unchanged.
  - Plan 05 will add the Python ctypes fuzz harness
    (`tests/python/fuzz_buffer.py`) running 10^6 random operations,
    asserting after each step that
    `buffer_address >= mBASE && buffer_address <= 0x7FFFE` and that
    `(buffer_address & 1) == 0` holds *unless* the most recent op was
    a snap with an odd value (the T-02-28 exception).

- **Revision paths:**
  - Milestone 5 hardware witness contradicts "snap exactly on write" —
    a new ADR supersedes with the observed behavior; the D-11 seam
    makes the change a one-file edit.
  - Controllers needs a runtime-swappable handler — additive ADR that
    promotes the internal handler to a function-pointer slot in
    `spu94_state`. No break to existing callers.
  - Hardware witness shows bit 0 IS masked at snap time — flip the
    snap body to `state->buffer_address = (uint32_t)new_mbase & ~1u;`
    in a new ADR; the T-02-28 invariant relaxes accordingly.

**Sources:**

- External (paraphrased, cite-only): nocash psx-spx, "Reverb Volume and
  Address Registers (R/W)" subsection, which in plain language describes
  that an mBASE write additionally sets the current buffer address to
  the written value. URL:
  https://psx-spx.consoledev.net/soundprocessingunitspu/
  (extracted via WebFetch on 2026-04-19; paraphrased here per
  PROJECT.md licensing posture — no prose or tables transcribed).
- External (paraphrased): nocash psx-spx, "SPU Reverb Formula" section,
  which defines the `max(mBASE, (addr+2) AND 0x7FFFE)` wrap formula.
  Same URL. Used verbatim for the arithmetic (uncopyrightable facts,
  not prose).
- External (absence-of-evidence): hitmen c02 SPU documentation, jsgroth
  PS1 SPU Part 3 blog series — neither documents a different mBASE
  side effect; no contradiction.
- Internal: `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md`
  § "mBASE Side-Effect Evidence" — full evidence table, secondary-
  source survey, and contradiction-with-D-09 flag.
- Internal: `src/spu94/spu94_buffer.c` — the implementation this ADR
  documents.
- Prior ADR: ADR-0005 (write-timing policy table) — mBASE's IMMEDIATE
  policy is established there; the snap side effect documented here
  fires AFTER the register-value update. ADR-0005's text references
  the Plan-03 location of the handler in `spu94_write_policy.c`; the
  handler was lifted to `spu94_buffer.c` in Plan 04 (ODR preserved).

---

## ADR-0005: Per-register mid-stream write-timing policy — split policy with swappable table

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

The nocash psx-spx documentation is silent on what happens when an SPU
reverb register is written mid-tick — i.e., during the 22.05 kHz stereo
tick in which the reverb network is computing. The Sony BIOS procedure for
setting up a new reverb effect is to disable reverb, write every register,
then re-enable, which strongly implies that mid-stream writes are not a
spec'd operation. SPU-94 nonetheless has to pick a defensible behavior
because (a) PROJECT.md treats real-time modulation of every register as a
first-class use case (reverb-as-living-instrument), and (b) the algorithm
must not crash, glitch, or corrupt memory on a mid-stream write.

The 35 reverb-affecting registers fall into two structural families:

1. **Gain-type registers** (`v*`-prefix: vLOUT, vROUT, vIIR, vCOMB1..4,
   vWALL, vAPF1, vAPF2, vLIN, vRIN — 12 total) participate in per-sample
   multiplies. A faithful model of the multiplier reads each register at
   the multiply site; a mid-tick write is naturally visible to the next
   multiply that reads it.

2. **Address/delay-type registers** (`d*` / `m*`-prefix — 22 total) index
   into the reverb work buffer. The reverb algorithm's correctness depends
   on a consistent pair of L and R addresses across a stereo tick. A
   mid-tick change here would corrupt the L/R address relationship and
   produce phase/buffer artifacts that have nothing to do with the
   musical intent.

`mBASE` is a third case: the register itself is `u16` and structurally
belongs with the address family, but its update timing is IMMEDIATE so the
config value is observable instantly. Its stateful side effect — snapping
the running BufferAddress — is resolved separately by ADR-0006.

**Decision:**

SPU-94 ships a split mid-stream write policy:

- **IMMEDIATE** for all 12 `v*` gain registers AND for `mBASE`.
  Writes become visible to the next register read (and to the next
  multiply, for `v*`) within the same tick. For `mBASE` the additional
  side effect — `state->buffer_address := mBASE` — is invoked through
  the `spu94_mbase_on_write` handler defined in
  `src/spu94/spu94_write_policy.c`.

- **TICK_LATCHED** for the remaining 22 `d*` / `m*` address/delay
  registers. Writes stage into a shadow slot
  (`state->pending_values[reg]`, with the corresponding bit set in
  `state->pending_mask`) and are applied atomically at the start of
  the next `spu94_tick()` call, before any buffer-address advance or
  reverb computation.

The policy is implemented as a `static const spu94_write_policy_t`
35-entry array keyed by `spu94_reg_t`, defined in
`src/spu94/spu94_write_policy.c`. **This array IS the swappable seam
(D-05).** The future SPU-94 Controllers milestone (D-22, D-24) re-points
the table at an alternative policy by linking its own translation unit
that defines `spu94_write_policy_table` differently — without touching
core engine code. SPU-94 itself ships the table pinned to the
PS1-faithful split above.

The pending-value shadow is observable to callers via
`spu94_get_reg_i16_pending` and `spu94_get_reg_u16_pending`, which
return what will be applied at the next tick. For IMMEDIATE-policy
registers the pending and active readings always match — the engine
mirrors IMMEDIATE writes into the pending slot specifically so that
callers polling the `_pending` accessor never see a stale value.

**Consequences:**

- **Assumption flagged.** The exact per-register assignment (every `v*`
  IMMEDIATE, every `d*` / `m*` TICK_LATCHED) is structurally defensible
  but not spec-backed. It could be revised if hardware-witness evidence
  in Phase 7 contradicts a specific entry. The seam exists precisely so
  that revision is a one-line edit, not an architectural change.

- **Test obligation.** Plan 05 will add per-register policy tests under
  `tests/unit/registers/test_register_policy.c` that exercise:
  - For every `v*` register: write -> `get == get_pending == value`.
  - For every `d*` / `m*` register: write -> `get != get_pending`
    immediately, then `spu94_tick()` -> `get == get_pending == value`.
  - For `mBASE` specifically: the IMMEDIATE update of the config value
    (the snap side effect itself is in ADR-0006's scope, exercised in
    `tests/unit/buffer/test_buffer_mbase.c` per Plan 04).

- **Pitfall 4 protection.** `spu94_apply_pending_writes()` is called
  from EXACTLY one location — the first line of `spu94_tick()`. Any
  future change that invokes it from a second site violates the
  contract. A grep-based CI guard could be added if drift becomes a
  concern; the call-site-uniqueness is currently enforced by code
  review.

- **Revision paths.**
  - **Hardware witness (Milestone 5):** behavioral capture from an
    original PS1 may show that some `v*` registers are actually
    TICK_LATCHED on the real chip (e.g., if the multiplier reads at
    tick start rather than per-sample). Resolution is a new ADR
    superseding the relevant rows of this one.
  - **Controllers milestone:** may want to add a third policy
    (CROSSFADE — for zipper-free real-time gain modulation) by adding
    a new `spu94_write_policy_t` enum value, new rows in the table,
    and an additional case in the engine setter switch. This is an
    additive ADR; the IMMEDIATE/TICK_LATCHED entries above remain
    pinned for SPU-94's own consumers.
  - **Per-tick observation:** if Phase 3 reveals that the reverb
    algorithm needs a "did this tick flush any pending writes?"
    signal for diagnostics, the apply function can return the mask
    that was flushed. Pure-additive change; existing callers ignore
    the return value.

**Sources:**

- Internal: `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md`
  § "Write-Timing Policy" and § "Per-Register Policy Table" — the research
  notes that built the structural-family argument and the L/R consistency
  requirement.
- External (paraphrased, not transcribed): nocash psx-spx, SPU reverb
  section. https://psx-spx.consoledev.net/soundprocessingunitspu/ —
  facts used: the 22.05 kHz internal tick rate with L/R half-cycle
  alternation; the BIOS "disable -> rewrite -> enable" reverb-setup
  convention.
- Prior ADR: ADR-0004 (extensibility taps) — same swappable-seam
  architectural principle (D-22, D-23, D-24) applied to a different
  piece of the API.

---

## ADR-0004: Extensibility taps — `q15_mul_truncate_with_err` and `spu94_tick`

**Status:** Accepted (2026-04-19, Phase 2)

**Context:**

Phase 2 lands two intentional API-surface commitments that the phase-context
discussion designated as "extensibility taps": `q15_mul_truncate_with_err` in
the Q15 fixed-point surface, and the public `spu94_tick` per-stereo-tick
processing entry point. Both exist to serve future consumers — specifically, a
planned SPU-94 Controllers milestone (an exploration/modulation layer that
consumes SPU-94's public API as-is) and a separate Error Accumulator project
(a performance-oriented audio effect that reuses the Q15 primitives) — without
forcing those consumers to re-implement anything in SPU-94's bit-faithful core.

Neither tap is load-bearing for the reverb algorithm itself. Both are
observability and composition hooks. Landing them in Phase 2 avoids the cost
of retrofitting them later, when consumers exist and their API expectations
have become sticky. This ADR records them so they read as deliberate seams
rather than accidental public surface.

**Decision:**

SPU-94 exposes two extensibility taps as public, stable API:

1. `q15_mul_truncate_with_err(int16_t a, int16_t b, int16_t *err_out)` — the
   Q15 multiply primitive whose math is pinned by ADR-0001. Additionally
   writes the truncation remainder (the bits discarded by the `>>15` shift)
   via `err_out` when non-NULL. Passing `NULL` is permitted and makes the
   function behaviorally identical to `q15_mul_truncate`, which is now a thin
   wrapper. The remainder is *pre-saturation*: for the `INT16_MIN * INT16_MIN`
   edge case, the saturated result is `INT16_MAX` (per ADR-0001) while the
   reported remainder is zero (the product `+2^30` is exactly divisible by
   `2^15`). Callers that need the additional saturation discard can infer it
   from the difference between the pre-saturation shifted value and
   `INT16_MAX`.

2. `spu94_tick(spu94_state *state)` — the per-22.05 kHz-stereo-tick processing
   entry point. In Phase 2 it is a no-op stub; Phase 3 implements the reverb
   algorithm inside it; Phase 5's `spu94_process` wraps it as a loop.
   Observers (Controllers, Error Accumulator telemetry, test harnesses)
   interleave reads of public accessors between ticks with the guarantee that
   the observed state is instantaneous and consistent. The function is
   null-safe: `spu94_tick(NULL)` is a no-op.

**Consequences:**

- *Tradeoff accepted:* Two functions in the public API surface that the core
  reverb algorithm does not strictly need. Offset: both are committed by
  CONTEXT.md decisions D-18 and D-19, so the marginal cost is essentially
  zero — we were going to add them anyway, and adding them later would cost
  more because downstream consumers would have adapted to their absence.

- *Bit-faithfulness preserved:* Neither tap alters the reverb data path. The
  `err_out` parameter is a side channel (a write-only observation hook); the
  `spu94_tick` body in Phase 2 is empty and will be filled in Phase 3 with
  the reverb algorithm, whose correctness is orthogonal to the existence of
  the function name. ADR-0001's Q15 semantics are preserved bit-exactly:
  `q15_mul_truncate` is now a wrapper that passes `err_out = NULL`; the
  Phase 1 Q15 reference table continues to pass unchanged.

- *Observer ergonomics:* External projects (Error Accumulator consuming
  `_with_err`; Controllers milestone consuming the tick-boundary observer
  contract) now have a stable target. If either consumer discovers a need
  not covered here, the response is either "add a new seam" (cheap, additive
  ADR) or "reshape this one" (requires a new ADR superseding this one).

- *Test obligations:* Phase 1's Q15 test table must continue to pass against
  the refactored `q15_mul_truncate` — verified in Plan 02 Task 2 alongside a
  new remainder-verification table for `q15_mul_truncate_with_err` and two
  `spu94_tick` null-safety tests. Plan 05's per-register battery will
  re-exercise the remainder observation in the larger integration context.

- *Revision paths:*
  - If a future use case shows the remainder signedness convention is wrong
    for the Error Accumulator's needs, a new ADR supersedes this one and
    every `_with_err` call site is revisited.
  - If `spu94_tick` ever needs to change signature (e.g., returning a status
    code), a new ADR records the break and the migration story.
  - The `err_out` parameter type (`int16_t *`) may need widening to
    `int32_t *` if future callers need the full pre-shift product; that is
    an additive ADR (a second accessor), not a break.

**Sources:**

- Internal: `.planning/notes/2026-04-19-error-accumulator-concept.md` —
  algorithm + hardware brief motivating the per-multiply remainder tap.
- Internal: `.planning/notes/2026-04-19-spu94-controllers-seed.md` — future
  exploration-layer milestone; drives the tick-boundary observer contract.
- Internal: `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md`
  § D-18, D-19, D-22, D-23, D-24 — discussion-time locked decisions
  (extensibility taps, seams principle, observability principle, Controllers
  as future consumer).
- Prior ADR: ADR-0001 (Q15 multiply semantics) — `_with_err` preserves the
  ASR + saturation semantics pinned there.

---

## ADR-0001: Q15 multiply semantics — truncation direction and INT16_MIN² edge case

**Status:** Accepted (2026-04-18, Phase 1)

**Context:**

The PS1 SPU reverb algorithm applies Q15 fixed-point multiplication throughout: every
gain-type register (vWALL, vIIR, vCOMB1..4, vAPF1..2, vLOUT, vROUT, and the other
volume coefficients that affect the reverb path) participates in a `sample * register`
multiplication whose intermediate must be right-shifted by 15 bits to re-scale into the
int16 sample range before being written back to the work buffer or the output mix.

Two ambiguities exist:

1. The rounding direction for negative intermediate products. In C17 (§6.5.7/5) and
   C23, the expression `E1 >> E2` when `E1` is a negative signed integer is
   implementation-defined. On two's-complement hardware (now mandated by C23 via WG14
   N2412), every mainstream compiler — gcc, clang, arm-none-eabi-gcc, MSVC — emits
   arithmetic shift right (ASR), which rounds toward negative infinity. The alternative
   rounding direction in C is integer division (`/`), which rounds toward zero. These
   are different operations on negative results: `-1 >> 15` is `-1` under ASR but `0`
   under C division.

2. The INT16_MIN × INT16_MIN edge case. The mathematically-correct product
   `(-32768) × (-32768) = +2^30`. Right-shifted by 15 bits, that is `+2^15 = +32768`,
   which does not fit in the int16 range `[-32768, +32767]`. The naive cast to int16
   aliases to `-32768`, which has the wrong sign.

nocash's SPU documentation describes the reverb multiply-shift chain and states the
result is saturated to the int16 range, but does not explicitly specify the rounding
direction for negative products. Behavioral witnesses (public emulators, jsgroth.dev
PS1 SPU series — consulted output-only, not source-read, per PROJECT.md licensing
posture) consistently reflect ASR semantics. DSP hardware convention in commercial
fixed-function multiplier units is ASR.

**Decision:**

SPU-94's Q15 multiply helper is:

```c
static inline int16_t q15_mul_truncate(int16_t a, int16_t b) {
    int32_t product = (int32_t)a * (int32_t)b;
    int32_t shifted = product >> 15; /* ASR, verified by _Static_assert */
    return sat_s16(shifted);
}
```

- **Rounding direction:** arithmetic shift right (toward negative infinity).
- **INT16_MIN × INT16_MIN:** saturate to `INT16_MAX` via `sat_s16`.
- **Compiler assumption:** the target compiler emits ASR for signed negative shifts.
  Enforced at compile time by a `_Static_assert((-1 >> 1) == -1, ...)` in
  `include/spu94/spu94_q15.h`.

The function is `static inline`, header-only, and lives in the PUBLIC API at
`include/spu94/spu94_q15.h` (CONTEXT.md D-05, D-06, D-08).

**Consequences:**

- *Easier:* All target compilers (gcc 11+, clang 14+, arm-none-eabi-gcc) agree;
  zero runtime cost; idiomatic portable code.
- *Harder:* A future target compiler that emits logical-shift-right for signed
  negative values would fail the `_Static_assert` at compile time. The port is
  blocked until either a compiler swap or an explicit branchless ASR helper is
  introduced (not worth the complexity until that day).
- *Test obligation:* `tests/unit/q15/test_q15.c` asserts the ASR-vs-division
  distinguisher (`-1 * 1` returning `-1` under ASR, not `0` under division)
  and the INT16_MIN² saturation case as explicit table entries.
- *Future revision path:* If Milestone 5 hardware capture reveals the real SPU
  rounds toward zero, this ADR is superseded by a new ADR reopening the choice
  and every Q15 multiply site is revisited. The likelihood is low (industry DSP
  convention + emulator witness consensus favor ASR), but the revision path is
  acknowledged.

**Sources:**

- ISO/IEC 9899:2018 (C17) §6.5.7/5 — implementation-defined signed right shift.
- ISO/IEC 9899:2023 (C23) via WG14 N2412 — two's complement mandated; shift
  semantics unchanged.
- `BIB-001` (future bibliography entry) — nocash PSX SPU documentation,
  reverb formula section. Facts paraphrased; prose is SPU-94's own.
- `BIB-002` (future) — jsgroth.dev PS1 SPU series (behavioral witness).
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §Q15 Semantics Deep Dive, and RESEARCH.md §Common Pitfalls — Pitfall 1 + Pitfall 2.

---

## ADR-0002: vIIR = -0x8000 anomaly — reproduce faithfully

**Status:** Accepted (2026-04-18, Phase 1) — implementation deferred to Phase 3.

**Context:**

The nocash SPU documentation describes a quirk in the reverb vIIR coefficient: the
register is nominally expected in the range `-0x7FFF..+0x7FFF`, but when a caller
writes exactly `-0x8000` the final computed reverb value at that stage is negated
rather than simply clamped. nocash explicitly describes this as a documented quirk
and NOT a simple overflow bug, and notes the effect also touches the `+[mLSAME-2]`
addition term that normally should not be perturbed. Similar negation effects may
occur on other volume registers when written with exactly `-0x8000`.

The question: does SPU-94 reproduce this anomaly, or treat `-0x8000` as either
clamped to `-0x7FFF` or as an input error?

**Decision:**

Reproduce the anomaly faithfully. When the vIIR coefficient register holds exactly
`-0x8000`, the final computed value at the vIIR application site is negated. This
matches the documented hardware behavior and preserves bit-faithfulness for any
preset or modulation sequence that historically exercised this code path.

Implementation lands in **Phase 3** (at the register-application site inside the
reverb tick), **not in `q15_mul_truncate` itself**. `q15_mul_truncate` remains a
clean generic Q15 multiply; the vIIR anomaly is register-specific and is applied
as a post-step at the site where vIIR is consumed.

Phase 3 is also responsible for enumerating which *other* volume registers (if any)
exhibit the same `-0x8000` negation behavior per the nocash note on "similar
effects." That enumeration is a Phase 3 task and may update this ADR with a
`Follow-up (Phase 3)` sub-entry.

**Consequences:**

- *Easier:* Golden-file regression tests (Phase 7, TEST-04) and witness-diff
  comparisons against hardware captures (Milestone 5) align with any preset or
  test input that writes `-0x8000` to vIIR.
- *Harder:* Every register subject to the negation behavior must carry the
  anomaly logic; increases test surface. Phase 3 TEST-06 will assert the
  anomaly fires under `vIIR = -0x8000` and does NOT fire under `vIIR = -0x7FFF`
  against a hand-derived reference.
- *Semantics note:* The anomaly is not a general signed-overflow pattern. It is
  a named hardware quirk. Do NOT attempt to generalize it via a UBSan `no_sanitize`
  annotation on `q15_mul_truncate` — the multiply itself is well-defined; only
  the vIIR register application site applies the negation.

**Sources:**

- `BIB-001` (future) — nocash PSX SPU documentation, SPU Reverb Formula section,
  vIIR quirk description. Facts paraphrased; prose is SPU-94's own.
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §Q15 Semantics Deep Dive — The vIIR = −0x8000 Anomaly (ADR-0002 resolution).

---

## ADR-0003: UBSan `no_sanitize` policy — surgical, function-scoped, enumerated

**Status:** Accepted (2026-04-18, Phase 1) — first use deferred to Phase 3.

**Context:**

CI runs a UBSan build (`-fsanitize=undefined -fno-sanitize-recover=undefined`) to
catch undefined behavior in the core library. The PS1 SPU reverb algorithm relies
on documented hardware behaviors (saturation, specific overflow semantics at the
mix-bus hard clip, ADR-0002's vIIR negation site) that UBSan would otherwise flag
as `signed-integer-overflow` errors. Without a policy, the temptation is to disable
UBSan globally or to annotate broad swaths of code.

The question: under what narrow conditions may a function be exempted from UBSan's
integer checks, and how is the exemption recorded?

**Decision:**

1. **Surgical annotation only.** Individual functions that model documented SPU
   hardware wraparound or saturation may be annotated with
   `SPU94_NO_SANITIZE_INTEGER` (defined below). File-level or project-level
   disables are prohibited.

2. **Enumerated registry.** Every annotated function gains a row in the table
   below with columns *Function*, *File*, *ADR-introducing-it*, *Rationale*.
   A function with `SPU94_NO_SANITIZE_INTEGER` but no matching row in this table
   is a compliance failure. (CI enforcement of this registry check is a possible
   future enhancement; Phase 1 documents the discipline.)

3. **Phase 1 has zero entries.** No core-library function in Plan 01 wraps,
   saturates intentionally, or has any reason for annotation. The UBSan job
   passes clean on the empty-reverb-core baseline.

4. **Macro definition** (copy into `include/spu94/spu94_internal.h` or an
   equivalent project header when Phase 3 needs it):

    ```c
    #if defined(__clang__)
    #  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize("integer")))
    #elif defined(__GNUC__) && __GNUC__ >= 8
    #  define SPU94_NO_SANITIZE_INTEGER __attribute__((no_sanitize_undefined))
       /* GCC's no_sanitize("integer") exists but is less granular; the
          _undefined flavor is the pragmatic cross-compiler fallback.
          See ADR-0003 Consequences for the audit approach. */
    #else
    #  define SPU94_NO_SANITIZE_INTEGER /* empty */
    #endif
    ```

**Annotated Functions registry** (Phase 1: empty; later phases append rows):

| Function | File | ADR | Rationale |
|----------|------|-----|-----------|
| *(none in Phase 1)* | — | — | — |

**Consequences:**

- *Easier:* Intentional SPU wraparound / saturation has a pre-authorized path;
  no ad-hoc discussion when Phase 3 adds the mix-bus hard clip (CORE-02) or the
  vIIR negation (ADR-0002 implementation).
- *Harder:* Every new annotated function forces a row-add in this ADR's registry
  table. Forgetting the row is a discipline failure; future CI enhancement may
  check programmatically by grepping for the macro and matching against the table.
- *Portability note:* Under GCC the annotation uses `no_sanitize_undefined`
  which is broader than Clang's `no_sanitize("integer")`. The audit obligation
  (row in the registry + explicit rationale) compensates for the broader scope.
- *Revision trigger:* If a Phase 3+ author wants to disable sanitization at
  file level or for a non-SPU-hardware reason, the path is "new ADR that
  updates ADR-0003," not an in-place edit.

**Sources:**

- Clang UBSan reference — group name `integer` covers signed-overflow,
  unsigned-overflow, shift, integer-divide-by-zero, implicit-truncation,
  and sign-change (`BIB-003` future).
- GCC documentation on `no_sanitize_undefined` attribute (`BIB-004` future).
- Internal: `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-RESEARCH.md`
  §CI Wiring Details — UBSan CI job; §Architecture Patterns — Pattern 4.
