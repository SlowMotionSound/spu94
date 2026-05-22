# Technology Stack

**Project:** SPU-94 v1.9 Complete Voice (PMON, NON, Volume Sweep, Signed Volume)
**Researched:** 2026-05-21

## Executive Summary

v1.9 adds four PS1 SPU voice features to the existing 24-voice sampler engine. All four are **pure fixed-point integer math** -- no new external libraries needed. The implementation lives entirely within the existing C99 core (`libspu94`), extending `spu94_voice_t` and `spu94_voice_mixer_t` with new per-voice state fields and a shared noise generator.

Zero new dependencies. Zero library additions. The "stack" for this milestone is structural changes to existing code, not new technology.

## Recommended Stack

### Core Framework -- No Changes

| Technology | Version | Purpose | Why |
|------------|---------|---------|-----|
| C99/C11 | gcc 13+ / clang 17+ | DSP core | Already validated; all four features are integer math |
| JUCE | 8.0.7 (pinned) | GUI + plugin host | Existing; new GUI controls only |
| CMake | 3.24+ | Build system | Add new .c files to `spu94_obj` OBJECT library |

### New Source Files (C Core)

| File | Purpose | Rationale |
|------|---------|-----------|
| `src/spu94/spu94_noise_gen.c` | SPU noise LFSR generator | Separate from DAC noise -- different polynomial (Fibonacci XOR vs Galois), different stepping (timer-based countdown), global shared state across all voices |
| `include/spu94/spu94_noise_gen.h` | Public header for noise generator | Matches existing module-per-header pattern (cf. `spu94_adsr.h`, `spu94_dac_noise.h`) |
| `src/spu94/spu94_vol_sweep.c` | Volume sweep envelope tick | Same counter-accumulate mechanism as ADSR but applied to volume register, not envelope level |
| `include/spu94/spu94_vol_sweep.h` | Public header for volume sweep | Keeps sweep logic testable in isolation like ADSR |

### No New Files Needed For

| Feature | Why No New File |
|---------|-----------------|
| PMON (pitch modulation) | ~20 lines added to the voice iteration loop in `spu94_voice_mixer_tick()` |
| Signed Volume / Phase Inversion | Already supported by `vol_l`/`vol_r` being declared `int16_t`; `q15_mul_truncate` handles negative values correctly; only needs API/GUI surface to expose negative volumes |

### Supporting Libraries -- No Additions

| Library | Version | Purpose | When to Use |
|---------|---------|---------|-------------|
| Unity test framework | vendored | Unit tests for new modules | Test spu94_noise_gen, spu94_vol_sweep, PMON chain behavior |
| dr_wav | vendored | Golden file I/O | Golden regression tests for voice-with-PMON/NON audio |
| pytest + numpy | existing | Integration/fuzz tests | Noise frequency verification, sweep trajectory validation |

## Structural Changes to Existing Code

### 1. `spu94_voice_t` -- New Fields

```c
/* v1.9 additions to spu94_voice_t (include/spu94/spu94_voice.h) */

/* Volume Sweep: per-voice automatic volume ramp (independent L/R) */
spu94_vol_sweep_t sweep_l;    /* left channel volume sweep state */
spu94_vol_sweep_t sweep_r;    /* right channel volume sweep state */
```

The `spu94_vol_sweep_t` struct (defined in `spu94_vol_sweep.h`):

```c
typedef struct {
    /* Register fields -- loaded before activation */
    uint8_t  mode;       /* 0 = linear, 1 = exponential */
    uint8_t  direction;  /* 0 = increase, 1 = decrease */
    uint8_t  phase;      /* 0 = positive, 1 = negative (target polarity) */
    uint8_t  shift;      /* 0..31 (same semantics as ADSR shift) */
    uint8_t  step;       /* 0..3 (same semantics as ADSR step) */

    /* Runtime state */
    uint8_t  active;     /* 0 = sweep disabled (direct volume mode) */
    uint32_t counter;    /* accumulator -- same counter-accumulate as ADSR */
} spu94_vol_sweep_t;
```

Size: ~12 bytes per sweep instance. Two per voice = ~24 bytes per voice.
Total mixer growth: 24 voices * 2 config copies * ~24 bytes = ~1152 bytes. Negligible vs the 512 KB voice_ram.

Note: `noise_on` does NOT live in the voice struct. NON is a mixer-level bitmask (like `eon_flags`), not per-voice state. This matches the PS1 where NON is a single 24-bit register, not part of the voice configuration.

### 2. `spu94_voice_mixer_t` -- New Fields

```c
/* v1.9 additions to spu94_voice_mixer_t */

/* PMON: bitmask -- bit N set = voice N pitch is modulated by voice N-1 output */
uint32_t  pmon_flags;         /* bits 1..23 valid; bit 0 always ignored (no voice -1) */

/* NON: bitmask -- bit N set = voice N outputs noise instead of ADPCM */
uint32_t  non_flags;          /* bits 0..23 valid */

/* SPU noise generator: GLOBAL shared state (all NON voices read same output per tick) */
spu94_noise_gen_t noise_gen;  /* LFSR + timer, stepped once per mixer tick */
```

