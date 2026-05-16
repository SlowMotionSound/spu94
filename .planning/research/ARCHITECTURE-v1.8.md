# Architecture: v1.8 PSX Voice Engine Integration

**Project:** SPU-94  
**Milestone:** v1.8 — 24-voice ADPCM sampler  
**Researched:** 2026-05-16  
**Confidence:** HIGH — based on direct inspection of all existing C source files

---

## Executive Summary

The existing spu94_process.c already contains a **single-voice ADPCM pitch engine**
(voice_pitch, voice_counter, gauss_ring, adpcm encode/decode buffers) wired into the
patina bus. This is the architectural seed for the v1.8 voice engine. The work is
primarily a **refactor and expansion** of what already exists, not greenfield code.

The real PS1 SPU processes all 24 voices, sums them, then feeds the sum into the reverb
input. That ordering maps cleanly onto the existing mixer architecture: voices sum into
`mix_bus_l/r` before the reverb network runs, which is exactly the existing send path.

The 512 KB SPU RAM (`SPU94_WORK_BUF_MAX_BYTES = 0x80000`) already exists as the reverb
work buffer. On real hardware, SPU RAM is shared — the reverb network owns a region near
the top (set by `mBASE`), and voice ADPCM sample data lives in the lower region. That
partition must be modeled explicitly in v1.8.

---

## 1. How the Real PS1 SPU Routes Voices

On the real SPU (nocash psx-spx, paraphrased):

```
For each 44.1 kHz sample tick:
  For each of 24 voices:
    advance voice_counter by voice_pitch
    if counter crossed integer boundary:
      advance ADPCM sample pointer (with loop flag handling)
      push new decoded sample into 4-tap Gaussian ring
    output = Gaussian_interpolate(ring, counter_frac)
    apply ADSR envelope
    voice_out_L = output * vol_L
    voice_out_R = output * vol_R
    if voice.reverb_on:
      reverb_in_L += voice_out_L
      reverb_in_R += voice_out_R
    dry_out_L += voice_out_L
    dry_out_R += voice_out_R
  run reverb on reverb_in_L/R  ->  reverb_out_L/R
  final_L = dry_out_L + reverb_out_L * vLOUT
  final_R = dry_out_R + reverb_out_R * vROUT
```

Key observations from the spec:
1. All 24 voices run **before** reverb. Reverb input is the **sum** of reverb-enabled voices.
2. The dry output is the **sum of all voices** (reverb-on AND reverb-off).
3. Per-voice `reverb_on` flag is a **1-bit gate**, not a send level.
4. ADSR envelope is applied **per-voice before** the L/R volume split.
5. The single pitch counter drives **both** sample advancement and Gaussian interpolation
   — exactly as already implemented in spu94_state for the single voice.

---

## 2. Mapping to Existing Architecture

### What Already Exists (Do Not Break)

| Component | Location | Role in v1.8 |
|-----------|----------|---------------|
| `spu94_adpcm_encode_block` / `spu94_adpcm_decode_block` | `spu94_adpcm.c` | Reused unchanged for each voice's ADPCM decode |
| Gaussian table + interpolation | `spu94_gauss.c` | Reused unchanged per-voice |
| `spu94_fir_chain_step` / `spu94_tick` / `spu94_reverb_body` | `spu94_io_chain.c`, `spu94_tick.c`, `spu94_reverb.c` | Untouched — reverb network is downstream of the voice sum |
| Single-voice path in `spu94_process` | `spu94_process.c` lines 54-151 | Becomes voice 0 in the multi-voice expansion |
| `spu94_state.voice_pitch`, `voice_counter`, `gauss_ring_*`, `adpcm_buf_pos`, `adpcm_in_buf_*`, `adpcm_out_buf_*`, `adpcm_state_*`, `gauss_out_pos`, `gauss_ring_pos` | `spu94_state_internal.h` | Migrated into per-voice struct |
| `work_buf` + `work_buf_size` = 512 KB | `spu94_state_internal.h` | Partitioned: low = voice ADPCM data, high = reverb delay lines |
| RT-safety discipline (no heap, no locks, no syscalls) | All .c files | Must remain absolute — verified by existing ctest gates |

