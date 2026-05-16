# Technology Stack: v1.8 PSX Voice Engine

**Project:** SPU-94
**Milestone:** v1.8 — PSX Voice Engine (24-voice ADPCM sampler)
**Researched:** 2026-05-16
**Overall confidence:** HIGH (existing codebase is primary source; nocash spec + DEEP-SPU-VOICE-PATH.md cross-verified)

---

## Executive Summary

v1.8 adds a full 24-voice PS1 SPU playback engine. The required stack additions are entirely in plain C99, zero new external dependencies, and slot cleanly into the existing libspu94 architecture.

The existing ADPCM codec, Gaussian interpolation table, and single-counter voice path (already live in `spu94_process.c`) are directly reusable — they implement the correct per-voice hardware behavior for one voice. The v1.8 work is: (1) promote the single-voice state out of `spu94_state` into a reusable `spu94_voice_t` struct, (2) add a 512 KB SPU RAM array as a new caller-provided or internally-owned flat byte block, (3) implement the ADSR envelope state machine as a new module, and (4) build a 24-voice mixer that routes dry sums and per-voice reverb sends.

No MIDI library is needed for the standalone testbed — a minimal note-trigger surface (key-on, key-off, note number → pitch register) is one small C translation unit. No heap. No locks.

---

## New C Modules Required

### 1. `spu94_voice.h` / `spu94_voice.c` — Voice State and Tick

**What:** Struct and per-tick function for a single voice. This is a generalization of the existing one-voice path already embedded in `spu94_process.c`.

**Existing code that moves here:**

From `spu94_state_internal.h` (current single-voice fields):
```
voice_pitch, voice_counter, decim_prev_l/r, adpcm_buf_pos,
adpcm_in_buf_l/r[28], adpcm_out_buf_l/r[28],
adpcm_state_l/r, gauss_ring_l/r[4], gauss_ring_pos, gauss_out_pos,
adpcm_enabled, gauss_enabled, aa_filter_enabled
```

These collapse into a `spu94_voice_t` struct (one per voice) with:
- All of the above fields
- ADSR state (new — see module 2)
- Loop address, loop start address, current SPU RAM read address (pointers into SPU RAM)
- `reverb_on` flag (1 bit: voice contributes to reverb send bus)
- Key-on/key-off flags
- ADSR volume (current envelope level, int16 or uint16)
- Voice volumes: VxVoll, VxVolr (per-voice stereo pan, Q15)

**`spu94_voice_tick(spu94_voice_t *v, const uint8_t *spu_ram, int16_t *out_l, int16_t *out_r)`:**

Per-tick function. Order matches PS1 hardware (from DEEP-SPU-VOICE-PATH.md, Section 5.1):
1. Decode ADPCM block from SPU RAM if needed (`has_samples == false`)
2. Gaussian interpolation on decoded ring buffer → raw sample
3. Apply ADSR envelope level → volume-scaled sample
4. Advance pitch counter (`counter += pitch`)
5. Check if `counter >> 12 >= 28` → flag next block decode, handle loop
6. Apply VxVoll/VxVolr → write to `*out_l`, `*out_r`

The ADPCM decode reads directly from `spu_ram[current_address]`. Loop is handled by checking the VAG flag byte returned from `spu94_adpcm_decode_block`: flag `0x01` (loop end) triggers address wrap to `loop_start_address`.

**Reuse:** `spu94_adpcm_decode_block` and `spu94_gauss_table` are called directly — no adaptation needed. The single-counter architecture already implemented in `spu94_process.c` (lines 77–117) is the template; it moves into `spu94_voice_tick` verbatim with the SPU RAM read replacing the encode path.

Confidence: HIGH. The single-counter architecture is already validated in production (`v1.7`, commit `57eca98`). Moving it to a struct is a refactor, not new ground.

---

### 2. `spu94_adsr.h` / `spu94_adsr.c` — Exponential ADSR Envelope

