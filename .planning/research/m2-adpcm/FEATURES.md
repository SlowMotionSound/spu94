# Feature Landscape: Sony 4-bit ADPCM Encode/Decode for libspu94

**Domain:** PS1 SPU ADPCM codec added to an existing bit-faithful reverb library
**Researched:** 2026-04-26
**Overall confidence:** HIGH (decode algorithm well-documented across multiple authoritative sources; encode algorithm less formally specified but well-understood in practice)

---

## Context

The PS1 hardware signal path is: SPU RAM (4-bit ADPCM) -> ADPCM Decoder (per voice) -> Envelope/Volume/Pitch -> Mix Bus -> Reverb -> DAC. The reverb input on real hardware was always ADPCM-decoded audio. M2 adds the ADPCM encode/decode stage so users can hear what the reverb sounds like with ADPCM-colored input -- the authentic PS1 signal path.

libspu94's existing API takes int16 PCM stereo in, processes through the reverb network, and outputs int16 PCM stereo out. The ADPCM codec sits BEFORE the reverb: clean PCM -> ADPCM encode -> ADPCM decode -> reverb -> output. The encode/decode round-trip introduces the quantization noise and filter coloration that characterized every PS1 game's audio.

---

## Table Stakes

Features that are mandatory for bit-faithfulness. Missing any of these means the coloration is wrong.

| Feature | Why Expected | Complexity | Notes |
|---------|--------------|------------|-------|
| **Decode: 16-byte block parsing** | Every PS1 voice uses this format. Without correct block parsing, nothing works. | Low | Byte 0 = shift(bits 0-3) + filter(bits 4-6). Byte 1 = flags(bits 0-2). Bytes 2-15 = 28 packed nibbles (low nibble = 1st sample, high nibble = 2nd). |
| **Decode: All 5 filter coefficient pairs** | The SPU supports filters 0-4. XA-ADPCM uses only 0-3; the SPU adds filter 4. Omitting filter 4 = not SPU-faithful. | Low | See "Exact Filter Coefficients" section below. |
| **Decode: Shift parameter (0-12 valid range)** | Controls quantization granularity per block. | Low | 4-bit nibble sign-extended to 16 bits via `(int16_t)(nibble << 12)`, then arithmetic right-shifted by shift value. |
| **Decode: Shift 13-15 maps to shift 9** | Real hardware maps these reserved values to shift=9. No game intentionally uses them, but bit-faithfulness requires matching hardware. | Low | nocash psx-spx: "reserved shift values 13..15 will act same as shift=9." One-line clamp in the decoder. |
| **Decode: Filter index validation** | Filter field is 3 bits (0-7) but only 0-4 are valid. Values 5-7 need defined behavior. | Low | Gray area requiring ADR. See "Gray Areas" section. |
| **Decode: Predict + residual formula with rounding bias** | The core ADPCM math. Must use integer arithmetic with the +32 rounding bias before /64 division. | Medium | `result = shifted + (pos * old + neg * older + 32) / 64`. The +32 is NOT optional -- it changes output values. |
| **Decode: Clamp to int16 after prediction** | After filter prediction, result is clamped to [-32768, +32767]. | Low | Clamping happens AFTER the full prediction computation, not at intermediate steps. Intermediate values must be wider than 16 bits (int32 minimum). |
| **Decode: Two-sample state (old/older)** | Decoder carries two samples of state across block boundaries. | Low | `older = old; old = clamped_result` after each sample. First block starts with old=0, older=0. State persists across blocks within a stream. |
| **Decode: Flag byte parsing** | Flags are part of the block format. Ignoring them = incomplete parse. | Low | Bit extraction from byte 1. Expose to caller for metadata completeness. |
| **Encode: Per-block filter+shift selection** | The encoder must choose the optimal (filter, shift) pair for each 28-sample block. This is the "adaptive" in ADPCM. | High | Brute-force: try all 5 filters x 13 valid shifts = 65 combinations per block, pick lowest reconstruction error. |
| **Encode: 4-bit quantization with clamping** | Each residual must be quantized to 4 bits [-8, +7] and packed into nibbles. | Medium | Residuals exceeding [-8, +7] clamp to the boundary values. |
| **Encode: Internal decoder simulation** | Encoder must carry state using DECODED (reconstructed) values, not original PCM. | Medium | Critical: using original PCM for state instead of reconstructed samples causes encoder-decoder drift. The encoder must contain an internal copy of the decoder. |
| **Encode: Flag byte production** | Caller must be able to set loop points on encoded data. | Low | Caller passes flag byte per block. Encoder writes it to byte 1. |
| **Caller-allocated state (no heap)** | Matches libspu94's zero-heap architecture. | Low | 4 bytes of state (two int16_t for old/older). Reuse existing `sat_s16()`. |
| **Round-trip correctness test** | Encode -> decode must produce bitstream-identical output when decoded by any conforming decoder. | Medium | The correctness gate. Decode of our encode must match our decode sample-for-sample. |

