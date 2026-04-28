# Deep Technical Research: PS1 Audio Circuit and Signal Path

**Researched:** 2026-04-28
**Overall confidence:** MEDIUM-HIGH (hardware path well-documented; some analog stage details require service manual verification)

---

## 1. Complete PS1 Audio Signal Path: SPU to RCA Jacks

**Confidence: HIGH** (corroborated across psx-spx, emu-russia/psxrev, firebrandx digital audio mod, dogbreath.de, Archimago measurements)

### Block Diagram (Text)

```
                   DIGITAL DOMAIN                          ANALOG DOMAIN
  +-----------+                                  
  | 24 Voices |--+                               
  | (ADPCM)   |  |   +-------+    I2S Bus     +----------+     +----------+
  +-----------+  +-->|       |   (3 wires)    |          |     |          |
                     | SPU   |=== DATO ======>| AK4309VM |---->| Output   |---> RCA / 
  +-----------+  +-->| Mixer |=== LRCO ======>| (DAC)    |     | Stage    |     Multiout
  | CD Audio  |--+   |       |=== BCKO ======>|          |     |          |
  | (from     |  +-->|       |                +----------+     +----------+
  |  CD-ROM   |  |   +-------+                     ^
  |  decoder) |  |       |                         |
  +-----------+  |       v                    MCLK from SPU
                 |   +--------+                (XCK pin)
  +-----------+  |   | Reverb |
  | External  |--+   | Engine |
  | Audio In  |      +--------+
  +-----------+
```

### Signal Chain (SCPH-1001 / PU-8, the "classic" path):

1. **SPU chip** (IC308, CXD2922Q or CXD2925Q, 100-pin QFP)
   - Performs all digital mixing: 24 ADPCM voices + CD audio + external audio + reverb
   - Outputs final stereo mix as serial I2S data

2. **I2S bus** (3 signal wires + clock):
   - DATO (pin 99): Serial audio data
   - LRCO (pin 98): Left/Right word clock at 44,100 Hz
   - BCKO (pin 97): Bit clock at ~2.1168 MHz (48fs)
   - XCK (pin 89): Master clock output to DAC MCLK

3. **AK4309VM DAC** (IC402, 24-pin SSOP) -- on boards with external DAC only:
   - Receives I2S serial data on SDATA (pin 9), BICK (pin 8), LRCK (pin 10)
   - MCLK (pin 6) from SPU XCK
   - Outputs analog stereo on AOUTL (pin 16) and AOUTR (pin 15)
   - Output level: ~2.8 Vpp (~1.0 Vrms), lower than standard CD player line level

4. **Post-DAC analog stage** (varies by board revision -- see section 3)

5. **Output connectors:**
   - SCPH-1001 only: Dedicated RCA jacks (L/R/composite video) -- goes through NJM2100 op-amp buffer
   - All models: AV Multi-Out connector (pins 9=Audio L, 11=Audio R, 10/12=GND)

### Sources:
- psx-spx pinouts: https://psx-spx.consoledev.net/pinouts/
- emu-russia psxrev SPU: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md
- Firebrandx digital audio mod: https://www.firebrandx.com/psxdigitalaudio.html
- dogbreath.de DAC page: https://dogbreath.de/PS1/DAC/DAC.html

---

## 2. SPU to DAC Digital Interface

**Confidence: HIGH** (confirmed by firebrandx I2S tap measurements, psx-spx pinouts, AK4309 datasheet summary)

### Bus Protocol: I2S (Inter-IC Sound)

The SPU sends audio to the DAC over a standard **I2S serial bus** with these signals:

| Signal | SPU Pin | AK4309 Pin | Function | Frequency |
|--------|---------|------------|----------|-----------|
| DATO   | 99      | SDATA (9)  | Serial audio data, MSB first | Clocked by BCKO |
| LRCO   | 98      | LRCK (10)  | Word select (L/R channel) | 44,100 Hz (1fs) |
| BCKO   | 97      | BICK (8)   | Bit clock | 2.1168 MHz (48fs) |
| XCK    | 89      | MCLK (6)   | Master clock | 16.9344 MHz (384fs) |

