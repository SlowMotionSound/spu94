# Architecture Research: ADPCM Integration into libspu94

**Domain:** Sony 4-bit ADPCM encode/decode as an optional signal-coloring stage in libspu94
**Researched:** 2026-04-26
**Confidence:** HIGH on ADPCM algorithm (nocash spec + psxavenc reference encoder). HIGH on integration pattern (follows established libspu94 conventions). MEDIUM on encoder filter-selection strategy (multiple approaches exist; MSE-brute-force is the pragmatic default).

---

## 1. The ADPCM Algorithm

### 1.1 Block Structure

Each SPU-ADPCM block is 16 bytes encoding 28 PCM samples:

| Byte | Content |
|------|---------|
| 0 | Shift (bits 0-3) + Filter (bits 4-6) |
| 1 | Loop flags (bits 0-2) -- irrelevant for SPU-94's coloration use case |
| 2-15 | 14 bytes of packed 4-bit nibbles (LSB = 1st sample, MSB = 2nd) |

### 1.2 Filter Coefficients

Five filters (SPU-ADPCM uses all 5; XA-ADPCM uses only 0-3):

| Filter | pos (f0) | neg (f1) |
|--------|----------|----------|
| 0 | 0 | 0 |
| 1 | +60 | 0 |
| 2 | +115 | -52 |
| 3 | +98 | -55 |
| 4 | +122 | -60 |

### 1.3 Decode Formula (per sample within a block)

```
nibble = sign_extend_4bit(raw_nibble)
shifted = nibble << max(0, 12 - shift)
sample = shifted + (old * f0 + older * f1 + 32) / 64
sample = clamp(sample, -32768, +32767)
older = old
old = sample
```

State carried between blocks: two int16 values (`old`, `older`). That is the entire decoder state.

### 1.4 Encode Algorithm (per block of 28 samples)

For each of the 5 filters:
1. Compute the minimum shift that prevents overflow for this block
2. Try shift-1, shift, shift+1 (optimal can be off by 1)
3. For each candidate (filter, shift): quantize all 28 residuals to 4-bit, reconstruct decoded output, compute MSE against input
4. Select the (filter, shift) pair with lowest MSE

State carried between blocks: same two int16 values (`old`, `older`), representing the last two *decoded* (not input) samples from the previous block.

### 1.5 Gray Areas for SPU-94

- **Shift values 13-15:** nocash is ambiguous; jsgroth's blog reports these default to shift=9 on real hardware. Needs an ADR if encountered during encoding.
- **Filter 4 on XA vs SPU:** XA uses only filters 0-3; SPU supports 0-4. SPU-94's encoder should use all 5.
- **Rounding in filter application:** The `+32` before `/64` is a rounding bias. This must be integer division (truncation toward zero), not C's default signed division. Needs explicit implementation per the Q15 truncation discipline already in libspu94.

---

## 2. Integration Decision: INSIDE libspu94, Not a Separate Library

**Recommendation: ADPCM lives inside libspu94 as new source files, not as a separate `libspu94_adpcm` library.**

Rationale:

1. **Signal flow coupling.** ADPCM encode+decode is an inline processing stage UPSTREAM of the reverb. On real PS1 hardware: ADPCM decode -> envelope -> mix -> reverb. SPU-94 simplifies to: PCM input -> ADPCM encode -> ADPCM decode -> existing reverb chain. This is a pipeline stage, not a separate utility.

2. **Shared state lifetime.** ADPCM state (2 previous samples per channel for the decoder, 2 per channel for the encoder) must persist across `spu94_process()` calls with the same lifetime as the reverb state. Embedding it in `spu94_state` is natural; a separate library would need its own state handle and force callers to coordinate two lifetimes.

3. **Zero-heap discipline.** A separate library means a separate caller-allocated buffer, separate init/destroy, separate size macros. This doubles the ceremony for callers. Embedding ADPCM state inside `spu94_state` keeps the existing single-allocation pattern.

4. **Enable/disable via a flag, not a link-time decision.** ADPCM should be an opt-in flag on the existing state, not a build-time dependency choice. The JUCE plugin will expose "ADPCM coloration on/off" as a UI toggle.