---

### Exact Filter Coefficients

All sources agree on these values. **Confidence: HIGH.**

Cross-verified across: nocash psx-spx XA-ADPCM section (`pos_xa_adpcm_table` / `neg_xa_adpcm_table`), jsgroth's PS1 SPU blog series (filter formulas with integer constants), and FFmpeg's `AV_CODEC_ID_ADPCM_PSX` codec implementation.

| Filter | pos (K0 integer) | neg (K1 integer) | K0 as fraction (pos/64) | K1 as fraction (neg/64) | Character |
|--------|-------------------|-------------------|--------------------------|--------------------------|-----------|
| 0 | 0 | 0 | 0.0 | 0.0 | No prediction (raw residual only) |
| 1 | 60 | 0 | 0.9375 | 0.0 | 1st-order, gentle low-pass prediction |
| 2 | 115 | -52 | 1.796875 | -0.8125 | 2nd-order, aggressive |
| 3 | 98 | -55 | 1.53125 | -0.859375 | 2nd-order, moderate |
| 4 | 122 | -60 | 1.90625 | -0.9375 | 2nd-order, most aggressive (SPU-only) |

**C representation:**
```c
static const int16_t adpcm_pos[5] = {  0,  60, 115,  98, 122 };
static const int16_t adpcm_neg[5] = {  0,   0, -52, -55, -60 };
```

**SPU vs XA difference:** XA-ADPCM (CD audio subsystem) supports only filters 0-3. SPU-ADPCM supports 0-4. The fifth filter (index 4) is the most aggressive predictor -- its poles are closest to the unit circle, producing the most ringing on transients. SPU-94 must implement all 5 for SPU-faithfulness.

---

### Decode Algorithm: Complete Step-by-Step

For each 16-byte ADPCM block, producing 28 int16 samples:

```
Input:  block[16]          -- 16-byte ADPCM block
State:  old, older         -- int32, persisted across blocks (init to 0)
Output: samples[28]        -- decoded int16 PCM

1. Parse header:
   shift  = block[0] & 0x0F
   filter = (block[0] >> 4) & 0x07

2. Clamp shift:
   if (shift > 12) shift = 9     // hardware behavior for reserved values 13-15

3. Look up coefficients:
   pos = pos_table[filter]       // {0, 60, 115, 98, 122}
   neg = neg_table[filter]       // {0,  0, -52, -55, -60}
   // filter 5-7 behavior: gray area, see Gray Areas section

4. For i = 0 to 13 (14 data bytes, bytes 2..15):
   byte = block[2 + i]

   // First sample: low nibble (bits 0-3)
   nibble = byte & 0x0F
   if (nibble >= 8) nibble -= 16           // sign-extend 4-bit to signed
   shifted = nibble << (12 - shift)        // apply quantization scale
   predicted = shifted + (pos * old + neg * older + 32) / 64
   clamped = clamp(predicted, -32768, 32767)
   samples[i*2] = (int16_t)clamped
   older = old
   old = clamped

   // Second sample: high nibble (bits 4-7)
   nibble = (byte >> 4) & 0x0F
   if (nibble >= 8) nibble -= 16           // sign-extend
   shifted = nibble << (12 - shift)
   predicted = shifted + (pos * old + neg * older + 32) / 64
   clamped = clamp(predicted, -32768, 32767)
   samples[i*2 + 1] = (int16_t)clamped
   older = old
   old = clamped

5. Return 28 samples. State (old, older) persists for next block.
```

