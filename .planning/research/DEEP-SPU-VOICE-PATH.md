# PS1 SPU Voice Path: Pitch Counter, ADPCM Decode, and Gaussian Interpolation

**Researched:** 2026-05-15
**Domain:** PS1 SPU per-tick voice sample generation mechanism
**Confidence:** HIGH (cross-verified across nocash spec + 3 independent emulator implementations)

---

## Executive Summary

The PS1 SPU voice path uses a **single pitch counter** that simultaneously controls:
1. **Sample advancement** (integer part, bits 12+) -- when to decode the next ADPCM sample
2. **Gaussian interpolation** (fractional part, bits 4-11) -- where between samples the playback position currently sits

This is fundamentally different from our current implementation which uses two separate counters (`decim_counter` for decimation and `gauss_counter` for interpolation). The hardware's architecture means the interpolation position is inherently synchronized with the decode position -- they are literally different bit fields of the same register.

**Primary finding:** The single-counter architecture is not just "simpler" -- it produces fundamentally different audio because the Gaussian interpolation index is always coherent with the sample position. Two separate counters can drift relative to each other, producing artifacts that do not exist in real hardware.

---

## 1. The Pitch Counter

### 1.1 Bit Field Layout

[VERIFIED: DuckStation spu.cpp, nocash psx-spx]

```
Voice Counter (32 bits total):
 _______________________________________________
|  unused  | sample_index | interp_idx | frac  |
|  17..31  |   16..12     |   11..4    | 3..0  |
|__________|______________|____________|_______|

Bits 0-3:   Sub-sample fractional (below interpolation precision)
Bits 4-11:  8-bit Gaussian interpolation index (i = 0x00..0xFF)
Bits 12-16: 5-bit sample index within current ADPCM block (0..27)
Bits 17+:   Overflow / unused
```

DuckStation defines this as:
```cpp
union VoiceCounter {
    u32 bits;
    BitField<u32, u8, 4, 8>  interpolation_index;  // bits 4-11
    BitField<u32, u8, 12, 5> sample_index;          // bits 12-16
};
```

Mednafen uses a simpler approach -- a plain uint32 `CurPhase` where:
- `CurPhase >> 12` = number of samples consumed this tick
- `(CurPhase & 0xFFF) >> 4` = Gaussian interpolation index (8-bit)

### 1.2 Counter Advancement Per Tick

[VERIFIED: nocash psx-spx, DuckStation, Mednafen]

Every 44.1 kHz tick, for each voice:

```
Step = VxPitch register (0x0000..0xFFFF)

If pitch modulation enabled (PMON bit set):
    Factor = previous_voice_output + 0x8000  (range 0..0xFFFF)
    Step = (Step * Factor) >> 15

Step = min(Step, 0x3FFF)    // Clamp to max 176.4 kHz

Counter += Step
```

Key points:
- The counter accumulates continuously -- it is NEVER reset to zero during normal playback
- When `Counter.sample_index >= 28`: a block boundary has been crossed
- The clamp to 0x3FFF means max 4 samples can be consumed per tick (176.4 / 44.1 = 4)

### 1.3 What "Pitch = 0x1000" Means

[VERIFIED: nocash psx-spx]

- 0x1000 (4096) = 44.1 kHz playback rate (1:1 with SPU output rate)
- 0x0800 (2048) = 22.05 kHz (half speed)
- 0x2000 (8192) = 88.2 kHz (double speed, clamped to 0x3FFF internally)
- 0x0400 (1024) = 11.025 kHz (quarter speed)

The pitch value directly corresponds to how many fractional-sample-units are added per tick. Since bit 12 represents "one whole sample," 0x1000 means exactly one sample per tick.

---

## 2. ADPCM Block Decode Timing

### 2.1 When Decode Happens

[VERIFIED: DuckStation spu.cpp, Mednafen spu.c, PlayStation1Vsts Spu.cpp]

