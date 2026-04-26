# Phase 5: Public API + Presets Integration - Context

**Gathered:** 2026-04-20
**Status:** Ready for planning — shell-ergonomics decisions locked; preset value sourcing research-gated (researcher agent performs three-source cross-reference before plans land).

<domain>
## Phase Boundary

Phase 5 delivers **the public block-based entry point that wraps the Phase 1–4 algorithm**, the ten PS1 factory reverb presets as a register-config table, first-class mid-stream register modulation across live processing, and a permanent real-time-safety audit infrastructure.

The algorithm itself is already bit-faithful (Phases 1–4). Phase 5 is the *shell* around it — the front door of the house, not the plumbing inside. Shell decisions are about serving 2026 C callers (DAWs, CLI, Python, future JUCE/MCU), which is a different category from the authenticity work of Phases 1–4.

**In scope:**
- `spu94_process` public block-based entry point (44.1 kHz int16 stereo in/out, planar buffers, any block size ≥ 1, in-place allowed)
- `spu94_flush` named drain function for offline-render tail capture
- Mix-bus wiring: new `mix_bus_l` / `mix_bus_r` fields on `spu94_state`, populated by `spu94_process` before each `spu94_tick`; `spu94_reverb_body` reads them where it currently hardcodes `left_in = 0, right_in = 0` (at `src/spu94/spu94_reverb.c:580`)
- 10 factory presets (`Room`, `Studio A`, `Studio B`, `Studio C`, `Hall`, `Half Echo`, `Space Echo`, `Echo`, `Delay`, `Off`) as `const spu94_preset_t presets[10]` with enum indexing (`SPU94_PRESET_HALL`, etc.)
- `spu94_load_preset` atomic bulk-writer using the Phase 2 engine-layer setters — respects D-04 split write policy (v* immediate, d*/m* tick-latched)
- Mid-stream register writes validated first-class: any register, any time, no crashes, no corruption, no required reset — proven by `tests/python/fuzz_process.py` (10⁶-step random-walk extension of Phase 2/3/4 fuzz pattern)
- RT-safety audit infrastructure in `tests/rt_safety/`: no-heap (linker-symbol check; already in CI since Phase 1 — Phase 5 adds runtime hook belt-and-suspenders optional), no-locks (linker-symbol check), no-syscalls (strace-based loop test), no-variable-latency (ctypes timing benchmark with bounded max/median ratio). Wired into CI permanently as regression gates.
- Three-source cross-reference preset research pass (`05-RESEARCH.md`) — mirrors the Phase 4 FIR coefficient discipline: (a) nocash psx-spx preset tables, (b) independent hardware-readout or forum/SDK source, (c) a third independent source; byte-for-byte compare; disagreements flagged and resolved with documented rationale
- ADR landings in `docs/DECISIONS.md` — planner discretion for exact count and split (API shape, mix-bus mailbox pattern, preset-load atomicity semantics, preset value sourcing provenance, RT-safety audit methodology)

**Governing frame (user, 2026-04-20):**

> *"I still feel like I am making these choices, but have no idea how accurate it is to how the original engineers must have done it."*

Reframe honored in this CONTEXT: Phase 5 decisions are **shell-ergonomics**, not algorithm-authenticity. The authenticity lives in Phases 1–4 (register semantics, Q15 truncation, reverb topology, FIR coefficients, hard-clip, vIIR anomaly, mBASE snap-on-write, split write policy) and is already locked. Phase 5 shell decisions serve modern callers and do NOT carry authenticity weight — with ONE exception: the 10 preset register values themselves are Sony-engineer-authored facts and get Phase 4-grade three-source rigor.

**Explicitly NOT in scope:**
- Python ctypes bindings / wheel — Phase 6 (PYBIND-01..06)
- CLI (`spu94 --preset hall in.wav out.wav`) — Phase 6 (CLI-01..04)
- Witness-diff harness regression tests — Phase 7 (TEST-03)
- Golden-file regression tests per preset — Phase 7 (TEST-04)
- Modulation-harness verification per register (sine, sweep, random walk at live rate) — Phase 7 (TEST-05). Phase 5 proves mid-stream writes don't crash/corrupt via `fuzz_process.py`; Phase 7 proves musical stability per register.
- MCU cross-compile validation — Phase 8
- LEVERS-CATALOG.md annotation — Phase 7 / DOCS-02 (Phase 5 may seed entries opportunistically but the doc's completion is Phase 7's job)
- Named musical levers ("Room Size", "Pre Delay", etc.) — Milestone 4
- Mid-stream preset morph / crossfade — Milestone 4 (musical feature on top of the bit-faithful load-preset primitive Phase 5 ships)

</domain>

<decisions>
## Implementation Decisions

### Area A — `spu94_process` block API shape (LOCKED)