### What the Current "ADPCM Voice Path" Actually Is

The existing single-voice path in `spu94_process.c` (lines 54-151):
- Has one pitch counter, one Gaussian ring, and one 28-sample encode/decode buffer
- On each 44.1 kHz tick: accumulates pitch into `voice_counter`, detects integer crossing,
  pushes input PCM through encode+decode, pushes decoded sample into ring, Gaussians out
- This is a **live ADPCM insert effect** (encode WAV input → decode back), not a sampler
- In v1.8 this becomes: load pre-encoded ADPCM into SPU RAM, decode from RAM on demand,
  no encode step during playback

The encode step (`spu94_adpcm_encode_block`) disappears from the hot path. Playback reads
raw ADPCM blocks from SPU RAM and decodes them. Encoding happens once at sample-load time,
off the hot path, before `spu94_process` is ever called.

---

## 3. New Components Required

### 3a. `spu94_voice.h` / `spu94_voice.c` — New Module

Per-voice state struct and voice-tick function. This is the primary new file.

```c
/* Conceptual layout -- exact field names to be finalized during implementation */
typedef struct {
    /* SPU RAM addressing */
    uint32_t  sample_start_addr;   /* byte offset in SPU RAM: start of ADPCM data */
    uint32_t  loop_start_addr;     /* byte offset in SPU RAM: loop start block */
    uint32_t  current_addr;        /* byte offset of current ADPCM block being decoded */

    /* Pitch / single-counter (matches existing voice_counter discipline) */
    uint16_t  pitch;               /* 0x0001..0x3FFF; 0x1000 = 44.1 kHz */
    uint16_t  counter;             /* 4.12 fixed-point, same semantics as voice_counter */

    /* ADPCM decode state */
    spu94_adpcm_state  adpcm_state;
    int16_t            decode_buf[SPU94_ADPCM_BLOCK_SAMPLES];  /* current block */
    uint8_t            decode_buf_pos;  /* sample position within current block */

    /* Gaussian ring: last 4 decoded samples */
    int16_t   gauss_ring[4];
    uint8_t   gauss_ring_pos;     /* write head (0..3) */

    /* ADSR envelope */
    int16_t   env_level;          /* current envelope value, Q15 */
    uint8_t   env_phase;          /* ATTACK / DECAY / SUSTAIN / RELEASE */
    /* ADSR parameters: attack_rate, decay_rate, sustain_level, sustain_rate,
       release_rate — per-voice, caller-configured */
    uint8_t   attack_rate;
    uint8_t   decay_rate;
    uint8_t   sustain_level;      /* 0..15 -> 0..0x7800 Q15 */
    uint8_t   sustain_rate;
    uint8_t   release_rate;
    uint8_t   env_mode;           /* attack shape: linear=0, exponential=1 */

    /* Routing */
    uint8_t   reverb_on;          /* 1 = contribute to reverb send */
    int16_t   vol_l;              /* Q15 left output volume */
    int16_t   vol_r;              /* Q15 right output volume */

    /* Lifecycle */
    uint8_t   active;             /* 0 = voice is off/stolen; 1 = playing */
    uint8_t   loop_flag;          /* latched ADPCM block[1] from last decode */
} spu94_voice_t;

/* Tick one voice: advance counter, decode if needed, Gaussian interpolate,
 * apply envelope. Returns (left_sample, right_sample) as int32 (pre-clamp).
 * Reads ADPCM blocks from spu_ram[voice->current_addr].
 * RT-safe: no heap, no function pointers, no syscalls. */
void spu94_voice_tick(spu94_voice_t *voice,
                      const unsigned char *spu_ram, uint32_t spu_ram_size,
                      int32_t *out_l, int32_t *out_r);
```

The voice tick function is called once per voice per 44.1 kHz tick, **before** the
`spu94_fir_chain_step` call. This matches the real hardware order.

### 3b. Voice Array in `spu94_state`

The 24-voice array lives in `spu94_state`, alongside existing fields:

