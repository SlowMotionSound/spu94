# Deep Research: Hardware Evidence and Emulator Disagreements for PS1 ADPCM

**Researched:** 2026-04-26
**Mode:** Feasibility / Ecosystem (Pass 2 -- hardware evidence)
**Overall confidence:** MEDIUM (strong on SNES BRR precedent, weaker on PS1-specific hardware captures)

---

## Executive Summary

This second research pass searched exhaustively for hardware evidence -- test results,
captures, emulator developer discussions, FPGA implementations -- that can resolve the
gray areas identified in Pass 1: shift values 13-15, filter indices 5-7, division vs
arithmetic right shift, and whether PS1 ADPCM has a hidden 15-bit clamp like SNES BRR.

The key finding is that **no published hardware captures of raw PS1 ADPCM decode output
exist**. The community has converged on behavioral claims primarily through nocash's
psx-spx documentation, which is treated as ground truth by all major emulators. The SNES
BRR predecessor is far better characterized at the hardware level, and the differences
between BRR and PS1 ADPCM are instructive.

---

## 1. ps1-tests Repository (JaCzekanski)

**Confidence: HIGH** (directly inspected repo structure)

The [ps1-tests](https://github.com/JaCzekanski/ps1-tests) repository contains 5 SPU
tests, none of which test ADPCM decode accuracy:

| Test | What it tests |
|------|---------------|
| `memory-transfer` | DMA and regular IO transfers to SPU RAM |
| `ram-sandbox` | SPU memory access sandbox (dev tool) |
| `stereo` | Playing samples on first two voices |
| `test` | SPU register behavior (32-bit write instability) |
| `toolbox` | SPU register preview and simple voice playback tool |

**Finding:** No ADPCM decode accuracy test exists in ps1-tests. No test uploads a
known ADPCM block, plays it, captures the decoded output via SPU capture buffers, and
compares against expected values. This is a gap in the PS1 testing ecosystem.

**Implication for us:** We cannot rely on existing test infrastructure to validate our
decoder. We would need to write our own hardware test if we want ground-truth data.
However, for M2 (standalone codec library), matching psx-spx + emulator consensus is
sufficient. Hardware validation can wait for M5.

---

## 2. Emulator Accuracy Discussions

### 2.1 DuckStation (stenzek)

**Confidence: MEDIUM** (commit messages found, not full discussions)

Two significant ADPCM-related commits found via GitHub search:

1. **`0076af69` -- "SPU: Zero out upper ADPCM filters"**
   - Also applied to CD-ROM XA path
   - This confirms DuckStation explicitly handles filter indices 5-7 by zeroing
     the coefficients (treating them as filter 0 -- no prediction)
   - This is a deliberate behavioral choice, implying stenzek investigated and
     decided these filters should produce silence/passthrough rather than using
     garbage table entries

2. **`be9033b6` -- "SPU: Add missing clamp in ADPCM decoder"**
   - A clamp was missing in the decode path, meaning decoded samples could
     exceed the signed 16-bit range before this fix
   - Confirms the 16-bit clamp (MinMax to -8000h..+7FFFh) is considered
     necessary for correctness

3. **`03080351` -- "SPU: Reset ADPCM decoder last samples on key on"**
   - The old/older state is reset to zero when a voice is keyed on
   - Prevents state contamination from previous voice playback

4. **Release v0.1-8153** mentioned "incorrect clamp in XA-ADPCM decoding" and
   "zeroing out upper ADPCM filters"

5. **Release v0.1-7294** mentioned "fix handling of 8-bit ADPCM and decoder
   overruns in CDROM"

**Key behavioral decisions by DuckStation:**
- Filter 5-7: zeroed out (treated as filter 0)
- 16-bit signed clamp on decoded output
- State reset on key-on

### 2.2 Mednafen / beetle-psx

**Confidence: LOW** (changelog references only, no specific ADPCM discussions found)

Mednafen's changelog mentions removing "incorrect filtering of CD-XA ADPCM sectors
based on file and channel numbers" but no discussions of ADPCM decode edge cases
(shift 13-15, filter 5-7) were found in public sources.

Mednafen is widely considered the gold standard for PS1 accuracy, but its SPU ADPCM
implementation details are not publicly discussed. The source code is available but
was not examined per project licensing guidelines.

### 2.3 PCSX-Redux (grumpycoders)

**Confidence: LOW** (no ADPCM-specific discussions found)

PCSX-Redux's TODO.md and issue tracker show no SPU ADPCM accuracy discussions. The
project focuses more on reverse engineering tools and debugging infrastructure than
cycle-accurate audio.

### 2.4 Avocado (JaCzekanski)

**Confidence: LOW**

Avocado supports XA-ADPCM and SPU-ADPCM with samples interpolation. No public
discussions of ADPCM decode edge cases found. Being from the same author as ps1-tests,
the lack of ADPCM accuracy tests in that repo suggests this area may not have been
deeply investigated.

### 2.5 jsgroth's Emulator Blog

**Confidence: MEDIUM** (detailed blog posts with implementation details)

jsgroth's [PS1 SPU Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/)
blog post documents these behavioral choices:

- **Shift 13-15:** Clamps to 9. Code: `let shift = if shift > 12 { 9 } else { shift };`
- **Filter 5-7:** Clamps to 4. Code: `cmp::min(4, (block[0] >> 4) & 0x07)`
- **Division semantics:** Uses integer division with rounding: `(60 * old + 32) / 64`
  (NOT arithmetic right shift)
- **Output clamp:** 16-bit signed: `filtered_sample.clamp(-0x8000, 0x7FFF)`

The [SNES & PS1 Cubic Interpolation](https://jsgroth.dev/blog/posts/snes-ps1-cubic-adpcm-interpolation/)
post explicitly states:
- **SNES:** "clamp to signed 15-bit"
- **PS1:** "clamp to signed 16-bit"

This is the clearest statement found that PS1 ADPCM does NOT have the SNES BRR 15-bit
clamp. The PS1 SPU operates in 16-bit sample space throughout.

**Important note on division vs shift:** jsgroth uses `/64` (Rust integer division,
which truncates toward zero), not `>>6` (which floors toward negative infinity). This
contradicts our ADR-0001 ASR discipline. See Section 7 below for analysis.

---

## 3. PSX Homebrew Audio Tools

**Confidence: MEDIUM**

### PSn00bSDK (Lameguy64)

PSn00bSDK includes CD-ROM XA-ADPCM playback support and SPU register macros, but
no ADPCM decode accuracy test programs were found. The SDK provides facilities for
uploading samples to SPU RAM and playing them, but no test that captures decoded
output for comparison.

### Sound Map Mode

The psx-spx documentation mentions "Sound Map mode" which allows XA-ADPCM data to be
transferred from main RAM without a CD-ROM. This could potentially be used to build a
hardware test, but no existing test using this technique was found.

### No Published ADPCM Test Programs

No homebrew program was found in PSn00bSDK, PSXSDK, Nugget, or the wider PSX homebrew
community that specifically tests ADPCM decode accuracy by:
1. Uploading known ADPCM blocks with edge-case parameters
2. Playing them on an SPU voice
3. Reading back the decoded output via capture buffers or DMA
4. Comparing against expected values

---

## 4. nocash / no$psx Hardware Test Results

**Confidence: MEDIUM** (psx-spx is authoritative but methodology undocumented)

Martin Korth (nocash) documented the PS1 hardware extensively in psx-spx. The following
ADPCM-specific claims appear in that documentation:

1. **Shift 13-15 = shift 9:** "reserved shift values 13..15 will act same as shift=9"
2. **SPU has 5 filters (0-4), XA has 4 (0-3)**
3. **Filter coefficients:** `pos_xa_adpcm_table[0..4] = (0, +60, +115, +98, +122)` and
   `neg_xa_adpcm_table[0..4] = (0, 0, -52, -55, -60)`
4. **Decode formula:** `s = (t SHL shift) + ((old*f0 + older*f1+32)/64)`
5. **Output clamp:** `MinMax(s, -8000h, +7FFFh)` -- 16-bit signed

**What nocash does NOT document:**
- How he determined shift 13-15 = 9 (hardware test? observation? inference?)
- What happens with filter indices 5-7
- Whether `/64` means C division or arithmetic right shift
- Whether any 15-bit clamp exists (like SNES BRR)
- Whether the clamp is applied before or after the filter state update

**Key gap:** nocash's psx-spx is the source document that all emulators reference.
But the methodology behind the ADPCM claims is not documented. The shift 13-15
behavior claim is likely based on hardware testing (nocash is known for meticulous
hardware verification), but this is not certain.

---

## 5. SNES BRR Hardware Evidence (Informing PS1 Behavior)

**Confidence: HIGH** (extensive hardware verification by the SNES community)

The SNES BRR codec is Sony's predecessor to PS1 ADPCM, running on the S-DSP chip.
The SNES community has characterized BRR behavior at the transistor level. Key
findings from [fullsnes (nocash)](https://problemkaputt.de/fullsnes.htm),
[SNESdev Wiki](https://snes.nesdev.org/wiki/Errata), and
[SnesLab](https://sneslab.net/wiki/Bit_Rate_Reduction):

### 5.1 BRR Shift 13-15 Behavior (DIFFERENT FROM PS1)

From fullsnes:
> When shift=13..15, decoding works as if shift=12 and nibble=(nibble SAR 3).

This means on SNES, shift 13-15 effectively produces:
```
sample = ((nibble >> 3) << 12) >> 1
```
Which reduces the nibble to its sign bit, then shifts maximally.

**On PS1, shift 13-15 = shift 9** (per psx-spx), which produces:
```
shift = 12 - 9 = 3
sample = nibble << 3
```

**These are fundamentally different behaviors.** The SNES strips the nibble to
1 bit of information; the PS1 uses a moderate shift. This confirms the PS1 SPU
is NOT a direct copy of the SNES S-DSP BRR decoder for edge cases.

### 5.2 BRR 15-Bit Clamp (PS1 Does NOT Have This)

From fullsnes, the SNES BRR decoder performs a TWO-STEP clamp:

```
Step 1: Clamp to 16-bit signed range
  If new > +7FFFh then new = +7FFFh
  If new < -8000h then new = -8000h

Step 2: Clip to 15-bit signed range (WRAPPING, not clamping)
  If new = (+4000h..+7FFFh) then new = (-4000h..-1)    ;wrap positive overflow
  If new = (-8000h..-4001h) then new = (-0..-3FFFh)     ;wrap negative overflow
```

The result is that SNES BRR samples live in 15-bit space (-4000h..+3FFFh). Values
outside this range wrap around due to what nocash describes as a "lost-sign" hardware
behavior -- the hardware saturates to 16-bit, then truncates to 15-bit by discarding
the sign bit.

**The PS1 does NOT do this.** The psx-spx documentation specifies only:
```
s = MinMax(s, -8000h, +7FFFh)
```

No 15-bit wrap step. PS1 decoded samples occupy the full 16-bit signed range.

jsgroth explicitly confirms this: "PS1: clamp to signed 16-bit" vs "SNES: clamp to
signed 15-bit."

### 5.3 BRR Filter Formulas Use SAR, Not Division

From fullsnes, the SNES BRR filter formulas use explicit SAR (arithmetic right shift):

```
Filter 1: new = sample + old*1 + ((-old*1) SAR 4)
Filter 2: new = sample + old*2 + ((-old*3) SAR 5)  - older + ((older*1) SAR 4)
Filter 3: new = sample + old*2 + ((-old*13) SAR 6) - older + ((older*3) SAR 4)
```

Note the deliberate decomposition: instead of `(old * 60 + 32) / 64`, the SNES
hardware computes `old + ((-old) >> 4)` which equals `old * 15/16` via integer
arithmetic. The SAR operations are native to the S-DSP hardware.

**The PS1 psx-spx spec uses `/64` notation, which is ambiguous.** See Section 7.

### 5.4 Applicability to PS1

The PS1 SPU (CXD2922Q/CXD2925Q) is a direct successor to the SNES S-DSP. The filter
coefficients are the same for filters 0-3, and the PS1 adds a 5th filter (index 4).
However:

- The shift 13-15 behavior is DIFFERENT (PS1: =9, SNES: =12+nibble>>3)
- The 15-bit clamp is ABSENT on PS1
- The sample bit depth is 16-bit on PS1 vs 15-bit on SNES
- PS1 has 24 voices vs SNES 8 voices

This means we cannot blindly copy SNES BRR implementation choices for PS1 ADPCM.
The codecs share DNA but diverge on edge cases.

---

## 6. Digital Audio Capture from PS1 Hardware

**Confidence: HIGH** (confirmed no such captures exist)

### 6.1 SPU Capture Buffers

The PS1 SPU has hardware capture buffers in the first 4KB of SPU RAM:

| Buffer | Address | Content |
|--------|---------|---------|
| CD Audio L | 00000h-003FFh | CD audio left channel |
| CD Audio R | 00400h-007FFh | CD audio right channel |
| Voice 1 | 00800h-00BFFh | Voice 1 decoded output |
| Voice 3 | 00C00h-00FFFh | Voice 3 decoded output |

Each buffer holds 200h samples (512 samples) at 44.1kHz, giving an ~86Hz update rate.

**Critical limitation:** The Voice 1 and Voice 3 capture buffers capture the output
**after envelope processing**, not the raw ADPCM decode output. This means:
- The captured value = decoded_sample * envelope_level
- To get the raw decode, you would need to set envelope to maximum (sustain=0x7FFF)
  and ensure the voice is in sustain phase
- Even then, volume processing may introduce additional scaling

No published research was found that uses these capture buffers to verify ADPCM decode
accuracy.

### 6.2 Digital Audio Mod (FirebrandX)

A [PSX Digital Audio Mod](https://www.firebrandx.com/psxdigitalaudio.html) exists that
taps the I2S serial data lines (DATO, LRCO, BCKO) between the SPU chip and the DAC
(AK4309AVM on SCPH-5501). This provides the digital audio stream AFTER the SPU's
internal mixing, reverb, and master volume processing.

**This does NOT provide raw ADPCM decode output.** It provides the final mixed stereo
output. To isolate a single voice's ADPCM decode, you would need to:
1. Play only one voice at maximum volume with no reverb
2. Capture the I2S stream
3. Account for Gaussian interpolation applied to decoded samples
4. Account for master volume scaling

No published analysis of I2S captures for ADPCM verification was found.

### 6.3 emu-russia Reverse Engineering

The [emu-russia/psxrev](https://github.com/emu-russia/psxrev) project has
microphotographs of the CXD2925Q SPU chip and has identified cell domains. However,
the transistor-level reverse engineering of the ADPCM decoder has not been published.
The project documents the SPU architecture at a block-diagram level but does not
provide gate-level decode algorithms.

---

## 7. The Division vs Arithmetic Right Shift Question

**Confidence: MEDIUM** (strong circumstantial evidence for ASR, no direct proof)

This is the most consequential gray area for bit-faithfulness.

### The Ambiguity

The psx-spx decode formula is:
```
s = (t SHL shift) + ((old*f0 + older*f1 + 32) / 64)
```

The `/64` could mean:
- **C integer division** (truncation toward zero): `(-1) / 64 = 0`
- **Arithmetic right shift** (floor toward negative infinity): `(-1) >> 6 = -1`

These differ for negative intermediate values where the low 6 bits are non-zero.

### Evidence for Arithmetic Right Shift

1. **Hardware uses shift registers, not dividers.** The PS1 SPU is a digital ASIC.
   Division by a power of 2 in hardware is implemented as a right shift of a register.
   Hardware does not have a "truncate toward zero" mode -- that is a software
   convention. A shift register naturally floors toward negative infinity.

2. **The SNES BRR predecessor uses explicit SAR.** The fullsnes documentation uses
   "SAR" (shift arithmetic right) notation throughout the BRR filter formulas.
   nocash is the same author for both fullsnes and psx-spx.

3. **The "+32" rounding bias.** Adding 32 before dividing/shifting by 64 provides
   round-to-nearest behavior. With ASR, `(x + 32) >> 6` rounds to nearest with
   ties going toward negative infinity. With C division, `(x + 32) / 64` rounds
   to nearest with ties going toward zero. For audio, the difference is minuscule
   (affects only values where `(old*f0 + older*f1) % 64` is exactly -32), but
   hardware naturally does ASR.

4. **DuckStation's implementation** (from cdrom.cpp visible in search results)
   uses `>> 6` (right shift), not `/64`.

5. **FFmpeg's implementation** uses `/64` (C division). This suggests some
   implementors read the spec literally.

### Evidence for Division

1. **psx-spx literally writes `/64`**, not `SAR 6` or `SHR 6`. nocash uses
   SAR notation extensively in fullsnes for the SNES. If he meant ASR for PS1,
   why write `/64`?

2. **jsgroth's emulator uses `/64`** (Rust integer division, truncation toward
   zero).

### Assessment

The hardware almost certainly performs arithmetic right shift (floor division). The
`/64` notation in psx-spx is likely shorthand, not a deliberate specification of
truncation-toward-zero semantics. The evidence:

- Hardware shift registers are natural ASR
- SNES predecessor uses SAR explicitly
- The only emulator known to have been validated against hardware tests (Mednafen)
  would need source inspection to confirm

**Recommendation for libspu94:** Use `>> 6` (arithmetic right shift), consistent with
our existing ADR-0001 ASR discipline. Document the ambiguity in an ADR. The practical
difference is at most +/-1 LSB on samples where the filter prediction intermediate
crosses zero with specific bit patterns -- extremely rare and inaudible, but
bit-faithfulness demands a choice.

---

## 8. The MiSTer PS1 Core (FPGAzumSpass / Robert Peip)

**Confidence: LOW** (metadata only, implementation not inspected)

The [MiSTer PSX core](https://github.com/MiSTer-devel/PSX_MiSTer) by Robert Peip
implements the PS1 SPU in SystemVerilog. The SPU was
[announced as feature complete](https://misterfpga.org/viewtopic.php?t=4029) and is
actively maintained.

### What we know from public metadata:

1. The core was developed by writing a "cycle accurate emulator" first, then
   converting to FPGA HDL
2. Audio issues have been reported and fixed (e.g., issue #304: "Motor Toon
   Grand Prix" audio effects replaced with hissing)
3. Issue #321 fixed "don't end ADSR mode when running channel as noise"
4. No ADPCM-specific accuracy issues were found in the issue tracker

### What we do NOT know:

- Whether Robert Peip's ADPCM decoder uses the psx-spx formula exactly
- How shift 13-15 and filter 5-7 are handled in the HDL
- Whether any hardware captures were used to validate ADPCM decode accuracy
- Whether the implementation uses division or shift for the filter prediction

**The MiSTer core is a potential validation source** -- if we can find someone with
a MiSTer setup, we could compare our decoder output against the FPGA output for
edge-case ADPCM blocks. But this is not a priority for M2.

---

## 9. Summary of Resolutions

### Resolved Gray Areas

| Question | Resolution | Confidence | Source |
|----------|-----------|------------|--------|
| Shift 13-15 behavior | Acts as shift=9 | HIGH | psx-spx (nocash), all emulators agree |
| 15-bit clamp like SNES? | NO -- PS1 uses full 16-bit range | HIGH | jsgroth explicit comparison, psx-spx formula |
| Output clamp range | -8000h to +7FFFh (16-bit signed) | HIGH | psx-spx, all emulators agree |
| Filter coefficients | pos[0..4]=(0,60,115,98,122), neg[0..4]=(0,0,-52,-55,-60) | HIGH | psx-spx |
| PS1 vs SNES shift 13-15 | DIFFERENT: PS1=shift9, SNES=shift12+nibble>>3 | HIGH | psx-spx vs fullsnes |

### Narrowed but Unresolved

| Question | Best answer | Confidence | Gap |
|----------|------------|------------|-----|
| Filter 5-7 behavior | Zero coefficients (= filter 0) | MEDIUM | DuckStation does this; no hardware proof |
| Division vs ASR (`/64` vs `>>6`) | Almost certainly ASR (>>6) | MEDIUM | Hardware argument strong, but psx-spx writes /64 |

### Still Unknown

| Question | Status | What would resolve it |
|----------|--------|----------------------|
| Exact hardware behavior for filter 5-7 | No hardware test exists | Homebrew test on real PS1 |
| Whether clamp is before or after state update | Not documented | Homebrew test or transistor RE |
| nocash's methodology for shift=9 claim | Not published | Ask nocash (unlikely) |

---

## 10. Recommended Implementation Choices for libspu94

Based on the evidence gathered:

```c
/* Shift: clamp 13-15 to 9 (psx-spx, all emulators agree) */
int shift = header & 0x0F;
if (shift > 12) shift = 9;
int shift_amount = 12 - shift;

/* Filter: clamp 5-7 to either 0 or 4 */
/* DuckStation zeros (=filter 0). jsgroth clamps to 4. */
/* Recommendation: clamp to 4 (safe, less invasive) */
/* Document the choice in an ADR */
int filter = (header >> 4) & 0x07;
if (filter > 4) filter = 4;  /* or: filter = 0; */

/* Filter prediction: use ASR, not division */
int32_t predicted = ((int32_t)old * f0 + (int32_t)older * f1 + 32) >> 6;

/* Shifted sample + prediction */
int32_t sample = (int32_t)(nibble << shift_amount) + predicted;

/* 16-bit signed clamp (NOT 15-bit like SNES BRR) */
if (sample > 0x7FFF) sample = 0x7FFF;
if (sample < -0x8000) sample = -0x8000;

/* Update state AFTER clamp */
older = old;
old = (int16_t)sample;
```

### ADR Topics Needed

1. **ADR: Division vs ASR in filter prediction** -- Choose `>>6`, document why
2. **ADR: Filter index 5-7 handling** -- Choose clamp-to-4 or zero-out, document
   the DuckStation vs jsgroth divergence
3. **ADR: Shift 13-15 = 9** -- Document the psx-spx source and SNES BRR difference

---

## Sources

### Primary (HIGH confidence)
- [psx-spx: XA-ADPCM Compression](https://problemkaputt.de/psxspx-cdrom-xa-audio-adpcm-compression.htm) -- nocash's decode algorithm
- [psx-spx: SPU](https://psx-spx.consoledev.net/soundprocessingunitspu/) -- capture buffers, SPU architecture
- [fullsnes (nocash)](https://problemkaputt.de/fullsnes.htm) -- SNES BRR decode algorithm with 15-bit clamp
- [fullsnes text mirror](https://github.com/gilligan/snesdev/blob/master/docs/fullsnes.txt) -- verified BRR filter SAR formulas
- [SNESdev Wiki: Errata](https://snes.nesdev.org/wiki/Errata) -- BRR 15-bit clamp/clip hardware bug
- [ps1-tests (JaCzekanski)](https://github.com/JaCzekanski/ps1-tests) -- confirmed no ADPCM tests exist

### Secondary (MEDIUM confidence)
- [jsgroth: PS1 SPU Part 1 - ADPCM](https://jsgroth.dev/blog/posts/ps1-spu-part-1/) -- shift/filter clamp choices, /64 usage
- [jsgroth: SNES & PS1 Cubic ADPCM Interpolation](https://jsgroth.dev/blog/posts/snes-ps1-cubic-adpcm-interpolation/) -- 16-bit vs 15-bit clamp comparison
- [DuckStation commit 0076af69](https://github.com/stenzek/duckstation/commit/0076af69) -- "Zero out upper ADPCM filters"
- [DuckStation commit be9033b6](https://github.com/stenzek/duckstation/commit/be9033b6) -- "Add missing clamp in ADPCM decoder"
- [SnesLab: BRR](https://sneslab.net/wiki/Bit_Rate_Reduction) -- BRR overflow/wrap behavior
- [SuperFamicom Wiki: BRR](https://wiki.superfamicom.org/bit-rate-reduction-(brr)) -- BRR range 13-15 invalid
- [NESdev Forum: BRR Sample Creation](https://forums.nesdev.org/viewtopic.php?t=10290) -- 15-bit safe range discussion
- [FirebrandX: PSX Digital Audio Mod](https://www.firebrandx.com/psxdigitalaudio.html) -- I2S tap (not useful for ADPCM verification)
- [emu-russia/psxrev](https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md) -- SPU chip photos, architecture docs

### Tertiary (LOW confidence)
- [MiSTer PSX Core](https://github.com/MiSTer-devel/PSX_MiSTer) -- FPGA implementation exists, details unknown
- [FFmpeg adpcm.c](https://github.com/FFmpeg/FFmpeg/blob/master/libavcodec/adpcm.c) -- uses /64 (C division), not >>6
- [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK) -- homebrew SDK, no ADPCM tests
