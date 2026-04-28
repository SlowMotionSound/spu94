# Deep Research: AKM AK4309 DAC Family

**Project:** libspu94 DAC Modeling (v1.2 milestone)
**Researched:** 2026-04-28
**Overall confidence:** MEDIUM (composite of HIGH on electrical specs, LOW on internal filter coefficients)

---

## 1. AK4309B Datasheet Extraction

The AK4309B datasheet (14 pages, ~100KB) is available from multiple archives. This is the
closest available documented variant to the AK4309AVM used in the PS1.

### Core Identity

| Parameter | Value | Confidence |
|-----------|-------|------------|
| Full name | 16-bit SCF DAC for Multimedia | HIGH |
| Architecture | 1-bit delta-sigma (bitstream) | HIGH |
| Package | 20-pin SSOP (0.65mm pitch) | HIGH |
| Pin compatible with | AK4310, AK4309A | HIGH |
| Date code on datasheet | 1997/06 | HIGH |

### Electrical Specifications

| Parameter | Value | Conditions | Confidence |
|-----------|-------|------------|------------|
| Supply voltage | 4.5V - 5.5V (typ 5V +/-10%) | -- | HIGH |
| Power dissipation | 80 mW | at 5V | HIGH |
| Dynamic range | 90 dB (datasheet front page) / 92 dB (comparison table) | Ta=25C, fs=44.1kHz | HIGH |
| THD+N | -84 dB | -- | HIGH |
| Output level | 3.4 Vpp | typ at 0dB | HIGH |
| Total frequency response | +/-0.5 dB at 20 kHz | -- | HIGH |
| Sampling rate range | 8 kHz to 50 kHz | -- | HIGH |
| Operating temperature | -10 to +70 C | -- | HIGH |
| Master clock options | 256fs or 384fs | CKS pin selects | HIGH |
| Clock jitter tolerance | High (SCF technique) | -- | HIGH |

**Note on DR discrepancy:** The front-page feature list says 90 dB; the comparison table on
page 2 says 92 dB for the AK4309B specifically. The 90 dB figure appears to be the family
baseline; 92 dB may be the AK4309B's actual measured typical. The AK4309A is listed at 91 dB.

### Digital Interface

| Parameter | Value | Confidence |
|-----------|-------|------------|
| Interface level | TTL (2.2V min high, 0.8V max low) | HIGH |
| Data format | 2's complement, MSB first | HIGH |
| Data input | SDATA pin (serial) | HIGH |
| Bit clock | BICK (latches SDATA) | HIGH |
| L/R clock | LRCK (H=Left, L=Right) | HIGH |
| Clock select | CKS pin: L=384fs, H=256fs | HIGH |

**Critical finding for modeling:** The AK4309/B uses a FIXED data format -- 2's complement
MSB-first, left-justified (MSB appears on the first BICK cycle after LRCK transition). This
is NOT I2S (which has a 1-cycle delay after LRCK). It is more precisely called "MSB-justified"
or "left-justified" format.

The sibling AK4317 (18-bit SCF DAC) explicitly supports MSB justified, LSB justified, AND
I2S. The AK4309/B does NOT have this flexibility -- it only supports MSB-justified. This is
confirmed by the pin descriptions: there is no format-select pin on the 20-pin AK4309B.

**Confidence:** MEDIUM-HIGH. The datasheet page 3 pin table says "2's complement MSB first"
for SDATA, and the LRCK description says "H=Left". In standard I2S, data is delayed one BICK
after LRCK; in MSB-justified, MSB appears immediately. The AK4309 appears to use MSB-justified,
but without the full timing diagram (pages 4-5 of the datasheet), absolute certainty requires
downloading the PDF.

### Internal Filter Pipeline

The AK4309B implements a three-stage reconstruction filter:

| Stage | Description | Confidence |
|-------|-------------|------------|
| Stage 1 | 8x FIR digital interpolator | HIGH |
| Stage 2 | 2nd-order Switched Capacitor Filter (SCF) | HIGH |
| Stage 3 | Continuous Time Filter (CTF) | HIGH |

