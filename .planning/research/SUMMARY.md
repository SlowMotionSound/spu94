# Research Summary — SPU-94

**Project:** SPU-94 — bit-faithful PS1 SPU reverb C library
**Domain:** Fixed-point DSP reimplementation, plain C library, MCU/FPGA-portable
**Researched:** 2026-04-18
**Confidence:** HIGH on algorithm and architecture (nocash psx-spx is unambiguous); MEDIUM-HIGH on stack choices; MEDIUM on legal and community-demand signals

---

## Updates Needed in PROJECT.md

Research revealed four facts that contradict or underspecify the current PROJECT.md. These must be reconciled before roadmap creation so phases are built on correct assumptions.

### 1. Register count is 33, not 24

PROJECT.md says "implement all 24 SPU reverb registers." The full reverb register set as documented in nocash psx-spx is **33 registers**:

- 2 output volumes (vLOUT, vROUT) at 1F801D84-D86 — outside the main block
- 1 base address (mBASE) at 1F801DA2 — outside the main block
- 28 registers in the 1F801DC0-DFEh block (dAPF1/2, vIIR, vCOMB1-4, vWALL, vAPF1/2, mLSAME/mRSAME, mLCOMB1-4/mRCOMB1-4, dLSAME/dRSAME, mLDIFF/mRDIFF, dLDIFF/dRDIFF, mLAPF1/2/mRAPF1/2)
- 2 input volumes (vLIN, vRIN) at 1F801DFC-DFE

The "24" framing counts only a subset of the register block. The code, API, and spu94_reg_t enum use 33. ipatix's struct has 32 fields, confirming the count is in the 32-33 range.

**Action:** Update PROJECT.md M1 bullet to "implement all 33 SPU reverb registers with documented behavior."

### 2. Sample rate architecture: 22.05 kHz internal, 44.1 kHz boundary

The PS1 SPU reverb runs its state machine at 22.05 kHz (L and R each processed on alternating 44.1 kHz ticks). Input is downsampled via a 39-tap symmetric half-band FIR (coefficients documented in nocash); output is upsampled via the same FIR. lv2-psx-reverb explicitly skips this and its README documents the resulting brightness difference. SPU-94 must implement the 39-tap FIR on both sides. The core processes at 22.05 kHz; spu94_process() accepts 44.1 kHz pairs and handles conversion internally. This is a first-class M1 deliverable.

**Action:** Add to PROJECT.md M1 active items: "Implement 39-tap half-band FIR decimator (44.1->22.05 kHz) and interpolator (22.05->44.1 kHz); core reverb state machine runs at 22.05 kHz."

### 3. nocash psx-spx has ambiguous copyright status

The psx-spx GitHub repo README explicitly states it contains material "copy/pasted from confidential code and documentation from Sony." SPU-94 documentation must not reproduce nocash tables verbatim — paraphrase in its own words. Register values (facts) are uncopyrightable individually; presentation structure should be SPU-94's own. Cite nocash as one reference among several, not the single authoritative source.

**Action:** Add a caveat to PROJECT.md's Context section noting the copyright ambiguity and the paraphrase-not-transcribe rule.

### 4. Register names in PROJECT.md are PS2/PSP names, not PS1 names

PROJECT.md's Context section mentions register names like FB_SRC_A, FB_SRC_B, IIR_ALPHA, ACC_COEF_A/B/C/D. These are PS2/PSP SPU2 register names, not PS1 names. The PS1 names (from nocash) are: vIIR, vCOMB1-4, vWALL, vAPF1/2, mLSAME/mRSAME, etc. SPU-94 implements the PS1 set exclusively.

**Action:** Remove PS2 register name examples from PROJECT.md Context or clearly label them as PSP/PS2 names used for illustrative comparison only.

---

## Executive Summary

SPU-94 is a bit-faithful software reimplementation of the Sony PS1 SPU reverb algorithm — a Schroeder/Gardner-style network of comb filters and all-pass filters operating on a fixed-point work buffer. The project's entire value proposition is that the character of the PS1 reverb comes from implementation artifacts: Q15 truncation (not rounding), hard-clip overflow behavior on the mix bus, 22.05 kHz internal processing with half-band FIR reconstruction, and the specific gain topology across 33 registers. Any design decision that smoothes over these artifacts — floating-point math, rounded arithmetic, missing the half-rate FIR, parameter smoothing in the core — destroys what makes the sound recognizable. This commits the implementation to significant discipline in the hot path while remaining a useful tool.

