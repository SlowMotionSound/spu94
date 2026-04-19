# Phase 2: Buffer + Register Infrastructure — Research

**Researched:** 2026-04-19
**Domain:** PS1 SPU reverb register-I/O semantics; opaque-handle / caller-allocated C library architecture; property-based stateful testing via Python ctypes.
**Confidence:** HIGH on the register table and the wrap formula; HIGH on the mBASE side-effect direction (the preliminary floor-only lean in D-09 is contradicted by the primary source); MEDIUM on register-write timing (nocash is silent); HIGH on the opaque-handle pattern.

## Summary

The canonical register inventory (33 entries), exact hardware offsets, signed/unsigned typing, and the `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)` wrap formula are directly available in nocash psx-spx and cross-confirmed by a secondary writeup. The **mBASE write side-effect is explicitly documented** in psx-spx: writing mBASE additionally **sets the current buffer address to that value** — this is a BufferAddress-snap, not a floor-only update. D-09's preliminary "floor-only" lean is wrong and ADR-0006 must land on "snap BufferAddress to the new mBASE on every mBASE write" to be spec-faithful. No evidence was found for any implicit buffer-clear or implicit reset-on-write beyond the BufferAddress snap.

Register-write timing (immediate vs tick-latched) is not specified by nocash. D-04's split policy (v\* immediate / d\*/m\* tick-latched) is a well-defensible pragmatic choice that matches the observable structure of the algorithm (gain registers are read fresh at each multiply site; address registers index memory and a mid-tick change would corrupt a two-half-cycle L/R pair) and is the canonical way this decision is resolved when the spec is silent. ADR-0005 should land this as the SPU-94 pinned default with the rationale that (a) spec is silent, (b) the split matches the observable data-flow structure, (c) Sony's own BIOS procedure for reverb changes wraps them in a reverb-off / reconfigure / reverb-on cycle, implying mid-stream multi-register changes are not a hardware-supported operation and the hardware's exact behavior is undefined-by-spec — SPU-94 picks a principled split and documents it as the seam.

The 22.05 kHz tick derivation is unambiguous: the SPU clocks at 44.1 kHz but alternates L / R half-cycles within the reverb unit, producing one full stereo reverb sample every two 44.1 kHz cycles. `spu94_tick()` models the full L+R stereo step (not half of it); internal implementation may use two half-steps if Phase 3 wants.

