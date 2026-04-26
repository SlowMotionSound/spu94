# Domain Pitfalls — M2: Sony 4-bit ADPCM Encode/Decode

**Domain:** PS1 SPU ADPCM codec for libspu94
**Researched:** 2026-04-26

## Critical Pitfalls

Mistakes that produce wrong output or break bit-faithfulness.

### Pitfall 1: Using `/64` Instead of `>> 6` for the Filter Prediction

**What goes wrong:** The filter prediction formula is `(old * f0 + older * f1 + 32) >> 6`. Using C integer division `/64` instead of arithmetic right shift `>> 6` produces different results for negative intermediate values. C integer division truncates toward zero; arithmetic right shift truncates toward negative infinity (floor division).

**Why it happens:** `/64` and `>> 6` look equivalent and ARE equivalent for non-negative values. The difference only appears when `(old * f0 + older * f1 + 32)` is negative — which happens frequently with audio signals.

**Consequences:** Off-by-one errors in decoded samples. Subtle but audible as a DC offset or low-level noise. Breaks bit-faithfulness.

**Prevention:** Use `>> 6` exclusively. The existing libspu94 codebase already has the ASR discipline documented in ADR-0001 and verified by `SPU94_STATIC_ASSERT` in `spu94_q15.h`. The same static assert covers ADPCM. Document this in a new ADR.

**Detection:** Round-trip test with known vectors. Any difference between hand-computed values and code output in the negative-intermediate case.

### Pitfall 2: Using Original PCM (Not Decoded Value) as Encoder Prediction State

**What goes wrong:** The encoder predicts the next sample using `old` and `older`. If it uses the original PCM values instead of the reconstructed (decoded) values, the prediction diverges from what the decoder will compute. The encoder thinks it encoded well; the decoder produces different output.

**Why it happens:** Intuitive to use the "true" signal for prediction. But the decoder does not have the true signal — it only has its own reconstructed output.

**Consequences:** Dramatically worse encode quality. Error accumulates across blocks. Audible distortion, especially on sustained tones.

**Prevention:** After encoding each nibble, immediately decode it using the exact decode algorithm. Use the decoded value as the new `old`/`older` state. The encoder must contain a copy of the decoder.

**Detection:** Compare round-trip output (encode → decode) against output from encoding with a known-good encoder. Large divergence = this bug.

### Pitfall 3: Wrong Nibble Order Within Data Bytes

**What goes wrong:** Each data byte contains two 4-bit nibbles. The low nibble (bits 0-3) is the FIRST sample, the high nibble (bits 4-7) is the SECOND sample. Getting this reversed decodes samples in the wrong order — every pair is swapped.

**Why it happens:** "High bits first" feels natural (MSB-first thinking). PS1 ADPCM is low-nibble-first within each byte.

**Consequences:** Every pair of samples is swapped. Audible as a mangled, aliased mess.

**Prevention:** Extract nibbles as:
```c
int16_t nibble_even = (int16_t)(int8_t)((byte << 4) & 0xF0) >> 4;  /* low nibble first */
int16_t nibble_odd  = (int16_t)(int8_t)(byte & 0xF0) >> 4;         /* high nibble second */
```
Or equivalently:
```c
int nibble_lo = (byte >> 0) & 0x0F;  /* even sample index */
int nibble_hi = (byte >> 4) & 0x0F;  /* odd sample index */
/* then sign-extend each from 4 bits */
```

**Detection:** Decode a known block with an alternating nibble pattern (e.g., 0x12). If the output shows value-for-filter-1 then value-for-filter-2 (not vice versa), ordering is correct.

### Pitfall 4: Sign Extension of 4-bit Nibbles

**What goes wrong:** The 4-bit nibble is a signed value in range [-8, +7]. If treated as unsigned (0-15), the decode produces wrong shifted values for all nibbles >= 8.

**Why it happens:** C has no native 4-bit signed type. Extracting a nibble with `(byte >> 4) & 0x0F` gives an unsigned 0-15 value. Must explicitly sign-extend.

**Consequences:** All negative-nibble samples decode to large positive values instead of small negative values. Completely wrong output.

