# Pitfalls Research — ADPCM Encode/Decode for libspu94

**Domain:** Adding Sony 4-bit ADPCM encode/decode to an existing bit-faithful PS1 SPU reverb reimplementation
**Researched:** 2026-04-26
**Confidence:** MEDIUM-HIGH (decode algorithm from nocash/psx-spx XA-ADPCM spec is well-documented; shift 13-15 edge case has MEDIUM confidence due to conflicting accounts; encoder design has LOW-MEDIUM confidence as Sony SDK encoder is not publicly documented in detail)

---

## Orientation

This document covers pitfalls specific to **adding ADPCM encode/decode to the existing libspu94 reverb library**. It does NOT repeat the M1 reverb pitfalls (those are in the git history of this file). The focus is:

1. Getting the decode algorithm bit-accurate to PS1 hardware
2. Building an encoder that produces hardware-compatible output
3. Integrating ADPCM into the existing libspu94 pipeline without breaking reverb correctness
4. Maintaining the project's licensing posture while implementing a codec that every GPL emulator has already implemented

Phase labels below reference the ADPCM milestone's expected structure:

- **P-DECODE** — implement the 4-bit ADPCM decoder (nibble-to-PCM)
- **P-ENCODE** — implement the ADPCM encoder (PCM-to-nibble)
- **P-INTEGRATE** — wire ADPCM decode into the reverb pipeline (ADPCM colors audio before reverb)
- **P-LOOPFLAGS** — implement loop flag handling (loop start, loop end, loop repeat, one-shot)
- **P-VERIFY** — test infrastructure for ADPCM bit-accuracy
- **P-DECISIONS** — document gray-area resolutions

---

## Critical Pitfalls

Mistakes that cause rewrites, bit-accuracy failures, or licensing problems.

### C1: Shift values 13-15 — the biggest known divergence between emulators

**Severity:** Critical (for bit-accuracy claim)

**What goes wrong:** The ADPCM header byte's low 4 bits encode a shift value (0-15). The nocash decode formula computes the effective shift as `shift = 12 - (header AND 0Fh)`. For header shift values 0-12, this produces left-shifts of 12 down to 0. For values 13-15, the formula produces *negative* shift values (-1, -2, -3). The nocash spec simply states: "reserved shift values 13..15 will act same as shift=9."

Different sources describe different hardware behavior:

- **nocash/psx-spx:** "act same as shift=9" (i.e., treat header value 13-15 as if it were 3, producing `12 - 3 = 9` effective shift, which is equivalent to `nibble << 9`). This is the simplest interpretation.
- **jsgroth's blog (emulator developer):** States shift 13-15 are "invalid and behave the same as shift=9."
- **SNES BRR (closely related codec on the SPC700):** For range values 13-15, the SnesLab wiki documents the hardware formula as `sample = (nibble >> 3) << 11`. This is NOT equivalent to shift=9. For a nibble of -8 (0x8 signed), shift=9 gives -4096 while the BRR formula gives -2048. The SNES and PS1 SPU share a codec lineage (both are Sony ADPCM), but the hardware may differ.
- **No definitive hardware capture exists** in the public domain that disambiguates the PS1 behavior specifically for shift 13-15.

**Why it happens:** Shift 13-15 are never used by any known commercial PS1 game's ADPCM data. The Sony SDK encoder never produces them. Emulator developers implement "whatever makes the spec stop complaining" without hardware verification.

**Consequences:** If SPU-94's decoder picks the wrong behavior, any future ADPCM test data that exercises shift 13-15 will diverge from hardware. More importantly, the ADPCM encoder must never *produce* these shift values, so the decoder behavior is only relevant for round-trip fidelity of adversarial test vectors.