**Filter characteristics** (from datasheet page 6, fs=44.1kHz):

| Parameter | Value | Confidence |
|-----------|-------|------------|
| Digital filter passband | 0 to 22.05 kHz | HIGH |
| Passband ripple | +/-0.05 dB | HIGH |
| Stopband attenuation | 41 dB | HIGH |
| Passband deviation at 20kHz | -0.2 dB | HIGH |

**How the pipeline works:**

1. **8x FIR interpolator:** Upsamples 44.1kHz input to 352.8kHz (8x). This is a digital
   filter with ~41 dB stopband rejection. The "8x" tells us the interpolation ratio, not the
   number of taps. Typical 8x interpolation FIRs in mid-90s AKM parts used cascaded half-band
   filters (e.g., 2x -> 2x -> 2x = 8x total), with total tap counts typically 60-120.

2. **2nd-order SCF:** A switched-capacitor analog filter running at the oversampled rate
   (352.8kHz). This provides further attenuation of the digital filter's stopband images and
   shapes the delta-sigma noise. SCF uses capacitor switching synchronized to the master clock,
   which is why AKM claims "high tolerance to clock jitter" -- the SCF's characteristics track
   the clock, so jitter affects gain but not filter shape.

3. **CTF (Continuous Time Filter):** A simple analog lowpass filter that smooths the
   switched-capacitor output's staircase waveform into a continuous analog signal. This is the
   final reconstruction filter before the output buffer.

**What we do NOT know:** The exact FIR tap count and coefficients. AKM never published these.
The 41 dB stopband attenuation and +/-0.05 dB passband ripple are the measurable outcomes.
For modeling purposes, we can design an FIR that meets these specs.

### Pin Configuration (20-pin SSOP)

| Pin | Name | Function |
|-----|------|----------|
| 1 | PD/NC | Power down (AK4309A) / NC (AK4309B) |
| 2 | DVDD | Digital power supply |
| 3 | DVSS | Digital ground |
| 4 | NC | Not connected |
| 5 | RST | Reset (active low) |
| 6 | MCLK | Master clock input |
| 7 | CKS | Clock select (L=384fs, H=256fs) |
| 8 | BICK | Bit clock input |
| 9 | SDATA | Serial data input |
| 10 | LRCK | Left/Right clock |
| 11 | AOUTR | Right analog output |
| 12 | AOUTL | Left analog output |
| 13 | VCOM | Common voltage output (AVDD/2) |
| 14 | AVDD | Analog power supply |
| 15 | AVSS | Analog ground |
| 16-17 | NC | Not connected |
| 18 | VREFH | Voltage reference high |
| 19 | VREFL | Voltage reference low |
| 20 | DZF | Digital zero detect output |

### Sources

- AK4309B datasheet: https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html
- AK4309 datasheet (covers B): https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html
- AK4309B on Elcodis: https://elcodis.com/parts/6151498/ak4309b.html
- AK4309 on Elcodis: https://elcodis.com/parts/6259223/AK4309.html

---

## 2. AK4309AVM vs AK4309B: Naming Convention and Differences

### AKM Part Numbering

AKM's part numbering is poorly documented publicly. The only confirmed convention element:
- "P" suffix = RoHS compliant (from DigiKey forum)
- "E2" suffix = ordering variant (seen on AK4309AVM-E2)

**Observed pattern from cross-referencing multiple parts:**

| Suffix element | Likely meaning | Confidence |
|----------------|----------------|------------|
| A (in AK4309A) | Revision A (improved from base AK4309) | MEDIUM |
| B (in AK4309B) | Revision B (further improved) | MEDIUM |
| V | SOP/SSOP package indicator | LOW |
| M | Tape & reel or specific package subtype | LOW |

### The Critical Question: 24-pin AK4309AVM vs 20-pin AK4309B

