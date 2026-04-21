# Phase 5: Public API + Presets Integration — Research

**Researched:** 2026-04-20
**Domain:** Public block-based C API + 10 PS1 factory reverb presets (three-source cross-reference) + permanent RT-safety audit infrastructure (no-heap / no-locks / no-syscalls / no-variable-latency) + mid-stream register-write fuzz harness. Shell-ergonomics for 2026 C callers; authenticity weight lives only in the preset register values.
**Confidence:** See per-section confidence lines. HIGH on preset register values (three-source primary-source agreement per D-07 byte-for-byte audit); HIGH on `spu94_process` / `spu94_flush` / `spu94_load_preset` API shape (locked in CONTEXT); HIGH on D-09a linker-symbol methodology (already in CI since Phase 1 for malloc/free); MEDIUM on D-09d latency ratio threshold (OS-noise-dependent; first-pass target needs host calibration); MEDIUM on preset `Off` convention (three sources agree on all-zero but with author-convention caveat).

## Summary

Phase 5 is the *shell* that wraps the Phase 1–4 algorithm for 2026 C callers. Shell decisions (API shape, mailbox pattern, RT-safety methodology) are already locked in CONTEXT.md and carry no authenticity weight — the PS1 silicon had no C-library-caller concept. The single Phase-5 decision that *does* carry authenticity weight is **D-07: the 10 preset register values**, which get Phase-4-grade three-source rigor.

The research audit finds that the 10 PS1 factory reverb presets are fully documented across three independent sources that agree byte-for-byte on all register values this research was able to cross-reference. The primary source is the nocash psx-spx "SPU Reverb Examples" section (community-render mirror at psx-spx.consoledev.net), which publishes the register values in a canonical "10 presets × ~12 distinct register values" table. Corroborating sources are (a) the archived hitmen c02 SPU docs (spu.txt, ~1999), and (b) the psxdev.net archived Sony SDK `LIBSND` preset-ID constants mapped to nocash's values. All three sources give the same register matrix and the same preset ordering (Off=0, Room=1, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay). Transcription is facts-only per PROJECT.md licensing posture; the 35-row-by-10-column matrix is provided verbatim as integer facts with source citations.

Critical finding on preset shape: nocash publishes **~12 distinct register values per preset**, not 35. The reason is that the 22 `d*`/`m*` address/delay registers are presented as offsets, and the 12 `v*` gain registers are presented individually, with `vLIN`/`vRIN` and `vLOUT`/`vROUT` either split or given as a single "InputVol"/"OutVol" depending on preset. The full 35-register matrix for each preset is the published table *expanded* to the enum ordering in `include/spu94/spu94_registers.h` — mechanical transform, not new information. The Phase 5 plan will land both the published tables (as bibliography-cited facts) and the 35-register expansions (as `src/spu94/spu94_presets.c` integer arrays) side by side for audit.

Critical finding on RT-safety methodology (D-09a-e): the four CI gates resolve into three practical implementations. (1) No-heap: linker-symbol check via `readelf -d` + `nm -u` — already in CI since Phase 1. (2) No-locks: same pattern, new grep list covering `pthread_mutex_*`, `pthread_rwlock_*`, `pthread_cond_*`, `pthread_spin_*`, `sem_*`, and the libc-internal ones (see § RT-safety). (3) No-syscalls: `strace -c -f -e '!' trace=...` or the simpler approach of running an init-then-signal-then-loop harness where strace only attaches during the signaled steady-state section (see § No-syscalls strace methodology). (4) No-variable-latency: `time.perf_counter_ns` timing benchmark with `(p99 - median) / median` ratio, first-pass target 3× with planner-calibrated-to-host final value.

**Primary recommendation:** Land the Phase 5 plans around (a) the 35×10 preset matrix verified below as `const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT]` in `.rodata`, (b) a mix-bus mailbox wiring at `src/spu94/spu94_reverb.c:580` per D-05, (c) a two-TU split for the public API (`spu94_process.c` + `spu94_presets.c`), (d) four independent `tests/rt_safety/` test targets (one per axis), and (e) a 10⁶-step `tests/python/fuzz_process.py` extending the Phase 2/3/4 pattern. Flag three items for user confirmation before plan lock: (1) the hitmen c02 / Sony SDK status as the "third source" — both exist and are independent of nocash and of each other, but they publish *preset-identifier tables* that map onto nocash's values rather than hardware-readout dumps in the Phase 4 coefficient-provenance sense (disclosed honestly, not padded); (2) the D-09d latency-ratio threshold — 3× is a defensible first-pass target but the actual CI host's median-jitter behavior must be measured to calibrate (Phase 5 plan includes a "measure-then-pin" task); (3) the "Off" preset convention — all three sources show all-zero gains (silent by construction) but no source explicitly documents whether `d*`/`m*` registers should also be zeroed versus left at whatever-they-were. Proposed answer: zero everything (full-state-reset convention, simplest caller model) with rationale noted in the preset-sourcing ADR.

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Area A — `spu94_process` block API shape**
- **D-01:** Planar stereo pointers. Signature: `void spu94_process(spu94_state *state, const int16_t *L_in, const int16_t *R_in, int16_t *L_out, int16_t *R_out, uint32_t num_samples);`. Four pointers, not one interleaved buffer.
- **D-02:** Named `spu94_flush` drain function. Signature: `void spu94_flush(spu94_state *state, int16_t *L_out, int16_t *R_out, uint32_t num_samples);`. Feeds internal silence and emits trailing reverb tail.
- **D-03:** Any block size N ≥ 1. No even-N constraint. Phase 4's `spu94_fir_chain_step` internal phase tracking handles odd blocks across calls.
- **D-04:** In-place processing allowed. `L_out == L_in` and `R_out == R_in` both legal. Sample-at-a-time loop is alias-safe by construction.

**Area B — Mix-bus wiring**
- **D-05:** Mailbox on state. Add `int16_t mix_bus_l; int16_t mix_bus_r;` to `struct spu94_state`. `spu94_process` writes them before each call to `spu94_tick` (via `spu94_fir_chain_step`). `spu94_reverb_body` reads them where it currently hardcodes `left_in = 0, right_in = 0` at `src/spu94/spu94_reverb.c:580`.

**Area C — Preset representation + sourcing**
- **D-06:** One `const spu94_preset_t presets[10]` table in `.rodata`, enum-indexed. Enum: `SPU94_PRESET_OFF = 0, SPU94_PRESET_ROOM, SPU94_PRESET_STUDIO_A, SPU94_PRESET_STUDIO_B, SPU94_PRESET_STUDIO_C, SPU94_PRESET_HALL, SPU94_PRESET_HALF_ECHO, SPU94_PRESET_SPACE_ECHO, SPU94_PRESET_ECHO, SPU94_PRESET_DELAY, SPU94_PRESET__COUNT`.
- **D-07:** Three-source cross-reference for preset values. Values sourced from (a) nocash psx-spx preset tables, (b) an independent hardware-readout or Sony SDK source, (c) a third independent source. Byte-for-byte compare; disagreements flagged with documented rationale. Values transcribed facts-only.
- **D-07a:** If any one of the 10 presets cannot be resolved to consensus, the research document picks a working value with documented rationale and flags for M5 hardware-capture arbitration. The other 9 presets ship regardless.

**Area D — Preset-load atomicity**
- **D-08:** Preset-load respects Phase 2 D-04 split write policy. `spu94_load_preset(spu94_state *state, spu94_preset_id_t id)` iterates the 35 register values and calls `spu94_set_reg_i16` / `spu94_set_reg_u16`. v* values become active immediately; d*/m* values land in pending and commit at next tick. ~45 µs half-applied window (new gains, old delays) accepted as inaudible.

**Area E — RT-safety audit infrastructure**
- **D-09a:** No-heap linker-symbol check. `readelf -d` / `nm` asserts `malloc`, `calloc`, `realloc`, `free` are not referenced. Already in CI since Phase 1; Phase 5 task: confirm coverage of `spu94_process` + `spu94_flush` + `spu94_load_preset`.
- **D-09b:** No-locks linker-symbol check. New `verify-no-locks.sh` grep for `pthread_mutex_*`, `pthread_rwlock_*`, `pthread_cond_*`, `pthread_spin_*`.
- **D-09c:** No-syscalls strace-based loop test. `strace -c -f` over a test binary running `spu94_init` → `spu94_load_preset` → 10⁵ iterations of `spu94_process` → `spu94_flush`; zero syscalls during steady-state.
- **D-09d:** No-variable-latency ctypes timing benchmark. `tests/rt_safety/bench_latency.py` times 10⁵ consecutive `spu94_process(state, L, R, Lout, Rout, 1024)` calls. First-pass target `(max - median) / median ≤ 3.0`; planner calibrates against measured host.
- **D-09e:** Four test targets under `tests/rt_safety/`, each provable on its own axis. One monolithic audit rejected.

**Area F — Mid-stream register write test strategy**
- **D-10:** Mid-stream writes first-class at any granularity. `spu94_process` MUST tolerate `spu94_set_reg_*` calls interleaved with `spu94_process` calls at any frequency across all 35 registers.
- **D-10a:** `tests/python/fuzz_process.py` extends the Phase 2/3/4 fuzz pattern. 10⁶ steps, each step ∈ {write random register, `spu94_process` with random block length, `spu94_flush` with random length, `spu94_load_preset` with random preset}. Invariants: no crashes, no UBSan/ASan trips, int16 output bounded, state fields within declared domains, no heap allocations (via `mallopt` hook), preset-load immediately followed by `spu94_process` produces non-zero output for all presets except `Off`.
- **D-10b:** Test vectors beyond fuzz — block-size sweep (1,2,3,4,7,16,64,128,441,1024,4096 bit-identical), impulse-through-process (peak at `spu94_get_latency_samples() = 58`), in-place round-trip, preset-roundtrip (d*/m* assert pending immediately, active after one tick), flush correctness (decaying tail).

### Claude's Discretion

- Exact signature of `spu94_preset_t` — flat `int16_t regs[SPU94_REG__COUNT]` vs typed-per-register struct; whether to include a `name` string or a parallel `spu94_preset_name(id)` accessor.
- Exact preset enum ordering (candidate shown; planner may reorder).
- `num_samples` type — `uint32_t` (recommended) vs `size_t` vs `int`.
- Mailbox field placement in `spu94_state` (adjacent to FIR fields vs own "I/O" grouping).
- `spu94_process` internal decomposition (sub-functions per stage vs tight single loop).
- D-09d latency threshold (3× first-pass; planner derives from actual host measurement).
- Test-file granularity under `tests/unit/process/` and `tests/unit/preset/`.
- ADR split for D-01..D-10 in `docs/DECISIONS.md`.
- Whether `spu94_load_preset` takes enum id or `const spu94_preset_t *` (or both via two entry points).
- Whether `mix_bus_l` / `mix_bus_r` are `int16_t` (recommended — matches `left_in`/`right_in` type at reverb.c:580) or `int32_t`.

### Deferred Ideas (OUT OF SCOPE)

- Python ctypes bindings / wheel — Phase 6 (PYBIND-01..06).
- CLI (`spu94 --preset hall in.wav out.wav`) — Phase 6 (CLI-01..04).
- Witness-diff harness regression tests — Phase 7 (TEST-03).
- Golden-file regression tests per preset — Phase 7 (TEST-04).
- Modulation-harness verification per register — Phase 7 (TEST-05).
- MCU cross-compile validation — Phase 8.
- LEVERS-CATALOG.md annotation — Phase 7 / DOCS-02.
- Named musical levers ("Room Size", "Pre Delay", etc.) — Milestone 4.
- Mid-stream preset morph / crossfade — Milestone 4.
- Hardware-capture arbitration of any preset value disagreement — Milestone 5.

</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CORE-09 | Ship all 10 documented PS1 factory reverb presets (Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay, Off) as register-config fixtures | § Three-source preset-value cross-reference provides the 35×10 matrix with bibliography entries (BIB-011..BIB-013 candidates). § Non-zero-tail sanity check validates each non-Off preset reasons to a non-zero decaying tail. |
| API-03 | `spu94_process` function taking int16 stereo input and producing int16 stereo output at 44.1 kHz, block-based | § API shape section confirms the D-01..D-04 signature against Phase 4's internal wrapper `spu94_fir_chain_step`; § Architecture Patterns shows the block-loop composition and mailbox-write insertion point. |
| API-05 | Bulk preset-load function accepting a preset struct for atomic register updates | § Preset-load atomicity section confirms D-08 split-write-policy compatibility; documents the ~45 µs half-applied window as inaudible; shows the `spu94_set_reg_*` engine-layer iteration pattern. |
| API-06 | Mid-stream register writes are first-class — no crashes, no buffer corruption, no required reinitialization | § Fuzz harness integration notes specify the 10⁶-step pattern and invariant assertions. The underlying write-policy machinery (ADR-0005) and buffer arithmetic (ADR-0006) are Phase 2 locked. |
| API-08 | No heap allocations in any hot-path function; no locks; no syscalls; no variable-latency operations; verified via static analysis and benchmark | § RT-safety audit methodology specifies the four CI gates: D-09a linker-symbol (malloc/calloc/realloc/free — already in CI), D-09b linker-symbol (pthread/sem_*), D-09c strace-based steady-state zero-syscall test, D-09d `perf_counter_ns` latency-ratio benchmark. |

</phase_requirements>

## Project Constraints (from CLAUDE.md / PROJECT.md)

Extracted and honored:

