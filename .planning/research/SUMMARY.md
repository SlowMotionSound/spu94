# Project Research Summary — M2: Sony 4-bit ADPCM Encode/Decode

**Project:** SPU-94
**Domain:** PS1 SPU ADPCM codec — bit-faithful 4-bit ADPCM encode/decode integrated with existing reverb library
**Researched:** 2026-04-26
**Confidence:** HIGH (decode algorithm), MEDIUM-HIGH (encoder strategy), MEDIUM (shift 13-15 edge case)

## Executive Summary

Sony's PS1 SPU uses a 4-bit ADPCM codec to compress voice audio before it reaches the reverb network. Every PS1 game's reverb character includes ADPCM decode artifacts — quantization noise, filter ringing, and block-boundary effects that give the reverb tail its characteristic grit. M2 adds this missing coloration stage to libspu94 so the standalone (and future plugin) can reproduce the authentic PS1 signal path: PCM → ADPCM encode → ADPCM decode → reverb.

The ADPCM algorithm is well-documented in nocash's psx-spx spec and cross-verified against three independent sources. The codec is small (~250 lines of C), needs zero new dependencies, and fits cleanly into the existing libspu94 architecture as a peer module. The decode algorithm uses 5 fixed filter coefficient pairs, 16-byte blocks encoding 28 samples each, and integer-only arithmetic with a round-to-nearest bias (`+32 >> 6`) that differs from the reverb core's truncation convention — both are correct for their respective hardware subsystems. The encoder is a brute-force search over 65 (filter, shift) combinations per block, trivially fast for offline or real-time use.

