# Requirements: SPU-94

**Defined:** 2026-04-18
**Core Value:** Reproduce the PS1 SPU reverb algorithm from spec — sample-accurate where the spec is explicit, deliberately and documentedly chosen where it isn't — in a form that ports cleanly from desktop to hardware without a rewrite.

## v1 Requirements (Milestone 1)

Requirements for the first shipped artifact: a plain C library with Python bindings and CLI that reproduces the PS1 SPU reverb network and hard-clip stage. Each maps to roadmap phases during roadmap creation.

### CORE — DSP Algorithm

- [ ] **CORE-01**: Fixed-point Q15 arithmetic with integer truncation (not rounding), matching SPU saturation and overflow semantics
- [ ] **CORE-02**: Hard clip / saturation behavior on the mix bus (-0x8000..+0x7FFF), matching hardware
- [x] **CORE-03**: Reverb work buffer with correct wrap-address math per nocash (`BufferAddress = MAX(mBASE, (BufferAddress+2) AND 0x7FFFE)`) and documented mBASE-write side effects
- [x] **CORE-04**: All 35 SPU registers that affect reverb output, each implemented with documented behavior (the 32 reverb-block registers at `0x1F801DC0–0x1F801DFE` — including `vLIN`/`vRIN` reverb input volumes — plus `mBASE`, `vLOUT`, `vROUT`)
- [ ] **CORE-05**: All-pass + comb filter network topology matching nocash's documented processing order (input scale → SAME IIR → DIFF IIR → 4-tap comb → APF1 → APF2 → output scale → buffer advance)
- [ ] **CORE-06**: 39-tap half-band FIR at the 44.1→22.05kHz input boundary — nocash's documented coefficients verbatim
- [ ] **CORE-07**: 39-tap half-band FIR at the 22.05→44.1kHz output boundary — nocash's documented coefficients verbatim
- [ ] **CORE-08**: Reproduce the documented vIIR=-0x8000 hardware anomaly (negates the final reverb result) for bit-fidelity
- [ ] **CORE-09**: Ship all 10 documented PS1 factory reverb presets (Room, Studio A, Studio B, Studio C, Hall, Half Echo, Space Echo, Echo, Delay, Off) as register-config fixtures
- [x] **CORE-10**: Per-register mid-stream write policy — decided, documented in DECISIONS.md, implemented consistently (gain-type registers vs address/delay-type registers treated distinctly; mBASE handled as documented hardware special case)

### API — C Library Public Surface

- [x] **API-01**: Opaque handle type with caller-allocated state (no heap allocations in the library)
- [x] **API-02**: Init/reset/destroy lifecycle functions; caller provides work buffer memory
- [ ] **API-03**: `spu94_process` function taking int16 stereo input and producing int16 stereo output at 44.1kHz, block-based
- [x] **API-04**: Typed register read/write functions covering all 35 registers (via enum/constant identifiers, not magic numbers)
- [ ] **API-05**: Bulk preset-load function accepting a preset struct for atomic register updates
- [ ] **API-06**: Mid-stream register writes are first-class — no crashes, no buffer corruption, no required reinitialization; register modulation is a supported use case, not an edge case
- [x] **API-07**: Public header `spu94.h` is C99/C11 compliant, uses no C++-only features, wraps cleanly via `extern "C"` for future JUCE/C++ consumers
- [ ] **API-08**: No heap allocations in any hot-path function; no locks; no syscalls; no variable-latency operations; verified via static analysis and benchmark
- [x] **API-09**: Core library depends only on the freestanding C subset (no malloc, no stdio, no pthreads) — proves MCU portability at the API level

### PYBIND — Python Bindings

- [ ] **PYBIND-01**: ctypes-based wrapper exposing the full C API (no pybind11, no cffi)
- [ ] **PYBIND-02**: numpy array interop via `numpy.ctypeslib.ndpointer` for process_block I/O; zero-copy where possible
- [ ] **PYBIND-03**: Register identifiers exposed as Python IntEnum matching C-side enum values
- [ ] **PYBIND-04**: Factory preset fixtures loadable from Python
- [ ] **PYBIND-05**: Struct-layout drift caught by runtime assertion at import time (matched `_Static_assert` on C side + Python-side check)
- [ ] **PYBIND-06**: Buildable into a wheel via scikit-build-core + cibuildwheel on Linux

### CLI — Command-Line Tool

- [ ] **CLI-01**: `spu94` CLI reads an input WAV file, applies reverb with a named preset or register-config file, and writes an output WAV file
- [ ] **CLI-02**: Accepts either `--preset <name>` (e.g., `hall`) or `--config <path.json>` (explicit register values)
- [ ] **CLI-03**: Uses vendored dr_wav for WAV I/O; dr_wav is not linked into the core library
- [ ] **CLI-04**: Exits non-zero with a useful stderr message on any error

### TEST — Verification