5. **Current headroom is massive.** `spu94_state` is 560 bytes; `SPU94_STATE_SIZE_MAX` is 16384 bytes. ADPCM adds ~16 bytes of state (4 x int16 for encode old/older L/R + 4 x int16 for decode old/older L/R + enable flag + padding). No need to bump the size macro.

**What stays separate:** Standalone encode/decode utility functions (for offline WAV-to-ADPCM conversion, testing, etc.) should be usable independently. This means the core ADPCM encode_block/decode_block functions are pure (take state + samples in, produce samples/bytes out) with NO dependency on `spu94_state`. The integration layer inside `spu94_process` calls these pure functions.

---

## 3. Component Boundaries

### 3.1 New Files

| File | Responsibility |
|------|---------------|
| `src/spu94/spu94_adpcm.c` | Pure encode/decode block functions. No dependency on spu94_state. Stateless per-call; caller passes in old/older as a small struct. |
| `src/spu94/spu94_adpcm.h` | Internal header: `spu94_adpcm_state_t` (2 x int16), `spu94_adpcm_encode_block()`, `spu94_adpcm_decode_block()`, filter coefficient table. |
| `include/spu94/spu94_adpcm.h` | **Public** header: standalone encode/decode API for external callers (CLI, Python binding, testing). Exposes the same block functions with documented contracts. |

### 3.2 Modified Files

| File | Change |
|------|--------|
| `src/spu94/spu94_state_internal.h` | Add ADPCM state fields to `struct spu94_state` |
| `src/spu94/spu94_io_chain.c` | Insert ADPCM encode+decode step in `chain_step_impl` before feeding the decimator |
| `include/spu94/spu94.h` | Add `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()` API |
| `src/spu94/spu94_state.c` | Zero ADPCM state in `spu94_init` and `spu94_reset` |
| `src/spu94/CMakeLists.txt` | Add `spu94_adpcm.c` to the OBJECT library |

### 3.3 Unchanged

The reverb network (`spu94_reverb.c`, `spu94_tick.c`), FIR chain (`spu94_fir.c`), presets, registers, write policy -- all unchanged. ADPCM is purely additive upstream of the existing pipeline.

---

## 4. State Design

### 4.1 ADPCM State Struct (Internal to spu94_adpcm.h)

```c
/* Per-channel ADPCM decoder/encoder state. Two previous decoded samples
 * are the complete state for both encode and decode. */
typedef struct {
    int16_t old;    /* most recent decoded sample */
    int16_t older;  /* second-most-recent decoded sample */
} spu94_adpcm_state_t;
```

This struct is used by the pure block functions. It is also embedded directly in `spu94_state`:

### 4.2 Additions to spu94_state

```c
/* Inside struct spu94_state, appended at the tail per the existing
 * "append-only" convention for struct growth: */

/* ADPCM coloration state (M2). When adpcm_enabled != 0, the I/O chain
 * runs encode+decode on each input sample before the FIR decimator sees
 * it. State persists across spu94_process calls; zeroed by spu94_reset. */
uint8_t            adpcm_enabled;
spu94_adpcm_state_t adpcm_enc_l;  /* encoder state, left channel */
spu94_adpcm_state_t adpcm_enc_r;  /* encoder state, right channel */
spu94_adpcm_state_t adpcm_dec_l;  /* decoder state, left channel */
spu94_adpcm_state_t adpcm_dec_r;  /* decoder state, right channel */

/* ADPCM internal accumulation buffer for the 28-sample block boundary.
 * See Section 5 for the block/frame alignment design. */
int16_t            adpcm_buf_l[28];
int16_t            adpcm_buf_r[28];
uint8_t            adpcm_buf_count; /* samples accumulated, 0..27 */
```

Total addition: 1 + (4 * 4) + (28 * 2 * 2) + 1 = **130 bytes**. State grows from 560 to ~690 bytes, well within the 16384 ceiling.

---

## 5. The Block/Frame Boundary Problem (Critical Design Decision)

### 5.1 The Problem

ADPCM operates on fixed 28-sample blocks. `spu94_process()` accepts arbitrary `num_samples` (could be 1, 64, 128, 512, anything). The encoder must accumulate 28 samples before it can select a filter+shift and produce a compressed block. The decoder then produces 28 samples from that block.