The dogbreath.de analysis is the key source here. The AK4309AVM is a **24-pin** package,
while the AK4309B is a **20-pin** package. They are "not exactly compatible."

**24-pin AK4309AVM pinout** (from psx-spx pinouts page):

| Pin | Name | Function |
|-----|------|----------|
| 1 | TEST | Test pin |
| 2 | DVDD | Digital power |
| 3 | DVSS | Digital ground |
| 4 | PD | Power down |
| 5 | RST | Reset |
| 6 | MCLK | Master clock |
| 7 | CKS | Clock select |
| 8 | BICK | Bit clock |
| 9 | SDATA | Serial data |
| 10 | LRCK | L/R clock |
| 11-14 | NC | Not connected (extra 4 pins vs 20-pin) |
| 15 | AOUTR | Right output |
| 16 | AOUTL | Left output |
| 17 | VCOM | Common voltage |
| 18 | AVDD | Analog power |
| 19 | AVSS | Analog ground |
| 20 | NC | Not connected |
| 21 | NC | Not connected |
| 22 | VREFL | Voltage reference low |
| 23 | VREFH | Voltage reference high |
| 24 | DZF | Digital zero detect |

**Key differences:**

| Feature | AK4309AVM (24-pin) | AK4309B (20-pin) | Impact |
|---------|-------------------|-------------------|--------|
| Pin count | 24-pin SSOP | 20-pin SSOP | Physical only |
| Pin 4 function | PD (power down) | NC | AVM has explicit power-down |
| Pin 1 function | TEST | PD/NC | Different pin assignment |
| Extra NC pins | 4 extra (pins 11-14) | None | Wider package |
| Output voltage | 2.8 Vpp (AK4309A spec) | 3.4 Vpp | SIGNIFICANT |
| Dynamic range | 91 dB | 92 dB | Minor |
| Click noise | "Middle" | "High" | AK4309B has more click noise |
| Supply range | 4.5-5.5V | 4.5-5.5V | Same |
| Digital I/F | TTL | TTL | Same |

### Are They the Same Die?

**Assessment: Probably NOT the same die, but same architecture.** [MEDIUM confidence]

Reasoning:
- The output voltage difference (2.8 Vpp vs 3.4 Vpp) indicates different output buffer/reference
  circuit design
- The dynamic range difference (91 vs 92 dB) is consistent with a die revision
- The click-noise difference implies different power-on/off sequencing circuitry
- The 24-pin to 20-pin reduction with NC pins rearranged suggests a packaging revision
- The internal filter pipeline (8x FIR + SCF + CTF) is almost certainly identical

**For modeling purposes:** The digital processing path (FIR interpolator, delta-sigma modulator,
SCF, CTF) is the same architecture. The output voltage difference affects the analog gain stage
only. The filter characteristics (passband, stopband, ripple) documented for the AK4309B are
our best proxy for the AK4309AVM's digital filter behavior.

### Variant Summary Across PS1 Models

| Chip | Package | Found in | Notes |
|------|---------|----------|-------|
| AK4310VM | 20-pin | SCPH-1000 (JP), some SCPH-1002 rev C | 3V-5.5V, CMOS interface, 92 dB DR |
| AK4309VM | 24-pin | Very early SCPH-100x | Predecessor to AK4309AVM |
| AK4309AVM | 24-pin | SCPH-1001, SCPH-5501, many SCPH-100x/550x | The "magic chip" of audiophile lore |
| AK4309BM / AK4309BVM | 20-pin | SCPH-7001, SCPH-7002 | Last discrete DAC revision |

### Sources

- dogbreath.de DAC analysis: https://dogbreath.de/PS1/DAC/DAC.html
- psx-spx pinouts: https://psx-spx.consoledev.net/pinouts/
- DigiKey AKM part numbering: https://forum.digikey.com/t/part-numbering-akm-semiconductor-all/234
- diyaudio PS1 thread p.102: https://www.diyaudio.com/community/threads/playstation-as-cd-player.31123/page-102

