# Milestones

## v1.0 Reverb Core (Shipped: 2026-04-25)

**Phases completed:** 7 phases, 33 plans, 58 tasks

**Key accomplishments:**

- libspu94 now builds as shared+static from a single OBJECT library with determinism flags (-Werror, -ffp-contract=off, -fno-fast-math) locked in and verifiable in compile_commands.json, and header-only Q15 helpers (q15_mul_truncate, sat_s16, q15_add_sat) pass 3 Unity test functions against a hand-audited reference table including the INT16_MIN² saturation case and ASR-vs-C-division distinguishers.
- CI is now the enforcement layer for Phase 1 success criteria 3 and 4: a 5-job GitHub Actions workflow (matrix build gcc+clang, grep-guard, clang-tidy, cppcheck, UBSan-hard-abort) gates every future commit, with a positive+negative fixture meta-test guarding the grep guard itself and a jq-based flag-drift detector over `compile_commands.json`. Every third-party action is pinned to a full 40-hex commit SHA per T-01-01.
- Seeded docs/DECISIONS.md with three Phase 1 ADRs (Q15 multiply, vIIR = -0x8000 anomaly, UBSan no_sanitize policy + macro template) and added a defensive LICENSE placeholder deferring the MIT/Apache-2.0 pick to end of Milestone 1.
- Added formal KNOWN LIMITATIONS block to grep-guard.sh (documenting the line-granular `long long`/`long` edge case and linking to the fixture pin) and case 7 to test-grep-guard.sh (asserting current exit-0 behavior on `long long ll; long n;`) — the final discipline layer promised by 01-02-PLAN and flagged missing by 01-VERIFICATION.md.
- Opaque spu94_state handle with caller-allocated storage, SPU94_STATE_SIZE_MAX-bounded shell type, init/reset/destroy lifecycle, plus the Phase-2 SC-1 linker-symbol heap-free proof and the SC-6 C99 + C++ extern-C consumer compile tests.
- The 35-register identity surface (enum + hw_offset + name + atomic snapshot declaration), the Q15 error-observation tap with pre-saturation remainder semantics, the public spu94_tick per-tick entry point stub, and ADR-0004 documenting both taps as intentional seams — all on a chassis that compiles clean under C99-pedantic and through an extern "C" C++ consumer.
- Engine-layer typed register accessors with signedness validation, the 35-entry write-policy table as the D-05 swappable seam, the pending-shadow + tick-flush plumbing that honors Pitfall 4's exactly-one-call-site rule, the 105-wrapper hand-written facade header, and ADR-0005 documenting the split write-timing policy — all on a chassis whose linker surface stays heap-free and whose public headers stay C99-pedantic + C++-pedantic clean.
- The CORE-03 BufferAddress wrap formula (`MAX(mBASE, (addr+2) AND 0x7FFFE)`) implemented in byte arithmetic, the ADR-0006 snap-on-write side effect lifted from a Plan-03 stub into a real handler in `src/spu94/spu94_buffer.c`, the public `spu94_get_buffer_address` observability accessor declared and implemented, the `spu94_tick` body completed for Phase 2 (apply_pending_writes → buffer_advance), and ADR-0006 documenting the resolution prepended above ADR-0005 — all on a chassis whose linker surface stays heap-free, whose public headers stay C99-pedantic + C++-pedantic clean, and whose ODR invariant is verified per-symbol via `nm`.
- The complete Phase 2 test battery: four register test TUs (35 registers x roundtrip / types / policy / edges), two buffer test TUs (wrap-formula corners + mBASE snap + work-buf-unchanged invariant), a 10^6-step Python ctypes fuzz harness with an independent Python model of the wrap formula, and a structured reference-table addition to the q15 suite -- all green under ctest, closing TEST-02, ROADMAP Phase 2 SC 3, ROADMAP Phase 2 SC 4, and the ADR-0004/0005/0006 test obligations. Phase 2 is now complete.
- Found during:
- Found during:
- Found during:
- None.
- Manual verification:
- 1. [Rule 3 - Blocking] Replaced "Branch-free" with "No branches" in the D-05 tap comment.
- 1. [Rule 1 - Bug] Corrected SPU94_LATENCY_SAMPLES = 38u -> 58u.
- FIR unit tests: 11 total.
- Ten PS1 factory reverb preset tables landed in .rodata with byte-for-byte traceability to two audited sources; Off preset resolved in favor of BIB-011's defensive 0x0001 buffer-offset values per the documented priority chain.
- `spu94_process` + `spu94_flush` landed as public ABI T-symbols atop the Phase 1–4 algorithm; D-05 mix-bus mailbox (int16_t mix_bus_l/r) wires `spu94_reverb_body` to per-sample input via struct-field mailslot with zero regression on Phase 3 body-level tests.
- `spu94_load_preset` ships as a T-symbol on libspu94.so, iterating the 35-register preset table via Phase 2's engine-layer setters; D-08 split-policy semantics proven end-to-end via 6 sub-tests covering all 10 presets x 35 registers + 2 behavioral sub-tests pinning SC-2 (non-Off non-silent tail; Off silent-input silent-output).
- Four permanent ctest regression gates under `tests/rt_safety/` (label `rt_safety`) prove API-08 at the contract level: `libspu94.so` + the Phase 5 static-link closure reference no heap, no locks, no syscalls, and exhibit bounded (ratio=0.741, budget=3.0) per-block latency variance across 10^5 consecutive `spu94_process` blocks.
- 10^6-step public-API random-walk harness, block-size + in-place bit-identity unit tests, and 6 ADR landings for D-01..D-10 close Phase 5 behaviorally; dev-host fuzz passes in 595 s with all 6 per-step invariants holding across 1M ops; RT_LATENCY_THRESHOLD pinned at 2.0 per measure-then-pin protocol.
- Binding suite (`ctest -L binding`):
- Binding suite (`ctest -L binding`):
- Native C `spu94` CLI with dr_wav-backed WAV I/O, jsmn-backed --config JSON (override + flat shapes), polished engineer-oriented error messages, and a permanent nm-audit gate keeping dr_wav/jsmn out of libspu94.so.
- Local dev build
- DOCS-04 closed. Phase 6 algorithm work is complete; Milestone 1 is positioned for the /gsd-verify-work 6 pass.
- `scripts/ci/check_coverage.py`
- Shipped 50-golden .wav corpus (10 presets × 5 inputs) with paired SHA-256 sidecars, a deterministic `regenerate_goldens.py` with `--check` diff mode, a bookworm-slim sha-pinned `Dockerfile.repro` that reproduces the corpus byte-for-byte inside CI, and the `.dockerignore` that makes that reproducibility claim actually hold.
- 1. [Rule 3 - Blocking] `lv2apply` 0.24.26 segfaults on lv2-psx-reverb
- All 105 × 2 gates green.
- Shipped BUILD-06 D-20 in its gate-split form: hotpath_alloc_gate (merge-blocking, hard CI fail on any heap syscall in spu94_process) + pytest-benchmark harness (report-only, 10 presets x 2 block sizes, committed baseline per D-21) + paired CI jobs.
- 1. [Rule 3 — Blocking] BIB-003 and BIB-004 orphaned in DECISIONS.md since Phase 1