**Prevention:** Sign-extend after extraction:
```c
int nibble = (byte >> shift) & 0x0F;
if (nibble >= 8) nibble -= 16;  /* sign extend: 8→-8, 9→-7, ..., 15→-1 */
```

**Detection:** Test with a block where all nibbles are 0x0F (should decode as -1, not +15).

## Moderate Pitfalls

### Pitfall 5: Forgetting to Handle Shift > 12

**What goes wrong:** Shift values 13, 14, 15 are valid in the header byte but behave as shift = 9 on hardware. If the code computes `12 - shift` for these values, it gets negative shift amounts (-1, -2, -3), producing undefined behavior or wrong results.

**Prevention:** Clamp shift before use:
```c
int shift = header & 0x0F;
if (shift > 12) shift = 9;
int shift_amount = 12 - shift;  /* now always >= 0 */
```

### Pitfall 6: VAG Header Endianness

**What goes wrong:** VAG header fields (version, data size, sample rate) are big-endian. On a little-endian host (x86, ARM), reading them directly as `uint32_t` produces byte-swapped garbage.

**Prevention:** Explicit byte-order conversion. Do NOT use `ntohl()` (requires POSIX headers, not available on MCU). Use explicit shift-and-mask:
```c
uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}
```

### Pitfall 7: Intermediate Overflow in Filter Prediction

**What goes wrong:** `old * f0` where `old` is int16_t and `f0` is up to 122. The product can be up to 122 * 32767 = 3,997,574, and `old * f0 + older * f1` can be up to ~4M + ~2M = ~6M. This fits in int32_t (max ~2.1B) but NOT in int16_t.

**Prevention:** Compute filter prediction in int32_t:
```c
int32_t predicted = (int32_t)old * f0 + (int32_t)older * f1;
```
The cast to `int32_t` before multiplication is essential.

### Pitfall 8: Off-by-One in Block Count

**What goes wrong:** VAG data size field (header offset 0x0C) is the size of the ADPCM data in bytes. Number of blocks = data_size / 16. But the last block may be a terminator block (all zeros with flags = 0x07 or 0x05). Treating it as real audio data adds 28 samples of near-silence at the end.

**Prevention:** Check flag byte of each block during decode. Stop when loop-end flag is set (or at data_size boundary). The terminator block is part of the format, not a data block.

## Minor Pitfalls

### Pitfall 9: Filter Index Clamping

**What goes wrong:** Filter index > 4 is undocumented behavior. Some ADPCM data (corrupt or intentionally weird) may have filter 5/6/7 in the header.

**Prevention:** Clamp to range 0-4 before table lookup. Log or return a warning code (extend `spu94_result_t` if desired).

### Pitfall 10: Zero-Padding Final Block

**What goes wrong:** If the input PCM has a sample count not divisible by 28, the final block needs padding. Failing to zero-pad produces uninitialized nibbles in the last block.

**Prevention:** Zero-pad the final 28-sample frame before encoding. Document that the encoded stream may contain up to 27 samples of trailing silence.

### Pitfall 11: State Reset Between Independent Streams

**What goes wrong:** Reusing an `spu94_adpcm_state` from a previous decode without resetting it. The `old`/`older` values from the previous stream contaminate the first block of the new stream.

**Prevention:** Document that callers must zero-init state for a new stream. Provide `spu94_adpcm_state state = {0};` as the canonical init pattern. Consider adding an `spu94_adpcm_state_init()` function for clarity.

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| Decode implementation | ASR vs division (Pitfall 1), nibble order (Pitfall 3), sign extension (Pitfall 4) | Known-vector tests with hand-computed expected values |
| Encode implementation | Encoder prediction state (Pitfall 2) | Round-trip test: encode → decode must be deterministic and match standalone decode |
| VAG file I/O | Endianness (Pitfall 6), terminator block (Pitfall 8) | Test with real VAG files from PS1 homebrew |
| Python binding | Byte buffer handling across ctypes boundary | Use `ctypes.c_char * 16` for block, `numpy.ctypeslib.ndpointer` for sample arrays |
| Integration testing | State contamination (Pitfall 11) | Always zero-init state in test fixtures |