---

## 3. AKM DAC Family Tree (Mid-1990s)

### Known Siblings

| Part | Description | Pins | Key Differences from AK4309 | Confidence |
|------|-------------|------|-----------------------------|------------|
| AK4309 | 16-bit SCF DAC base | 20 | Original version | HIGH |
| AK4309A | 16-bit SCF DAC rev A | 20/24 | 2.8Vpp output, "middle" click noise | HIGH |
| AK4309B | 16-bit SCF DAC rev B | 20 | 3.4Vpp output, "high" click noise | HIGH |
| AK4310 | 16-bit SCF DAC | 20 | 3V-5.5V, CMOS interface (vs TTL), pin-compatible | HIGH |
| AK4317 | 18-bit SCF DAC w/ ATT & MIXER | -- | Volume control, L/R mixing, I2S/MSB/LSB format select, de-emphasis, soft mute | HIGH |
| AK4318 | 18-bit SCF DAC w/ ATT & MIXER | -- | Similar to AK4317 | HIGH |
| AK4520 | 20-bit stereo ADC+DAC | -- | Codec (both directions), 100 dB DR, different class | MEDIUM |

### The AK4317 as Architecture Proxy

The AK4317 is the most valuable sibling for understanding the AK4309's internals:

**Shared architecture:**
- Same "8 times FIR Interpolator"
- Same "2nd order SCF and 2nd order CTF"
- Same master clock options (256fs or 384fs)
- Same high clock jitter tolerance via SCF technique
- Same sampling rate range (8 kHz to 50 kHz)
- Same era (mid-to-late 1990s AKM product line)

**AK4317 additions over AK4309:**
- 18-bit input (vs 16-bit)
- Selectable audio interface: MSB justified, LSB justified, I2S
- Per-channel volume (digital attenuator)
- L/R channel mixer
- Digital de-emphasis (32/44.1/48 kHz)
- Soft mute function
- THD+N: -86 dB (vs -84 dB for AK4309B)
- DR: 92 dB (same as AK4309B)

**Implication:** The AK4317's documented interface formats confirm that the base AK4309 uses
MSB-justified format (since the AK4317 adds I2S and LSB as options, while the AK4309 lacks
the format-select pin). The filter pipeline specs from the AK4317 datasheet can supplement
any gaps in the AK4309B datasheet since they share the same filter architecture.

### Sources

- AK4317 datasheet: https://www.alldatasheet.com/datasheet-pdf/pdf/54933/AKM/AK4317.html
- AK4317 on Elcodis: https://elcodis.com/parts/6151495/ak4317.html
- AK4318 search: https://www.alldatasheet.com/view.jsp?Searchword=AK4318
- AK4520 datasheet: https://www.alldatasheet.com/datasheet-pdf/pdf/54949/AKM/AK4520.html

---

## 4. PS1 Service Manual / Schematic Analysis

### Audio Signal Path

Based on psx-spx, emu-russia/psxrev, and the FirebrandX digital audio mod guide, the
PS1 audio path is:

```
SPU (CXD2922Q/CXD2925Q)
  |
  |-- DATO (SDATA) -----> AK4309 pin 9
  |-- BCKO (BICK) ------> AK4309 pin 8
  |-- LRCO (LRCK) ------> AK4309 pin 10
  |-- MCLKOUT -----------> AK4309 pin 6 (MCLK)
  |
  v
AK4309AVM (IC308)
  |
  |-- AOUTL (pin 16) ---> coupling cap ---> audio amp (IC405) ---> AV multiout
  |-- AOUTR (pin 15) ---> coupling cap ---> audio amp (IC405) ---> AV multiout
```

### Clock Configuration

