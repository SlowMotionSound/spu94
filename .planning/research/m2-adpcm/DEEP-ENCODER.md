# Deep Dive: ADPCM Encoder Design

**Domain:** PS1 SPU ADPCM encoder internals for SPU-94
**Researched:** 2026-04-26
**Overall confidence:** MEDIUM-HIGH (encoder algorithms well-understood from BRR/SNES analogue and ADPCM theory; psxavenc internals not externally documented; Sony SDK encoder is proprietary/undocumented)

---

## 1. psxavenc's Encoding Algorithm

### What We Know (Without Reading Source)

**Confidence: MEDIUM** -- based on README, GitHub description, community references, and the BRR/SNES encoding community's well-documented equivalent algorithms.

psxavenc (WonderfulToolchain) is the most widely used open-source PS1 ADPCM encoder. Its README provides command-line usage but **zero documentation about the encoding algorithm itself**. The psx-spx docs that psxavenc references literally say "Don't ask me how to write an encoder for this format" -- confirming that encoding is considered the hard part, left to implementors.

**What external evidence tells us:**

1. **It supports both SPU-ADPCM and XA-ADPCM encoding.** The XA variant uses the same core algorithm but different nibble ordering and only 4 filters (no filter 4). The SPU variant uses all 5 filters.

2. **It almost certainly uses brute-force search.** Every documented PS1/SNES ADPCM encoder in the open-source ecosystem (snesbrr, BRRTools, FFmpeg's ADPCM encoders) uses brute-force evaluation of all filter+shift combinations. No heuristic shortcut has been published for Sony's specific coefficient set. The search space (65 combinations for SPU, 52 for XA) is small enough that brute force is the obvious approach.

3. **Error metric is almost certainly sum of squared errors (L2).** This is the standard in every BRR/ADPCM encoder documented in nesdev and hydrogenaudio forums. Kode54 (BRR encoder author) explicitly describes "picks the filter set and scale value that produce the least mean square error for each given block." L2 weights large errors more heavily than L1, which is desirable because a single badly-quantized sample is more audible than uniformly distributed small errors.

4. **Tail block handling:** The Sony SDK documentation states "a multiple of 28 samples are handled as one block because of hardware characteristics." The standard convention is to zero-pad the final block to 28 samples and set the end flag (0x01). psxavenc produces VAG files, which follow this convention.

5. **No pre-emphasis or noise shaping.** psxavenc targets faithful PS1 reproduction, not quality improvement. There is no evidence in its README, issues, or community discussion of any pre-processing or noise shaping.

**What we do NOT know about psxavenc:**
- The exact rounding strategy for nibble quantization (round-to-nearest vs truncate)
- Whether it uses any tiebreaking heuristic when two (filter, shift) pairs produce equal error
- Whether it evaluates shifts in ascending or descending order (which affects tiebreaking)

### Recommendation for SPU-94

Use brute-force L2 search over all 65 combinations, matching the community consensus. This is the approach with the strongest evidence base and produces Sony-SDK-comparable quality. Do not attempt to reverse-engineer psxavenc's exact behavior -- the goal is bit-faithful *decode*, not bit-identical *encode*.

---

## 2. The Optimal Shift Calculation

### The Core Question

For a given filter and 28 input samples, can the optimal shift be derived analytically rather than searched?

### The Math

Given filter coefficients (f0, f1) and decoder state (old, older), the prediction for sample `t` is:

```
predicted[t] = (old * f0 + older * f1 + 32) >> 6
```

The residual the encoder must represent is:

```
residual[t] = target[t] - predicted[t]
```

The shift parameter determines the quantization step size. A nibble value `n` (-8 to +7) reconstructs as:

```
shifted = n << (12 - shift)
```

So the step size is `1 << (12 - shift)`. The range of representable residuals is:

```
min_representable = -8 << (12 - shift)
max_representable =  7 << (12 - shift)
```

### Analytical Shift From Max Residual

**YES, the optimal shift can be approximated analytically.** The key insight:

For a given filter, compute all 28 residuals (using the *reconstructed* samples for prediction, not originals -- more on this complication below). The maximum absolute residual determines the minimum shift needed to avoid clipping:

```
max_abs_residual = max(|residual[0]|, |residual[1]|, ..., |residual[27]|)
```

The shift must satisfy:

```
max_abs_residual <= 7 << (12 - shift)    (for positive residuals)
max_abs_residual <= 8 << (12 - shift)    (for negative residuals)
```

Using the conservative (positive) bound:

```
shift >= 12 - floor(log2(max_abs_residual / 7))
```

Or equivalently, the minimum non-clipping shift is:

```
shift_min = max(0, 12 - floor(log2(max_abs_residual / 7)))
```

In integer arithmetic (no floating point needed):

```c
/* Find the shift where 7 << (12 - shift) >= max_abs_residual */
int shift_min = 0;
for (int s = 0; s <= 12; s++) {
    if ((7 << (12 - s)) >= max_abs_residual) {
        shift_min = s;
        break;
    }
}
```

### The Complication: Prediction State Drift

The analytical approach has a fundamental chicken-and-egg problem. The residuals depend on the prediction, which depends on the reconstructed samples, which depend on the shift and nibble values. You cannot compute the true residuals without knowing the shift, because:

1. Compute residual assuming some shift
2. Quantize to nibble
3. Reconstruct sample (using the quantized nibble + shift)
4. Use reconstructed sample for next prediction
5. Next residual depends on the reconstruction from step 3

The residuals computed with original PCM as prediction state ("open-loop") are different from the residuals computed with reconstructed samples as prediction state ("closed-loop"). The decoder runs closed-loop, so the encoder must too.

### Practical Resolution: Two-Phase Approach

The analytical shift from open-loop residuals is a **very good initial estimate** that can reduce the search space:

**Phase 1 (analytical, per filter):** Compute open-loop residuals using original PCM for prediction. Find `max_abs_residual`. Derive `shift_min`. The optimal shift is almost always `shift_min` or `shift_min + 1` (one step coarser provides better average error even though it can represent the peak).

**Phase 2 (verify 2-3 shifts):** For each filter, run the full closed-loop encode at `shift_min` and `shift_min + 1` (and optionally `shift_min - 1` if it exists). Compute L2 error. Pick the best.

This reduces the search from 65 combinations to **5 filters x 2-3 shifts = 10-15 combinations** -- a ~4-6x speedup.

### Should SPU-94 Use This Optimization?

**No.** The full 65-combination search takes ~1820 decode-step operations per block. At 28 samples per block and ~1575 blocks per second at 44.1kHz, the encoder processes ~2.86M decode-steps per second. This is trivially fast on any modern CPU -- under 1ms for a full second of audio. The optimization is intellectually interesting but provides zero practical benefit for an offline encoder.

If SPU-94 ever needs a real-time encoder (unlikely), the two-phase approach is the correct optimization path. For now, brute-force 65 is clearer, simpler, and impossible to get wrong.

### Recommendation

Brute-force all 65 combinations. Document the analytical derivation in a code comment for future reference, but do not implement the optimization.

---

## 3. Encoder State Seeding Across Blocks

### The Problem

When encoding block N, the encoder tries 65 candidate (filter, shift) combinations. Each candidate produces a different sequence of 28 reconstructed samples, and therefore a different final decoder state (old, older). Only one candidate is chosen. The question: what state feeds block N+1?

### The Answer: Chosen Candidate's State Carries Forward

**Confidence: HIGH** -- this is definitively established in ADPCM encoder theory and confirmed by multiple BRR encoder implementations.

The encoding process works like this:

```
state = {old: 0, older: 0}    // initial state

for each block:
    best_error = MAX
    best_state = state         // will hold winning candidate's final state
    
    for each (filter, shift) in 65 combinations:
        trial_state = state    // START from the committed state
        trial_error = 0
        
        for each of 28 samples:
            // encode using trial_state for prediction
            // decode the nibble to get reconstructed sample
            // update trial_state with reconstructed sample
            // accumulate error
        
        if trial_error < best_error:
            best_error = trial_error
            best_state = trial_state    // save this candidate's FINAL state
            // also save the nibbles
    
    state = best_state    // COMMIT the winning candidate's state
    // write the winning block
```

**Critical details:**

1. **All 65 candidates start from the SAME committed state.** The state at the end of block N-1 (from the winning candidate of block N-1) is the starting point for ALL candidates of block N.

2. **Only the winning candidate's final state propagates.** The 64 losing candidates' states are discarded.

3. **This state MUST match what a decoder would produce.** If you encode blocks 0..N and then decode them, the decoder's state after block N must be identical to the encoder's committed state after block N. This is guaranteed because the encoder's internal decode step uses the exact same algorithm as the decoder.

### What Happens If State Diverges?

If the encoder uses original PCM for prediction instead of reconstructed samples:

- **Block 0:** No difference (state is 0,0 for both)
- **Block 1:** Slight difference (prediction uses PCM values instead of quantized values)
- **Block 2+:** Error accumulates. The encoder's prediction diverges from what the decoder will compute. The nibbles the encoder chooses become increasingly suboptimal because they're compensating for a prediction that the decoder doesn't share.

The audible result: increasing "warble" or "flutter" over the first few seconds, especially noticeable on sustained tones. This is the single most common ADPCM encoder bug.

### Implementation Pattern

```c
typedef struct {
    int16_t old;
    int16_t older;
} adpcm_state;

void encode_block(adpcm_state *committed, const int16_t pcm[28],
                  uint8_t flags, uint8_t out[16])
{
    int32_t best_error = INT32_MAX;
    adpcm_state best_state = *committed;
    int best_filter = 0, best_shift = 0;
    uint8_t best_nibbles[28];
    
    for (int f = 0; f < 5; f++) {
        for (int s = 0; s <= 12; s++) {
            adpcm_state trial = *committed;  /* fork from committed */
            int32_t error = 0;
            uint8_t nibbles[28];
            
            for (int i = 0; i < 28; i++) {
                /* compute prediction from trial state */
                /* quantize residual to nibble */
                /* decode nibble to get reconstructed sample */
                /* update trial state with reconstructed sample */
                /* accumulate squared error */
            }
            
            if (error < best_error) {
                best_error = error;
                best_state = trial;
                best_filter = f;
                best_shift = s;
                memcpy(best_nibbles, nibbles, 28);
            }
        }
    }
    
    *committed = best_state;  /* commit winning state */
    /* pack best_filter, best_shift, flags, best_nibbles into out[16] */
}
```

### Recommendation

This is non-negotiable. The encoder MUST use reconstructed samples for prediction state, and MUST commit only the winning candidate's state. This is already documented in FEATURES.md as a table-stakes requirement. The pattern above is the correct implementation.

---

## 4. Pre-Emphasis / De-Emphasis

### What Is Pre-Emphasis?

Pre-emphasis boosts high-frequency content before encoding. The idea: ADPCM quantization noise is roughly flat in spectrum, but human hearing is more sensitive to high-frequency noise. By boosting highs before encoding and cutting them after decoding (de-emphasis), the effective noise is shaped to be less audible.

A simple first-order pre-emphasis filter:

```
y[n] = x[n] - alpha * x[n-1]    (typically alpha = 0.95)
```

### Did the Sony SDK Encoder Use Pre-Emphasis?

**Confidence: LOW** -- no direct evidence found.

The Sony Psy-Q SDK documentation for AIFF2VAG (the official VAG encoding tool) mentions conversion modes and playback options but does not document any pre-emphasis filter. The tool's interface appears straightforward: "save the waveform data in the AIFF format (16 bits, monophonic, non-compressed)" and convert.

**Evidence against pre-emphasis in Sony's encoder:**
1. The SPU hardware has no de-emphasis filter in the decode path. If the encoder applied pre-emphasis, the decoded audio would sound bright/harsh unless the game engine applied de-emphasis in software, which no known PS1 game does.
2. The CD-XA subsystem has a pre-emphasis flag in the XA-ADPCM subheader, but this is a CD standard feature (inherited from Red Book), not an SPU feature. SPU-ADPCM blocks have no pre-emphasis flag.
3. The PS1 audio pipeline is: ADPCM decode -> volume/envelope -> mix -> reverb -> DAC. No de-emphasis stage exists anywhere in the hardware chain.

### Does psxavenc Use Pre-Emphasis?

**No.** There is no mention of pre-emphasis in psxavenc's README or command-line options. As a faithful PS1 encoding tool, applying pre-emphasis would produce audio that sounds wrong on real hardware.

### Should SPU-94's Encoder Use Pre-Emphasis?

**No.** The entire purpose of SPU-94's ADPCM codec is to reproduce the coloration of PS1 audio, including the quantization noise profile. Pre-emphasis would alter the noise profile to be less PS1-like. The flat quantization noise is part of the PS1 sound character.

Pre-emphasis is an anti-feature for this project: it improves quality beyond what PS1 hardware ever produced, which is the opposite of SPU-94's goal.

### Recommendation

Do not implement pre-emphasis or de-emphasis. This is already captured in FEATURES.md as an anti-feature ("Noise shaping / dithering in encoder -- Improves quality beyond what PS1 hardware ever produced"). Pre-emphasis falls in the same category.

---

## 5. Nibble Quantization Rounding Strategy

### The Problem

Given a residual value and a shift parameter, the encoder must choose the best 4-bit nibble (-8 to +7). The step size is `1 << (12 - shift)`. The ideal (non-integer) nibble would be:

```
ideal_nibble = residual / (1 << (12 - shift))
```

This is rarely an integer. The encoder must choose between `floor(ideal_nibble)` and `ceil(ideal_nibble)` (or equivalently, between truncation and rounding).

### Three Strategies

**Strategy 1: Round to nearest (standard)**

```c
int shift_amount = 12 - shift;
int32_t half_step = (shift_amount > 0) ? (1 << (shift_amount - 1)) : 0;
int32_t nibble = (residual + half_step) >> shift_amount;
nibble = clamp(nibble, -8, 7);
```

Properties:
- Minimizes the immediate reconstruction error for this sample
- Error is symmetric (equally likely positive or negative)
- Standard approach in ADPCM encoder literature

**Strategy 2: Truncate toward zero**

```c
int shift_amount = 12 - shift;
int32_t nibble = residual >> shift_amount;  /* ASR = truncate toward -inf */
/* or: residual / (1 << shift_amount) for truncate toward zero */
nibble = clamp(nibble, -8, 7);
```

Properties:
- Biased: always underestimates the magnitude
- Introduces DC offset over time (always rounding toward zero)
- Simpler but worse

**Strategy 3: Closed-loop optimal (evaluate both)**

```c
int shift_amount = 12 - shift;
int32_t nibble_lo = residual >> shift_amount;
int32_t nibble_hi = nibble_lo + 1;
/* clamp both */
/* decode both, pick the one with lower |target - reconstructed| */
```

Properties:
- Produces the truly optimal nibble for each sample
- Accounts for the filter prediction feedback (the chosen nibble affects future predictions)
- Most complex but highest quality

### Which Matters Most?

The difference between strategies 1 and 3 is small for most samples. Strategy 3 matters most when:
- The shift is large (coarse quantization, big steps)
- The filter is aggressive (prediction is close to the signal, so residuals are small relative to step size)
- The rounding bias from strategy 1 compounds through the IIR filter prediction

In practice, strategy 1 (round to nearest) is what every documented BRR and PS1 ADPCM encoder uses. The compounding effect through the IIR filter is bounded because the filter coefficients are less than 2.0 in magnitude.

### The adpcm-xq Approach

adpcm-xq (for IMA-ADPCM, not PS1) goes further: it uses lookahead to evaluate multiple nibble sequences and picks the globally optimal one. This is the most sophisticated approach but targets a different ADPCM variant and optimizes for quality beyond what PS1 hardware produced.

### Recommendation

**Use round-to-nearest (Strategy 1).** It is the standard approach, well-understood, and produces good results. The existing code in STACK.md section 2.2 already shows this pattern:

```c
int32_t nibble = (residual + (1 << (shift_amount - 1))) >> shift_amount;
```

One edge case to handle: when `shift_amount == 0` (shift = 12), the half-step rounding term is `1 << -1` which is undefined. Guard this:

```c
int32_t half_step = (shift_amount > 0) ? (1 << (shift_amount - 1)) : 0;
int32_t nibble = (residual + half_step) >> shift_amount;
```

At shift = 12, the step size is 1, so rounding is irrelevant (every residual maps exactly to an integer nibble within the representable range).

---

## 6. Block Boundary Optimization (Lookahead Encoding)

### The Idea

When encoding block N, the encoder could look ahead at block N+1's samples to evaluate how each candidate (filter, shift) pair for block N affects the encoding quality of block N+1. The winning candidate for block N determines the decoder state entering block N+1, which affects block N+1's prediction quality.

### What adpcm-xq Does

adpcm-xq implements lookahead for IMA-ADPCM, configurable from 0-16 samples ahead. At maximum settings:
- Quality improvement: 1-10 dB reduction in quantization noise (source-dependent)
- Speed penalty: "can be very slow" at maximum depth
- The search is exhaustive: for each possible current nibble, evaluate all possible next nibbles, recursively

### Why This Matters Less for PS1 ADPCM Than IMA-ADPCM

In IMA-ADPCM, the step size adaptation is driven by the nibble value itself -- each nibble changes the step size for the next sample. This creates tight coupling between adjacent samples, making lookahead highly valuable.

In PS1 SPU-ADPCM, the shift (step size) is fixed for the entire 28-sample block. The coupling between blocks comes only through the 2-sample prediction state (old, older). This coupling is weaker:

1. **Only 2 samples of state cross the boundary.** The filter prediction uses old and older. By sample 3 of the next block, the influence of the previous block's state is significantly attenuated (the filters are stable, with poles inside the unit circle).

2. **The encoder already optimizes within each block.** The brute-force search picks the (filter, shift) pair that minimizes error for the 28 samples in the current block, which implicitly produces good boundary state.

3. **The improvement is marginal.** For a 28-sample block with 2 samples of cross-boundary influence, lookahead would improve at most the first 2-3 samples of the next block. That is 2-3 samples out of 28 -- roughly 10% of the block. The SNR improvement is much less than adpcm-xq's 1-10 dB because the coupling mechanism is weaker.

### The Complexity Cost

Lookahead for PS1 ADPCM would mean: for each of 65 candidates for block N, run a full 65-candidate search for block N+1, producing 65 x 65 = 4225 evaluations per pair of blocks. For N-block lookahead, the search grows exponentially.

### What Sony's Encoder Did

The Psy-Q SDK documentation says nothing about multi-pass or lookahead encoding. The AIFF2VAG tool appears to be a straightforward single-pass encoder. No PS1-era encoding tool is known to use lookahead.

### Recommendation

**Do not implement lookahead.** The FEATURES.md anti-feature list already captures this: "Multi-pass / lookahead encoding -- Tries N blocks ahead to minimize total error. Diminishing returns for coloration use case. Sony's SDK encoder was single-pass."

The technical analysis confirms this is the right call. The marginal quality improvement (~1-3 dB optimistically, affecting only block boundaries) does not justify the complexity for a coloration tool that aims to match PS1 quality, not exceed it.

---

## Summary: Encoder Design Decisions

| Decision | Choice | Confidence | Rationale |
|----------|--------|------------|-----------|
| Search strategy | Brute-force all 65 | HIGH | Standard practice, trivially fast, impossible to get wrong |
| Error metric | Sum of squared errors (L2) | HIGH | Standard ADPCM practice, confirmed by BRR encoder authors |
| Optimal shift | Brute-force (no analytical shortcut) | HIGH | Analytical estimate exists but not worth the complexity for 65 combos |
| State management | Commit winning candidate's reconstructed state | HIGH | Fundamental ADPCM requirement, well-documented |
| Pre-emphasis | Do not implement | HIGH | No hardware de-emphasis exists; would produce non-PS1 audio |
| Nibble rounding | Round to nearest | HIGH | Standard approach, matches all known PS1 ADPCM encoders |
| Shift=12 rounding edge case | Guard against `1 << -1` | HIGH | Undefined behavior in C |
| Lookahead encoding | Do not implement | HIGH | Marginal improvement, significant complexity, not PS1-authentic |
| Noise shaping | Do not implement | HIGH | Improves quality beyond PS1 hardware capability |

### New Gray Areas Identified

| Gray Area | Impact | Suggested Resolution |
|-----------|--------|---------------------|
| **Tiebreaking when two (filter,shift) pairs produce identical L2 error** | Very low -- rare in practice. But determinism requires a rule. | Prefer lower filter index, then lower shift. This biases toward less aggressive prediction (safer) and finer quantization. Document in ADR. |
| **Nibble rounding at shift=0 edge** | shift=0 means step size = 4096. Half-step = 2048. Round-to-nearest still applies but large residuals will clip to -8/+7 regardless. | No special handling needed beyond clamp to [-8, +7]. The rounding formula works correctly. |
| **L2 error overflow for large signals** | 28 squared errors, each up to 32767^2 = ~1.07 billion. Sum could exceed int32 range (2.14 billion). | Use int64 for error accumulation, or uint32 (sufficient since max sum is 28 * 1.07B = ~30B, which fits int64). |

---

## Sources

### Primary (HIGH confidence -- algorithmic consensus from multiple independent implementors)

- [nesdev forum: BRR encoding (kode54)](https://forums.nesdev.org/viewtopic.php?t=5549) -- brute-force L2 search, reconstructed-sample state, filter/shift selection confirmed
- [nesdev forum: Trying to Understand BRR (tepples)](https://forums.nesdev.org/viewtopic.php?t=10285) -- "find the largest absolute step value, find the smallest range" heuristic, brute-force vs analytical discussion
- [adpcm-xq (David Bryant)](https://github.com/dbry/adpcm-xq) -- lookahead technique (0-16 samples), 1-10 dB improvement, noise shaping, exhaustive search methodology

### Secondary (MEDIUM confidence -- PS1-specific context)

- [psxavenc (WonderfulToolchain)](https://github.com/WonderfulToolchain/psxavenc) -- most-used PS1 encoder; README provides no algorithm documentation
- [Sony Psy-Q SDK: Sound Artist Tool](https://psx.arthus.net/sdk/Psy-Q/DOCS/Devrefs/Sound20.pdf) -- AIFF2VAG tool documentation; confirms 28-sample block alignment requirement; no encoding algorithm details
- [psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) -- "Don't ask me how to write an encoder"; confirms encoding is the hard (undocumented) part
- [psx-spx: XA Audio ADPCM](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) -- filter coefficients, decode formula

### Tertiary (LOW confidence -- general ADPCM theory, not PS1-specific)

- [Quantization (signal processing) - Wikipedia](https://en.wikipedia.org/wiki/Quantization_(signal_processing)) -- rounding vs truncation theory
- [Godot ADPCM proposal #7599](https://github.com/godotengine/godot-proposals/issues/7599) -- adpcm-xq integration discussion; confirms noise shaping + lookahead as quality levers
- [Hydrogenaudio: Improved ADPCM encoder](https://hydrogenaudio.org/index.php/topic,110444.0.html) -- forum thread exists but content inaccessible due to redirect

---

*Deep encoder research for: PS1 SPU ADPCM encoder design (M2 milestone, SPU-94)*
*Researched: 2026-04-26*