This creates a **latency and alignment mismatch** between the caller's frame size and ADPCM's block size.

### 5.2 Two Approaches

**Approach A: Block-accumulate (internal ring buffer)**
- Accumulate input samples in a 28-sample buffer inside `spu94_state`
- When 28 samples arrive: encode the block, decode it, emit all 28 colored samples into the downstream FIR chain
- Adds 0-27 samples of latency depending on caller frame size
- Requires buffering decoded output when the emit rate doesn't align with the caller's consumption rate

**Approach B: Sample-at-a-time with periodic block commits**
- Same accumulation, but the ADPCM stage runs in the `chain_step_impl` per-sample loop
- Each input sample is stored in the accumulation buffer
- When the buffer fills (28 samples): encode block, decode block, and the next 28 calls to chain_step_impl emit from the decoded buffer instead of accumulating
- Net effect: identical to Approach A but naturally integrated into the existing per-sample loop

### 5.3 Recommendation: Approach B (sample-at-a-time with block commits)

Approach B fits the existing `spu94_process` -> `chain_step_impl` per-sample architecture with zero structural change. The implementation is:

```
chain_step_impl(state, l_in, r_in, ...):
    if adpcm_enabled:
        // Accumulate
        state->adpcm_buf_l[state->adpcm_buf_count] = l_in
        state->adpcm_buf_r[state->adpcm_buf_count] = r_in
        state->adpcm_buf_count++

        if state->adpcm_buf_count == 28:
            // Encode + decode in place
            spu94_adpcm_encode_block(state->adpcm_buf_l, 28,
                                     &state->adpcm_enc_l, encoded_bytes)
            spu94_adpcm_decode_block(encoded_bytes,
                                     &state->adpcm_dec_l, state->adpcm_buf_l)
            // (same for R channel)
            state->adpcm_buf_count = 0
            // adpcm_buf now contains decoded output; emit from index 0

        // Emit the sample at current position from the decoded buffer
        l_colored = state->adpcm_buf_l[current_emit_index]
        r_colored = state->adpcm_buf_r[current_emit_index]
    else:
        l_colored = l_in
        r_colored = r_in

    // Feed l_colored, r_colored into the FIR decimator (existing path)
    spu94_fir_decimate(state, l_colored, r_colored, ...)
```

**Wait -- this has a subtlety.** The emit index and the accumulation index are the same buffer. During accumulation (before 28 samples are ready), what does the stage emit?

### 5.4 The Emit-During-Accumulation Problem

There are two sub-approaches:

**5.4a: Zero-latency passthrough during accumulation (NOT recommended)**
Pass through uncolored PCM while accumulating, then switch to ADPCM-colored output once a block is ready. Creates audible discontinuities at block boundaries (uncolored -> colored -> uncolored transition every 28 samples).

**5.4b: Fixed 28-sample latency (RECOMMENDED)**
Use a double-buffer: one buffer accumulates input, the other holds the most recently decoded block for emission. When accumulation fills, encode+decode the accumulation buffer, swap roles.

```
state fields:
    int16_t  adpcm_in_l[28], adpcm_in_r[28];   // accumulation
    int16_t  adpcm_out_l[28], adpcm_out_r[28];  // emission
    uint8_t  adpcm_idx;                          // 0..27, shared cursor
```

Per sample:
1. Store input at `adpcm_in_*[adpcm_idx]`
2. Emit from `adpcm_out_*[adpcm_idx]`
3. Increment `adpcm_idx`
4. When `adpcm_idx == 28`: encode `adpcm_in_*` -> decode into `adpcm_out_*`, reset `adpcm_idx = 0`

The first block of 28 samples emits zeros (the initial `adpcm_out_*` is zeroed). This is a fixed 28-sample latency at 44.1kHz (~0.635 ms), which is musically negligible and consistent (no jitter). After the first block, output is always exactly one ADPCM-block behind input.

**Updated state cost with double buffer:** 28 * 2 * 2 * 2 + 1 = 225 bytes. State grows to ~785 bytes. Still well within ceiling.