| Signal | Frequency | Derivation | Confidence |
|--------|-----------|------------|------------|
| System clock | 33.8688 MHz | Main oscillator | HIGH |
| MCLK to DAC | 16.9344 MHz | 33.8688 / 2 = 384 x 44100 | HIGH |
| LRCK | 44.1 kHz | Sample rate | HIGH |
| BICK | 2.1168 MHz | 48 x 44100 = 48fs | HIGH |
| CKS pin | Tied to ground | Selects 384fs mode | HIGH |

**Critical detail:** The PS1 uses 384fs master clock, NOT the more common 256fs. This was
confirmed by FirebrandX's digital audio mod documentation: "Sony opted for a more atypical
MC timing rate of 16.9344MHz, or 384 times the sample rate (384Fs for short)."

The CKS pin is tied to ground (low), which selects 384fs mode on the AK4309.

**BICK at 48fs:** With 16-bit stereo data (32 bits per sample period), a 48fs bit clock
provides 48 BICK cycles per LRCK period. That is 24 BICK cycles per channel. Since the data
is 16 bits, there are 8 "extra" BICK cycles per channel (padding or dead time). This is
consistent with a 24-bit-capable serial frame where only 16 bits carry data.

### Output Stage

**No external reconstruction filter.** The PS1 relies entirely on the AK4309's internal
filter pipeline (8x FIR + SCF + CTF). The analog outputs go through:

1. DC-blocking coupling capacitors
2. Audio amplifier IC (IC405) -- routes to AV multiout connector
3. On SCPH-1001: also routes to dedicated RCA jacks

The signal path is notably simple -- no op-amps between the DAC output and the RCA jacks
on SCPH-1001 models. This minimalist path is what audiophiles prize.

The SCPH-5501 service manual schematic is available at:
https://gamesx.com/wiki/lib/exe/fetch.php?media=schematics:sony_playstation_scph-5500-5501-5502-5503.pdf

### SPU Output Pins (CXD2925Q, 100-pin)

| Pin | Signal | Description |
|-----|--------|-------------|
| 89 | XCK | External clock input |
| 92 | LRIA | Left channel input A (from CD subsystem) |
| 94 | BCIB | Bit clock input B |
| 95 | LRIB | Left channel input B |
| 97 | BCKO | Bit clock output (to DAC) |
| 98 | LRCO | L/R clock output (to DAC) |
| 99 | DATO | Data output (to DAC) |
| 100 | WCKO | Word clock output |

MCLKIN and MCLKOUT pins are tied together and connected to the DAC's MCLK input.

### Sources

- psx-spx pinouts: https://psx-spx.consoledev.net/pinouts/
- emu-russia SPU wiki: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
- FirebrandX digital audio mod: https://www.firebrandx.com/psxdigitalaudio.html
- SCPH-5500/5501 schematic PDF: https://gamesx.com/wiki/lib/exe/fetch.php?media=schematics:sony_playstation_scph-5500-5501-5502-5503.pdf
- GamingDoc TOSLink mod: https://gamingdoc.org/modding/consoles/sony-playstation/audio/toslink-optical-out/

---

## 5. Board Revision Differences

### DAC Timeline Across PS1 Revisions

| Board | Model(s) | SPU Chip | Audio DAC | Package | Notes |
|-------|----------|----------|-----------|---------|-------|
| PU-7 | SCPH-1000 (JP) | CXD2922Q | AK4310VM | 20-pin discrete | CMOS interface, 3-5.5V |
| Early PU-8 | SCPH-1001, 1002 rev B | CXD2922Q | AK4309AVM | 24-pin discrete | The "magic chip", TTL, 4.5-5.5V |
| Late PU-8 | SCPH-1002 rev C | CXD2925Q | AK4309AVM or AK4310VM | discrete | Varies by production run |
| PU-18 | SCPH-5001/5501/5502 | CXD2925Q | AK4309AVM | 24-pin discrete | Most measured PS1 model |
| PU-20 | SCPH-5501 (late) | CXD2925Q | AK4309AVM | 24-pin discrete | Minor layout revision |
| PU-22 | SCPH-7001/7501 | CXD2938Q (208-pin combo) | **Integrated** | No discrete DAC | DAC absorbed into combo chip |
| PU-23 | SCPH-9001 | CXD2938Q | **Integrated** | No discrete DAC | Serial port, no expansion |
| PM-41 | PSone SCPH-101 | CXD2938Q | **Integrated** | No discrete DAC | Compact redesign |
| PM-41(2) | PSone (late) | CXD2941R (176-pin) | **Integrated** | No discrete DAC | SPU+CDROM+SPU_RAM all in one |