- **D-01: Planar stereo pointers.** Public signature is `void spu94_process(spu94_state *state, const int16_t *L_in, const int16_t *R_in, int16_t *L_out, int16_t *R_out, uint32_t num_samples);`. Four pointers, not one interleaved buffer. Matches JUCE/VST/plugin-framework conventions at M4; CLI and numpy deinterleave at their adapter edges (one line each). User rationale: *"control surface exposure over each side independently"* — aligns with the "living instrument" posture in PROJECT.md.
- **D-02: Named `spu94_flush` drain function.** Public signature is `void spu94_flush(spu94_state *state, int16_t *L_out, int16_t *R_out, uint32_t num_samples);`. Feeds internal silence and emits trailing reverb tail. Required for offline-render use (CLI's `spu94 in.wav out.wav` WAV output captures the tail beyond the input's duration). Makes the concept *"the reverb is ringing, drain it"* named API surface instead of CLI-source folklore. Same underlying math as `spu94_process` with `L_in/R_in = zeros`.
- **D-03: Any block size N ≥ 1.** No even-N constraint. Phase 4's `spu94_fir_chain_step` internal phase tracking (`state->fir_interpolate_phase`) handles odd blocks correctly across calls. Full flexibility for caller-chosen block sizes (1, 64, 128, 441, arbitrary).
- **D-04: In-place processing allowed.** `L_out == L_in` and `R_out == R_in` both legal. Sample-at-a-time loop is alias-safe by construction — each input sample is consumed before its output slot is written. No restriction documented; DAW hosts often process in-place.

### Area B — Mix-bus wiring (LOCKED)

- **D-05: Mailbox on state.** Add two fields to `struct spu94_state`: `int16_t mix_bus_l; int16_t mix_bus_r;`. `spu94_process` writes them before each call to `spu94_tick`. `spu94_reverb_body` reads them where it currently hardcodes `left_in = 0, right_in = 0` (line 580). **What changes:** 4 bytes added to state (well within `SPU94_STATE_SIZE_MAX` headroom); one `= 0` line in `spu94_reverb_body` becomes `= state->mix_bus_l` / `state->mix_bus_r`; every existing Phase 3 reverb test passes unchanged because the fields default to zero (which is what those tests implicitly assume). **Why this shape:** matches the existing register pattern (set via one path, read during tick via another); zero blast radius on Phase 3 tests; honest about the mix bus being conceptually "the current input of the reverb" = state.

### Area C — Preset representation + sourcing (LOCKED; values research-gated)

