# Feature Landscape: v1.8 PSX Voice Engine

**Domain:** Spec-faithful PS1 SPU voice playback engine added to SPU-94
**Researched:** 2026-05-16
**Primary source:** nocash psx-spx (problemkaputt.de / psx-spx.consoledev.net)
**Cross-checked:** DEEP-SPU-VOICE-PATH.md (HIGH confidence, already verified across 3 emulators)
**Overall confidence:** HIGH — most features are explicitly documented in spec with register addresses

---

## How the PS1 SPU Voice Engine Works

The SPU contains 24 independent voice channels. Each tick of the 44.1 kHz clock, every voice:

1. Reads compressed ADPCM data from SPU RAM (16-byte blocks, 28 samples each)
2. Decodes it through the same 4-bit Sony ADPCM codec already in `spu94_adpcm.c`
3. Applies 4-tap Gaussian interpolation using fractional bits of the single pitch counter
4. Runs through the ADSR envelope generator
5. Scales by per-voice L/R volume
6. Contributes to the main mixer sum
7. Optionally sends a copy to the reverb input bus

All 24 voices are processed sequentially within one 44.1 kHz tick. The reverb unit (already built) receives the sum of all reverb-enabled voices and processes it using the existing reverb network.

The pitch counter is a single 32-bit register that simultaneously drives sample advancement (bits 12+) and Gaussian interpolation (bits 4–11). This is the single-counter architecture already shipped in v1.7.

---

## Table Stakes

Features the PS1 hardware has that a faithful sampler must include. Missing any of these means
the engine is not spec-faithful.

| Feature | Spec Reference | Complexity | Dependency on Existing SPU-94 Code |
|---------|---------------|------------|-------------------------------------|
| **ADPCM sample playback** — read 16-byte blocks from a sample buffer, decode 28 samples per block, serve one per pitch-counter tick | `1F801C06h` start address, ADPCM block format byte 0 (shift/filter), bytes 2–15 (28 nibbles) | Low | Reuses `spu94_adpcm.c` decode path directly; needs a 512 KB sample RAM buffer added |
| **Single-counter voice path** — one 32-bit counter; bits 12+ = sample index within block; bits 4–11 = Gaussian interpolation index | nocash "SPU ADPCM Pitch" page; bits documented exactly | Low | ALREADY BUILT in v1.7 single-counter commit; `voice_counter += pitch` per tick |
| **4-tap Gaussian interpolation on playback** — coefficient table indexing by counter bits 4–11 | nocash Gaussian formula; `gauss[0xFF-i]*s0 + gauss[0x1FF-i]*s1 + gauss[0x100+i]*s2 + gauss[0x000+i]*s3` | Low | Gaussian table and formula already in `spu94_gauss.c`; ring buffer pattern already established |
| **Pitch register (VxPitch)** — 16-bit per-voice sample rate; 0x1000 = 44.1 kHz; range 0x0000–0xFFFF; values > 0x3FFF clamped to 0x3FFF | `1F801C04h + N*10h` | Low | Counter-advance loop already written; just needs pitch read from per-voice register |
| **Block boundary handling** — decode full 28-sample block on demand; carry 3 samples of history across block boundary for interpolation continuity | nocash ADPCM block format; DuckStation cross-verified | Medium | History carry-forward pattern already in existing voice path code |
| **Loop flag processing** — ADPCM block header byte 1, bit 0 = loop end, bit 1 = loop repeat, bit 2 = loop start | nocash ADPCM loop flags | Medium | New: requires reading flag byte from each block; no equivalent in current reverb path |
| **Loop start address latch** — when bit 2 set, copy current block address to VxRepeatAddress | `1F801C0Eh + N*10h` | Low | New register state; simple write on flag detect |
| **Loop end jump** — when bit 0 set, jump playback to VxRepeatAddress; set ENDX bit for this voice | `1F801D9Ch` ENDX | Low | New state machine branch in tick loop |
| **One-shot silence termination** — when bit 0 set but bit 1 clear, release envelope immediately and mute | nocash loop flag code 1 (bit0=1, bit1=0) | Low | Depends on ADSR release being built first |
| **KON (Key ON)** — write to 24-bit register triggers attack phase; initializes ADSR to zero; copies start address to current playback position | `1F801D88h` | Low | New: needs per-voice state reset logic |
| **KOFF (Key OFF)** — write triggers release phase regardless of current envelope stage; can abort attack, decay, sustain | `1F801D8Ch` | Low | New: needs per-voice envelope state transition |
| **ENDX status register** — read-only 24-bit register; bit set when voice hits loop-end flag; bit cleared by KON | `1F801D9Ch` | Low | New read-only status accumulator |
| **ADSR envelope generator** — four phases: Attack, Decay, Sustain, Release; each voice independent | `1F801C08h + N*10h` (lower), `1F801C0Ah + N*10h` (upper) | High | New DSP unit; most complex new feature |
| **ADSR current volume register** — 16-bit readable/writable per-voice; overwritten by ADSR generator each tick it applies a step | `1F801C0Ch + N*10h` | Low | Side effect of ADSR generator |
| **Per-voice L/R volume** — fixed mode (bit 15=0): direct volume -4000h to +3FFFh | `1F801C00h + N*10h` (L), `1F801C02h + N*10h` (R) | Low | New per-voice scalar multiply; same Q15 multiply already in reverb path |
| **Main output volume L/R** — master volume applied after all voices summed | `1F801D80h` (L), `1F801D82h` (R) | Low | New master gain stage after mixer sum |
| **Voice mixer sum** — all enabled voices accumulated to stereo output bus before reverb | implicit in SPU architecture | Low | New accumulator; straightforward int32 sum then clip |
| **Reverb send per voice (EON)** — 24-bit register; when bit set, voice output also fed to reverb input bus alongside normal output | `1F801D98h` | Low | Existing reverb engine receives this bus; just needs the routing conditional |
| **SPU RAM budget — 512 KB shared** — sample data occupies 0x01000 onward; reverb work area at top; collision is a configuration error not a hardware error | nocash RAM layout | Medium | New: need a static allocator or size guard for sample loading; reverb already uses mBASE |
| **Start address register** — 16-bit per-voice, in units of 8 bytes, points to first ADPCM block | `1F801C06h + N*10h` | Low | New per-voice state; straightforward register |
| **Repeat address register** — 16-bit per-voice, in units of 8 bytes; updated by loop-start flag; jump target for loop-end | `1F801C0Eh + N*10h` | Low | New per-voice state |

