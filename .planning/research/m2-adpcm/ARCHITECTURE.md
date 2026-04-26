# Architecture Patterns — M2: Sony 4-bit ADPCM Encode/Decode

**Domain:** PS1 SPU ADPCM codec for libspu94
**Researched:** 2026-04-26

## Recommended Architecture

The ADPCM codec is a **peer module** alongside the existing reverb engine inside libspu94 — not a layer on top, not a plugin, not a separate library. Both modules share the same architectural discipline (zero-heap, caller-allocated state, integer-only, C11) but have independent state and independent processing functions.

### Component Boundaries

| Component | Responsibility | Communicates With |
|-----------|---------------|-------------------|
| `spu94_adpcm` (decode) | Decode 16-byte ADPCM blocks → 28 int16_t samples | Caller provides blocks; caller feeds decoded PCM to reverb if desired |
| `spu94_adpcm_encode` | Encode 28 int16_t samples → 16-byte ADPCM blocks | Caller provides PCM; receives ADPCM blocks |
| `spu94_adpcm_tables` | Static coefficient tables (f0, f1) | Read-only; shared by encode and decode |
| Reverb engine (existing) | Process PCM through reverb network | Receives PCM from any source, including ADPCM-decoded audio |
| CLI (existing + extended) | WAV/VAG file I/O, subcommand dispatch | Calls ADPCM encode/decode + dr_wav for WAV I/O |
| Python binding (existing + extended) | ctypes wrappers for ADPCM API | Calls C ADPCM functions via ctypes |

### Data Flow

**Decode path (VAG → WAV):**
```
VAG file → CLI reads header → CLI reads 16-byte blocks
  → spu94_adpcm_decode_block() per block → 28 int16_t samples
  → CLI accumulates samples → dr_wav writes WAV
```

**Encode path (WAV → VAG):**
```
WAV file → dr_wav reads PCM → CLI chunks into 28-sample frames
  → spu94_adpcm_encode_block() per frame → 16-byte block
  → CLI writes VAG header + accumulated blocks
```

**Integration with reverb (user's workflow, not a built-in pipeline):**
```
VAG → decode → PCM buffer → spu94_process() → reverbed PCM → WAV
```
The codec and reverb are called separately by the user. No internal coupling.

## Patterns to Follow

### Pattern 1: Block-at-a-Time API (matches hardware granularity)

**What:** Each API call processes exactly one 16-byte ADPCM block (decode) or 28 samples (encode). State carries across calls via a small caller-allocated struct.

**When:** Always. This is the only correct granularity for ADPCM.

**Why:** The PS1 SPU decodes one block at a time. The ADPCM state (old/older samples) crosses block boundaries. A block-at-a-time API lets the caller control streaming, looping, and block-level inspection without the codec imposing buffering policy.

**Example:**
```c
spu94_adpcm_state state = {0};  /* zero-init: old=0, older=0 */
int16_t samples[28];

for (size_t i = 0; i < num_blocks; i++) {
    uint8_t flags = spu94_adpcm_decode_block(&state, &adpcm_data[i * 16], samples);
    /* process 28 samples... */
    if (flags & 0x01) { /* loop end */ break; }
}
```

### Pattern 2: Stateless Tables, Stateful Processing

**What:** Filter coefficient tables are `static const` — shared, read-only, no state. Processing state is in a small struct that the caller allocates and passes in.

**Why:** Matches the existing libspu94 pattern (reverb state is caller-allocated via `spu94_state`). Enables multiple independent decode streams without any global state. Thread-safe by construction (no shared mutable state).

**Example:**
```c
/* Tables: file-scope static const, shared by all callers */
static const int16_t adpcm_f0[5] = {0, 60, 115, 98, 122};
static const int16_t adpcm_f1[5] = {0,  0, -52, -55, -60};

/* State: caller owns, passes in */
typedef struct {
    int16_t old;
    int16_t older;
} spu94_adpcm_state;
```

### Pattern 3: Encode Mirrors Decode Exactly

**What:** The encoder contains an embedded decoder. After encoding each nibble, the encoder decodes it using the exact same algorithm as the standalone decoder, and uses the decoded value (not the original PCM) as the prediction state for the next sample.

**Why:** This is critical for bit-faithfulness. If the encoder uses the original PCM for prediction but the decoder uses its own reconstructed values, the error accumulates differently. The encoder must "think like the decoder" to produce optimal nibbles.

**Example:**
```c
/* Inside encode_block, after choosing nibble for sample i: */
int32_t decoded = (nibble_signed << shift_amount)
               + ((old * f0 + older * f1 + 32) >> 6);
decoded = clamp_s16(decoded);
older = old;
old = (int16_t)decoded;  /* NOT original PCM */
```

### Pattern 4: VAG Header Handling Is CLI-Only

**What:** The VAG file format (48-byte header + ADPCM blocks) is parsed and produced only in the CLI and Python binding layers, never in the core library.

**Why:** The core ADPCM API is format-agnostic — it processes blocks and samples. File format concerns (endianness, header fields, version numbers) belong in the I/O layer. This keeps the core portable to MCU/FPGA where file I/O does not exist.

## Anti-Patterns to Avoid

### Anti-Pattern 1: Coupling ADPCM Decode Into the Reverb Tick

**What:** Making `spu94_process()` accept ADPCM data directly, or adding an "ADPCM mode" flag to the reverb engine.

**Why bad:** The reverb engine is a PCM-in/PCM-out signal processor. Coupling ADPCM decode into it means:
- Reverb API changes for a feature most callers don't need
- MCU callers who pre-decode ADPCM externally pay for unused code
- Testing the reverb now requires ADPCM test fixtures
- Violates the "each module has one job" principle

**Instead:** Keep them separate. Caller calls `spu94_adpcm_decode_block()` then feeds the result to `spu94_process()`.

### Anti-Pattern 2: Global Decode State

**What:** Storing `old`/`older` in a global or file-scope variable.

**Why bad:** Prevents multiple simultaneous decode streams (e.g., decoding left and right channels of a stereo pair from two VAG files). Also breaks thread safety.

**Instead:** Caller-allocated `spu94_adpcm_state` struct, passed by pointer.

### Anti-Pattern 3: Float Intermediate Values

**What:** Converting 4-bit nibbles to float, doing the prediction in float, then converting back to int16.

**Why bad:** Introduces rounding differences. The hardware is integer-only. Float introduces platform-dependent rounding that breaks bit-faithfulness.

**Instead:** All arithmetic in int32_t. Clamp result to int16_t. No float anywhere.

### Anti-Pattern 4: Over-Engineering the Encoder

**What:** Implementing psychoacoustic models, noise shaping, or multi-pass optimization for the ADPCM encoder.

**Why bad:** The goal is to produce ADPCM data that decodes correctly on real PS1 hardware (or in SPU-94's decoder). The Sony SDK encoder was a straightforward brute-force search. Fancy optimization produces different (possibly better-sounding) output that does not match what PS1 games actually contained.

**Instead:** Brute-force search over 65 (filter, shift) combinations per block. Simple, correct, fast enough.

## Scalability Considerations

Not applicable in the traditional sense — ADPCM blocks are 16 bytes / 28 samples with O(1) decode per block. There is no scaling concern. The brute-force encoder is O(65 * 28) = O(1820) operations per block regardless of input size.

The only relevant "scale" question is: how large a WAV file can the CLI encode/decode? Answer: any size, because the block-at-a-time API processes one block without buffering the entire file. Memory usage is constant (4 bytes of state + one block buffer).