- [ ] **TEST-01**: Spec-conformance test suite — each nocash-documented reverb behavior has a corresponding test
- [x] **TEST-02**: Register-level unit tests — each of the 35 registers exercised in isolation (value sweeps, edge cases, zero-value-meaningful cases)
- [ ] **TEST-03**: Witness-diff harness running the same input through lv2-psx-reverb (output-only; no source reading) and comparing outputs; lv2-psx-reverb is excluded from the frequency-response axis per Key Decisions
- [ ] **TEST-04**: Golden-file regression tests — each preset and reference test case has a snapshot-and-compare golden file; byte-stable across reproducible-build environments
- [ ] **TEST-05**: Modulation test — every register modulated during a live audio stream (sine, sweep, random walk); output verified bounded, stable, non-corrupting, and matching the decided write-policy semantics
- [ ] **TEST-06**: vIIR=-0x8000 hardware anomaly specifically tested
- [ ] **TEST-07**: Fixed-point saturation, truncation, and overflow edge cases specifically tested
- [ ] **TEST-08**: Reproducibility — golden files identical across clean Docker-pinned CI and host dev environments

### BUILD — Build System and Portability

- [ ] **BUILD-01**: CMake-based build producing shared and static library artifacts on Linux
- [ ] **BUILD-02**: Determinism compiler flags locked in (`-ffp-contract=off`, `-fno-fast-math`, `-Werror` on agreed warning set)
- [ ] **BUILD-03**: Cross-compile smoke test to Cortex-M7 via `arm-none-eabi-gcc` (bare metal, no libDaisy, no audio I/O; confirms the core links and runs on MCU)
- [ ] **BUILD-04**: Static analysis in CI — clang-tidy, cppcheck, compiler warnings as errors
- [ ] **BUILD-05**: UndefinedBehaviorSanitizer in CI with surgical `__attribute__((no_sanitize("integer")))` annotations on functions where overflow is the intended SPU behavior
- [ ] **BUILD-06**: Benchmark harness (pytest-benchmark) verifying no hot-path allocations and no pathological timing regressions
- [ ] **BUILD-07**: CI grep guard prohibiting `float`, `double`, `malloc`, `calloc`, `realloc`, `free`, `long` (unqualified) in core library sources
- [ ] **BUILD-08**: Docker-pinned reproducible build environment for golden-file determinism

### DOCS — First-Class Documentation Artifacts

- [ ] **DOCS-01**: `DECISIONS.md` begun and maintained — gray-area log with one entry per resolution, each including: the ambiguity, the options considered, what was chosen, the rationale, and (where relevant) what witnesses were consulted and what their source references were
- [ ] **DOCS-02**: `docs/LEVERS-CATALOG.md` begun and maintained — each register annotated with its musical role and candidacy for M4 lever mapping (Room Size, Pre Delay, Decay, Diffusion, Damping, etc.)
- [ ] **DOCS-03**: Bibliography tracking nocash citations, Sony SDK references, and other fact sources — paraphrased in SPU-94's own docs, never transcribed
- [ ] **DOCS-04**: `README.md` with build instructions, minimal usage example, licensing posture summary, and project-status banner
- [ ] **DOCS-05**: `LICENSE` placeholder noting that the final permissive-license pick (MIT vs Apache-2.0) is deferred to end of M1

## v2+ Requirements (Deferred to Future Milestones)

### Milestone 2 — ADPCM

- **M2-ADPCM-01**: 4-bit Sony ADPCM encode
- **M2-ADPCM-02**: 4-bit Sony ADPCM decode with filter coefficient tables, loop flags, block structure
- **M2-ADPCM-03**: Integrated pipeline: PCM → ADPCM encode → ADPCM decode → M1 reverb → PCM
- **M2-ADPCM-04**: ADPCM-specific DECISIONS.md entries for its own gray areas

### Milestone 3 — DAC Reconstruction Modeling

- **M3-DAC-01**: Period-appropriate DAC coloration modeled in DSP
- **M3-DAC-02**: Reconstruction filter matching PS1 DAC chip behavior (CXD2562Q / CXD2925Q era)
- **M3-DAC-03**: DAC modeling stays in software; hardware DAC recreation is a future project

### Milestone 4 — Plugin and Musical Lever Layer

- **M4-PLUG-01**: C++ / JUCE wrapper around the C core
- **M4-PLUG-02**: VST3, AU, LV2, and Standalone plugin targets
- **M4-LEVERS-01**: Named musical lever abstraction (Room Size, Pre Delay, Decay, Diffusion, Damping, etc.) mapping one or more registers per lever with smooth curves
- **M4-LEVERS-02**: Parameter smoothing layer (zipper-noise-free modulation)
- **M4-LEVERS-03**: CV-style input paths for each lever, ready for Eurorack wiring
- **M4-PLUG-03**: Factory preset menu in the plugin UI
- **M4-PLUG-04**: Advanced mode exposing raw register access

### Milestone 5 — Hardware Validation

- **M5-HW-01**: PSX homebrew that loads known PCM and sets known register values
- **M5-HW-02**: Digital capture path for the PS1's pre-DAC SPU output (hardware mod)
- **M5-HW-03**: Witness-diff pipeline: SPU-94 output vs captured hardware output
- **M5-HW-04**: Update DECISIONS.md with hardware-observed divergences and resolutions