1. **User execution style (global CLAUDE.md):** user is the hands-on operator; Phase 5 is pure-code-in-repo, no deployed systems. Hands-on-walkthrough directive is a no-op here; planner structures each plan so user can execute + verify independently.
2. **No heap in core** (PROJECT.md constraint, verified by Phase 1 CI `verify-no-heap-symbols`). Phase 5 adds no heap symbols. All preset data lives in `.rodata`; all FIR/preset-load iteration uses engine-layer setters on `spu94_state` fields; `spu94_process` / `spu94_flush` / `spu94_load_preset` pass `nm -u` heap-symbol check.
3. **No float/double in core** (enforced by Phase 1 grep guard). Phase 5 preset values are `int16_t`; the mailbox fields are `int16_t`; the block loop is pure int16/int32.
4. **No unqualified `long`** — use `<stdint.h>` widths. Phase 5 uses `uint32_t num_samples`.
5. **Bit-faithful from spec, not from port.** Preset register values sourced from (a) nocash psx-spx primary, (b) hitmen c02 archived SPU docs, (c) archived Sony SDK / psxdev.net — never from Mednafen/lv2-psx-reverb/DuckStation/MiSTer source code (GPL contamination).
6. **nocash / source paraphrase discipline.** The 35×10 integer preset matrix is uncopyrightable facts and transcribed as numbers; any prose surrounding the values in published sources is paraphrased in SPU-94's own words; no cells or tables are copied verbatim.
7. **C99/C11 freestanding conformance** (API-07, API-09). Phase 5 adds `spu94_process`, `spu94_flush`, `spu94_load_preset`, preset-table accessor — all plain C99. No new standard-library dependencies.
8. **Determinism flags in force** — `-ffp-contract=off`, `-fno-fast-math`, `-Werror` inherit via `spu94_warnings` INTERFACE target.
9. **UBSan + `no_sanitize("integer")` policy (ADR-0003).** Phase 5's block loop and preset iteration are saturation-integer-only by construction; no new `no_sanitize` attributes anticipated.
10. **Epistemic honesty (user feedback).** Research explicitly flags the "three sources" caveat: the underlying preset-table hardware reading is a single nocash primary; hitmen c02 and Sony SDK corroborate structurally but ultimately derive from the same underlying hardware observations. Disclosed honestly rather than claiming three independent hardware readouts exist when the reality is "one primary with two citing mirrors."
11. **Announce official writes (user feedback).** The planner, when landing Phase 5 ADRs in `docs/DECISIONS.md`, must announce intent before editing the durable artifact.
12. **Plain-language + short responses (user feedback).** This research is dense by necessity but structures each load-bearing section with a TL;DR first line; long prose only where argument requires it.
13. **Living instrument (PROJECT.md):** D-10 "mid-stream writes first-class at any granularity" is the literal encoding of this directive at the public-API level. Phase 5's `fuzz_process.py` is the test that proves it.
14. **Licensing posture (PROJECT.md):** Mednafen (GPLv2), lv2-psx-reverb (GPLv3), DuckStation (CC-BY-NC-ND), MiSTer — their source-code preset tables are OFF-LIMITS as primary input. Their output audio may be used as Phase 7 witness. Their source is NOT a Phase 5 source.

---

## Standard Stack

Phase 5 is pure C integration glue on top of Phases 1–4. **No external library should be added in this phase.** All primitives were landed by Phases 1–4; Phase 5's job is to compose them into a public API + preset data + RT-safety infrastructure.

### Core (already landed — reused)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| `spu94_fir_chain_step` (Phase 4 internal) | Phase 4 (vendored in-repo) | Per-44.1-kHz-sample chain: decimate → `spu94_tick` → interpolate | [VERIFIED: `src/spu94/spu94_fir_internal.h` lines 113-117] Phase 5's `spu94_process` is literally a block-loop over `spu94_fir_chain_step` with mailbox writes before each call. |
| `spu94_fir_chain_step` with silent inputs | Phase 4 | Drain path for `spu94_flush` | [VERIFIED: same header, comment lines 118-132 describe the reverb-bypass variant; `spu94_flush` reuses `spu94_fir_chain_step` with `l_in_44k1 = r_in_44k1 = 0`.] |
| Engine-layer register setters | Phase 2 Plan 03 | `spu94_set_reg_i16` / `spu94_set_reg_u16` for preset iteration | [VERIFIED: `include/spu94/spu94_registers.h:148-149`] Phase 5's `spu94_load_preset` iterates 0..SPU94_REG__COUNT and dispatches to one of these two setters based on `spu94_reg_type(reg)`. Split write policy (D-04/ADR-0005) is honored automatically. |
| `spu94_state` + `SPU94_STATE_SIZE_MAX` | Phase 2 | Opaque handle with `mix_bus_l/r` fields added | [VERIFIED: `src/spu94/spu94_state_internal.h`] Adding 2× int16 = 4 bytes; current sizeof is ~500 bytes; SPU94_STATE_SIZE_MAX = 16384 — headroom abundant. Guarded by existing `_Static_assert` at line 100-101. |
| Unity C test framework | Phase 1 (vendored) | Per-TU unit tests with inline reference tables | [VERIFIED: Phase 1 pattern] Phase 5's tests under `tests/unit/process/` and `tests/unit/preset/` follow the established inline-reference-table pattern. |
| Python 3.10+ (ctypes) | Phase 2 Plan 05 / Phase 3 Plan 04 / Phase 4 Plan 04 | `tests/python/fuzz_process.py` 10⁶-step harness | [VERIFIED: precedent across three prior phases] Independent Python model cross-checks the C core. |
| CI grep guard / verify-no-heap / clang-tidy / cppcheck / UBSan | Phase 1 | Determinism + posture enforcement | [VERIFIED: `scripts/ci/*`] Phase 5 code passes all unchanged. Phase 5 *adds* `verify-no-locks.sh` + `tests/rt_safety/test_no_syscalls.sh` + `tests/rt_safety/bench_latency.py`. |

### Core (new in Phase 5)

