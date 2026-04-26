# Deep Research: ADPCM Decode Edge Cases and Underdocumented Behavior

**Domain:** PS1 SPU ADPCM decode — bit-level arithmetic edge cases
**Researched:** 2026-04-26
**Overall confidence:** MEDIUM — several topics have definitive answers, others are genuinely underdocumented

---

## 1. Sign-Extension Path for Nibbles: Exact Analysis

### The Question

Two common implementations exist for extracting and shifting a 4-bit signed nibble:

**Approach A (explicit sign-extend then shift):**
```c
int nibble = (byte >> bit_offset) & 0x0F;
if (nibble >= 8) nibble -= 16;         /* range: -8..+7 */
int32_t shifted = nibble << (12 - shift);
```

**Approach B (fused sign-extend-and-shift):**
```c
int16_t shifted = (int16_t)(nibble << 12) >> shift;
```

### Are These Equivalent? NO — With Caveats

Approach B is the one described in the psx-spx XA-ADPCM docs and used by several emulators (notably jsgroth's jgenesis). The nocash docs describe `signed4bit()` simply as extracting a value in range -8..+7.

**The math for Approach B, step by step:**

1. `nibble << 12`: nibble is 0x0-0xF. Left-shift by 12 gives 0x0000-0xF000. For nibble >= 8, e.g. nibble=0xF: 0xF000.
2. Cast to `int16_t`: 0xF000 as int16_t = -4096 (which is -1 << 12, correct for nibble value -1).
3. `>> shift` (arithmetic right shift): propagates sign bit downward.

For nibble = -1 (0xF), shift = 0: `(int16_t)(0xF000) >> 0` = -4096 = (-1) << 12. Correct.
For nibble = -1 (0xF), shift = 12: `(int16_t)(0xF000) >> 12` = -1. Correct.
For nibble = 7 (0x7), shift = 0: `(int16_t)(0x7000) >> 0` = 28672 = 7 << 12. Correct.

**However, Approach B has a subtle problem: it operates in int16_t, which limits the intermediate range.**

The filter prediction `((old * f0 + older * f1 + 32) >> 6)` is ADDED to the shifted value. If `shifted` is stored as int16_t (range -32768..+32767), the addition must happen in int32_t. This is fine as long as the caller widens before adding.

**The real danger: Approach B in pure int16_t throughout.**

Consider nibble = -8, shift = 0:
- Approach A: shifted = -8 << 12 = -32768. Fits in int16_t (barely).
- Approach B: (int16_t)(0x8 << 12) = (int16_t)(0x8000) = -32768. Same.

Now nibble = 7, shift = 0:
- Approach A: shifted = 7 << 12 = 28672. Fits in int16_t.
- Approach B: (int16_t)(0x7000) = 28672. Same.

**They are provably equivalent for shift 0-12.** The intermediate `nibble << 12` fits in 16 bits because nibble is 4 bits and 4 + 12 = 16. The sign extension from int16_t's MSB is correct because bit 15 is the sign of the 4-bit nibble propagated by the left-shift.

### Shift 13-15 (Treated As 9)

Both approaches produce identical results when shift > 12 is remapped to 9:
- Approach A: nibble << 3 (shift_amount = 12 - 9 = 3)
- Approach B: (int16_t)(nibble << 12) >> 9 — the intermediate value is the same 16-bit quantity right-shifted by 9 instead of by the original 13-15.

**The shift-13-as-9 behavior** is confirmed in psx-spx and implemented consistently across Mednafen, DuckStation, and jgenesis. No emulator developer disputes this.

### Recommendation for SPU-94

**Use Approach A (explicit sign-extend then shift) in int32_t throughout.**

Rationale:
- Clearer intent; no hidden reliance on int16_t wrapping behavior
- The hardware is an integer pipeline, not a C type-width puzzle
- Avoids any future portability issue with implementation-defined arithmetic right shift on int16_t (though C11 leaves ASR implementation-defined, we already assert it in spu94_q15.h)

```c
/* Canonical SPU-94 nibble decode */
int32_t nibble = (byte >> bit_offset) & 0x0F;
if (nibble >= 8) nibble -= 16;  /* sign extend: -8..+7 */

int shift_amount = (shift > 12) ? 3 : (12 - shift);
int32_t shifted = nibble << shift_amount;
```

### Confidence: HIGH
Multiple emulator sources agree. The math is provably equivalent for all valid shift values. No intermediate overflow exists when computed in int32_t.

---

## 2. The +32 Rounding Bias: PS1 vs SNES BRR Comparison

### PS1 SPU ADPCM Formula

From psx-spx (XA-ADPCM, shared with SPU-ADPCM):
```
sample = shifted + ((old * f0 + older * f1 + 32) >> 6)
```

The `+ 32` before `>> 6` is a standard round-half-up bias. Since 64/2 = 32, adding 32 before dividing by 64 (via ASR 6) rounds the prediction to the nearest integer, with ties rounding away from zero for positive values and toward zero for negative values (because ASR floors).

**Precise rounding behavior of `(x + 32) >> 6`:**
- x = 0: (0 + 32) >> 6 = 0 (correct: 0/64 = 0)
- x = 31: (31 + 32) >> 6 = 0 (correct: 31/64 rounds to 0)
- x = 32: (32 + 32) >> 6 = 1 (correct: 32/64 rounds to 1)
- x = -1: (-1 + 32) >> 6 = 0 (correct: -1/64 rounds to 0)
- x = -32: (-32 + 32) >> 6 = 0 (correct: -32/64 rounds to 0)
- x = -33: (-33 + 32) >> 6 = -1 (floor of -1/64 = -1; this is round-away-from-zero for -0.015)

This is NOT symmetric rounding. For negative values, the bias still adds +32, so the rounding threshold shifts. The behavior is: floor((x + 32) / 64) where floor is toward negative infinity (ASR behavior).

### SNES SPC700 BRR Formula

The SNES BRR uses a DIFFERENT filter formula structure. From the nesdev forum thread and SnesLab docs:

**Filter 0:** `new = s` (no prediction)
**Filter 1:** `new = s + old + (-old >> 4)` = s + old * 15/16
**Filter 2:** `new = s + (old << 1) + ((-3*old) >> 5) - older + (older >> 4)` = s + old * 61/32 - older * 15/16
**Filter 3:** `new = s + (old << 1) + ((-(old + 4*old + 8*old)) >> 6) - older + ((2*older + older) >> 4)` = s + old * 115/64 - older * 13/16

**Key difference: SNES BRR uses bit shifts and additions to approximate fractional coefficients. There is NO +32 rounding bias.** The SNES computes everything through shift-and-add operations that implicitly truncate (floor) at each step.

### Coefficient Comparison

| Filter | PS1 f0 | PS1 f1 | SNES f0 equiv | SNES f1 equiv |
|--------|--------|--------|---------------|---------------|
| 0 | 0 | 0 | 0 | 0 |
| 1 | 60/64 | 0 | 15/16 (=60/64) | 0 |
| 2 | 115/64 | -52/64 | 61/32 (=122/64) | -15/16 (-60/64) |
| 3 | 98/64 | -55/64 | 115/64 | -13/16 (-52/64) |
| 4 | 122/64 | -60/64 | N/A | N/A |

**Filter 1 coefficients are identical** (60/64 = 15/16).
**Filters 2-3 coefficients are DIFFERENT.** The PS1 and SNES use different coefficient values for these filters. Filter 4 is PS1-only.

### Cross-Pollination Insights

The SNES BRR documentation is more battle-tested because bsnes/ares achieved cycle-accurate DSP emulation years before PS1 emulators reached comparable accuracy. Key insight: **the SNES BRR's shift-and-add approach introduces cumulative truncation errors at each intermediate step that are different from the PS1's single-division approach with rounding bias.**

For SPU-94, the +32 rounding bias is well-documented and agreed upon. The SNES precedent confirms that Sony used different approaches in different hardware generations — this is not a documentation error.

### Confidence: HIGH
The PS1 formula is consistent across psx-spx, jsgroth, and all major emulators. The SNES comparison is well-documented in fullsnes/SnesLab/nesdev.

---

## 3. Hardware Clipping: Is There a Second (15-bit) Clamp on PS1?

### SNES: The 15-bit Anomaly

The SNES S-DSP has a well-documented hardware bug in BRR decoding:

1. After filter application, the result is clamped to signed 16-bit (-0x8000..+0x7FFF)
2. Then **clipped to signed 15-bit** by discarding the MSB (bit 15)
3. This effectively reinterprets the 16-bit value as a 15-bit signed value (range -0x4000..+0x3FFF)
4. Values in +0x4000..+0x7FFF wrap to -0x4000..-1

Nocash documented this as accidental: "Sony accidentally saturated the values to 16bits and then crippled it to 15bits by removing the sign bit, when what they wanted to do was saturate to 15bits." The effect is that the BRR decoder's samples fed to the Gaussian interpolation table are 15-bit, not 16-bit.

### PS1: No Evidence of 15-bit Clamp

**All evidence indicates the PS1 SPU clamps to the full signed 16-bit range (-0x8000..+0x7FFF) with NO secondary 15-bit clip.**

Evidence:
1. **psx-spx** documents `MinMax(s, -8000h, +7FFFh)` — 16-bit, no mention of 15-bit
2. **jsgroth's blog** explicitly states PS1 clamps to signed 16-bit, contrasting with SNES's signed 15-bit
3. **DuckStation source** uses `clamp(-0x8000, 0x7FFF)` — 16-bit
4. **The PS1 Gaussian interpolation table** was designed so that the four multipliers sum to less than 0x8000, meaning 16-bit decoded samples will not overflow during interpolation. The SNES table had a bug where sums could exceed 0x8000, which is partly why the 15-bit clamp existed (to reduce overflow risk in the Gauss table)
5. **jsgroth's interpolation post** explicitly states: "PS1 should clamp to signed 16-bit [...] SNES should clamp to signed 15-bit, to match the range of values that actual hardware can produce"

### Why This Matters for SPU-94

It does not. SPU-94's ADPCM codec does not need to replicate the Gaussian interpolation stage (that is the pitch/voice subsystem, not the ADPCM decoder). The ADPCM decode itself clamps to 16-bit on PS1, full stop.

If SPU-94 ever models the voice/pitch subsystem (well beyond current scope), the Gaussian interpolation would need to clamp its output to 16-bit (not 15-bit as on SNES).

### Confidence: HIGH
Multiple authoritative sources explicitly contrast PS1 (16-bit) with SNES (15-bit). No source suggests PS1 has a 15-bit clamp.

---

## 4. ADPCM Block at SPU RAM Boundary (512KB Wrap)

### SPU RAM Address Space

The PS1 SPU has exactly 512KB (0x80000 bytes) of RAM, addressed as 0x00000..0x7FFFF. Voice start and repeat addresses are stored in 8-byte units (the register value is multiplied by 8 to get the byte address).

### Wrapping Behavior

**For reverb:** psx-spx explicitly documents that "all memory addresses are relative to the current BufferAddress, and wrapped within mBASE..7FFFEh when exceeding that region." The reverb addresses wrap within the reverb work area.

**For voice playback:** The documentation is less explicit, but:
- Voice current address is an internal register that advances by 16 bytes (one ADPCM block) after decoding each block
- The address register is 18 bits wide (enough to address 0x00000..0x3FFFF in 8-byte units, which covers the full 512KB)
- **Wrapping almost certainly occurs** at the 512KB boundary (0x80000 wraps to 0x00000), because the address bus is only 19 bits wide (512K = 2^19)

### Relevance for SPU-94

**Essentially irrelevant.** SPU-94 does not model the SPU RAM address space. It models the ADPCM codec (encode/decode) and the reverb network. The ADPCM codec operates on blocks provided by the caller. RAM addressing, voice playback, and address wrapping are the voice/playback subsystem's concern.

If SPU-94 ever models the full SPU memory map, address wrapping would be implemented as a simple bitmask: `address & 0x7FFFF`.

### Confidence: MEDIUM
Reverb wrapping is documented. Voice address wrapping is inferred from hardware constraints (address bus width) but not explicitly documented for voice playback.

---

## 5. Decode State Initialization on Voice Key-On

### What Happens on Key-On

When a PS1 voice is keyed on (bit set in register 0x1F801D88/0x1F801D8A):

1. The start address register is copied to the current address (documented in psx-spx)
2. The ADSR envelope is initialized: volume starts at zero, attack phase begins (documented)
3. The pitch counter is reset to 0 (documented in jsgroth)
4. **The ADPCM decode state (old/older) is... unclear**

### Evidence For Zeroing old/older on Key-On

1. **VAG format convention:** All VAG files are required to begin with a 16-byte block of zeros (shift=0, filter=0, all nibbles=0). The Archive Team VAG documentation states this "initializes the SPU in order to prevent clipping noises." This implies the hardware does NOT zero old/older on key-on — if it did, the zero lead-in block would be unnecessary.

2. **psx-spx XA-ADPCM note:** "old/older values are usually that from the previous part, or garbage (in case of decoding errors), or whatever (in case there was no previous part) (ie. maybe zero on power-up?)." The language is explicitly uncertain — nocash did not verify this on hardware.

3. **Emulator implementations:** jsgroth's jgenesis resets the ADPCM current address and pitch counter on key-on, then decodes the initial block. DuckStation similarly copies start address to current address. Neither source explicitly documents whether old/older are zeroed.

### Most Likely Hardware Behavior

**old/older are NOT explicitly zeroed on key-on.** They retain whatever values they had from the voice's previous playback. The zero lead-in block in VAG files exists precisely to "flush" these stale values to zero through normal decode (filter 0, shift 0, all-zero nibbles will produce zero samples regardless of old/older state).

This is consistent with hardware design principles: the key-on logic copies a register (start address) and resets a counter (pitch counter / ADSR). It would be unusual for it to also reach into the decode pipeline's state registers to zero them.

### Loop-End Behavior

When a voice reaches a loop-end block (flag bit 0 set):
- If loop-repeat flag (bit 1) is also set: current address jumps to repeat address; **decode state (old/older) carries across the loop boundary**
- If loop-repeat flag is NOT set: voice is forced into ADSR Release phase (effectively muting); address still jumps to repeat address

**State carries across loops.** This is critical for loop quality — the first block after the loop point is decoded using old/older from the last block before the loop-end. The loop-start block should be designed with this in mind (typically using filter 0 or a filter that works with the expected old/older values at that point).

### Implications for SPU-94

The SPU-94 ADPCM codec API already has the right design: `spu94_adpcm_state` is caller-managed, initialized by the caller with `{0}`. This correctly models the hardware behavior:
- For new voices: caller zeros the state (equivalent to the VAG zero lead-in block)
- For loop repeats: caller preserves state across the loop jump
- The codec itself never resets state internally

### Confidence: MEDIUM
The "old/older not zeroed on key-on" conclusion is inferred from the VAG lead-in convention and nocash's uncertain language. No hardware test has definitively confirmed this. The "state carries across loops" conclusion has HIGHER confidence — it is the only interpretation consistent with loop audio quality in real games.

---

## 6. Known Test ROMs and Hardware Test Programs

### ps1-tests (JaCzekanski)

**Repository:** https://github.com/JaCzekanski/ps1-tests

Contains SPU tests, but they focus on:
- DMA and regular I/O transfers to SPU RAM (memory-transfer)
- SPU memory access sandbox (ram-sandbox)
- Basic stereo voice playback (stereo)
- SPU register behavior, particularly the 32-bit vs 16-bit write instability (test)
- SPU register preview and voice playback tool (toolbox)

**No ADPCM-specific decode accuracy tests exist in this repository.** There are no tests that verify the decode algorithm's arithmetic, edge cases with specific shift/filter values, or state behavior on key-on.

### Amidog's PS1 Tests

Focus on CPU (Exception, Flag, Value, Timing). **No SPU or ADPCM tests.**

### FPGA PS1 Projects

The SuperStation FPGA PS1 project would need accurate ADPCM decoding, but their test methodology is not publicly documented for ADPCM specifically.

### emu-russia/psxrev

The psxrev project has hardware reverse-engineering of the SPU chip (CXD2922Q/CXD2925Q), but their wiki focuses on the system architecture and signal flow, not the ADPCM decode algorithm specifics.

### PSn00bSDK / PSX Homebrew

PSn00bSDK (Lameguy64) provides SPU library functions for homebrew, including VAG playback. However, these are utilities for game development, not hardware accuracy test suites. They use standard Sony-documented behavior and do not exercise ADPCM edge cases.

### The Capture Buffer Approach

The PS1 SPU has four 1KB capture buffers in the first 4KB of SPU RAM. Voices 1 and 3 have their decoded output captured to these buffers as signed 16-bit samples. **This is the most promising approach for hardware validation:**

1. Upload a crafted ADPCM block with known edge-case values to SPU RAM
2. Play it on voice 1 or 3
3. Read back the capture buffer to get the hardware's actual decoded output
4. Compare against the software decoder's output

No published test program does this systematically. **This is a gap in the PS1 emulation ecosystem.** If Anthony owns original PSX hardware, this would be a definitive validation method for SPU-94's ADPCM decoder. However, this is a hardware-validation project unto itself and should be considered a stretch goal, not a blocking dependency.

**Caveat:** The capture buffer has a known instability issue documented in psx-spx — "unstable values occur only every 32 halfwords or so (probably when the SPU is simultaneously reading ADPCM data)" — requiring repeated reads and ANDing to get stable values. This is manageable but adds complexity.

### Confidence: HIGH for "no existing ADPCM-specific test ROM"
The gap is real. The PS1 emulation community has not produced a systematic ADPCM decode accuracy test. This is because most games work fine with any reasonable implementation, and the edge cases (shift 13-15, filter > 4, overflow clamp) are rare in practice.

---

## 7. Summary of Findings and Implications

### What Is Definitively Known (HIGH confidence)

| Topic | Finding |
|-------|---------|
| Sign extension | Both approaches (explicit and fused) are provably equivalent for shift 0-12 |
| Shift 13-15 | Treated as shift 9. Universal agreement. |
| +32 rounding | Round-half-up bias via `(x + 32) >> 6`. Not symmetric for negative values due to ASR flooring. |
| PS1 vs SNES clamp | PS1 clamps to 16-bit (-0x8000..+0x7FFF). SNES clips to 15-bit. They are different. |
| SNES BRR vs PS1 ADPCM | Different coefficient values (except filter 1). Different arithmetic approach (SNES uses shift-and-add, PS1 uses multiply-and-shift with rounding). |
| Loop state | old/older carry across loop boundaries (not reset). |

### What Is Probable But Unconfirmed (MEDIUM confidence)

| Topic | Finding |
|-------|---------|
| Key-on state | old/older are likely NOT zeroed; VAG zero lead-in block exists for this reason |
| SPU RAM wrapping | Almost certainly wraps at 0x80000 via address bus width, but not explicitly documented for voice addresses |

### What Is Unknown (LOW confidence)

| Topic | Finding |
|-------|---------|
| No published ADPCM edge-case test ROM | The hardware behavior for shift 13-15, filter > 4, and key-on state has never been systematically verified on real hardware with capture buffer readback |
| Filter index 5-7 | Emulators clamp to 4, but hardware behavior is genuinely unknown |

### Implications for SPU-94 M2

1. **Use explicit sign-extend + shift in int32_t.** Clearer, provably correct, no hidden type-width assumptions.
2. **Use `(predicted + 32) >> 6` for the filter formula.** This is the correct rounding behavior. Document that this is ASR (floor division), not symmetric rounding.
3. **Clamp to 16-bit, not 15-bit.** PS1 is definitively different from SNES here.
4. **Initialize state to zero in documentation and examples.** This matches the VAG lead-in convention and produces correct results regardless of actual hardware key-on behavior.
5. **Preserve state across loop boundaries.** The codec API already supports this via caller-managed state.
6. **No ADPCM edge-case test ROM exists.** Hardware validation would require writing a custom PSX homebrew program using the capture buffer approach. This is valuable but out of scope for M2 — consider as a future stretch goal if Anthony wants to exercise his original PSX hardware.

---

## Sources

### Primary (HIGH confidence)
- [psx-spx: CDROM XA Audio ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) — decode formula, filter coefficients, +32 rounding, MinMax clamp to 16-bit
- [psx-spx: SPU ADPCM Samples](https://problemkaputt.de/psxspx-spu-adpcm-samples.htm) — block structure, flag bytes, shift/filter header
- [psx-spx: Sound Processing Unit (SPU)](https://psx-spx.consoledev.net/soundprocessingunitspu/) — SPU RAM size, key-on behavior, capture buffers, address registers
- [jsgroth: PS1 SPU Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) — complete decode implementation, key-on behavior, 16-bit clamp
- [jsgroth: SNES & PS1 Cubic ADPCM Interpolation](https://jsgroth.dev/blog/posts/snes-ps1-cubic-adpcm-interpolation/) — explicit PS1=16-bit vs SNES=15-bit comparison

### Secondary (MEDIUM-HIGH confidence)
- [SnesLab: Bit Rate Reduction](https://sneslab.net/wiki/Bit_Rate_Reduction) — SNES BRR filter formulas, 15-bit clamp description
- [SNESdev Wiki: Errata](https://snes.nesdev.org/wiki/Errata) — BRR 15-bit clamp anomaly, Gaussian interpolation bug, FIR filter bug
- [nesdev forum: BRR decoding/encoding](https://forums.nesdev.org/viewtopic.php?t=5737) — BRR filter fixed-point arithmetic, 15-bit clamp details
- [nesdev forum: FullSNES BRR recommendations](https://forums.nesdev.org/viewtopic.php?t=10290) — nocash's description of accidental 16-bit-then-15-bit clamp
- [DuckStation SPU source](https://github.com/stenzek/duckstation/blob/master/src/core/spu.cpp) — 16-bit clamp, key-on implementation

### Tertiary (MEDIUM confidence)
- [ps1-tests (JaCzekanski)](https://github.com/JaCzekanski/ps1-tests) — confirms no ADPCM-specific tests exist
- [hitmen SPU docs](https://hitmen.c02.at/files/docs/psx/spu.txt) — SPU RAM layout, voice registers, key-on initiation
- [VAG format (Archive Team)](http://justsolve.archiveteam.org/wiki/VAG_(PlayStation)) — zero lead-in block requirement
- [emu-russia/psxrev SPU](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md) — hardware architecture overview

---

*Deep research for: ADPCM decode edge cases and underdocumented behavior*
*Researched: 2026-04-26*