```c
/* Addition to struct spu94_state (spu94_state_internal.h) */
spu94_voice_t  voices[24];
uint32_t       voice_on_mask;    /* bit N set = voice N was just triggered this tick */
uint32_t       voice_off_mask;   /* bit N set = start release phase on voice N */
uint32_t       active_mask;      /* bit N set = voice N is playing (not silent) */
```

Size budget: `sizeof(spu94_voice_t)` needs to stay within budget.
`spu94_state` currently must fit within `SPU94_STATE_SIZE_MAX = 16384` bytes.
A rough count: 24 voices × ~120 bytes each = ~2880 bytes. Current struct is well under
16384 (all existing fields sum to roughly 5-6 KB). Adding 24 voices stays comfortably
within the limit. The `_Static_assert` in `spu94_state_internal.h` will catch any
overflow at compile time.

### 3c. SPU RAM Partition

The existing `work_buf` (512 KB) plays the role of SPU RAM. Its current use by the
reverb network is via `mBASE`-relative addressing (`reverb_buf_read/write` in
`spu94_reverb.c`). The reverb region starts at `mBASE * 8` (bytes). Real hardware
mBASE values for the deepest preset (Hall) are around 0x5000 halfwords = 0xA000 bytes.
The lower ~40 KB is therefore free for voice ADPCM data on every factory preset.

The partition is **implicit** — the voice engine reads from absolute byte offsets set by
`sample_start_addr` per voice, and the reverb engine reads from `buffer_address +
halfword_offset`. No collision can occur as long as voice data is below `mBASE * 8`.

A new public API function handles sample loading:

```c
/* Load ADPCM data into SPU RAM at a specific byte offset.
 * source: pre-encoded ADPCM blocks (e.g. from spu94_adpcm_encode_block calls)
 * addr: destination byte offset in work_buf (caller manages allocation)
 * Returns SPU94_OK or SPU94_INVALID_ARG if addr+size overflows work_buf or
 * collides with the reverb region. */
spu94_result_t spu94_voice_load_sample(spu94_state *state,
                                       uint32_t     addr,
                                       const uint8_t *source,
                                       uint32_t      num_blocks);

/* Trigger a voice: set start address, pitch, vol, reverb_on; arm ATTACK.
 * voice_idx: 0..23. */
spu94_result_t spu94_voice_key_on(spu94_state *state, int voice_idx,
                                  uint32_t sample_start_addr,
                                  uint32_t loop_start_addr,
                                  uint16_t pitch,
                                  int16_t vol_l, int16_t vol_r,
                                  int reverb_on);

/* Release a voice (start release phase). */
spu94_result_t spu94_voice_key_off(spu94_state *state, int voice_idx);
```

---

## 4. Modified Components

### 4a. `spu94_process.c` — Hot Path Wiring Change

The single-voice ADPCM block (lines 54-151) is replaced with the 24-voice tick loop.
The structure becomes:

```c
/* BEFORE (current): */
int16_t patina_l, patina_r;
if (state->adpcm_enabled) {
    /* single voice encode/decode path */
    ...
}

/* AFTER (v1.8): */
int32_t voice_mix_l = 0, voice_mix_r = 0;
int32_t reverb_send_l = 0, reverb_send_r = 0;
if (state->voice_engine_enabled) {
    for (int v = 0; v < 24; v++) {
        if (!state->voices[v].active) continue;
        int32_t vl = 0, vr = 0;
        spu94_voice_tick(&state->voices[v],
                         state->work_buf, state->work_buf_size,
                         &vl, &vr);
        voice_mix_l = sat_s32_16(voice_mix_l + vl);  /* or sat_s16 accumulation */
        voice_mix_r = sat_s32_16(voice_mix_r + vr);
        if (state->voices[v].reverb_on) {
            reverb_send_l = sat_s32_16(reverb_send_l + vl);
            reverb_send_r = sat_s32_16(reverb_send_r + vr);
        }
    }
    /* Pass reverb_send into the existing reverb send path */
    send_l = sat_s16(reverb_send_l);
    send_r = sat_s16(reverb_send_r);
    /* voice_mix feeds dry output directly */
    ...
}
```