**Arithmetic width note:** The multiplication `pos * old` where pos=122 and old=32767 produces 3,997,574 -- well beyond int16 range. Intermediates MUST be computed in int32 (or wider). The clamp to int16 happens only at the final step.

**Alternative sign-extension formulation:** Some implementations express the shift as `(int16_t)(nibble << 12) >> shift` which is equivalent: left-shifting the 4-bit nibble into the sign position of int16, then arithmetic-right-shifting back. Both produce the same result when done in sufficiently wide integers.

---

### Block Flag Byte (Byte 1)

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | Loop End | Set ENDX flag, jump to loop address |
| 1 | Loop Repeat | If bit 0 also set: force release and zero ADSR level (mute). If bit 0 clear: ignored. |
| 2 | Loop Start | Copy current block address to repeat address register |
| 3-7 | Unused | Typically 0 |

**Practical flag combinations:**
- `0x00`: Normal -- continue to next block
- `0x01`: End + Mute -- stop playback, silence the voice
- `0x03`: End + Repeat -- jump to loop start, continue playing
- `0x04`: Mark loop start -- remember this address for future loop-back
- `0x07`: Mark loop start + End + Repeat (common for looping one-shot samples)

**For M2:** Parse and expose flags for completeness. Do NOT implement looping playback behavior. The decoder is a one-shot stream processor for coloration purposes. Looping is voice-engine behavior (out of scope per PROJECT.md).

---

## Differentiators

Features that go beyond bare correctness and provide real utility in the SPU-94 context.

| Feature | Value Proposition | Complexity | Notes |
|---------|-------------------|------------|-------|
| **Toggleable ADPCM coloration in signal path** | "How much PS1 do you want?" -- A/B clean PCM vs ADPCM-colored input to the reverb. | Low | Boolean flag in the signal path. When enabled, input PCM goes through encode->decode before reverb. When disabled, current M1 behavior. |
| **VAG file read** | Load actual PS1 game samples for testing. Standard PS1 audio interchange format. | Medium | 48-byte header ("VAGp" magic, version, sample rate, data size, name) + raw ADPCM blocks. |
| **VAG file write** | Produce PS1-compatible audio data from PCM. | Medium | Header construction + block encoding. |
| **Round-trip quality metrics** | SNR, max error, per-block filter/shift histogram. Lets user evaluate encoding quality. | Low | Computed during round-trip; numpy in Python. |
| **Batch encode/decode (WAV -> ADPCM stream -> WAV)** | Convenient wrapper over block-at-a-time API. | Low | Loop + tail padding logic. |
| **CLI subcommands** | `spu94 adpcm-encode`, `spu94 adpcm-decode`, `spu94 adpcm-roundtrip` | Medium | Extend existing CLI with subcommand dispatch. |
| **Python high-level API** | `spu94.adpcm_encode()`, `spu94.adpcm_decode()`, `spu94.vag_read()`, `spu94.vag_write()` | Low | Thin ctypes wrappers over C API. |
| **Golden-file tests for known ADPCM blocks** | Regression gate: hand-crafted test vectors with known decode output. | Low | ~5-10 blocks covering each filter, edge-case shifts, and boundary conditions. |
| **Per-block encoder diagnostics** | Expose which filter+shift the encoder chose for each block and the error metric. | Low | Educational/diagnostic. Encoder already computes this internally. |
| **ADPCM artifact analysis tooling (Python)** | Spectral comparison, quantization error visualization, filter selection heatmaps. | Medium | numpy/scipy/matplotlib already in toolchain. Fits the project's "understand every artifact" philosophy. |