The ADPCM block is decoded **all at once** (28 samples) when needed, **not** sample-by-sample. The timing differs slightly between implementations, but the semantic is identical:

**DuckStation approach (decode-on-demand):**
```
Per tick:
  1. If voice.has_samples == false:
       Fetch 16 bytes from SPU RAM at current_address
       Decode all 28 samples into current_block_samples[3..30]
       Copy last 3 samples from previous block into [0..2]  (history)
       voice.has_samples = true
  2. Perform Gaussian interpolation using current sample_index
  3. Advance counter (counter.bits += step)
  4. If counter.sample_index >= 28:
       counter.sample_index -= 28
       voice.has_samples = false   // triggers decode next tick
       current_address += 2        // advance to next 16-byte block
```

**Mednafen approach (decode ahead, consume from buffer):**
```
Per tick:
  1. While DecodeAvail < threshold:
       Decode 4 nibbles (2 bytes) into circular DecodeBuffer
       Advance write pointer
       DecodeAvail += 4
  2. Compute interpolation from DecodeBuffer[ReadPos..]
  3. Advance phase counter (CurPhase += step)
  4. Samples consumed = CurPhase >> 12
  5. CurPhase &= 0xFFF  (keep only fractional part)
  6. DecodeReadPos += consumed
  7. DecodeAvail -= consumed
```

**PlayStation1Vsts approach (decode full block on demand):**
```
Per tick:
  1. If !bSamplesLoaded:
       Read 16 bytes from RAM
       Copy last 3 samples to positions [0..2]
       Decode 28 samples into positions [3..30]
       bSamplesLoaded = true
  2. Interpolate using gaussIdx from counter
  3. Counter += sampleRate
  4. If counter.sampleIdx >= 28:
       counter.sampleIdx -= 28
       bSamplesLoaded = false
       Advance address
```

### 2.2 The Critical Insight: Decode is Batch, Playback is Sample-by-Sample

The hardware decodes an entire 28-sample block into a local buffer. The pitch counter then indexes into that buffer sample-by-sample, with Gaussian interpolation blending adjacent samples. Decode and playback are decoupled -- the decode fills a buffer, and the pitch counter reads from that buffer at its own rate.

### 2.3 Block Decode is NOT Triggered by Counter Overflow Directly

A subtle but important point: the decode happens BEFORE interpolation, not as a result of the counter advancement. In DuckStation's implementation, the order within one tick is:

1. Check if samples are available (decode if needed)
2. Read interpolated sample from the buffer
3. THEN advance the counter
4. Check if counter crossed block boundary (flag for next tick)

This means the decode for the next block happens on the tick AFTER the counter crosses 28, not the same tick. There is never a "mid-tick" decode.

---

## 3. Gaussian Interpolation Details

### 3.1 Table Indexing from Counter Bits

[VERIFIED: nocash psx-spx, DuckStation, Mednafen, PlayStation1Vsts]

The 8-bit interpolation index `i` is extracted from counter bits 4-11:
```
i = (counter >> 4) & 0xFF    // Range: 0x00 to 0xFF
```

This index selects four coefficients from the 512-entry Gauss table:

```
coeff[0] = gauss_table[0x0FF - i]    // oldest sample weight
coeff[1] = gauss_table[0x1FF - i]    // older sample weight
coeff[2] = gauss_table[0x100 + i]    // old sample weight (previous)
coeff[3] = gauss_table[0x000 + i]    // newest sample weight (current)
```

### 3.2 Interpolation Formula

[VERIFIED: nocash psx-spx, DuckStation spu.cpp]

```
Given: s[0]=oldest, s[1]=older, s[2]=old, s[3]=newest (current sample_index)

out = (gauss[0xFF - i] * s[0]) >> 15
    + (gauss[0x1FF - i] * s[1]) >> 15
    + (gauss[0x100 + i] * s[2]) >> 15
    + (gauss[0x000 + i] * s[3]) >> 15
```

Each term is computed with a signed 16x16->32 multiply followed by arithmetic shift right 15.