### 5.5 Latency Reporting

`SPU94_LATENCY_SAMPLES` is currently 58 (FIR round-trip). With ADPCM enabled, it becomes 58 + 28 = 86. This should be queryable at runtime:

```c
uint32_t spu94_get_latency_samples(void);  // existing -- returns 58 or 86
```

Or better: keep the existing function returning the fixed FIR latency, and add:

```c
uint32_t spu94_get_total_latency_samples(const spu94_state *state);
// Returns SPU94_LATENCY_SAMPLES + (state->adpcm_enabled ? 28 : 0)
```

This lets the JUCE plugin report correct latency to the DAW for PDC (plugin delay compensation).

---

## 6. API Surface

### 6.1 Public API Additions to spu94.h

```c
/* Enable/disable ADPCM coloration stage. When enabled, input PCM is
 * encoded to 4-bit ADPCM then decoded back to PCM before entering the
 * reverb chain. This introduces the quantization artifacts and filter
 * coloration characteristic of PS1 audio.
 *
 * Changing this flag mid-stream is safe. Disabling clears the ADPCM
 * accumulation buffer (emits silence for the partial block, then
 * resumes passthrough). Enabling starts accumulating from scratch.
 *
 * Latency: adds 28 samples (~0.635ms) when enabled. */
void spu94_set_adpcm_enabled(spu94_state *state, int enabled);
int  spu94_get_adpcm_enabled(const spu94_state *state);

/* Total processing latency in samples at 44.1kHz, accounting for
 * current ADPCM state. */
uint32_t spu94_get_total_latency_samples(const spu94_state *state);
```

### 6.2 Public Standalone API (include/spu94/spu94_adpcm.h)

```c
/* Standalone ADPCM encode/decode for offline use, testing, CLI tools.
 * These functions have NO dependency on spu94_state. */

typedef struct {
    int16_t old;
    int16_t older;
} spu94_adpcm_state_t;

/* Encode 28 PCM samples into a 16-byte ADPCM block.
 * pcm: input array of exactly 28 int16 samples.
 * enc_state: encoder state (old/older); updated in place.
 * out_block: output array of exactly 16 bytes.
 *
 * Filter selection: brute-force MSE minimization across all 5 filters. */
void spu94_adpcm_encode_block(const int16_t pcm[28],
                               spu94_adpcm_state_t *enc_state,
                               uint8_t out_block[16]);

/* Decode a 16-byte ADPCM block into 28 PCM samples.
 * block: input array of exactly 16 bytes.
 * dec_state: decoder state (old/older); updated in place.
 * pcm: output array of exactly 28 int16 samples. */
void spu94_adpcm_decode_block(const uint8_t block[16],
                               spu94_adpcm_state_t *dec_state,
                               int16_t pcm[28]);
```

### 6.3 Why Both APIs

The standalone API serves:
- **Testing:** Unit tests can exercise encode/decode in isolation without spu94_state ceremony
- **CLI:** A future `spu94 --adpcm-encode input.wav output.vag` mode
- **Python binding:** Direct ctypes access for exploration/analysis
- **Separation of concerns:** The encode/decode math is pure; the integration is plumbing

The integrated API (`spu94_set_adpcm_enabled`) serves:
- **Plugin use case:** Single toggle in the JUCE UI
- **Correct state lifetime:** Encoder/decoder state lives with reverb state, zeroed together

---

## 7. Signal Flow (Before and After)

### 7.1 Current (M1)

```
44.1kHz PCM input
    |
    v
[FIR Decimator] -- 39-tap half-band, 44.1k -> 22.05k
    |
    v
[Reverb Network] -- IIR + comb + all-pass on work buffer
    |
    v
[FIR Interpolator] -- 39-tap half-band, 22.05k -> 44.1k
    |
    v
44.1kHz PCM output
```

### 7.2 With ADPCM (M2)

```
44.1kHz PCM input
    |
    v
[ADPCM Encode+Decode] -- 28-sample block, adds coloration + 28-sample latency
    |                      (bypassed when adpcm_enabled == 0)
    v
[FIR Decimator] -- 39-tap half-band, 44.1k -> 22.05k
    |
    v
[Reverb Network] -- IIR + comb + all-pass on work buffer
    |
    v
[FIR Interpolator] -- 39-tap half-band, 22.05k -> 44.1k
    |
    v
44.1kHz PCM output
```