---

## Anti-Features

Features to explicitly NOT build in the ADPCM milestone.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **Gaussian/cubic interpolation** | SPU voice pitch engine, a different subsystem. ADPCM decode produces raw int16 samples; Gaussian interpolation happens AFTER decode during pitch-shifted playback. Mixing them conflates two concerns. | Decode ADPCM at native sample rate. Output raw decoded int16. |
| **ADSR envelope** | Voice engine, not codec. Out of scope for entire project per PROJECT.md. | N/A |
| **Pitch modulation / sample rate conversion** | SPU uses pitch counters to advance through decoded samples at variable rates. Voice-engine territory. | ADPCM codec has no concept of playback rate. |
| **SPU RAM simulation** | Real SPU stores ADPCM in shared 512KB RAM with DMA. Simulating this memory model adds complexity for zero audio benefit. | ADPCM blocks decoded from linear buffer. |
| **Loop playback implementation** | Loop flags control voice engine playback position. Their EFFECT is voice behavior, not decode behavior. | Parse and expose flags. Do not implement looping. |
| **CD-XA ADPCM variant** | Different nibble ordering, different block size (2x28 nibbles in XA sectors), only 4 filters. Related but distinct format. | SPU-ADPCM only. |
| **Noise shaping / dithering in encoder** | Improves quality beyond what PS1 hardware ever produced. Goal is to match PS1 coloration, not exceed it. | Brute-force encoder matches Sony SDK quality. |
| **Multi-pass / lookahead encoding** | Tries N blocks ahead to minimize total error. Diminishing returns for coloration use case. Sony's SDK encoder was single-pass. | Single-pass brute-force per block. |
| **Real-time ADPCM decode in reverb hot path** | Reverb takes PCM input. Coupling ADPCM decode into the reverb tick adds complexity for zero benefit. | Separate preprocessing step. |
| **VAB container parsing** | VAB is a multi-sample instrument bank format. Parsing game asset containers is not SPU-94's purpose. | Out of scope. |
| **SIMD optimization** | 28 samples of integer math per block. SIMD overhead exceeds benefit at this scale. | Plain scalar C. |

---

## Feature Dependencies

```
sat_s16() [existing in libspu94] ──> ADPCM decode
sat_s16() [existing in libspu94] ──> ADPCM encode

ADPCM decode ──────────────────────> VAG file read
ADPCM encode ──────────────────────> VAG file write
ADPCM decode + encode ─────────────> Round-trip test infrastructure
ADPCM encode (contains internal decoder) ──depends-on──> ADPCM decode

VAG read/write ────────────────────> CLI subcommands
ADPCM decode/encode C API ─────────> Python ctypes bindings

Integration: spu94_set_adpcm_enabled() ──depends-on──> both encode + decode
                                       ──depends-on──> spu94_process() [existing]
```

**Critical dependency chain:** Decoder first -> Encoder (embeds decoder internally) -> Integration -> Tests.

The encoder MUST contain an internal copy of the decoder to simulate reconstruction. This is the single most common ADPCM encoder bug -- using original PCM for prediction state instead of reconstructed samples causes drift between encoder and decoder, producing increasingly wrong output over long streams.

**Integration contract:** ADPCM decode outputs int16 PCM, which feeds directly into `spu94_process()`'s existing int16 input. No format conversion needed.

---

## ADPCM Coloration Characteristics

Understanding WHY ADPCM colors the audio is essential for validating the implementation sounds right. This section documents the expected sonic artifacts.