### Clock Math

- System crystal: **67.7376 MHz** (X101, 4-pin, NTSC; divided by 2 internally to get CPU clock)
- CPU clock: **33.8688 MHz** (67.7376 / 2)
- SPU sample rate: **44,100 Hz** (33,868,800 / 768 = 44,100 exactly)
- Master clock to DAC: **16.9344 MHz** = 384 x 44,100 Hz = 384fs
  - This is 33.8688 MHz / 2
- Bit clock: **2.1168 MHz** = 48 x 44,100 Hz = 48fs
  - 48 bits per sample period = 2 channels x 24 bit slots (16 data + 8 padding, standard I2S)

### CKS Pin Configuration

The AK4309's CKS pin selects between 256fs and 384fs master clock modes. In the PS1, the MCLK is 384fs (16.9344 MHz), so **CKS is tied to select 384fs mode**. This is consistent with the AK4309 datasheet specifying 256fs or 384fs options.

### Is There Digital Processing Between SPU and DAC?

**No.** The I2S bus runs directly from the SPU output pins to the AK4309 input pins. The firebrandx digital audio mod taps these exact signals "before they reach the legs of the DAC" and feeds them to a Toshiba TC9231N I2S-to-S/PDIF transmitter. There is no intermediate DSP, FIFO, or sample-rate converter. The SPU's serial output IS the final digital audio.

### Sources:
- Firebrandx (measured clock frequencies): https://www.firebrandx.com/psxdigitalaudio.html
- psx-spx SPU docs (768 cycles per sample): https://psx-spx.consoledev.net/soundprocessingunitspu/
- AK4309 datasheet summary (256fs/384fs): https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html
- jsgroth blog (33.8688/768): https://jsgroth.dev/blog/posts/ps1-spu-part-1/

---

## 3. Post-DAC Analog Circuit

**Confidence: MEDIUM** (general topology confirmed; exact component values require service manual schematic verification)

### AK4309 Analog Output Characteristics

- Output type: Single-ended (not differential)
- Output level: **3.4 Vpp** (datasheet), ~2.8 Vpp measured (dogbreath.de)
- ~1.0 Vrms -- notably lower than standard CD player line level (~2.0 Vrms)
- Internal reconstruction: 8x oversampling FIR interpolator + 2nd-order switched-capacitor filter (SCF) + continuous-time filter (CTF)
- THD+N: -84 dB (datasheet spec)
- Dynamic range: 90 dB

### Post-DAC Signal Path (SCPH-1001 / PU-8)

```
AK4309 AOUTL/AOUTR (pins 16/15)
    |
    v
DC-blocking capacitors (electrolytic, original values TBD)
    |
    v
NJM2100E op-amp buffer (IC, 8-pin) --- ONLY on RCA output path
    |
    v
Muting transistors (software-controlled mute)
    |
    v
Series resistors (1k ohm + 100 ohm reported)
    |
    v
RCA jacks (SCPH-1001 only) / AV Multi-Out connector
```

### Key Differences by Output Connector

**RCA jacks (SCPH-1001 only):**
- Signal passes through NJM2100E op-amp buffers
- Additional DC-blocking capacitors
- Muting transistor circuit
- The NJM2100 is a low-cost, low-power dual op-amp -- considered the weak link by audiophiles

**AV Multi-Out (all models):**
- On SCPH-5501 and later, the AV Multi-Out path does NOT go through the NJM2100 op-amps
- Shorter signal path from DAC to connector
- This is why audiophiles prefer the SCPH-5501 via Multi-Out over the SCPH-1001 via RCA

### What This Means for Our Model Boundary