**What:** PS1-faithful ADSR envelope state machine. New module, no existing code to reuse.

**Spec source:** nocash psx-spx, section "SPU ADSR Envelope Generator" (HIGH confidence).

**State struct `spu94_adsr_t`:**
```c
typedef struct {
    uint16_t adsr1;        /* VxADSR1 register value */
    uint16_t adsr2;        /* VxADSR2 register value */
    int16_t  level;        /* current envelope level 0..0x7FFF */
    uint16_t counter;      /* 16-bit accumulation counter */
    uint8_t  phase;        /* 0=attack, 1=decay, 2=sustain, 3=release, 4=off */
} spu94_adsr_t;
```

**`spu94_adsr_tick(spu94_adsr_t *a)` — called once per 44.1 kHz tick:**

Nocash pseudo-code (verified from psx-spx.consoledev.net, HIGH confidence):
```
Extract from adsr1/adsr2:
  phase_specific (shift, step, mode, direction)

AdsrStep = 7 - StepValue
IF Decreasing XOR PhaseNegative:
    AdsrStep = ~AdsrStep
AdsrStep = AdsrStep << max(0, 11 - ShiftValue)
CounterIncrement = 0x8000 >> max(0, ShiftValue - 11)

IF exponential AND increase AND level > 0x6000:
    AdsrStep >>= 2   (fake exponential: slower linear at high levels)
ELSE IF exponential AND decrease:
    AdsrStep = AdsrStep * level / 0x8000

counter += CounterIncrement
IF (counter & 0x8000) == 0: return   (not time to step yet)
counter = 0

level += AdsrStep
IF increasing: clamp to [0, 0x7FFF]
IF decreasing: clamp to [0, 0x7FFF]

Phase transitions:
  attack  -> decay   when level >= 0x7FFF
  decay   -> sustain when level <= ((adsr1 & 0xF) + 1) * 0x0800
  release -> off     when level <= 0
```

**Key ADSR facts from spec:**
- Decay is always exponential, always decreasing, fixed step -8
- Release is always fixed step -8, mode selectable (linear or exponential)
- Sustain level = `(adsr1[3:0] + 1) * 0x0800` (16 levels: 0x0800..0x8000)
- "Exponential increase" is fake: the hardware simply halves the step when level > 0x6000 (nocash explicitly says "not true exponential")
- "Exponential decrease" IS genuine: step is proportional to current level
- Attack always increases from 0. Key-on resets level to 0 and phase to attack.
- ADSR volume is 15-bit unsigned (0..0x7FFF) for reading; writing allows signed range