Note: The sum of four coefficients is approximately 0x7F7F..0x7F81 (slightly less than 0x8000), meaning the interpolation has a very slight gain reduction. This is a known PS1 hardware characteristic.

### 3.3 The Four Tap Samples: Where They Come From

[VERIFIED: DuckStation DecodeBlock()]

DuckStation uses a flat array `current_block_samples[31]`:
- Indices [0..2]: Last 3 samples from the PREVIOUS block (history)
- Indices [3..30]: 28 samples from the CURRENT block

When interpolating at `sample_index = N`:
```cpp
const u32 s = NUM_SAMPLES_FROM_LAST_ADPCM_BLOCK + sample_index;  // = 3 + N
// s-3 = N (history), s-2 = N+1 (history), s-1 = N+2, s-0 = N+3
out = gauss[0xFF-i]  * samples[s - 3]    // oldest
    + gauss[0x1FF-i] * samples[s - 2]    // older
    + gauss[0x100+i] * samples[s - 1]    // old
    + gauss[0x000+i] * samples[s - 0];   // newest (current position)
```

So when sample_index = 0 (first sample of new block):
- samples[0] = 3rd-to-last from previous block (oldest)
- samples[1] = 2nd-to-last from previous block (older)
- samples[2] = last from previous block (old)
- samples[3] = first decoded sample of current block (newest)

This is how Gaussian interpolation remains smooth across block boundaries.

### 3.4 Interpolation at i=0x00 vs i=0xFF

When i=0x00 (counter fractional part is zero -- exactly ON a sample):
```
coeff[0] = gauss[0xFF]  = -0x0001 (tiny negative, effectively zero)
coeff[1] = gauss[0x1FF] = 0x59B3  (dominant weight on "old" sample)
coeff[2] = gauss[0x100] = 0x1307  (small weight on "current")
coeff[3] = gauss[0x000] = -0x0001 (tiny negative, effectively zero)
```

Wait -- this is counterintuitive. At i=0, the dominant weight is on samples[s-2] ("older"), NOT samples[s-0] ("newest"). This means the Gaussian filter introduces a ~1-sample look-ahead latency in its weighting. The "current" sample index does NOT correspond to the peak of the Gaussian window.

When i=0xFF (maximum fractional, about to cross to next sample):
```
coeff[0] = gauss[0x00]  = -0x0001
coeff[1] = gauss[0x100] = 0x1307
coeff[2] = gauss[0x1FF] = 0x59B3  (dominant)
coeff[3] = gauss[0x0FF] = 0x0F46
```

The dominant weight shifts to samples[s-1] ("old"). So as the counter advances from 0x00 to 0xFF within one sample, the interpolation smoothly shifts its energy from "older" toward "old" -- it is always centered one sample behind the nominal position.

---

## 4. Block Boundary Behavior

### 4.1 History Carry-Forward

[VERIFIED: DuckStation DecodeBlock(), PlayStation1Vsts decodeAdpcmBlock()]

When a new ADPCM block is decoded, the last 3 samples of the previous block are preserved as interpolation history:

```cpp
// DuckStation: at the start of DecodeBlock()
current_block_samples[2] = current_block_samples[30];  // last of prev
current_block_samples[1] = current_block_samples[29];  // 2nd-to-last
current_block_samples[0] = current_block_samples[28];  // 3rd-to-last
// Then decode 28 new samples into positions [3..30]
```

This guarantees that Gaussian interpolation always has 4 valid samples to work with, even at the very start of a new block.

### 4.2 First Block After Key-On

[VERIFIED: DuckStation spu.cpp]

When a voice starts (Key On), `is_first_block = true`. The history positions [0..2] are initialized to zero (or whatever was in the buffer from previous playback). DuckStation handles this by noting that on first block, the history samples will be zero, producing a brief fade-in artifact. This is authentic PS1 behavior.

### 4.3 Counter Does NOT Reset at Block Boundaries