The DAC's internal reconstruction filtering (8x FIR + SCF + CTF) is INSIDE the AK4309 and is part of the DAC's character. Everything after the AK4309's analog output pins (coupling caps, op-amps, muting transistors, series resistors) is the "analog output stage" and is explicitly out of scope for our DSP model.

**Our model boundary is: the AK4309's digital input.** We model the DAC's conversion characteristics (oversampling, noise shaping, reconstruction filtering) in DSP. We do NOT model the post-DAC analog stage.

### Sources:
- dogbreath.de output stage: https://dogbreath.de/PS1/output/output.html
- dogbreath.de DAC pinout: https://dogbreath.de/PS1/DAC/DAC.html
- Archimago measurements: http://archimago.blogspot.com/2013/03/measurements-sony-playstation-1-scph.html
- Head-Fi SCPH-1001 mod thread: https://www.head-fi.org/threads/the-sony-ps1-scph-1001-extreme-mod-thread.340509/

---

## 4. CD Audio Path vs SPU Path

**Confidence: HIGH** (confirmed by psx-spx register documentation and multiple emulator implementations)

### Critical Finding: CD Audio Goes THROUGH the SPU

CD audio does NOT have its own separate path to the DAC. All audio -- game voices, CD-DA, XA-ADPCM -- is mixed digitally inside the SPU before being sent to the single AK4309 DAC.

### CD Audio Signal Path

```
CD-ROM Drive
    |
    v
CD-ROM Decoder (CXD1815Q or integrated)
  - Reads raw CD-DA PCM or XA-ADPCM sectors
  - XA-ADPCM is decompressed in hardware by the decoder
    |
    v (serial digital audio bus: DTIA, LRIA, BCIA pins on SPU)
SPU CD Audio Input
  - CD Audio Volume applied (register 1F801DB0h L / 1F801DB2h R)
  - Optionally routed to reverb (SPUCNT bit 2: "CD Audio Reverb")
  - Mixed with voice sum and external audio
    |
    v
SPU Main Mixer -> DAC
```

### Implications for Archimago/Stereophile Measurements

**This is important:** The Archimago and Stereophile measurements of the PS1 as a "CD player" measured audio that traveled:

```
CD-ROM decoder -> SPU (CD volume, mixing, NO reverb in CD playback mode) -> I2S -> AK4309 -> analog output
```

The CD audio path goes through the SPU's digital mixer and volume controls even for simple CD playback. The SPU's CD Audio Volume registers must be initialized, and SPUCNT bit 0 (CD Audio Enable) must be set.

**However:** During normal CD playback, the reverb is typically disabled (SPUCNT bit 2 = 0), no game voices are playing, and CD volume is set to maximum. So the CD audio path through the SPU is essentially a pass-through with volume scaling -- the signal is not being processed by the reverb engine.

**Conclusion:** The Archimago/Stereophile measurements ARE representative of what the AK4309 DAC sounds like, because the SPU is just passing CD data through with unity-ish gain. They are NOT representative of what game audio sounds like through the reverb engine, because that processing happens before the DAC. The DAC characteristics (THD, noise floor, frequency response) measured via CD playback apply equally to game audio, since all audio exits through the same DAC.

### Sources:
- psx-spx SPUCNT register: https://psx-spx.consoledev.net/soundprocessingunitspu/
- psx-spx CD-ROM drive: https://psx-spx.consoledev.net/cdromdrive/
- emu-russia SPU: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md

---

## 5. Multiple DAC Channels and Mixing Architecture

**Confidence: HIGH** (confirmed by hardware analysis -- single stereo DAC, all mixing digital)

### Single DAC, All Digital Mixing

The PS1 has exactly ONE stereo DAC (the AK4309, or its integrated equivalent in later revisions). ALL audio mixing happens digitally inside the SPU before the single I2S output stream reaches the DAC.

### SPU Mixing Pipeline (Reconstructed Order)

Per 768-CPU-cycle sample period (one 44,100 Hz sample):

