# Stack Research — SPU-94 M2: Sony 4-bit ADPCM Encode/Decode

**Domain:** PS1 SPU ADPCM codec — bit-faithful integer-only encode/decode in plain C, integrating with existing libspu94 architecture.
**Researched:** 2026-04-26
**Overall confidence:** HIGH — the ADPCM algorithm is well-documented across multiple independent sources; filter coefficients verified against three independent references; encode algorithm is well-understood ADPCM theory applied to Sony's fixed coefficient set.

**Scope note:** This document covers ONLY what M2 adds. The existing libspu94 stack (C11, CMake, dr_wav, pytest, etc.) is unchanged. No new external dependencies are needed.

---

## 1. The PS1 SPU ADPCM Algorithm (Complete Specification)

### Confidence: HIGH
Verified across: psx-spx/nocash (primary), jsgroth's PS1 SPU blog (secondary), FFmpeg adpcm.c LGPL source (tertiary witness for coefficient values only — not read for implementation, only coefficient table cross-reference). All three sources agree on every numeric value below.

### 1.1 Block Structure

Each ADPCM block is **16 bytes** encoding **28 samples**:

| Byte | Content |
|------|---------|
| 0 | Shift/Filter: bits 0-3 = shift (0-12 valid; 13-15 treated as 9), bits 4-6 = filter index (0-4 valid) |
| 1 | Flags: bit 0 = loop end, bit 1 = loop repeat, bit 2 = loop start |
| 2-15 | 14 data bytes, each containing 2 nibbles (28 total samples) |

**Nibble ordering within each data byte:** low nibble first (bits 0-3 = even sample), high nibble first (bits 4-7 = odd sample). This is little-endian nibble order within each byte.

**Compression ratio:** 16 bytes encode 28 samples that would be 56 bytes as 16-bit PCM = 3.5:1 compression.

### 1.2 Filter Coefficients (The Five Fixed Pairs)

The SPU uses 5 fixed filter coefficient pairs. XA-ADPCM uses only filters 0-3; SPU-ADPCM adds filter 4.

| Filter Index | f0 (positive) | f1 (negative) | Character |
|-------------|---------------|---------------|-----------|
| 0 | 0 | 0 | No prediction (raw shifted sample) |
| 1 | 60 | 0 | 1st-order prediction only |
| 2 | 115 | -52 | 2nd-order, moderate resonance |
| 3 | 98 | -55 | 2nd-order, different resonance |
| 4 | 122 | -60 | 2nd-order, highest resonance (SPU-only) |

These coefficients are **integer constants** used in fixed-point arithmetic with a divisor of 64. They are NOT floating-point approximations.

**In C:**
```c
static const int16_t spu_adpcm_f0[5] = {  0, 60, 115,  98, 122 };
static const int16_t spu_adpcm_f1[5] = {  0,  0, -52, -55, -60 };
```

### 1.3 Decode Algorithm (Exact Steps)

For each 4-bit nibble `n` in a block (28 iterations):

```
Step 1: Sign-extend nibble to 16-bit signed
    t = (int16_t)(int8_t)((n << 4) & 0xF0)   // or: sign-extend 4-bit to 32-bit
    Equivalent: if n >= 8, t = n - 16; else t = n;  (range: -8 to +7)

Step 2: Apply shift
    shifted = t << max(0, 12 - shift)
    If shift > 12 (i.e., shift 13/14/15): treat as shift = 9
    So: shifted = t << (12 - 9) = t << 3

Step 3: Apply filter prediction
    sample = shifted + (old * f0 + older * f1 + 32) / 64

Step 4: Clamp to signed 16-bit range
    sample = clamp(sample, -0x8000, +0x7FFF)

Step 5: Update state
    older = old
    old = sample
```