- **D-06: One `const spu94_preset_t presets[10]` table in `.rodata`.** `typedef struct spu94_preset_t { const char *name; int16_t regs[SPU94_REG__COUNT]; } spu94_preset_t;` (shape at planner's discretion — might be flatter `int16_t[35]` keyed by enum, might include a version field). Enum indexing: `SPU94_PRESET_OFF = 0, SPU94_PRESET_ROOM, SPU94_PRESET_STUDIO_A, SPU94_PRESET_STUDIO_B, SPU94_PRESET_STUDIO_C, SPU94_PRESET_HALL, SPU94_PRESET_HALF_ECHO, SPU94_PRESET_SPACE_ECHO, SPU94_PRESET_ECHO, SPU94_PRESET_DELAY, SPU94_PRESET__COUNT`. Data-centric: Python/CLI index the table by enum; enables introspection (dump all presets, diff preset values).
- **D-07: Three-source cross-reference for preset values.** Mirrors Phase 4 D-10 coefficient-sourcing discipline. Phase 5 research (`05-RESEARCH.md`) pulls the 35×10 register matrix from (a) nocash psx-spx preset tables, (b) at least one hardware-readout or Sony SDK source (candidates: archived PSX SDK docs, bannister.org / forum SCPH-hardware dumps, hitmen c02 SPU docs if they publish presets, psxdev forum archives), (c) a third independent source. Byte-for-byte compare; any disagreement flagged and resolved with documented rationale in `docs/DECISIONS.md`. Values transcribed as facts-only (no prose copied) per PROJECT.md licensing posture. All three sources cited in `docs/BIBLIOGRAPHY.md`.
- **D-07a (Claude's discretion within D-07):** If any one of the 10 presets cannot be resolved to consensus, the research document picks a working value with a documented rationale and flags it for M5 hardware-capture arbitration. The other 9 presets ship regardless.

### Area D — Preset-load atomicity (LOCKED)

- **D-08: Preset-load respects Phase 2 D-04 split write policy.** `spu94_load_preset(spu94_state *state, spu94_preset_t id)` iterates the 35 register values and calls the existing engine-layer setters (`spu94_set_reg_i16` / `spu94_set_reg_u16`). `v*` values become active immediately; `d*`/`m*` values land in the pending slot and commit at the next tick. One unified write path — no preset-specific bypass, no D-04 divergence. Cost: ~45-microsecond "half-applied" window (new gains, old delays) — inaudible. Benefit: consistency, simpler mental model, no new code path, no new bug surface. **Seam (ADR-0005 style):** if M4 or M5 ever demonstrates a need for atomic whole-preset commit, the split-policy seam already supports re-pointing via D-11 from Phase 2 (swappable write-policy table) — no Phase 5 code change required.

### Area E — RT-safety audit infrastructure (LOCKED)

API-08 requires four RT-safety guarantees on `spu94_process`: no heap, no locks, no syscalls, no variable-latency operations. SC-4 requires this verified across 10⁵ consecutive blocks. Phase 5 ships four permanent CI regression gates under `tests/rt_safety/`:

- **D-09a: No-heap — linker-symbol check.** `readelf -d` / `nm` asserts `malloc`, `calloc`, `realloc`, `free` are not referenced by the shared library's dynamic symbols. **Already in CI since Phase 1** (`verify-no-heap-symbols` script). Phase 5 task: confirm the script covers `spu94_process` + `spu94_flush` + `spu94_load_preset` by linking in a test binary that exercises all three; optionally add a runtime `mallopt(M_CHECK_ACTION, ...)` hook as belt-and-suspenders (planner decides).
- **D-09b: No-locks — linker-symbol check.** `readelf -d` asserts `pthread_mutex_*`, `pthread_rwlock_*`, `pthread_cond_*`, `pthread_spin_*` are not referenced. New Phase 5 CI script `verify-no-locks.sh` (adjacent to the existing `verify-no-heap-symbols.sh`).
- **D-09c: No-syscalls — strace-based loop test.** New test binary calls `spu94_init` → `spu94_load_preset` → 10⁵ iterations of `spu94_process` → `spu94_flush`. Running under `strace -c -f`, Phase 5 asserts zero syscalls during the steady-state 10⁵-block loop (startup / shutdown syscalls are OK). Lives in `tests/rt_safety/test_no_syscalls.sh` + matching C harness. Linux-only; Phase 5 skips gracefully on non-Linux ctest hosts.
- **D-09d: No-variable-latency — ctypes timing benchmark.** New Python test `tests/rt_safety/bench_latency.py` times 10⁵ consecutive `spu94_process` calls (1024-sample block size), asserts `(max - median) / median` ratio is within a planner-derived bound (first-pass target: 3× — wide enough to tolerate OS noise, tight enough to catch cache-dependent branches or accidental syscalls). Warmup: discard first N calls to let caches populate. Uses `time.perf_counter_ns` for nanosecond resolution. Lands as a ctest target; CI runs on a dedicated no-other-load step.

- **D-09e: Test layout.** Four test targets under `tests/rt_safety/`, each provable on its own axis. One monolithic audit is rejected — per-axis failure diagnosis is cheaper. Wired into `ctest` via `add_test()` + new CMake `RT_SAFETY_TESTS` target group.

### Area F — Mid-stream register write test strategy (LOCKED)

- **D-10: Mid-stream writes are first-class at any granularity.** Not limited to block boundaries. `spu94_process` MUST tolerate `spu94_set_reg_*` calls interleaved with `spu94_process` calls at any frequency, across all 35 registers. Matches PROJECT.md "living instrument" directive — every parameter is modulatable, not just "at safe moments."
- **D-10a: `tests/python/fuzz_process.py` extends the Phase 2/3/4 fuzz pattern.** Drives 10⁶ steps where each step is one of: {write random register with random value, call `spu94_process` with random block of random length, call `spu94_flush` with random length, call `spu94_load_preset` with random preset}. Invariants asserted at every step:
  - No crashes, no UBSan trips, no ASan trips
  - Output samples are bounded within int16 range (no wraps escape the hard-clip)
  - `spu94_state` fields remain within their declared domains (buffer-address-wrap formula holds, FIR delay indices in `[0, 39)`, err/overflow tap accumulators are monotonic under saturating input)
  - No heap allocations (via `mallopt` hook integrated into the fuzz harness)
  - Preset-load followed immediately by `spu94_process` produces non-zero output for all 10 presets except `Off` (reverb tail or direct path is live)
- **D-10b: Test vectors beyond the fuzz.** Per the test-vector robustness precedent from Phase 3/4:
  - Block-size sweep: 1, 2, 3, 4, 7, 16, 64, 128, 441, 1024, 4096 — same input content through each block size produces bit-identical output (the block size is pure flow-control; it must not affect output)
  - Impulse-through-process: unit impulse at t=0, assert peak response at `t = spu94_get_latency_samples() = 38`
  - In-place round-trip: `L_out == L_in` and `R_out == R_in`, compare against out-of-place baseline, assert bit-identical
  - Preset-roundtrip: `spu94_load_preset(HALL)` → `spu94_snapshot_registers` → assert all 35 register active values match the Hall preset table (modulo the `d*`/`m*` tick-latched one-tick window — assert pending-slot match immediately, active-slot match after one `spu94_tick` call)
  - `spu94_flush` correctness: non-silent input → `spu94_flush` → assert tail is decaying (not growing), eventually falls below audible threshold

### Architectural Principles (carried forward from Phases 1–4)

- **D-22 Extensibility Seams (Phase 2):** The preset-load path reuses Phase 2's engine-layer setters + D-11 swappable write-policy table. `spu94_process` dispatches to Phase 4's `spu94_fir_chain_step`, which in turn dispatches to `spu94_tick` (Phase 2 public), which dispatches to `spu94_reverb_body` (Phase 3 internal). No new seams added in Phase 5 — Phases 1–4 left enough surface area.
- **D-23 Observability (Phase 2):** Phase 5 preserves read-only observability. `spu94_load_preset` writes state; no observer hooks added (D-21 reasoning holds). Mix-bus mailbox fields (`mix_bus_l`/`mix_bus_r`) are internal — not exposed via public accessors (Phase 5 callers write them *through* `spu94_process`'s audio path, not via a setter).
- **D-24 Controllers as Future Consumer (Phase 2):** The M4 Controllers layer consumes Phase 5's public API unchanged. `spu94_load_preset` becomes the backbone of the M4 "preset menu"; `spu94_process` is the audio path Controllers sits atop. No Controllers-specific hooks added in Phase 5.
- **ADR-0001 (Q15 truncation direction):** no new multiplies introduced in Phase 5 — the path is glue code (block loop, mailbox writes, preset iteration) on top of the Phase 3/4 math.
- **ADR-0005 (Pitfall 4 single-call-site discipline):** `spu94_process` is the single caller of `spu94_fir_chain_step` for the public path; `spu94_flush` is a separate caller for the drain path. `spu94_reverb_body` remains single-caller from `spu94_tick`. No new multi-caller code paths.

### Claude's Discretion (within the locked decisions above)

- Exact signature of `spu94_preset_t` — flat `int16_t regs[SPU94_REG__COUNT]` vs typed-per-register struct; whether to include a `name` string field or keep presets name-less and add a parallel `spu94_preset_name(id)` accessor
- Exact enum for preset identifiers (above shows one candidate; planner may reorder or rename per consistency with existing enum conventions)
- Exact C prototype of `spu94_process` / `spu94_flush` / `spu94_load_preset` — specifically the `num_samples` type (`uint32_t` vs `size_t` vs `int`)
- Whether the mix-bus mailbox fields live adjacent to the FIR fields in `spu94_state` or in their own "I/O" grouping
- Whether `spu94_process` internally decomposes into sub-functions (one per stage: deinterleave/read-input, run-chain, write-output) or is a tight single loop
- Exact threshold for the ctypes latency benchmark (D-09d) — `3× median` is a first-pass target; planner derives from actual host measurement
- Test-file granularity under `tests/unit/process/` and `tests/unit/preset/` — single TU per concern vs combined
- Number and split of ADRs appended to `docs/DECISIONS.md` — planner discretion on whether each of D-01..D-10 gets its own ADR or natural groupings (API shape, preset architecture, RT-safety methodology) are combined
- Whether the `spu94_load_preset` helper takes `spu94_preset_t id` (enum) or `const spu94_preset_t *preset_ptr` (pointer — more flexible, lets callers bring custom preset tables) or both via two entry points
- Whether `mix_bus_l` / `mix_bus_r` are `int16_t` (pre-input-scale, matches current `left_in`/`right_in` type at reverb.c:580) or `int32_t` (wider mix-bus, matches the Phase 3 hard-clip stage input) — the former is the minimum-change answer; the latter would require an input-scale-stage rewire. Recommendation: `int16_t`, keep Phase 3 stage boundaries intact.

### Folded Todos

None — no pending todos matched Phase 5 scope at discussion time.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents (researcher, planner, executor) MUST read these before proceeding.**

### Project Spec (internal)

- `.planning/PROJECT.md` — Core Value, constraints (no heap, no float, bit-faithfulness), licensing posture, "living instrument" framing, Key Decisions table — specifically the "M1 narrow: reverb network + hard clip only" and "SPU-94 is a living instrument, not a preset engine" entries. The latter governs D-10's "mid-stream writes are first-class at any granularity" lock.
- `.planning/REQUIREMENTS.md` — Phase 5 owns CORE-09 (10 presets), API-03 (`spu94_process`), API-05 (bulk preset-load), API-06 (mid-stream writes first-class), API-08 (no heap/locks/syscalls/variable-latency, verified via static analysis + benchmark).
- `.planning/ROADMAP.md` § Phase 5 — four success criteria must all be TRUE. SC-1 (block-based 44.1 kHz int16 stereo with 22.05 kHz + FIR hidden), SC-2 (all 10 presets loadable atomically, non-zero tails for non-silent input except `Off`), SC-3 (any-time register writes, no crashes/corruption/reset), SC-4 (benchmark-audit confirms no-heap/no-locks/no-syscalls/no-variable-latency across 10⁵ blocks).

### Prior Phase CONTEXT.md files (must read for consistency)

- `.planning/phases/01-foundation-fixed-point-math-build-infrastructure/01-CONTEXT.md` — Q15 API shape, truncation direction, inline-reference-table test pattern, grep-guard + UBSan CI (Phase 5 extends with RT-safety CI gates).
- `.planning/phases/02-buffer-register-infrastructure/02-CONTEXT.md` — D-04 split write policy (preset-load honors this per D-08), D-12 opaque state handle (Phase 5 adds fields to the internal struct, not the public type), D-18/D-19 extensibility taps, D-22/D-23/D-24 architectural principles.
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-CONTEXT.md` — D-05/D-06 reverb body shape (Phase 5 wires mix-bus into the `left_in = 0, right_in = 0` site at reverb.c:580), D-07 cascading-sat comb (Phase 5 doesn't touch the reverb math), D-11 err-tap + overflow-magnitude surface (Phase 5 doesn't add observers; M4 will).
- `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-CONTEXT.md` — D-07 internal `spu94_fir_chain_step` (Phase 5's public `spu94_process` wraps this in a block loop), D-09 `spu94_get_latency_samples()` contract (Phase 5's impulse test asserts peak at sample 38), D-10 coefficient-sourcing three-source discipline (Phase 5 D-07 mirrors this for preset values).

### Prior Phase Research (shape precedent for Phase 5 research)

- `.planning/phases/02-buffer-register-infrastructure/02-RESEARCH.md` — primary-source + witness-comparison pattern; mBASE snap-on-write resolution shape.
- `.planning/phases/03-core-reverb-algorithm-hard-clip/03-RESEARCH.md` — multi-gray-area research pass + post-research supplemental discuss → lock pattern.
- `.planning/phases/04-sample-rate-conversion-39-tap-half-band-fir/04-RESEARCH.md` — three-source cross-reference coefficient-extraction pattern. Phase 5's `05-RESEARCH.md` directly mirrors this for preset values (D-07).

### ADR Log (appended in Phase 5)

- `docs/DECISIONS.md` — existing ADRs: ADR-0001 (Q15 multiply semantics), ADR-0002 (vIIR anomaly), ADR-0003 (UBSan no_sanitize policy), ADR-0004 (extensibility taps), ADR-0005 (per-register write-timing policy), ADR-0006 (mBASE snap-on-write), ADR-0007 (comb-sum cascading sat), ADR-0008 (L/R write timing), ADR-0009 (hard-clip placement), ADR-0010 (vIIR anomaly branch), ADR-0011 (per-multiply err-tap scope + overflow-magnitude tap), ADR-0012..ADR-0020 (Phase 4 FIR decisions — half-rate architecture, lv2-psx-reverb out-of-axis exclusion, FIR math form, accumulator width, clamp policy, overflow-magnitude tap, err-tap parity, internal wrapper shape, per-channel state, latency contract, coefficient sourcing + bibliography, witness empirics). Phase 5 appends (exact count at planner's discretion): public API shape (D-01..D-04), mix-bus mailbox pattern (D-05), preset storage shape (D-06), preset value sourcing provenance (D-07), preset-load atomicity (D-08), RT-safety audit methodology (D-09), mid-stream write test strategy (D-10).

### External References (paraphrased only — do NOT transcribe per PROJECT.md licensing posture)

- **nocash PSX SPU documentation** (problemkaputt.de / psx-spx.consoledev.net) — primary candidate for the 10 factory preset register tables (35 registers × 10 presets = 350 int16 values). **Critical from Phase 4 research lesson:** nocash is not always comprehensive (nocash didn't publish FIR coefficients). Phase 5 research (D-07) must verify nocash covers all 10 presets before relying on it; cross-reference against other sources per D-07.
- **hitmen c02 PSX SPU documentation** — candidate secondary source for preset values (publishes factory presets in some versions of the doc).
- **Sony PSX SDK documentation** (archived) — candidate tertiary source. PSX SDK includes SPU API with preset constants. Factual values only; no copied prose.
- **bannister.org forum / psxdev forum archives** — candidate source for hardware-readout preset dumps (community reverse-engineering, same approach as Phase 4's FIR coefficient sources).
- **jsgroth "PlayStation: The SPU" series** — not a primary source for preset values but may corroborate via worked examples.
- **Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation, MiSTer FPGA PSX core** — their preset tables live in source code (explicitly GPL, off-limits as primary per PROJECT.md licensing posture); their *output audio* for each preset may be used as a witness in Phase 7 golden-file / witness-diff work; their source code is not a primary input.

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets (from Phases 1–4)

- `include/spu94/spu94.h` — public umbrella. Phase 5 adds: `spu94_process`, `spu94_flush`, `spu94_load_preset`, `spu94_preset_t` typedef, `spu94_preset_id_t` enum (`SPU94_PRESET_OFF..SPU94_PRESET__COUNT`), preset-table accessor (`extern const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT];`).
- `include/spu94/spu94_q15.h` — Q15 helpers unchanged; Phase 5 uses no new multiplies.
- `include/spu94/spu94_registers.h` + `include/spu94/spu94_register_facade.h` — Phase 5's `spu94_load_preset` iterates `SPU94_REG__COUNT` and calls `spu94_set_reg_i16` / `spu94_set_reg_u16` via the engine layer (not facade wrappers).
- `src/spu94/spu94_state_internal.h` — Phase 5 adds `int16_t mix_bus_l; int16_t mix_bus_r;`. Current `sizeof(spu94_state)` after Phase 4 is ~500 bytes (168 pre-Phase-4 + ~340 for FIR state); `SPU94_STATE_SIZE_MAX` is 16384 — headroom abundant.
- `src/spu94/spu94_reverb.c:571-580` — the literal insertion point for mix-bus wiring. `const int16_t left_in = 0; const int16_t right_in = 0;` becomes `const int16_t left_in = state->mix_bus_l; const int16_t right_in = state->mix_bus_r;`. Body-level tests that depend on `left_in = 0` remain valid (they don't write `mix_bus_l/r`, so the default-zero state gives the same behavior).
- `src/spu94/spu94_io_chain.c` — `spu94_fir_chain_step(state, L, R, Lout, Rout)` is Phase 5's per-sample building block. `spu94_process` is a block-level loop: for each `i` in `[0, num_samples)`, set `state->mix_bus_l = L_in[i]; state->mix_bus_r = R_in[i];` then call `spu94_fir_chain_step(state, L_in[i], R_in[i], &L_out[i], &R_out[i])`.
- `src/spu94/spu94_fir_internal.h` — `spu94_fir_chain_step` and `spu94_fir_chain_step_reverb_bypass` are declared here. `spu94_flush` reuses `spu94_fir_chain_step` with zero inputs.
- `src/spu94/spu94_tick.c` — per-22.05-kHz-tick entry. Phase 5 does NOT modify it; mix-bus mailbox fields are read inside `spu94_reverb_body` (which `spu94_tick` already calls).
- `src/spu94/spu94_state.c` — `spu94_init` / `spu94_reset`. Phase 5 adds zeroing of `mix_bus_l`/`mix_bus_r` (already covered by the wholesale-state-zeroing pattern from Phase 2 Plan 01).
- `src/spu94/spu94_register_io.c` — engine-layer setters used by `spu94_load_preset`.
- `src/spu94/spu94_pending.c` — tick-start pending-to-active flush; preset-load writes d*/m* values here via the engine layer, they commit at next tick (D-08).
- `tests/unit/` — existing Unity harness. Phase 5 adds `tests/unit/process/` (block-loop correctness, in-place, block-size sweep), `tests/unit/preset/` (preset-load atomicity, preset-roundtrip, non-zero-tail assertion), and `tests/unit/mix_bus/` (mailbox field behavior in isolation).
- `tests/python/fuzz_buffer.py` + `fuzz_reverb.py` + `fuzz_fir.py` — template for `tests/python/fuzz_process.py`.
- CMake `spu94_obj` OBJECT library + `spu94_shared` + `spu94_static` — new Phase 5 source files (`src/spu94/spu94_process.c`, `src/spu94/spu94_presets.c`) picked up automatically.
- CI: grep-guard, `verify-no-heap-symbols`, clang-tidy, cppcheck, UBSan — Phase 5 code passes all unchanged. Phase 5 *adds* `verify-no-locks.sh`, `tests/rt_safety/test_no_syscalls.sh`, `tests/rt_safety/bench_latency.py`.

### Established Patterns

- **One-concern-per-TU grain** (Phase 2): `spu94_process.c` holds `spu94_process` + `spu94_flush` (both are "public block-based entry points"); `spu94_presets.c` holds the `const spu94_preset_t presets[10]` table + `spu94_load_preset` body. Two new TUs, not one monolith.
- **Engine-layer-not-facade for iteration** (Phase 2 Plan 03): `spu94_load_preset` iterates registers via `spu94_set_reg_*`, NOT via the 35 facade wrappers. Engine layer is designed for bulk iteration; facade is for readable per-register call sites.
- **Inline hand-computed reference tables in test `.c` files** (Phase 1): every Phase 5 test TU carries a `{input, expected_output}` table with INT16_MIN / INT16_MAX / 0 / saturation-tripping / impulse / near-Nyquist / preset-loaded / mid-stream-modulated cases.
- **Pitfall 4 single-call-site discipline** (ADR-0005): `spu94_process` is the only caller of `spu94_fir_chain_step` for the public audio path; `spu94_flush` is the drain path; `spu94_load_preset` is the only internal consumer of the presets table.
- **ADR-style DECISIONS.md entries prepended at top** (Phase 1 D-12 style): Phase 5 appends a block of ADRs for D-01..D-10.
- **Research artifact structure** (Phase 2/3/4 `*-RESEARCH.md`): primary-source evidence facts-only, secondary corroboration, witness comparison deferred. Phase 5's `05-RESEARCH.md` adds a three-source coefficient-style cross-reference section for the 35×10 preset matrix.

### Integration Points

- **Phase 6 (Python bindings + CLI)** wraps the Phase 5 public surface directly. `spu94_process` becomes `process_block(state, L_np, R_np, L_out_np, R_out_np)` via ctypes; `spu94_load_preset` becomes `load_preset(state, preset_id)` or `load_preset(state, "hall")` with a lookup. `spu94_presets[]` is importable as a Python list of 10 dicts (reflection via `spu94_reg_name` from Phase 2 D-17).
- **Phase 7 (witness diff + golden files)** consumes Phase 5's public API unchanged. Per-preset golden files (TEST-04) drive `spu94_process` with test inputs through each preset and snapshot outputs. Witness-diff harness (TEST-03) drives lv2-psx-reverb / Mednafen / DuckStation with the same preset register values and cross-correlates outputs (frequency-response axis excluded for lv2-psx-reverb per Phase 4 ADR).
- **Phase 8 (MCU cross-compile)** — `spu94_process` + `spu94_flush` + `spu94_load_preset` are the three public symbols the mcu-smoke `main.c` exercises (ROADMAP Phase 8 SC-1: "init + load_preset + one process block").
- **Future Milestone 4 (Controllers / JUCE plugin)** — `spu94_load_preset` is the backbone of the plugin's preset menu; `spu94_process` is the audio path; the mix-bus mailbox (D-05) is invisible to M4 (M4 doesn't touch `mix_bus_l/r` directly, it just calls `spu94_process`); the named-lever layer (Room Size, Pre Delay, etc.) sits atop `spu94_set_reg_*`.
- **Future Milestone 5 (Hardware validation)** — the preset-value three-source cross-reference (D-07) becomes M5's first ratification target. If M5 hardware capture ever reveals a discrepancy on any of the 10 presets' register values, the table in `src/spu94/spu94_presets.c` is updated and a new ADR documents the revision.

</code_context>

<specifics>
## Specific Ideas

### API Signature Details (planner discretion within D-01..D-04)

Rough prototype shapes to inform planner:

```c
/* Block-based processing. In-place allowed (Lout == Lin, Rout == Rin OK). */
void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples);

/* Drain: emit trailing reverb tail by feeding internal silence. */
void spu94_flush(spu94_state *state,
                 int16_t *L_out, int16_t *R_out,
                 uint32_t num_samples);

/* Load one of the 10 factory presets atomically (respects D-04 split policy). */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id);