```
1. VOICE PROCESSING (24 voices):
   For each voice:
     a. Read ADPCM sample from SPU RAM, decode to 16-bit PCM
     b. Apply pitch interpolation (Gaussian)
     c. Apply ADSR envelope
     d. Apply per-voice L/R volume
     e. Accumulate into voice_sum_L / voice_sum_R
     f. If voice reverb enabled (EON bit): accumulate into reverb_input_L / reverb_input_R

2. CD AUDIO INPUT:
   - Read CD audio samples from CD-ROM decoder (via DTIA/LRIA/BCIA serial input)
   - Apply CD Audio Volume (1F801DB0h/DB2h)
   - Add to voice_sum_L / voice_sum_R
   - If CD reverb enabled (SPUCNT bit 2): add to reverb_input_L / reverb_input_R
   - Write to capture buffer at 00000h-007FFh

3. EXTERNAL AUDIO INPUT:
   - Read from external audio pins (DTIB)
   - Apply External Audio Volume (1F801DB4h/DB6h)
   - Add to voice_sum_L / voice_sum_R
   - If external reverb enabled (SPUCNT bit 3): add to reverb_input_L / reverb_input_R

4. REVERB PROCESSING (at 22,050 Hz -- every other sample):
   - Input: reverb_input scaled by vLIN/vRIN (1F801DFCh/DFEh)
   - Process through IIR reflection + comb + all-pass filter network
   - Output: reverb_output_L / reverb_output_R

5. FINAL MIX:
   - output_L = voice_sum_L + (reverb_output_L * vLOUT)
   - output_R = voice_sum_R + (reverb_output_R * vROUT)
   - Apply Main Volume (1F801D80h/D82h)
   - Saturate to signed 16-bit range (-8000h..+7FFFh)

6. DAC OUTPUT:
   - Serialize as I2S
   - Send via DATO/LRCO/BCKO to AK4309
```

### Saturation Behavior

Per psx-spx: "The values written to memory are saturated to -8000h..+7FFFh." Multiplication results are "divided by +8000h, to fit them to 16bit range." This indicates 16-bit signed saturation (hard clipping) at mixing stages, which our M1 reverb core already models.

### Sources:
- psx-spx SPU mixer registers: https://psx-spx.consoledev.net/soundprocessingunitspu/
- hitmen SPU docs: https://hitmen.c02.at/files/docs/psx/spu.txt
- emu-russia psxrev: https://github.com/emu-russia/psxrev/blob/master/wiki_eng/spu.md

---

## 6. PS1 Model-Specific Audio Variations

**Confidence: MEDIUM-HIGH** (board revision mapping well-documented; subjective sonic differences are community anecdote)

### Audio Component Matrix by Board Revision

| Board | SCPH Models | SPU Chip | DAC | Audio Amp | RCA Jacks? | Notes |
|-------|-------------|----------|-----|-----------|------------|-------|
| PU-7 | 1000 (JP), early 3000 | CXD2922Q | AK4309VM (external) | NJM2100E | Yes (1001 only) | Earliest revision |
| PU-8 (early) | 1001 (US), 3000, 5000 | CXD2922Q/BQ | AK4309VM (external) | NJM2100E | Yes (1001 only) | "Audiophile" favorite |
| PU-8 (late) | 5001, 5501 | CXD2925Q | AK4309VM (external) | NJM2100E | No | SGRAM, improved GPU shading |
| PU-18 | 5501, 5502 | CXD2925Q | AK4309VM (external) | NJM2174 (w/ mute) | No | Smaller board |
| PU-20 | 7001, 7501 | CXD2925Q | AK4309VM (external) | NJM2174 | No | Similar to PU-18 |
| PU-22 | 7501, 9001 | CXD2925Q | **Integrated in SPU** | NJM2100E | No | DAC moves inside SPU |
| PU-23 | 9001 | CXD2925Q | **Integrated** | NJM2100E | No | Minor PU-22 variant |
| PM-41 | 101 (PSone) | CXD2938Q | **Integrated in combo** | NJM2174 | No | SPU+CDROM combo chip |
| PM-41(2) | 101 (PSone late) | CXD2941R | **Integrated** | NJM2174 | No | SPU+CDROM+SPU_RAM |