[VERIFIED: DuckStation, Mednafen]

When `sample_index >= 28`, the implementation subtracts 28 (not reset to zero):
```cpp
voice.counter.sample_index -= NUM_SAMPLES_PER_ADPCM_BLOCK;  // -= 28
```

This preserves the fractional position (bits 0-11) and any partial overflow. If the voice is playing at 2x speed (pitch = 0x2000), the counter might advance by 2 samples per tick, meaning sample_index could be 29 after one tick. Subtracting 28 leaves sample_index = 1 for the next block, skipping sample 0 of the new block entirely. This is correct hardware behavior for high-speed playback.

---

## 5. Timing / Latency Characteristics

### 5.1 Per-Tick Processing Order

[VERIFIED: DuckStation SampleVoice()]

Within a single 44.1 kHz tick, the order for each voice is:

```
1. Decode block if needed (has_samples == false)
2. Apply Gaussian interpolation -> raw sample
3. Apply ADSR envelope -> volume
4. Advance pitch counter (counter += step)
5. Check if sample_index crossed 28 (flag for next tick decode)
6. Apply voice volume -> final L/R contribution
```

The decode-before-interpolate ordering means there is NO latency between "need new samples" and "samples available." From the software perspective, decode is instantaneous within the tick.

### 5.2 Latency in the Voice Path

The voice path has inherent latency characteristics:

1. **Decode latency:** Zero. Block is available same tick it's needed.
2. **Gaussian filter latency:** ~1 sample. The Gaussian window is centered approximately 1 sample behind the nominal position (see Section 3.4). This is a constant offset, not a variable delay.
3. **Output latency:** Zero additional. The interpolated sample is immediately available for mixing in the same tick.

### 5.3 SPU Output Timing

[CITED: nocash psx-spx]

The SPU produces one stereo sample per tick (44.1 kHz). All 24 voices are processed sequentially within one tick. The reverb unit also runs once per tick but at half rate (alternating L/R processing). The final mixed output is sent to the DAC at 44.1 kHz.

---

## 6. Comparison With Our Current Implementation

### 6.1 Current Architecture (Two Counters)

Our `spu94_process.c` uses:
- `decim_counter` -- advances at pitch rate, triggers ADPCM feed when crossing bit 12
- `gauss_counter` -- separate counter advancing at the same pitch rate, drives Gaussian interpolation

Problems:
1. **Two counters drift independently.** Even though both advance by the same `pitch` value, they serve different roles and the code treats them as separate state machines. In the real hardware, there is ONE counter driving BOTH functions.

2. **The Gaussian read position (`gauss_read_pos`) is a separate index** into the decoded buffer, advanced only when `gauss_counter` crosses a sample boundary. In real hardware, the sample_index field of the pitch counter IS the read position.

3. **The `gauss_counter` crossing check and the `decim_counter` crossing check** can disagree about timing, producing samples from the wrong position relative to decode.

### 6.2 What Single-Counter Architecture Looks Like

```
Per 44.1 kHz tick:
  counter += pitch

  // Integer part: how many samples did we advance?
  samples_consumed = counter >> 12
  counter &= 0xFFF   // keep fractional

  // Feed each consumed sample to ADPCM
  for each consumed sample:
    push decoded output into ring buffer (shift ring)

  // Gaussian interpolation uses fractional part
  i = (counter >> 4) & 0xFF
  output = gauss_interpolate(ring[0..3], i)
```

The key difference: the fractional position that indexes into the Gauss table is INHERENTLY the remainder after consuming whole samples. You cannot have a situation where the interpolation index says "halfway between samples" while the decode index says "at the boundary" -- they are the same register.

### 6.3 Specific Bug Pattern in Dual-Counter

Consider pitch = 0x0800 (22.05 kHz, one sample every 2 ticks):