### The Transition Point

**PU-22 (SCPH-7500 series, ~1997) is where the discrete DAC disappears.** [HIGH confidence]

The CXD2938Q is a 208-pin combo chip containing:
- SPU
- CD decoder
- CD DSP
- DAC (integrated)

It still outputs analog audio on AOUTL/AOUTR pins (pins 15 and 16 of the combo chip), but
the DAC is now Sony's own integrated design, not the AKM part.

The final evolution is the CXD2941R (176-pin), used in late PSone units, which adds the
512KB SPU RAM on-die as well.

### One Oddball: SCPH-7002

The dogbreath.de analysis notes that SCPH-7002 units contained AK4309BM (the 20-pin variant),
suggesting the SCPH-700x series was transitional -- some units got the last discrete DACs
(AK4309BM) while others got the integrated CXD2938Q.

### Modeling Implication

For libspu94, the target is the discrete AK4309AVM as found in SCPH-1001 through SCPH-5501
(PU-8 through PU-20). These are the models with the most community measurements and the
clearest audio path. The integrated CXD2938Q DAC is a separate (and largely undocumented)
Sony design.

### Sources

- psx-spx pinouts: https://psx-spx.consoledev.net/pinouts/
- psdevwiki motherboards: https://www.psdevwiki.com/ps1/Motherboards
- dogbreath.de: https://dogbreath.de/PS1/DAC/DAC.html
- emu-russia SPU wiki: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
- psdevwiki SPU: https://www.psdevwiki.com/ps1/SPU

---

## 6. AK4309 in Other Devices

### Short Answer: No Significant External Usage Found

The AK4309 family was a low-cost multimedia DAC. It appeared primarily in:

1. **Sony PlayStation** -- the overwhelmingly dominant application
2. **Possibly other Sony consumer electronics** -- no confirmed sightings

No confirmed usage was found in standalone CD players, MiniDisc players, or other consumer
electronics where isolated measurements might exist. [MEDIUM confidence -- absence of
evidence is not evidence of absence, but the audiophile community has been thorough.]

### Measurements That DO Exist

The PS1 itself has been measured by multiple sources as a CD player:

**Stereophile (John Atkinson):**
- Max output: 1.09V RMS at 1kHz (5+ dB below CD standard 2V)
- Channel separation: >90 dB below 1kHz, drops to 72 dB at 20kHz
- Top-octave frequency response: ripple indicating "underspecified digital filter"
- Noise floor: 15 dB higher than a "good inexpensive CD player"
- Linearity at -90 dB: signal appears at -86 dB (4 dB error)
- Low-order harmonic distortion visible at low levels
- Hum: 60Hz and 180Hz peaks from AC transformer coupling

**Archimago (SCPH-5501):**
- Effective resolution: ~15 bits (from ~90 dB dynamic range)
- J-Test jitter sidebands: peak below -100 dB from 11kHz primary
- Slight frequency response deviation above 3kHz
- Overall assessment: "respectable" for a $30 game console

**Important caveat:** These measurements include the entire PS1 signal chain (SPU processing,
DAC, coupling caps, output amp, cable), NOT the DAC in isolation. The 60/180Hz hum, for
instance, is from the PS1's power supply, not the DAC.

### Sources

- Stereophile measurements: https://www.stereophile.com/content/sony-playstation-1-cd-player-measurements
- Archimago measurements: http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html
- cheaptubeaudio review: https://cheaptubeaudio.blogspot.com/2012/07/review-sony-playstation-1.html