The existing `adpcm_enabled` / `patina_fader` / `patina_send` path remains active for
the reverb-as-effect use case (WAV input through ADPCM → reverb). These are two separate
operating modes. The `voice_engine_enabled` flag gates the voice engine; `adpcm_enabled`
continues to gate the insert-effect path. They can coexist or be mutually exclusive — a
design decision to document in DECISIONS.md.

### 4b. `spu94_state_internal.h` — New Fields

Add `voices[24]`, the mask registers, a `voice_engine_enabled` toggle, and a
`spu94_voice_t` struct definition (or include a forward-declared internal header
`spu94_voice_internal.h`). The `_Static_assert` catches any size overflow automatically.

### 4c. `include/spu94/spu94.h` — New Public API Surface

Add the voice key_on / key_off / load_sample / engine enable functions. These follow
the existing null-safe, no-heap API style exactly.

### 4d. Standalone (`src/standalone/`) — New Usage Pattern

The existing `WavLoader` loads a WAV to a `LoadedWav` struct (a heap-allocated
`std::vector<int16_t>`). In v1.8, the standalone testbed gains a "load sample" step:

1. Load WAV with `WavLoader::load()` (unchanged)
2. Call `spu94_adpcm_encode_block()` in a loop to encode the PCM into ADPCM blocks
   (one 16-byte block per 28 samples)
3. Call `spu94_voice_load_sample()` to write encoded blocks into the work_buf
4. Call `spu94_voice_key_on()` with pitch, volume, reverb routing
5. Drive `spu94_process()` as before — no change to the inner loop

This is the "load sample into SPU RAM, trigger voice playback" transform described in
the milestone scope. The WAV load/play flow becomes a WAV encode/load/trigger flow.
The `spu94_process` call signature does not change.

---

## 5. Data Flow Diagram

```
LOAD TIME (off hot path, standalone message thread / init):
  WAV file
    -> WavLoader::load()          float -> int16 at 44.1 kHz
    -> ADPCM encode               (spu94_adpcm_encode_block x N)
    -> spu94_voice_load_sample()  writes encoded blocks to work_buf[addr]

TRIGGER:
  spu94_voice_key_on(voice=0, addr, pitch, vol_l, vol_r, reverb_on=1)
  -> sets voices[0].active=1, arms ATTACK phase

PER-SAMPLE HOT PATH (44.1 kHz, called from spu94_process):
  [for each active voice v]
    spu94_voice_tick(&state->voices[v], state->work_buf, ...)
      advance voice_counter by pitch
      if integer crossing:
        if decode_buf_pos == 28:
          read next 16-byte block from work_buf[current_addr]
          spu94_adpcm_decode_block() -> decode_buf[28]
          handle loop_flag (END -> jump to loop_start_addr)
          current_addr += 16
          decode_buf_pos = 0
        push decode_buf[decode_buf_pos++] into gauss_ring
      gauss_out = Gaussian_interpolate(gauss_ring, counter & 0xFF)
      apply ADSR envelope:  gauss_out = q15_mul(gauss_out, env_level)
      step ADSR state machine
      out_l = q15_mul(gauss_out, vol_l)
      out_r = q15_mul(gauss_out, vol_r)
    accumulate into voice_mix (dry) and reverb_send (if reverb_on)

  voice_mix -> dry bus fader -> master output
  reverb_send -> spu94_fir_chain_step(send_l, send_r)
                   -> FIR decimate -> spu94_tick -> spu94_reverb_body
                   -> FIR interpolate -> rev_l, rev_r
  master = sat_s16(voice_mix + rev * reverb_fader + ...)
  DAC section (unchanged)
  -> L_out, R_out
```

Crucially: `spu94_reverb_body` and `spu94_tick` are **not touched**. Reverb sees a
different input (the voice reverb-send sum instead of an external signal) but the same
interface: `state->mix_bus_l/r` written by `chain_step_impl`, consumed by
`spu94_reverb_body`. The reverb network is downstream of the voice sum, exactly as
on real hardware.

---

## 6. SPU RAM Region Model