The recommended approach is a layered plain-C architecture: a hermetic libspu94 core with caller-allocated state and work buffers, zero heap use, and integer-only arithmetic in the reverb path; Python ctypes bindings for test and exploration; a small CLI for golden-file rendering; and a Cortex-M cross-compile smoke test that proves MCU portability cheaply in M1. The algorithm is well-specified in nocash psx-spx. The gray areas are narrow but important: Q15 truncation direction, exact per-register mid-stream write semantics, the mBASE write behavior, comb-sum intermediate precision, and the vIIR = -0x8000 hardware anomaly. All of these need a DECISIONS.md entry with a witness check before code is written.

The key risks are: (1) subtle algorithmic divergence that only surfaces after many reverb recirculations (truncation vs rounding, operation ordering); (2) the missing 22.05 kHz half-rate FIR causing SPU-94 to sound brighter than hardware; (3) inadvertent float creep in the reverb core; and (4) mid-stream register write semantics becoming a source of instability if not handled with a per-register policy from the start. All four are preventable with the architectural and testing patterns identified in research.

---

## Key Findings

### Recommended Stack

The stack is largely locked in. Research fills in specific versions and settles remaining open questions.

**Core technologies:**
- **C11** (`-std=c11 -pedantic`) — buys `_Static_assert`, `_Alignas`, anonymous structs; all toolchains in scope support it fully. HIGH confidence.
- **CMake 3.25+** as integrator build system + hand-written `core/Makefile.inc` for Daisy template compatibility. Meson is technically cleaner but loses on ecosystem gravity and Daisy's existing Makefile-based workflow.
- **dr_wav v0.14.5** vendored into `cli/` — MIT-0/public domain, single-header. libsndfile is LGPL-2.1-or-later, which contaminates downstream licensing.
- **Hand-rolled fixed-point helpers** in `spu94_fixed.h` — explicitly not libfixmath (rounds), not CMSIS-DSP (saturating+rounding), not fpm (C++ only). All external fixed-point libraries make the "correct" DSP arithmetic choice, which is the wrong arithmetic for PS1 SPU emulation.
- **pytest-regressions 3.0+** for golden-file snapshotting; **pytest-benchmark 5.x** for real-time safety regression tracking.
- **scikit-build-core 0.10+** + cibuildwheel for Python wheel packaging.

**Critical compiler flags:**
- `-ffp-contract=off -fno-fast-math` — GCC defaults to `-ffp-contract=fast` even without `-ffast-math`, enabling FMA substitution that breaks cross-machine golden-file determinism. Most commonly missed flag in DSP libraries claiming reproducibility.
- `-fsanitize=integer` in CI (Clang) with `__attribute__((no_sanitize("integer")))` on documented SPU saturation functions.
- `-O2` not `-O3` — autovectorization can shift last-bit results.

**CI:** Pinned Docker image (not Nix) is sufficient for output reproducibility. Five discipline points: integer core, `-ffp-contract=off`, pinned toolchain, pinned Python deps via `uv lock`, normalized build artifacts.

### Expected Features