### Key Transition Points

**PU-7/PU-8 to PU-22: External to Integrated DAC**

The most significant audio-relevant change. On PU-7 through PU-20, the AK4309VM is a discrete 24-pin IC (IC402) with its own analog power supply pins and voltage references. Starting with PU-22, the DAC function moves inside the SPU die. This means:

- PU-22+ has no I2S bus to tap for digital audio mods
- The integrated DAC may have different analog characteristics (different voltage references, different output impedance)
- The firebrandx digital audio mod only works on pre-PU-22 boards

**NJM2100E vs NJM2174**

- NJM2100E: Simple dual op-amp, no mute function
- NJM2174: Dual audio amplifier with software-controlled mute, used on PU-18/PU-20 and PSone

### Audiophile Community Consensus

The PS1 audiophile community (Head-Fi, diyAudio, Stereophile) generally agrees:

1. **SCPH-1001 (PU-8)** is the "reference" for CD playback -- shortest signal path to RCA jacks, though the NJM2100 buffers are a weak point
2. **SCPH-5501 (PU-18) via AV Multi-Out** is arguably better because it bypasses the NJM2100 entirely
3. **PSone (SCPH-101)** is "underrated" per Head-Fi community -- integrated DAC but decent output
4. All models with external AK4309 sound essentially identical through the same output path
5. The DAC integration in PU-22+ is a **potential** sonic difference but has not been rigorously measured

### What This Means for Our Model

For DAC modeling purposes, we should target the AK4309VM's known characteristics (8x oversampling, delta-sigma, 90dB dynamic range, -84dB THD+N). The integrated DAC in PU-22+ is presumably similar but this is unverified. Since the "classic" PS1 sound that people associate with the console comes from the SCPH-1001/5501 era with the external AK4309, that is the correct target.

### Sources:
- psx-spx chipset summary: https://problemkaputt.de/psxspx-pinouts-chipset-summary.htm
- ConsoleMods PS1 model differences: https://consolemods.org/wiki/PS1:PS1_Model_Differences
- Head-Fi SCPH-1001 thread: https://www.head-fi.org/threads/the-sony-ps1-scph-1001-extreme-mod-thread.340509/
- Head-Fi PSone thread: https://www.head-fi.org/threads/the-sony-psone-scph-101-mod-thread-a-very-under-rated-cdp.340823/

---

## 7. The CDXA/ADPCM Path

**Confidence: HIGH** (register-level documentation confirms unified path)

### Complete ADPCM Voice + Reverb Signal Path

When a game plays ADPCM-decoded voice data mixed with reverb, the signal path is:

```
1. ADPCM sample data in SPU RAM (512 KB)
      |
      v
2. SPU Voice Engine (per voice, 24 channels):
   - ADPCM decode: 4-bit -> 16-bit PCM (with filter coefficients)
   - Pitch interpolation (Gaussian, 4-point)
   - ADSR envelope
   - Per-voice L/R volume
      |
      +---> Voice sum (dry path, all 24 voices accumulated)
      |
      +---> Reverb input (only voices with EON bit set)
                |
                v
3. Reverb Engine (operates at 22,050 Hz):
   - Input scaled by vLIN/vRIN
   - IIR reflections -> Comb filters -> All-pass filters
   - Output scaled by vLOUT/vROUT
      |
      v
4. Final Mixer:
   - Dry voice sum + Reverb output + CD audio + External audio
   - Main Volume applied
   - Hard clip to 16-bit signed
      |
      v
5. I2S serializer -> DATO/LRCO/BCKO pins
      |
      v
6. AK4309VM DAC -> Analog output
```

### XA-ADPCM (Streaming from CD) Path