The primary risks are: licensing (the decode loop is ~15 lines of C, making structural similarity with GPL emulators hard to rebut — strict build-from-spec discipline is essential); the shift 13-15 edge case (no public hardware capture disambiguates PS1 behavior — default to nocash's "treat as shift=9" and flag for M5 hardware validation); and the rounding-vs-truncation discipline (ADPCM rounds, reverb truncates — both correct, but the difference must be documented in a new ADR).

## Key Findings

### Stack Additions

**Zero new C dependencies.** The ADPCM codec is pure integer arithmetic using `stdint.h` and the existing `sat_s16()` clamp helper. No floating point, no heap, no external libraries.

**One new test dependency:** `hypothesis` (Python, test-only) for property-based testing of encode/decode round-trips.

**New source files (estimated):**
- `include/spu94/spu94_adpcm.h` — public standalone API (~40 lines)
- `src/spu94/spu94_adpcm.c` — decode implementation (~80 lines)
- `src/spu94/spu94_adpcm_encode.c` — encode implementation (~120 lines)
- `src/spu94/spu94_adpcm_tables.c` — filter coefficient tables (~15 lines)
- CLI extensions, tests, Python bindings (~500 lines)

### Filter Coefficients (Verified HIGH confidence)

Cross-verified across nocash psx-spx, jsgroth's PS1 SPU blog, and FFmpeg's coefficient table:

| Filter | f0 (pos) | f1 (neg) | Character |
|--------|----------|----------|-----------|
| 0 | 0 | 0 | No prediction (raw residual) |
| 1 | 60 | 0 | 1st-order, gentle low-pass |
| 2 | 115 | -52 | 2nd-order, moderate resonance |
| 3 | 98 | -55 | 2nd-order, different resonance |
| 4 | 122 | -60 | 2nd-order, most aggressive (SPU-only) |

SPU-ADPCM supports all 5 filters; XA-ADPCM (CD subsystem) supports only 0-3. Filter 4 is part of the PS1 sound — its poles are closest to the unit circle, producing the most ringing on transients.

### Decode Algorithm (Complete)

For each 4-bit nibble in a 16-byte block (28 samples):
1. Sign-extend nibble from 4-bit to signed integer
2. Apply shift: `shifted = nibble << (12 - shift)` (shift clamped: 13-15 → 9)
3. Apply filter: `sample = shifted + ((old * f0 + older * f1 + 32) >> 6)`
4. Clamp to int16 range (single clamp point, AFTER full expression)
5. Update state: `older = old; old = clamped_sample`

**Critical arithmetic notes:**
- `+32 >> 6` is round-to-nearest via arithmetic right shift — NOT truncation, NOT `/64`
- Intermediates must be int32 (max intermediate ~6M, well within int32 range)
- Feedback state uses clamped values, not pre-clamp intermediates
- Low nibble decoded first within each data byte (bits 0-3 = even sample)

### Expected Features

**Table stakes (mandatory for bit-faithfulness):**
- Decode: 16-byte block parsing, all 5 filters, shift 0-12 + edge case 13-15, int16 clamping, two-sample state carry
- Encode: brute-force filter+shift selection (65 combinations), internal decoder simulation, 4-bit quantization
- Caller-allocated state (no heap), integer-only arithmetic
- Round-trip correctness: encode→decode is deterministic and bit-identical across implementations

**Differentiators (real utility):**
- Toggleable ADPCM coloration in the reverb signal path (A/B comparison)
- VAG file read/write (standard PS1 audio format)
- CLI subcommands (`spu94 adpcm-encode`, `adpcm-decode`, `adpcm-roundtrip`)
- Python ctypes bindings for analysis/exploration

**Anti-features (explicitly NOT building):**
- Gaussian interpolation (voice pitch engine, different subsystem)
- ADSR envelope, pitch modulation (voice engine, out of scope)
- XA-ADPCM variant (CD subsystem, different format)
- Noise shaping / multi-pass encoding (exceeds PS1 quality)
- SPU RAM simulation (zero audio benefit)

### Architecture Approach

ADPCM is a **peer module** inside libspu94 — not a separate library. The standalone encode/decode block functions are pure (no `spu94_state` dependency) and publicly exposed for CLI/Python/testing. The integration layer wires them into `chain_step_impl` in `spu94_io_chain.c`, before the FIR decimator — matching the PS1 hardware signal path where ADPCM-decoded audio feeds the reverb.

**Block/frame boundary:** Double-buffer approach. Input accumulates in one 28-sample buffer; output emits from the previous block's decoded samples. Fixed 28-sample latency (~0.635ms at 44.1kHz), zero jitter.

**State cost:** ~225 bytes added to `spu94_state` (double buffers + encoder/decoder state per channel). State grows from 560 to ~785 bytes, well within the 16384-byte ceiling.

**API surface:**
- `spu94_adpcm_decode_block()` / `spu94_adpcm_encode_block()` — standalone, public, pure
- `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()` — integration toggle
- `spu94_get_total_latency_samples()` — reports 58 (FIR) + 28 (ADPCM) when enabled

### Critical Pitfalls

1. **ASR vs division (`>>6` not `/64`)** — C integer division truncates toward zero; hardware uses arithmetic right shift (floor division). Differ for negative intermediates. Use `>>6`, validated by existing `_Static_assert`. Needs ADR.

2. **Encoder prediction state** — encoder MUST use decoded (reconstructed) values for prediction, not original PCM. Using original PCM causes encoder-decoder drift, producing increasingly wrong output. Encoder must contain an internal copy of the decoder.

3. **Nibble order** — low nibble first (bits 0-3 = even sample), high nibble second (bits 4-7 = odd sample). Getting this backwards swaps every pair.

4. **Clamping order** — single clamp after full expression, not premature clamping of intermediates. Feedback state uses clamped values.

5. **Licensing risk** — decode loop is ~15 lines of C. Build exclusively from nocash spec. Do NOT read GPL emulator source. Use audio-level witness comparison for gray areas, not source comparison.

6. **Shift 13-15** — biggest known emulator disagreement. Default to nocash "treat as shift=9". Flag for M5 hardware validation. Write dedicated test vectors.

## Gray Areas Requiring ADRs

| Gray Area | Recommended Resolution | Confidence |
|-----------|----------------------|------------|
| ADPCM rounding vs reverb truncation | Both correct — ADPCM rounds (`+32 >> 6`), reverb truncates. New ADR. | HIGH |
| Shift 13-15 behavior | Treat as shift=9 per nocash. M5 hardware verification target. | MEDIUM |
| Filter index 5-7 | Treat as (0, 0) or clamp to 4. Table with 8 entries, 5-7 zeroed. | MEDIUM |
| Division semantics | Use `>> 6` (ASR), consistent with ADR-0001 discipline. | HIGH |
| Encoder error metric | L2 (sum of squared errors), integer-only (`int64_t`). | HIGH |
| Tail block padding | Zero-pad final block to 28 samples. Set end flag. | HIGH |
| Mid-stream enable/disable | Discard partial accumulation buffer on disable. Silence gap is inaudible. | HIGH |

## Implications for Roadmap

### Phase 1: ADPCM Codec Core
**Rationale:** Decode is fully specified, low ambiguity. Foundation for everything else. Encode depends on decode (contains internal decoder copy).
**Delivers:** `spu94_adpcm_decode_block()`, `spu94_adpcm_encode_block()`, filter coefficient tables, known-vector tests, round-trip tests.
**Addresses:** Table-stakes decode + encode features.
**Avoids:** Pitfalls 1-6 (ASR, nibble order, sign extension, clamping order, feedback state) via targeted test vectors.

### Phase 2: Integration + Pipeline Wiring
**Rationale:** Depends on Phase 1. Wires ADPCM into existing `spu94_process` signal path.
**Delivers:** `spu94_set_adpcm_enabled()`, double-buffer block/frame alignment, latency reporting, `spu94_reset` zeroing of ADPCM state, integration tests (ADPCM on vs off).
**Addresses:** Toggleable coloration, correct signal-path ordering.
**Avoids:** Integration pitfalls (RT-safety regression, state contamination).

### Phase 3: CLI + Python Binding + VAG I/O
**Rationale:** Depends on Phase 2. Extends existing CLI and binding with ADPCM capabilities.
**Delivers:** `spu94 adpcm-encode/decode/roundtrip` subcommands, VAG file read/write, Python `spu94.adpcm_encode/decode()`, `--adpcm` flag for reverb processing.
**Addresses:** VAG I/O, CLI workflow, Python analysis.
**Avoids:** VAG endianness pitfall, ctypes boundary issues.

### Phase 4: Validation + Golden Files
**Rationale:** Depends on Phases 1-3. Proves correctness, locks regression gates.
**Delivers:** ADPCM golden files, spectral comparison (with vs without coloration), witness diff against emulator output, Hypothesis property tests, DECISIONS.md ADR entries.
**Addresses:** All gray areas documented, bit-accuracy claim substantiated.
**Avoids:** Undocumented gray areas, regression without detection.

### Phase Ordering Rationale

- Decode before encode (encoder embeds decoder)
- Standalone codec before integration (testable in isolation)
- Integration before CLI/binding (internal API must stabilize first)
- Validation last (needs all features present to test comprehensively)

### Research Flags

- **Phase 1:** Standard implementation from spec — no additional research needed.
- **Phase 2:** Block/frame boundary design is well-understood (double-buffer). No research needed.
- **Phase 3:** VAG format is documented. No research needed.
- **Phase 4:** Gray areas (shift 13-15, filter 5-7) may need witness testing against emulator output during this phase.

## Confidence Assessment

| Area | Confidence | Notes |
|------|------------|-------|
| Stack | HIGH | Zero new deps; well-understood algorithm |
| Features | HIGH | Well-bounded scope; clear table stakes |
| Architecture | HIGH | Peer-module pattern proven by existing reverb engine |
| Pitfalls | HIGH | All pitfalls are well-known ADPCM implementation traps |
| Filter coefficients | HIGH | Verified across 3 independent sources |
| Decode algorithm | HIGH | nocash + jsgroth + FFmpeg consistent |
| Encode algorithm | MEDIUM-HIGH | Standard brute-force; no official Sony spec |
| Shift 13-15 | MEDIUM | nocash says shift=9; no hardware capture |

**Overall confidence:** HIGH

### Gaps to Address

- **Shift 13-15 hardware behavior:** Default to shift=9, flag for M5 hardware validation.
- **Filter 5-7 hardware behavior:** Default to (0,0), flag for M5.
- **Sony SDK encoder output:** No reference to compare against. Round-trip determinism is the testable property.

## Sources

### Primary (HIGH confidence)
- [psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) — block format, flags
- [psx-spx: XA-ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — filter coefficients, decode formula
- [psx-spx: SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/) — signal path, architecture

### Secondary (MEDIUM-HIGH confidence)
- [jsgroth: PS1 SPU Part 1 — ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — implementation details, shift edge cases, all 5 coefficients confirmed
- [jsgroth: PS1 SPU Part 4](https://jsgroth.dev/blog/posts/ps1-spu-part-4/) — loop quirks, IRQ interaction

### Tertiary (MEDIUM confidence)
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc) — reference encoder approach
- [FFmpeg adpcm.c](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c) — coefficient cross-reference only
- [VAG format (Archive Team)](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation)) — file header structure

---
*Research completed: 2026-04-26*
*Ready for requirements: yes*