```
work_buf (512 KB = 0x80000 bytes)
  [0x00000 .. voice_data_end)      Voice ADPCM sample data
                                   (managed by caller: spu94_voice_load_sample)
  [voice_data_end .. mBASE*8)      Free / guard zone
  [mBASE*8 .. 0x7FFFE]            Reverb delay lines
                                   (managed by reverb_buf_read/write)
```

The boundary between voice data and reverb is `mBASE * 8` bytes. The reverb engine
never reads or writes below `buffer_address + 0` (which cycles relative to `mBASE`).
Voice data lives below `mBASE * 8` — the regions never overlap as long as the caller
obeys the partition.

`spu94_voice_load_sample` should validate `addr + num_blocks * 16 <= mBASE_bytes` and
return `SPU94_INVALID_ARG` if the write would enter the reverb region. This is a
correctness guard, not a complex allocator — caller manages the voice data address space.

The current `spu94_preset_min_work_buf_size()` already gives `(max_halfword_value + 1)
* 2` bytes required by the reverb. Subtract that from 0x80000 to get the available
voice data space. For Hall (the deepest preset), this is roughly 512 - 41 = 471 KB of
usable sample space. Adequate for a 24-voice sampler.

---

## 7. ADSR Envelope

The PS1 ADSR uses a hardware state machine with four phases. The key architectural
point: **envelope is applied per-voice before L/R volume scaling**, not after. The
existing codebase has no ADSR at all. This is net-new DSP code.

ADSR parameters per voice (from nocash psx-spx):
- Attack: rate (0..127), shape (linear/exponential)
- Decay: rate (0..15), always exponential
- Sustain: level (0..15, maps to 0..0x7800 Q15), rate (0..127), direction, shape
- Release: rate (0..31), shape

The ADSR state machine runs **once per 44.1 kHz sample tick** inside `spu94_voice_tick`.
Envelope advance uses the same Q15 arithmetic as the reverb. The nocash rate tables give
precise tick-per-step counts that match real hardware behavior.

ADSR is required for spec-faithful loop behavior: a sample that loops indefinitely
sustains at the sustain envelope level. Without ADSR the voice plays at full level
forever, which is wrong for any real PS1 instrument.

---

## 8. Loop Flag Handling

The PS1 ADPCM block header byte (block[1]) carries three flags:
- bit 0: END — this is the last block; after decoding, jump to loop_start_addr
- bit 1: REPEAT — if set alongside END, enable loop; if not set, silence voice after END
- bit 2: LOOP START — marks which block is the loop point

`spu94_adpcm_decode_block` already returns the flag byte (the existing API). The voice
engine reads this return value after each decode to drive the loop state machine:

```c
uint8_t flags = spu94_adpcm_decode_block(&voice->adpcm_state,
                                          block_ptr, voice->decode_buf);
voice->loop_flag = flags;
if (flags & 0x01) {
    if (flags & 0x02) {
        /* loop: jump to loop_start_addr */
        voice->current_addr = voice->loop_start_addr;
        voice->adpcm_state = ...  /* reset or carry? -- document in DECISIONS.md */
    } else {
        /* end, no loop: start release phase or silence immediately */
        spu94_voice_key_off(state, v);
    }
}
```

Loop ADPCM state carry (whether `adpcm_state.old/older` is reset or preserved on loop)
is a gray area. On real hardware the decoder state carries through the loop point because
the SPU's ADPCM decoder is stateful across blocks. This must be documented in
DECISIONS.md — it directly affects sound at the loop seam.

---

## 9. Build Order: Monophonic First, Polyphonic Second

### Phase 1 — Single Voice in Standalone Testbed

Target: one voice, no ADSR, no loop, pitch + Gaussian interpolation working.

1. Define `spu94_voice_t` struct in a new `spu94_voice_internal.h`
2. Implement `spu94_voice_tick()` in new `spu94_voice.c`:
   - pitch counter advance (identical to existing `voice_counter` logic)
   - ADPCM decode from SPU RAM (`spu94_adpcm_decode_block`, existing)
   - Gaussian interpolation (existing `spu94_gauss_table`, existing logic)
   - no ADSR yet: output at full level