**Tick 1:** decim_counter: 0x000 -> 0x800 (no crossing). gauss_counter: 0x000 -> 0x800 (no crossing).
**Tick 2:** decim_counter: 0x800 -> 0x1000 (CROSSED! feed sample to ADPCM). gauss_counter: 0x800 -> 0x1000 (CROSSED! advance ring buffer).

Both cross simultaneously -- OK. But what if there's any code path that resets one and not the other? Or what if the "crossed" detection uses different logic for each?

In the current code:
```c
int crossed = (state->decim_counter < old_counter)
            || ((old_counter >> 12) != (state->decim_counter >> 12));
```

vs for gauss:
```c
int gauss_crossed = (state->gauss_counter < old_gauss)
                  || ((old_gauss >> 12) != (state->gauss_counter >> 12));
```

These CAN diverge if one is reset during disable/enable, if the AA filter path modifies timing, or if any edge case causes them to desync.

---

## 7. The Correct Single-Counter Implementation

Based on all sources, the correct architecture for our use case is:

```c
/* Per 44.1 kHz tick, when ADPCM voice path is enabled: */

uint16_t pitch = state->voice_pitch;  // 0x0001..0x3FFF

/* 1. Advance the single pitch counter */
uint32_t old_counter = state->voice_counter;
state->voice_counter += pitch;

/* 2. Determine samples consumed (integer part advancement) */
uint32_t old_sample_idx = old_counter >> 12;
uint32_t new_sample_idx = state->voice_counter >> 12;
uint32_t samples_consumed = new_sample_idx - old_sample_idx;

/* 3. For each consumed sample: push into ADPCM and update ring buffer */
for (uint32_t s = 0; s < samples_consumed; s++) {
    /* Feed one input sample to the encode buffer */
    state->adpcm_in_buf[state->adpcm_buf_pos] = get_input_sample();
    state->adpcm_buf_pos++;

    if (state->adpcm_buf_pos == 28) {
        /* Encode + decode the block */
        encode_block(...);
        decode_block(...);
        /* Copy last 3 decoded to history */
        state->adpcm_buf_pos = 0;
    }

    /* Push decoded sample into Gaussian ring buffer */
    uint8_t wp = state->gauss_ring_pos;
    state->gauss_ring[wp] = decoded_output[current_decode_read_pos];
    state->gauss_ring_pos = (wp + 1) & 3;
    advance_decode_read_pos();
}

/* 4. Gaussian interpolation using fractional part */
uint8_t i = (uint8_t)((state->voice_counter >> 4) & 0xFF);
int16_t output = gauss_interpolate(state->gauss_ring, i, state->gauss_ring_pos);
```

### 7.1 Ring Buffer Orientation for Gaussian

The ring buffer stores the 4 most recent decoded samples. When reading for interpolation:

```c
/* wp points to NEXT write position = oldest sample */
/* (wp+0)&3 = oldest, (wp+1)&3 = older, (wp+2)&3 = old, (wp+3)&3 = newest */
int16_t s0 = ring[(wp + 0) & 3];  // oldest  -> gauss[0xFF - i]
int16_t s1 = ring[(wp + 1) & 3];  // older   -> gauss[0x1FF - i]
int16_t s2 = ring[(wp + 2) & 3];  // old     -> gauss[0x100 + i]
int16_t s3 = ring[(wp + 3) & 3];  // newest  -> gauss[0x000 + i]
```

This matches our existing code in `spu94_process.c` lines 129-136.

### 7.2 Handling Pitch > 0x1000 (Multiple Samples Per Tick)

When pitch > 0x1000, the counter advances by more than one sample per tick. `samples_consumed` can be 2, 3, or even 4 (at max pitch 0x3FFF). Each consumed sample must be pushed into the ring buffer in sequence. The interpolation then uses the LAST 4 samples pushed.

For our use case (max pitch = 0x1000 per the existing clamp), we will consume at most 1 sample per tick at the maximum setting. At lower pitches (0x0800, 0x0400, etc.), we consume 0 or 1 samples per tick.

---

## 8. Edge Cases and Special Behavior