---

## v1.4 Preset System (Shipped: 2026-05-02)

**Phases completed:** 3 phases, 5 plans
**Tag:** `v1.4`
**Requirements:** 10/10 complete (PRE-01..PRE-10)

**What shipped:**

Human-readable preset save/load system for SPU-94. The C core serializes all 46 engine fields (35 registers + 7 mixer faders + 4 DAC toggles) to a versioned INI-style `.spu94` text file and restores them with bit-identical fidelity. Accessible via C API (`spu94_preset_save` / `spu94_preset_load`), CLI (`preset-dump` subcommand + `--load-preset` flag), and JUCE standalone GUI (Save/Load buttons with native file dialogs, custom preset dropdown, modified-state asterisk indicator).

**Key accomplishments:**

1. `spu94_preset_save` — EMIT-macro-based overflow-safe serializer writing 46 fields across 3 INI sections (registers, mixer, DAC) with version header
2. `spu94_preset_load` — section-aware strchr-based parser with 512-byte line buffer, hand-rolled `parse_hex_u16` (grep-guard compliant)
3. CLI `preset-dump` with `--preset`/`--name`/`-o`/`--list-presets` flags + `--load-preset` on reverb with three-way mutual exclusion
4. JUCE Save/Load buttons with native file dialogs, custom preset dropdown (diamond prefix), modified-state asterisk (30Hz 46-field diff)
5. Integration-level golden round-trip test proving bit-identical audio output after save/load through `spu94_process` (factory Hall + custom Delay with non-default mixer/DAC)