3. Implement `spu94_voice_load_sample()` — writes pre-encoded ADPCM into work_buf
4. Implement `spu94_voice_key_on()` / `spu94_voice_key_off()` — lifecycle
5. Modify `spu94_process.c`: add `voice_engine_enabled` check; call `spu94_voice_tick`
   for voice 0 only; route output to dry + reverb send
6. Expose public API in `spu94.h`
7. Update standalone: encode WAV on load, call key_on, run process loop

Dependency: Phase 1 depends on existing `spu94_adpcm_decode_block` (stable), existing
Gaussian table (stable), existing `spu94_process` hot path (modify only the input stage).

Verification gate: standalone plays a loaded WAV sample with pitch control through the
reverb engine. Golden-file test of voice output at a fixed pitch.

### Phase 2 — ADSR Envelope

Target: spec-faithful four-phase envelope on the single voice.

1. Add ADSR state fields to `spu94_voice_t`
2. Implement ADSR step function (net-new DSP, no existing code to build on)
3. Integrate into `spu94_voice_tick` after Gaussian output
4. Test: measure attack, sustain, release curves against nocash rate tables

Dependency: Phase 2 requires Phase 1 (voice tick infrastructure).

Verification gate: ADSR curve matches nocash rate table predictions within 1 tick.

### Phase 3 — Loop Flags

Target: END/REPEAT loop flag handling; infinite loop on sustained notes.

1. Add `loop_start_addr` to voice struct
2. After `spu94_adpcm_decode_block`, check returned flags and branch
3. Document ADPCM state carry decision in DECISIONS.md (gray area)
4. Test: looping sample does not pop at seam

Dependency: Phase 3 requires Phase 1 (decode path in place).

### Phase 4 — 24-Voice Polyphony

Target: all 24 voices in `spu94_state`; voice stealing; per-voice reverb routing.

1. Add `voices[24]` array to `spu94_state` (replaces single-voice fields)
2. Migrate existing single-voice state fields (`voice_counter`, `gauss_ring_*`, etc.)
   into `voices[0]` — remove from top-level struct
3. Update `spu94_process.c` hot path to loop over all 24 voices
4. Implement `active_mask` to skip silent voices efficiently
5. Add voice-stealing policy (optional: not strictly PS1 spec, document choice)
6. Verify `spu94_state` still fits within `SPU94_STATE_SIZE_MAX = 16384`

Dependency: Phase 4 requires Phases 1-3 (single voice fully working).

The struct migration in step 2 is the most disruptive change to existing code. Existing
tests that read `voice_counter` directly from the state (if any exist) will need updating.
Check test files before executing.

### Phase 5 — Standalone Testbed UX

Target: CLI or standalone JUCE surface to load a sample, set pitch, and play.

1. CLI subcommand `spu94 voice-play --sample foo.wav --pitch 0x1000 --preset Hall`
2. Or standalone JUCE: file picker for sample, pitch control, note trigger button
3. This is the "PSX Sampler Instrument" preview experience

Dependency: Phase 5 requires Phase 4 (polyphony) for meaningful UX, but single-voice
(Phase 1) is enough to begin CLI prototyping.

---

## 10. Integration Points Summary

| Integration Point | Nature | Risk |
|-------------------|--------|------|
| `spu94_process.c` voice block | Modify (replace single-voice with 24-voice loop) | Medium — existing test coverage on ADPCM insert path will need update |
| `spu94_state_internal.h` | Modify (add voice array, remove old fields) | Medium — byte-offset dependent Python tests probe struct layout |
| `spu94_voice.c` / `spu94_voice.h` | New file | Low — no existing code displaced |
| `spu94_reverb.c`, `spu94_tick.c`, `spu94_io_chain.c` | Untouched | None — reverb receives a different input but same interface |
| `spu94_adpcm.c` / `spu94_gauss.c` | Untouched | None — reused by voice tick |
| `spu94.h` (public API) | Additive (new functions) | Low — existing callers unaffected |
| Standalone `WavLoader.cpp` | Untouched | None — load step unchanged |
| Standalone init / process loop | Modify (add encode+load+key_on) | Low |
| Python byte-offset probes in `fuzz_process.py` | Update if `spu94_state` layout changes | Low — CI will catch immediately |

