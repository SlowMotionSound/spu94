# Roadmap: SPU-94 — Milestone 1

**Created:** 2026-04-18
**Milestone:** 1 (v1.0) — reverb network + hard clip
**Granularity:** standard
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## Milestone Goal

Ship `libspu94` (plain C library) + ctypes Python bindings + `spu94` CLI that bit-faithfully reproduces the PS1 SPU reverb network and mix-bus hard clip, verified by spec-conformance tests, witness diffs, golden files, and a modulation harness, and proven MCU-portable by a Cortex-M cross-compile smoke test.

## Phases

- [ ] **Phase 1: Foundation — Fixed-Point Math + Build Infrastructure** — Q15 helpers, CMake/CI determinism flags, DECISIONS.md seeded
- [ ] **Phase 2: Buffer + Register Infrastructure** — work buffer, 35-register state, mid-stream write policy, opaque-handle lifecycle API
- [ ] **Phase 3: Core Reverb Algorithm + Hard Clip** — SAME/DIFF IIR + 4-tap comb + APF1/APF2 topology, mix-bus clip, vIIR anomaly
- [ ] **Phase 4: Sample Rate Conversion (39-tap half-band FIR)** — 44.1↔22.05 kHz I/O boundaries with nocash coefficients
- [ ] **Phase 5: Public API + Presets Integration** — `spu94_process` orchestration, 10 factory presets, glitch-free mid-stream writes end-to-end
- [ ] **Phase 6: Python Binding + CLI** — ctypes wrapper, numpy interop, `spu94` WAV tool, README
- [ ] **Phase 7: Verification — Golden Files, Witness Diff, Modulation** — spec-conformance suite, lv2-psx-reverb diff, modulation harness, LEVERS-CATALOG.md
- [ ] **Phase 8: MCU Cross-Compile + CI Hardening** — Cortex-M7 smoke test via arm-none-eabi-gcc

## Phase Details