### Deferred Platform Support

- **v2-PLAT-01**: macOS build (Homebrew clang, universal binary)
- **v2-PLAT-02**: Windows build (MSVC or MinGW)
- **v2-PLAT-03**: Full libDaisy integration (beyond smoke test)
- **v2-PLAT-04**: FPGA HLS target
- **v2-PLAT-05**: Eurorack hardware module (PCB, panel, period-appropriate DAC hardware, CV inputs)

## Out of Scope

Explicitly excluded from SPU-94 entirely, or deliberately rejected as features that would compromise the Core Value.

| Feature | Reason |
|---------|--------|
| SPU voice engine (voice allocation, ADSR envelopes, pitch modulation, noise generator) | This project is reverb-focused only; modeling the full SPU is ~10× the scope |
| Oversampling / anti-aliasing beyond the documented 39-tap FIR | Changes the algorithm's spectral character; defeats bit-faithfulness |
| Automatic gain compensation / normalization | Masks the documented hard-clip behavior that is part of the sound |
| Stereo widening / spatial enhancement | Compromises bit-faithfulness; the PS1 stereo image is part of the character |
| Float / double arithmetic anywhere in the core library | Breaks bit-exactness across platforms; explicitly enforced by CI grep guard |
| IIR low-pass filters as an alternative to the FIR | Would not be bit-exact across platforms; FIR is the correct choice |
| Parameter smoothing baked into the core library | Blurs the bit-faithfulness claim; smoothing belongs in the M4 lever layer |
| Reading Mednafen, lv2-psx-reverb, DuckStation, or MiSTer source as a primary development activity | Licensing posture — GPL avoidance and original-work defense |
| Transcribing nocash prose, tables, or phrasing into SPU-94 documentation | nocash's copyright status is ambiguous; SPU-94 paraphrases facts with bibliography entries |
| Product name selection | Deferred to pre-ship; SPU-94 names the algorithm, not necessarily the product |
| Patent / trademark legal review | Deferred to pre-commercial if/when commercialization becomes active |

## Traceability

Mapping of requirements to roadmap phases — populated during roadmap creation.

| Requirement | Phase | Status |
|-------------|-------|--------|
| CORE-01 | Phase 1 | Pending |
| CORE-02 | Phase 3 | Pending |
| CORE-03 | Phase 2 | Complete |
| CORE-04 | Phase 2 | Complete |
| CORE-05 | Phase 3 | Pending |
| CORE-06 | Phase 4 | Pending |
| CORE-07 | Phase 4 | Pending |
| CORE-08 | Phase 3 | Pending |
| CORE-09 | Phase 5 | Pending |
| CORE-10 | Phase 2 | Complete |
| API-01 | Phase 2 | Complete |
| API-02 | Phase 2 | Complete |
| API-03 | Phase 5 | Pending |
| API-04 | Phase 2 | Complete |
| API-05 | Phase 5 | Pending |
| API-06 | Phase 5 | Pending |
| API-07 | Phase 2 | Complete |
| API-08 | Phase 5 | Pending |
| API-09 | Phase 2 | Complete |
| PYBIND-01 | Phase 6 | Pending |
| PYBIND-02 | Phase 6 | Pending |
| PYBIND-03 | Phase 6 | Pending |
| PYBIND-04 | Phase 6 | Pending |
| PYBIND-05 | Phase 6 | Pending |
| PYBIND-06 | Phase 6 | Pending |
| CLI-01 | Phase 6 | Pending |
| CLI-02 | Phase 6 | Pending |
| CLI-03 | Phase 6 | Pending |
| CLI-04 | Phase 6 | Pending |
| TEST-01 | Phase 7 | Pending |
| TEST-02 | Phase 2 | Complete |
| TEST-03 | Phase 7 | Pending |
| TEST-04 | Phase 7 | Pending |
| TEST-05 | Phase 7 | Pending |
| TEST-06 | Phase 3 | Pending |
| TEST-07 | Phase 3 | Pending |
| TEST-08 | Phase 7 | Pending |
| BUILD-01 | Phase 1 | Pending |
| BUILD-02 | Phase 1 | Pending |
| BUILD-03 | Phase 8 | Pending |
| BUILD-04 | Phase 1 | Pending |
| BUILD-05 | Phase 1 | Pending |
| BUILD-06 | Phase 7 | Pending |
| BUILD-07 | Phase 1 | Pending |
| BUILD-08 | Phase 7 | Pending |
| DOCS-01 | Phase 1 | Pending |
| DOCS-02 | Phase 7 | Pending |
| DOCS-03 | Phase 7 | Pending |
| DOCS-04 | Phase 6 | Pending |
| DOCS-05 | Phase 1 | Pending |

**Coverage:**
- v1 requirements: 49 total
- Mapped to phases: 49
- Unmapped: 0

---
*Requirements defined: 2026-04-18*
*Last updated: 2026-04-18 after roadmap creation (traceability populated)*