The ADPCM stage inserts at the very top of `chain_step_impl`, BEFORE the call to `spu94_fir_decimate`. This is the correct position per PS1 hardware: voice data is ADPCM-decoded before entering the mix bus, and the mix bus feeds the reverb.

### 7.3 Where in chain_step_impl

In `spu94_io_chain.c`, the modification is at the top of `chain_step_impl`:

```c
static void chain_step_impl(spu94_state *state,
                            int16_t l_in, int16_t r_in,
                            int16_t *l_out, int16_t *r_out,
                            int reverb_active) {
    /* --- NEW: ADPCM coloration stage --- */
    if (state->adpcm_enabled) {
        // Store input, emit from previous decoded block
        state->adpcm_in_l[state->adpcm_idx] = l_in;
        state->adpcm_in_r[state->adpcm_idx] = r_in;
        l_in = state->adpcm_out_l[state->adpcm_idx];
        r_in = state->adpcm_out_r[state->adpcm_idx];
        state->adpcm_idx++;
        if (state->adpcm_idx == 28) {
            uint8_t block[16];
            spu94_adpcm_encode_block(state->adpcm_in_l,
                                     &state->adpcm_enc_l, block);
            spu94_adpcm_decode_block(block, &state->adpcm_dec_l,
                                     state->adpcm_out_l);
            // Same for R channel
            spu94_adpcm_encode_block(state->adpcm_in_r,
                                     &state->adpcm_enc_r, block);
            spu94_adpcm_decode_block(block, &state->adpcm_dec_r,
                                     state->adpcm_out_r);
            state->adpcm_idx = 0;
        }
    }
    /* --- END ADPCM --- */

    /* Existing FIR decimator path (unchanged) */
    int16_t dec_l = 0, dec_r = 0;
    int dec_valid = 0;
    spu94_fir_decimate(state, l_in, r_in, &dec_l, &dec_r, &dec_valid);
    // ... rest unchanged ...
}
```

The `uint8_t block[16]` is a stack temporary -- it exists only during the encode+decode and is never persisted. This respects the zero-heap constraint.

---

## 8. Patterns to Follow

### 8.1 Pure Functions + Integration Wrapper

The ADPCM encode/decode block functions are **pure**: they take explicit inputs and state, produce explicit outputs, and have no side effects beyond updating the passed-in state struct. The integration in `chain_step_impl` is the only place that touches `spu94_state`'s ADPCM fields.

This mirrors the existing pattern: `spu94_fir_decimate` and `spu94_fir_interpolate` are internal functions with explicit I/O; `chain_step_impl` orchestrates them.

### 8.2 Append-Only Struct Growth

New fields go at the tail of `struct spu94_state`, per the existing convention documented in the struct comments. The `_Static_assert` on `SPU94_STATE_SIZE_MAX` catches overflows at compile time.

### 8.3 ADR for Every Gray Area

Each gray area in the ADPCM algorithm (shift 13-15 handling, rounding behavior, filter 4 inclusion, mid-stream enable/disable semantics) gets its own ADR entry in `docs/DECISIONS.md`, following the existing numbered ADR convention.

### 8.4 Test Isolation

Unit tests exercise `spu94_adpcm_encode_block` and `spu94_adpcm_decode_block` directly (no spu94_state needed). Integration tests exercise the full pipeline with ADPCM enabled/disabled via `spu94_set_adpcm_enabled`.

---

## 9. Anti-Patterns to Avoid

### 9.1 Don't Make ADPCM a Compile-Time Option

Making ADPCM conditional via `#ifdef SPU94_ADPCM` fragments the build matrix, complicates testing, and defeats the "enable flag" design. The code cost of having ADPCM compiled in but disabled is negligible (~130-225 bytes of state, zero CPU when disabled).

### 9.2 Don't Share Encoder/Decoder State

The encoder and decoder each have their own `old/older` pair per channel. Even though in a round-trip scenario the decoder's state will track the encoder's output, they must be independent structs. Sharing would break if someone uses the standalone encode API separately from decode, and it obscures the data flow.