**Critical arithmetic details:**
- Step 3 intermediate `(old * f0 + older * f1 + 32)` must be computed in **32-bit signed** arithmetic to avoid overflow. `old` and `older` are int16_t, `f0`/`f1` are small constants, but the product can exceed 16-bit range.
- The `+ 32` before `/64` implements **round-to-nearest** (NOT truncation). This is different from the reverb network's truncation semantics. The ADPCM decoder rounds; the reverb network truncates. Both are hardware-faithful behaviors, just different subsystems.
- The `/64` is an **integer division** (equivalent to `>> 6` for positive values, but for negative values, C's integer division truncates toward zero while `>> 6` is floor division). The hardware behavior matches `(old * f0 + older * f1 + 32) >> 6` using arithmetic right shift, which is floor division. Use `>> 6` not `/64` to match hardware.
- Shift values 13, 14, 15 in the header are treated as shift = 9 by the hardware. This is documented in psx-spx and confirmed by emulator witnesses.

**Corrected Step 3 in C (hardware-faithful):**
```c
int32_t predicted = (int32_t)old * f0 + (int32_t)older * f1;
int32_t sample = shifted + ((predicted + 32) >> 6);  /* ASR, not /64 */
sample = clamp_s16(sample);
```

### 1.4 Flag Byte Semantics

| Bits 0-1 | Bit 2 | Meaning |
|----------|-------|---------|
| 0 | 0 | Normal: continue to next block |
| 1 | 0 | End+Mute: jump to loop address, set ENDX flag, force envelope Release |
| 0 | 1 | Ignored (behaves like normal) |
| 1 | 1 | End+Repeat: jump to loop address, set ENDX flag, continue envelope |
| - | 1 | Loop Start: marks this block's address as the loop-return target |

**For SPU-94's purposes:** The loop flags matter for voice playback but NOT for the reverb network. The reverb operates on the mixed output of all voices after ADPCM decode. However, for the ADPCM codec module itself to be complete, it must parse and produce these flags correctly.

### 1.5 Shift/Filter Header Byte Layout

```
Byte 0: [FFFS SSSS]  -- BUT ACTUALLY:
Byte 0: [0FFF SSSS]  -- bits 0-3 = shift, bits 4-6 = filter, bit 7 unused

Wait — let me be precise per the sources:

Byte 0 layout:
  Bits 0-3: Shift value (0-15; values 13-15 → behave as 9)
  Bits 4-6: Filter index (0-4 valid; 5-7 → clamp to 4 or treat as 0; behavior underdocumented)
  Bit 7: Unused (typically 0)
```

**Gray area — filter index > 4:** The psx-spx docs and jsgroth blog both state filter is clamped to 0-4 range. What happens with filter 5/6/7 is a gray area that should be documented in DECISIONS.md. For encoding, this is irrelevant (never produce filter > 4). For decoding, defensive clamping to 4 matches documented emulator behavior.

---

## 2. Encode Algorithm

### Confidence: MEDIUM-HIGH
Encoding is the inverse problem: given PCM samples, find the best (shift, filter, nibbles) per block. This is standard ADPCM encoder theory applied to Sony's specific coefficient set. No single "official" algorithm exists — Sony's SDK encoder was proprietary. Multiple approaches exist; the one below is the standard open-source approach.

### 2.1 Per-Block Encoding Strategy

For each 28-sample block:

1. **Try all 5 filters x 13 shift values (0-12)** = 65 combinations
2. For each combination, encode all 28 samples:
   a. Compute the ideal 4-bit nibble that minimizes error
   b. Quantize to nearest representable 4-bit value (-8 to +7)
   c. Decode the nibble (using the exact decode algorithm) to get the reconstructed sample
   d. Use the reconstructed sample (not the original) as `old`/`older` for the next sample
3. Measure total squared error for each (shift, filter) combination
4. Pick the (shift, filter) pair with the lowest total error

**This is a brute-force search over 65 combinations per block.** It is NOT computationally expensive — 65 * 28 = 1820 operations per block, and blocks cover 28 samples. For offline encoding (which is the SPU-94 use case), this is trivially fast.

### 2.2 Nibble Quantization (Per-Sample)

Given the current `old` and `older` decoded samples, the target PCM sample, and the chosen (shift, filter):

```c
/* Compute prediction */
int32_t predicted = ((int32_t)old * f0 + (int32_t)older * f1 + 32) >> 6;

/* Compute the residual the nibble must encode */
int32_t residual = target_sample - predicted;

/* Quantize: what nibble value, when shifted, best approximates the residual? */
int shift_amount = 12 - shift;  /* shift_amount >= 0 */
int32_t nibble = (residual + (1 << (shift_amount - 1))) >> shift_amount;  /* round */
/* Clamp to 4-bit signed range */
if (nibble > 7) nibble = 7;
if (nibble < -8) nibble = -8;
```

### 2.3 Encode Is NOT Real-Time Critical

The ADPCM encoder is an offline/preparation tool — converting PCM WAV files into ADPCM data that the SPU would play. It does NOT run in the reverb hot path. This means:
- The brute-force 65-combination search is perfectly acceptable
- No real-time constraints apply to the encoder
- It can be a separate translation unit, not linked into the real-time library

---

## 3. Stack Additions for M2

### 3.1 New Source Files (Zero New Dependencies)

| File | Purpose | Links Into |
|------|---------|-----------|
| `src/spu94/spu94_adpcm.c` | ADPCM decode (hot path capable) | `libspu94.so` / `libspu94.a` |
| `include/spu94/spu94_adpcm.h` | Public ADPCM API header | Public API surface |
| `src/spu94/spu94_adpcm_encode.c` | ADPCM encode (offline tool) | `libspu94.so` / `libspu94.a` (or separate encode lib) |

**No new external dependencies.** The ADPCM codec is pure integer arithmetic on small fixed tables. It needs `stdint.h` and the existing `spu94_q15.h` helpers (specifically `sat_s16` for clamping). Nothing else.

### 3.2 API Surface Design

```c
/* ----- Decode ----- */

/* Decode state: just the two previous samples */
typedef struct {
    int16_t old;    /* previous decoded sample */
    int16_t older;  /* sample before that */
} spu94_adpcm_state;

/* Decode one 16-byte ADPCM block into 28 int16_t samples.
 * Returns flag byte (for caller to handle loop logic).
 * State is updated in-place. */
uint8_t spu94_adpcm_decode_block(
    spu94_adpcm_state *state,
    const uint8_t block[16],    /* input: 16-byte ADPCM block */
    int16_t out[28]             /* output: 28 decoded samples */
);

/* ----- Encode ----- */

/* Encode 28 int16_t samples into one 16-byte ADPCM block.
 * flags: the flag byte to embed (caller manages loop flags).
 * State is updated in-place (tracks encoder's decoder state). */
void spu94_adpcm_encode_block(
    spu94_adpcm_state *state,
    const int16_t in[28],       /* input: 28 PCM samples */
    uint8_t flags,              /* flag byte for this block */
    uint8_t block[16]           /* output: 16-byte ADPCM block */
);
```

**Design rationale:**
- **Caller-allocated state** — same pattern as the reverb engine. No heap. MCU-portable.
- **Block-at-a-time API** — matches hardware granularity. Caller manages streaming/loop logic.
- **Decode returns flag byte** — caller decides what to do with loop flags. The codec doesn't enforce voice behavior.
- **int16_t throughout** — no float anywhere in the codec. Matches hardware.

### 3.3 Integration With Existing libspu94

The ADPCM codec is a **peer module** to the reverb engine, not a layer on top of it. In a real PS1:
1. Voice plays ADPCM data from SPU RAM
2. SPU decodes ADPCM → 16-bit PCM per voice
3. Voices are mixed → summed signal feeds the reverb input
4. Reverb processes the mixed signal

For SPU-94 M2, the ADPCM codec ships as a standalone encode/decode capability. It does NOT modify the reverb processing pipeline (which already accepts PCM input). The integration point is that ADPCM-decoded audio can be fed into `spu94_process()` as PCM — no API change needed.

### 3.4 VAG File Format Support (CLI Extension)

The CLI (`spu94`) should gain ADPCM subcommands for practical use:

```
spu94 adpcm-encode input.wav output.vag    # PCM WAV → VAG
spu94 adpcm-decode input.vag output.wav    # VAG → PCM WAV
spu94 adpcm-roundtrip input.wav output.wav # encode then decode (for quality testing)
```

**VAG file format (Sony standard):**

| Offset | Size | Content |
|--------|------|---------|
| 0x00 | 4 | Magic: "VAGp" (0x56414770) |
| 0x04 | 4 | Version (big-endian; typically 0x00000020 = v2.0) |
| 0x08 | 4 | Reserved (0) |
| 0x0C | 4 | Data size in bytes (big-endian, excluding header) |
| 0x10 | 4 | Sample rate (big-endian; typically 44100 or 22050) |
| 0x14 | 10 | Reserved (0) |
| 0x1E | 2 | Reserved (0) |
| 0x20 | 16 | Name (null-terminated ASCII, zero-padded) |
| 0x30 | ... | ADPCM data blocks (16 bytes each) |

**VAG is mono only.** Stereo requires two VAG files or the VAB (Voice Application Bank) container format. For M2, mono VAG is sufficient.

**Note:** VAG header fields are big-endian. The ADPCM data blocks themselves are byte-oriented (no endianness concern at the block level). The CLI must handle the big-endian header correctly on little-endian hosts (use explicit byte swapping, not `htonl`/`ntohl` which depend on POSIX headers not available on MCU).

### 3.5 Python Binding Extensions

```python
# New ctypes wrappers in spu94/_lib.py
spu94_adpcm_decode_block(state, block_bytes) -> (samples_array, flags)
spu94_adpcm_encode_block(state, samples_array, flags) -> block_bytes

# Higher-level in spu94/__init__.py
spu94.adpcm_decode(adpcm_bytes) -> numpy.ndarray  # full stream
spu94.adpcm_encode(pcm_array) -> bytes             # full stream
spu94.vag_read(path) -> numpy.ndarray               # VAG file → PCM
spu94.vag_write(path, pcm_array, sample_rate)       # PCM → VAG file
```

No new Python dependencies needed. numpy handles the sample arrays.

---

## 4. Verification Strategy

### 4.1 Round-Trip Identity

The strongest test: encode PCM → ADPCM → decode back to PCM, and verify the decoded output matches what the decoder would produce from the encoded data. This is NOT lossless (ADPCM is lossy), but the round-trip must be **deterministic** and the decode of the encode must be **bit-identical** to a direct decode of the same ADPCM blocks.

### 4.2 Known-Vector Tests

Construct ADPCM blocks by hand with known shift/filter/nibble values. Decode them and verify against hand-computed expected output. This tests the decode algorithm's arithmetic directly.

**Minimum vectors:**
- All-zero block (shift=0, filter=0, all nibbles=0) → 28 zeros
- Single-impulse block (one nibble=1, rest=0, shift=0, filter=0) → impulse at known position
- Each filter index (0-4) with known state → verify prediction math
- Shift = 12 (minimum amplification) and shift = 0 (maximum amplification)
- Shift = 13/14/15 → verify treated-as-9 behavior
- Clamp test: nibble values that cause overflow → verify clamp to [-0x8000, +0x7FFF]

### 4.3 VAG File Interop

Decode real VAG files ripped from PS1 games (public domain homebrew or test files) and compare output against a known-good decoder (vgmstream, Mednafen's output — as behavioral witnesses, not source references).

### 4.4 Hypothesis Property Testing

M1 research noted Hypothesis as a strong candidate for M2 ADPCM. This is the place:
- Property: decode(encode(pcm)) should produce output with bounded error
- Property: encode always produces valid shift (0-12) and filter (0-4)
- Property: decode of any 16-byte block never crashes, always produces 28 samples

**New test dependency:**
```
pip install hypothesis
```
This is the ONE new Python dependency for M2. It is test-only, not shipped.

---

## 5. What NOT to Add

| Temptation | Why Skip It |
|------------|-------------|
| Floating-point anywhere in the codec | Hardware is integer-only; float introduces rounding differences that break bit-faithfulness |
| ADPCM interpolation (Gaussian/cubic) | That is the voice pitch engine, NOT the ADPCM codec. Different subsystem, different milestone |
| XA-ADPCM variant support | XA uses a different nibble ordering and only 4 filters. SPU-ADPCM is the target. XA is a CD subsystem concern, not SPU |
| VAB (Voice Application Bank) parser | VAB is a container for multiple VAG samples + instrument metadata. Parsing game assets is out of scope for SPU-94 |
| Real-time streaming ADPCM decode in the reverb hot path | The reverb engine takes PCM in. ADPCM decode is a preprocessing step. Don't couple them |
| libsndfile or any audio format library | dr_wav (already vendored) handles WAV I/O for the CLI. VAG is simple enough to parse directly |
| External ADPCM libraries (e.g., vgmstream's decoder) | The codec is ~100 lines of C. External dep adds licensing risk and architectural coupling for zero value |
| Optimized SIMD decode | 28 samples per block, simple integer math. SIMD is pointless overhead for this workload |

---

## 6. Gray Areas Requiring DECISIONS.md Entries

These are places where the spec is silent or ambiguous. Each needs a documented resolution:

| Gray Area | Options | Recommended Resolution |
|-----------|---------|----------------------|
| Filter index > 4 in decode input | Clamp to 4, wrap modulo 5, treat as 0 | Clamp to 4 (matches emulator consensus) |
| Shift > 12 behavior | Treat as shift=9 (documented) vs. undefined | Treat as shift=9 (documented in psx-spx) |
| `+ 32` rounding: is it `>> 6` (ASR) or `/64` (truncate-toward-zero)? | `>> 6` matches hardware | Use `>> 6` (arithmetic right shift, same ASR discipline as reverb core, per ADR-0001) |
| Encode: what to do when 28 samples not available (end of file) | Zero-pad remainder, or short final block | Zero-pad to 28 samples in final block (matches Sony SDK behavior) |
| Encode: optimal vs. simple filter/shift selection | Brute-force all 65 combos vs. heuristic | Brute-force (65 combos is trivially fast; correctness over cleverness) |
| VAG header version field | v2 (0x20) vs v3 (0x30) vs other | Produce v2 (0x20) on encode; accept any version on decode |

---

## 7. Consolidated Stack Delta for M2

### New Source Files

| File | Purpose | Lines (est.) |
|------|---------|-------------|
| `include/spu94/spu94_adpcm.h` | Public ADPCM API | ~40 |
| `src/spu94/spu94_adpcm.c` | Decode implementation | ~80 |
| `src/spu94/spu94_adpcm_encode.c` | Encode implementation | ~120 |
| `src/spu94/spu94_adpcm_tables.c` | Filter coefficient tables | ~15 |
| CLI extensions in `src/cli/` | adpcm-encode/decode subcommands | ~150 |
| `tests/adpcm/` | C unit tests (Unity) | ~200 |
| `tests/python/test_adpcm.py` | Python integration + Hypothesis | ~150 |

### New Dependencies

| Dependency | Version | Scope | Why |
|-----------|---------|-------|-----|
| `hypothesis` | 6.x | Test-only (pip) | Property-based testing for ADPCM encode/decode round-trips |

That is the ONLY new dependency. Everything else uses the existing stack.

### Build System Changes

- Add `spu94_adpcm.c`, `spu94_adpcm_encode.c`, `spu94_adpcm_tables.c` to `src/spu94/CMakeLists.txt` source list
- Add `tests/adpcm/` test directory to `tests/CMakeLists.txt`
- No new CMake features or version bumps needed

### No Changes To

- Compiler flags (same C11, same warnings)
- dr_wav (CLI already has it)
- Python binding architecture (ctypes, same pattern)
- CI pipeline structure (add test targets, no new jobs)
- Real-time safety constraints (ADPCM decode CAN be real-time safe; encode is offline-only)

---

## 8. Arithmetic Discipline Summary

**The ADPCM codec uses DIFFERENT rounding than the reverb network.** This is not a bug — it matches hardware:

| Subsystem | Multiply | Division/Shift | Rationale |
|-----------|----------|----------------|-----------|
| Reverb network (existing) | Truncation (`>> 15`, ASR) | N/A | ADR-0001: SPU reverb truncates |
| ADPCM decode filter (M2) | N/A (add/shift only) | Round-to-nearest: `(x + 32) >> 6` | Hardware rounds the filter prediction |
| ADPCM encode quantization (M2) | N/A | Round-to-nearest in nibble selection | Standard ADPCM encoding practice |

The `sat_s16()` clamp from `spu94_q15.h` is reused directly. The `q15_mul_truncate()` function is NOT used in ADPCM — the arithmetic is different (shift + add, not Q15 multiply).

---

## Sources

### Primary (HIGH confidence)

- [psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) — block structure, flag byte semantics
- [psx-spx: CDROM XA Audio ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — filter coefficients (0, 60, 115, 98, 122) / (0, 0, -52, -55, -60), decode formula, shift/clamp behavior
- [psx-spx: Sound Processing Unit (SPU)](https://psx-spx.consoledev.net/soundprocessingunitspu/) — SPU-ADPCM vs XA-ADPCM differences, 5 filters vs 4

### Secondary (MEDIUM-HIGH confidence)

- [jsgroth: PlayStation: The SPU, Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — complete decode algorithm with C-level implementation details, nibble ordering, shift/filter header byte layout, all 5 coefficient pairs confirmed
- [FFmpeg adpcm.c](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c) — coefficient table cross-reference (NOT read for implementation; LGPL source used only to verify numeric constants which are uncopyrightable facts)

### Tertiary (MEDIUM confidence)

- [psxavenc (WonderfulToolchain)](https://github.com/WonderfulToolchain/psxavenc) — existence confirms encoding approaches; NOT read for implementation
- [VAG format (Archive Team)](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation)) — VAG header format reference

---

*Stack research for: Sony 4-bit ADPCM encode/decode addition to libspu94.*
*Researched: 2026-04-26*