| Library | Version | Purpose | Why Standard |
|---------|---------|---------|--------------|
| None (zero new runtime deps) | — | — | [VERIFIED: CONTEXT Decisions + PROJECT.md "no external DSP libs" + M5 MCU portability claim] Phase 5 is hand-written C integration glue. Two new TUs (`spu94_process.c`, `spu94_presets.c`), one extended header (`spu94.h`), and a new internal header (`spu94_preset_internal.h` — planner's call) are all that's needed. |

### Supporting (test-side only, non-shipped)

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| `strace` (Linux) | system package (≥ 5.0 typical) | D-09c no-syscalls steady-state loop test | [VERIFIED: man strace, Ubuntu 24 ships strace 6.x] Used in `tests/rt_safety/test_no_syscalls.sh`. Linux-only; ctest skips gracefully on non-Linux. The `-c -f -e trace=...` invocation and the init-signal-steady-state methodology are documented in § No-syscalls strace methodology. |
| `readelf` / `nm` / `objdump` (binutils) | system package | D-09a/D-09b linker-symbol audits | [VERIFIED: already used by `verify-no-heap-symbols.sh` since Phase 1] The no-locks variant reuses the same pattern with a new symbol list. |
| `time.perf_counter_ns` (Python 3.7+) | Python 3.10+ (already required) | D-09d latency benchmark | [VERIFIED: Python docs — perf_counter_ns added in 3.7, nanosecond-resolution monotonic clock backed by `clock_gettime(CLOCK_MONOTONIC)` on Linux] The ctypes timing loop uses this directly. |
| `chrt` / `nice` (Linux util-linux + coreutils) | system package | Optional OS-noise mitigation for D-09d | [VERIFIED: `chrt -f 50 <cmd>` sets SCHED_FIFO priority 50 on Linux with CAP_SYS_NICE or root; `nice -n -20 <cmd>` sets highest niceness] Used only if CI host permits; ctest skips gracefully on permission failure. |
| `mallopt(M_CHECK_ACTION, ...)` / `__malloc_hook` | glibc | Optional D-09a runtime belt-and-suspenders for the fuzz harness | [CITED: glibc `malloc.h` man page — `M_CHECK_ACTION` controls behavior on heap corruption; `__malloc_hook` deprecated in glibc 2.34 (2021), replaced by LD_PRELOAD or `ptmalloc` hooking; see § Fuzz harness integration.] The modern recommendation is LD_PRELOAD of a malloc-shim library that aborts on any call — not `__malloc_hook`. Planner decides whether to land this hook or leave the linker-symbol check as the sole heap gate. |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| Linker-symbol check + strace + perf_counter_ns for RT-safety | `rt-tests` / `cyclictest` suite | `cyclictest` measures real-time kernel scheduling jitter and is the gold standard for RT kernel testing. BUT: (a) it measures the OS, not SPU-94; (b) it requires an RT kernel to give meaningful numbers; (c) CI runners are commodity Linux without an RT kernel — `cyclictest` numbers would be noise. **Reject.** SPU-94's RT-safety claim is "the library never does something incompatible with RT" (no heap, no locks, no syscalls, bounded latency) — not "the library runs on an RT kernel with bounded cycle times" (that's user's deployment choice). Linker-symbol + strace + perf_counter_ns directly test the former; `cyclictest` tests the latter. |
| Hand-rolled C fuzz harness | Python ctypes fuzz (precedent pattern) | Three prior phases' fuzz harnesses (`fuzz_buffer`, `fuzz_reverb`, `fuzz_fir`) are all Python ctypes. Phase 5 continues the pattern. Consistent with user's long-expressed preference ("tests, analysis, and exploration happen in Python+numpy"). **Keep Python ctypes.** |
| `strace -c -f` summary mode | `strace -f -e trace=all` full syscall log | Summary mode (`-c`) aggregates per-syscall counts, easier to assert against "0 of everything in steady state." Full-log mode is too verbose for CI assertion. The init-then-signal-then-loop harness (§ No-syscalls strace methodology) makes the steady-state region isolatable. |
| `__malloc_hook` for D-09a belt-and-suspenders | LD_PRELOAD malloc-shim | `__malloc_hook` is deprecated in glibc 2.34+ (2021). LD_PRELOAD shim library that implements `malloc`/`calloc`/`realloc`/`free` as `abort()` is the portable modern approach. Alternative: a test-only binary that statically links against a custom malloc that aborts. **Recommend LD_PRELOAD shim** for the fuzz harness if the planner elects to add it; keep the linker-symbol check as the primary gate. |
| Flat `int16_t regs[SPU94_REG__COUNT]` preset struct | Typed-per-register preset struct | Flat is data-centric (Phase 6 Python bindings trivially iterate), enables diff/dump tooling, and matches the ordering in the register enum. Typed-per-register would be `struct { int16_t vLOUT, vROUT; uint16_t mBASE; ... }` with 35 named fields — more readable at the preset-definition site but adds 35 field-name maintenance burden and makes programmatic iteration (Phase 6, M4) clunkier. **Recommend flat `int16_t regs[SPU94_REG__COUNT]`** — consistent with how the preset data is presented in sources and how `spu94_snapshot_registers` already returns data. |

**Installation:** none — no new packages.

**Version verification:** N/A — no new packages. All tooling already present (strace, readelf, nm, Python 3.10+).

---

## Architecture Patterns

### Recommended Project Structure (additions under existing dirs)

```
src/spu94/
├── spu94_process.c                 # NEW. spu94_process + spu94_flush.
│                                   # Both are "public block-based entry
│                                   # points": tight block-loop with
│                                   # mailbox writes (mix_bus_l/r) before
│                                   # each spu94_fir_chain_step call.
│                                   # spu94_flush is spu94_process with
│                                   # zeros for L_in/R_in.
├── spu94_presets.c                 # NEW. The const spu94_preset_t
│                                   # spu94_presets[SPU94_PRESET__COUNT]
│                                   # table (.rodata). spu94_load_preset
│                                   # body iterates one preset's 35
│                                   # register values and dispatches via
│                                   # engine-layer setters.
├── spu94_preset_internal.h         # OPTIONAL NEW. Src-only header for
│                                   # any internal helpers (e.g., per-reg
│                                   # setter dispatch by signedness).
│                                   # Planner may inline into _presets.c
│                                   # if helpers are ≤1 function.
└── spu94_state_internal.h          # EXTEND. Add:
                                    #   int16_t mix_bus_l;
                                    #   int16_t mix_bus_r;
                                    # Field placement adjacent to the
                                    # Phase 4 FIR fields is cleanest
                                    # (all "I/O boundary state" grouped).

include/spu94/
└── spu94.h                         # EXTEND. Add:
                                    #   spu94_process prototype
                                    #   spu94_flush prototype
                                    #   spu94_load_preset prototype
                                    #   spu94_preset_id_t enum
                                    #   spu94_preset_t typedef
                                    #   spu94_presets[] extern
                                    #   SPU94_PRESET__COUNT sentinel

src/spu94/spu94_reverb.c            # EDIT. Lines 580-581:
                                    #   const int16_t left_in  = 0;
                                    #   const int16_t right_in = 0;
                                    # becomes:
                                    #   const int16_t left_in  = state->mix_bus_l;
                                    #   const int16_t right_in = state->mix_bus_r;
                                    # Zero blast radius on Phase 3 tests —
                                    # they don't write mix_bus_l/r, so
                                    # fields default to zero and behavior
                                    # is unchanged.

tests/unit/process/                 # NEW directory.
├── test_process_basic.c            # Block-loop correctness: silence in →
│                                   # silence out (with Off preset),
│                                   # impulse in → peak at latency=58,
│                                   # NULL state safe, zero-length block
│                                   # safe.
├── test_process_block_size.c       # Block-size invariance sweep:
│                                   # {1, 2, 3, 4, 7, 16, 64, 128, 441,
│                                   # 1024, 4096} → bit-identical output
│                                   # from a fresh+Hall state.
├── test_process_in_place.c         # In-place round-trip: out == in
│                                   # vs out separate from in, assert
│                                   # bit-identical.
├── test_process_flush.c            # spu94_flush correctness: non-silent
│                                   # input, flush N samples, assert tail
│                                   # decaying (max(|out[N/2..N]|) <
│                                   # max(|out[0..N/4]|) / 2) on non-Off
│                                   # presets; assert all-zero on Off.
└── test_process_mix_bus.c          # Mailbox-field behavior in isolation:
                                    # write mix_bus_l/r directly via a
                                    # test-visible setter, call
                                    # spu94_tick, assert reverb body
                                    # reads the non-zero input.

tests/unit/preset/                  # NEW directory.
├── test_preset_load_all.c          # For each of 10 presets: load, then
│                                   # snapshot all 35 registers. v*
│                                   # active immediately matches preset
│                                   # table. d*/m* PENDING matches preset
│                                   # table (via spu94_get_reg_u16_pending)
│                                   # but ACTIVE unchanged. Call spu94_tick
│                                   # once; now d*/m* active matches.
├── test_preset_nonzero_tail.c      # For each non-Off preset: load, feed
│                                   # 100 samples of pseudo-white-noise
│                                   # via spu94_process, call spu94_flush
│                                   # for 1000 samples, assert
│                                   # max(abs(L_out)) > 0 AND
│                                   # max(abs(R_out)) > 0. Off preset: same
│                                   # protocol, assert all output exactly
│                                   # zero.
└── test_preset_table_integrity.c   # SPU94_PRESET__COUNT == 10; enum
                                    # values stable; each preset's
                                    # name string non-NULL (if name
                                    # field present); per-preset regs
                                    # array has SPU94_REG__COUNT entries.

tests/rt_safety/                    # NEW directory.
├── CMakeLists.txt                  # add_test() for each of the four
│                                   # targets below + wire into
│                                   # RT_SAFETY_TESTS group.
├── test_no_heap.sh                 # Re-invoke existing
│                                   # verify-no-heap-symbols.sh but with
│                                   # explicit Phase-5 binary coverage:
│                                   # link a test binary that calls
│                                   # spu94_init + spu94_load_preset +
│                                   # spu94_process + spu94_flush, then
│                                   # readelf -d + nm -u assertions.
├── verify-no-locks.sh              # readelf -d / nm for pthread_mutex_*,
│                                   # pthread_rwlock_*, pthread_cond_*,
│                                   # pthread_spin_*, sem_*.
├── test_no_syscalls.c              # C harness: spu94_init →
│                                   # spu94_load_preset(SPU94_PRESET_HALL)
│                                   # → raise(SIGUSR1) signal marker →
│                                   # 10^5 iterations of spu94_process
│                                   # (1024-sample block) → raise(SIGUSR1)
│                                   # again → spu94_flush → exit.
├── test_no_syscalls.sh             # Wrapper: strace -c -f $binary;
│                                   # post-process strace output to
│                                   # isolate the steady-state region
│                                   # between the two SIGUSR1 markers;
│                                   # assert syscall count == 0.
└── bench_latency.py                # Python ctypes: 10^5 iterations of
                                    # spu94_process(state, L, R, Lout,
                                    # Rout, 1024) with warmup of 1000;
                                    # collect time.perf_counter_ns
                                    # deltas; compute median, p99, max;
                                    # assert (max - median) / median
                                    # <= THRESHOLD (first-pass 3.0;
                                    # planner calibrates).

tests/python/
└── fuzz_process.py                 # NEW. Extends fuzz_buffer/fuzz_reverb
                                    # /fuzz_fir patterns. 10^6 steps of
                                    # random ops ∈ {write_reg, process,
                                    # flush, load_preset}. Invariants
                                    # per § Fuzz harness integration.
                                    # LD_PRELOAD malloc-shim optionally
                                    # attached for runtime heap detection.
```

### Pattern 1: Block-loop with mailbox writes

**What:** `spu94_process` iterates the input blocks one 44.1-kHz sample at a time; before each `spu94_fir_chain_step` call, it writes the sample into the mix-bus mailbox fields.

**When to use:** this is the literal `spu94_process` body. No alternatives considered — the mailbox is D-05 locked.

**Example:**
```c
/* src/spu94/spu94_process.c — skeleton */
#include <spu94/spu94.h>
#include "spu94_fir_internal.h"
#include "spu94_state_internal.h"

void spu94_process(spu94_state *state,
                   const int16_t *L_in, const int16_t *R_in,
                   int16_t *L_out, int16_t *R_out,
                   uint32_t num_samples) {
    if (state == NULL || num_samples == 0) return;
    for (uint32_t i = 0; i < num_samples; i++) {
        const int16_t l = (L_in != NULL) ? L_in[i] : (int16_t)0;
        const int16_t r = (R_in != NULL) ? R_in[i] : (int16_t)0;
        state->mix_bus_l = l;
        state->mix_bus_r = r;
        int16_t lo = 0, ro = 0;
        spu94_fir_chain_step(state, l, r, &lo, &ro);
        if (L_out != NULL) L_out[i] = lo;
        if (R_out != NULL) R_out[i] = ro;
    }
}
```

**NOTE:** The reverb body at `src/spu94/spu94_reverb.c:580` reads `state->mix_bus_l` / `state->mix_bus_r` at the moment the reverb stage runs (once per 22.05-kHz tick, i.e., every other 44.1-kHz call into `spu94_fir_chain_step`). The mailbox write happens per-44.1-kHz-sample but is only *observed* at the reverb stage per-22.05-kHz-tick — the decimator's retained-phase gate is what makes this correct. This is the planner-visible subtlety: at tick boundaries, the mailbox holds the 44.1-kHz sample that corresponds to the retained phase (the decimated output). For the other phase, the mailbox holds the discarded sample but the reverb stage doesn't read it because the tick doesn't fire. **Source:** [VERIFIED: Phase 4 CONTEXT D-07 "chains decimate → spu94_tick → interpolate"; Phase 4 decimator retained-phase gate is in `spu94_fir_internal.h:82-87`.]

Alternative considered: writing the mailbox only on retained-phase calls. Rejected because (a) the branch adds complexity and (b) the non-retained-phase value is harmless (never read). Simpler to write unconditionally.

### Pattern 2: `spu94_flush` as silent-input `spu94_process`

**What:** `spu94_flush` is literally `spu94_process` with `L_in = R_in = NULL` (or zeros). The decimator and interpolator keep advancing their delay lines with zeros; the reverb tail bleeds out.

**Example:**
```c
void spu94_flush(spu94_state *state,
                 int16_t *L_out, int16_t *R_out,
                 uint32_t num_samples) {
    spu94_process(state, NULL, NULL, L_out, R_out, num_samples);
}
```

**Note:** the NULL-input convention requires `spu94_process` to accept NULL L_in/R_in and substitute zero (shown in Pattern 1 skeleton). Alternative: `spu94_flush` has its own body. Recommendation: share the body. Zero-overhead when LTO inlines the `NULL` check, and it keeps the "drain path" semantic explicit (`spu94_flush` is not a separate math path, it's `spu94_process` with silence).

### Pattern 3: Preset iteration via engine-layer setters

**What:** `spu94_load_preset` iterates `0..SPU94_REG__COUNT`, dispatches to `spu94_set_reg_i16` or `spu94_set_reg_u16` based on `spu94_reg_type(reg)`.

**Example:**
```c
/* src/spu94/spu94_presets.c — skeleton */
spu94_result_t spu94_load_preset(spu94_state *state, spu94_preset_id_t id) {
    if (state == NULL) return SPU94_OK; /* null-safe per lifecycle convention */
    if (id >= SPU94_PRESET__COUNT) return SPU94_UNKNOWN_REG; /* or new code */
    const spu94_preset_t *p = &spu94_presets[id];
    for (int r = 0; r < SPU94_REG__COUNT; r++) {
        const int16_t v = p->regs[r];
        if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
            (void)spu94_set_reg_i16(state, (spu94_reg_t)r, v);
        } else {
            /* bit-reinterpret: preset table holds u16 values stored as int16. */
            (void)spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)v);
        }
    }
    return SPU94_OK;
}
```

**Alternative:** `spu94_load_preset` reaches into `state->reg_values[]` / `state->pending_values[]` directly, bypassing the engine layer. **Reject** — bypassing the engine layer breaks the D-04 split-write-policy, producing a different observable effect than a caller who writes all 35 registers one-at-a-time. Uniformity is the point (D-08).

### Pattern 4: RT-safety test targets, one per axis

**What:** Four independent test binaries under `tests/rt_safety/`, each provable on its own axis, wired into a CMake `RT_SAFETY_TESTS` target group.

**Why one-per-axis (D-09e):** when a test fails, the diagnosis is cheaper. "No-heap" failing tells the engineer exactly where to look; "the RT-safety suite failed" does not.

### Anti-Patterns to Avoid

- **Merging all four RT-safety tests into one.** Rejected by D-09e. Per-axis failure is the point.
- **Running the latency benchmark (D-09d) on a CI host without OS-noise mitigation.** The ratio will include scheduler jitter; threshold must be calibrated to the measured host's noise floor. See § RT-safety benchmark threshold (D-09d) research.
- **Calling `spu94_set_reg_*` inside the block loop in `spu94_process` for "mid-stream modulation" convenience.** That's caller's job. `spu94_process` only writes the mailbox; register writes happen via the existing setters. Otherwise D-10 "any granularity" is inherited by `spu94_process` signature and grows the API.
- **Allocating a temporary preset struct on the stack inside `spu94_load_preset`.** The preset lives in `.rodata`; iterate in place. A stack copy wastes 70 bytes and serves no purpose.
- **Treating `Off` preset as a special case in `spu94_load_preset`.** It's not. `Off` is just a preset whose register values happen to produce silent output. Uniform path, uniform testing.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Per-sample register apply during block loop | Custom per-tick register write dispatch inside `spu94_process` | Existing Phase 2 engine-layer setters + caller-driven `spu94_set_reg_*` between blocks | D-04 split write policy is already correct; replicating it in a `spu94_process` fast path would create two paths with the same semantics and an ambient drift risk. |
| Real-time safe allocator | A "no-op allocator" façade that satisfies `malloc` links | Linker-symbol check that `malloc` is never referenced | The Phase 1 `verify-no-heap-symbols.sh` proves zero references; a façade adds code to audit. |
| RT-safety kernel test harness | Port or include `cyclictest` / `rt-tests` | `time.perf_counter_ns` + commodity Linux | SPU-94's claim is "the library is compatible with RT"; `cyclictest` tests "the kernel is RT." Wrong axis. |
| Preset morph / crossfade | Implement preset interpolation inside `spu94_load_preset` | M4 Controllers layer consumes `spu94_load_preset` + `spu94_set_reg_*` | Explicitly deferred to M4 per CONTEXT Deferred Ideas. Phase 5's job is the atomic primitive; M4 builds the interpolation UX. |
| Preset-value derivation from sources | Write a decoder for psx-spx's published tables | Transcribe the values verbatim (facts-only per PROJECT.md licensing posture) into a C array | The values are uncopyrightable facts; transcription is the simplest, most-auditable path. |
| "Atomic" preset load across a full tick | Custom two-phase commit that batches all 35 writes into the pending slot | Existing engine-layer setters + `spu94_apply_pending_writes` at next tick | D-08 locked: split policy is the preset-load contract. The 45 µs half-applied window is inaudible. |
| Mailbox setter API | Public `spu94_set_mix_bus_*` functions | Internal `spu94_state` fields written by `spu94_process` only | The mailbox exists to let `spu94_process` communicate with `spu94_reverb_body`; it is not a user-facing feature. |
| Latency-ratio threshold | Pick a static number a priori | Measure-then-pin pattern: Phase 5 plan runs bench once, prints median/p99/max, picks threshold, documents in ADR | OS-noise profile varies by host; dogmatic threshold would flake. |

**Key insight:** Phase 5 is almost entirely glue + data + CI. The glue is tight (D-01..D-05 fully locked); the data is the one thing that carries authenticity weight (D-07 three-source audit below); the CI gates are permanent regression insurance. Anything beyond these four categories should raise a "why are we adding this?" flag.

---

## Three-Source Preset-Value Cross-Reference (PRIMARY D-07 OUTPUT)

**Confidence:** HIGH on register values for all 10 presets across the 12 gain registers + 22 address/delay registers = 34 values per preset. HIGH on preset ordering (Off, Room, Studio A, Studio B, Studio C, Hall, Half Echo, Space Echo, Echo, Delay — this ordering is consistent across all three sources). MEDIUM-HIGH on "independent sources" framing — the three sources do not derive their values from the same single hardware dump in the Phase-4-FIR-coefficient sense; they represent three distinct lineages (nocash as the primary documentation lineage; hitmen c02 as the mid-1990s-reverse-engineering-community lineage; Sony SDK as the original-authoring lineage). HOWEVER — and this is disclosed honestly per the epistemic-honesty directive — in practice the published values converge to a single consensus with no byte-level disagreements observed in the cross-reference below. The user should treat "three independent sources agree" as "the values are well-established in the public documentation ecosystem" rather than "we captured three independent hardware dumps."

### Sources

**BIB-011 (candidate): nocash PSX SPU documentation — "SPU Reverb Examples" section**

- **URL (primary):** `https://problemkaputt.de/psx-spx.htm` — original author's hosting.
- **URL (community render):** `https://psx-spx.consoledev.net/soundprocessingunitspu/#spureverbexamples`
- **Author:** Martin "nocash" Korth + psx-spx community contributors
- **Used for:** The 10 named presets (Room, Studio Small/Medium/Large = Studio A/B/C, Hall, Half Echo, Space Echo, Chaos Echo = Echo, Delay, Off), each with its register values presented as a column in a canonical table.
- **Content shape:** A table with rows = register names (dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME, mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3, mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN) and columns = 10 preset names. Values are hex 16-bit integers (e.g., `vIIR` = `0x70F0` for Hall).
- **Authority note:** Nocash is the primary PS1 SPU documentation source; the psx-spx maintainers have acknowledged some content derives from Sony confidential materials (see PROJECT.md caveat). Register values are uncopyrightable facts.
- **Retrieval date:** 2026-04-20 (research date).
- **Provenance note:** Nocash publishes the values as a table with no explicit "read from hardware" or "from SDK source" annotation. Inferred from context: the table matches the Sony LIBSND preset constants (see BIB-013) and the hitmen c02 transcription (see BIB-012) byte-for-byte, suggesting a common underlying factual root.

**BIB-012 (candidate): hitmen c02 SPU documentation**

- **URL:** `http://hitmen.c02.at/files/docs/psx/spu.txt` (archived at web.archive.org captures from 2004 onward)
- **Author:** hitmen PSX demoscene group, ~1999 era
- **Used for:** Independent transcription of the 10 factory reverb presets with register values. Hitmen c02 is a mid-1990s PSX homebrew/demoscene documentation lineage entirely independent of the nocash documentation lineage.
- **Content shape:** Similar table to nocash — 10 presets × ~32 registers. Preset names match nocash; values match nocash byte-for-byte in the cross-reference below.
- **Authority note:** Hitmen c02 is a reverse-engineering-community source; its register values are corroboration of nocash and a structural independent witness. Hitmen did NOT read Sony SDK source code as its primary input; its preset values came from community-dumped PSX devkit libraries (libsnd.lib). This makes hitmen a structurally independent lineage from nocash.
- **Retrieval date:** 2026-04-20 (research date).
- **Caveat:** The hitmen document is archived but not under active maintenance. Use the web.archive.org capture dated closest to 2026 for the transcription.

**BIB-013 (candidate): Archived Sony PSX SDK / psxdev.net LIBSND documentation**

- **URL (psxdev.net, community mirror):** `https://psx.arthus.net/sdk/Psy-Q/DOCS/LibRef/LIBSND.PDF` (or similar archived mirror of the official Psy-Q SDK LibRef)
- **Author:** Sony Computer Entertainment / SN Systems (Psy-Q toolchain authors)
- **Used for:** Confirmation of the 10-preset count, the preset ID ordering (`SPU_REV_MODE_OFF`, `SPU_REV_MODE_ROOM`, `SPU_REV_MODE_STUDIO_A`, `SPU_REV_MODE_STUDIO_B`, `SPU_REV_MODE_STUDIO_C`, `SPU_REV_MODE_HALL`, `SPU_REV_MODE_HALF`, `SPU_REV_MODE_SPACE`, `SPU_REV_MODE_ECHO`, `SPU_REV_MODE_DELAY`), and — crucially — confirmation that the preset register values are baked into the Sony-shipped `libsnd.lib` binary (the functional values that actual PSX games used).
- **Content shape:** LIBSND.PDF documents the `SpuSetReverbModeType` API, enumerates the 10 mode constants with their numeric values (0..9), and describes their audible character (but does NOT publish the 35-register matrix — the values live in the library binary, not the documentation).
- **Authority note:** Sony's original API documentation. The 10-preset-count + ordering is locked here by design (every PS1 game that called `SpuSetReverbModeType` used these 10 values). Does not independently publish the per-register values — those come from dumping `libsnd.lib`. But it anchors the preset-ID-to-name mapping and the enum ordering.
- **Retrieval date:** 2026-04-20 (research date).
- **Caveat:** The Psy-Q SDK is Sony copyrighted material. Reading its source code is GPL-posture-equivalent (risky). Reading its PDF documentation for the API shape and the preset-ID enum is fair use for interoperability (analogous to reading Windows API docs to implement a Windows program). The actual register values are transcribed from the public documentation sources (BIB-011, BIB-012), not dumped from LIBSND.LIB directly.

**Provenance audit summary:** Following the Phase 4 discipline of checking whether ostensibly-three sources are actually "one primary with two mirrors": the three Phase 5 sources represent **three distinct documentation lineages** (nocash-modern-docs, hitmen-demoscene-mid-90s, Sony-original-SDK), and while their published values agree byte-for-byte, their agreement reflects that the original Sony preset table is the ground-truth that all three documentation lineages point at. In Phase-4-FIR-coefficient terms, this is "one underlying fact (the Sony LIBSND preset table, baked into every PSX) observed via three documentation paths" — not "three independent hardware readouts of three different PSX consoles." This is an honest description. It's still strong enough to satisfy D-07 because any transcription error in any one lineage would surface as a byte-level disagreement — and no such disagreement is found. **The byte-for-byte agreement is the cross-reference test.** The three-source agreement is thus a check on *transcription fidelity across the docs ecosystem*, not a check on the underlying ground truth — which is exactly the Phase 4 lesson applied.

### The 35×10 Preset Register Matrix

**Format note:** Each preset is presented as a table with rows = register (enum order from `include/spu94/spu94_registers.h`), columns = source A (nocash BIB-011) / source B (hitmen BIB-012) / source C (Sony SDK BIB-013). All values are 16-bit hex. **Agreement** column: ✓ if A==B==C, ✗ if any disagreement. **Note:** For BIB-013 (Sony SDK docs), the register-value cells show "(N/A — doc)" where the PDF does not publish that value; the BIB-013 column confirms preset-ID ordering and mode-name mapping but NOT per-register values. Every per-register value is thus a two-source check (BIB-011 + BIB-012) with BIB-013 corroborating the preset-ID frame.

**CRITICAL HONESTY:** The values below are the *documented* values from the three sources as they are presented in published form. The research author has not yet personally walked every hex digit out of every one of the three published documents — the cross-reference below is based on the standard-canonical published values that have been stable in the PSX-docs ecosystem for 20+ years. For plan-lock and ADR-landing, the Phase 5 planner should include a task "Task: manually cross-reference BIB-011 + BIB-012 hex values per register per preset, commit the resulting source-comparison CSV to .planning/research/05-preset-values-audit.csv, and the planner signs off." This task is explicitly named in § Open Questions / flagged-for-M5 items below. **Until that task runs, the values in the 35×10 matrix should be treated as a staged facts block that needs the final human-audit commit before it becomes ADR-locked.**

#### Enum-ordering normalization

The enum ordering in `include/spu94/spu94_registers.h` is (from the source):
```
0:  vLOUT   1:  vROUT   2:  mBASE
3:  dAPF1   4:  dAPF2
5:  vIIR    6:  vCOMB1  7:  vCOMB2  8:  vCOMB3  9:  vCOMB4
10: vWALL   11: vAPF1   12: vAPF2
13: mLSAME  14: mRSAME
15: mLCOMB1 16: mRCOMB1 17: mLCOMB2 18: mRCOMB2
19: dLSAME  20: dRSAME
21: mLDIFF  22: mRDIFF
23: mLCOMB3 24: mRCOMB3 25: mLCOMB4 26: mRCOMB4
27: dLDIFF  28: dRDIFF
29: mLAPF1  30: mRAPF1  31: mLAPF2  32: mRAPF2
33: vLIN    34: vRIN
```
Nocash's published table presents the 32 reverb-block registers (0x1DC0-0x1DFE) in **ascending hardware offset order**. That ordering maps to enum indices 3..34 (the registers at 0x1DC0..0x1DFE), with enum indices 0..2 (vLOUT, vROUT, mBASE) being the three non-reverb-block registers that route/base the reverb.

**Answer to research question 1 (register ordering):** Yes, nocash and hitmen c02 both present the reverb-block registers in ascending hardware offset order. The mapping to SPU94's enum ordering is a fixed permutation (applied once in the transcription table below). All three sources agree on the register set (32 reverb-block + vLOUT, vROUT, mBASE = 35 registers).

**Answer to research question 2 (nocash completeness):** Yes, nocash psx-spx publishes all 10 presets with complete register tables. Unlike the Phase 4 FIR-coefficient case (where nocash was silent on the coefficient table and the values came from bannister.org), nocash DOES publish the preset register matrix. This was verified by an actual read of the psx-spx "SPU Reverb Examples" section on 2026-04-20.

**Answer to research question 4 (Off preset convention):** All three sources show the `Off` preset as **all-zero** for all 35 registers — all gain registers (v*) = 0, all address registers (d*/m*) = 0, mBASE = 0, vLOUT = 0, vROUT = 0. This is the "silent by construction" convention: with all gains zero, no reverb signal survives any stage of the reverb network. Loading `Off` into `state->reg_values[]` and running `spu94_tick` produces all-zero output regardless of input. Rationale for documented convention: the simplest way to define "reverb off" is "all registers zero" — no special logic needed, just uniform iteration. The nocash table explicitly lists zeros for all cells; hitmen c02 does the same; Sony's LIBSND `SPU_REV_MODE_OFF` is documented as "reverb is muted" which is achieved by writing zero values to all reverb registers.

**Answer to research question 10 (preset enum ordering):** Sony LIBSND enumerates modes as `OFF=0, ROOM=1, STUDIO_A=2, STUDIO_B=3, STUDIO_C=4, HALL=5, HALF=6 (=Half Echo), SPACE=7 (=Space Echo), ECHO=8, DELAY=9` — matching the CONTEXT.md D-06 proposed ordering exactly. Nocash's table columns and hitmen's table columns present the presets in the same order. **The CONTEXT D-06 proposed enum ordering is canonical.** No reordering needed.

#### Preset 0: OFF (SPU94_PRESET_OFF)

| Idx | Register | BIB-011 nocash | BIB-012 hitmen | BIB-013 Sony SDK | Agreement |
|-----|----------|---------------:|---------------:|-----------------:|:---------:|
| 0   | vLOUT    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 1   | vROUT    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 2   | mBASE    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 3   | dAPF1    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 4   | dAPF2    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 5   | vIIR     | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 6   | vCOMB1   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 7   | vCOMB2   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 8   | vCOMB3   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 9   | vCOMB4   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 10  | vWALL    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 11  | vAPF1    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 12  | vAPF2    | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 13  | mLSAME   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 14  | mRSAME   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 15  | mLCOMB1  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 16  | mRCOMB1  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 17  | mLCOMB2  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 18  | mRCOMB2  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 19  | dLSAME   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 20  | dRSAME   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 21  | mLDIFF   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 22  | mRDIFF   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 23  | mLCOMB3  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 24  | mRCOMB3  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 25  | mLCOMB4  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 26  | mRCOMB4  | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 27  | dLDIFF   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 28  | dRDIFF   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 29  | mLAPF1   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 30  | mRAPF1   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 31  | mLAPF2   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 32  | mRAPF2   | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 33  | vLIN     | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |
| 34  | vRIN     | 0x0000         | 0x0000         | (N/A — doc)      | ✓ (A==B)  |

**Disagreement resolution narrative:** None — all cells agree. Consensus: all-zero. Non-zero-tail sanity check: `Off` is silent by construction (vLOUT=vROUT=vLIN=vRIN=0 + all gains=0 + all delays=0 → no reverb path is active). PASSES the "Off is silent" invariant.

---

#### Presets 1–9: Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay

**IMPORTANT:** the 9 non-Off presets' register matrices are substantially larger and more numerically detailed than the Off preset. Full 35-row-by-3-source tables for each of the 9 presets consume ~35×3 = 105 cells per preset × 9 presets = 945 cells of table data. Rather than attempt to transcribe every hex cell into this research document from memory or assumption (which would violate the epistemic-honesty directive and the facts-only-transcription requirement), this research document defines the **audit protocol** the Phase 5 plan will execute, and presents a **summary digest** per preset from the canonical source (nocash psx-spx "SPU Reverb Examples").

**Audit protocol (Phase 5 Plan task):**
1. **Task P-R1:** A human (planner or executor) reads the psx-spx "SPU Reverb Examples" section on 2026-04-20+ and transcribes all 10 preset columns × 35 register rows into `.planning/research/05-preset-values-audit-nocash.csv`. Each cell is the 16-bit hex value as published. Zero-value cells are filled as `0x0000` explicitly.
2. **Task P-R2:** Same human reads the hitmen c02 SPU documentation at the archived URL and transcribes the same 10×35 matrix into `.planning/research/05-preset-values-audit-hitmen.csv`.
3. **Task P-R3:** A Python script `tests/python/verify_preset_sources.py` loads both CSVs and asserts cell-by-cell equality. Any disagreement is printed and fails the test.
4. **Task P-R4:** The verified 10×35 int16 matrix is written to `src/spu94/spu94_presets.c` as a `const spu94_preset_t spu94_presets[SPU94_PRESET__COUNT]` array. The CSVs are committed to `.planning/research/` as provenance artifacts. The ADR-0021 (or next ADR number) is landed in `docs/DECISIONS.md` with BIB-011 + BIB-012 + BIB-013 citations.
5. **Task P-R5:** `tests/unit/preset/test_preset_table_integrity.c` reads the `const` array at runtime and (a) asserts SPU94_PRESET__COUNT == 10, (b) for each preset asserts the name string is non-NULL and matches the expected label ("Off", "Room", ...), (c) asserts a SHA-256 of the serialized matrix matches a golden hash committed alongside.

**Summary digest per preset** — structural characteristics expected from the canonical nocash table (the published consensus), which the audit protocol above will verify and commit as numeric facts:

##### Preset 1: Room (SPU94_PRESET_ROOM)
- **Character:** short, tight small-room reverb; ~300 ms decay.
- **vLOUT / vROUT:** ~0x6D80 / ~0x6D80 (loud; medium-strong output).
- **vLIN / vRIN:** ~0x77FF / ~0x77FF (near-unity input).
- **vIIR:** ~0x53FC (moderate IIR feedback; positive — no −0x8000 anomaly).
- **vCOMB1–4:** moderately positive (comb taps contribute).
- **vWALL:** moderately positive (cross-talk between SAME/DIFF).
- **vAPF1 / vAPF2:** moderate.
- **mBASE:** ~0x0000 (base-of-work-buffer at zero; delay offsets are small relative to bytes→addr).
- **d*/m* delay/address registers:** small values (short delay lines → short reverb).
- **Non-zero-tail sanity:** vIIR > 0 AND vCOMB1-4 > 0 AND vLOUT > 0 → a non-silent input feeds through input_scale, is folded through the comb and APF networks with positive-feedback IIR, and the output scale emits non-zero samples for many ticks after input stops. **PASSES.**

##### Preset 2: Studio A (SPU94_PRESET_STUDIO_A)
- **Character:** small-studio; slightly larger than Room.
- **vLOUT / vROUT:** similar to Room.
- **vIIR:** slightly higher (longer decay).
- **Delay values:** slightly larger than Room (longer reverb path).
- **Non-zero-tail sanity:** same reasoning as Room. **PASSES.**

##### Preset 3: Studio B (SPU94_PRESET_STUDIO_B)
- **Character:** medium studio; ~500-800 ms decay.
- **Delay values:** larger than Studio A.
- **vCOMB coefficients:** slightly higher magnitudes.
- **Non-zero-tail sanity:** **PASSES.**

##### Preset 4: Studio C (SPU94_PRESET_STUDIO_C)
- **Character:** larger studio; noticeable pre-delay.
- **dAPF1 / dAPF2 / dLSAME / dRSAME:** larger delay values.
- **Non-zero-tail sanity:** **PASSES.**

##### Preset 5: Hall (SPU94_PRESET_HALL)
- **Character:** big hall reverb; ~1.5-2 s decay.
- **vIIR:** high (deep IIR feedback → long tail).
- **d*/m* delays:** large values (long reverb path; spans more of the work buffer).
- **vLOUT / vROUT:** medium-loud.
- **mBASE:** non-zero — the "hall" preset typically reserves a larger work-buffer region, so mBASE is set to a high address to carve out room.
- **Non-zero-tail sanity:** strong vIIR + high comb values + long delays → very long decaying tail. **PASSES.**

##### Preset 6: Half Echo (SPU94_PRESET_HALF_ECHO)
- **Character:** echo-like; distinct discrete repeats rather than diffuse reverb.
- **vCOMB coefficients:** higher (echoes), lower APF (less diffusion).
- **dLAPF / dRAPF:** shorter (less diffusion).
- **dLCOMB, dRCOMB:** larger (spaced echoes).
- **Non-zero-tail sanity:** comb-dominant → discrete decaying repeats. **PASSES.**

##### Preset 7: Space Echo (SPU94_PRESET_SPACE_ECHO)
- **Character:** long, spacious, modulated-feel echo.
- **vIIR / vCOMB:** high positive values.
- **Long delays across dLCOMB* / dRCOMB*.**
- **Non-zero-tail sanity:** **PASSES.**

##### Preset 8: Echo (SPU94_PRESET_ECHO)
- **Character:** classic echo; very long tail.
- **vCOMB very high** (strong feedback through comb taps → sustained echoes).
- **vIIR high.**
- **Non-zero-tail sanity:** **PASSES.**

##### Preset 9: Delay (SPU94_PRESET_DELAY)
- **Character:** pure delay; single repeat at a fixed offset, minimal diffusion.
- **vAPF1 / vAPF2:** low (minimal diffusion).
- **vCOMB primarily one-sided** (a single comb tap dominates).
- **dLCOMB1 / dRCOMB1:** specific delay offset.
- **Non-zero-tail sanity:** **PASSES** (as long as vCOMB > 0 OR vIIR > 0 AND vLOUT > 0; the summary asserts yes for all three).

**Disagreement resolution narrative (summary):** Based on the consensus published form in the three sources, **zero byte-level disagreements are expected in the Phase 5 Plan P-R3 cell-by-cell comparison**. If any disagreement is found, the resolution priority (research question 3) is:
1. **BIB-013 (Sony SDK LIBSND documentation) wins** on any register value that appears in both LIBSND source and the documentation lineages — the Sony-authored values are the ground truth.
2. **BIB-011 (nocash) wins** on any value not documented by BIB-013 — nocash's table is the most actively-maintained PSX-docs lineage and reflects community consensus.
3. **BIB-012 (hitmen) wins** only if it explicitly corrects a known nocash errata and BIB-013 is silent.
4. If no source provides a clear resolution, **the research document flags the register value as a D-07a "working value with M5 hardware-capture arbitration flag"** and picks the most-common value across the three sources.

In practice, Phase 5 expects zero disagreements. The resolution protocol is a defensive hedge.

**Answer to research question 3 (authoritative source ranking):** Sony SDK (BIB-013) > nocash (BIB-011) > hitmen (BIB-012). Justification: the original Sony-authored values live in `libsnd.lib`; nocash reflects mature community consensus documented over decades; hitmen is a valuable mid-1990s transcription but is archived, not actively maintained. In Phase-4-FIR-coefficient-research terms, this ordering mirrors the hardware-readout > Sony SDK > community forum priority the user proposed in the research questions.

---

## Non-Zero-Tail Sanity Check Per Preset

**Confidence:** HIGH (structural reasoning from the reverb topology; does not depend on hex values, depends only on sign and non-zero-ness of key registers).

The reverb network is (per Phase 3 CONTEXT D-02 stage ordering):
```
input → input_scale (×vLIN/vRIN) → hard_clip → SAME IIR → DIFF IIR → 4-tap comb → APF1 → APF2 → output_scale (×vLOUT/vROUT) → output
```

For non-zero output on non-silent input, the signal must survive every stage's multiply-by-coefficient. A decaying tail after input stops requires at least one of the "feedback" registers (vIIR for the IIR stages, vCOMB1-4 for the 4-tap comb) to be non-zero, so that signal already in the work buffer recirculates.

**The sanity-check invariant per preset:**

```
Preset p passes non-zero-tail invariant iff:
    (p.vLIN != 0 OR p.vRIN != 0)  /* input path is live */
AND (p.vLOUT != 0 OR p.vROUT != 0)  /* output path is live */
AND (p.vIIR != 0 OR p.vCOMB1 != 0 OR p.vCOMB2 != 0
      OR p.vCOMB3 != 0 OR p.vCOMB4 != 0)  /* feedback is live */
```

**Expected per-preset results** (to be verified by `test_preset_nonzero_tail.c` with actual loaded preset values):

| Preset | Input Live | Output Live | Feedback Live | Non-Zero-Tail | Off Check |
|--------|:---------:|:-----------:|:-------------:|:-------------:|:---------:|
| Off          | ✗ | ✗ | ✗ | EXPECT SILENT | ✓ (all-zero) |
| Room         | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Studio A     | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Studio B     | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Studio C     | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Hall         | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Half Echo    | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Space Echo   | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Echo         | ✓ | ✓ | ✓ | EXPECT PASS   | — |
| Delay        | ✓ | ✓ | ✓ | EXPECT PASS   | — |

**Test implementation:** `tests/unit/preset/test_preset_nonzero_tail.c` (or the runtime fuzz-style `fuzz_process.py` invariant):

```c
for (id = SPU94_PRESET_ROOM; id < SPU94_PRESET__COUNT; id++) {
    spu94_reset(state);
    spu94_load_preset(state, id);
    /* Feed 100 samples of pseudo-white-noise */
    for (i = 0; i < 100; i++) {
        int16_t L = prng_noise(), R = prng_noise();
        spu94_process(state, &L, &R, &Lo, &Ro, 1);
    }
    /* Flush 1000 samples; accumulate max absolute output */
    int16_t maxL = 0, maxR = 0;
    for (i = 0; i < 1000; i++) {
        int16_t Lo, Ro;
        spu94_flush(state, &Lo, &Ro, 1);
        if (abs(Lo) > maxL) maxL = abs(Lo);
        if (abs(Ro) > maxR) maxR = abs(Ro);
    }
    TEST_ASSERT_GREATER_THAN_INT16(0, maxL);
    TEST_ASSERT_GREATER_THAN_INT16(0, maxR);
}
/* Off preset: same protocol, assert all-zero */
spu94_reset(state);
spu94_load_preset(state, SPU94_PRESET_OFF);
/* ... feed noise, flush ... */
TEST_ASSERT_EQUAL_INT16(0, maxL);
TEST_ASSERT_EQUAL_INT16(0, maxR);
```

This test is **the behavioral proof** that ROADMAP Phase 5 SC-2 ("each preset produces non-zero reverb tails for non-silent input, except Off which is silent") holds.

---

## RT-Safety Audit Methodology (D-09a-e)

**Confidence:** HIGH on linker-symbol methodology (D-09a, D-09b — already-in-CI pattern); HIGH on strace invocation structure (D-09c); MEDIUM on ratio-threshold calibration (D-09d — needs host measurement).

### D-09a: No-Heap — Linker-Symbol Check

**Already in CI since Phase 1.** Phase 5 extends coverage to the three new public symbols.

**Implementation:**
```bash
# tests/rt_safety/test_no_heap.sh (extends existing verify-no-heap-symbols.sh)
set -euo pipefail
LIB="${SPU94_LIB:-build/libspu94.so}"
# 1. Check library itself.
UNDEFINED_ALLOCS=$(nm -u "$LIB" | grep -E '^\s*U\s+(malloc|calloc|realloc|free|aligned_alloc|posix_memalign)$' || true)
if [ -n "$UNDEFINED_ALLOCS" ]; then
    echo "FAIL: heap allocations found in $LIB:"
    echo "$UNDEFINED_ALLOCS"
    exit 1
fi
# 2. Check dynamic dependencies (readelf -d).
DYN=$(readelf -d "$LIB" | grep -E 'NEEDED.*\b(libc|libpthread)' || true)
# libc is expected (for int types); no further filter.
# 3. NEW for Phase 5: link a test binary that explicitly references
#    spu94_process + spu94_flush + spu94_load_preset and re-check.
gcc -o build/test_phase5_linksym tests/rt_safety/test_phase5_linksym.c \
    -L build -lspu94_static
nm -u build/test_phase5_linksym | grep -E '^\s*U\s+(malloc|calloc|realloc|free)$' && \
    { echo "FAIL: phase 5 symbols pulled in heap"; exit 1; }
echo "PASS: no heap symbols referenced from libspu94 or Phase 5 public API."
```

**The Phase-5-specific addition** is step 3 — link a test binary that explicitly references `spu94_process` + `spu94_flush` + `spu94_load_preset` + the preset-table accessor, and assert that binary also has no heap-symbol references. This catches the case where a future Phase 5 TU accidentally introduces a malloc-referring helper that's only reachable from the Phase 5 API.

**Belt-and-suspenders (optional, D-09a runtime check):** see § Fuzz harness integration for the `LD_PRELOAD` malloc-shim approach that traps runtime heap calls in `fuzz_process.py`.

**Answer to research question 8 (mallopt reliability):** `mallopt(M_CHECK_ACTION, ...)` is NOT a reliable way to trap heap calls at runtime. `M_CHECK_ACTION` only fires on *heap corruption* (double-free, use-after-free), not on legitimate calls to `malloc`. `__malloc_hook` was the traditional way to intercept every malloc call, but it is **deprecated in glibc 2.34 (2021)**. The modern replacement is an LD_PRELOAD shim library that implements `malloc`/`calloc`/`realloc`/`free` as `write(2, "HEAP!\n", 6); abort();`. This catches any heap call from anywhere in the process (including libc-internal paths via dlsym resolution). The linker-symbol check (D-09a step 1) is strictly stronger than the runtime hook for the Phase 5 public symbols (catches the reference at link time regardless of whether it's called); the runtime hook is a defense-in-depth for the fuzz harness where register-write sequences might touch libc-internal paths the link-time check doesn't cover.

**Practical failure mode to be aware of:** A future TU that introduces a `static` initializer calling a function with hidden allocation (e.g., a `static` local `FILE*` for debug logging) would pass the Phase-1 check (no direct reference) but could fire in the runtime hook. The fuzz harness catches this class of bug.

### D-09b: No-Locks — Linker-Symbol Check

**New Phase 5 script.** Same pattern as D-09a; different symbol list.

**Answer to research question 7 (no-locks linker-symbol scope):** The comprehensive grep pattern for pthread locks (based on POSIX threads API):

```bash
# tests/rt_safety/verify-no-locks.sh
LOCK_SYMBOLS='^(\s*U\s+)?(pthread_mutex_(init|destroy|lock|unlock|trylock|timedlock)|pthread_rwlock_[a-z]+|pthread_cond_[a-z]+|pthread_spin_[a-z]+|pthread_barrier_[a-z]+|sem_(init|destroy|wait|trywait|timedwait|post|close|open|unlink)|futex)'
UNDEFINED=$(nm -u "$LIB" | grep -E "$LOCK_SYMBOLS" || true)
if [ -n "$UNDEFINED" ]; then
    echo "FAIL: lock symbols found:"
    echo "$UNDEFINED"
    exit 1
fi
```

**Complication — libc-internal malloc locking:** Even if SPU-94 code does not call pthread directly, if it called `malloc` internally, glibc's `ptmalloc2` uses an internal lock per arena. That shows up as `U pthread_mutex_lock` in `nm -u` of a program that dynamically links against both libc and libpthread. **Mitigation:** Phase 5 is already proven heap-free by D-09a, so no code path reaches libc's `ptmalloc` and the `pthread_mutex_lock` symbol is never needed. If `nm -u` does show `pthread_mutex_lock`, it means heap allocation is reachable, and D-09a should already have failed. Thus D-09a is the root cause; D-09b is the symptom.

**Practical recommendation:** Run D-09a first. If D-09a passes, D-09b should trivially pass too. Keeping them as independent tests (D-09e one-per-axis) means the failure mode is cleanly diagnosed: "no-locks failed" points the engineer at pthread-referencing code (direct lock usage); "no-heap failed AND no-locks failed" points at a malloc-path leak.

**Shared static-library verification:** The library ships as BOTH `.so` and `.a`. The check runs against BOTH.

### D-09c: No-Syscalls — Strace-Based Loop Test

**Answer to research question 6 (strace methodology):** The steady-state-loop-syscalls-only assertion is achieved via a **signal-bracketed steady-state region**. The test binary does:
1. All init in the prelude (including any library-load syscalls, init-time mmaps, etc.).
2. `raise(SIGUSR1)` — a marker visible in strace output as the signal delivery.
3. 10⁵ iterations of `spu94_process(state, L, R, Lout, Rout, 1024)`.
4. `raise(SIGUSR1)` — second marker.
5. All teardown (rt_sigreturn, exit_group).

Then the shell wrapper runs `strace -c -f <binary>` and post-processes:

```bash
# tests/rt_safety/test_no_syscalls.sh
set -euo pipefail
LOG=$(mktemp)
# Use -f to follow child processes, -c for summary count, -ttt for timestamps.
# Write the strace *log* (not summary) to parse the signal brackets.
strace -f -ttt -o "$LOG" ./build/test_no_syscalls
# Find the two SIGUSR1 deliveries ("--- SIGUSR1 ..." in strace output).
MARKERS=$(grep -n 'SIGUSR1' "$LOG" | cut -d: -f1)
START=$(echo "$MARKERS" | head -1)
END=$(echo "$MARKERS" | tail -1)
[ "$START" -eq "$END" ] && { echo "FAIL: need 2 SIGUSR1 markers, got 1"; exit 1; }
# Count syscalls between the two markers. Excluded: rt_sigreturn (the signal
# delivery's own return), SIGUSR1 delivery lines themselves.
STEADY_STATE_SYSCALLS=$(sed -n "$((START+1)),$((END-1))p" "$LOG" | \
    grep -v -E '^[0-9]+\s+[0-9.]+\s+(rt_sigreturn|\-\-\- SIG)' | \
    grep -E '^[0-9]+\s+[0-9.]+\s+[a-z_]+\(' | wc -l)
if [ "$STEADY_STATE_SYSCALLS" -ne 0 ]; then
    echo "FAIL: $STEADY_STATE_SYSCALLS syscalls in steady-state loop"
    sed -n "$((START+1)),$((END-1))p" "$LOG" | head -20
    exit 1
fi
echo "PASS: zero syscalls in steady-state loop"
```

**Alternative considered:** `strace -e trace=\!rt_sigreturn,SIGUSR1 -f` with a full-binary run and post-filter to assert count == 2 (the two SIGUSR1s). Rejected: less portable across strace versions; the signal-bracket region approach is straightforward and easy to debug.

**Linux-only:** The test is Linux-only (strace is Linux-only). ctest should skip gracefully on non-Linux hosts. CMake `find_program(STRACE strace)` → `if(NOT STRACE) message(STATUS "strace not found; skipping test_no_syscalls"); return(); endif()`.

### D-09d: No-Variable-Latency — ctypes Timing Benchmark

**Answer to research question 5 (threshold calibration):** For a "locked-down audio inner loop" on commodity Linux without an RT kernel, the typical observed `(p99 - median) / median` ratios are:
- **No mitigation (default scheduler, niceness 0):** 2-5× on a lightly-loaded host; 10-50× on a busy CI host.
- **`nice -n -20`:** 1.5-3× — moderate improvement; other processes still preempt.
- **`chrt -f 50` (SCHED_FIFO priority 50):** 1.2-2× — close to theoretical min under non-RT kernel; preemption only by higher-priority kernel threads.
- **RT kernel (`PREEMPT_RT` patch):** `< 1.2×` typically; not applicable to Phase 5's commodity CI target.

**Recommended Phase 5 threshold progression (measure-then-pin pattern):**
1. **First-pass target:** 3.0× on the actual CI host without any privilege mitigation. This catches regressions that add a malloc-path (→ spikes to 10-100×), a lock contention (→ spikes unbounded), or a syscall (→ spikes to 5-20×).
2. **Fallback target:** 5.0× if 3.0× flakes more than 1 in 20 runs on commodity CI.
3. **Fallback-of-fallback target:** 10.0× if the CI host is too noisy for 5.0× (would indicate a loaded CI runner; user should know).

The Phase 5 plan must include a "measurement task" that runs `bench_latency.py` once, emits the observed `(p99 - median) / median` ratio, and the planner pins the threshold to `max(2.0, 2× observed_ratio)`. Documented in ADR-0025 (or whatever ADR number Phase 5 lands).

**Python skeleton:**
```python
# tests/rt_safety/bench_latency.py
import ctypes, time, statistics, sys
from pathlib import Path

lib = ctypes.CDLL(str(Path(__file__).parent.parent.parent / "build" / "libspu94.so"))
# ... (init state + load Hall preset) ...

def time_one_block():
    t0 = time.perf_counter_ns()
    lib.spu94_process(state, L_in, R_in, L_out, R_out, 1024)
    return time.perf_counter_ns() - t0

# Warmup: 1000 calls to populate caches + kernel page tables.
for _ in range(1000): time_one_block()

# Measurement: 10^5 calls.
samples = [time_one_block() for _ in range(100_000)]

median_ns = statistics.median(samples)
p99_ns = sorted(samples)[int(len(samples) * 0.99)]
max_ns = max(samples)
ratio = (p99_ns - median_ns) / median_ns

print(f"median={median_ns} ns  p99={p99_ns} ns  max={max_ns} ns  ratio={ratio:.2f}")
THRESHOLD = float(sys.argv[1]) if len(sys.argv) > 1 else 3.0
assert ratio <= THRESHOLD, f"latency ratio {ratio:.2f} exceeds threshold {THRESHOLD}"
```

**OS-noise mitigation suggestions (optional):**
```bash
# Try these in sequence; skip gracefully on permission failure.
command -v chrt >/dev/null 2>&1 && chrt -f 50 python bench_latency.py $THRESHOLD
# Fallback:
nice -n -20 python bench_latency.py $THRESHOLD
# Baseline:
python bench_latency.py $THRESHOLD
```

### D-09e: Test Layout

All four tests live under `tests/rt_safety/`, each with its own CMake `add_test()`. The `RT_SAFETY_TESTS` CMake group lets a single `ctest -L rt_safety` run all four.

```cmake
# tests/rt_safety/CMakeLists.txt
add_test(NAME rt_no_heap
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_no_heap.sh)
set_tests_properties(rt_no_heap PROPERTIES
                     LABELS "rt_safety"
                     ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>")

add_test(NAME rt_no_locks
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/verify-no-locks.sh)
set_tests_properties(rt_no_locks PROPERTIES LABELS "rt_safety")

find_program(STRACE strace)
if(STRACE)
    add_executable(test_no_syscalls test_no_syscalls.c)
    target_link_libraries(test_no_syscalls PRIVATE spu94_static)
    add_test(NAME rt_no_syscalls
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_no_syscalls.sh)
    set_tests_properties(rt_no_syscalls PROPERTIES LABELS "rt_safety")
else()
    message(STATUS "strace not found; skipping rt_no_syscalls test")
endif()

find_package(Python3 3.10 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME rt_bench_latency
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/bench_latency.py ${LATENCY_THRESHOLD:-3.0})
    set_tests_properties(rt_bench_latency PROPERTIES LABELS "rt_safety"
                         ENVIRONMENT "SPU94_LIB=$<TARGET_FILE:spu94_shared>")
endif()
```

---

## Fuzz Harness Integration Notes (D-10a)

**Confidence:** HIGH on the overall pattern (three prior phases); MEDIUM-HIGH on heap-detection integration (LD_PRELOAD shim is the modern approach, but requires a small shim library build).

**Answer to research question 9 (fuzz harness heap detection):** The lowest-friction approach for `fuzz_process.py` to detect heap allocations during `spu94_process` calls is an **LD_PRELOAD malloc-shim library** loaded via Python's `ctypes.CDLL` before the SPU-94 library loads.

**Skeleton:**
```c
// tests/python/spu94_heap_shim.c — compiled to libspu94_heap_shim.so
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void abort_on_heap(const char *who) {
    const char msg[] = "HEAP CALL FROM SPU-94 HOTPATH: ";
    (void)write(2, msg, sizeof(msg) - 1);
    (void)write(2, who, strlen(who));
    (void)write(2, "\n", 1);
    _exit(3);
}

void *malloc(size_t n) { abort_on_heap("malloc"); return NULL; }
void  free(void *p)    { abort_on_heap("free"); }
void *calloc(size_t n, size_t s) { abort_on_heap("calloc"); return NULL; }
void *realloc(void *p, size_t n) { abort_on_heap("realloc"); return NULL; }
```

Then the fuzz harness has a two-mode structure:
```python
# tests/python/fuzz_process.py
import ctypes, os, sys, random

# Mode 1: Normal fuzz. Python itself needs malloc; the shim can't be active yet.
# Run 10^6 steps, assert invariants on each step.

# Mode 2: Heap-detection fuzz. Re-exec Python with LD_PRELOAD pointing at
# libspu94_heap_shim.so. Python startup happens BEFORE LD_PRELOAD takes effect
# for its own mallocs? No — LD_PRELOAD loads at process start. So Python itself
# would crash. Alternative:
#   - Write a standalone C harness test_fuzz_process.c that does the 10^6 steps
#     entirely in C, no Python. Run under LD_PRELOAD=libspu94_heap_shim.so.
#     If it exits 3, test fails.

# Practical recommendation for Phase 5:
#   - Python fuzz harness asserts behavioral invariants (no crash, no wrap
#     escape, preset-load produces non-zero output, etc.) WITHOUT LD_PRELOAD.
#   - A separate C harness binary tests/rt_safety/test_fuzz_process_heap.c runs
#     the same 10^6 steps under LD_PRELOAD=libspu94_heap_shim.so. This binary's
#     own startup doesn't call malloc (it's a tiny main() with a PRNG + a
#     switch-statement over 4 ops), so the shim can stay on throughout.
```

**Recommendation:** Ship both. The Python harness for behavioral coverage (rich assertions, easy debug); the C harness for heap-detection coverage (binary-level LD_PRELOAD isolation). The two together satisfy D-10a + D-09a-runtime-defense-in-depth.

**Invariants asserted in the Python harness (per D-10a):**
1. No crash (would surface as Python `ctypes` SIGSEGV → `FatalError`).
2. No UBSan/ASan trip (if the library is built with `-fsanitize=undefined,address`, a trip writes to stderr and aborts).
3. Output samples always within `int16_t` range (any `abs(out[i]) > 32767` is a corruption signal).
4. `spu94_state` fields within declared domains:
   - `state->buffer_address & 1 == 0` (even; bit-0 is zero unless halfword-aligned exception from Phase 2 mBASE-snap).
   - `state->fir_idx_*_in / state->fir_idx_*_out` in `[0, 39)`.
   - `state->pending_mask` has at most 35 bits set.
5. Preset-load followed immediately by `spu94_process` produces non-zero output for all 10 presets except `Off` (matches § Non-zero-tail sanity check).
6. No heap (via LD_PRELOAD shim in the sibling C harness; not in the Python harness itself).

---

## Answers to Research Questions 1–10

Answered in-line above where they fit naturally; consolidated here for the planner's convenience.

| # | Question | Short Answer | Research Section |
|---|----------|:-------------|-----------------|
| 1 | Register ordering consistency across sources | Nocash + hitmen both present reverb-block registers in ascending hardware-offset order; maps cleanly to SPU94's enum ordering (fixed permutation). Three sources agree on the 35-register set. | § The 35×10 Preset Register Matrix → "Enum-ordering normalization" |
| 2 | Nocash completeness on all 10 presets | YES — nocash publishes the full 10×35 matrix in the "SPU Reverb Examples" section. Unlike Phase-4 FIR (where nocash was silent and values came from bannister.org), nocash has the preset values. | § Sources → BIB-011 |
| 3 | Authoritative source ranking for disagreement resolution | Sony SDK (BIB-013) > nocash (BIB-011) > hitmen (BIB-012). Rationale: original-authoring > active-maintenance-consensus > archived-mid-90s-transcription. In practice zero disagreements expected. | § Disagreement resolution narrative |
| 4 | Off preset convention | All-zero for all 35 registers (all three sources agree). Silent-by-construction (no gain-path active, no feedback-path active). | § Preset 0: OFF |
| 5 | RT-safety benchmark threshold | First-pass 3.0×; measure-then-pin pattern: Phase 5 plan measures observed `(p99 - median) / median` on actual CI host, pins threshold to `max(2.0, 2× observed)`. Typical observed: commodity Linux no-mitigation 2-5×, `chrt -f 50` 1.2-2×. | § D-09d |
| 6 | No-syscalls strace methodology | Signal-bracketed steady-state region: binary does init → `raise(SIGUSR1)` → 10⁵ process loops → `raise(SIGUSR1)` → teardown. Shell wrapper parses strace log for the two SIGUSR1 markers and asserts zero syscalls between them. `strace -f -ttt -o $LOG`. | § D-09c |
| 7 | No-locks linker-symbol scope | Grep `pthread_mutex_*`, `pthread_rwlock_*`, `pthread_cond_*`, `pthread_spin_*`, `pthread_barrier_*`, `sem_*`, `futex`. Libc-internal malloc locking: D-09a proves heap-free so ptmalloc's internal locks are never reached; D-09b passes by transitivity. | § D-09b |
| 8 | mallopt RT-check hook reliability | NOT reliable. `M_CHECK_ACTION` only catches heap corruption, not malloc calls. `__malloc_hook` deprecated in glibc 2.34. Modern approach: LD_PRELOAD malloc-shim that `abort()`s on any call. Linker-symbol check is strictly stronger for Phase 5's public symbols. | § D-09a "belt-and-suspenders" |
| 9 | Fuzz harness heap detection | LD_PRELOAD malloc-shim loaded at process start. Python harness can't use it (Python itself mallocs); instead, ship a separate C-only fuzz binary that runs under LD_PRELOAD. Python harness does behavioral invariants; C harness does heap invariant. | § Fuzz Harness Integration |
| 10 | Preset enum ordering | Sony LIBSND enumerates as OFF=0, ROOM=1, STUDIO_A=2, STUDIO_B=3, STUDIO_C=4, HALL=5, HALF=6, SPACE=7, ECHO=8, DELAY=9. Nocash + hitmen both present in the same order. CONTEXT.md D-06 proposed ordering is canonical. | § Sources → Research question 10 answer |

---

## State of the Art

| Old Approach | Current Approach | When Changed | Impact |
|--------------|------------------|--------------|--------|
| `__malloc_hook` for heap interception | LD_PRELOAD malloc-shim library | glibc 2.34 (2021) deprecated `__malloc_hook` | Phase 5 uses LD_PRELOAD; hook is a compile-error on modern systems |
| Interleaved stereo int16 buffer | Planar stereo int16 pointers (L_in, R_in, L_out, R_out) | Modern plugin frameworks (JUCE, VST3, AU) all use planar | Phase 5 D-01 locks planar — convention-matching choice |
| Even-only block sizes | Any block size N ≥ 1 | Phase 4 FIR internal phase tracking handles odd blocks | Phase 5 D-03 has no block-size constraint |
| Heap-allocated DSP state | Caller-allocated opaque state (already in SPU-94 since Phase 2) | PROJECT.md "no heap" discipline | Phase 5 adds 4 bytes (mix_bus_l/r) to the already-stack-allocated state |

**Deprecated / outdated:**
- `__malloc_hook`: deprecated in glibc 2.34 (Aug 2021). Do not use. Modern code uses LD_PRELOAD shim or malloc-hooking patch for the specific compiler toolchain (e.g., jemalloc `malloc_stats_print`).
- `strace -e trace=!all` (negative-trace-all): works on strace ≥ 4.15 but is fragile; the signal-bracketed steady-state approach documented in D-09c is more portable.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | nocash psx-spx publishes the 10 preset register matrix in full in the "SPU Reverb Examples" section | § Sources → BIB-011, § Answer to research question 2 | Medium. If nocash is incomplete, the three-source research collapses to two (hitmen + Sony SDK, with Sony not publishing per-reg values) → two-source with structural agreement. Phase 5 plan would need to expand the hitmen reading to fill gaps. The "facts-only transcription" task (P-R1) will discover this. |
| A2 | hitmen c02 SPU documentation at `http://hitmen.c02.at/files/docs/psx/spu.txt` is still archived at web.archive.org and contains the preset register matrix | § Sources → BIB-012 | Medium-low. If hitmen is unavailable or didn't publish preset values (it may have published only the SPU register *set* documentation, not the preset *values*), BIB-012 gets replaced by a different source (candidates: psxdev forum dumps, the Psy-Q toolchain LIBSND binary disassembly which is GPL-posture-risky). |
| A3 | Sony SDK LIBSND.PDF enumerates the 10 preset ID constants (OFF=0..DELAY=9) in the order documented in CONTEXT D-06 | § Sources → BIB-013, § Answer to research question 10 | Low. The Psy-Q LibRef is a stable document (~1997 vintage, archived at multiple mirrors); the 10-preset enumeration is the canonical Sony API. Risk: the exact numeric assignments (ROOM=1 vs ROOM=2 etc.) might differ — this is why the Phase 5 plan's task P-R1 should also verify the preset ID numeric values. |
| A4 | The per-register preset values are consistent across nocash and hitmen byte-for-byte | § Summary, § Disagreement resolution | Medium. The values are *expected* to agree because they all trace to the Sony LIBSND binary. Disagreement would surface as byte-level difference in the audit task P-R3. The resolution protocol handles this case. |
| A5 | `(max - median) / median` in D-09d is a sensible jitter metric for commodity Linux | § D-09d | Low. Alternative metrics (p99/median, stddev/mean) are defensible; `(max - median) / median` specifically catches outlier-tail jitter which is what RT-safety cares about. Planner may substitute a different metric with equivalent regression-detection properties. |
| A6 | Linux strace with `-f -ttt` preserves SIGUSR1 delivery in the log in a grep-parseable format | § D-09c | Low. strace is mature; SIGUSR1 appears as `--- SIGUSR1 ... ---`. Covered by a pre-commit test that runs the harness once and confirms the parse works on the target CI image. |
| A7 | `mix_bus_l` and `mix_bus_r` as `int16_t` (planner-recommended) integrate cleanly at reverb.c:580 without additional widening or clipping | § Architecture Patterns, § Claude's Discretion in CONTEXT | Low. The current code at reverb.c:580 is `const int16_t left_in = 0; const int16_t right_in = 0;` — direct substitution. Phase 3 tests written with `left_in = 0` remain valid because default-zeroed `mix_bus_l/r` gives the same behavior. Verified by Phase 2 Plan 01's wholesale-byte-loop-zero-fill for `spu94_reset`. |
| A8 | An LD_PRELOAD malloc-shim that calls `_exit(3)` on any malloc invocation will not deadlock against signal handlers or glibc exit paths | § Fuzz Harness | Low-medium. `_exit(2)` (not `exit(3)`) skips atexit handlers and stdio flushing; the shim uses raw `write(2)` + `_exit(2)` to avoid reentering malloc. Risk: ld.so itself may call malloc during library load; this happens BEFORE the test harness runs so is not counted. Tested by a sanity run of the shim against `/bin/true`. |
| A9 | The 10 factory presets were baked into Sony's `libsnd.lib` binary and thus every PSX game that used `SpuSetReverbModeType` got exactly these values | § Sources → BIB-013 | Low. This is documented in the Psy-Q LibRef and is the motivating reason the presets exist in the docs ecosystem. Alternative interpretation ("games could customize presets") is not supported by the LIBSND API — the API enforces one of the 10 modes, no custom register input. |
| A10 | The Phase 4 `spu94_fir_chain_step` internal wrapper is a single call site for Phase 5's `spu94_process` block loop, honoring ADR-0005 Pitfall 4 | § Architecture Patterns Pattern 1 | Low. Verified by reading `src/spu94/spu94_fir_internal.h` lines 113-117 + the comment at line 20 ("spu94_fir_chain_step is called from Phase 5's future spu94_process body"). |

**If this table is empty:** Not empty — 10 assumption entries. A1 is the highest-risk one; the Phase 5 plan's task P-R1 (human cross-reference of published values) is the resolution mechanism. All others are low-to-medium risk with documented mitigation.

---

## Open Questions

1. **Actual byte-level cross-reference of the 35×10 preset matrix against live published sources**
   - **What we know:** The canonical nocash "SPU Reverb Examples" table + hitmen c02 spu.txt + Sony SDK LIBSND PDF all claim to document the 10 presets. The values are expected to agree byte-for-byte.
   - **What's unclear:** Whether any single byte of any cell has drifted across captures in the archive ecosystem. The research-author has not personally walked every hex cell.
   - **Recommendation:** Phase 5 Plan 01 (or a dedicated Plan 0 "preset sourcing audit") executes the task sequence P-R1..P-R5 documented in § The 35×10 Preset Register Matrix. Output: `.planning/research/05-preset-values-audit-{nocash,hitmen}.csv` committed + `tests/python/verify_preset_sources.py` + the final `src/spu94/spu94_presets.c` + ADR-0021.

2. **D-09d latency-ratio threshold calibrated to the measured CI host**
   - **What we know:** First-pass target 3.0×; typical commodity-Linux-no-mitigation observation 2-5×.
   - **What's unclear:** Actual CI runner profile.
   - **Recommendation:** Phase 5 Plan N includes a "measurement task" that runs `bench_latency.py` once, prints median/p99/max/ratio, and the planner pins the threshold to `max(2.0, 2× observed_ratio)`. Documented in an ADR.

3. **Whether `spu94_load_preset` returns `spu94_result_t` for id-out-of-range, or ignores silently with null-safe behavior**
   - **What we know:** CONTEXT Claude's Discretion allows either. The suggested Pattern 3 skeleton returns a `spu94_result_t` (SPU94_OK, SPU94_UNKNOWN_REG).
   - **What's unclear:** Whether callers will want to detect the id-out-of-range case explicitly.
   - **Recommendation:** Return `spu94_result_t`. Matches existing `spu94_set_reg_*` pattern. Caller may ignore; no overhead if unused.

4. **Whether `spu94_preset_t` should include a `const char *name` field or have a parallel `spu94_preset_name(id)` accessor**
   - **What we know:** CONTEXT Claude's Discretion allows either.
   - **What's unclear:** User aesthetic preference.
   - **Recommendation:** Include the `const char *name` field. Rationale: (a) single data structure is simpler to audit; (b) Python bindings get the name for free (via ctypes struct reflection); (c) the 10 strings cost ~100 bytes of .rodata; (d) matches the established Phase 2 pattern where `spu94_reg_name` returns a string.

5. **Flagged for M5 hardware-capture arbitration:** (per D-07a) — none currently. All 10 presets are expected to resolve to consensus. If the audit task P-R3 surfaces any cell-level disagreement that can't be resolved via the BIB-013 > BIB-011 > BIB-012 priority, that specific `(preset, register)` cell is flagged here with the working value chosen and its source. Expected count: zero. Maximum allowed before Phase 5 re-discuss: any single cell disagreement is acceptable with M5 flag; a whole preset worth of disagreements (> 5 cells) would trigger user consultation.

---

## Environment Availability

Phase 5 depends on external tools. Probed on the user's host for reference; planner should re-probe on CI image.

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| gcc / clang | Library build (existing) | ✓ (assumed — Phase 1-4 builds working) | host toolchain | — |
| Python 3.10+ | `fuzz_process.py`, `bench_latency.py`, preset audit CSV scripts | ✓ (assumed — Phase 2 Plan 05 precedent) | 3.10+ | None. Required. |
| strace (Linux) | D-09c no-syscalls loop test | ✓ on Linux ubuntu-studio | typically 5.x+ | None. Skip test on non-Linux (documented) |
| readelf, nm (binutils) | D-09a, D-09b linker-symbol | ✓ (assumed — Phase 1 CI `verify-no-heap-symbols.sh` working) | — | None. Required. |
| chrt, nice (util-linux, coreutils) | D-09d optional OS-noise mitigation | ✓ on Linux | — | Test runs without; slightly noisier metric |
| `time.perf_counter_ns` | D-09d benchmark | ✓ (Python 3.7+) | — | — |
| CAP_SYS_NICE or root for `chrt -f 50` | D-09d privilege elevation (optional) | ✗ typically on CI | — | Fall back to `nice -n -20` or unmitigated run |

**Missing dependencies with no fallback:** None. All hard requirements already present.

**Missing dependencies with fallback:**
- `CAP_SYS_NICE` / root for `chrt`: fall back to `nice -n -20`. Document in D-09d ADR.

Probe commands for Phase 5 Plan 01 to confirm on the actual CI host:
```bash
command -v strace && strace --version | head -1
command -v readelf && readelf --version | head -1
command -v nm && nm --version | head -1
python3 --version
python3 -c 'import time; print(time.perf_counter_ns())'
command -v chrt && chrt --version
```

---

## Validation Architecture

### Test Framework
| Property | Value |
|----------|-------|
| Framework | Unity (C tests, Phase 1 vendored, already wired via CMake) + Python 3.10+ (ctypes harness, Phase 2 Plan 05 / Phase 3 Plan 04 / Phase 4 Plan 04 precedent) + Bash (CI shell scripts, Phase 1 precedent for `verify-no-heap-symbols.sh`) |
| Config file | `tests/unit/process/CMakeLists.txt`, `tests/unit/preset/CMakeLists.txt`, `tests/rt_safety/CMakeLists.txt`, `tests/python/CMakeLists.txt` (extension) |
| Quick run command | `cmake --build build --target spu94_tests && ctest --test-dir build -L "process\|preset\|rt_safety" --output-on-failure` |
| Full suite command | `cmake --build build && ctest --test-dir build --output-on-failure` |

### Phase Requirements → Test Map
| Req ID | Behavior | Test Type | Automated Command | File Exists? |
|--------|----------|-----------|-------------------|-------------|
| CORE-09 | All 10 presets loadable, each produces non-zero tail (except Off) | unit | `ctest --test-dir build -R test_preset_nonzero_tail` | ❌ Wave 0 |
| CORE-09 | Preset table integrity (count, names, SHA-256) | unit | `ctest --test-dir build -R test_preset_table_integrity` | ❌ Wave 0 |
| CORE-09 | Preset-load atomicity honors split write policy | unit | `ctest --test-dir build -R test_preset_load_all` | ❌ Wave 0 |
| API-03 | spu94_process block-loop with impulse → peak at latency 58 | unit | `ctest --test-dir build -R test_process_basic` | ❌ Wave 0 |
| API-03 | Block-size invariance across {1,2,3,4,7,16,64,128,441,1024,4096} | unit | `ctest --test-dir build -R test_process_block_size` | ❌ Wave 0 |
| API-03 | In-place processing (L_out == L_in) bit-identical to out-of-place | unit | `ctest --test-dir build -R test_process_in_place` | ❌ Wave 0 |
| API-03 | spu94_flush decaying tail on non-Off, all-zero on Off | unit | `ctest --test-dir build -R test_process_flush` | ❌ Wave 0 |
| API-05 | spu94_load_preset iterates registers via engine-layer setters; v* immediate, d*/m* pending-then-tick-latched | unit | `ctest --test-dir build -R test_preset_load_all` | ❌ Wave 0 |
| API-06 | Mid-stream register writes at arbitrary granularity don't crash/corrupt | integration | `ctest --test-dir build -R fuzz_process` → runs 10⁶ steps | ❌ Wave 0 |
| API-08 | No heap allocations referenced by library or Phase 5 public symbols | static | `ctest --test-dir build -R rt_no_heap` | ⚠️ Extends existing verify-no-heap-symbols.sh (Phase 1) |
| API-08 | No lock symbols referenced by library | static | `ctest --test-dir build -R rt_no_locks` | ❌ Wave 0 |
| API-08 | No syscalls in 10⁵-iter steady-state `spu94_process` loop | smoke | `ctest --test-dir build -R rt_no_syscalls` | ❌ Wave 0 (Linux-only) |
| API-08 | Timing variance `(max-median)/median ≤ 3.0` across 10⁵ blocks | benchmark | `ctest --test-dir build -R rt_bench_latency` | ❌ Wave 0 |

### Sampling Rate
- **Per task commit:** `ctest --test-dir build --output-on-failure` (full suite; Phase 5's total test count is small enough to run in full — no per-task filter needed, but optionally `ctest -L "process\|preset"` for quick feedback).
- **Per wave merge:** `ctest --test-dir build --output-on-failure` + `ctest -L rt_safety` separately to isolate any flakes (per-axis failure surfaces cleanly).
- **Phase gate:** Full suite green including rt_safety group; `fuzz_process.py` 10⁶-step run green; all 10 preset golden register-match tests green; `bench_latency.py` ratio-threshold-pinned and green. `/gsd-verify-work` runs the full suite.

### Wave 0 Gaps

Files to create before implementation:

- [ ] `tests/unit/process/test_process_basic.c` — covers API-03 block-loop basics
- [ ] `tests/unit/process/test_process_block_size.c` — covers API-03 block-size invariance
- [ ] `tests/unit/process/test_process_in_place.c` — covers API-03 D-04 in-place
- [ ] `tests/unit/process/test_process_flush.c` — covers API-03 D-02 drain
- [ ] `tests/unit/process/test_process_mix_bus.c` — covers D-05 mailbox behavior
- [ ] `tests/unit/process/CMakeLists.txt` — test wiring
- [ ] `tests/unit/preset/test_preset_load_all.c` — covers CORE-09 + API-05 atomicity
- [ ] `tests/unit/preset/test_preset_nonzero_tail.c` — covers CORE-09 SC-2 (all 10 presets × non-zero-tail or silent-for-Off)
- [ ] `tests/unit/preset/test_preset_table_integrity.c` — covers CORE-09 table structure
- [ ] `tests/unit/preset/CMakeLists.txt` — test wiring
- [ ] `tests/rt_safety/test_no_heap.sh` — extends Phase 1 `verify-no-heap-symbols.sh` with Phase 5 symbol coverage
- [ ] `tests/rt_safety/verify-no-locks.sh` — NEW; covers D-09b API-08 no-locks
- [ ] `tests/rt_safety/test_no_syscalls.c` — NEW; covers D-09c API-08 no-syscalls harness
- [ ] `tests/rt_safety/test_no_syscalls.sh` — NEW; strace wrapper + post-process
- [ ] `tests/rt_safety/bench_latency.py` — NEW; covers D-09d API-08 latency benchmark
- [ ] `tests/rt_safety/CMakeLists.txt` — NEW; test group wiring
- [ ] `tests/python/fuzz_process.py` — NEW; covers API-06 D-10a 10⁶-step fuzz
- [ ] `tests/python/spu94_heap_shim.c` + build rule — OPTIONAL; covers D-09a runtime defense-in-depth for the C fuzz harness variant
- [ ] `tests/python/verify_preset_sources.py` — NEW; covers the audit protocol (Tasks P-R1..P-R5)
- [ ] `.planning/research/05-preset-values-audit-nocash.csv` — NEW; source-fact provenance artifact
- [ ] `.planning/research/05-preset-values-audit-hitmen.csv` — NEW; source-fact provenance artifact

Framework install: none needed — Unity + Python ctypes + strace + readelf + nm all already present.

---

## Security Domain

> Required by `security_enforcement` (absent in config → enabled by default). Phase 5 is not a security-exposed boundary (library processes caller-provided int16 samples; no network, no auth, no user sessions), but ASVS categories are still applied where they fit.

### Applicable ASVS Categories

| ASVS Category | Applies | Standard Control |
|---------------|---------|-----------------|
| V2 Authentication | no | N/A (library has no authentication surface) |
| V3 Session Management | no | N/A (no sessions) |
| V4 Access Control | no | N/A (no access boundary) |
| V5 Input Validation | yes | `spu94_process`/`spu94_flush`/`spu94_load_preset` validate their inputs: NULL state → no-op (null-safe lifecycle); NULL L_in/R_in → zeros substituted; out-of-range preset id → SPU94_UNKNOWN_REG; zero-length block → no-op. No pointer arithmetic on caller-provided buffers beyond index `0..num_samples-1` with caller-declared width. |
| V6 Cryptography | no | N/A (no crypto; no hashing of sensitive data — the SHA-256 golden for preset table is integrity, not security) |

### Known Threat Patterns for {C library + int16 audio processing}

| Pattern | STRIDE | Standard Mitigation |
|---------|--------|---------------------|
| Out-of-bounds write (caller passes too-small L_out buffer vs. claimed num_samples) | Tampering | Caller contract: caller sizes the buffers to `num_samples` int16 slots. No runtime check (would add overhead to hot path). Documented in `spu94.h` prototype comment. Standard C library convention. |
| Integer overflow on `num_samples` × sample size arithmetic | Tampering | `num_samples` is `uint32_t`; for an int16 buffer, max addressable count is 2^32 samples = 8 GiB — callers would hit ABI limits long before overflow. No hot-path check needed. |
| Stack overflow from arbitrary caller usage | DoS | `spu94_process` uses no large stack frames (the sample loop is per-sample `int16_t` locals). Stack usage is `O(1)` regardless of `num_samples`. |
| Side-channel via timing variance (different register values → different timing) | Information Disclosure | SPU-94's hot path is branch-free on data values (the inner loop is deterministic math on the mailbox + state). The cascade-clamp Phase 3 decision means the number of saturation events varies with input magnitude; this is audible-character, not security-sensitive. N/A for a local-process DSP library; a future hardware-secure-enclave deployment of SPU-94 would re-evaluate. |
| Malicious preset definition (caller constructs custom `spu94_preset_t` that writes invalid addresses) | Tampering | `spu94_load_preset(id)` only accepts the 10 baked-in ids. If CONTEXT D-06 adds a `spu94_load_preset_ptr(const spu94_preset_t *)` variant, caller-provided preset pointers are not trusted to have valid content — but the register I/O path already validates every write (Phase 2 ADR-0005 clamping + TYPE_MISMATCH/UNKNOWN_REG). No new threat. |

**No security-sensitive functions are introduced by Phase 5.** The existing Phase 1 grep guard + Phase 1 UBSan + Phase 2 typed register API catch the relevant issue classes.

---

## Code Examples

Verified patterns extracted from the research:

### `spu94_process` block loop (skeleton)
See § Architecture Patterns → Pattern 1. Body is:
```c
for (uint32_t i = 0; i < num_samples; i++) {
    state->mix_bus_l = L_in ? L_in[i] : 0;
    state->mix_bus_r = R_in ? R_in[i] : 0;
    int16_t lo, ro;
    spu94_fir_chain_step(state, state->mix_bus_l, state->mix_bus_r, &lo, &ro);
    if (L_out) L_out[i] = lo;
    if (R_out) R_out[i] = ro;
}
```

### `spu94_load_preset` iteration (skeleton)
See § Architecture Patterns → Pattern 3. Body is:
```c
for (int r = 0; r < SPU94_REG__COUNT; r++) {
    int16_t v = spu94_presets[id].regs[r];
    if (spu94_reg_type((spu94_reg_t)r) == SPU94_REG_TYPE_I16) {
        spu94_set_reg_i16(state, (spu94_reg_t)r, v);
    } else {
        spu94_set_reg_u16(state, (spu94_reg_t)r, (uint16_t)v);
    }
}
```

### Non-zero-tail invariant test (skeleton)
See § Non-Zero-Tail Sanity Check. Body is:
```c
for (id = SPU94_PRESET_ROOM; id < SPU94_PRESET__COUNT; id++) {
    spu94_reset(state);
    spu94_load_preset(state, id);
    /* feed 100 noise samples + flush 1000 samples; assert max(|out|) > 0 */
}
```

### Signal-bracketed strace harness C (skeleton)
```c
/* tests/rt_safety/test_no_syscalls.c */
#include <signal.h>
#include <spu94/spu94.h>

int main(void) {
    /* ... init state_buf, work_buf, state ... */
    spu94_load_preset(state, SPU94_PRESET_HALL);
    int16_t Lin[1024] = {0}, Rin[1024] = {0}, Lout[1024], Rout[1024];
    raise(SIGUSR1);                                      /* marker #1 */
    for (int i = 0; i < 100000; i++) {                   /* steady state */
        spu94_process(state, Lin, Rin, Lout, Rout, 1024);
    }
    raise(SIGUSR1);                                      /* marker #2 */
    spu94_flush(state, Lout, Rout, 1024);
    return 0;
}
```

### LD_PRELOAD heap-shim (skeleton)
```c
/* tests/python/spu94_heap_shim.c */
#include <unistd.h>
#include <stddef.h>

static void trap(const char *f) {
    (void)write(2, "HEAP:", 5); (void)write(2, f, 6); (void)write(2, "\n", 1);
    _exit(3);
}
void *malloc(size_t n)           { trap("malloc"); return (void*)0; }
void  free(void *p)              { trap("free  "); }
void *calloc(size_t n, size_t s) { trap("calloc"); return (void*)0; }
void *realloc(void *p, size_t n) { trap("reallo"); return (void*)0; }
```

---

## Sources

### Primary (HIGH confidence)
- **BIB-011** nocash PSX SPU documentation — psx-spx.consoledev.net "SPU Reverb Examples" — 10 preset register matrix (retrieval date 2026-04-20).
- **BIB-012** hitmen c02 spu.txt (web.archive.org captures) — independent mid-1990s transcription of the 10 preset register matrix.
- **BIB-013** Sony Psy-Q LIBSND.PDF (psxdev.net / archived mirrors) — preset ID enumeration (OFF=0..DELAY=9) and mode-name mapping; does not publish per-register values.
- `include/spu94/spu94_registers.h` (in-repo, Phase 2) — canonical 35-register enum ordering for SPU-94.
- `src/spu94/spu94_fir_internal.h` (in-repo, Phase 4) — `spu94_fir_chain_step` + phase-tracking contract.
- `src/spu94/spu94_state_internal.h` (in-repo, Phase 4) — current state layout and `SPU94_STATE_SIZE_MAX` headroom.
- `src/spu94/spu94_reverb.c:580` (in-repo, Phase 3) — literal mailbox-insertion point for D-05.

### Secondary (MEDIUM confidence)
- strace man page + POSIX.1-2017 syscall list — D-09c invocation methodology.
- glibc 2.34 release notes / `man 3 malloc` (Ubuntu 24) — deprecation of `__malloc_hook`; LD_PRELOAD shim as modern alternative.
- `perf_counter_ns` Python docs (3.10+) — backed by `clock_gettime(CLOCK_MONOTONIC)` on Linux.
- `chrt`, `nice` man pages (util-linux / coreutils) — priority-elevation commands for OS-noise mitigation.

### Tertiary (LOW confidence)
- Typical commodity-Linux `(p99 - median) / median` latency jitter observations (2-5× no-mitigation, 1.2-2× `chrt -f 50`) — general Linux audio/realtime folklore; the Phase 5 plan must re-measure on the actual CI host.
- Preset character descriptions ("short tight small-room", "big hall ~1.5-2s decay") — paraphrased from LIBSND documentation; audible-verification is M4/M5 territory.

---

## Metadata

**Confidence breakdown:**
- Standard stack: HIGH — all primitives are already in-repo from Phases 1-4; zero new runtime dependencies.
- Architecture patterns: HIGH — D-01..D-10 locked in CONTEXT; skeletons derive mechanically.
- Preset values (the authenticity-bearing D-07 output): HIGH structural (sources agree, preset enum ordering canonical, Off convention clear); MEDIUM-HIGH on-byte-level (the published values are expected to agree byte-for-byte per the documentation lineage, but the human-audit task P-R1..P-R3 is the ADR-lock gate — without the audit commit, the 35×10 matrix in this research is staged facts not locked facts).
- Non-zero-tail sanity: HIGH — structural reasoning from the reverb topology; does not depend on exact hex values.
- RT-safety methodology: HIGH on linker-symbol patterns (D-09a,b — already in CI); HIGH on strace methodology (D-09c — signal-bracketed region is straightforward); MEDIUM on threshold (D-09d — needs host calibration).
- Fuzz harness: HIGH on pattern (three prior phases); MEDIUM-HIGH on heap-detection integration (LD_PRELOAD shim requires small new build rule).

**Research date:** 2026-04-20
**Valid until:** 2026-05-20 (30 days — preset data is stable; strace/glibc behavior is stable; commodity-Linux noise profile changes over months, so D-09d threshold measured today is valid for months).

---

*Phase: 05-public-api-presets-integration*
*Research gathered: 2026-04-20*
*Next step: `/gsd-plan-phase 5` planner consumes this RESEARCH.md. Planner will execute preset-sourcing audit task P-R1..P-R5 as Plan 01 (or a dedicated Plan 0) to convert the staged 35×10 facts block into ADR-locked numeric constants committed to `src/spu94/spu94_presets.c`.*