**Must have (M1 ships):**
- T1-T15 (all table-stakes API): create/init with caller-allocated state+buffer, reset, process stereo int16 planar (bit-faithful entry point), process stereo float32, set/get register by enum, load preset (atomic 33-register write), bulk register write, version query, last-error query, documented contract, Python ctypes binding, CLI for WAV processing, work-buffer lifecycle, threading contract
- 10 factory presets as C constant arrays (not JSON at runtime — zero parser, zero MCU RAM)
- D1: register-level runtime API as first-class (the project's core differentiator)
- D3: glitch-free mid-stream writes, tested via modulation harness
- D7: bit-faithful flags in config struct (`bit_faithful_truncation`, `bit_faithful_clipping`)
- DECISIONS.md and LEVERS-CATALOG.md maintained throughout M1
- Witness-diff harness + golden files
- Cortex-M cross-compile smoke test

**Should have (differentiators in M1):**
- D2: DECISIONS.md as first-class deliverable (novelty — no PS1 reverb project publishes this)
- D5: witness-diff harness published as a community resource
- D6: MCU cross-compile proved, not just claimed

**Defer (M2+):**
- ADPCM encode/decode (M2)
- DAC reconstruction model (M3)
- JUCE plugin with musical levers (M4)
- Parameter smoothing, user-editable preset banks, CV-style modulation UI (M4)
- Hardware validation via PSX homebrew + digital capture (M5)

**Anti-features (never in core):**
- Internal float math, parameter smoothing, AGC on preset change, automatic SRC inside core, stereo widening, oversampling — all destroy the artifact character that is the product

### Architecture Approach

The architecture is an opaque-handle, caller-allocated-state, block-processing C library with a hermetic core. The opaque handle means struct layout can evolve without ABI breaks; caller-allocated storage means zero heap use in the library, enabling MCU and FPGA porting without modification.

**Major components:**
1. `spu94_fixed.c` — Q15 truncating multiply, saturating arithmetic, `_Static_assert` on arithmetic shift direction
2. `spu94_buffer.c` — BufferAddress advance (`MAX(mBASE, (addr+2) AND 0x7FFFE)`), tap-address resolution, mBASE-write reset
3. `spu94_registers.c` — per-register write policy (immediate for v* gains, latched-to-tick for d*/m* addresses), validation table for all 33 registers
4. `spu94_clip.c` — hard clip on the mix bus input (independent of reverb network)
5. `spu94_resample.c` — 39-tap half-band FIR decimator (44.1->22.05 kHz) and interpolator (22.05->44.1 kHz)
6. `spu94_reverb.c` — the per-22050 Hz tick algorithm, line-by-line from nocash pseudocode
7. `spu94_presets.c` — static read-only preset data in flash/rodata
8. `spu94_public.c` — public API dispatch, init, the process loop orchestrating FIR+reverb+FIR

**Key pattern decisions:**
- Flat `uint16_t regs[33]` indexed by enum (slight type-safety tradeoff for trivial preset save/restore, trivial Python binding, trivial bulk ops)
- Per-register write policy (Option C): v* registers immediate, d*/m* latched to 22050 Hz tick boundary, mBASE special (resets BufferAddress)
- 39-tap FIR delay lines (4 x 39 int16 = 312 bytes) live in spu94_state, not the work buffer

### Critical Pitfalls

1. **Missing the 22.05 kHz half-rate FIR** — running reverb at 44.1 kHz produces a brighter reverb than hardware. lv2-psx-reverb makes this tradeoff and documents it. SPU-94 must not. Implement `spu94_resample.c` with nocash's 39-tap coefficients before wiring to the reverb algorithm. Do not use lv2-psx-reverb as a frequency-accuracy witness.

2. **Truncation vs rounding in Q15 multiply** — catastrophic for bit-accuracy, invisible on single samples, cumulative after recirculation. Use a single `q15_mul_truncate` helper everywhere; unit-test with negative operands (where truncate and round diverge); use no external fixed-point library (all round).

3. **Signed vs unsigned coefficient interpretation** — all v* registers are signed int16; treating them as unsigned turns negative coefficients into wildly wrong positive values. Public API takes `int16_t` for volumes, `uint16_t` for addresses; internal storage is `int16_t` for coefficients.

4. **Saturation vs wrap on overflow** — intermediate reverb values must saturate to [-0x8000, +0x7FFF]; address registers must wrap. Separate helpers for each; both independently tested.

5. **Float creep in the reverb core** — a single float intermediate breaks the bit-accuracy claim after recirculation. CI grep: `\b(float|double)\b` in `libspu94/src/reverb*.c` must return zero matches.

6. **vIIR = -0x8000 hardware anomaly** — PS1 hardware negates the result at this specific value. Explicit special-case required; unit test and DECISIONS.md entry.

7. **Work buffer addressing errors** — the `MAX(mBASE, (addr+2) AND 0x7FFFE)` formula has multiple ways to go wrong (signed vs unsigned comparison, wrong mask, wrong stride). Centralize in `spu94_buffer.c`; fuzz test with 10^6 advance steps.

**Gray-area decisions that must be committed before code (DECISIONS.md entries):**

| Decision | Must resolve in phase | Risk if deferred |
|----------|----------------------|------------------|
| Q15 multiply: `>> 15` vs `/ 0x8000` (same on our targets, UB by C standard) | Phase 1 | Static analyzers flag it; exotic targets diverge |
| Comb sum: saturate after each term or accumulate 32-bit and saturate once at output | Phase 3 | Different behavior on loud transients |
| mBASE write: zero work buffer or leave stale content? | Phase 2 | Preset-switch behavior; modulation test design |
| Per-register write policy: latched to tick boundary vs immediate for d*/m* | Phase 2 | All modulation tests depend on uniform policy |
| Register write between L-tick and R-tick: stereo-synchronous vs immediate | Phase 3 | Subtle L/R imbalance on fast modulation |
| vIIR = -0x8000: reproduce negate-result bug or clamp to -0x7FFF? | Phase 1 | Edge-case preset audition diverges from hardware |

---

## Implications for Roadmap

All four researchers converged on the same implementation order. The following 8-phase sequence is recommended for M1.

### Phase 1 — Foundation: Fixed-Point Math + Project Infrastructure

**Rationale:** Every other module calls into fixed-point helpers. Building them wrong makes every downstream test meaningless. Simultaneously, the build system and CI must be in place before any work can be tracked. This is also when the gray-area decisions with earliest code impact (Q15 truncation, vIIR anomaly) must be committed.

**Delivers:**
- `spu94_fixed.c` / `spu94_math.h` with `q15_mul_truncate`, `sat_s16`, `addr_wrap_u16`, `q15_shr` with `_Static_assert` on arithmetic shift
- Full unit test suite for fixed-point helpers covering negative operands, INT16_MIN, boundary values
- CMake build, Makefile wrapper, CI jobs (GCC, Clang, sanitizers, MCU smoke scaffold)
- DECISIONS.md template committed; first entries: Q15 truncation direction, vIIR = -0x8000 policy

**Avoids:** Truncation vs rounding pitfall (1.4), signed right-shift UB pitfall (2.3)

---

### Phase 2 — Buffer + Register Infrastructure

**Rationale:** Buffer addressing and register write policy must be locked before the reverb algorithm. The algorithm reads/writes tap addresses that depend on both. Bugs here are the most subtle because they appear as buffer-wrap artifacts or phase discontinuities rather than obviously wrong values.

**Delivers:**
- `spu94_buffer.c` — BufferAddress advance, tap-address resolution, mBASE-write behavior
- `spu94_registers.c` — write/read with per-register policy, validation table for all 33 registers, double-buffer pending mask for latched registers
- `spu94_public.c` (stub) — `spu94_state_size()`, `spu94_init()`, `spu94_reset()`, `spu94_write_reg()`, `spu94_read_reg()` wired
- Unit tests: BufferAddress boundary (mBASE clamp, 0x7FFFE wrap), fuzz address, register round-trip for all 33 registers, signed/unsigned interpretation

**Avoids:** Buffer addressing errors (pitfall 1.7), signed/unsigned coefficient confusion (pitfall 1.2), mid-stream write instability (pitfalls 4.1, 4.3)

---

### Phase 3 — Core Reverb Algorithm

**Rationale:** All foundations are in place. Bugs in this phase are algorithm bugs or spec-interpretation bugs, not infrastructure bugs. The algorithm file should be a nearly literal transcription of the nocash pseudocode.

**Delivers:**
- `spu94_reverb.c` — `spu94_tick_22khz(state)` implementing nocash pseudocode line-by-line, each stage in its own named local with nocash citation comment
- `spu94_clip.c` — hard clip on mix bus input
- Conformance tests for each sub-stage: same-side IIR, diff-side IIR, 4-tap comb, APF1, APF2, output scale
- vIIR = -0x8000 hardware anomaly test
- Saturation test with deliberately overflowing operands

**Avoids:** Operation reordering (pitfall 1.10), saturation vs wrap confusion (pitfall 1.5), input gain misplacement (pitfall 1.9)

---

### Phase 4 — Sample Rate Conversion (39-tap FIR)

**Rationale:** The 22.05 kHz half-rate reverb core exists. The FIR is logically separate from the algorithm and can be built and tested independently. This phase is the most novel part of the implementation — no existing open-source PS1 reverb library models it correctly.

**Delivers:**
- `spu94_resample.c` — 39-tap FIR decimator (44.1->22.05 kHz) and interpolator (22.05->44.1 kHz) using nocash's exact coefficient table in integer arithmetic
- Unit tests: impulse response, DC passthrough, frequency response at Nyquist boundary, filter symmetry
- DECISIONS.md entry documenting the half-rate architecture and recording that lv2-psx-reverb is NOT a frequency-accuracy witness

**Avoids:** Missing 22.05 kHz half-rate FIR (pitfall 1.6 — the single biggest implementation divergence in the field)

---

### Phase 5 — Public API + Presets Integration

**Rationale:** All DSP modules exist. Wire them through the public API and load the 10 factory presets as the first real end-to-end integration test.

**Delivers:**
- `spu94_public.c` fully wired — `spu94_process()` calling FIR decimator + `spu94_tick_22khz()` + FIR interpolator; `spu94_load_preset()` calling bulk register write
- `spu94_presets.c` — 10 factory preset arrays as C constants, all 33 fields populated with nocash values
- `spu94_version_string()`, `spu94_last_error()` API surface
- End-to-end integration test: each preset, impulse input, output bounded and non-zero
- `spu94_work_buffer_size_bytes()` helper with correct values for all 10 presets

**Avoids:** "Reverb off" preset non-silence (pitfall 1.8), zero-register aliasing

---

### Phase 6 — Python Binding + CLI

**Rationale:** With a working library, bind Python and ship the CLI. These unlock the test harness, golden-file generation, and witness-diff infrastructure. The binding is thin; build what is being bound first.

**Delivers:**
- `bindings/python/spu94/` — ctypes bindings with all function signatures declared, numpy array exchange, `Reg` IntEnum, `Preset` enum, `py.typed` marker
- `cli/spu94_cli.c` — `spu94 [--preset NAME] in.wav out.wav`, dr_wav for I/O
- `pyproject.toml` with scikit-build-core backend; `pip install -e .` works
- Basic Python tests: create/destroy, load_preset, process round-trip, register read-back

**Avoids:** numpy GC while ctypes pointer live (pitfall 5.1), ctypes struct layout mismatch (pitfall 5.3), hidden allocations in Python binding (pitfall 3.1)

---

### Phase 7 — Verification: Golden Files, Witness Diff, Modulation Test

**Rationale:** The library works. Now prove it. This phase is the verification pass — the moment M1 earns its accuracy claim or reveals remaining bugs. Golden files must be generated from a substantially correct implementation.

**Delivers:**
- Golden-file infrastructure — signed-off WAVs for each preset x standard input (impulse, white noise, 1 kHz sine, silence); SHA-256 sidecars; sign-off criteria documented
- Witness-diff harness — cross-correlate SPU-94 vs lv2-psx-reverb; report aligned RMS diff; tolerance calibrated per preset; README documents known divergence on frequency content (lv2-psx-reverb does not downsample to 22.05 kHz)
- Modulation test — sweep each register (sine, random walk) during live processing; assert output bounded, no crashes, no buffer corruption; delay-length changes produce expected click, not corruption
- LEVERS-CATALOG.md — all 33 registers annotated with musical role, modulation cost (free/sample-quantized/catastrophic), expected zipper behavior, suggested M4 lever grouping

**Avoids:** Witness-chasing (pitfall 7.3), false confidence from preset audition (pitfall 7.4), golden files pinned to buggy implementation (pitfall 2.5), sample-offset ambiguity in witness diffs (pitfall 2.4)

---

### Phase 8 — MCU Cross-Compile Smoke Test + CI Hardening

**Rationale:** Prove MCU portability before M1 closes. Cheap now; expensive to retrofit after M4 adds plugin machinery.

**Delivers:**
- `mcu-smoke/main.c` — minimal arm-none-eabi-gcc build: spu94_init + spu94_load_preset + spu94_process; no HAL, no libDaisy, no heap
- `cmake/toolchain-arm-none-eabi.cmake` with cortex-m7 + fpv5-d16 flags
- CI mcu-smoke job: compile + link clean; `arm-none-eabi-size` asserts `.text < 64 kB`
- CI guards: float-in-core grep; malloc-in-libspu94 grep; long/int usage grep; `readelf -d` ABI audit
- `_Static_assert(sizeof(spu94_state_t) == EXPECTED)` and Python-side size assertion at import

**Avoids:** MCU dynamic allocation (pitfall 8.1), long/int portability (pitfall 8.2), unaligned access (pitfall 8.3), newlib-nano missing symbols (pitfall 8.5)

---

### Phase Ordering Rationale

- Foundation before algorithm: phases 1 and 2 must precede phase 3 because algorithm correctness depends on helpers and buffer/register infra being correct first
- FIR before public API wiring: phase 4 must precede phase 5 because spu94_process() orchestrates FIR + algorithm + FIR
- Presets after algorithm: preset loading is "write 33 registers then process"; depends on phases 1-4
- Python/CLI after core: bind what exists; no dependency inversion
- Verification last: golden files generated from a substantially correct implementation only; generating from a broken one is a trap
- MCU smoke last: portability proof, not a functional test; the core must exist

### Research Flags

Phases with well-documented patterns (skip dedicated research phase):
- Phase 1 (fixed-point helpers): Standard Q15 DSP; nocash specifies truncation explicitly
- Phase 2 (buffer/register infra): Architecture fully designed in ARCHITECTURE.md; decisions are policy choices, not unknown domains
- Phase 5 (presets/API wiring): Preset values in nocash; API fully designed
- Phase 6 (Python/CLI): Stack choices locked; patterns documented in STACK.md
- Phase 8 (MCU smoke): Lean smoke-test approach is fully designed

Phases that may benefit from targeted research during planning:
- Phase 4 (39-tap FIR): FIR specified by nocash; integer implementation (Q-format precision, intermediate accumulation width) may need a narrow research spike to verify the implementation matches hardware exactly
- Phase 7 (witness diff): lv2-psx-reverb diverges on frequency content (documented); calibrating appropriate aligned-RMS-diff tolerance requires actually running both and measuring — flag as empirical calibration time, not a one-liner

---

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Algorithm (nocash spec) | HIGH | Unambiguous on register layout, pseudocode, FIR coefficients, saturation; cross-referenced against ipatix |
| Stack (C11, CMake, dr_wav, hand-rolled fixed-point) | HIGH | All project-specific and well-reasoned; license confirmations done |
| Stack (Python packaging, scikit-build-core) | MEDIUM-HIGH | Mainstream 2026 path; evolving ecosystem but stable choices |
| Register count (33, not 24) | HIGH | Directly counted from nocash; cross-referenced against ipatix struct |
| 22.05 kHz half-rate architecture | HIGH | Documented in nocash including FIR coefficients; lv2-psx-reverb deviation self-documented |
| Per-register write policy | MEDIUM | nocash note is brief; specific per-register policy is a design choice, not hardware-confirmed |
| FIR integer implementation precision | MEDIUM | Coefficients exact; integer implementation approach requires verification during implementation |
| nocash copyright status | MEDIUM | psx-spx GitHub README is candid; risk real but low for personal non-commercial project |
| Community demand signals | MEDIUM | Indirect (forum inference, GameVerb marketing, Shirobon IR pack); no direct user research |

**Overall confidence:** HIGH on algorithm correctness and architecture; MEDIUM on a few implementation details requiring empirical verification.

### Gaps to Address

- **mBASE write behavior** — does writing mBASE zero the work buffer or leave stale content? Research found ipatix's approach (memset) but not hardware confirmation. Resolve in DECISIONS.md during Phase 2 by choosing hardware-consistent behavior (hardware does NOT zero; stale content is part of reverb tail bleed).
- **Comb sum intermediate precision** — spec says result written as saturated int16 but says nothing about intermediate precision during 4-tap accumulation. Flagged for DECISIONS.md in Phase 3.
- **Integer FIR coefficient scaling** — nocash FIR coefficients are Q15 values (e.g., 4000h for center tap); verify that 39-term Q15-product accumulation fits in 32-bit intermediate. Simple mathematical check during Phase 4.
- **Witness-diff tolerance calibration** — no lv2-psx-reverb outputs have been produced yet; aligned RMS divergence tolerance is empirical work in Phase 7.
- **Hardware capture** — the M5 plan (PSX homebrew + digital capture) is the only true ground-truth for several gray-area decisions. DECISIONS.md should flag which decisions are "subject to revision when hardware capture is available."

---

## Sources

### Primary (HIGH confidence)

- [nocash psx-spx — Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) — register layout, reverb pseudocode, 39-tap FIR coefficients, buffer addressing, saturation rule, vIIR anomaly
- [problemkaputt.de/psx-spx.htm](https://problemkaputt.de/psx-spx.htm) — canonical mirror
- [ipatix/lv2-psx-reverb](https://github.com/ipatix/lv2-psx-reverb) — API shape, preset struct (32 fields), self-documented 22050 Hz deviation
- [BodbDearg/PlayStation1Vsts](https://github.com/BodbDearg/PlayStation1Vsts) — 44.1 kHz-only approach; PsyQ SDK preset inheritance
- [dr_libs GitHub (mackron)](https://github.com/mackron/dr_libs) — v0.14.5 license (MIT-0) confirmed
- [libsndfile homepage](https://libsndfile.github.io/libsndfile/) — LGPL-2.1-or-later confirmed
- [libDaisy Makefile](https://github.com/electro-smith/libDaisy/blob/master/core/Makefile) — Daisy toolchain conventions
- [Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) — arm-none-eabi-gcc current release
- [GCC FP Implementation docs](https://gcc.gnu.org/onlinedocs/gcc/Floating-point-implementation.html) — -ffp-contract default behavior
- [pytest-regressions (ESSS)](https://github.com/ESSS/pytest-regressions) — golden-file plugin with numpy support
- [psx-spx GitHub README](https://github.com/psx-spx/psx-spx.github.io/blob/master/docs/index.md) — copyright ambiguity statement

### Secondary (MEDIUM confidence)

- [jsgroth — PS1 SPU Part 4](https://jsgroth.dev/blog/posts/ps1-spu-part-4/) — corroboration of reverb half-rate characteristic
- [Simon Byrne: Beware of fast-math](https://simonbyrne.github.io/notes/fastmath/) — determinism writeup
- [blastbay/verblib](https://github.com/blastbay/verblib) — C library API shape comparison
- [Shirobon PS1 Reverb IRs](https://shirobon.bandcamp.com/album/ps1-reverb-impulse-responses) — community demand signal
- [Impact Soundworks GameVerb](https://impactsoundworks.com/product/gameverb/) — "Geek Mode" market signal
- [joerick/python-ctypes-package-sample](https://github.com/joerick/python-ctypes-package-sample) — ctypes + cibuildwheel pattern
- [Computer Associates v. Altai](https://en.wikipedia.org/wiki/Computer_Associates_Int%27l,_Inc._v._Altai,_Inc.) — clean-room legal precedent
- [numpy GH#28238](https://github.com/numpy/numpy/issues/28238) — ctypes/GC pitfall
- [Ross Bencina "real-time audio programming 101"](http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing) — RT-safety patterns
- [Mednafen ChangeLog](https://mednafen.github.io/documentation/ChangeLog.txt) — Nov 2020 SPU output precision fix

### Tertiary (LOW confidence — verify at implementation time)

- Exact pytest-regressions v3.0 feature set — verify against PyPI at implementation time
- scikit-build-core exact version — pin to latest stable when CI is configured
- Specific clang-tidy check list — iterate during Phase 1; starter in STACK.md is a recommendation

---

*Research completed: 2026-04-18*
*Ready for roadmap: yes*