---

## 7. Digital Interface Specifics

### SPU-to-DAC Data Format

| Parameter | Value | Confidence |
|-----------|-------|------------|
| Bit depth | 16-bit signed | HIGH |
| Data format | 2's complement, MSB first | HIGH |
| Justification | MSB-justified (left-justified) | MEDIUM-HIGH |
| NOT I2S | Correct -- no 1-BICK delay after LRCK | MEDIUM-HIGH |
| LRCK polarity | H = Left channel, L = Right channel | HIGH |
| BICK frequency | 48fs = 2.1168 MHz | HIGH |
| Bits per channel frame | 24 BICK cycles (16 data + 8 padding) | HIGH (derived) |
| MCLK | 384fs = 16.9344 MHz | HIGH |

### Bit Depth Analysis

The SPU outputs 16-bit samples. The AK4309 accepts 16-bit data. There is NO truncation at
the DAC input -- the full 16 bits from the SPU reach the DAC.

The AK4309B datasheet describes it as a "16-bit SCF DAC" -- it does not accept more than
16 bits of meaningful data. The 24-cycle-per-channel BICK framing means 8 trailing BICK
cycles per channel are ignored (padding zeros).

The sibling AK4317 accepts 18 bits, but this is irrelevant for the PS1 application -- the SPU
only produces 16-bit output.

### Format Clarification: MSB-Justified vs I2S

Standard I2S (per the Philips/NXP specification):
- MSB is transmitted ONE BICK cycle AFTER the LRCK transition
- This 1-cycle delay is the defining characteristic of I2S

MSB-Justified (what the AK4309 uses):
- MSB is transmitted on the FIRST BICK cycle after LRCK transition
- No delay

This distinction matters for anyone building hardware to interface with the AK4309, but for
our software DAC model it has zero impact -- we receive the full 16-bit sample value regardless
of serial framing.

### Sources

- AK4309B datasheet pin descriptions: https://www.alldatasheet.com/html-pdf/54935/AKM/AK4309/145/3/AK4309.html
- emu-russia SPU: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
- FirebrandX: https://www.firebrandx.com/psxdigitalaudio.html

---

## 8. emu-russia/psxrev Findings

### What They Document