XA-ADPCM is a different codec from SPU ADPCM. Key difference in routing:

```
CD-ROM Disc (XA-ADPCM sectors)
    |
    v
CD-ROM Decoder chip (CXD1815Q or integrated)
  - Decompresses XA-ADPCM in hardware
  - Outputs 16-bit PCM at 37,800 Hz or 18,900 Hz (XA modes)
    |
    v (serial bus: DTIA/LRIA/BCIA to SPU)
SPU CD Audio Input
  - Treated identically to CD-DA audio from this point
  - CD Audio Volume applied
  - Optionally sent to reverb (SPUCNT bit 2)
  - Mixed into final output
```

### Confirmation: Our Architecture Is Correct

Our model (reverb after voice processing, before DAC) is architecturally correct. The signal flow is:

```
ADPCM decode -> ADSR -> voice volume -> [reverb engine] -> mixer -> main volume -> DAC
```

The reverb engine receives its input from voice-processed samples (post-ADSR, post-voice-volume according to register documentation -- though capture buffers capture post-ADSR, pre-voice-volume, suggesting reverb send might also be pre-voice-volume). The reverb output is mixed back into the main bus before the final output to DAC.

**Note:** There is a subtle uncertainty about whether the reverb send tap is pre- or post-voice-volume. The psx-spx capture buffer documentation says voice 1/3 captures are "after ADSR envelope volume is applied but before voice L/R volumes are applied." If the reverb send follows the same tap point, reverb input would be mono (pre-L/R panning). This is consistent with psx-spx showing separate vLIN/vRIN (reverb input volume) applied to what appears to be a summed input. This detail matters for our reverb implementation but does not change the DAC model boundary.

### Sources:
- psx-spx SPU reverb: https://psx-spx.consoledev.net/soundprocessingunitspu/
- psx-spx CD-ROM: https://psx-spx.consoledev.net/cdromdrive/
- jsgroth SPU part 4 (capture buffers): https://jsgroth.dev/blog/posts/ps1-spu-part-4/

---

## 8. Clock Tree for Audio

**Confidence: HIGH** (math is exact; physical routing confirmed by firebrandx measurements)

### Complete Clock Derivation

```
Crystal Oscillator (X101)
  67.7376 MHz (NTSC) -- 4-pin package on PU-7 through PU-20
  [PAL uses 67.7376 MHz equivalent; video clock differs but audio doesn't]
      |
      v (divide by 2, internal to main IC or clock distribution)
CPU Clock: 33.8688 MHz
      |
      v (used as SPU system clock, XCK input)
SPU processes one sample every 768 CPU clocks:
  33,868,800 / 768 = 44,100.000 Hz (EXACT -- no drift, no approximation)
      |
      v
SPU generates output clocks:
  
  MCLK (to AK4309 pin 6): 16.9344 MHz = 384 x 44,100 = 384fs
    [33.8688 / 2 = 16.9344]
  
  BCKO (bit clock, to AK4309 pin 8): 2.1168 MHz = 48 x 44,100 = 48fs
    [16.9344 / 8 = 2.1168]
    [48 bits per stereo sample = 2 channels x 24 bit slots]
  
  LRCO (word clock, to AK4309 pin 10): 44,100 Hz = 1fs
```

### AK4309 Internal Processing (from MCLK)

The AK4309 in 384fs mode uses the 16.9344 MHz MCLK to drive its internal processing:

1. **8x FIR interpolation filter**: Upsamples from 44.1 kHz to 352.8 kHz (8fs)
2. **1-bit delta-sigma modulator**: Converts to high-frequency 1-bit stream
3. **2nd-order switched-capacitor filter (SCF)**: Analog reconstruction
4. **Continuous-time filter (CTF)**: Final smoothing

The oversampling ratio is **8x** (built into the DAC). The effective internal sample rate after FIR interpolation is 352,800 Hz.

### PU-22+ (Integrated DAC) Clock