---

## ADSR Envelope: Sub-Feature Detail

The ADSR is the most complex new DSP unit. It warrants its own breakdown.

**Algorithm per tick (rate formula):**

```
AdsrCycles = 1 << max(0, ShiftValue - 11)
AdsrStep   = StepValue << max(0, 11 - ShiftValue)

IF exponential AND increase AND AdsrLevel > 0x6000:
    AdsrCycles = AdsrCycles * 4       // slow down near peak

IF exponential AND decrease:
    AdsrStep = AdsrStep * AdsrLevel / 0x8000   // proportional decay

Every AdsrCycles ticks:
    AdsrLevel += AdsrStep
```

Note: "exponential increase" is implemented as slower linear above 0x6000 — it is a fake
exponential. "Exponential decrease" is genuinely proportional (multiplied by current level).
This matches real hardware behavior exactly.

| Sub-feature | Register bits | Complexity | Notes |
|-------------|--------------|------------|-------|
| **Attack phase** — linear or exponential; runs from 0 to 0x7FFF; step +4 to +7 | `1F801C08h` bits 15 (mode), 14–10 (shift 0–31), 9–8 (step 0–3) | Medium | Exponential fake above 0x6000 |
| **Decay phase** — always exponential decrease; step always -8; shift 0–15 | `1F801C08h` bits 7–4 (shift), implicit step -8 | Low | Fixed exponential; simpler than attack |
| **Sustain level threshold** — formula: `(N+1) * 0x800h`; decay stops here | `1F801C08h` bits 3–0 (0–15) | Low | Determines where decay transitions to sustain |
| **Sustain phase** — linear or exponential; increase or decrease independently; sustain can ramp up or down | `1F801C0Ah` bits 31 (mode), 30 (direction), 28–24 (shift), 23–22 (step) | Medium | Four combinations: lin-up, lin-down, exp-up, exp-down |
| **Release phase** — triggered by KOFF; runs to 0x0000; linear or exponential | `1F801C0Ah` bits 20–16 (shift); mode bit TBD per spec page | Medium | Note: nocash says release mode is selectable (lin/exp); decay is ALWAYS exp |
| **ADSR timing precision** — AdsrCycles can be 1..2^20 ticks; step can be 1..2^11 per cycle | derived from shift/step fields | Low | Pure integer arithmetic; no floating point |

The shift field of 0–31 combined with the step field of 0–3 gives 128 unique rates for attack/sustain/release (4 steps × 32 shifts), and 64 for decay (1 step × 16 shifts). This covers the full range from nearly instantaneous to ~several minutes.

---

## Differentiators

Features the real SPU has that go beyond simple looping playback. Include these for a complete
instrument; they are spec-faithful, not embellishments.