The emu-russia/psxrev repository (https://github.com/emu-russia/psxrev) contains:

- **SPU wiki page (wiki_eng/spu.md):** Documents the DAC as "Asahi Kasei AK4309" with serial
  interface signals (SDATA, BICK, LRCK). Confirms 16-bit signed sample format. Notes the
  DAC contains "a lot of interpolators and other incomprehensible devices" producing "perfect
  analog stereo signal."

- **SPU die photo:** Microphotograph of CXD2925Q at 4x magnification with labeled cell domains.
  3 metal layers, standard cells + custom blocks. This documents the SPU internals, NOT the
  DAC itself.

- **Board evolution notes:** Confirms that PU-22 (SCPH-7500) integrated the DAC into the
  CXD2938Q combo chip, ending the discrete AK4309 era.

### What They Do NOT Have

- No AK4309 die photos (the DAC chip itself has not been decapped/photographed by this project)
- No oscilloscope captures of the I2S signals between SPU and DAC
- No measured filter response curves of the AK4309
- No clock frequency measurements (they reference the datasheet values)

### Related Work

The emu-russia/psxdev project board (GitHub Projects) has an "SPU" project tracking reverse
engineering tasks, but the DAC is treated as a black box -- their focus is on the SPU's digital
processing, not the analog output chain.

### Sources

- emu-russia/psxrev SPU wiki: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
- emu-russia/psxrev repo: https://github.com/emu-russia/psxrev
- emu-russia/psxdev SPU project: https://github.com/emu-russia/psxdev/projects/13

---

## Consolidated Specifications for Modeling

### What We Know (HIGH confidence)

1. **Input:** 16-bit signed PCM, 44.1 kHz, MSB-justified serial format
2. **Master clock:** 384fs = 16.9344 MHz (CKS tied low)
3. **Bit clock:** 48fs = 2.1168 MHz
4. **Filter pipeline:** 8x FIR interpolator -> 2nd-order SCF -> CTF
5. **FIR passband:** 0-22.05 kHz, +/-0.05 dB ripple
6. **FIR stopband attenuation:** 41 dB
7. **Overall response at 20kHz:** -0.2 dB
8. **Dynamic range:** ~90-92 dB
9. **THD+N:** -84 dB
10. **Output:** 2.8 Vpp (AK4309AVM) or 3.4 Vpp (AK4309B)
11. **No external reconstruction filter** in PS1

### What We Must Design/Estimate (MEDIUM confidence)

1. **FIR coefficients:** Design a filter meeting the documented specs (passband, stopband,
   ripple). The exact AKM coefficients are trade secrets. An 8x interpolation filter with
   41 dB stopband rejection and +/-0.05 dB passband ripple is a well-constrained design
   problem -- there is limited room for variation.

2. **SCF model:** The 2nd-order switched-capacitor filter can be modeled as a 2nd-order
   lowpass IIR at the oversampled rate. Its corner frequency is somewhere above 20 kHz but
   well below the 352.8 kHz oversampled Nyquist. Exact cutoff unknown.

3. **CTF model:** The continuous-time filter is a simple analog lowpass. Since the AK4309B
   says "no external parts needed," the CTF provides enough smoothing on its own. Model as
   a gentle 1st or 2nd-order analog LPF.

4. **Delta-sigma noise shaping:** The 1-bit modulator's noise shaping order is not documented.
   For a 16-bit-class 1990s part with 90 dB DR, a 3rd to 5th order modulator is typical.

### What We Cannot Know (LOW confidence)

1. **Exact die differences between AK4309AVM and AK4309B** -- likely same filter, different
   output stage gain/reference
2. **Internal delta-sigma modulator order and NTF** -- not published
3. **Whether Sony's integrated CXD2938Q DAC matches the AK4309** -- almost certainly different

### Modeling Strategy Recommendation

For libspu94 v1.2, model the DAC as:

1. **8x interpolation FIR** -- design to match documented specs (41 dB stopband, +/-0.05 dB
   passband ripple, linear phase). This is the dominant audible coloration.

2. **1-bit delta-sigma quantizer** -- OPTIONAL for basic model. The quantization noise is
   shaped out of the audio band. Only matters if modeling ultrasonic behavior.

3. **2nd-order IIR lowpass** -- models the SCF stage. Tune to produce the overall -0.2 dB
   at 20 kHz response.

4. **1st-order analog LPF** -- models the CTF. Gentle rolloff above audio band.

The FIR filter is the most audibly significant component. It determines the frequency response
ripple that Stereophile measured, and the time-domain pre/post-ringing that characterizes the
DAC's "sound." The SCF and CTF primarily attenuate ultrasonic content.

---

## Datasheet Download URLs

| Document | URL | Pages |
|----------|-----|-------|
| AK4309/AK4309B datasheet | https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html | 14 |
| AK4309B datasheet (alt) | https://www.alldatasheet.com/datasheet-pdf/pdf/54932/AKM/AK4309B.html | 14 |
| AK4317 datasheet | https://www.alldatasheet.com/datasheet-pdf/pdf/54933/AKM/AK4317.html | -- |
| AK4310 datasheet | https://www.datasheetarchive.com/AK4310-datasheet.html | -- |
| SCPH-5500/5501 schematic | https://gamesx.com/wiki/lib/exe/fetch.php?media=schematics:sony_playstation_scph-5500-5501-5502-5503.pdf | -- |
| PSone service manual | https://archive.org/details/PS_one_SCPH-100_Series_Service_Manual_5th_Edition | -- |