### Quantization Noise Profile
- 4-bit encoding = only 16 possible residual values per sample (-8 to +7).
- With shift=0 (finest): quantization step = 1 LSB. With shift=12 (coarsest): step = 4096.
- The encoder's filter selection partially compensates: a good predictor means smaller residuals, allowing finer shift values and lower noise.
- Quiet passages suffer most. When signal amplitude is small relative to quantization step, the noise floor becomes audible as characteristic "grit" or "sandpaper" texture.
- The noise is signal-correlated (not white noise) -- it follows the signal shape, producing modulation noise rather than hiss. This is a defining characteristic of ADPCM vs dithered PCM.

### Filter Ringing on Transients
- Filters 2-4 have negative K1 coefficients, creating resonant 2nd-order IIR prediction.
- Filter 4 (K0=1.90625, K1=-0.9375) has poles very close to the unit circle -- audible ringing on percussive transients (drums, impacts, speech plosives).
- The ringing is part of the PS1 sound character. Most noticeable on isolated transients in quiet backgrounds.
- The encoder's filter selection per block means ringing characteristics change every 28 samples (~0.63ms at 44.1kHz), creating subtle timbral shifts.

### Block Boundary Effects
- Filter and shift can change at every block boundary (every 28 samples).
- Decoder state (old/older) carries across boundaries, but a new filter with very different characteristics can cause a small discontinuity in the predicted trajectory.
- At 44.1kHz, the block rate is ~1575 Hz. Filter switching artifacts may appear as low-level spectral content near this frequency.

### Cumulative Effect on Reverb
- ADPCM-decoded audio contains embedded quantization noise + filter coloration.
- This colored PCM feeds into the reverb network, where noise and ringing get recirculated through all-pass and comb filters.
- The reverb tail amplifies and smears ADPCM artifacts -- quantization noise becomes part of the reverb character, adding a grainy texture to decay tails.
- This is the authentic PS1 sound: reverb tails have a characteristic graininess that clean-PCM reverb lacks. M2 exists specifically to reproduce this coloration.

---

## Gray Areas Requiring ADR Documentation

Each needs investigation and a documented decision in DECISIONS.md, following the project's established gray-area philosophy (24 ADRs already shipped in M1).

| Gray Area | What's Unknown | Impact | Suggested Resolution |
|-----------|---------------|--------|---------------------|
| **Filter index 5-7 behavior** | 3-bit field allows 0-7, only 0-4 defined. Hardware behavior with 5, 6, 7 is undocumented. | Low (no game uses these), but bit-faithfulness demands an answer. | Witness-test against emulators (Mednafen, DuckStation). Most likely: filter coefficients treated as zero (same as filter 0) or wrapped modulo 5. Document in ADR. |
| **Division semantics: truncation vs ASR** | `(pos*old + neg*older + 32) / 64` -- C-style truncation-toward-zero or hardware ASR (toward negative infinity)? | Affects negative intermediate values. Edge cases only, but bit-faithful means matching them. | PS1 SPU is hardware DSP with barrel shifter; ASR (`>> 6`) is the likely implementation. The +32 bias partially compensates but doesn't eliminate the difference for all negative values. Verify via witness diff on signals producing negative filter outputs. Document in ADR. |
| **Sign extension path details** | Is nibble sign-extended as `(int16_t)(nibble << 12)` widened to int32, or directly to int32? | Mathematically equivalent for most values; potential edge case at nibble=0x8 with certain shift values due to intermediate int16 overflow. | Use `(int16_t)(nibble << 12)` then widen to int32, matching nocash's description. Verify with witness diff. |
| **Encoder error metric** | L1 (sum of absolute), L2 (sum of squared), or Linf (max absolute) for filter/shift selection? | Affects which filter+shift pair the encoder selects and therefore audio quality/coloration. | Use L2 (sum of squared errors). Standard ADPCM practice, weights large errors more heavily, matches what most PS1 tool encoders use. Document in ADR. |
| **State initialization between channels** | When encoding/decoding stereo as two independent mono streams, do L and R share ADPCM state? | Affects stereo ADPCM behavior. | Independent state per channel. Each PS1 voice has its own decoder state. L and R are separate voices on the hardware. |
| **Tail block handling** | If the input PCM length is not a multiple of 28 samples, how to handle the final partial block? | Encoder must produce complete 16-byte blocks. | Zero-pad the final block to 28 samples. Set end flag (0x01) on the last block. This matches Sony SDK conventions. |