On boards where the DAC is integrated into the SPU die, the I2S bus is internal. The DAC section presumably still receives the same clock relationships, but there is no external MCLK wire to measure. The integrated DAC likely uses the SPU's internal clock directly.

### PSone (PM-41) Clock

The PM-41 board uses a different oscillator arrangement:
- X201 (2-pin): 14.3181 MHz (NTSC) or 17.734 MHz (PAL)
- This is the video/system clock; the audio clock is still derived to produce 44,100 Hz
- The CXD2938Q/CXD2941R combo chip handles all clock division internally

### Sources:
- Firebrandx (measured: BC=2.1168MHz, MC=16.9344MHz, LRC=44.1kHz): https://www.firebrandx.com/psxdigitalaudio.html
- psx-spx (768 cycles, 33.8688 MHz): https://psx-spx.consoledev.net/soundprocessingunitspu/
- jsgroth (44100 = 33868800/768): https://jsgroth.dev/blog/posts/ps1-spu-part-1/
- AK4309 datasheet (384fs, 8x FIR): https://www.alldatasheet.com/datasheet-pdf/pdf/54935/AKM/AK4309.html
- psx-spx chipset (oscillator values): https://problemkaputt.de/psxspx-pinouts-chipset-summary.htm

---

## Summary: Model Scope Boundary

### What Our DAC Model Should Include

1. **8x oversampling FIR interpolation** -- the AK4309's digital filter that runs at 352.8 kHz
2. **Delta-sigma noise shaping** -- the 1-bit modulator's quantization noise profile
3. **Reconstruction filter response** -- the combined SCF+CTF frequency response (a gentle roll-off above 20 kHz, measured at +/-0.5 dB at 20 kHz per datasheet)
4. **Dynamic range limitation** -- 90 dB (vs theoretical 96 dB for 16-bit)
5. **THD+N floor** -- -84 dB

### What Our DAC Model Should NOT Include

1. DC-blocking coupling capacitors (analog domain)
2. NJM2100/NJM2174 op-amp coloration (analog domain, revision-dependent)
3. Muting transistor circuits (analog domain)
4. Output impedance interactions with cables/loads (analog domain)
5. Power supply noise coupling (analog domain, unit-specific)

### Architecture Confirmation

Our existing architecture is correct:

```
[ADPCM decode] -> [Voice processing] -> [Reverb engine] -> [Mixer] -> [DAC model] -> output
                                                                           ^
                                                                    WE ARE HERE
                                                                    (future milestone)
```

The DAC model sits at the very end of the digital chain, after all mixing and reverb processing. This is exactly where the real AK4309 sits in the PS1 hardware. The DAC receives a single stereo 16-bit 44.1kHz stream and converts it -- our model will simulate that conversion's coloration in DSP.

---

## Open Questions / Gaps

1. **Exact component values in post-DAC analog stage**: Service manual schematics exist (SCPH-5500/5501 at GameSX, PSone at Internet Archive) but are PDFs requiring manual inspection. Not critical for our DAC model but useful for documentation completeness.

2. **Integrated DAC characteristics on PU-22+**: Nobody has done rigorous A/B measurements of external AK4309 vs integrated DAC. The integrated version may have different noise floor, different output impedance, different reconstruction filter. LOW confidence that it sounds identical.

3. **AK4309 full datasheet**: The original AK4309AVM datasheet is reportedly unavailable (discontinued part). The AK4309B datasheet exists and is pin-compatible but may have different internal characteristics. We should treat AK4309 specs as approximate.

4. **Reverb send tap point**: Pre-voice-volume or post-voice-volume? Capture buffer evidence suggests pre-volume. This affects the reverb model (M1), not the DAC model, but is worth flagging.

5. **PSone clock tree**: The exact derivation of 44,100 Hz from the PM-41's 14.3181 MHz oscillator needs verification. The combo chip handles this internally so it may not be externally observable.