/* Preset-table introspection (primarily for Phase 6 Python bindings). */
typedef struct {
    const char *name;
    int16_t regs[SPU94_REG__COUNT];
} spu94_preset_t;

extern const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT];
```

`num_samples` type — `uint32_t` is recommended (unambiguous on MCU; aligns with Phase 4's `SPU94_LATENCY_SAMPLES` as `uint32_t` in `spu94_get_latency_samples()`). `size_t` is also defensible.

### Preset Value Research (`05-RESEARCH.md`) Required Contents

- **Three-source coefficient-style cross-reference table** — for each of the 10 presets, extract the 35 register values from each of the three sources, present as a 35-row side-by-side table with agreement/disagreement column.
- **Source bibliography entries** — URL, section/page reference, retrieval date, a one-line note on the source's authority (e.g., "hardware-readout from SCPH-5501", "official Sony SDK documentation", "reverse-engineering writeup").
- **Disagreement resolution protocol** — if any register value differs across sources: which source wins, why, and what ADR captures the rationale. If no tiebreaker source exists, flag for M5 hardware-capture arbitration.
- **Non-zero-tail sanity check per preset** — hand-compute (or reason through) that each preset's `vIIR`/`vCOMB*`/`vAPF*` values will produce a non-zero decaying tail for non-silent input (except `Off`, which should be silent by construction — `vLIN = vRIN = vLOUT = vROUT = 0` or equivalent).

### RT-safety Benchmark Details (D-09d)

- **Warmup**: discard first ~1000 calls to let instruction / data caches populate.
- **Measurement**: 10⁵ timed iterations of `spu94_process(state, L, R, Lout, Rout, 1024)`.
- **Analysis**: compute median, max, p99, stddev. Primary assertion: `(max - median) / median ≤ 3.0` (first-pass — planner validates against actual host measurement in Phase 5 Plan N).
- **OS noise mitigation**: if host timing is noisy, run with `chrt -f 50` or `nice -n -20` in CI to reduce scheduling jitter. Skip gracefully if unsupported.
- **Threshold tuning**: document the measured host's p99 during Phase 5 Plan N; if the 3× target is unreachable on typical CI runners, relax to 5× or 10× with justification. The goal is regression detection, not absolute real-time-correctness proof — that requires an RT kernel.

### Test Measure Additions

- **Impulse-through-process**: unit impulse at `L_in[0] = 32767` with Hall preset loaded; assert `L_out[38]` > 0 (peak near latency) and `L_out[N-1]` < `L_out[38]/2` (decaying) across N = 44100 samples (1 second).
- **In-place round-trip**: run `spu94_process(state, L, R, L, R, N)` vs `spu94_process(state, L, R, Lo, Ro, N)` with identical initial state, assert `L == Lo && R == Ro` bit-identical.
- **Block-size invariance**: same input content through block sizes `{1, 2, 3, 7, 16, 64, 128, 441, 1024, 4096}` produces bit-identical outputs from a fresh-initialized + Hall-preset-loaded state.
- **Preset-load atomicity (respecting D-04)**: load Hall preset, immediately read back every `v*` register via `spu94_get_reg_i16` — assert active value equals preset table. For every `d*`/`m*` register, assert pending-slot equals preset table (via `spu94_get_reg_u16_pending`), active-slot equals whatever it was before (still unchanged). Call `spu94_tick` once; now assert active-slot equals preset table. This test proves D-08 works exactly as designed.
- **Non-zero-tail per preset (SC-2)**: for each of the 10 presets (except `Off`), load preset → feed 100 samples of white noise via `spu94_process` → call `spu94_flush(state, L, R, 1000)` → assert max(abs(L)) > 0 and max(abs(R)) > 0. `Off` preset → same protocol → assert all output is zero.
- **Mid-stream write doesn't crash**: 10⁶-step `fuzz_process.py` as D-10a.

</specifics>

<deferred>
## Deferred Ideas

### Planted Seeds for M4 (Controllers / Plugin Era)

- **Mid-stream preset morph / crossfade** — M4 musical feature on top of Phase 5's atomic `spu94_load_preset` primitive. M4 Controllers can implement "morph from Hall to Room over 2 seconds" by reading both presets' register values and driving a per-register interpolation via `spu94_set_reg_*` calls each block. Phase 5's job is the atomic primitive; M4's job is the interpolation UX. No Phase 5 work required.
- **User-supplied custom preset tables** — if `spu94_load_preset` takes `const spu94_preset_t *` instead of enum id (Claude's discretion within D-06), M4 plugin users can ship their own preset banks. Pure API-shape extension; no algorithm change.
- **Preset A/B compare** — "switch between Hall and Room instantly to A/B the sound." Built on top of `spu94_load_preset`'s atomicity. M4 UX.
- **Per-channel modulation via mix-bus mailbox (D-05)** — the mailbox fields are technically writable by M4 Controllers between `spu94_process` blocks to inject test signals, null signals, or cross-feed tricks. Phase 5 does not expose setters; M4 decides whether to surface this or keep it internal.

### Deferred to Phase 6

- **Python bindings for `spu94_process`, `spu94_flush`, `spu94_load_preset`, preset table introspection** — Phase 6 (PYBIND-01..06).
- **CLI integration** — `spu94 --preset hall in.wav out.wav` glue in Phase 6 (CLI-01..04). Phase 5 ships the primitives; Phase 6 wraps them in the WAV-reading executable.

### Deferred to Phase 7

- **Golden-file regression tests per preset × input combination** — TEST-04. Phase 5 ships the presets and proves they load; Phase 7 snapshots their outputs against standard inputs (impulse, white noise, 1 kHz sine, silence) and diffs future runs.
- **Witness-diff harness per preset** — TEST-03. Phase 5 ships the preset register values (transcribed from three sources); Phase 7 drives the same values through lv2-psx-reverb / Mednafen / DuckStation and cross-correlates outputs (frequency-response axis excluded for lv2-psx-reverb per Phase 4 ADR).
- **Modulation harness per register** — TEST-05. Phase 5's `fuzz_process.py` proves mid-stream writes don't crash; Phase 7's modulation harness proves musical stability (sine, frequency sweep, random walk) for each of the 35 registers at live rates.
- **LEVERS-CATALOG.md maintenance** — DOCS-02 / Phase 7. Phase 5 may seed entries opportunistically (e.g., "`vIIR` = global feedback — the `Decay` lever in M4"), but the doc's completion is Phase 7's job.

### Deferred to Phase 8

- **MCU-smoke `main.c`** — init + load_preset + one process block + `.text` size check + `readelf -d` no-malloc check. Phase 5 ships the public API symbols the smoke test calls; Phase 8 writes the cross-compile harness and the Cortex-M7 toolchain file.

### Deferred to Milestone 4

- **Named musical levers** (Room Size, Pre Delay, Decay, Diffusion, Damping, etc.) — M1 non-scope per PROJECT.md.
- **Parameter smoothing** — M4 work.
- **JUCE plugin wrapper** — M4 work.
- **Plugin UI** — M4 work.

### Deferred to Milestone 5

- **Hardware-capture arbitration of preset values** — if M5 hardware capture on original PSX silicon reveals any of the 10 preset register blocks ship with values different from the three-source-ratified table, the table is revised and an ADR documents the revision.
- **Hardware validation of `spu94_process` timing** — M5 captures timing on actual Cortex-M7 or Daisy hardware under the RT-safety benchmark, validating the `3× median` bound holds on real embedded targets.

### Raised in Discussion, Routed Elsewhere

- **"How accurate is this to how the original engineers did it?"** (user, 2026-04-20) — reframed during Phase 5 discuss: the *algorithm* authenticity work lives in Phases 1–4 and is locked; Phase 5 *shell* decisions (API shape, mailbox pattern, RT-safety methodology) do not carry authenticity weight because the PS1 silicon had no C-library-caller concept. The one Phase 5 decision that does carry authenticity weight is the 10 preset register values themselves (D-07), and it's research-gated with three-source discipline. This reframe was recorded to avoid a future false-equivalence where shell-ergonomics decisions get treated as algorithm-authenticity decisions.

### Reviewed Todos (not folded)

None — no pending todos existed at discussion time.

### Scope Creep Rejections

None raised. Discussion stayed within the Phase 5 domain boundary. User's one redirect ("Should we research phase 5 before going through these questions?") was resolved by sorting gray areas into research-gated (D-07 preset values) vs not (everything else) — the non-research-gated areas were completed in-discussion, and D-07 defers value extraction to the planner-spawned `05-RESEARCH.md` pass.

</deferred>

---

*Phase: 05-public-api-presets-integration*
*Context gathered: 2026-04-20*
*Next step: `/gsd-plan-phase 5` — consumes CONTEXT.md. Planner will first invoke research (for the three-source preset-value cross-reference per D-07), then task breakdown.*