### 9.3 Don't Buffer Encoded Bytes

The 16-byte encoded block is a transient. Encode, immediately decode, discard the bytes. There is no use case for persisting the compressed representation inside `spu94_state`. The standalone API lets external callers save encoded blocks if they want.

### 9.4 Don't Process L+R as Interleaved

The existing pipeline is planar (separate L/R arrays). ADPCM blocks are mono (each block encodes one channel). Keep them separate -- encode L block, encode R block. No interleaving logic.

---

## 10. Build Order

The implementation naturally decomposes into phases with clean dependency boundaries:

### Phase 1: Standalone ADPCM codec (no libspu94 dependency)

1. `src/spu94/spu94_adpcm.c` + `src/spu94/spu94_adpcm.h` (internal)
2. `include/spu94/spu94_adpcm.h` (public standalone API)
3. Unit tests: known-vector decode, round-trip encode-decode, filter coefficient verification, edge cases (silence, DC, full-scale)
4. Add to `CMakeLists.txt` OBJECT library

**Exit criterion:** `spu94_adpcm_decode_block` produces bit-identical output to nocash reference decode for known test vectors. Encoder round-trips through decoder with measurably low MSE.

### Phase 2: Integration into spu94_state and spu94_process

1. Add ADPCM fields to `struct spu94_state`
2. Add `spu94_set_adpcm_enabled()` / `spu94_get_adpcm_enabled()` to public API
3. Add `spu94_get_total_latency_samples()` to public API
4. Wire ADPCM stage into `chain_step_impl` in `spu94_io_chain.c`
5. Handle `spu94_reset` zeroing of ADPCM state
6. Integration tests: process with ADPCM on vs off, verify coloration is present, verify latency offset, verify mid-stream toggle behavior

**Exit criterion:** `spu94_process` with ADPCM enabled produces audibly different (ADPCM-colored) output compared to disabled. Latency reporting is correct. All existing tests pass unchanged (ADPCM is off by default).

### Phase 3: Python binding + CLI support

1. Expose standalone encode/decode via ctypes
2. Expose `set_adpcm_enabled` / `get_adpcm_enabled` via ctypes
3. Add `--adpcm` flag to CLI
4. Python test harness for A/B comparison (ADPCM on vs off)

### Phase 4: Witness validation + golden files

1. Generate ADPCM-on golden files for all 10 presets
2. Spectral analysis: verify ADPCM coloration matches expected frequency-response degradation (high-frequency rolloff from the 4-bit quantization)
3. If PS1 hardware comparison data becomes available: witness diff

---

## 11. Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|------------|
| Block boundary creates audible artifacts when enabling/disabling mid-stream | Medium | Zero the output buffer on disable; start fresh accumulation on enable. First block after enable emits silence (28 samples). Document in ADR. |
| Encoder MSE brute-force is too slow for real-time | Low | 5 filters x 3 shifts = 15 candidates per block. Each candidate processes 28 samples. That is 420 multiply-adds per block of 28 samples at 44.1kHz -- trivially fast. |
| ADPCM latency breaks existing golden files | None | ADPCM is off by default. Existing tests never enable it. Zero blast radius. |
| State struct growth breaks SPU94_STATE_SIZE_MAX | None | Growing by ~225 bytes from 560 to ~785. Ceiling is 16384. |
| rt_safety gates fail (new code allocates) | Low | ADPCM functions are pure arithmetic on stack/passed-in buffers. No heap, no locks, no syscalls. Same discipline as existing reverb code. Existing rt_safety gates cover the whole library. |

---

## Sources

- [nocash psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) -- block structure, flag byte, nibble packing
- [nocash psx-spx: CDROM XA Audio ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) -- filter coefficients, decode pseudocode (shared algorithm)
- [jsgroth: PlayStation SPU Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) -- implementation insights, shift edge cases, filter application
- [psxavenc (WonderfulToolchain)](https://github.com/WonderfulToolchain/psxavenc) -- reference encoder, MSE brute-force filter selection strategy
- [psx-spx.consoledev.net SPU page](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- canonical spec mirror