---

## 11. RT-Safety Checklist for New Code

Every new function that runs inside `spu94_process` must satisfy:

- No `malloc`, `calloc`, `realloc`, `free` (verify with `nm -u`)
- No `pthread_mutex_*`, `sem_*` (verify with `nm -u`)
- No `fopen`, `fwrite`, `printf`, `puts`, `fflush` (verify with `nm -u`)
- No `sin`, `cos`, `exp`, `pow` (these can syscall on some libm implementations)
- All buffers are caller-allocated (existing pattern: `voices[]` lives in `spu94_state`,
  which is caller-allocated; SPU RAM is the existing `work_buf`)

`spu94_adpcm_decode_block` already passes the RT-safety gates (it is called from the
existing `spu94_process` hot path). ADSR arithmetic uses integer multiply + shift —
RT-safe by construction. No new heap is introduced.

The four existing RT-safety ctest targets (`rt_no_heap`, `rt_no_locks`,
`rt_no_syscalls`, `rt_bench_latency`) will catch violations automatically.

---

## 12. New vs Modified Components at a Glance

```
NEW
  src/spu94/spu94_voice.c         -- voice tick, ADSR, loop flag handler
  include/spu94/spu94_voice.h     -- public voice API (key_on, key_off, load_sample)
  src/spu94/spu94_voice_internal.h -- spu94_voice_t struct definition

MODIFIED
  src/spu94/spu94_state_internal.h -- add voices[24], active_mask; migrate old fields
  src/spu94/spu94_process.c        -- replace single-voice block with 24-voice loop
  include/spu94/spu94.h            -- add voice API declarations + voice_engine_enabled

UNTOUCHED
  src/spu94/spu94_reverb.c
  src/spu94/spu94_tick.c
  src/spu94/spu94_io_chain.c
  src/spu94/spu94_adpcm.c
  src/spu94/spu94_adpcm_encode.c
  src/spu94/spu94_gauss.c
  src/spu94/spu94_fir.c
  src/spu94/spu94_dac_fir.c
  src/spu94/spu94_dac_noise.c
  src/spu94/spu94_preset_io.c
  src/spu94/spu94_interp.c
  src/spu94/spu94_slew.c
  src/spu94/vag.c
  src/standalone/WavLoader.cpp
  src/plugin/PluginProcessor.cpp   (voice engine is a C-core feature; plugin wraps it)
```

---

## 13. Gray Areas Requiring DECISIONS.md Entries

| Gray Area | Relevance | Disposition |
|-----------|-----------|-------------|
| ADPCM decoder state carry at loop point | Loop seam pop vs. silence | Follow real hardware: carry state |
| Voice sum saturation: sat_s16 per-accumulate vs. int32 accumulate + single clamp | Final mix character | Research nocash / witness behavior; document choice |
| Voice stealing policy for 24-voice overflow | Not specified in SPU spec | Document as implementation choice |
| ADSR sustain direction (increase/decrease after sustain level) | Sustain register encoding | Follow nocash rate table exactly |
| `adpcm_enabled` (insert effect) + `voice_engine_enabled` (sampler) coexistence | Are they mutually exclusive or stackable? | Document as design choice |

---

## 14. Existing Code to Validate Before Phase 4

Before removing the top-level `voice_counter` / `gauss_ring_*` / `adpcm_buf_pos` fields
from `spu94_state`, run:

```bash
grep -rn "voice_counter\|gauss_ring\|adpcm_buf_pos\|adpcm_in_buf\|adpcm_out_buf\|adpcm_state_l\|adpcm_state_r\|gauss_out_pos\|gauss_ring_pos" tests/
```

Any test that reads these fields by byte offset (the Python `fuzz_process.py` pattern)
will need to be updated after the struct layout changes. The `_Static_assert` on struct
size will catch growth; byte-offset tests require a manual grep pass to find silently
stale constants.

---

*Architecture research complete. Source: direct code inspection of all files in
`src/spu94/`, `include/spu94/`, `src/standalone/`, and `src/plugin/`. Confidence: HIGH.*