**Key decisions:**

- EMIT macro pattern for overflow-safe snprintf writes
- strchr-based key=value splitting (strtok modifies strings)
- Save dialog simplified to single native file dialog step (kdialog touch-create workaround)
- File-preset audio-thread handoff via pendingPresetBuf + acquire/release atomics
- loaded_pid = -1 for file presets prevents default-fader overwrite

**Issues deferred:** None — clean close.

**Archived to:** `.planning/milestones/v1.4-ROADMAP.md`, `.planning/milestones/v1.4-REQUIREMENTS.md`

---

## v1.3 True Oversampled DAC (Shipped: 2026-05-01)

**Phases completed:** 3 phases, 8 plans
**Tag:** `v1.3`

**What shipped:** Genuine 8x oversampling at 352.8kHz replacing v1.2's single-rate approximation. Sum-of-8 proper decimation, unified HP-shaped noise model, A/B mode toggle across all surfaces.

**Archived to:** `.planning/milestones/v1.3-ROADMAP.md` (if exists)

---

## v1.2 DAC Modeling (Shipped: 2026-04-30)

**Phases completed:** 5 phases, 12 plans
**Tag:** `v1.2`
**Requirements:** 14/14 complete

**What shipped:** AK4309 interpolation filter + delta-sigma noise model as toggleable DAC coloration stage. Send/return mixer architecture with 3 buses, 6 faders, latency compensation.

**Archived to:** `.planning/milestones/v1.2-ROADMAP.md`, `.planning/milestones/v1.2-REQUIREMENTS.md`

---

## v1.1 ADPCM Encode/Decode (Shipped: 2026-04-27)

**Phases completed:** 4 phases, 10 plans
**Tag:** `v1.1`
**Requirements:** 23/23 complete (ADPCM-01..07, ADPCM-INT-01..06, ADPCM-IO-01..06, ADPCM-TEST-01..04)

**What shipped:**

Bit-faithful Sony 4-bit ADPCM encode/decode added to libspu94 as a peer module (380 LOC C, zero heap, integer-only). Wired into the reverb pipeline as a toggleable coloration stage — when enabled, input PCM round-trips through ADPCM before reverb, reproducing the quantization noise and filter ringing of PS1 audio. Accessible via C API, CLI (`--adpcm` flag + `adpcm-encode`/`adpcm-decode` subcommands), Python ctypes, and JUCE standalone GUI toggle.

**Key accomplishments:**

1. ADPCM decoder + encoder with 5 SPU filter pairs, brute-force best-fit encoder over 65 combinations, caller-allocated 4-byte state
2. Pipeline integration as toggleable upstream stage with 28-sample latency, default-off, all rt_safety gates passing
3. VAG v2 file format I/O (big-endian, terminator blocks), CLI subcommands, Python bindings, JUCE ADPCM toggle
4. 32 unit tests with coverage maps, 30 ADPCM golden files (10 presets x 3 inputs) with SHA-256 regression gate
5. 7 ADRs (ADR-0047 through ADR-0053) formalizing all gray-area resolutions

**Key decisions:**

- Rounding: `(old*f0 + older*f1 + 32) >> 6` — round-to-nearest via +32 bias (ADR-0047)
- Shift 13-15 mapped to shift 9 per psx-spx (ADR-0048)
- Filter 5-7 clamped to filter 4 per emulator consensus (ADR-0049)
- ASR division semantics per ADR-0001 discipline (ADR-0050)
- L2 error metric in int64 (ADR-0051), strict `<` tiebreak with iteration order (ADR-0052)
- Caller zero-pads tail blocks to 28 samples (ADR-0053)

**Issues deferred:** None — clean close.

**Archived to:** `.planning/milestones/v1.1-ROADMAP.md`, `.planning/milestones/v1.1-REQUIREMENTS.md`

---