---

## MVP Recommendation

**Build order (prioritized):**

1. **ADPCM Decoder** -- Fully specified, low ambiguity. Foundation for everything.
   - Block parsing (16 bytes -> 28 samples)
   - All 5 filter coefficient pairs
   - Shift 0-12 + shift 13-15 clamping to 9
   - int16 clamping after prediction
   - Two-sample state tracking across blocks
   - Unit tests against hand-computed reference values
   - Reuse existing `sat_s16()` from libspu94

2. **ADPCM Encoder** -- Brute-force single-pass (65 combinations per block).
   - Internal decoder simulation for state tracking
   - Per-block filter+shift selection (minimize L2 error)
   - 4-bit quantization with [-8, +7] clamping
   - Nibble packing (low nibble first, high nibble second)
   - Round-trip test: encode -> decode produces consistent output

3. **VAG file I/O** -- Read/write standard PS1 sample format.
   - 48-byte header parse/construct
   - Batch decode/encode wrapping block-level API
   - CLI subcommands for encode/decode/roundtrip

4. **Integration with libspu94** -- Wire encode+decode as optional pre-processing.
   - Boolean toggle: `spu94_set_adpcm_enabled(ctx, bool)`
   - Signal path: PCM in -> encode -> decode -> spu94_process() -> PCM out
   - Python binding exposure
   - CLI flag: `spu94 --adpcm --preset hall in.wav out.wav`

5. **Validation and analysis** -- Prove the coloration is correct.
   - Spectral comparison: reverb output with vs without ADPCM coloration
   - Golden-file tests for known ADPCM bitstreams
   - Document gray-area decisions in DECISIONS.md ADRs

**Defer:**
- Multi-pass encoding: Sony didn't do it
- Configurable coloration intensity (wet/dry blend): M4 plugin lever
- Per-block diagnostics exposure: nice to have, add if time permits
- Hypothesis property tests: valuable but not blocking

---

## Sources

- [nocash psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) -- block format, flag byte semantics, nibble ordering (HIGH confidence)
- [nocash psx-spx: CDROM XA Audio ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) -- filter coefficient tables (pos/neg), decode formula, shift 13-15 behavior, SPU-vs-XA filter count difference (HIGH confidence)
- [nocash psx-spx: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- SPU signal path context, pitch counter, Gaussian interpolation context (HIGH confidence)
- [jsgroth: PlayStation: The SPU, Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) -- decode algorithm walkthrough with worked examples, filter coefficient confirmation, shift 13-15 = shift 9, filter 4 confirmation (HIGH confidence, cross-verified with nocash)
- [jsgroth: SNES & PlayStation Cubic ADPCM Interpolation](https://jsgroth.dev/blog/posts/snes-ps1-cubic-adpcm-interpolation/) -- confirms Gaussian interpolation is AFTER ADPCM decode, not part of codec (HIGH confidence)
- [FFmpeg adpcm.c: AV_CODEC_ID_ADPCM_PSX](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c) -- reference decode implementation (MEDIUM confidence)
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc) -- PS1 ADPCM encoder reference tool, VAG format support (MEDIUM confidence, archived)
- [VAG format (Archive Team)](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation)) -- VAG file header structure (MEDIUM confidence)
- [SoundCy: PS1 Sound Downsampling](https://soundcy.com/article/how-much-were-sounds-downsampled-ps1) -- quantization noise characteristics (LOW confidence, secondary)

---

*Feature research for: PS1 SPU ADPCM encode/decode (M2 milestone, SPU-94)*
*Researched: 2026-04-26*