**Prevention:**
1. The decoder MUST handle shift 13-15 explicitly, not through undefined behavior (negative shift in C is UB).
2. Pick the nocash interpretation (treat as shift=9) as the default. Document in DECISIONS.md.
3. Implement via a clamp: `if (shift_from_header > 12) shift_from_header = 9;` — this matches nocash literally and avoids negative-shift UB.
4. Flag this as a **hardware-verification target for M5** (when Anthony's PS1 is available for capture testing).
5. Write a dedicated unit test with nibble patterns at shift 13, 14, 15 so the behavior is locked and documented regardless of which interpretation is chosen.

**Detection:** Test vectors with shift=13/14/15 headers compared against at least two emulator witnesses.

**Phase:** P-DECODE, P-DECISIONS, P-VERIFY

**Source:** [psx-spx XA-ADPCM](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm); [jsgroth SPU Part 1](https://jsgroth.dev/blog/posts/ps1-spu-part-1/); [SnesLab BRR](https://sneslab.net/wiki/Bit_Rate_Reduction)

---

### C2: The +32 rounding bias in the filter — truncation vs rounding inconsistency with reverb core

**Severity:** Critical (for internal consistency)

**What goes wrong:** The nocash ADPCM decode formula explicitly includes a rounding term: `s = (t SHL shift) + ((old*f0 + older*f1 + 32) / 64)`. The `+32` before `/64` is a round-to-nearest bias (adding half the divisor before integer division). This is **rounding**, not truncation.

Meanwhile, the existing libspu94 reverb core uses *truncation* everywhere (ASR >>15, no rounding bias) per ADR-0001. An implementer who internalizes "SPU-94 always truncates" will reflexively omit the +32, producing a decoder that truncates where the hardware rounds.

**Why it happens:** The reverb and ADPCM are different hardware subsystems with different arithmetic conventions. The reverb's Q15 multiplies truncate; the ADPCM filter's division-by-64 rounds. Applying a blanket "no rounding" policy to the entire project is wrong.

**Consequences:** Every decoded sample is potentially off by 1 LSB. Over 28 samples per block with filter feedback, the error compounds through prev1/prev2 state, producing audibly different decode output on any non-trivial audio.

**Prevention:**
1. Implement the ADPCM decode formula EXACTLY as nocash specifies it, including `+32`.
2. Document in DECISIONS.md: "ADPCM filter uses rounding (`+32` / 64) per the spec. This differs from the reverb core's truncation convention (ADR-0001). Both are correct for their respective hardware subsystems."
3. Do NOT refactor `q15_mul_truncate` to serve double duty for ADPCM filter math. The ADPCM filter operates at a different precision (divide-by-64, not divide-by-32768).
4. Unit test: decode a known ADPCM block with and without the +32 term; verify the +32 version matches witness output.

**Detection:** Witness diff of decoded ADPCM output against Mednafen or DuckStation; the +32 rounding is well-established across emulators.

**Phase:** P-DECODE, P-DECISIONS

**Source:** [psx-spx XA-ADPCM decode formula](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm)

---

### C3: Clamping to int16 AFTER filter, not before — wrong clamping order

**Severity:** Critical (for bit-accuracy)

**What goes wrong:** The nocash formula specifies: compute the full expression `s = (t SHL shift) + ((old*f0 + older*f1 + 32) / 64)`, THEN clamp: `s = MinMax(s, -8000h, +7FFFh)`. An implementer who clamps the shifted nibble to int16 *before* adding the filter contribution, or who clamps the filter contribution separately, gets different results when the intermediate exceeds int16 range.

The critical detail: `old` and `older` (the feedback samples) are the *clamped* output from the previous iteration. So the pipeline is:

```
raw = shift_nibble + filter_contribution(old_clamped, older_clamped)
clamped = MinMax(raw, -0x8000, +0x7FFF)
older = old;  old = clamped;  // feedback uses CLAMPED value
```

**Why it happens:** Premature clamping is a natural defensive-programming instinct. An implementer sees a 32-bit intermediate and thinks "clamp early to prevent overflow." But the hardware computes the full sum in wider-than-16-bit arithmetic and only clamps the final result.

**Consequences:** Divergence on any sample where shifted nibble + filter contribution temporarily exceeds int16 range before the final clamp brings it back. This affects loud passages and high-shift-value blocks.

**Prevention:**
1. Use `int32_t` for ALL intermediates in the decode loop. The maximum possible value is bounded: nibble (-8..+7) shifted left by 12 = -32768..+28672, plus filter contribution bounded by 2 * 32767 * 122 / 64 ~ 124,000. Total fits comfortably in int32.
2. Clamp only once, at the assignment to the output and to the feedback state.
3. Explicit unit test: construct a block where shifted nibble is +28672 and filter contribution pushes the intermediate above +32767, verify the output is +32767 (clamped), not some premature-clamp artifact.

**Detection:** Test vectors with deliberately overflowing intermediates.

**Phase:** P-DECODE, P-VERIFY

**Source:** [psx-spx XA-ADPCM formula](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — clamping follows the full expression

---

### C4: Filter coefficient index out of range — SPU has 5 filters, XA has 4

**Severity:** Significant (wrong filter = wrong audio)

**What goes wrong:** The filter index is extracted from bits 4-6 of the header byte (for SPU-ADPCM) or bits 4-5 (for XA-ADPCM). This gives a 3-bit range of 0-7 for SPU-ADPCM. Only indices 0-4 are defined:

| Filter | f0 (pos) | f1 (neg) |
|--------|----------|----------|
| 0      | 0        | 0        |
| 1      | +60      | 0        |
| 2      | +115     | -52      |
| 3      | +98      | -55      |
| 4      | +122     | -60      |

Filter indices 5, 6, 7 are undefined. An implementation that indexes into a 5-element table with index 5-7 reads garbage memory (buffer overrun). An implementation that clamps to 4 or wraps modulo 5 has made an assumption about hardware behavior.

**Why it happens:** The header format allows 3 bits for filter, giving indices 0-7, but only 5 are defined. No commercial game uses filter 5-7. The hardware behavior for these indices is undocumented.

**Prevention:**
1. Allocate the coefficient table as 8 entries (indices 0-7), with entries 5-7 set to (0, 0) as a safe default.
2. Document in DECISIONS.md: "Filter indices 5-7 are treated as (0, 0). Hardware behavior for these indices is undocumented. This is a hardware-verification target."
3. Alternatively, clamp filter index to 0-4 with a note that the clamp value is a guess.
4. Never let an untrusted ADPCM header index directly into a fixed-size array without bounds checking.

**Detection:** Fuzz test with random header bytes; valgrind/ASAN on decode of adversarial data.

**Phase:** P-DECODE, P-DECISIONS

**Source:** [psx-spx XA-ADPCM](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — "SPU-ADPCM supports five filters (0..4)"

---

### C5: Licensing pitfall — every GPL emulator has an ADPCM decoder you must not copy

**Severity:** Critical (licensing)

**What goes wrong:** The ADPCM decode loop is ~15 lines of C. Every PS1 emulator (Mednafen GPLv2, DuckStation GPLv2, PCSX-Redux GPLv2) has one. The temptation to "just look at how they do it" is enormous because the nocash spec's pseudocode is somewhat ambiguous on edge cases. If the implementer reads any GPL decode loop, the resulting code is arguably derivative even if rewritten — the structure, variable names, and edge-case handling are absorbed unconsciously.

**Why it happens:** The decode algorithm is simple enough that independent implementations look similar. But the project's licensing posture (MIT/Apache, build from spec) requires that the implementation demonstrably derives from the spec, not from GPL sources.

**Consequences:** If derivative-work status is established, the entire libspu94 library inherits GPL, foreclosing MIT/Apache licensing. The blast radius is worse than for the reverb (M1) because ADPCM is simpler and structural similarity is harder to rebut.

**Prevention:**
1. Implement EXCLUSIVELY from the nocash XA-ADPCM pseudocode (which is a factual specification, not a copyrightable implementation).
2. Do NOT read Mednafen's `SPU_Decode_ADPCM()`, DuckStation's `DecodeBlock()`, or any GPL source.
3. Use SPU-94's own naming conventions (already established: `sat_s16`, `q15_mul_truncate`, etc.) — do NOT mirror emulator variable names.
4. If a gray area arises that the spec doesn't resolve, use audio-level witness comparison (decode the same ADPCM block, diff outputs numerically) rather than reading the witness's source code.
5. Log every spec consultation in DECISIONS.md per the existing M1 protocol.
6. The decode loop's simplicity is actually an advantage: there are very few ways to write `shifted_nibble + filter(old, older)` in C. The spec IS the implementation; there is no creative expression to copy.

**Phase:** P-DECODE, P-DECISIONS

**Source:** Project constraint from `.planning/PROJECT.md`; [Clean-room design principles](https://en.wikipedia.org/wiki/Clean_room_design)

---

### C6: Feedback state (old/older) initialization and carry across blocks

**Severity:** Significant (audible glitch at block boundaries)

**What goes wrong:** The ADPCM decoder maintains two feedback samples (`old` and `older`, also called `prev1` and `prev2`). These carry across block boundaries — the last two decoded samples of block N become the initial `old`/`older` for block N+1. Getting this wrong produces:

- **Zeroed state at each block:** A click every 28 samples (every block boundary) as the filter "restarts from silence."
- **Swapped old/older:** Filter coefficients are asymmetric (f0 != f1 for filters 1-4), so swapping the two feedback samples produces wrong predictions.
- **Unclamped feedback:** If the raw (pre-clamp) value is fed back instead of the clamped value, the filter sees values outside int16 range, producing cascading overflow.

**Why it happens:**
- "Reset state per block" is a natural assumption from other codecs (e.g., IMA-ADPCM resets predictor state per block).
- Variable naming confusion: which is `old` (most recent, one sample ago) and which is `older` (two samples ago)? The nocash formula uses `old` for sample[-1] and `older` for sample[-2], but other sources reverse the naming.

**Prevention:**
1. State struct holds `int16_t prev1, prev2;` with clear documentation: `prev1` = most recent decoded sample (nocash's `old`), `prev2` = second most recent (nocash's `older`).
2. State is initialized to zero at voice key-on (matching hardware behavior — the PS1 zeroes the decode state when a voice is keyed on).
3. State carries across blocks without reset (unless loop-end flag triggers a jump to loop-start, at which point state carries, it does NOT reset).
4. Feedback uses the CLAMPED output value, not the pre-clamp intermediate.
5. Unit test: decode two consecutive blocks where block 2's filter depends on block 1's tail samples. Verify that zeroing state between blocks produces different (wrong) output.

**Detection:** Audible click at block boundaries in decoded audio; witness diff shows periodic 28-sample-interval errors.

**Phase:** P-DECODE, P-VERIFY

**Source:** [psx-spx XA-ADPCM](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — `older=old, old=s` at end of each sample

---

## Significant Pitfalls

### S1: Nibble extraction order — high nibble first or low nibble first?

**Severity:** Significant (every other sample is wrong)

**What goes wrong:** Each byte of ADPCM data contains two 4-bit nibbles. The extraction order matters: for SPU-ADPCM, the LOW nibble (bits 0-3) is decoded FIRST, then the HIGH nibble (bits 4-7). Getting this backwards swaps every pair of samples, producing garbled audio that still "kind of sounds like something."

This is the OPPOSITE of some other ADPCM variants (e.g., IMA-ADPCM typically decodes high nibble first).

**Why it happens:** Byte-level data layout is easy to get backwards. The nocash spec for XA-ADPCM describes a different nibble arrangement than SPU-ADPCM (XA interleaves differently due to sector structure), adding confusion.

**Prevention:**
1. SPU-ADPCM: low nibble first, high nibble second within each data byte.
2. Extract with: `nibble_lo = (byte >> 0) & 0xF; nibble_hi = (byte >> 4) & 0xF;` — decode lo first.
3. Sign-extend 4-bit to int32: `int32_t signed_nibble = (nibble < 8) ? nibble : nibble - 16;` or cast through `int8_t`: `int32_t signed_nibble = ((int8_t)(nibble << 4)) >> 4;`
4. Unit test: decode a known VAG file (the Sony SDK includes sample VAG files; psxavenc can produce them) and compare against a known-good PCM decode.

**Detection:** Decoded audio sounds "phasy" or garbled but still recognizably musical — the samples are present but misordered within pairs.

**Phase:** P-DECODE, P-VERIFY

**Source:** [psx-spx SPU ADPCM](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm); [jsgroth SPU Part 1](https://jsgroth.dev/blog/posts/ps1-spu-part-1/)

---

### S2: Encoder filter selection — greedy vs brute-force vs Sony SDK behavior

**Severity:** Significant (encoder quality, not decoder correctness)

**What goes wrong:** The ADPCM encoder must choose, for each 28-sample block, which filter (0-4) and which shift (0-12) minimize the quantization error. There are 5 * 13 = 65 possible (filter, shift) combinations per block. Three encoding strategies exist:

- **Sony SDK (vagconv/aiff2vag):** Unknown algorithm, but the output is the "ground truth" that PS1 games shipped with. The SDK tools are not publicly available and their encoding strategy is not documented.
- **Greedy/heuristic:** Try all 5 filters, pick the one with lowest error, then find the best shift. Fast but may not find the global optimum for a given block.
- **Brute-force:** Try all 65 combinations, pick the one with lowest total error across all 28 samples. Slow but optimal given the block constraint.

The pitfall: an encoder that picks filter/shift independently (best filter ignoring shift, then best shift for that filter) can produce worse quality than brute-force. And ANY encoder that doesn't simulate the actual decode loop (including clamping and feedback) during encoding will produce output that sounds different after hardware decode than the encoder predicted.

**Why it happens:** Encoder quality is a separate concern from decoder correctness. But if the project ships an encoder that produces poor ADPCM, users will blame the reverb for "sounding bad" when the degradation is actually in the encoding.

**Prevention:**
1. Encoder evaluates ALL 65 (filter, shift) combinations per block.
2. For each candidate, the encoder runs the actual decode loop (including clamping and feedback) to compute the true reconstruction error — not a linear approximation.
3. Error metric: sum of squared differences between original PCM and decoded PCM across the 28-sample block.
4. The encoder's internal decode loop MUST be identical to the standalone decoder. Factor into a shared function.
5. Document the encoding strategy in DECISIONS.md, including the quality-vs-speed tradeoff.

**Detection:** Encode a sine wave, decode it, compare SNR against psxavenc output.

**Phase:** P-ENCODE, P-DECISIONS

**Source:** [psxavenc](https://github.com/WonderfulToolchain/psxavenc); general ADPCM encoder design

---

### S3: Loop flag handling — the repeat-address-disable quirk

**Severity:** Significant (games depend on this; SPU-94's scope may not include it, but it must be documented)

**What goes wrong:** The ADPCM block's flag byte (byte 1) contains three meaningful bits:
- Bit 0: Loop End — jump to repeat address, set ENDX flag
- Bit 1: Loop Repeat — if set with bit 0, voice continues looping; if bit 0 alone, voice enters release/mute
- Bit 2: Loop Start — copy current block address to repeat address register

The documented quirk (from jsgroth's PS1 emulator blog, confirmed by Valkyrie Profile and Tron Bonne behavior): **if software writes to a voice's repeat address register directly, the Loop Start flag in ADPCM headers is disabled until the voice is keyed on again.** This means the hardware remembers "repeat address was set by software, not by ADPCM flag" and suppresses the flag until key-on.

For SPU-94, which implements reverb but not the full voice engine (no ADSR, no pitch, no key-on), the question is: does the ADPCM codec need to handle loop flags at all, or is that the caller's responsibility?

**Prevention:**
1. The ADPCM decoder's minimal contract: decode 16-byte blocks to 28 PCM samples. Loop flags are parsed and returned to the caller as metadata, not acted upon by the decoder.
2. The caller (future voice engine, or test harness) is responsible for acting on loop flags.
3. Document this boundary in DECISIONS.md: "ADPCM decoder reports loop flags; does not implement loop behavior. Loop behavior is out of scope for the reverb-focused codec."
4. If SPU-94 later grows a voice engine, the repeat-address-disable quirk must be implemented. Flag it in PITFALLS for that milestone.

**Detection:** N/A for the codec milestone (loop behavior is out of scope); relevant for future voice-engine work.

**Phase:** P-LOOPFLAGS, P-DECISIONS

**Source:** [jsgroth SPU Part 4](https://jsgroth.dev/blog/posts/ps1-spu-part-4/); [psx-spx SPU ADPCM](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm)

---

### S4: ADPCM as reverb input coloration — integration ordering matters

**Severity:** Significant (architectural)

**What goes wrong:** In the real PS1, the signal path is: ADPCM decode -> Gaussian interpolation -> ADSR envelope -> voice volume -> mix bus -> reverb input. The ADPCM decode step *colors* the audio before it reaches the reverb. Quantization noise from 4-bit compression, the filter's predictive errors, and the codec's frequency response all feed into the reverb and become part of the reverb's character.

If SPU-94's ADPCM module is bolted on as a separate tool (encode file, decode file, then feed PCM to reverb), the pipeline is:

```
PCM -> ADPCM encode -> ADPCM decode -> [file] -> reverb
```

This produces the correct coloration. But if the integration skips ADPCM decode (feeding the original PCM directly to the reverb), users will hear "cleaner" reverb that lacks the PS1's characteristic grit. The ADPCM is not optional coloration — it IS part of the sound.

**Why it happens:** The natural inclination is "reverb already works on PCM, why add a lossy step?" But the PS1 never feeds raw PCM to reverb — it always goes through ADPCM first.

**Prevention:**
1. libspu94's pipeline API should offer both paths: raw PCM in (for DAW use where ADPCM coloration is optional) and ADPCM-colored input (for authentic PS1 reproduction).
2. The "authentic" path decodes ADPCM on-the-fly, sample by sample, before feeding each sample to the reverb input.
3. Do NOT decode ADPCM in bulk and then process reverb in bulk — this is functionally equivalent but obscures the per-sample coloration interaction.
4. The Gaussian interpolation and ADSR envelope are NOT in scope for M2 (they are voice-engine features). ADPCM decode -> reverb is the M2 signal path. Document this simplification.

**Detection:** A/B listening test: reverb on raw PCM vs reverb on ADPCM-decoded PCM. The difference should be audible as added grit/noise floor.

**Phase:** P-INTEGRATE, P-DECISIONS

**Source:** [psx-spx SPU signal path](https://psx-spx.consoledev.net/soundprocessingunitspu/)

---

### S5: The division-by-64 is an arithmetic right shift by 6 — precision matters

**Severity:** Significant (off-by-one in filter output)

**What goes wrong:** The filter formula `(old*f0 + older*f1 + 32) / 64` involves integer division by 64. In C, integer division truncates toward zero for positive values and is implementation-defined for negative values (C99 specifies truncation toward zero, but C89 left it implementation-defined). The hardware likely performs an arithmetic right shift by 6 (truncation toward negative infinity), which differs from C's `/` operator for negative values.

Example: `(-33 + 32) / 64 = -1 / 64 = 0` in C (truncation toward zero), but `(-33 + 32) >> 6 = -1 >> 6 = -1` with arithmetic right shift (truncation toward negative infinity).

The +32 rounding bias makes this divergence less frequent (it shifts the distribution toward positive intermediates), but it doesn't eliminate it for large negative filter contributions.

**Why it happens:** Using C's `/` operator for what the hardware implements as a right shift. The libspu94 codebase already has the `_Static_assert` for arithmetic right shift (ADR-0001), so using `>> 6` is safe on the project's target compilers.

**Prevention:**
1. Implement the filter division as `>> 6`, not `/ 64`, and add a comment explaining why.
2. The existing `_Static_assert` in `spu94_q15.h` already validates arithmetic shift behavior.
3. Unit test: verify that the decode of a block with large negative filter contributions matches the `>> 6` interpretation, not the `/ 64` interpretation.
4. Document in DECISIONS.md: "ADPCM filter division implemented as `>> 6` (arithmetic right shift), matching hardware behavior. This differs from C's `/ 64` for negative values."

**Detection:** Divergence on blocks with strong negative filter feedback; rare in practice but fails bit-accuracy.

**Phase:** P-DECODE, P-DECISIONS

**Source:** Inferred from hardware behavior (Sony's SPU is a hardware shift, not a software divider); consistent with libspu94's existing ASR policy (ADR-0001)

---

### S6: Sign extension of 4-bit nibbles — the classic off-by-one

**Severity:** Significant (every sample wrong if botched)

**What goes wrong:** Each ADPCM nibble is a 4-bit signed value in the range -8 to +7 (two's complement). Sign-extending from 4 bits to 32 bits requires recognizing bit 3 as the sign bit. Common mistakes:

- Treating nibbles as unsigned 0-15 (no sign extension) — all "negative" samples become large positive values.
- Sign-extending from bit 7 instead of bit 3 (treating as int8 instead of int4) — wrong sign for values 8-15.
- Using `(int8_t)(nibble << 4) >> 4` which works but is subtle and easy to get the shift count wrong.

**Prevention:**
1. Explicit sign extension: `int32_t signed_nib = (nibble & 0x8) ? (nibble | 0xFFFFFFF0) : nibble;` — or equivalently `int32_t signed_nib = (int32_t)(int16_t)(int8_t)((nibble << 4) & 0xF0) >> 4;`
2. Simplest correct form: `int32_t signed_nib = (nibble < 8) ? nibble : nibble - 16;`
3. Unit test: verify sign extension for all 16 possible nibble values (0x0 through 0xF). This is an exhaustive test.

**Detection:** All audio sounds "bright" and "clicky" because negative nibbles become large positive values.

**Phase:** P-DECODE

**Source:** General two's-complement arithmetic; [psx-spx](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — `signed4bit()` function

---

### S7: Encoder must model decode loop exactly — encoder/decoder asymmetry

**Severity:** Significant (encoder produces non-optimal output)

**What goes wrong:** An ADPCM encoder that evaluates candidate (filter, shift) combinations using a simplified model (e.g., linear prediction without clamping) will select different parameters than one that runs the full decode loop. The difference: the simplified model doesn't account for the nonlinearity of clamping. When a sample clamps, the feedback state diverges from the linear prediction, and subsequent samples in the block are encoded against a wrong prediction.

**Why it happens:** Running the full decode loop for each candidate combination is 65x slower than a linear approximation. The temptation to approximate is strong.

**Consequences:** Encoder chooses suboptimal filter/shift for blocks where clamping occurs (loud passages, high-energy content). Output has higher distortion than necessary.

**Prevention:**
1. Factor the decode loop into a function used by BOTH the decoder and the encoder.
2. Encoder calls `decode_block(candidate_shift, candidate_filter, nibbles, &prev1, &prev2)` for each candidate and measures actual reconstruction error.
3. For encoder performance: the inner loop is 28 iterations of simple integer math — 65 * 28 = 1820 iterations per block is fast enough for offline encoding.
4. If real-time encoding is ever needed (unlikely for SPU-94's use case), the brute-force search can be narrowed by heuristic pre-filtering.

**Phase:** P-ENCODE

**Source:** General ADPCM encoder design; [adpcm-xq](https://github.com/dbry/adpcm-xq) demonstrates brute-force search for IMA-ADPCM

---

## Minor Pitfalls

### M1: One-shot samples need a terminator block

**What goes wrong:** A one-shot ADPCM sample (no loop) must end with a dummy block that has both Loop Start and Loop End flags set, with all nibbles zero. Without this terminator, the SPU voice engine continues reading past the end of the sample data, producing noise from whatever happens to be in SPU RAM.

**Prevention:** The encoder must append a terminator block for one-shot samples. The decoder should document that it expects the caller to handle termination (the decoder itself just decodes blocks as given).

**Phase:** P-ENCODE, P-LOOPFLAGS

**Source:** [psx-spx SPU ADPCM](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) — "one-shot samples must use a dummy block"

---

### M2: VAG file header parsing — big-endian fields in a little-endian ecosystem

**What goes wrong:** The VAG file format (standard PS1 ADPCM container) uses big-endian fields in its header (version, data size, sample rate) despite the PS1 being a little-endian MIPS machine. Parsing VAG headers with native-endian reads produces wrong values on little-endian hosts.

**Prevention:**
1. Read VAG header fields through explicit `read_u32_be()` helpers.
2. The raw ADPCM block data (after the header) is byte-oriented and endian-neutral.
3. Unit test: parse a known VAG file and verify header fields match expected values.

**Phase:** P-ENCODE (VAG output), P-DECODE (VAG input), P-VERIFY

**Source:** [VAG format](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation))

---

### M3: Keyed-off voices still decode ADPCM (SPU IRQ interaction)

**What goes wrong:** On real PS1 hardware, voices with volume=0 (keyed off) continue to decode ADPCM data. This matters because ADPCM decoding can trigger SPU IRQs when the decode address matches the IRQ address. Games like Casper depend on this for lip-sync animation timing.

For SPU-94 (reverb-only, no voice engine), this is out of scope. But if the ADPCM decoder is designed with an "early-exit if voice is muted" optimization, it will break this behavior when a voice engine is added later.

**Prevention:** The ADPCM decoder should not have any "skip decode if muted" logic. It always decodes when called. The caller decides whether to call it.

**Phase:** P-DECODE (design), P-DECISIONS

**Source:** [jsgroth SPU Part 4](https://jsgroth.dev/blog/posts/ps1-spu-part-4/) — "keyed off voices can still trigger SPU IRQs"

---

## Integration Pitfalls (ADPCM + existing libspu94)

### I1: ADPCM module must not break existing RT-safety guarantees

**What goes wrong:** The existing libspu94 core passes 4 rt_safety ctest targets (no heap, no locks, no syscalls, bounded latency). Adding ADPCM decode/encode code that calls `malloc`, uses `printf` for debug, or has unbounded loops would fail these gates.

**Prevention:**
1. ADPCM decode: pure function, no allocations, no state beyond the caller-provided struct. Follows the same pattern as `spu94_process()`.
2. ADPCM encode: may be offline-only (not RT-safe), but if included in libspu94.so, it must still pass the rt_safety gates. If encode is slow (brute-force search), it should be a separate compilation unit excluded from the RT binary, or clearly documented as non-RT.
3. Run all 4 existing rt_safety tests after ADPCM integration. They must still pass.

**Phase:** P-INTEGRATE, P-VERIFY

**Source:** Existing libspu94 constraints; `rt_safety` ctest targets

---

### I2: ADPCM state is per-voice, not per-reverb-instance

**What goes wrong:** The reverb has one state object (`spu94_state`). ADPCM decode state (prev1, prev2, current block address, loop address) is per-voice — the PS1 has 24 voices. If ADPCM state is crammed into the reverb state struct, the API becomes confused: reverb is a shared resource, ADPCM is per-voice.

**Prevention:**
1. ADPCM state is a separate struct: `spu94_adpcm_state` (or `spu94_voice_state` if it grows to include pitch/envelope later).
2. Caller allocates one per voice (or one per ADPCM stream in the non-voice use case).
3. The reverb API accepts PCM input — the caller is responsible for running ADPCM decode and feeding PCM to the reverb.
4. Integration helper (convenience function): `spu94_adpcm_decode_to_reverb()` that decodes one block and feeds the output to the reverb input — but this is a composition of two independent APIs, not a merged one.

**Phase:** P-INTEGRATE, P-DECODE (API design)

**Source:** PS1 architecture — 24 voices share one reverb unit

---

### I3: Float-free ADPCM — no floating point creep from encoder optimization

**What goes wrong:** The encoder's brute-force search involves computing error metrics. A natural implementation computes MSE as `float mse = (float)total_sq_error / 28.0f;`. If this float code ends up in `libspu94.so`, it breaks the float-free CI gate (`grep -E '\b(float|double)\b'` in core sources).

**Prevention:**
1. Encoder error metric: use integer sum-of-squared-errors (`int64_t`), compared as raw sums without division. Division by 28 is unnecessary for comparison (same divisor for all candidates).
2. If the encoder must live in a separate compilation unit that allows float (for analysis/reporting), ensure it is NOT linked into `libspu94.so`.
3. The decoder is trivially float-free (all integer arithmetic).

**Phase:** P-ENCODE

**Source:** Existing libspu94 CI gate; ADR-0001

---

## Verification Strategies for ADPCM Bit-Accuracy

### V1: Known-answer test vectors

Generate ADPCM blocks with hand-computed expected output:
- All-zero nibbles with each filter (0-4) — verifies filter coefficients
- Maximum positive nibble (+7) at each shift (0-12) — verifies shift range
- Maximum negative nibble (-8) at each shift — verifies sign extension + shift
- Shift 13, 14, 15 — verifies the edge-case policy
- Block that causes intermediate overflow — verifies clamping order
- Two consecutive blocks — verifies state carry

### V2: Round-trip test (encode then decode)

Encode a known PCM signal (sine wave, impulse, white noise), decode it, measure SNR. Compare SNR against psxavenc + a known-good decoder.

### V3: Witness comparison (audio-level, not source-level)

Decode the same ADPCM data with SPU-94 and with a witness (psxavenc's decoder, or audio captured from a PS1 emulator). Compare decoded PCM sample-by-sample. Acceptable divergence: 0 (bit-exact) for the main decode path; document any divergence on shift 13-15 edge cases.

### V4: Hardware capture (M5 target)

Use Anthony's PS1 to:
1. Upload known ADPCM data to SPU RAM via homebrew
2. Key on a voice with volume=max, no ADSR modulation, pitch=1:1 (sample rate = native)
3. Capture the digital output (SPU capture buffers or DAC output)
4. Compare captured samples against SPU-94's decode output

This is the gold standard for resolving the shift 13-15 question and any other undocumented edge cases.

### V5: Encoder quality regression test

Encode a reference WAV file, store the resulting ADPCM as a golden file. On each commit, re-encode and verify bit-identical ADPCM output. This catches accidental changes to filter/shift selection.

---

## Phase-Specific Warnings

| Phase Topic | Likely Pitfall | Mitigation |
|-------------|---------------|------------|
| P-DECODE | Shift 13-15 UB in C (negative shift) | Clamp shift, document choice, test edge cases |
| P-DECODE | Wrong nibble order (hi/lo swap) | Test against known VAG decode |
| P-DECODE | Omit +32 rounding bias | Implement formula verbatim from nocash |
| P-DECODE | Premature clamping | Single clamp point after full expression |
| P-DECODE | `/64` vs `>>6` for negative values | Use `>>6`, validated by existing _Static_assert |
| P-ENCODE | Simplified error model (no clamping in eval) | Shared decode function for encoder and decoder |
| P-ENCODE | Float creep in error metric | int64_t sum-of-squares, no division |
| P-ENCODE | Missing terminator block for one-shot | Append dummy block with loop start+end flags |
| P-INTEGRATE | ADPCM state in reverb struct | Separate struct, per-voice not per-reverb |
| P-INTEGRATE | Breaking rt_safety gates | Run existing 4 rt_safety tests after integration |
| P-LOOPFLAGS | Acting on loop flags in decoder | Report only; caller acts |
| P-VERIFY | Testing against only one witness | Use at least 2 independent witnesses |
| P-DECISIONS | Not logging the +32 rounding policy | DECISIONS.md entry for ADPCM arithmetic |

---

## Technical Debt Patterns

| Shortcut | Immediate Benefit | Long-term Cost | When Acceptable |
|----------|-------------------|----------------|-----------------|
| Omit shift 13-15 handling | Fewer branches | UB on adversarial input; untested code path | Never — must handle, even if behavior is guessed |
| Skip the +32 rounding | "Consistent with reverb truncation" | Every sample off by up to 1 LSB; compounds through feedback | Never — spec says +32, implement +32 |
| Encode with float error metric | Cleaner code | Float creep into libspu94; CI gate failure | Only in a separate tool binary, never in libspu94 |
| Merge ADPCM state into reverb state | One struct to manage | Wrong abstraction; blocks future voice-engine work | Never — separate structs |
| Read a GPL decoder "just to check" | Fast resolution of ambiguity | Derivative-work risk; license contamination | Never — use audio-level witness comparison |
| Approximate encoder (skip decode simulation) | Faster encoding | Suboptimal quality on clamping-heavy blocks | Acceptable for draft/preview; not for final encode |

---

## Recovery Strategies

| Pitfall | Recovery Cost | Recovery Steps |
|---------|---------------|----------------|
| Shift 13-15 wrong | LOW | Change one conditional; re-run tests; update DECISIONS.md |
| +32 rounding omitted | MEDIUM | Add +32; regenerate all ADPCM golden files; re-audit witness diffs |
| Wrong nibble order | MEDIUM | Swap extraction; all decoded audio changes; regenerate goldens |
| Wrong clamping order | MEDIUM | Restructure decode loop; regenerate goldens |
| GPL contamination discovered | HIGH-CATASTROPHIC | Rewrite decode loop clean-room with a second reviewer; document provenance; may require relicensing |
| Encoder selects wrong filters | LOW | Improve search; re-encode test vectors; no decoder change needed |

---

## Sources

### Primary (HIGH confidence)
- [psx-spx XA-ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — decode formula, filter coefficients, shift handling
- [psx-spx SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) — block format, flag bytes, loop handling
- [psx-spx SPU documentation](https://psx-spx.consoledev.net/soundprocessingunitspu/) — signal path, capture buffers, SPU architecture

### Emulator developer blogs (MEDIUM-HIGH confidence)
- [jsgroth SPU Part 1 — ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — decode implementation details, shift edge cases
- [jsgroth SPU Part 4 — Everything Else](https://jsgroth.dev/blog/posts/ps1-spu-part-4/) — loop quirks, repeat address disable, capture buffers, IRQ interaction

### Related codec documentation (MEDIUM confidence)
- [SnesLab BRR (Bit Rate Reduction)](https://sneslab.net/wiki/Bit_Rate_Reduction) — SNES cousin of PS1 ADPCM, shift 13-15 hardware behavior documented
- [SNESdev BRR samples](https://snes.nesdev.org/wiki/BRR_samples) — filter coefficients and edge cases for the related SNES codec

### Tools and test infrastructure (MEDIUM confidence)
- [psxavenc](https://github.com/WonderfulToolchain/psxavenc) — open-source PS1 ADPCM encoder
- [ps1-tests](https://github.com/JaCzekanski/ps1-tests) — PS1 hardware test suite (limited SPU coverage)
- [VAG format spec](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation)) — file format details

### Existing libspu94 architecture (HIGH confidence — project-internal)
- `include/spu94/spu94_q15.h` — existing ASR policy, _Static_assert for arithmetic shift
- `docs/DECISIONS.md` — ADR-0001 (truncation policy), ADR-0004 (error observation)
- `.planning/PROJECT.md` — licensing constraints, witness-only policy

---

*Pitfalls research for: ADPCM encode/decode milestone for libspu94*
*Researched: 2026-04-26*