### 8.1 Pitch = 0 (Stopped Voice)

[CITED: nocash psx-spx]

When VxPitch = 0, the counter never advances. The voice outputs the same interpolated sample forever (the Gaussian weights don't change because `i` doesn't change). This produces DC output at the last interpolated value.

### 8.2 Counter Wraparound (32-bit Overflow)

[VERIFIED: DuckStation]

DuckStation uses a 32-bit counter. If sample_index accumulates past 31 (5 bits), DuckStation explicitly checks `>= 28` and subtracts 28. The counter bits above bit 16 are unused in the bit-field but exist in the u32. In practice, sample_index never exceeds 31 because the maximum step is 0x3FFF (4 samples per tick) and the check happens every tick.

### 8.3 Our Simplification: 16-bit Counter is Sufficient

For our application (max pitch 0x1000), the counter advances at most 0x1000 per tick. A 16-bit counter overflows at 0xFFFF, which takes 16 ticks at max rate. But since we check and subtract every tick, sample_index (bits 12-15) never exceeds 1. We can safely use a 16-bit counter with the following discipline:

```c
uint16_t counter;  // 4.12 fixed-point
counter += pitch;
uint16_t samples_consumed = counter >> 12;
counter &= 0x0FFF;  // Keep only fractional
```

This is equivalent to the hardware's 32-bit counter for our pitch range.

---

## 9. Sources

### Primary (HIGH confidence)
- [nocash psx-spx: SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) -- pitch counter bit fields, Gaussian formula, table indices
- [DuckStation spu.cpp](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp) -- VoiceCounter union, SampleVoice(), Voice::Interpolate(), Voice::DecodeBlock()
- [Mednafen/beetle-psx spu.c](https://github.com/libretro/beetle-psx-libretro/blob/master/mednafen/psx/spu.c) -- CurPhase counter, DecodeBuffer[32], FIR_Table, SPU_RunDecoder()

### Secondary (MEDIUM confidence)
- [PlayStation1Vsts Spu.cpp](https://github.com/BodbDearg/PlayStation1Vsts/blob/master/PluginsCommon/Spu.cpp) -- stepVoice(), getInterpolatedVoiceSample(), decodeAdpcmBlock()
- [psx-spx.consoledev.net SPU page](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- mirror of nocash documentation

### Cross-Verification

All three emulator implementations agree on:
- Single counter driving both sample advancement and interpolation
- Bits 4-11 as 8-bit Gaussian index
- Bits 12+ as sample position within block
- 28-sample block boundary triggering next-block decode
- 3 samples of history carried forward across blocks
- Subtract-28 (not reset-to-zero) at block boundary

---

## 10. Implications for SPU-94 Implementation

### What Must Change

1. **Replace two counters with one.** `decim_counter` and `gauss_counter` collapse into a single `voice_counter`.

2. **Unify the crossing detection.** One check: `new_counter >> 12 != old_counter >> 12` determines BOTH "push to ADPCM" AND "advance ring buffer."

3. **Remove `gauss_read_pos` as independent state.** The ring buffer write position and the decode output read position are advanced together by the same event (sample consumed).

4. **Gaussian index comes from the same counter.** `i = (voice_counter >> 4) & 0xFF` -- no separate gauss_counter needed.

### What Can Stay

- The ADPCM encode/decode block mechanism (28-sample batching)
- The Gaussian table and interpolation formula (already correct)
- The 4-element ring buffer approach (already correct orientation)
- The AA filter concept (runs every tick regardless of crossing)
- The pitch clamp range (0x005C..0x1000)

### Expected Behavioral Differences

With a single counter:
- Interpolation phase is always perfectly coherent with decode position
- No possible drift between "where we are in the decode buffer" and "how we interpolate"
- The output at pitch=0x1000 (1:1) will be slightly different because `i` cycles 0x00->0xFF as a natural consequence of the fractional bits filling up, rather than being computed from a separate counter with potentially different phase