Confidence: HIGH (from psx-spx.consoledev.net and cross-verified against DuckStation's ADSR implementation logic).

**RT-safety:** No heap, no branches dependent on external state beyond the struct. Total cost: ~10 integer operations per tick per voice.

---

### 3. `spu94_voice_mixer.h` / `spu94_voice_mixer.c` — 24-Voice Mixer

**What:** Tick-level mixer that sums 24 voices into two output buses: dry sum (all voices) and reverb send (voices with `reverb_on == 1`).

**Struct `spu94_voice_mixer_t`:**
```c
typedef struct {
    spu94_voice_t  voices[24];
    int16_t        mvoll;          /* MVOLL master volume L, Q15 */
    int16_t        mvolr;          /* MVOLR master volume R, Q15 */
    int16_t        rvoll;          /* RVOLL reverb volume out L, Q15 */
    int16_t        rvolr;          /* RVOLR reverb volume out R, Q15 */
    uint32_t       endx;           /* ENDX: bit i set when voice i hit loop end */
} spu94_voice_mixer_t;
```

**`spu94_voice_mixer_tick(spu94_voice_mixer_t *m, const uint8_t *spu_ram, int16_t *dry_l, int16_t *dry_r, int16_t *rev_l, int16_t *rev_r)`:**

1. For each of 24 voices: call `spu94_voice_tick(...)` → get voice's L/R contribution
2. Accumulate into int32 dry sum (all voices) and int32 reverb send (reverb_on voices)
3. Apply MVOLL/MVOLR to dry sum → sat_s16 → `*dry_l`, `*dry_r`
4. Apply RVOLL/RVOLR to reverb sum → sat_s16 → `*rev_l`, `*rev_r`

The `*rev_l`, `*rev_r` outputs feed directly into the existing `spu94_process` reverb send path (replacing the current `patina_send` mixing), or wire in parallel alongside the existing reverb FIR chain.

**RT-safety:** 24 sequential voice ticks + 2 accumulator passes. At 44.1 kHz, each tick processes ~226 microseconds worth of audio. 24 voices × 10-15 operations each = well within budget on any modern processor and any MCU fast enough to run the reverb.

---

### 4. `spu94_spu_ram.h` — SPU RAM Contract

**What:** A header-only type definition and sizing macro. NOT a module — SPU RAM is a flat 512 KB byte array. The caller owns it.

```c
#define SPU94_SPU_RAM_BYTES  0x80000u   /* 512 KiB, exactly SPU94_WORK_BUF_MAX_BYTES */

/* SPU RAM is a flat uint8_t array. Caller allocates and provides at init.
 * The reverb work buffer and voice ADPCM sample data share this space
 * exactly as they did on real PS1 hardware: reverb work buffer occupies
 * the top of SPU RAM (high addresses), sample data occupies the bottom. */
```

Note: `SPU94_WORK_BUF_MAX_BYTES` is already defined as `0x80000u` in `spu94.h`. The reverb engine's work buffer IS the top portion of SPU RAM. In the full voice engine, the existing reverb work buffer pointer can optionally point into the top of the SPU RAM array, making the memory layout authentic.

For the standalone testbed (single-voice → 24-voice buildout), the simplest approach is a flat `static uint8_t spu_ram[SPU94_SPU_RAM_BYTES]` owned by the voice mixer or the standalone app. No heap.

---

### 5. `spu94_voice_note.h` / `spu94_voice_note.c` — Note Trigger (Standalone MIDI/Trigger)

**What:** A minimal note→pitch mapping and key-on/key-off interface. Replaces a MIDI library entirely. This is the standalone's interface for driving voices from keyboard input or JUCE callbacks.

```c
/* Convert MIDI note number (0..127) to SPU VxPitch value.
 * Root note A4 = MIDI 69 = pitch 0x1000 (44.1 kHz).
 * Each semitone is * 2^(1/12) ≈ multiply by 1.0595.
 * Returns a uint16_t in range [0x0001..0x3FFF]. */
uint16_t spu94_note_to_pitch(uint8_t midi_note);

/* Trigger key-on for voice N: reset ADSR, set address to sample start,
 * arm the voice for playback. */
void spu94_voice_key_on(spu94_voice_t *v, uint16_t pitch, uint32_t start_address);

/* Trigger key-off for voice N: transition ADSR to release phase. */
void spu94_voice_key_off(spu94_voice_t *v);
```

The `spu94_note_to_pitch` lookup table: 128 entries, precomputed as `(uint16_t)(0x1000 * pow(2.0, (note - 69) / 12.0))`, stored in `.rodata`. No floating point at runtime.

---

### 6. `spu94_sample_loader.h` / `spu94_sample_loader.c` — WAV-to-SPU-RAM Encoder

**What:** Takes a WAV file (already loaded as int16 PCM via WavLoader), ADPCM-encodes it block by block using the existing `spu94_adpcm_encode_block`, and writes the encoded ADPCM blocks into a caller-specified offset in SPU RAM. Sets loop flags per VAG convention.

```c
/* Encode a PCM buffer into SPU RAM at the given byte offset.
 * Mono only (voice playback is inherently mono on PS1 hardware — stereo
 * is achieved by two voices panned L/R, or by the reverb send).
 * Returns the number of bytes written, or -1 on overflow.
 * Sets the final block's flag byte to 0x01 (loop end) if loop_enable,
 * else 0x03 (end+mute). */
int32_t spu94_sample_encode_to_ram(
    const int16_t *pcm, uint32_t num_samples,
    uint8_t *spu_ram, uint32_t ram_offset, uint32_t ram_size,
    uint32_t *loop_start_out,
    int loop_enable
);
```

**Reuse:** Calls `spu94_adpcm_encode_block` directly — zero adaptation. This module is ~60 LOC.

---

## Memory Model for 512 KB SPU RAM Simulation

The PS1 SPU RAM is 512 KB (524,288 bytes). It has two users:

| Region | Contents | Size |
|--------|----------|------|
| Low addresses (0x0000–up) | Voice ADPCM sample data | Variable |
| High addresses (top–down) | Reverb work buffer | Preset-dependent |

The existing `SPU94_WORK_BUF_MAX_BYTES = 0x80000` macro already defines 512 KB as the maximum reverb buffer. This is the full SPU RAM size — the reverb work buffer in the existing code is sized as if it has the entire SPU RAM available, which is correct.

For v1.8, the simplest faithful model:

```c
uint8_t spu_ram[SPU94_SPU_RAM_BYTES];   /* 512 KB flat */

/* Reverb engine: point its work_buf at the TOP of spu_ram.
 * The mBASE register determines how far down into SPU RAM
 * the reverb buffer actually uses. For the default Hall preset,
 * mBASE = 0x7FFE (nearly the whole 512 KB). */
uint8_t *reverb_work = spu_ram + (SPU94_SPU_RAM_BYTES - reverb_buf_size);

/* Sample data: loaded at the BOTTOM, working upward. */
uint32_t next_free = 0x0000;
```

For the standalone testbed (single voice, then 24 voices), a simpler approach is fine: keep the existing reverb work buffer as a separate `static` array (already working), and add a separate 512 KB `static uint8_t spu_ram[]` for sample data only. Unifying them into one array is an architectural refinement that can happen in a later phase, after the voice engine is working.

**Stack size:** 512 KB as a static or stack-allocated array is fine in C99 on desktop (stack limit is typically 8 MB on Linux). For the JUCE standalone, it should be file-scope static. For MCU porting, this is the primary RAM budget concern and is noted in PITFALLS.

---

## Reuse Assessment: What Does NOT Change

| Existing Code | Reuse Status | Notes |
|---------------|--------------|-------|
| `spu94_adpcm_decode_block` | Direct reuse | Call from `spu94_voice_tick` with `spu_ram` pointer |
| `spu94_adpcm_encode_block` | Direct reuse | Call from `spu94_sample_loader` |
| `spu94_gauss_table[512]` | Direct reuse | Same table, same indexing |
| Single-counter voice path (in `spu94_process.c`) | Refactor into `spu94_voice_tick` | Move existing code; no logic change |
| `spu94_fir_chain_step` / reverb engine | Unchanged | Voice mixer outputs feed into existing reverb path |
| `spu94_process` | Modified at step 2 | Instead of the single-voice ADPCM path, call `spu94_voice_mixer_tick`, then pipe rev_l/rev_r into the existing reverb send |
| `spu94_state_internal.h` single-voice fields | Removed or retained as voice 0 | The current single-voice fields in `spu94_state` become the `spu94_voice_t` struct; voice 0 in the mixer IS the current voice |
| `spu94.h` API | Additive only | New `spu94_voice_*` and `spu94_adsr_*` public surface; existing API untouched |
| `spu94_vag.h` / `vag.c` | Direct reuse | VAG flag bytes already defined; `spu94_vag_read_header` used by sample loader |
| `WavLoader.cpp` | Unchanged | Already loads WAV to int16; feeds `spu94_sample_encode_to_ram` |
| `spu94_q15.h` helpers | Direct reuse | `q15_mul_truncate`, `sat_s16` used in ADSR multiply and voice volume |

---

## Adaptation: Single-Voice Path to Multi-Voice

The current `spu94_process.c` embeds a single-voice ADPCM path inline (lines 58–151). For v1.8, this block:

1. **Moves** to `spu94_voice_tick` in the new voice module — same logic, same arithmetic, same Gaussian table access
2. **Changes its input source:** instead of reading live audio from `L_in/R_in`, it reads decoded ADPCM from SPU RAM
3. **Keeps** the same single-counter architecture, same Gaussian ring buffer, same block-decode timing
4. **The `adpcm_buf_pos` / encode path** is replaced by a direct `spu94_adpcm_decode_block` from SPU RAM — the encode half is no longer needed in the voice tick (the audio is already stored as ADPCM in SPU RAM)

The current path (encode live audio → decode → Gaussian) was the "coloration" path for the reverb effect use case. The new path (decode from SPU RAM → Gaussian) is the pure voice playback path. Both use identical decode/Gauss code.

---

## MIDI / Trigger Input for Standalone

No MIDI library is needed. The JUCE standalone already receives MIDI in its `processBlock` callback via `juce::MidiBuffer`. The existing JUCE layer can call `spu94_voice_key_on` / `spu94_voice_key_off` directly:

```cpp
// In PluginProcessor::processBlock (or StandaloneApp):
for (const auto meta : midiMessages) {
    const auto msg = meta.getMessage();
    if (msg.isNoteOn()) {
        const uint8_t note = msg.getNoteNumber();
        const uint16_t pitch = spu94_note_to_pitch(note);
        int voice = find_free_voice();  // simple round-robin or oldest-steal
        spu94_voice_key_on(&mixer.voices[voice], pitch, sample_start_addr);
    } else if (msg.isNoteOff()) {
        spu94_voice_key_off(&mixer.voices[voice_for_note]);
    }
}
```

JUCE already provides MIDI input routing in the standalone wrapper. For the development testbed phase (single voice), a simple button in the JUCE GUI is sufficient — no MIDI routing required yet.

No new dependency. MIDI is a first-class JUCE feature already in the project.

---

## What NOT to Add

| Candidate | Decision | Reason |
|-----------|----------|--------|
| MIDI library (RtMidi, etc.) | Do not add | JUCE handles MIDI natively; RtMidi adds a dependency for zero gain |
| libsamplerate for voice pitch | Do not add | Pitch is already handled by the PS1-faithful single-counter Gaussian interpolation — this IS the pitch engine |
| C++ STL containers for voice allocation | Do not add | Plain array `spu94_voice_t[24]` is RT-safe; `std::vector` violates the no-heap constraint |
| Separate voice RAM allocator | Do not add | Fixed layout: sample data loaded at known offsets, no runtime allocation |
| Dynamic voice count | Do not add | PS1 hardware is exactly 24 voices; parameterizing this adds complexity for zero benefit |
| Noise generator (SPU voice noise mode) | Defer | Real SPU supports a noise mode per voice; not needed for sampler use case; add in future phase if needed |
| Pitch modulation (PMON) | Defer | Real SPU supports voice N modulating voice N+1's pitch; not needed for initial sampler; note it in PITFALLS |

---

## New Files Summary

| File | LOC (estimated) | Purpose |
|------|-----------------|---------|
| `include/spu94/spu94_voice.h` | ~80 | Public API: `spu94_voice_t`, key-on/off, tick |
| `src/spu94/spu94_voice.c` | ~180 | `spu94_voice_tick`, key-on/off, init |
| `include/spu94/spu94_adsr.h` | ~50 | Public API: `spu94_adsr_t`, tick, init |
| `src/spu94/spu94_adsr.c` | ~120 | ADSR state machine from spec |
| `include/spu94/spu94_voice_mixer.h` | ~60 | Public API: `spu94_voice_mixer_t`, tick |
| `src/spu94/spu94_voice_mixer.c` | ~100 | 24-voice summing, MVOLL/MVOLR, ENDX |
| `include/spu94/spu94_sample_loader.h` | ~30 | Public API: encode PCM to SPU RAM |
| `src/spu94/spu94_sample_loader.c` | ~80 | WAV-to-ADPCM-in-RAM encoder |
| `include/spu94/spu94_voice_note.h` | ~30 | `spu94_note_to_pitch`, key-on/off wrappers |
| `src/spu94/spu94_voice_note.c` | ~50 | 128-entry pitch table, trigger helpers |
| `include/spu94/spu94_spu_ram.h` | ~10 | `SPU94_SPU_RAM_BYTES` macro, layout comment |

Total new LOC: ~790 C (core) + headers. No new external files needed.

---

## CMakeLists.txt Changes

Add the new `.c` files to `spu94_obj OBJECT` in `src/spu94/CMakeLists.txt`. No other build changes required. The new headers go in `include/spu94/` and are automatically on the existing include path.

---

## State Size Impact

The current `spu94_state` contains single-voice fields that total approximately 400 bytes. Replacing with a 24-voice `spu94_voice_mixer_t` (stored separately from `spu94_state` — see integration note below) avoids growing `spu94_state` by 24× and breaking the `SPU94_STATE_SIZE_MAX = 16384` bound.

**Recommended: keep `spu94_voice_mixer_t` separate from `spu94_state`.**

The reverb engine state (`spu94_state`) and the voice engine state (`spu94_voice_mixer_t`) are separate hardware blocks on real PS1. Keeping them separate in code is correct and avoids struct size pressure.

The standalone app and JUCE plugin allocate `spu94_voice_mixer_t` as a file-scope static or class member alongside their existing `spu94_state`. The C API does not force them into the same struct.

`spu94_voice_t` size estimate:
- All existing single-voice fields from `spu94_state_internal.h`: ~400 bytes
- Plus: ADSR state (~12 bytes), loop/current address (~12 bytes), key/reverb flags (~4 bytes), voice volumes (~4 bytes)
- Total per voice: ~430 bytes
- 24 voices: ~10,320 bytes
- `spu94_voice_mixer_t` total: ~10,340 bytes (fits in stack or static, no heap needed)

---

## Integration Points with Existing Code

```
spu94_process() today:
  L_in/R_in → input_gain → [single ADPCM voice] → patina_l/r
                                                  → dry bus
                                                  → reverb send → spu94_fir_chain_step()
                                                  → master mixer
                                                  → DAC

spu94_process() after v1.8:
  L_in/R_in → input_gain → existing ADPCM coloration (unchanged, keep as effect)
  spu94_voice_mixer_tick() → voice_dry_l/r (24-voice sum)
                           → voice_rev_l/r (reverb-on voices)
  voice_dry_l/r → mix into dry bus (replace or alongside patina bus)
  voice_rev_l/r → replace or augment reverb send
  → spu94_fir_chain_step() (unchanged)
  → master mixer (unchanged)
  → DAC (unchanged)
```

The exact integration (whether voice output replaces or augments the existing coloration path) is an architectural decision for the roadmap phase, not a stack question. Either wiring is achievable with the proposed modules.

---

## Sources

- `src/spu94/spu94_process.c` — existing single-counter voice path (production code, HIGH confidence)
- `src/spu94/spu94_state_internal.h` — existing voice state fields (HIGH confidence)
- `.planning/research/DEEP-SPU-VOICE-PATH.md` — researched 2026-05-15, cross-verified against nocash + DuckStation + Mednafen (HIGH confidence)
- `include/spu94/spu94_adpcm.h` / `spu94_gauss.h` — existing codec and table (HIGH confidence)
- psx-spx.consoledev.net SPU ADSR section — register bit fields and envelope pseudo-code (HIGH confidence, verified against independent consoledev mirror)
- `include/spu94/spu94.h` — existing public API, state size bounds, work buffer contract (HIGH confidence)