### 3. `spu94_noise_gen_t` -- New Module

```c
/* include/spu94/spu94_noise_gen.h */
typedef struct {
    int16_t  level;       /* current output level, signed 16-bit */
    int32_t  timer;       /* countdown timer; steps when < 0 */
    uint8_t  shift;       /* 0..15: frequency control (SPUCNT bits 13-10) */
    uint8_t  step;        /* 0..3: maps to actual step 4,5,6,7 (SPUCNT bits 9-8) */
} spu94_noise_gen_t;
```

Algorithm per tick (directly from nocash psx-spx):
```
ParityBit = Level.Bit15 XOR Bit12 XOR Bit11 XOR Bit10 XOR 1
Timer -= (Step + 4)
IF Timer < 0:
    Level = Level * 2 + ParityBit    (left-shift + inject parity)
    Timer += (0x20000 >> Shift)
    IF Timer < 0:
        Timer += (0x20000 >> Shift)   (double-reload clamp)
```

This is DIFFERENT from the DAC noise generator in every respect:
- DAC noise: Galois LFSR, x^32 polynomial, HP-shaped output, models analog noise floor
- SPU noise: Fibonacci LFSR, XOR of bits 15/12/11/10, timer-gated stepping, raw digital noise for hi-hats/snares/effects

### 4. `spu94_voice_mixer_tick()` -- Processing Order Change

Current order:
1. Apply pending KON/KOFF
2. Iterate 24 voices independently (order preserved but not exploited)
3. Accumulate dry + reverb sums

New order (PS1-faithful):
1. Apply pending KON/KOFF
2. Step the global noise generator one tick
3. Iterate voices 0..23 IN STRICT ORDER:
   a. Step volume sweep for this voice (updates vol_l/vol_r if sweep active)
   b. If `non_flags & bit`: output = noise_gen.level (skip ADPCM decode + Gaussian)
   c. Else: existing ADPCM decode + Gaussian interpolation
   d. Apply ADSR envelope (existing)
   e. Apply current volume (may be sweep-updated, may be negative) (existing q15_mul_truncate)
   f. Cache mono pre-volume output as `prev_voice_outx` for PMON
   g. If NEXT voice has PMON bit set: compute modulated pitch for next voice
   h. Accumulate dry + reverb sums (existing)

Critical: PMON requires sequential voice evaluation (voice N-1 output feeds voice N pitch). The existing loop already runs 0..23 in order. The change is adding the output-tap cache and pitch modulation computation.

### 5. PMON Pitch Modulation Formula

Implemented as ~12 lines inside the mixer tick loop:

```c
/* After voice N produces its output: */
int16_t prev_outx = gauss_out;  /* post-ADSR, pre-volume = VxOUTX */

/* Before voice N+1 starts: apply PMON if flagged */
if (pmon_flags & (1u << (v + 1))) {
    int32_t factor = (int32_t)prev_outx + 0x8000;  /* normalize to 0..0xFFFF */
    int32_t base_pitch = (int32_t)(int16_t)m->voices[v+1].pitch;  /* sign-extend */
    int32_t modulated = (base_pitch * factor) >> 15;
    modulated &= 0x0000FFFF;
    if (modulated > 0x3FFF) modulated = 0x4000;  /* PS1 clamp behavior */
    /* Use modulated pitch for voice v+1's counter advance this tick */
}
```

No new function needed. No new source file. This lives in the mixer tick body.

### 6. Signed Volume / Phase Inversion

**Already works at the math level.** `q15_mul_truncate(sample, -volume)` produces a phase-inverted output. The fields `vol_l` and `vol_r` in `spu94_voice_t` are already declared `int16_t`.

What needs to change:
- `spu94_voice_mixer_key_on()`: remove any implicit clamp to positive (currently the GUI sends 0..32767 but the API accepts int16_t -- it already accepts negatives)
- Volume sweep: direction=increase with phase=negative targets -0x8000 instead of +0x7FFF
- GUI: allow volume knobs to go negative (or add a "phase invert" toggle that flips sign)

### 7. GUI Changes (JUCE)

| Component | Change | Scope |
|-----------|--------|-------|
| SamplerWindow / PluginEditor | NON toggle (per-voice or global) | Small -- toggle button |
| SamplerWindow / PluginEditor | PMON toggle (per-voice-pair) | Small -- toggle button |
| SamplerWindow / PluginEditor | Volume Sweep controls (mode, dir, shift) | Medium -- 3-4 controls |
| SamplerWindow / PluginEditor | Noise frequency (global shift + step) | Small -- 2 controls |
| SamplerWindow / PluginEditor | Phase inversion toggle (per-voice L/R) | Small -- 2 toggle buttons |