| Feature | Value | Complexity | Notes |
|---------|-------|------------|-------|
| **Pitch modulation (PMON)** — voice N uses previous voice N-1's post-ADSR amplitude as a pitch factor; range 0.00–1.99x | `1F801D90h` bits 1–23 per voice (bit 0 = unused; voice 0 cannot be modulated) | Medium | Musical use: FM-style synthesis. Dependency: ADSR output from voice N-1 must be computed before voice N in the tick loop. Already ordered correctly if voices processed sequentially 0→23. |
| **Noise generator voice** — replaces ADPCM sample output with LFSR noise; pitch control replaced by global SPUCNT noise frequency | `1F801D94h` NON per voice; SPUCNT `1F801DAAh` bits 13–8 (shift), 9–8 (step) | Medium | LFSR: `ParityBit = Level[15] xor [12] xor [11] xor [10] xor 1`; all noise voices share one frequency. Musical use: percussion, texture. |
| **Per-voice volume sweep mode** — bit 15 of VxVolL or VxVolR enables a secondary envelope on volume (independent of ADSR); linear or exponential, increasing or decreasing | `1F801C00h + N*10h` bit 15; shift/step/direction packed in remaining bits | High | Adds an extra temporal dimension to voice volume. Rarely used in practice. Complexity is in detecting and switching between fixed and sweep modes. |
| **CD audio input mixing** — CD-DA or XA-ADPCM from the CD-ROM drive summed to SPU output; volume via `1F801DB0h` L and `1F801DB2h` R | `1F801DB0h`, SPUCNT bit 0 (Ce) | Low | Not applicable to a standalone sampler instrument — no CD drive. Stub out or omit. |
| **External audio input mixing** — expansion port audio summed to output; volume via `1F801DB4h` L, `1F801DB6h` R | `1F801DB4h`, SPUCNT bit 2 (Ee) | Low | Not applicable to standalone instrument. Omit. |
| **Voice capture buffers** — voices 1 and 3 post-ADSR output written to fixed SPU RAM addresses (0x0800, 0x0C00); CPU-readable; also used as IRQ source | `1F801E00h` + voice * 4 read-back; SPU RAM 0x0800/0x0C00 | Medium | Used in PS1 games for echo/chorus tricks by routing voice 1 or 3 output back as a sample. Interesting for creative use but not needed for basic sampler. |
| **SPUIRQ (SPU interrupt)** — fires when playback address crosses a configured address in SPU RAM | `1F801DA4h` IRQ address; SPUCNT bit 6 | High | Used in PS1 games for sample-accurate event synchronization. Complex host integration. Not needed for instrument use. Omit. |
| **DMA sample upload** — PS1 CPU fills SPU RAM via DMA; modes: non-DMA (PIO), DMA write, DMA read | SPUCNT bits 5–4 | N/A | In our implementation, the host fills the sample buffer directly. No DMA emulation needed. |

---

## Anti-Features

Do not build these. Each has a clear reason.

| Anti-Feature | Why Avoid | What to Do Instead |
|--------------|-----------|-------------------|
| **CD audio input pipeline** | No CD drive in standalone/plugin context; would be permanently muted or require fake input | Omit entirely; note in DECISIONS.md |
| **External audio input pipeline** | Expansion port does not exist in plugin context | Omit entirely |
| **SPUIRQ emulation** | Sample-accurate IRQ timing requires tight host integration; adds significant complexity for zero musical benefit in instrument use | Omit; note in DECISIONS.md if a game emulation user ever asks |
| **DMA transfer emulation** | Irrelevant; host writes samples directly to our buffer struct | Replace with a direct `spu94_voice_load_sample()` C API call |
| **Reverb feedback from voice capture** — routing voice 1/3 capture back as a sample to create fake echo | A PS1 game trick; requires the capture buffer wiring | Defer to a "creative effects" milestone if Anthony wants it; don't build now |
| **SPU2 (PS2) extensions** — 48 voices, extended RAM, ADSR extensions | Different chip; incompatible with PS1 fidelity | Explicitly out of scope; PS1 only |
| **Float-point ADSR arithmetic** | PS1 ADSR runs in fixed-point (integer shift/step); floating point changes the character of envelope curves subtly | Use same Q15 / integer arithmetic discipline as reverb path |
| **Smoothing on KOFF** | Real hardware transitions immediately to release; smooth KOFF is a modern convenience | Follow spec: KOFF initiates release phase directly |

---

## Feature Dependencies

Reading bottom-to-top: each row depends on what's above it.

```
SPU RAM buffer (512 KB flat array)
  └── ADPCM sample loading (WAV → ADPCM encode → write to buffer)
        └── Start address register (per-voice pointer into buffer)
              └── ADPCM block decode (batch 28 samples on demand)
                    └── Block flag processing (loop start/end/repeat)
                          └── Loop address register (latch/jump)
                                └── Single-counter voice path (already built)
                                      └── 4-tap Gaussian interpolation (already built)
                                            └── ADSR envelope generator (new)
                                                  └── Per-voice L/R volume multiply
                                                        └── Voice mixer sum (24 voices → stereo bus)
                                                              ├── Main output L/R volume
                                                              ├── Reverb send (EON → existing reverb engine)
                                                              └── Output to DAC / plugin output

Pitch modulation (PMON):
  └── Requires ADSR output from voice N-1 before computing voice N
        └── Dependency: voices must be processed in index order 0..23

Noise generator (NON):
  └── Replaces ADPCM decode path for that voice
  └── Requires SPUCNT noise frequency register
  └── Does NOT depend on ADPCM loader or loop logic

KON / KOFF:
  └── KON: resets counter, clears ENDX, copies start address, begins ADSR attack
  └── KOFF: triggers ADSR release regardless of current phase
  └── Depends on: ADSR generator, start address register
```