The opaque-handle + caller-allocated-storage + runtime-size-query + compile-time-upper-bound pattern is well-established (Yann Collet's LZ4/zstd "shell type" approach, Memfault / interrupt.memfault.com / embedded C API writeups). The canonical implementation uses a `union { char body[SPU94_STATE_SIZE_MAX]; alignment_type align; }` shell with a `_Static_assert` keeping it ≥ the real `sizeof(struct spu94_state)`.

**Primary recommendation:** Plan Phase 2 around four locked facts — the 33-register table (Section "Register Inventory"), the verbatim wrap formula (Section "BufferAddress Arithmetic"), the mBASE-snap side effect (Section "mBASE Side-Effect Evidence"), and the shell-type opaque-handle pattern (Section "State-Allocation Pattern"). Flag D-09's preliminary floor-only lean as contradicted; the user confirms ADR-0006 before the planner locks it.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Register Access Shape**
- D-01: Two-layer API — engine layer (`spu94_{set,get}_reg_{i16,u16}`, `_pending` variants for tick-latched read-back) + facade layer (33 hand-written `static inline` per-register wrappers).
- D-02: Signed/unsigned distinction is structural (compiler-enforced). Gain (`v*`) uses `_i16`; delay/address (`d*`/`m*`) uses `_u16`.
- D-03: Hand-written wrappers, not macro-generated. ~66 one-line wrappers, auditable.

**Mid-Stream Write Timing**
- D-04: Split policy, per-register policy table. `v*` immediate; `d*`/`m*` tick-latched; pending value activates at next 22.05 kHz tick start.
- D-05: The policy table is a swappable seam (future Controllers re-points it).
- D-06: Tick-latched registers expose pending read-back (`spu94_get_reg_{i16,u16}_pending`) in addition to active.

**Error Reporting on Register Writes**
- D-07: Write functions return `spu94_result_t` with at minimum `SPU94_OK`, `SPU94_CLAMPED`, `SPU94_UNKNOWN_REG`. Zero-cost if ignored.
- D-08: Data behavior is bit-faithful regardless of status. Clamp/wrap per hardware; unknown reg = no-op. Status is sidecar.

**mBASE Write Side Effects**
- D-09: Preliminary policy is floor-only (`NOTE: contradicted by research — see Section "mBASE Side-Effect Evidence"`).
- D-10: FINAL DECISION DEFERRED TO PHASE 2 RESEARCH (this document). Research feeds ADR-0006 with evidence.
- D-11: mBASE side-effect handler is a swappable seam (function slot for future Controllers).

**State Allocation Shape**
- D-12: Opaque handle (`typedef struct spu94_state spu94_state;`) + `size_t spu94_state_size(void);` + `#define SPU94_STATE_SIZE_MAX <N>` compile-time upper bound.
- D-13: Caller provides work buffer memory separately (not part of `spu94_state`).
- D-14: `spu94_init`/`spu94_reset`/`spu94_destroy` lifecycle. `_destroy` zeros state, no-op since library never allocated.

**Register Identifier Numbering**
- D-15: Sequential enum values `SPU94_REG_<name> = 0..32`, with `SPU94_REG__COUNT`.
- D-16: `uint16_t spu94_reg_hw_offset(spu94_reg_t reg)` returns PS1 hardware offset.
- D-17: `const char *spu94_reg_name(spu94_reg_t reg)` returns static string (e.g., `"vIIR"`).

**Extensibility Taps**
- D-18: `q15_mul_truncate_with_err(int16_t a, int16_t b, int16_t *err_out)` added to `spu94_q15.h`. `err_out == NULL` allowed. Existing `q15_mul_truncate` becomes thin `static inline` wrapper passing NULL.
- D-19: `spu94_tick(state)` is public API. The atomic per-22.05 kHz-tick processing entry point.

**Register Observability**
- D-20: `spu94_snapshot_registers(state, int16_t out[33])` — atomic full-state snapshot.
- D-21: Change callbacks NOT in Phase 2 (deferred).

**Architectural Principles**
- D-22: Extensibility Seams Principle — gray areas resolved as pinnable mechanisms.
- D-23: Observability Principle — core exposes internal state as read-only; mutation via public write API only.
- D-24: Controllers as Future Consumer — all API design honors future Controllers milestone.

**ADRs Required**
- ADR-0004: Extensibility taps (`q15_mul_truncate_with_err`, `spu94_tick()`).
- ADR-0005: Per-register write-timing policy table.
- ADR-0006: mBASE write side-effect resolution (evidence-backed).

### Claude's Discretion

- Exact struct layout of `spu94_state` (field ordering, internal alignment decisions, sub-structs).
- Shape of the policy-table data structure (array / switch / generated lookup).
- Internal identifiers of `spu94_result_t` beyond the three committed above.
- Whether `spu94_reg_name` returns `"vIIR"` or `"SPU94_REG_vIIR"`.
- Internal file layout under `include/spu94/` (single umbrella vs `spu94_registers.h` sub-include).
- Boundary between `spu94_init` and `spu94_reset`.
- Test-harness organization under `tests/unit/` (new subdirectories for buffer/register coverage).
- Whether mBASE side-effect seam is a function pointer in Phase 2 or internal-only.
- State-allocation alignment guarantees — Phase 2 or deferred (flagged in CONTEXT § Deferred).
- Thread-safety doc-comment stance — Phase 2 or deferred to API-06 in Phase 5.

### Deferred Ideas (OUT OF SCOPE)

- Error Accumulator algorithm and hardware (captured in notes; lands in future Controllers milestone).
- SPU-94 Controllers milestone itself (post-M4/M5).
- Observer/callback pattern for register changes (D-21).
- Exposing intermediate tick-internal state (Phase 3 question).
- Change of mBASE side-effect policy after ADR-0006 lands (seam-swappable but not re-resolved in Phase 2).

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CORE-03 | Reverb work buffer with correct wrap-address math per nocash (`BufferAddress = MAX(mBASE, (addr+2) AND 0x7FFFE)`) and documented mBASE-write side effects | "BufferAddress Arithmetic" section confirms formula verbatim from nocash; "mBASE Side-Effect Evidence" section documents the verbatim psx-spx sentence "Writing a value to mBASE does additionally set the current buffer address to that value" and provides the evidence table for ADR-0006 |
| CORE-04 | All 33 SPU registers that affect reverb output, each implemented with documented behavior | "Register Inventory" section lists all 33 registers with hardware offsets, signed/unsigned typing, and zero-value-meaningful semantics |
| CORE-10 | Per-register mid-stream write policy — decided, documented in DECISIONS.md, implemented consistently | "Write-Timing Policy" section documents that nocash is silent, provides the rationale chain for D-04's split policy, and gives the per-register policy-table template |
| API-01 | Opaque handle type with caller-allocated state (no heap allocations) | "State-Allocation Pattern" section documents the shell-type pattern and gives a concrete C99/C11 implementation template |
| API-02 | Init/reset/destroy lifecycle; caller provides work buffer memory | "State-Allocation Pattern" section specifies the init/reset/destroy contract and the separate work-buffer parameter |
| API-04 | Typed register read/write covering all 33 registers via enum identifiers | "Register Inventory" section provides the canonical enum ordering; CONTEXT D-15..D-17 define the accessor shape |
| API-07 | Public header `spu94.h` compiles clean under `-std=c99 -pedantic` and `extern "C"` C++ consumer | "C99 Freestanding Conformance" section lists known pitfalls (`_Static_assert` availability, `stdint.h` only, no VLA in struct, no flexible array member in consumer-visible header) |
| API-09 | Core library depends only on freestanding C subset | "C99 Freestanding Conformance" section confirms the freestanding headers usable in `spu94.h`: `<stdint.h>`, `<stddef.h>`, `<limits.h>`, `<stdbool.h>` |
| TEST-02 | Register-level unit tests — each of the 33 registers exercised in isolation | "Test-Harness Strategy" section documents the per-register table pattern and the `tests/python/fuzz_buffer.py` stateful-property-test approach for the 10⁶-step BufferAddress fuzz |

</phase_requirements>

## Project Constraints (from CLAUDE.md / PROJECT.md)

Extracted and honored in this research; the planner must continue to honor them:

1. **No heap in core.** CORE library must not reference `malloc`/`calloc`/`realloc`/`free`. Enforced by the `scripts/ci/grep-guard.sh` guard seeded in Phase 1. `spu94_state` is caller-allocated; the work buffer is a separate caller-allocated region.
2. **No float/double in core.** Enforced by the same grep guard. All Q15 arithmetic uses `int16_t`/`int32_t`.
3. **No unqualified `long`.** Use explicit-width types from `<stdint.h>`.
4. **Bit-faithful-from-spec, not from-port.** Primary research input is nocash psx-spx. Mednafen/lv2-psx-reverb/DuckStation/MiSTer are not read as primary sources; their outputs may be used as behavioral witnesses only (not needed for Phase 2 — the spec is explicit enough on the questions Phase 2 must resolve).
5. **nocash paraphrase discipline.** Facts (register addresses, formulas, bit layouts) are free to use; nocash's explanatory prose is paraphrased in SPU-94's own words. This research document quotes verbatim where necessary for research traceability; ADR-0006 and any code comments MUST paraphrase.
6. **C99/C11 freestanding conformance.** Public headers depend only on the freestanding C subset (`<stdint.h>`, `<stddef.h>`, `<limits.h>`, `<stdbool.h>`). No `<stdio.h>`, `<stdlib.h>`, `<string.h>` in public headers.
7. **Determinism flags in force.** Phase 1 locked `-ffp-contract=off`, `-fno-fast-math`, `-Werror` on gcc+clang. Phase 2 additions inherit through `spu94_warnings` INTERFACE target — no new flag surface.
8. **User-execution-style (global CLAUDE.md):** user is the hands-on operator; tasks touching deployed systems must be presented as guided walkthroughs. Phase 2 is pure-code-in-repo (no deployed systems), so this is a no-op for Phase 2, but the planner should structure plans so the user can execute and verify each plan independently.

## Standard Stack

### Core (already landed by Phase 1 — reused)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| Unity (C test framework) | pinned in Phase 1 | Per-register unit tests (TEST-02) | [VERIFIED: Phase 1 01-CONTEXT D-09, 01-02-PLAN] Already vendored; small (~3 `.c` files); embedded-friendly; inline reference-table pattern already established in `tests/unit/q15/test_q15.c`. |
| CMake OBJECT library (`spu94_obj` → shared/static) | — | Flag-identical build artifacts | [VERIFIED: `src/spu94/CMakeLists.txt`] Already wired; Phase 2 source files add to the existing OBJECT list; determinism flags propagate via `spu94_warnings` INTERFACE. |

### Core (new in Phase 2)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| None (zero new runtime deps) | — | — | [VERIFIED: CONTEXT Decisions] Phase 2 is C99 freestanding + `_Static_assert`. All data structures are hand-written; the register table is a `static const` array; the write-policy table is a `static const` array. No third-party C code needed. |

### Supporting (test-side only, non-shipped)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Python 3.10+ (ctypes + hypothesis) | 3.10+ floor | 10⁶-step `BufferAddress` fuzz harness (SC 3) | When the C-side Unity tests can only cover finite tables. A Python ctypes driver gives us structured fuzzing without pulling a second C framework. |
| hypothesis | ≥ 6.0 | Stateful property test (`RuleBasedStateMachine` + `@rule` + `@invariant`) | When the fuzz harness needs shrinking of failing sequences and structured rule-based input generation. Optional — a pure-random harness with `random` is also acceptable for 10⁶ steps (faster, no dep). Recommendation: pure-random for Phase 2 (simpler, no dependency entering Phase 2 scope); upgrade to hypothesis if bugs are found that need shrinking. |
| pytest | 9.0.3 (local) | Test runner for Python-side tests | Standard. Local env already has 9.0.3; CI can pin a version when Phase 6 lands. Phase 2 uses it directly only if we wrap the fuzz harness in `tests/python/test_fuzz_buffer.py`; a `python tests/python/fuzz_buffer.py` script is equally acceptable. |

**Python floor recommendation:** 3.10. [VERIFIED: environment probe — local `python3 --version` → `Python 3.13.7`; ctypes and hypothesis both stable since well before 3.10.] 3.10 gives us `match` statements if wanted (not required), structural pattern matching, and is the oldest Python still receiving security fixes through mid-2026. 3.9 is EOL October 2025; do not target it. [CITED: https://devguide.python.org/versions/]

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Unity (C test framework, already present) | cmocka, greatest, µnit | Phase 1 already committed to Unity (D-09); no reason to introduce a second framework. |
| Python ctypes fuzz | C-only fuzz (e.g., libFuzzer, AFL) | Phase 2 is pre-Phase-6 (no Python binding yet); ctypes is the lightest possible approach. libFuzzer requires a clang build step not yet wired. A single-file `tests/python/fuzz_buffer.py` calling into `spu94_shared` via ctypes is an order of magnitude less infrastructure for a property test that runs 10⁶ steps in seconds. |
| Pure-random fuzz | Hypothesis RuleBasedStateMachine | Hypothesis gives shrinking; random gives speed and zero deps. Phase 2 picks pure-random; upgrade to Hypothesis if a failure is hard to reproduce. |
| Shell-type opaque handle | Fully-opaque-pointer-only (no compile-time max) | The compile-time max is required by D-12 (MCU callers need stack/static allocation). No alternative. |

**Installation:**

```bash
# Test-side Python (optional; only needed when running the fuzz harness)
# - Python 3.10+ — system/venv provides
# - hypothesis is OPTIONAL; pure-random fuzz in Phase 2 is the default
python3 -m pip install --user hypothesis    # OPTIONAL, only if shrinking is needed
```

**Version verification:**
- `python3 --version` locally returns `Python 3.13.7`. [VERIFIED: environment probe]
- `pytest --version` locally returns `9.0.3`. [VERIFIED: environment probe]
- No new versions to pin; Phase 2 introduces no new C dependencies.

## Architecture Patterns

### Recommended Project Structure (additions to Phase 1 layout)

```
include/spu94/
├── spu94.h                 # PUBLIC API umbrella — existing; Phase 2 extends
├── spu94_q15.h             # PUBLIC Q15 helpers — existing; Phase 2 adds q15_mul_truncate_with_err (D-18)
├── spu94_registers.h       # PUBLIC — register enum, accessors, facade wrappers (D-01, D-15..D-17)
└── spu94_state.h           # PUBLIC — opaque state forward decl, SPU94_STATE_SIZE_MAX, lifecycle (D-12..D-14)

src/spu94/
├── spu94_placeholder.c     # RETIRE in Phase 2 (replaced by real TUs; keep the version symbol in spu94_state.c)
├── spu94_state.c           # State storage, init/reset/destroy, shell-type definition
├── spu94_registers.c       # Register table, engine-layer accessors, snapshot
├── spu94_write_policy.c    # Policy table, pending-write storage, apply-pending-at-tick
├── spu94_buffer.c          # BufferAddress advance, mBASE-write side-effect handler
└── spu94_tick.c            # spu94_tick() entry point (empty reverb body in Phase 2; Phase 3 fills it)

tests/unit/
├── q15/                    # existing
├── registers/              # NEW — per-register unit tests (TEST-02)
│   ├── CMakeLists.txt
│   ├── test_register_roundtrip.c   # 33-register R/W round-trip table
│   ├── test_register_types.c       # signed-vs-unsigned preservation
│   ├── test_register_policy.c      # split-policy active-vs-pending behavior
│   └── test_register_edges.c       # zero, MIN, MAX, wraparound per register
└── buffer/                 # NEW — BufferAddress arithmetic (TEST-02, CORE-03)
    ├── CMakeLists.txt
    ├── test_buffer_wrap.c          # table-driven wrap formula corners
    └── test_buffer_mbase.c         # mBASE-write side-effect behavior

tests/python/               # NEW — pre-Phase-6 single-file ctypes driver
└── fuzz_buffer.py          # 10⁶-step BufferAddress fuzz (SC 3)
```

Discretion: planner may collapse any of the `src/spu94/*.c` files (e.g., keep `spu94_registers.c` + `spu94_write_policy.c` in a single TU) or split further. The separation above is recommendation, not lock.

### Pattern 1: Shell-Type Opaque Handle with Caller-Allocated Storage

**What:** Public header declares `typedef struct spu94_state spu94_state;` (incomplete type) plus a shell type with known size+alignment and a compile-time upper bound macro. The real `struct spu94_state` definition lives in a private header under `src/spu94/` and is kept in sync via `_Static_assert`.

**When to use:** D-12 exact requirements — opaque handle, runtime size query, compile-time upper bound for MCU/embedded stack allocation, caller-allocated storage.

**Template** (paraphrased pattern from Collet's `fastcompression` writeup and the Interrupt / Memfault Opaque Pointers article):

```c
/* include/spu94/spu94_state.h — PUBLIC */
#ifndef SPU94_STATE_H
#define SPU94_STATE_H

#include <stddef.h>
#include <stdint.h>

typedef struct spu94_state spu94_state;      /* incomplete type — opaque */

/* Compile-time upper bound. The real sizeof(struct spu94_state) is
 * _Static_asserted to be <= this value in src/spu94/spu94_state.c.
 * Chosen generously (round up to the next power of two) so modest future
 * field additions don't force a bump. */
#define SPU94_STATE_SIZE_MAX   16384        /* bytes — adjust during implementation */
#define SPU94_STATE_ALIGN_MAX  16           /* bytes — covers int64_t on all targets */

/* Shell type for stack/static allocation on MCUs:
 *   alignas(SPU94_STATE_ALIGN_MAX) unsigned char buf[SPU94_STATE_SIZE_MAX];
 * Unions are an equally valid idiom:
 *   union { unsigned char body[SPU94_STATE_SIZE_MAX]; uint64_t align; } buf;
 */

size_t spu94_state_size(void);       /* runtime query — returns sizeof(struct spu94_state) */
size_t spu94_state_align(void);      /* runtime query — returns alignof(struct spu94_state) */

spu94_state *spu94_init(void *state_buf, size_t state_buf_size,
                        void *work_buf,  size_t work_buf_size);
void         spu94_reset(spu94_state *s);
void         spu94_destroy(spu94_state *s);

#endif /* SPU94_STATE_H */
```

```c
/* src/spu94/spu94_state.c — PRIVATE */
#include <spu94/spu94_state.h>
#include "spu94_internal.h"      /* defines the real struct spu94_state */

/* Keep the shell bound honest. Fail the build if the real struct grows past
 * the compile-time max. */
_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX,
    "SPU94_STATE_SIZE_MAX too small for current state layout.");
_Static_assert(_Alignof(struct spu94_state) <= SPU94_STATE_ALIGN_MAX,
    "SPU94_STATE_ALIGN_MAX too small for current state alignment.");

size_t spu94_state_size(void)  { return sizeof(struct spu94_state);   }
size_t spu94_state_align(void) { return _Alignof(struct spu94_state); }

/* spu94_init validates buf sizes and alignment, zero-initializes, returns
 * a pointer to the caller's buffer typed as spu94_state*. No heap touch. */
```

**Sources:**
- [CITED: https://interrupt.memfault.com/blog/opaque-pointers] — Memfault "Practical Design Patterns: Opaque Pointers and Objects in C" (confirms the runtime-query + caller-provided-buffer pattern).
- [CITED: http://fastcompression.blogspot.com/2019/01/opaque-types-and-static-allocation.html] — Yann Collet "Opaque Types and Static Allocation" (the shell-type pattern with `_Static_assert` size sync).
- [CITED: https://en.cppreference.com/w/c/language/_Alignas.html] — `_Alignas`/`alignas` C11 reference; `alignas` is the C11 keyword (a macro from `<stdalign.h>` that expands to `_Alignas`).

### Pattern 2: Policy Table as the Swappable Seam (D-05)

**What:** A `static const struct { spu94_reg_t reg; spu94_write_policy_t policy; } spu94_write_policy_table[SPU94_REG__COUNT]` declares per-register write timing. Default table in Phase 2 ships pinned to the D-04 split (v\* immediate, d\*/m\* tick-latched). The register write function consults the table once per write.

**When to use:** D-04, D-05, CONTEXT § Specific Ideas "Policy table structure hint". This is the exact structure called out.

**Example:**

```c
/* src/spu94/spu94_write_policy.c */
typedef enum {
    SPU94_WRITE_POLICY_IMMEDIATE = 0,
    SPU94_WRITE_POLICY_TICK_LATCHED = 1,
} spu94_write_policy_t;

static const spu94_write_policy_t spu94_write_policy_default[SPU94_REG__COUNT] = {
    [SPU94_REG_vLOUT]  = SPU94_WRITE_POLICY_IMMEDIATE,
    [SPU94_REG_vROUT]  = SPU94_WRITE_POLICY_IMMEDIATE,
    [SPU94_REG_mBASE]  = SPU94_WRITE_POLICY_IMMEDIATE,  /* per ADR-0006 — snap BufferAddress on write is a stateful side effect, not a latched config change */
    [SPU94_REG_dAPF1]  = SPU94_WRITE_POLICY_TICK_LATCHED,
    /* ... 33 rows total ... */
};
```

Note mBASE is IMMEDIATE in this scheme (not tick-latched) because the side effect it triggers — BufferAddress snap — is a stateful hardware operation, not a configuration change. Flagged as a corner case in ADR-0005.

### Pattern 3: Pending-Write Shadow with Bitmask (Specific Ideas hint)

**What:** A flat `int16_t spu94_state::pending_values[SPU94_REG__COUNT]` array paired with a `uint64_t pending_mask` (33 bits fit in one u64). A write to a tick-latched register stores the value in `pending_values[reg]` and sets bit `reg` in `pending_mask`. At the start of each tick, `spu94_apply_pending_writes(state)` iterates the mask (e.g., `while (mask) { int reg = __builtin_ctzll(mask); active[reg] = pending[reg]; mask &= mask - 1; }`), copies pending→active, clears the mask.

**When to use:** CONTEXT § Specific Ideas calls this out. Simple, branch-free, cache-friendly, and `__builtin_ctzll` is available on gcc/clang/arm-none-eabi-gcc (the full target matrix from ADR-0001); a fallback loop works for any C99 compiler.

### Pattern 4: Register Facade as Zero-Cost `static inline` (D-03)

**What:** Each of the 33 registers has a hand-written pair of `static inline` wrappers in `include/spu94/spu94_registers.h`:

```c
static inline spu94_result_t spu94_set_vIIR(spu94_state *s, int16_t v) {
    return spu94_set_reg_i16(s, SPU94_REG_vIIR, v);
}
static inline int16_t spu94_get_vIIR(const spu94_state *s) {
    return spu94_get_reg_i16(s, SPU94_REG_vIIR);
}
```

**When to use:** Every one of the 33 registers gets this treatment. Maintenance-by-hand; auditable in five minutes; zero runtime cost (compilers inline away the indirection).

### Anti-Patterns to Avoid

- **`void *` everywhere.** The typed `int16_t` vs `uint16_t` split is the whole point of D-02. Do not write a single generic `spu94_set_reg(spu94_state*, spu94_reg_t, int32_t)` that accepts all register writes — it defeats the compiler-enforced signed/unsigned distinction.
- **Macro-generated wrapper bodies.** D-03 rules this out explicitly. Hand-written beats clever.
- **Buffering the ENTIRE active state for tick-latched application.** Only tick-latched register values need shadow storage; immediate-policy registers write straight through. Do not double-buffer all 33 values.
- **Mid-tick pending-writes flush.** If a pending-write flush is triggered at any time other than the *start* of a tick, the `v*` immediate / `d*`/`m*` latched split is broken. The flush site is one function at one call point.
- **Hiding the mBASE side-effect.** The BufferAddress snap MUST be visible to observers (D-23). Specifically, `spu94_set_mBASE(s, value)` must cause a subsequent `spu94_get_buffer_address(s)` (if we add that accessor) to return the new value before any further ticks.

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Opaque-handle size reporting | A macro that users hand-edit when the struct changes | `_Static_assert(sizeof(struct) <= MAX, ...)` + `size_t spu94_state_size(void)` runtime fn | Compiler enforces sync; humans forget. |
| Alignment computation for caller buffers | Hand-computed alignment constants | `_Alignof(struct spu94_state)` + `alignas(SPU94_STATE_ALIGN_MAX)` doc example | C11 provides the primitives; rolling your own gets wrong on new targets. |
| Register-index iteration | Three separate `for` loops per register subset | Sequential enum `SPU94_REG_... = 0..32` + `SPU94_REG__COUNT` + table-driven dispatch | Adding a new register edits one table instead of six loops. |
| Bitmask primitives for pending-flush | Custom `count_set_bits` / `first_set_bit` helpers | `__builtin_ctzll` / `__builtin_popcountll` (gcc/clang/arm-none-eabi-gcc) with a portable fallback guarded by `#ifdef __GNUC__` | The compiler intrinsic maps to a single CPU instruction on every M1 target; a hand-rolled loop is slower and no more readable. Fallback is for MSVC, which we don't target in Phase 2 anyway. |
| 10⁶-step stateful property test | Bespoke C fuzzer | Python ctypes + `random` (or Hypothesis `RuleBasedStateMachine`) | The test is orthogonal to the library-under-test; using a separate process sharpens the test boundary. Python + ctypes is a three-line binding. |

**Key insight:** Every structure here has a canonical pattern. The Phase 2 value is nailing the 33-register table and the three swappable seams (write policy, mBASE side effect, extensibility taps); zero cleverness is needed in the surrounding scaffolding.

## Register Inventory

The canonical 33-register inventory for SPU-94. All addresses are the actual PS1 SPU I/O map addresses (byte offsets). Size is always 2 bytes (16-bit halfword, accessed as a unit). Signedness follows the D-02 structural distinction (compiler-enforced at the facade layer).

Source: [CITED: https://psx-spx.consoledev.net/soundprocessingunitspu/] — extracted directly. Paraphrase before any of these go into `docs/DECISIONS.md`, `docs/LEVERS-CATALOG.md`, or public-facing code comments per PROJECT.md nocash discipline.

### Routing / Gain Registers (outside 1F801DC0–DFE block)

| # | Enum (proposed) | Address | Signed? | Semantic | Zero-Value Meaning |
|---|-----------------|---------|---------|----------|---------------------|
| 0 | `SPU94_REG_vLOUT` | 0x1F801D84 | **i16** (signed) | Output volume, reverb → mix bus, left | Silence on reverb output left |
| 1 | `SPU94_REG_vROUT` | 0x1F801D86 | **i16** (signed) | Output volume, reverb → mix bus, right | Silence on reverb output right |
| 2 | `SPU94_REG_mBASE` | 0x1F801DA2 | **u16** (unsigned, address/8) | Reverb work-area base address (halfword-addressed, divided by 8 from byte offset in spec convention) | Base = 0 — all of SPU RAM is the reverb work area (unusual in practice; legal) |

### Reverb Block (1F801DC0–1F801DFE)

| # | Enum (proposed) | Address | Signed? | Semantic | Zero-Value Meaning |
|---|-----------------|---------|---------|----------|---------------------|
| 3 | `SPU94_REG_dAPF1` | 0x1F801DC0 | **u16** | APF1 delay offset | Zero delay (APF degenerates to direct path) |
| 4 | `SPU94_REG_dAPF2` | 0x1F801DC2 | **u16** | APF2 delay offset | Zero delay |
| 5 | `SPU94_REG_vIIR` | 0x1F801DC4 | **i16** | IIR reflection volume (-0x8000 = **anomaly**: negates final reverb result per ADR-0002) | Mutes IIR feedback path |
| 6 | `SPU94_REG_vCOMB1` | 0x1F801DC6 | **i16** | Comb tap 1 volume | Mutes tap 1 |
| 7 | `SPU94_REG_vCOMB2` | 0x1F801DC8 | **i16** | Comb tap 2 volume | Mutes tap 2 |
| 8 | `SPU94_REG_vCOMB3` | 0x1F801DCA | **i16** | Comb tap 3 volume | Mutes tap 3 |
| 9 | `SPU94_REG_vCOMB4` | 0x1F801DCC | **i16** | Comb tap 4 volume | Mutes tap 4 |
| 10 | `SPU94_REG_vWALL` | 0x1F801DCE | **i16** | Wall-reflection volume (feeds into IIR) | Mutes early wall path |
| 11 | `SPU94_REG_vAPF1` | 0x1F801DD0 | **i16** | APF1 gain | APF1 feedback is zero → degenerate delay |
| 12 | `SPU94_REG_vAPF2` | 0x1F801DD2 | **i16** | APF2 gain | APF2 feedback is zero → degenerate delay |
| 13 | `SPU94_REG_mLSAME` | 0x1F801DD4 | **u16** | Same-side left IIR reflection address | Reads from buffer start (+ current BufferAddress offset) |
| 14 | `SPU94_REG_mRSAME` | 0x1F801DD6 | **u16** | Same-side right IIR reflection address | Reads from buffer start |
| 15 | `SPU94_REG_mLCOMB1` | 0x1F801DD8 | **u16** | Comb 1 left address | Reads from buffer start |
| 16 | `SPU94_REG_mRCOMB1` | 0x1F801DDA | **u16** | Comb 1 right address | Reads from buffer start |
| 17 | `SPU94_REG_mLCOMB2` | 0x1F801DDC | **u16** | Comb 2 left address | Reads from buffer start |
| 18 | `SPU94_REG_mRCOMB2` | 0x1F801DDE | **u16** | Comb 2 right address | Reads from buffer start |
| 19 | `SPU94_REG_dLSAME` | 0x1F801DE0 | **u16** | Same-side left offset | Zero offset |
| 20 | `SPU94_REG_dRSAME` | 0x1F801DE2 | **u16** | Same-side right offset | Zero offset |
| 21 | `SPU94_REG_mLDIFF` | 0x1F801DE4 | **u16** | Diff-side left address | Reads from buffer start |
| 22 | `SPU94_REG_mRDIFF` | 0x1F801DE6 | **u16** | Diff-side right address | Reads from buffer start |
| 23 | `SPU94_REG_mLCOMB3` | 0x1F801DE8 | **u16** | Comb 3 left address | Reads from buffer start |
| 24 | `SPU94_REG_mRCOMB3` | 0x1F801DEA | **u16** | Comb 3 right address | Reads from buffer start |
| 25 | `SPU94_REG_mLCOMB4` | 0x1F801DEC | **u16** | Comb 4 left address | Reads from buffer start |
| 26 | `SPU94_REG_mRCOMB4` | 0x1F801DEE | **u16** | Comb 4 right address | Reads from buffer start |
| 27 | `SPU94_REG_dLDIFF` | 0x1F801DF0 | **u16** | Diff-side left offset | Zero offset |
| 28 | `SPU94_REG_dRDIFF` | 0x1F801DF2 | **u16** | Diff-side right offset | Zero offset |
| 29 | `SPU94_REG_mLAPF1` | 0x1F801DF4 | **u16** | APF1 left address | Reads from buffer start |
| 30 | `SPU94_REG_mRAPF1` | 0x1F801DF6 | **u16** | APF1 right address | Reads from buffer start |
| 31 | `SPU94_REG_mLAPF2` | 0x1F801DF8 | **u16** | APF2 left address | Reads from buffer start |
| 32 | `SPU94_REG_mRAPF2` | 0x1F801DFA | **u16** | APF2 right address | Reads from buffer start |

### Input-Side Gain Registers (end of block)

| # | Enum (proposed) | Address | Signed? | Semantic | Zero-Value Meaning |
|---|-----------------|---------|---------|----------|---------------------|
| (33 if we include) | `SPU94_REG_vLIN` | 0x1F801DFC | **i16** | Input volume into reverb, left | Mutes left input → reverb |
| (34 if we include) | `SPU94_REG_vRIN` | 0x1F801DFE | **i16** | Input volume into reverb, right | Mutes right input → reverb |

**⚠ DECISION POINT FOR PLANNER AND USER:** CONTEXT and REQUIREMENTS refer to "the 33 SPU reverb-affecting registers" — but the psx-spx reverb block contains **34** distinct 16-bit halfword registers if we count `vLIN` (0x1F801DFC) and `vRIN` (0x1F801DFE) separately. This inventory totals **33 (including both vLIN and vRIN)** if we unify `mBASE + vLOUT + vROUT` = 3 and 0x1F801DC0..DFA = 30 = 33; but `vLIN` + `vRIN` bring it to 35. Re-reading nocash: `vLOUT`/`vROUT` are reverb OUTPUT volumes (post-reverb→mix-bus); `vLIN`/`vRIN` are reverb INPUT volumes (pre-reverb from the mix). Both pairs participate in the reverb path.

The "33" count from the project documents (PROJECT.md, CONTEXT.md, ROADMAP.md, REQUIREMENTS.md) most likely originates from the psx-spx reverb block size count of **34 – 1 = 33** under some specific counting convention. Candidate interpretations:

1. **33 = everything in 0x1F801DC0..DFE (= 32 registers ending at vRIN) + mBASE (= 1) = 33.** Excludes vLOUT/vROUT on the grounds that those are the SPUCNT-side output gains, not reverb-block registers. This fits the "24 reverb-block registers plus mBASE, vLOUT, vROUT" language in PROJECT.md § Active/M1 only if we re-count 1F801DC0..DFA (= 30 registers, excluding vLIN and vRIN).
2. **33 = 30 (0x1F801DC0..DFA block without vLIN/vRIN) + 3 (mBASE, vLOUT, vROUT) = 33**, excluding vLIN and vRIN.
3. **33 = 32 (full 1F801DC0..DFE block including vLIN, vRIN) + 1 (mBASE) = 33**, excluding vLOUT and vROUT.

PROJECT.md § Active-Milestone-1 explicitly names: "the 24 reverb-block registers in the `1F801DC0–DFE` range (vIIR, vWALL, dAPF1/2, dCOMB1-4, vCOMB1-4, dLSAME/dRSAME, dLDIFF/dRDIFF, dLAPF1/2, dRAPF1/2, vAPF1/2, etc.) plus the additional SPU registers whose values affect reverb output (`mBASE` buffer base, `vLOUT`/`vROUT` output gains, and related control/routing registers outside the DC0–DFE block)". This lists specifically 24 "named" registers in the reverb block (corresponding to the dAPF1/dAPF2/vIIR/vCOMB1-4/vWALL/vAPF1/vAPF2 gain-type = 9, and mLSAME/mRSAME/mLCOMB1-4/mRCOMB1-4/dLSAME/dRSAME/mLDIFF/mRDIFF/dLDIFF/dRDIFF/mLAPF1/mRAPF1/mLAPF2/mRAPF2 address/delay-type = 20; **that is actually 29, not 24**, so the project-doc "24" is approximate, not exact).

**Recommendation to the planner and user:** Treat "33" as the locked target count and adopt **interpretation #2** — 30 reverb-block registers (0x1F801DC0..DFA) + mBASE + vLOUT + vROUT = 33. This matches the PROJECT.md enumeration direction (explicitly names vLOUT/vROUT; does not explicitly name vLIN/vRIN, so they are the candidates for deferral). Flag this to the user in Phase 2 discuss-checkpoint: **"The 0x1F801DFC (vLIN) and 0x1F801DFE (vRIN) registers are PS1 reverb-affecting halfwords that are not among the 33 counted in this project. Is that intentional, or should we count 35 instead of 33 and update PROJECT.md / REQUIREMENTS.md / CONTEXT.md / ROADMAP.md?"**

This is a planning-level decision, not a research one. The research finding is: **there are 35 reverb-path halfwords total; the project's "33" count is a subset convention that needs to be explicitly nailed down before Phase 2 locks its enum**. [ASSUMED: the intended subset is interpretation #2; the user confirms before the planner emits the enum.]

## BufferAddress Arithmetic

**Verbatim formula from nocash psx-spx, SPU Reverb Formula section:** `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)`. [CITED: https://psx-spx.consoledev.net/soundprocessingunitspu/]

**Decomposition:**

| Term | Meaning | Consequence |
|------|---------|-------------|
| `(BufferAddress + 2)` | Increment by 2 bytes = one 16-bit sample. The address space is byte-addressed in nocash formula convention. | The tick advances BufferAddress by one 16-bit halfword per stereo (L+R) tick. This matches the jsgroth description: "The current reverb buffer address is automatically incremented by 2 (one 16-bit sample) after processing both L and R samples." [CITED: https://jsgroth.dev/blog/posts/ps1-spu-part-3/] |
| `AND 7FFFEh` | Mask to the reverb-buffer region. `7FFFEh = 524286 = 0b0111_1111_1111_1111_1110`. | Enforces 16-bit halfword alignment (the bottom bit is always 0) and caps the address at 0x7FFFE = 512 KB - 2 bytes. This is the SPU-RAM top boundary minus one halfword. |
| `MAX(mBASE, ...)` | Floor the wrapped result to mBASE. | If the advance wraps past 0x7FFFE to a value below mBASE (because `AND 0x7FFFE` can produce any value in `[0, 0x7FFFE]`), snap back up to mBASE. |

**Addressing unit ambiguity.** Spec says "All memory addresses are relative to the current BufferAddress, and wrapped within mBASE..7FFFEh" and also "address registers divide SPU memory by 8". This is tricky. The formula operates on *byte* addresses (`+2` per tick); the register values stored in `m*` registers are halved-by-8 halfword addresses. Translation between the two is a SPU hardware convention that Phase 3 will address when it consumes `m*` register values — Phase 2 only needs the raw BufferAddress advance (which is byte-based per the formula).

For Phase 2's purposes:
- `BufferAddress` is an internal `uint32_t` in the state; interpret as byte offset into SPU RAM.
- Initial value at `spu94_init` time: **0** (the documented consequence of writing mBASE is that BufferAddress becomes mBASE — and since init writes mBASE=0, BufferAddress=0).
- The `spu94_reset` behavior: restore BufferAddress to current mBASE value (per the mBASE-write semantic — reset is equivalent to re-initialization).
- Wrap formula applied once per `spu94_tick()` call (one stereo tick).

**Reset-state behavior:** Before any mBASE write, on a freshly-initialized state, mBASE=0 and BufferAddress=0. Operating in this regime is legal but unusual (entire SPU RAM = reverb work area); real applications always write an mBASE before enabling reverb. [VERIFIED: psx-spx "Writing a value to mBASE does additionally set the current buffer address to that value" implies the initial case — the first mBASE write is the canonical initialization.]

## mBASE Side-Effect Evidence

**This is the load-bearing research question for ADR-0006 (D-10).** The preliminary D-09 lean was "floor-only — writing mBASE updates the wrap floor but does not reset BufferAddress." The evidence directly contradicts that.

### Evidence Table

| Source | Claim | Quote / Observation | Confidence |
|--------|-------|---------------------|------------|
| nocash psx-spx, "Reverb Volume and Address Registers (R/W)" subsection | BufferAddress is snapped to the new mBASE value on every mBASE write. | **"Writing a value to mBASE does additionally set the current buffer address to that value."** [CITED: https://psx-spx.consoledev.net/soundprocessingunitspu/ — verbatim, extracted via WebFetch 2026-04-19] | **HIGH** — this is the primary spec's own plain-language statement; no ambiguity. |
| nocash psx-spx, "SPU Reverb Formula" section | The wrap formula itself uses `MAX(mBASE, ...)` — so even WITHOUT an explicit snap, the *next* tick's BufferAddress is clamped up to mBASE. | `BufferAddress = MAX(mBASE, (BufferAddress+2) AND 7FFFEh)` | HIGH — reinforces that mBASE is a floor in the formula too, independently of the write side effect. |
| nocash psx-spx, "Setting up a new reverb effect" (Sony BIOS procedure) | Hardware convention is to disable reverb before changing reverb registers, then re-enable. Implies mid-stream reverb-register changes are not a spec'd operation, and hardware behavior in the mid-stream case is governed by whatever the register-write machinery does — not a separately-specified mid-stream semantic. | "turn off the reverb, set Depth to 0, make delay & feedback calculations, copy the preset to the effect registers, turn on the reverb, and set Depth to desired value." [CITED: https://psx-spx.consoledev.net/soundprocessingunitspu/] | HIGH — confirms the set-buffer-address side effect is the only spec-described mBASE behavior; no additional "buffer clear" or "crossfade" is documented. |
| hitmen c02 SPU documentation (secondary reference) | No additional language on mBASE write behavior. Lists mBASE register at 0x1F801DA2 but does not document the side effect. | Absent. [CITED: https://hitmen.c02.at/files/docs/psx/spu.txt via WebFetch 2026-04-19] | — (absence of evidence; does not contradict nocash). |
| jsgroth PS1 SPU Part 3 — Reverb (secondary writeup) | Does not cover mBASE mid-stream write behavior in the passage quoted. | "does not cover mid-stream mBASE writes" [CITED: https://jsgroth.dev/blog/posts/ps1-spu-part-3/ via WebFetch 2026-04-19] | — (absence of evidence). |
| PSX homebrew examples exercising mid-stream mBASE | None findable. Documented reverb-setup procedures (Sony SDK library calls SpuSetReverbDepth etc., Sound Artist Tool documentation) all follow the BIOS convention: disable reverb → rewrite registers → re-enable. No demo or homebrew is known to write mBASE during active reverb processing. | — | LOW (absence of evidence; real PSX code does not do this, so the behavior is untested in the wild). |
| Mednafen (GPLv2, output-only witness) | NOT READ per project licensing posture. Would be readable as black-box witness, but doing so requires running a PSX homebrew and capturing audio — beyond Phase 2 scope and not necessary because the primary spec is unambiguous. | — | N/A |
| lv2-psx-reverb (GPLv3, output-only witness) | Same — not read. | — | N/A |
| DuckStation (output-only witness) | Same — not read. | — | N/A |

### Recommendation for ADR-0006

**Lock ADR-0006 to: "Writing mBASE sets BufferAddress = mBASE."** This is the verbatim behavior documented by the primary spec. No additional side effect (buffer clear, crossfade, tick-alignment delay) is documented; SPU-94 does not invent any.

**Implementation notes:**
- The write is immediate (the IMMEDIATE policy in the write-policy table is correct for mBASE). The BufferAddress update is a consequence of the write, not a separately-queued event.
- `spu94_set_mBASE(s, value)` → state updates mBASE AND BufferAddress atomically, both visible to the next observation (D-23 observability) and to the next tick.
- The side-effect is implemented via the seam in D-11: `state->mbase_handler(state, new_value)` with the default handler being `spu94_mbase_handler_snap` (does the snap). Future Controllers can re-point the handler to `spu94_mbase_handler_floor_only` (D-09's preliminary behavior) or `spu94_mbase_handler_clear_and_snap` (a hypothetical "re-zero buffer" variant) without touching core code.

### Contradiction Warning

**D-09's preliminary lean ("floor-only") is contradicted by primary-source evidence.** The planner must:
1. Flag this in the plan-checker step.
2. Present to the user at the Phase 2 execution boundary: "Research contradicts D-09. Primary source (psx-spx) states mBASE writes snap BufferAddress to the new mBASE. Recommend ADR-0006 = snap, not floor-only. Confirm before ADR-0006 lands in docs/DECISIONS.md."
3. Ensure the seam in D-11 is structured so that the ADR-0006 decision is swappable — if the user overrides the research and prefers floor-only, it's a one-line change in the handler slot.

**Assumption flagged for Open Questions:** [ASSUMED] That the nocash language "set the current buffer address to that value" means exactly BufferAddress = mBASE (literal equality) and not, e.g., BufferAddress = MAX(mBASE, current) or some tick-aligned variant. The plain-language reading is the literal-equality interpretation; no evidence contradicts it. Confidence: HIGH.

## Write-Timing Policy

nocash is silent on register-write timing. No direct spec language answers "does a mid-tick write to dCOMB1 take effect on THIS tick or the NEXT tick?" This is precisely why D-04's split policy is a swappable seam (D-05).

### Rationale Chain for the D-04 Split

The split policy (v\* immediate, d\*/m\* tick-latched) is defensible on structural grounds:

1. **Gain-type registers (v\*) participate in per-sample multiplies.** The hardware multiplier reads the register at the multiply site. A mid-tick write to a v\* register is visible to any multiply that runs after the write. Modeling this as "immediate" matches the natural data flow of a synchronous-register read-at-multiply-site hardware block. Faithful modeling of the real chip — where the multiplier has no buffered copy of the gain register — would be "immediate". SPU-94's software model preserves this.

2. **Address/delay registers (d\*/m\*) index memory.** The reverb algorithm's correctness depends on consistent address values across the L half-cycle and the R half-cycle of a single 44.1 kHz paired tick (nocash: "The reverb hardware spends one 44100h cycle on left calculations, and the next 44100h cycle on right calculations"). A mid-tick address change would corrupt the L/R relationship, producing audible asymmetries and potentially out-of-bounds reads in ways the spec does not describe. "Tick-latched" is the principled defensive choice: the register value is captured at tick start and held constant for the full stereo tick.

3. **Sony BIOS procedure implies mid-stream writes are not a supported operation.** The "turn off reverb → rewrite → turn on" BIOS convention is strong evidence that Sony hardware engineers did not expect mid-stream mass-register reconfiguration. SPU-94's job is to pick a defensible behavior that (a) does not crash, (b) is bit-faithful in the single-tick case, (c) matches the observable structure of the chip.

4. **The split enables D-23 observability.** Immediate-policy registers read back as `get_reg == get_reg_pending` always. Tick-latched registers potentially differ — the pending read-back is what the shadow holds. This gives Controllers a lever to see the "what's queued" state without altering behavior.

### Per-Register Policy Table (Phase 2 default, ADR-0005)

Based on the 33-register inventory above (interpretation #2 — user-confirmable):

| Register | Default Policy | Rationale |
|----------|----------------|-----------|
| vLOUT, vROUT | IMMEDIATE | Gain-type, output path — mid-tick write is read by the next output-mix multiply |
| mBASE | IMMEDIATE + side-effect | The BufferAddress snap is a stateful side effect. The mBASE config value itself is immediate; the side effect (BufferAddress=mBASE) is triggered on the write. |
| dAPF1, dAPF2 | TICK_LATCHED | Address/delay — captured at tick start |
| vIIR, vCOMB1-4, vWALL, vAPF1, vAPF2 | IMMEDIATE | Gain-type |
| mLSAME, mRSAME, mLCOMB1-4, mRCOMB1-4, mLDIFF, mRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2 | TICK_LATCHED | Address |
| dLSAME, dRSAME, dLDIFF, dRDIFF | TICK_LATCHED | Delay |

**Implementation seam:** The table lives in `src/spu94/spu94_write_policy.c` as a `static const spu94_write_policy_t` array indexed by `spu94_reg_t`. The Controllers milestone swaps it for alternative tables without touching core code.

**Assumption flagged:** [ASSUMED] That all v\* registers are appropriately IMMEDIATE and all m\*/d\* registers are appropriately TICK_LATCHED. The spec does not document per-register timing. Confidence: MEDIUM — the structural argument is strong but not spec-backed. The seam exists (D-05) precisely so this can be revisited without core rewrites.

## State-Allocation Pattern

See "Pattern 1: Shell-Type Opaque Handle with Caller-Allocated Storage" above for the template. Key planning-time decisions the planner must nail:

1. **Exact value of `SPU94_STATE_SIZE_MAX`.** Phase 2 implementation-time decision based on actual struct content. Suggested starting value: 16384 bytes (16 KB) — round up to next power of two after actual struct sizing. The work buffer (separate, D-13) is MUCH larger (up to 512 KB for full SPU RAM emulation); that's not part of state size.
2. **Alignment requirement.** Recommended: 16 bytes (`SPU94_STATE_ALIGN_MAX = 16`). Covers `int64_t`, `double` (not used but defensive), future SIMD-alignment on x86/ARM. MCUs generally have 8-byte alignment; 16 is generous.
3. **`spu94_init` contract.** On bad inputs (NULL buf, wrong size, misaligned): return NULL. Do not zero or touch the buffer. CONTEXT § Claude's Discretion leaves the exact init vs reset boundary open.
4. **`spu94_reset` contract.** Zero all registers, zero the work buffer, restore BufferAddress = mBASE (0), clear pending_mask. Does NOT change caller-provided memory pointers.
5. **`spu94_destroy` contract.** Zero the state bytes (for security-hygiene purposes) and invalidate any internal pointers. Does not free the buffer (caller owns).
6. **Work-buffer size requirement.** Caller provides a `work_buf_size` of at least some minimum (e.g., 2 bytes to support degenerate all-zero preset, or `SPU94_WORK_BUF_MIN` constant). For full PS1 fidelity with preset-appropriate sizes, 512 KB is the maximum. Phase 2 validates `work_buf_size >= some minimum` and stores the size.

## C99 Freestanding Conformance

Public headers (`spu94.h`, `spu94_q15.h`, `spu94_registers.h`, `spu94_state.h`) must compile under `-std=c99 -pedantic` and through an `extern "C"` C++ consumer (API-07). Constraints:

| Pattern | Allowed? | Notes |
|---------|----------|-------|
| `<stdint.h>` | YES | Freestanding C99+. Already used in Phase 1. |
| `<stddef.h>` | YES | Freestanding. For `size_t`. |
| `<limits.h>` | YES | Freestanding. For `INT16_MIN/MAX`. |
| `<stdbool.h>` | YES | Freestanding since C99. |
| `<stdalign.h>` (for `alignas`) | **C11+** | Freestanding since C11. Phase 1 sets `CMAKE_C_STANDARD 11` — OK. Note: `_Alignas` keyword works in C11 without the header; `alignas` macro requires the header. |
| `<stdio.h>`, `<stdlib.h>`, `<string.h>` | **NO in public headers** | Not freestanding. Allowed in private `.c` TUs only if genuinely needed (which Phase 2 doesn't — no `memset`/`memcpy` needed when fields are set by direct assignment; a loop-based zero-fill is fine for state reset). |
| `_Static_assert` | YES | C11+. Already used in Phase 1. |
| VLAs | NO | Makes header inclusion depend on runtime values. Don't use. |
| Flexible array members | NO | In an opaque type this is impossible anyway (caller doesn't know the struct layout). |
| `restrict` qualifier | YES (C99+) | Fine; document semantics when used. |
| Bitfields | Avoid | Portability across compilers + endianness edge cases. Not needed for 33 register slots. |
| `__attribute__((always_inline))` | NO | D-07 from Phase 1 forbids this explicitly. Rely on `static inline` and compiler optimization. |
| Compound literals | YES (C99+) | Fine. |

**`extern "C"` wrapping pattern:** Phase 1 already has this in `include/spu94/spu94.h`. Every new public header follows the same pattern:

```c
#ifndef SPU94_REGISTERS_H
#define SPU94_REGISTERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ... header body ... */

#ifdef __cplusplus
}
#endif

#endif /* SPU94_REGISTERS_H */
```

**Testing the C99 conformance claim.** Add a CI step or a test that compiles `spu94.h` under `-std=c99 -pedantic -Werror -Wall -Wextra` via a tiny `tests/api/c99_consumer.c`. Additionally, add a `tests/api/cxx_consumer.cpp` with `#include <spu94/spu94.h>` compiled under `-std=c++11 -Werror -pedantic`. Both pass means API-07 is honored. This may be a Phase 1 plan-04-style gap-closure task in Phase 2, or rolled into Plan 2-01. Planner's call.

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| gcc | Phase 1 build, Phase 2 continues | ✓ | 15.2.0 | clang (not installed locally, but CI has it per Phase 1) |
| clang | CI UBSan build | ✗ locally | — | CI runs clang; local dev works with gcc alone |
| cmake | build | ✓ | 3.31.6 | — |
| Unity | test framework | ✓ | vendored in Phase 1 | — |
| Python 3.10+ | `tests/python/fuzz_buffer.py` harness | ✓ | 3.13.7 | Skip Python-side fuzz (weakens SC 3 but C-side tests still cover formula corners) |
| pytest | optional Python test runner | ✓ | 9.0.3 | Run fuzz script standalone: `python3 tests/python/fuzz_buffer.py` |
| hypothesis (Python) | OPTIONAL shrinking in fuzz harness | unknown | — | Use pure-random fuzz (Phase 2 default); install only if bug shrinking is needed |
| `arm-none-eabi-gcc` | Phase 8 only (not Phase 2) | N/A for Phase 2 | — | — |
| `readelf`, `nm` (linker symbol check for SC 1) | SC 1 `malloc`/`free` reference check | ✓ (standard binutils) | — | — |

**Missing dependencies with no fallback:** None.

**Missing dependencies with fallback:**
- clang locally: CI covers it; local dev uses gcc. Not a Phase 2 blocker.
- hypothesis: Phase 2 default is pure-random fuzz; no install needed. If install is later preferred, it's a `pip install hypothesis` in the dev environment.

## Common Pitfalls

### Pitfall 1: Opaque-Handle `sizeof` Drift

**What goes wrong:** Real `struct spu94_state` grows beyond `SPU94_STATE_SIZE_MAX`; MCU callers segfault because their stack buffer is too small.

**Why it happens:** Developer adds a field to the private struct without bumping the public macro. No compile-time enforcement.

**How to avoid:** `_Static_assert(sizeof(struct spu94_state) <= SPU94_STATE_SIZE_MAX, "...")` in the TU that defines the struct. Fails the build immediately if the macro is too small.

**Warning signs:** Any PR that adds a field to `struct spu94_state` without bumping `SPU94_STATE_SIZE_MAX`. Add to code-review checklist.

### Pitfall 2: Alignment Violation on Caller-Provided Buffer

**What goes wrong:** Caller does `unsigned char buf[SPU94_STATE_SIZE_MAX]; spu94_init(buf, ...)`. If `buf` is misaligned (possible on stack frames that don't guarantee 16-byte alignment), subsequent memory accesses in `spu94_state` members that require 16-byte alignment trip UBSan or (worse) silently corrupt data.

**Why it happens:** C99 doesn't automatically align arbitrary arrays.

**How to avoid:** Document the requirement explicitly: caller must use `alignas(16) unsigned char buf[SPU94_STATE_SIZE_MAX]` or equivalent. `spu94_init` validates: `if (((uintptr_t)buf) % SPU94_STATE_ALIGN_MAX != 0) return NULL;`.

**Warning signs:** Any tutorial or test that uses a bare `char buf[SIZE]` without alignas. Catch in code review.

### Pitfall 3: BufferAddress Halfword-vs-Byte Confusion

**What goes wrong:** Implementer reads "register addresses divide SPU memory by 8" and applies `/8` to BufferAddress. The wrap formula's `+2` is then wrong (should be `+1` in halfword/8 units? `+2/8`?). Everything drifts.

**Why it happens:** nocash uses two different addressing conventions in nearby sections. The register values stored in `m*` registers are byte-address/8 (they are halved-by-8 to fit 64 KB worth of halfword addresses into 16 bits). But the `BufferAddress` the wrap formula manipulates is a byte address (`+2` per stereo tick).

**How to avoid:** Phase 2 `BufferAddress` is a raw byte address, stored as `uint32_t` in the state. Maintain a clear doc comment on the BufferAddress field: "byte offset into SPU RAM; advance formula is `MAX(mBASE, (BufferAddress+2) AND 0x7FFFE)` where mBASE is also a byte address; the halving-by-8 of `m*` register values is Phase 3's concern when consuming them."

**Warning signs:** Any Phase 2 test that expects BufferAddress values in units other than bytes.

### Pitfall 4: Pending-Writes Leak Into Wrong Tick

**What goes wrong:** Pending-write flush runs at the wrong time — e.g., at tick end instead of tick start, or after the first half of a stereo tick but before the second. Tick-latched register values applied mid-tick corrupt the L/R asymmetry.

**Why it happens:** Confusion about "tick" boundaries. In SPU-94 a "tick" is one stereo L+R pair (one 22.05 kHz step, two 44.1 kHz cycles).

**How to avoid:** Flush pending-writes at the start of `spu94_tick()`, before any reverb computation. Exactly one call site. Code-review rule: the `apply_pending_writes(state)` function is called in exactly one place. Add a cppcheck rule or grep guard in a follow-up phase if needed.

**Warning signs:** Any phase-3 PR that calls `apply_pending_writes` from more than one location, or that interleaves it with reverb computation.

### Pitfall 5: Forgetting the mBASE Side Effect at Reset Time

**What goes wrong:** `spu94_reset(state)` zeros the state but forgets to snap BufferAddress to mBASE (both = 0 after reset, so the final state is correct, but if the reset logic preserves mBASE while zeroing BufferAddress, the invariant `BufferAddress >= mBASE` breaks).

**Why it happens:** `spu94_reset` is hand-written field-by-field; easy to miss dependencies.

**How to avoid:** Implement `spu94_reset` as `memset-style zero of the whole state, then init-like setup of invariants`. Or: use `spu94_init` internally from `spu94_reset` to guarantee post-reset invariants match post-init invariants.

**Warning signs:** Any reset test that leaves BufferAddress and mBASE in different values.

### Pitfall 6: `_i16` / `_u16` Type Mismatch Silently Compiling

**What goes wrong:** `spu94_set_reg_u16(s, SPU94_REG_vIIR, 0xFFFF)` compiles and stores 0xFFFF into vIIR. But vIIR is signed; the user probably meant -1, and the write through the "wrong" typed accessor defeats the D-02 compiler-enforced guarantee.

**Why it happens:** C implicit conversions from `int` to `uint16_t` are silent.

**How to avoid:** The engine-layer accessors validate the register's expected signedness: `spu94_set_reg_u16` on a signed register returns `SPU94_UNKNOWN_REG` (or a new `SPU94_TYPE_MISMATCH` status). Facade-layer wrappers (one per register) naturally use the right type because each wrapper is bound to one register. Pitfall applies only to engine-layer callers (Controllers, Python binding) and is fixed by the runtime type check + status return.

**Warning signs:** Callers using `_u16` on a `v*` register or `_i16` on a `d*`/`m*` register. Code review catches this.

### Pitfall 7: Python Fuzz Harness Runs Against Stale Library Build

**What goes wrong:** Developer edits `src/spu94/*.c`, reruns `python3 tests/python/fuzz_buffer.py`, but the fuzz harness loaded the old `libspu94.so` into ctypes from the previous build. False green.

**Why it happens:** ctypes doesn't invalidate its loaded library on filesystem change.

**How to avoid:** The fuzz harness loads the library path from an environment variable or computes it relative to the build dir (`build/src/spu94/libspu94.so`). A `cmake --build build && python3 tests/python/fuzz_buffer.py` sequence is idiomatic and catches this. If making the fuzz harness a `pytest` test, structure it so the CMake build runs as a fixture.

**Warning signs:** Fuzz test passes on CI but local dev sees different behavior. Always rebuild before running the fuzz.

### Pitfall 8: Linker-Symbol Check for SC 1 False-Negatives

**What goes wrong:** SC 1 requires a linker-level symbol check confirming `malloc`/`free` are not referenced from the core library. A too-loose check (e.g., `nm libspu94.so | grep malloc`) can miss transitive references or produce false negatives if the library is stripped.

**Why it happens:** `nm` on a stripped shared library may not show all symbols; dynamic-relocation checks need `-D` or `readelf -d`.

**How to avoid:** Use both `nm -u libspu94.so` (undefined symbols = what the library imports) and `readelf -d libspu94.so` (runtime dependencies). Grep both outputs for `malloc|calloc|realloc|free`. The Phase 1 grep-guard only checks source text; the Phase 2 linker check is different and complementary.

**Warning signs:** SC 1 test passes but a `readelf --all` manual inspection shows a libc reference to malloc.

## Test-Harness Strategy

### Per-Register Unit Tests (TEST-02, SC 4)

Pattern follows Phase 1 D-10 (inline hand-computed reference tables). One test TU per test class:

- `tests/unit/registers/test_register_roundtrip.c` — 33-row table of `{reg, write_value, expected_read_value}`. Exercises every register. Passes if the simple-case round-trip works (write → immediate get → value matches, allowing for tick-latching via `get_pending`).
- `tests/unit/registers/test_register_types.c` — Signed/unsigned preservation. For each register, write boundary values and assert the read-back preserves them (`INT16_MIN` on an i16 register reads back as -32768; writing 0xFFFF to a u16 register reads back as 65535).
- `tests/unit/registers/test_register_policy.c` — Split-policy behavior. For v\* registers, assert `get == get_pending == written_value` immediately. For d\*/m\* registers, assert `get != get_pending` after a mid-tick write (old value active, new value pending), and `get == get_pending == written_value` after `spu94_tick()` commits the pending write.
- `tests/unit/registers/test_register_edges.c` — Edge values per register. Zero-meaningful (each register has a documented zero semantic; test that the behavior is correct). MIN/MAX/wraparound for each register's type. vIIR=-0x8000 is a register-level test that checks the write/read round-trip (NOT the negation anomaly — that's Phase 3 TEST-06).

### Buffer Wrap Tests (CORE-03, SC 3)

- `tests/unit/buffer/test_buffer_wrap.c` — Table-driven wrap formula corners. Verifies that after N advances starting from mBASE=X, BufferAddress tracks `MAX(mBASE, (prev+2) AND 0x7FFFE)` exactly. Test cases: advance from 0, from 0x7FFFC (one step before top), from 0x7FFFE (at top, next advance wraps), from mBASE = 0x40000 so wrap floors to mBASE instead of 0. Hand-computed reference.
- `tests/unit/buffer/test_buffer_mbase.c` — mBASE-write side-effect tests. Pre-write some BufferAddress state via a few `spu94_tick()` calls. Write mBASE to a new value. Assert BufferAddress == new mBASE (the snap). Also test mBASE write during first-ever state — verify BufferAddress = mBASE = 0 initial state.

### Python Fuzz Harness (SC 3, 10⁶ steps)

- `tests/python/fuzz_buffer.py` — Single-file ctypes driver. Loads `libspu94.so`, allocates a state buffer, initializes it, runs a loop of 10⁶ iterations of (randomly pick operation: mBASE write, tick, register write) and asserts after each step: `state.buffer_address >= state.mBASE AND state.buffer_address <= 0x7FFFE AND state.buffer_address is halfword-aligned (bit 0 == 0)`. The state field read-back is either via `spu94_get_buffer_address(state)` public accessor (Phase 2 adds it per D-23 observability) or via `spu94_snapshot_registers` extended to include BufferAddress. On failure, the harness prints the last ~10 operations for manual inspection.

**Default approach: pure-random.** No Hypothesis dependency. Python stdlib `random` seeds reproducibly via a fixed seed (fuzz harness records the seed in CI output so failures are replayable).

**Upgrade path:** If failures are found that don't shrink usefully with the last-N-operations printout, upgrade to Hypothesis `RuleBasedStateMachine` for automatic shrinking.

### Linker Symbol Check (SC 1)

A small shell script `scripts/ci/verify-no-heap-symbols.sh` that runs:

```bash
# Fail if libspu94.so imports malloc, calloc, realloc, or free.
if nm -u build/src/spu94/libspu94.so 2>/dev/null | grep -qE '\b(malloc|calloc|realloc|free)\b'; then
    echo "FAIL: libspu94 references heap functions" >&2
    exit 1
fi
if readelf -d build/src/spu94/libspu94.so 2>/dev/null | grep -qE '\b(malloc|calloc|realloc|free)\b'; then
    echo "FAIL: libspu94 dynamic relocations reference heap functions" >&2
    exit 1
fi
echo "OK: libspu94 heap-free" >&1
```

Wired into CI as a new job step after build.

## Code Examples

### Example 1: Register Enum + Hardware Offset Table

```c
/* include/spu94/spu94_registers.h — PUBLIC */
typedef enum {
    SPU94_REG_vLOUT = 0,
    SPU94_REG_vROUT,
    SPU94_REG_mBASE,
    SPU94_REG_dAPF1,
    SPU94_REG_dAPF2,
    SPU94_REG_vIIR,
    SPU94_REG_vCOMB1,
    /* ... 26 more ... */
    SPU94_REG_mRAPF2,
    SPU94_REG__COUNT    /* sentinel, = 33 */
} spu94_reg_t;

/* src/spu94/spu94_registers.c */
static const uint16_t spu94_reg_hw_offsets[SPU94_REG__COUNT] = {
    [SPU94_REG_vLOUT]  = 0x1D84,
    [SPU94_REG_vROUT]  = 0x1D86,
    [SPU94_REG_mBASE]  = 0x1DA2,
    [SPU94_REG_dAPF1]  = 0x1DC0,
    /* ... */
};
uint16_t spu94_reg_hw_offset(spu94_reg_t reg) {
    if (reg < 0 || reg >= SPU94_REG__COUNT) return 0xFFFF;
    return spu94_reg_hw_offsets[reg];
}
```

Full-address 0x1F801DC0 vs designated offset 0x1DC0: we store the 16-bit offset into the SPU I/O region and publish it as such; consumers add 0x1F801D00 if they want a PS1 absolute address. Convention choice — flag for planner discretion.

### Example 2: Typed Engine-Layer Setter with Status Return

```c
/* include/spu94/spu94_registers.h — PUBLIC */
typedef enum {
    SPU94_OK = 0,
    SPU94_CLAMPED = 1,
    SPU94_UNKNOWN_REG = 2,
    SPU94_TYPE_MISMATCH = 3,   /* discretion — internal ID beyond committed three */
} spu94_result_t;

spu94_result_t spu94_set_reg_i16(spu94_state *s, spu94_reg_t reg, int16_t value);
spu94_result_t spu94_set_reg_u16(spu94_state *s, spu94_reg_t reg, uint16_t value);
int16_t        spu94_get_reg_i16(const spu94_state *s, spu94_reg_t reg);
uint16_t       spu94_get_reg_u16(const spu94_state *s, spu94_reg_t reg);
int16_t        spu94_get_reg_i16_pending(const spu94_state *s, spu94_reg_t reg);
uint16_t       spu94_get_reg_u16_pending(const spu94_state *s, spu94_reg_t reg);
```

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| malloc-based opaque handles | caller-allocated shell-type | Since ~2015 (LZ4/zstd era); always standard for realtime / embedded C libraries | SPU-94 mandatory per D-12; no alternative. |
| Macro-generated register wrappers | Hand-written `static inline` wrappers | Always debatable; community moving back to hand-written for auditability | D-03 locks hand-written. |
| C99 everywhere | C11 for `_Static_assert`, `_Alignas`, `_Alignof` | Since 2011 ratification | Phase 1 sets `CMAKE_C_STANDARD 11`; API-07 uses C99 for header surface compatibility. |
| bespoke property-test harness in C | Python + ctypes + (optionally) Hypothesis | Since Hypothesis ~2016 | SPU-94 uses pure-random first, Hypothesis as upgrade path. |

**Deprecated/outdated:**
- `__attribute__((always_inline))` for "hot path" functions — rely on link-time optimization + `static inline` (Phase 1 D-07 already forbids `always_inline` macros).
- Macro-generated register-map headers (e.g., X-macros) — readable once, maintenance hazard forever; D-03 rejects.

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | The "33 registers" target count in project docs corresponds to interpretation #2: 30 reverb-block registers (0x1F801DC0..DFA) + mBASE + vLOUT + vROUT, excluding vLIN (0x1F801DFC) and vRIN (0x1F801DFE). | Register Inventory § Decision Point for Planner | HIGH — if vLIN/vRIN are supposed to be included, the count is actually 35, and PROJECT/REQUIREMENTS/CONTEXT/ROADMAP need updating. User must confirm. |
| A2 | The nocash psx-spx sentence "Writing a value to mBASE does additionally set the current buffer address to that value" means literal `BufferAddress := new_mBASE` assignment, not some tick-delayed or MAX-wrapped variant. | mBASE Side-Effect Evidence | LOW — plain-language reading is unambiguous; no evidence contradicts. |
| A3 | The D-04 split policy (v\* immediate, d\*/m\* tick-latched) is the "bit-faithful" choice absent spec guidance. | Write-Timing Policy | MEDIUM — structurally defensible but not spec-backed. The seam in D-05 exists precisely so this can be revisited. |
| A4 | Initial BufferAddress at `spu94_init` time is 0 (because init sets mBASE=0 and the mBASE-write side effect makes BufferAddress=mBASE). | BufferAddress Arithmetic § Reset-state behavior | LOW — follows from A2 applied to the init case. |
| A5 | Python 3.10 is the right floor for the optional Python fuzz harness. | Standard Stack | LOW — 3.10 is well past EOL for anything older; any realistic dev env has 3.10+. |
| A6 | The register hardware-offset convention SPU-94 publishes is the 16-bit low-word (e.g., 0x1DC0 for vIIR rather than full 0x1F801DC0). | Code Examples | LOW — pure planner discretion; either convention works. |
| A7 | `SPU94_STATE_SIZE_MAX = 16384` is a reasonable starting upper bound. | State-Allocation Pattern | LOW — the `_Static_assert` catches the bound at build time; starting value only affects stack/static footprint on MCUs. |
| A8 | nocash is silent on register-write timing because the BIOS procedure (disable → rewrite → enable) makes mid-stream writes an out-of-spec operation. | Write-Timing Policy | LOW — this is inference from Sony's own BIOS convention; the inference is corroborated by the Sound Artist Tool documentation search result. |
| A9 | The "shell type" opaque-handle pattern with `_Static_assert` size sync is the preferred idiom over malloc-based handles for realtime-audio C libraries. | Architecture Patterns § Pattern 1 | LOW — cited from multiple authoritative sources (Collet zstd writeup, Memfault, mbedded.ninja). Standard practice in embedded C. |

## Open Questions (RESOLVED)

1. **Register-count interpretation (vLIN/vRIN inclusion)** — see Assumption A1. **RESOLVED 2026-04-19: include vLIN + vRIN → count is 35.** User decision during plan-phase. All project docs (PROJECT.md, REQUIREMENTS.md, ROADMAP.md, CONTEXT.md) updated in commit `a9f4686`.
2. **mBASE side-effect final decision** — see "mBASE Side-Effect Evidence § Recommendation for ADR-0006." **RESOLVED 2026-04-19: snap-on-write.** User decision during plan-phase. CONTEXT.md D-09/D-10 updated; ADR-0006 entry documented by Plan 04 Task 2.
3. **Alignment guarantee for caller-provided buffer** — CONTEXT § Deferred flags this as planner-to-decide. **RESOLVED by Plan 01 Task 1:** 16-byte alignment committed in `spu94_init` doc-comment; enforced at runtime.
4. **Thread-safety stance** — CONTEXT § Deferred flags this as Phase 2 or Phase 5. **RESOLVED by Plan 01 Task 1:** `spu94_init` doc-comment reads "A `spu94_state` is not thread-safe; concurrent access from multiple threads requires external synchronization." Phase 5 may expand this for API-06.
5. **mBASE side-effect seam exposure** — CONTEXT § Claude's Discretion asks whether the seam is exposed as a function pointer in Phase 2 or kept internal. **RESOLVED by Plan 04 Task 1:** internal only (single symbol `spu94_mbase_on_write`), not a runtime-swappable function-pointer field. Controllers promotes it via a later additive ADR.
6. **Register-name string accessor prefix convention** — CONTEXT § Claude's Discretion. **RESOLVED by Plan 02 Task 1:** returns `"vIIR"` (not `"SPU94_REG_vIIR"`).

## Validation Architecture

### Test Framework

| Property | Value |
|----------|-------|
| Framework (C unit tests) | Unity (vendored in Phase 1) |
| Framework (fuzz harness) | Python 3.10+ with ctypes + (pure-random) Random; no Hypothesis dependency in default config |
| Config file | `tests/CMakeLists.txt` → `tests/unit/CMakeLists.txt` → per-module `CMakeLists.txt` |
| Quick run command | `cmake --build build --target test_q15 test_buffer_wrap test_buffer_mbase test_register_roundtrip && ctest --test-dir build --output-on-failure -R 'q15|buffer|registers'` |
| Full suite command | `cmake --build build && ctest --test-dir build --output-on-failure && python3 tests/python/fuzz_buffer.py` |

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|--------------|
| CORE-03 | BufferAddress wrap formula | unit (C) | `ctest --test-dir build -R buffer_wrap -x` | ❌ Wave 0 — `tests/unit/buffer/test_buffer_wrap.c` |
| CORE-03 | BufferAddress wrap invariant over 10⁶ steps | integration (Python) | `python3 tests/python/fuzz_buffer.py` | ❌ Wave 0 — `tests/python/fuzz_buffer.py` |
| CORE-03 | mBASE-write side effect (BufferAddress snap) | unit (C) | `ctest --test-dir build -R buffer_mbase` | ❌ Wave 0 — `tests/unit/buffer/test_buffer_mbase.c` |
| CORE-04 | All 33 registers writable/readable via typed enum | unit (C) | `ctest --test-dir build -R register_roundtrip` | ❌ Wave 0 — `tests/unit/registers/test_register_roundtrip.c` |
| CORE-04 | Signed/unsigned preservation (i16 vs u16) | unit (C) | `ctest --test-dir build -R register_types` | ❌ Wave 0 — `tests/unit/registers/test_register_types.c` |
| CORE-04 | Per-register edge cases (MIN/MAX/zero-meaningful) | unit (C) | `ctest --test-dir build -R register_edges` | ❌ Wave 0 — `tests/unit/registers/test_register_edges.c` |
| CORE-10 | Split write policy (v\* immediate, d\*/m\* tick-latched) + pending read-back | unit (C) | `ctest --test-dir build -R register_policy` | ❌ Wave 0 — `tests/unit/registers/test_register_policy.c` |
| API-01 | Opaque handle with caller-allocated state, no heap | linker-symbol (shell) | `bash scripts/ci/verify-no-heap-symbols.sh` | ❌ Wave 0 — `scripts/ci/verify-no-heap-symbols.sh` |
| API-02 | Init/reset/destroy lifecycle with caller-provided work buffer | unit (C) | `ctest --test-dir build -R state_lifecycle` | ❌ Wave 0 — `tests/unit/state/test_state_lifecycle.c` (new subdirectory) |
| API-04 | Typed register R/W (same as CORE-04 coverage) | — | (covered by CORE-04 tests) | — |
| API-07 | `spu94.h` compiles under `-std=c99 -pedantic` and `extern "C"` C++ consumer | compile-only | `cmake --build build --target test_c99_consumer test_cxx_consumer` | ❌ Wave 0 — `tests/api/c99_consumer.c` + `tests/api/cxx_consumer.cpp` + CMakeLists updates |
| API-09 | Core library depends only on freestanding C subset | manual inspection + grep-guard continuation | (already covered by Phase 1 grep-guard) | ✅ existing (Phase 1 `scripts/ci/grep-guard.sh`) |
| TEST-02 | Register-level unit tests — all 33 in isolation | unit (C) — aggregate | `ctest --test-dir build -R register` | ❌ Wave 0 (aggregate of above) |

### Sampling Rate

- **Per task commit:** `cmake --build build && ctest --test-dir build --output-on-failure -R 'q15|buffer|registers|state|api'`
- **Per wave merge:** `cmake --build build && ctest --test-dir build --output-on-failure && python3 tests/python/fuzz_buffer.py && bash scripts/ci/verify-no-heap-symbols.sh`
- **Phase gate:** Full suite green before `/gsd-verify-work` — all of the above plus the grep-guard + verify-flags + UBSan jobs from Phase 1 continue to pass.

### Wave 0 Gaps

Test files to create (in preference order, matching a likely plan order):

- [ ] `tests/unit/state/CMakeLists.txt` + `tests/unit/state/test_state_lifecycle.c` — covers API-01, API-02 (lifecycle + heap-free proof)
- [ ] `scripts/ci/verify-no-heap-symbols.sh` + CI wiring — covers API-01 linker-symbol check (SC 1)
- [ ] `tests/unit/buffer/CMakeLists.txt` + `tests/unit/buffer/test_buffer_wrap.c` — covers CORE-03 wrap corners
- [ ] `tests/unit/buffer/test_buffer_mbase.c` — covers CORE-03 mBASE-write side-effect
- [ ] `tests/unit/registers/CMakeLists.txt` + `tests/unit/registers/test_register_roundtrip.c` — covers CORE-04/API-04 baseline
- [ ] `tests/unit/registers/test_register_types.c` — covers CORE-04 signed/unsigned
- [ ] `tests/unit/registers/test_register_policy.c` — covers CORE-10 split-policy active/pending
- [ ] `tests/unit/registers/test_register_edges.c` — covers CORE-04 edge cases + TEST-02 per-register sweep
- [ ] `tests/api/CMakeLists.txt` + `tests/api/c99_consumer.c` + `tests/api/cxx_consumer.cpp` — covers API-07 C99/C++ consumer compile
- [ ] `tests/python/fuzz_buffer.py` — covers CORE-03 10⁶-step property test (SC 3)
- [ ] `tests/unit/CMakeLists.txt` — append `add_subdirectory(buffer)`, `add_subdirectory(registers)`, `add_subdirectory(state)` (currently only `q15/`)
- [ ] `tests/CMakeLists.txt` — append `add_subdirectory(api)` at the `tests/` root

## Sources

### Primary (HIGH confidence)

- [nocash psx-spx — Sound Processing Unit (SPU)](https://psx-spx.consoledev.net/soundprocessingunitspu/) — register inventory (complete), BufferAddress wrap formula verbatim, mBASE side-effect sentence verbatim, 22.05 kHz L/R alternation language, Sony BIOS reverb-setup procedure. Extracted 2026-04-19 via WebFetch.
- [nocash psx-spx I/O Map](https://psx-spx.consoledev.net/iomap/) — cross-reference for register hardware offsets.
- [Yann Collet — Opaque Types and Static Allocation](http://fastcompression.blogspot.com/2019/01/opaque-types-and-static-allocation.html) — the shell-type pattern with `_Static_assert` size sync (used by LZ4, zstd).
- [Memfault (interrupt.memfault.com) — Practical Design Patterns: Opaque Pointers and Objects in C](https://interrupt.memfault.com/blog/opaque-pointers) — caller-allocated opaque-handle pattern; runtime size query; anti-patterns.

### Secondary (MEDIUM confidence)

- [jsgroth — PlayStation: The SPU, Part 3 - Reverb](https://jsgroth.dev/blog/posts/ps1-spu-part-3/) — confirms 22.05 kHz L/R alternation derivation verbatim. Silent on mBASE side effect, write timing, vIIR anomaly (those were covered in Part 1 / Part 4 of the series, not Part 3).
- [hitmen c02 — PSX SPU documentation](https://hitmen.c02.at/files/docs/psx/spu.txt) — secondary reference. Absent on mBASE side-effect, confirms factory-preset table exists at 0x1DC0–0x1DFE.
- [Hypothesis — Stateful testing](https://hypothesis.readthedocs.io/en/latest/stateful.html) — `RuleBasedStateMachine` + `@rule` + `@invariant` reference. Not a Phase 2 dependency by default; upgrade path only.

### Tertiary (LOW confidence / absence-of-evidence)

- Absent evidence: no findable PSX homebrew exercises mid-stream mBASE writes. Consistent with Sony BIOS procedure implying this is not a supported operation.
- WebFetch on the main problemkaputt.de mirror (`https://problemkaputt.de/psx-spx.htm`) returned no reverb-formula section — this appears to be an index page that links to sub-pages. The `psx-spx.consoledev.net` mirror is the canonical full-text version.

## Metadata

**Confidence breakdown:**
- Register inventory (addresses, signedness): **HIGH** — directly extracted from primary source, cross-checked against secondary.
- BufferAddress wrap formula: **HIGH** — verbatim from primary, cross-confirmed.
- mBASE side-effect behavior: **HIGH** (for the direction — snap, not floor-only) — primary source is plain-language unambiguous. **MEDIUM** (for the absence of additional side effects like buffer-clear) — based on absence-of-evidence in primary and secondary sources.
- Write-timing policy: **MEDIUM** — spec is silent; D-04's split is structural reasoning, not spec-mandated. The seam exists for this reason.
- Opaque-handle pattern: **HIGH** — canonical pattern with multiple authoritative sources.
- C99 freestanding conformance: **HIGH** — C standard reference.
- Python ctypes / fuzz harness: **HIGH** — standard pattern, environment verified locally.

**Research date:** 2026-04-19
**Valid until:** 2026-05-19 (30 days — the nocash spec is stable; the Python ctypes surface is stable; the real risk is internal decision drift on A1–A9, not external source drift).

---

*Phase: 02-buffer-register-infrastructure*
*Researched: 2026-04-19*