### Phase 1: Foundation — Fixed-Point Math + Build Infrastructure
**Goal**: Every downstream module can rely on correct Q15 arithmetic, a deterministic build, and a committed place to log gray-area decisions.
**Depends on**: Nothing (first phase)
**Requirements**: CORE-01, BUILD-01, BUILD-02, BUILD-04, BUILD-05, BUILD-07, DOCS-01, DOCS-05
**Success Criteria** (what must be TRUE):
  1. `libspu94` builds on Linux as shared and static library artifacts with determinism flags (`-ffp-contract=off`, `-fno-fast-math`, `-Werror`) locked in and verifiable in the compile command line.
  2. A developer can call `q15_mul_truncate`, `sat_s16`, and related fixed-point helpers with negative operands, `INT16_MIN`, and boundary values, and the outputs match a hand-computed truncation-direction reference (never rounding).
  3. CI runs clang-tidy, cppcheck, compiler-warnings-as-errors, and UBSan with `no_sanitize("integer")` annotations on documented SPU saturation functions, and the suite goes green on an empty reverb core.
  4. A CI grep guard fails the build if `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, or unqualified `long` appear in core library sources.
  5. `DECISIONS.md` exists and contains resolved entries for (a) Q15 multiply semantics (`>> 15` direction and signed-shift policy) and (b) vIIR = -0x8000 policy; `LICENSE` placeholder notes the MIT/Apache-2.0 pick is deferred to end of M1.
**Plans:** 4 plans
Plans:
- [x] 01-01-PLAN.md — Scaffold + CMake (OBJECT library, shared+static, determinism flags) + Q15 headers + Unity vendor + q15 unit tests
- [x] 01-02-PLAN.md — GitHub Actions CI (gcc+clang matrix, clang-tidy, cppcheck, UBSan) + grep-guard + verify-flags + fixture meta-test
- [x] 01-03-PLAN.md — docs/DECISIONS.md (ADR-0001 Q15 semantics, ADR-0002 vIIR anomaly, ADR-0003 UBSan surgical policy) + LICENSE placeholder
- [x] 01-04-PLAN.md — Gap closure: KNOWN LIMITATIONS block in grep-guard.sh + fixture case 7 in test-grep-guard.sh

### Phase 2: Buffer + Register Infrastructure
**Goal**: A caller can allocate an SPU-94 state, write any of the 35 registers at any time, and the buffer addressing + write-policy machinery behaves identically per spec regardless of call order.
**Depends on**: Phase 1
**Requirements**: CORE-03, CORE-04, CORE-10, API-01, API-02, API-04, API-07, API-09, TEST-02
**Success Criteria** (what must be TRUE):
  1. A caller can compute state size, allocate storage externally, and call `spu94_init` / `spu94_reset` / destroy without the library touching the heap (verified by a linker-level symbol check that `malloc`/`free` are not referenced from core).
  2. All 35 SPU reverb registers are writable and readable via typed enum identifiers, round-trip correctly, and signed (v*) vs unsigned (d*, m*) interpretation is preserved across write/read.
  3. `BufferAddress` advance honors the `MAX(mBASE, (addr+2) AND 0x7FFFE)` rule across 10^6 fuzzed steps with no out-of-bounds access, and mBASE writes follow the policy documented in `DECISIONS.md`.
  4. Per-register unit tests exercise each of the 35 registers in isolation — value sweeps, edge cases, and zero-value-meaningful cases — and all pass.
  5. `DECISIONS.md` contains entries for (a) per-register mid-stream write policy (immediate vs tick-latched, with mBASE as documented special case) and (b) mBASE-write side-effect behavior on the work buffer.
  6. `spu94.h` compiles cleanly under `-std=c99 -pedantic` and under a `extern "C"` C++ consumer stub; the header depends only on the freestanding C subset.
**Plans:** 5 plans
Plans:
- [x] 02-01-PLAN.md — State chassis: opaque `spu94_state` + lifecycle API + linker-level no-heap proof + C99/C++ consumer compile tests
- [x] 02-02-PLAN.md — Register identity surface (35-entry enum + hw_offset + name + snapshot decl), q15 error tap, `spu94_tick` stub, ADR-0004
- [x] 02-03-PLAN.md — Register I/O engine + 35 facade wrappers + split write-timing policy table + pending shadow + tick flush + ADR-0005
- [ ] 02-04-PLAN.md — BufferAddress wrap formula + mBASE snap-on-write (resolves D-09/D-10) + observability accessor + ADR-0006
- [ ] 02-05-PLAN.md — Test battery: register roundtrip/types/policy/edges + buffer wrap/mBASE + Python ctypes 10^6-step fuzz + q15_with_err cases

### Phase 3: Core Reverb Algorithm + Hard Clip
**Goal**: The per-22.05 kHz-tick reverb algorithm — SAME/DIFF IIR, 4-tap comb, APF1, APF2 — plus the mix-bus hard clip, run correctly against every documented spec behavior at the algorithmic level.
**Depends on**: Phase 2
**Requirements**: CORE-02, CORE-05, CORE-08, TEST-06, TEST-07
**Success Criteria** (what must be TRUE):
  1. Driving the reverb tick with isolated stage inputs (same-side IIR, diff-side IIR, 4-tap comb, APF1, APF2, output scale) produces outputs matching hand-derived nocash-pseudocode reference values bit-for-bit.
  2. A mix-bus input driven deliberately past `±0x7FFF` saturates to the documented hard-clip range and does so independently of the reverb network (clip stage is isolated and independently testable).
  3. Loading a register state with `vIIR = -0x8000` causes the final reverb result to be negated per the hardware anomaly, and a dedicated test asserts this against a non-anomaly control case.
  4. Fixed-point saturation, truncation direction, and signed-overflow edge cases are exercised by dedicated tests and all pass.
  5. `DECISIONS.md` contains entries for (a) comb-sum intermediate accumulation precision and (b) register-write timing between L-tick and R-tick within a 44.1 kHz sample pair.
**Plans**: TBD

### Phase 4: Sample Rate Conversion (39-tap half-band FIR)
**Goal**: SPU-94 is bit-faithful at the I/O boundary — the 44.1 kHz host rate is converted to/from the internal 22.05 kHz reverb rate via nocash's documented 39-tap half-band FIR, closing the fidelity gap that lv2-psx-reverb explicitly leaves open.
**Depends on**: Phase 3
**Requirements**: CORE-06, CORE-07
**Success Criteria** (what must be TRUE):
  1. The input decimator produces a 22.05 kHz stream from a 44.1 kHz input using nocash's exact 39-tap coefficient table in integer arithmetic, and an impulse input produces the documented symmetric half-band impulse response.
  2. The output interpolator round-trips a 22.05 kHz DC signal back to 44.1 kHz without bias or drift, and filter symmetry is verified to machine precision.
  3. The 39-tap Q15-product accumulation is verified to fit in its chosen intermediate width (documented in code) across the full int16 input range; no intermediate overflow is reachable.
  4. `DECISIONS.md` contains an entry documenting the half-rate architecture and explicitly recording that lv2-psx-reverb is NOT a witness on the frequency-response axis.
**Plans**: TBD

### Phase 5: Public API + Presets Integration
**Goal**: An external caller can feed 44.1 kHz stereo int16 audio through `spu94_process`, load any of the 10 factory presets, and modulate registers mid-stream without glitches, crashes, or reinitialization.
**Depends on**: Phase 4
**Requirements**: CORE-09, API-03, API-05, API-06, API-08
**Success Criteria** (what must be TRUE):
  1. A caller drives `spu94_process` with block-based int16 stereo at 44.1 kHz and receives int16 stereo at 44.1 kHz, with the 22.05 kHz reverb tick and FIR resampling fully hidden behind the API.
  2. All 10 PS1 factory reverb presets (Room, Studio A/B/C, Hall, Half Echo, Space Echo, Echo, Delay, Off) are loadable via a bulk `spu94_load_preset` call that writes all 35 registers atomically, and each preset produces non-zero reverb tails for non-silent input (except Off, which is silent).
  3. A caller can write any register at any block boundary during live processing and the output contains no crashes, no buffer corruption, no required `spu94_reset` call; the mid-stream write policy from Phase 2 is honored end-to-end.
  4. A benchmark-driven audit confirms `spu94_process` performs no heap allocations, holds no locks, issues no syscalls, and exhibits no variable-latency operations across 10^5 consecutive blocks.
**Plans**: TBD

### Phase 6: Python Binding + CLI
**Goal**: Tests, gray-area exploration, and golden-file generation happen in Python + numpy; a user can render a WAV through any preset with a single CLI invocation.
**Depends on**: Phase 5
**Requirements**: PYBIND-01, PYBIND-02, PYBIND-03, PYBIND-04, PYBIND-05, PYBIND-06, CLI-01, CLI-02, CLI-03, CLI-04, DOCS-04
**Success Criteria** (what must be TRUE):
  1. A Python user `import spu94`s, calls the full public C API via ctypes, passes numpy int16 arrays through `process_block` with zero-copy where possible, and register IntEnum values match the C-side enum at import time (asserted via runtime size/layout check).
  2. `spu94 --preset hall in.wav out.wav` and `spu94 --config preset.json in.wav out.wav` both succeed end-to-end using vendored dr_wav for I/O, producing correctly formatted output WAVs; errors exit non-zero with an actionable stderr message.
  3. `pip install -e .` installs a Python wheel built via scikit-build-core, and a Linux wheel is producible via cibuildwheel.
  4. `README.md` documents the build, a minimal Python and CLI usage example, the licensing posture summary, and the project-status banner; a reader unfamiliar with the project can build and run their first WAV render from it alone.
**Plans**: TBD
**UI hint**: yes

### Phase 7: Verification — Golden Files, Witness Diff, Modulation
**Goal**: SPU-94 earns its bit-faithful accuracy claim with defensible evidence — spec-conformance coverage, witness diffs against lv2-psx-reverb, golden-file regression snapshots, and a modulation harness that proves every register is live-controllable without instability.
**Depends on**: Phase 6
**Requirements**: TEST-01, TEST-03, TEST-04, TEST-05, TEST-08, BUILD-06, BUILD-08, DOCS-02, DOCS-03
**Success Criteria** (what must be TRUE):
  1. The spec-conformance suite enumerates every nocash-documented reverb behavior and each has at least one passing dedicated test; a coverage table in the repo maps behaviors to tests.
  2. The witness-diff harness cross-correlates SPU-94 output vs lv2-psx-reverb output per preset and reports aligned RMS divergence within documented per-preset tolerances, with the frequency-response axis explicitly excluded per the Phase 4 decision.
  3. Golden-file regression tests exist for each of the 10 presets × a standard input set (impulse, white noise, 1 kHz sine, silence), each with a SHA-256 sidecar, and files are byte-identical across clean Docker-pinned CI and the host dev environment.
  4. The modulation test sweeps every one of the 35 registers (sine, frequency sweep, random walk) during live processing and the output is bounded, stable, free of zipper noise on gain-type registers, and free of buffer corruption on address/delay-type registers — matching the Phase 2 write policy.
  5. A pytest-benchmark harness runs `spu94_process` under regression tracking and fails CI on pathological timing regressions or any hot-path allocation signal.
  6. `docs/LEVERS-CATALOG.md` annotates each of the 35 registers with its musical role, modulation cost (free / sample-quantized / catastrophic), expected zipper behavior, and suggested M4 lever grouping; `docs/BIBLIOGRAPHY.md` cites every nocash section and Sony SDK reference used, with all prose paraphrased (nothing transcribed).
**Plans**: TBD

### Phase 8: MCU Cross-Compile + CI Hardening
**Goal**: The MCU-portability claim is proven (not asserted) before M1 closes — `libspu94` compiles, links, and runs a reverb block on Cortex-M7 bare metal under arm-none-eabi-gcc with no heap, no HAL, no audio I/O.
**Depends on**: Phase 7
**Requirements**: BUILD-03
**Success Criteria** (what must be TRUE):
  1. A CI job builds `libspu94` + a minimal `mcu-smoke/main.c` (init + load_preset + one process block) via `arm-none-eabi-gcc` against a Cortex-M7 + fpv5-d16 toolchain file; the build is warning-clean and the link succeeds without newlib-nano missing-symbol errors.
  2. `arm-none-eabi-size` asserts the resulting `.text` section is under 64 kB, proving MCU-footprint viability.
  3. `readelf -d` confirms no references to `malloc`, `free`, `pthread_*`, or other unsupported symbols in the MCU binary; the freestanding-C constraint holds end-to-end.
**Plans**: TBD

## Progress

| Phase | Plans Complete | Status | Completed |
|-------|----------------|--------|-----------|
| 1. Foundation — Fixed-Point Math + Build Infrastructure | 0/? | Not started | - |
| 2. Buffer + Register Infrastructure | 0/? | Not started | - |
| 3. Core Reverb Algorithm + Hard Clip | 0/? | Not started | - |
| 4. Sample Rate Conversion (39-tap half-band FIR) | 0/? | Not started | - |
| 5. Public API + Presets Integration | 0/? | Not started | - |
| 6. Python Binding + CLI | 0/? | Not started | - |
| 7. Verification — Golden Files, Witness Diff, Modulation | 0/? | Not started | - |
| 8. MCU Cross-Compile + CI Hardening | 0/? | Not started | - |

## Coverage Audit

- v1 requirements: 49 total
- Mapped to phases: 49
- Unmapped: 0
- Duplicates: 0

Every v1 requirement in REQUIREMENTS.md maps to exactly one phase. Traceability table in REQUIREMENTS.md has been updated.

---
*Roadmap created: 2026-04-18*