---

## MVP Recommendation

A single monophonic voice in the standalone testbed first, then expand to 24. This matches
the milestone plan in PROJECT.md.

**Phase 1 — Single voice, no envelope:**
1. SPU RAM flat buffer (512 KB static array in C core)
2. ADPCM sample loading API (`spu94_voice_load_sample()`)
3. Start address register, current address tracking
4. Single-counter advance per tick (already built; wire to new voice struct)
5. ADPCM block decode on demand (28-sample batch, 3-sample history carry)
6. Loop flag processing (bits 0, 1, 2 of block header byte 1)
7. Repeat address latch and loop-end jump
8. ENDX status bit
9. KON / KOFF (pitch counter reset, address reset, ENDX clear)
10. Per-voice L/R volume (fixed mode)
11. Reverb send conditional (EON bit per voice → existing reverb input)
12. Main output L/R volume

**Phase 2 — ADSR:**
13. ADSR envelope generator (attack/decay/sustain/release)
14. ADSR current volume register (readable)
15. One-shot mode (loop flag bit0=1, bit1=0 → release after loop-end)

**Phase 3 — Polyphony:**
16. Expand to 24 voices (array of 24 voice structs, sequential tick processing)
17. Voice mixer sum (int32 accumulator, 24 voices → stereo output)

**Phase 4 — Optional spec features:**
18. Pitch modulation (PMON) — voice N uses voice N-1 amplitude as pitch factor
19. Noise generator (NON) — per-voice LFSR replaces ADPCM path
20. Per-voice volume sweep mode (bit 15 = sweep, rarely used)

**Defer indefinitely:**
- CD audio input
- External audio input
- SPUIRQ
- DMA transfer emulation
- Voice capture buffer wiring (beyond basic readback)

---

## Complexity Summary

| Feature Group | Estimated Complexity | Primary Risk |
|--------------|---------------------|--------------|
| SPU RAM buffer + sample loader | Low | Memory layout collision with reverb work area |
| Single-counter + Gaussian (existing) | Already built | None |
| ADPCM decode on demand | Low | History carry-forward correctness at block boundary |
| Loop flag processing | Low-Medium | Flag byte decoding; one-shot vs loop state machine |
| KON / KOFF / ENDX | Low | State machine correctness; timing of address copy |
| Per-voice volume + mixer sum | Low | Integer overflow in 24-voice accumulator (use int32) |
| Reverb send routing | Low | EON gate + existing reverb input already wired |
| ADSR envelope generator | High | Rate table faithfulness; exponential fake above 0x6000; phase transition logic |
| Pitch modulation (PMON) | Medium | Ordering dependency; voice N-1 output must precede voice N |
| Noise generator (NON) | Medium | LFSR algorithm; shared frequency constraint |
| Volume sweep mode | High | Second envelope state machine per voice; rarely exercised in practice |

---

## Sources

- [psx-spx: Sound Processing Unit](https://psx-spx.consoledev.net/soundprocessingunitspu/) — voice architecture, register map, ENDX, KON/KOFF, EON, NON, mixer, SPU RAM layout. HIGH confidence.
- [nocash: SPU Volume and ADSR Generator](https://problemkaputt.de/psxspx-spu-volume-and-adsr-generator.htm) — ADSR rate formula, shift/step encoding, exponential fake above 0x6000, volume sweep mode. HIGH confidence.
- [nocash: SPU ADPCM Pitch](https://problemkaputt.de/psxspx-spu-adpcm-pitch.htm) — pitch counter bit fields, single-counter architecture, pitch modulation formula, max frequency clamp. HIGH confidence.
- [DEEP-SPU-VOICE-PATH.md](.planning/research/DEEP-SPU-VOICE-PATH.md) — internal research from 2026-05-15, cross-verified across nocash + DuckStation + Mednafen + PlayStation1Vsts. HIGH confidence. Contains the exact counter bit layout, block decode timing, Gaussian table indexing, history carry-forward pattern, and implementation comparison.
- [hitmen SPU docs](https://hitmen.c02.at/files/docs/psx/spu.txt) — secondary reference; consistent with nocash on RAM layout, volume modes, CD/external input. MEDIUM confidence (author-acknowledged incomplete).