### 8. `spu94_state` (Reverb Engine State) -- No Changes

The reverb engine state struct does NOT need modification. All v1.9 features live in the voice mixer, which is file-scope static in `spu94_process.c`. The `SPU94_STATE_SIZE_MAX` limit (16384 bytes, currently ~2700 bytes used) is not affected.

## What NOT to Add

| Temptation | Why Not |
|------------|---------|
| Floating-point math for sweep curves | PS1 uses integer counter-accumulate; float would break bit-faithfulness |
| Separate LFSR per voice for NON | PS1 has ONE global noise generator; all NON voices hear the same sample simultaneously |
| Smoothing/interpolation on PMON factor | PS1 applies modulation raw at 44.1 kHz tick rate; smoothing is a modern-DSP departure |
| New external noise library | The noise gen is ~15 lines of C; the existing DAC noise proves the project's LFSR pattern works |
| Per-voice noise frequency | PS1 SPU has one global noise frequency (SPUCNT bits 8-13); all NON voices share it |
| APVTS integration for new params | Existing architecture uses atomic scalar bridge (v1.7 precedent); keep consistent |
| libsamplerate / resampling for PMON pitch changes | PMON modulates the pitch counter directly, not the audio; no resampling involved |
| Anti-click/crossfade on NON switching | PS1 has no crossfade; switching noise on/off produces the hardware-authentic pop |
| Volume smoothing on sweep changes | Sweep itself IS the smoothing; it ramps by design |

## Integration Points

### With Existing ADSR

Volume Sweep runs IN ADDITION to ADSR. Signal chain per voice:
```
ADPCM decode (or noise) -> Gaussian interp -> ADSR envelope -> Volume (sweep-updated) -> L/R output
                                                                         ^
                                                                         |
                                                              sweep ticks vol_l/vol_r each sample
```
The volume sweep modifies `vol_l`/`vol_r` every tick (when active); ADSR modifies the envelope level independently. Both are Q15 multiplies in series. This matches the PS1 where sweep and ADSR are independent hardware units.

### With Existing Mixer

The mixer tick loop runs voices sequentially (0..23). PMON requires this ordering to be preserved (it already is). The noise generator tick happens once before the voice loop. Volume sweep ticks happen per-voice before the volume multiply. No mixer architecture changes needed -- just additions within the existing loop body.

### With DAW Plugin (v1.7)

New parameters (PMON flags, NON flags, noise frequency, sweep controls) will need host-automatable parameter entries in `ParameterBridge`. This is the same pattern used for the existing 9 parameters -- atomic scalars read in the audio callback. Likely additions: NON frequency (1 param), possibly PMON/NON/sweep controls exposed per-voice for the active voice.

### With Preset System (v1.4)

The preset serializer (`spu94_preset_io.c`) handles 46 fields currently. New fields (noise_shift, noise_step, pmon_flags, non_flags) will need new key=value entries. The parser ignores unknown keys (existing D-09 contract), so old presets load safely in new code. New presets in old code skip the unknown fields -- graceful degradation by design.

## Build System Changes

```cmake
# Add to src/spu94/CMakeLists.txt spu94_obj OBJECT library:
    spu94_noise_gen.c
    spu94_vol_sweep.c
```

No new `find_package`. No new vendored code. No new link dependencies.

## Estimated Code Size

| Component | New LOC | Changed LOC |
|-----------|---------|-------------|
| `spu94_noise_gen.c` + header | ~60 | 0 |
| `spu94_vol_sweep.c` + header | ~120 | 0 |
| `spu94_voice.h` struct additions | 0 | ~10 |
| `spu94_voice.c` tick body (NON path, sweep tick) | 0 | ~40 |
| `spu94_voice_mixer_tick` (PMON, NON dispatch, sweep) | 0 | ~50 |
| Mixer API additions (set_pmon, set_non, set_noise_freq) | ~40 | 0 |
| Unit tests (noise gen, sweep, PMON) | ~300 | 0 |
| GUI controls | ~80 | ~30 |
| **Total** | **~600** | **~130** |

## Sources

- nocash psx-spx SPU documentation: https://psx-spx.consoledev.net/soundprocessingunitspu/ -- PMON formula, noise polynomial, sweep mechanism, volume register layout, SPUCNT noise frequency bits -- HIGH confidence
- Existing codebase: `include/spu94/spu94_voice.h`, `src/spu94/spu94_voice.c`, `src/spu94/spu94_process.c`, `src/spu94/spu94_adsr.c` -- direct code analysis -- HIGH confidence
- Existing DAC noise implementation (`spu94_dac_noise.c`) -- architectural precedent for LFSR modules -- HIGH confidence
- v1.8 mixer implementation in `spu94_voice.c` lines 410-482 -- integration point analysis -- HIGH confidence
