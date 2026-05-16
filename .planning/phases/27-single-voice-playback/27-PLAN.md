---
phase: 27-single-voice-playback
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - include/spu94/spu94_voice.h
  - include/spu94/spu94_sample_loader.h
  - include/spu94/spu94_spu_ram.h
  - include/spu94/spu94_vag.h
  - src/spu94/spu94_voice.c
  - src/spu94/spu94_sample_loader.c
  - src/spu94/spu94_process.c
  - src/spu94/CMakeLists.txt
  - tests/unit/voice/test_voice_tick.c
  - tests/unit/voice/test_sample_loader.c
autonomous: true
requirements:
  - VOICE-01
  - VOICE-02
  - VOICE-03
  - VOICE-04
  - VOICE-05
  - VOICE-06
  - RAM-01
  - RAM-02
  - RAM-03
  - RAM-04

must_haves:
  truths:
    - "A WAV loaded into voice RAM encodes to ADPCM without touching the reverb work buffer"
    - "Key-on with pitch 0x1000 produces audible audio output at 44.1 kHz"
    - "Pitch 0x3FFF plays at hardware maximum; any value above 0x3FFF is clamped, not wrapped"
    - "Per-voice vol_l/vol_r (0-32767) scale amplitude; zero volume produces silence"
    - "Loading a sample whose encoded size exceeds 512 KB voice RAM returns SPU94_INVALID_ARG"
    - "24 spu94_voice_t structs exist with isolated gauss_ring[4] — no shared ring with the coloration bus"
  artifacts:
    - path: "include/spu94/spu94_voice.h"
      provides: "spu94_voice_t struct + spu94_voice_tick / spu94_voice_key_on / spu94_voice_key_off API"
      exports: ["spu94_voice_t", "spu94_voice_tick", "spu94_voice_key_on", "spu94_voice_key_off"]
    - path: "include/spu94/spu94_sample_loader.h"
      provides: "spu94_sample_encode_to_ram — off-hot-path WAV-to-ADPCM encoder"
      exports: ["spu94_sample_encode_to_ram"]
    - path: "include/spu94/spu94_spu_ram.h"
      provides: "SPU94_SPU_RAM_BYTES macro and layout contract comment"
      contains: "SPU94_SPU_RAM_BYTES"
    - path: "src/spu94/spu94_voice.c"
      provides: "spu94_voice_tick implementation (decode → Gauss → volume)"
    - path: "src/spu94/spu94_sample_loader.c"
      provides: "spu94_sample_encode_to_ram implementation"
    - path: "tests/unit/voice/test_voice_tick.c"
      provides: "Unity unit tests for voice tick: pitch clamp, Gauss output, volume scaling, silent when inactive"
    - path: "tests/unit/voice/test_sample_loader.c"
      provides: "Unity unit tests for sample loader: bounds check, round-trip encode, address arithmetic"
  key_links:
    - from: "src/spu94/spu94_voice.c"
      to: "src/spu94/spu94_adpcm.c"
      via: "spu94_adpcm_decode_block — decode-only, no encode call"
      pattern: "spu94_adpcm_decode_block"
    - from: "src/spu94/spu94_voice.c"
      to: "src/spu94/spu94_gauss.c"
      via: "spu94_gauss_table[512] read-only table"
      pattern: "spu94_gauss_table"
    - from: "src/spu94/spu94_process.c"
      to: "src/spu94/spu94_voice.c"
      via: "voice_engine_enabled guard + spu94_voice_tick call for voice 0"
      pattern: "spu94_voice_tick"
---

<objective>
Implement the spu94_voice_t struct, the spu94_voice_tick() per-sample function, and the
spu94_sample_encode_to_ram() load-time encoder. Wire voice 0 into spu94_process so that
a loaded ADPCM sample plays back with pitch control and 4-tap Gaussian interpolation.

Purpose: This is the architectural seed of the v1.8 PSX Voice Engine. Everything in
Phases 28-31 (ADSR, loop flags, 24-voice polyphony, standalone UX) builds on the voice
struct and tick loop defined here.

Output:
- Three new headers under include/spu94/
- Two new .c modules under src/spu94/
- spu94_process.c extended with voice_engine_enabled guard (voice 0 only)
- Unity tests for the voice tick and sample loader
- Build updated (CMakeLists.txt)
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/ROADMAP.md
@.planning/REQUIREMENTS.md
@.planning/research/ARCHITECTURE-v1.8.md
@.planning/research/PITFALLS-v1.8.md
@.planning/research/STACK-v1.8.md

# Key source files
@src/spu94/spu94_process.c
@src/spu94/spu94_state_internal.h
@src/spu94/spu94_adpcm.c
@include/spu94/spu94_vag.h
@include/spu94/spu94_adpcm.h
@include/spu94/spu94_gauss.h
@include/spu94/spu94_q15.h
@include/spu94/spu94.h
</context>

<interfaces>
<!-- Existing contracts the executor will call or extend. -->

From include/spu94/spu94_adpcm.h:
  spu94_adpcm_state { int16_t old; int16_t older; }
  uint8_t spu94_adpcm_decode_block(spu94_adpcm_state *state,
                                   const uint8_t block[16], int16_t out[28]);
  -- Returns block[1] (flag byte): bit 0=END, bit 1=REPEAT, bit 2=LOOP_START
  -- Updates state->old/older in-place for cross-block continuity
  -- SPU94_ADPCM_BLOCK_BYTES = 16, SPU94_ADPCM_BLOCK_SAMPLES = 28

  void spu94_adpcm_encode_block(spu94_adpcm_state *state,
                                const int16_t pcm[28], int loop_end,
                                uint8_t block_out[16]);
  -- Used ONLY at load time (spu94_sample_encode_to_ram). NEVER in voice tick.

From include/spu94/spu94_gauss.h:
  extern const int16_t spu94_gauss_table[512];
  -- Read-only. Shared across all voices (safe — it is a const in .rodata).
  -- Gaussian interpolation formula (from spu94_process.c lines 121-141):
       gi = (counter >> 4) & 0xFF;   // bits 4-11 of 16-bit counter
       wp = gauss_ring_pos;          // next-write index (0..3)
       s0 = gauss_ring[(wp+0)&3];    // oldest
       s1 = gauss_ring[(wp+1)&3];
       s2 = gauss_ring[(wp+2)&3];
       s3 = gauss_ring[(wp+3)&3];    // newest
       out = (gauss_table[0x0FF-gi]*s0 + gauss_table[0x1FF-gi]*s1
            + gauss_table[0x100+gi]*s2 + gauss_table[0x000+gi]*s3) >> 15

From include/spu94/spu94_q15.h:
  int16_t q15_mul_truncate(int16_t a, int16_t b);  -- (int32)(a*b) >> 15
  int16_t sat_s16(int32_t v);                       -- clamp to [-32768, 32767]
  int16_t q15_add_sat(int16_t a, int16_t b);

From include/spu94/spu94_vag.h:
  -- Existing flag constants cover only END and TERMINATOR.
  -- Phase 27 must ADD to this header:
       #define SPU94_VAG_FLAG_LOOP_START   0x04   /* bit 2: auto-latch loop addr */
       #define SPU94_VAG_FLAG_LOOP_END     0x01   /* bit 0: jump to loop_start   */
       #define SPU94_VAG_FLAG_LOOP_REPEAT  0x02   /* bit 1: repeat (set w/END)   */
       #define SPU94_VAG_FLAG_ONESHOT_END  0x01   /* END without REPEAT          */
  -- NOTE: SPU94_VAG_FLAG_END = 0x01 is ALREADY defined. The LOOP_END alias is the
     same bit — no redefinition, just a clarifying comment.

From include/spu94/spu94.h:
  SPU94_STATE_SIZE_MAX = 16384u
  SPU94_WORK_BUF_MAX_BYTES = 0x80000u  (512 KB reverb work buffer)
  spu94_result_t: SPU94_OK=0, SPU94_INVALID_ARG=6

Single-counter architecture (from spu94_process.c, lines 77-84):
  old_counter = voice_counter;
  voice_counter += pitch;                    // 16-bit addition, wraps naturally
  samples_consumed = (voice_counter >> 12) - (old_counter >> 12);
  if (voice_counter < old_counter) samples_consumed = 1;  // wrap guard
  voice_counter &= 0x0FFF;                   // keep fractional part only
  -- In spu94_voice_tick: counter is uint16_t inside spu94_voice_t (not in spu94_state).
  -- Pitch MUST be clamped to 0x3FFF before the add (C7 / VOICE-03).
  -- counter >> 12 gives integer sample advancement; (counter >> 4) & 0xFF gives Gauss index.
</interfaces>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Define spu94_voice_t struct and SPU RAM contract header</name>
  <files>include/spu94/spu94_spu_ram.h, include/spu94/spu94_voice.h, include/spu94/spu94_vag.h</files>
  <behavior>
    - spu94_voice_t contains exactly the fields listed below — no extras, no omissions
    - SPU94_SPU_RAM_BYTES macro equals 0x80000u (exactly SPU94_WORK_BUF_MAX_BYTES)
    - VAG header gains loop flag constants without redefining SPU94_VAG_FLAG_END
    - A C99 compile of the new headers with -Wall -Wextra produces zero warnings
  </behavior>
  <action>
Create include/spu94/spu94_spu_ram.h as a header-only file with:
  - include guard SPU94_SPU_RAM_H
  - #define SPU94_SPU_RAM_BYTES  0x80000u
  - A block comment explaining the memory layout: low addresses = voice ADPCM sample data;
    high addresses = reverb delay lines (anchored by mBASE); these are separate 512 KB
    allocations in v1.8, not a shared array — deliberate deviation from real hardware.

Add to include/spu94/spu94_vag.h (after the existing flag constants):
  #define SPU94_VAG_FLAG_LOOP_REPEAT  0x02  /* bit 1: keep playing after loop end */
  #define SPU94_VAG_FLAG_LOOP_START   0x04  /* bit 2: auto-latch loop start addr  */
  Do NOT redefine SPU94_VAG_FLAG_END (0x01). Add a comment clarifying that bit 0 is
  both "end" and "loop end jump trigger" — they are the same bit.

Create include/spu94/spu94_voice.h with:
  - Include guard SPU94_VOICE_H
  - Includes: spu94_adpcm.h, spu94_spu_ram.h, stdint.h
  - spu94_voice_t struct with these fields in this order:
      uint32_t  current_addr;       /* byte offset of current ADPCM block in voice RAM */
      uint32_t  sample_start_addr;  /* byte offset of sample start (for key-on reset) */
      uint16_t  pitch;              /* 0x0001..0x3FFF; 0x1000 = 44.1 kHz playback rate */
      uint16_t  pitch_counter;      /* 4.12 fixed-point; bits 12+: sample, bits 4-11: Gauss */
      spu94_adpcm_state adpcm_state;    /* filter history (old/older) across blocks */
      int16_t   decode_buf[28];     /* current decoded block (SPU94_ADPCM_BLOCK_SAMPLES) */
      uint8_t   decode_buf_pos;     /* next sample index in decode_buf (0..27) */
      uint8_t   has_block;          /* 1 = decode_buf is valid; 0 = need to decode next block */
      int16_t   gauss_ring[4];      /* last 4 decoded samples for Gaussian interpolation */
      uint8_t   gauss_ring_pos;     /* write head in gauss_ring (0..3) */
      int16_t   vol_l;              /* per-voice left volume (0..32767, unsigned semantics) */
      int16_t   vol_r;              /* per-voice right volume (0..32767, unsigned semantics) */
      uint8_t   active;             /* 1 = voice is playing; 0 = silent */
  - Function declarations:
      void spu94_voice_init(spu94_voice_t *v);
      void spu94_voice_key_on(spu94_voice_t *v, uint32_t start_addr,
                              uint16_t pitch, int16_t vol_l, int16_t vol_r);
      void spu94_voice_key_off(spu94_voice_t *v);
      void spu94_voice_tick(spu94_voice_t *v,
                            const uint8_t *voice_ram, uint32_t voice_ram_size,
                            int16_t *out_l, int16_t *out_r);

Struct field notes (all from ARCHITECTURE-v1.8.md and PITFALLS-v1.8.md):
  - vol_l/vol_r: declared as int16_t to allow phase inversion (S2), but Phase 27
    documents the unsigned semantics (0-32767) per VOICE-04. No clamping to positive
    values — negative = polarity flip, which is correct SPU behavior.
  - gauss_ring[4]: ISOLATED per voice — never references state->gauss_ring_l/r (C2).
  - adpcm_state: ISOLATED per voice — never references state->adpcm_state_l/r (C1).
  - has_block: gate flag used by the tick function (decode-before-interpolate order, S5).
  - pitch is stored for reference; pitch_counter accumulates the running fractional position.

Phase 27 defers: loop_start_addr, loop_adpcm_state, ADSR fields, reverb_on, endx.
These will be added to spu94_voice_t in Phases 28-29 without breaking the Phase 27 API.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && gcc -std=c99 -Wall -Wextra -I include -fsyntax-only include/spu94/spu94_voice.h include/spu94/spu94_spu_ram.h && echo "Headers compile clean"</automated>
  </verify>
  <done>All three headers exist, compile with no warnings under -Wall -Wextra, and the struct
contains exactly the listed fields. spu94_vag.h has LOOP_REPEAT and LOOP_START constants
without duplicate definitions.</done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Implement spu94_voice.c and spu94_sample_loader.c</name>
  <files>src/spu94/spu94_voice.c, include/spu94/spu94_sample_loader.h, src/spu94/spu94_sample_loader.c, tests/unit/voice/test_voice_tick.c, tests/unit/voice/test_sample_loader.c, src/spu94/CMakeLists.txt</files>
  <behavior>
    Voice tick behaviors (test these first as failing Unity tests, then implement):
    - spu94_voice_tick with active=0 writes out_l=0, out_r=0 regardless of voice RAM contents
    - spu94_voice_key_on sets active=1, resets pitch_counter=0, has_block=0, zeros gauss_ring,
      sets current_addr=start_addr, sets pitch (clamped to 0x3FFF), vol_l, vol_r
    - Pitch 0x0000 passed to key_on is treated as 0x1000 (same as existing coloration path)
    - Pitch value > 0x3FFF is clamped to 0x3FFF by key_on (VOICE-03 / C7)
    - With a one-block ADPCM sample in voice_ram, first call to tick decodes the block
      (has_block goes from 0 to 1) and returns a non-zero output if the encoded sample
      had non-silent content
    - With vol_l=0, vol_r=0x7FFF, out_l=0 and out_r=scaled non-zero for non-silent input
    - tick with voice_ram=NULL or voice_ram_size=0 clamps safely (writes 0, no crash)

    Sample loader behaviors (test these first, then implement):
    - spu94_sample_encode_to_ram with pcm=NULL returns -1
    - Encoding N samples writes ceil(N/28) * 16 bytes into ram_out (each block 16 bytes)
    - Encoding that would overflow ram_offset + encoded_size > ram_size returns -1
    - The last block's flag byte is 0x01 (SPU94_VAG_FLAG_END) when loop_enable=0
    - The last block's flag byte is 0x03 (END | REPEAT) when loop_enable=1
    - Round-trip: encode 28 int16 samples → decode with spu94_adpcm_decode_block →
      output differs from input by no more than the expected ADPCM quantization
      (not a bit-exact test — just confirms encode+decode produces plausible audio-range output)
    - ram_offset=0, ram_size=0 with num_samples=28 returns -1 (overflow guard)
  </behavior>
  <action>
Create src/spu94/spu94_voice.c:

  Includes: spu94/spu94_voice.h, spu94/spu94_gauss.h, spu94/spu94_q15.h, spu94/spu94_adpcm.h, stdint.h, stddef.h

  spu94_voice_init: memset(v, 0, sizeof(*v)); — zero all fields; active=0 by zero-init.

  spu94_voice_key_on:
    - If v == NULL, return immediately.
    - pitch = (pitch == 0) ? 0x1000 : pitch;
    - if (pitch > 0x3FFF) pitch = 0x3FFF;   /* C7: mandatory clamp */
    - Set v->pitch = pitch, v->pitch_counter = 0, v->current_addr = start_addr,
      v->sample_start_addr = start_addr, v->vol_l = vol_l, v->vol_r = vol_r.
    - Zero v->adpcm_state.old, v->adpcm_state.older.
    - Zero v->gauss_ring[0..3], v->gauss_ring_pos = 0.
    - v->has_block = 0, v->decode_buf_pos = 0.
    - v->active = 1.

  spu94_voice_key_off:
    - If v == NULL, return. v->active = 0.
    - (Phase 28 will replace this with ADSR release; for now, immediate silence.)

  spu94_voice_tick:
    - If v == NULL or out_l == NULL or out_r == NULL, return.
    - If v->active == 0: *out_l = 0; *out_r = 0; return.
    - If voice_ram == NULL or voice_ram_size == 0: *out_l = 0; *out_r = 0; return.

    Processing order (S5 — decode before interpolate, then advance counter):

    STEP 1 — Decode block if needed:
      if (!v->has_block) {
        /* Bounds check: refuse to read past voice_ram */
        if (v->current_addr + SPU94_ADPCM_BLOCK_BYTES > voice_ram_size) {
          v->active = 0; *out_l = 0; *out_r = 0; return;
        }
        spu94_adpcm_decode_block(&v->adpcm_state,
            voice_ram + v->current_addr, v->decode_buf);
        /* Loop/end flag handling deferred to Phase 29.
         * For now: advance addr by 16 bytes (M6). */
        v->current_addr += SPU94_ADPCM_BLOCK_BYTES;
        v->decode_buf_pos = 0;
        v->has_block = 1;
      }

    STEP 2 — Gaussian interpolation:
      gi = (v->pitch_counter >> 4) & 0xFF;
      wp = v->gauss_ring_pos;
      s0..s3 from v->gauss_ring[(wp+0..3)&3]
      interpolated = (gauss_table[0x0FF-gi]*s0 + gauss_table[0x1FF-gi]*s1
                    + gauss_table[0x100+gi]*s2 + gauss_table[0x000+gi]*s3) >> 15
      — Exact formula from spu94_process.c lines 121-141. No deviation.

    STEP 3 — Apply per-voice volume:
      *out_l = q15_mul_truncate((int16_t)sat_s16(interpolated), v->vol_l);
      *out_r = q15_mul_truncate((int16_t)sat_s16(interpolated), v->vol_r);

    STEP 4 — Advance pitch counter and push sample into ring:
      uint16_t old_ctr = v->pitch_counter;
      uint16_t effective_pitch = (v->pitch > 0x3FFF) ? 0x3FFF : v->pitch; /* C7 */
      v->pitch_counter += effective_pitch;
      uint16_t samples_consumed = (v->pitch_counter >> 12) - (old_ctr >> 12);
      if (v->pitch_counter < old_ctr) samples_consumed = 1;  /* 16-bit wrap guard */
      v->pitch_counter &= 0x0FFF;

      for each sample consumed (usually 0 or 1, occasionally 2-4 at high pitch):
        push v->decode_buf[v->decode_buf_pos] into v->gauss_ring:
          v->gauss_ring[v->gauss_ring_pos] = v->decode_buf[v->decode_buf_pos];
          v->gauss_ring_pos = (v->gauss_ring_pos + 1) & 3;
          v->decode_buf_pos++;
          if (v->decode_buf_pos >= SPU94_ADPCM_BLOCK_SAMPLES):
            v->has_block = 0;   /* trigger decode of next block on next tick */
            v->decode_buf_pos = 0;
            /* current_addr already advanced at decode time */

    Phase 27 does NOT implement loop flags — when the sample runs out of blocks
    (current_addr overflows voice_ram_size), the tick detects the bounds failure and
    sets active=0. Loop logic is Phase 29.

Create include/spu94/spu94_sample_loader.h:
  - Include guard SPU94_SAMPLE_LOADER_H
  - Include: stdint.h, spu94/spu94_spu_ram.h
  - Declaration:
      /* Encode a mono int16 PCM buffer into ADPCM blocks and write into voice_ram.
       * pcm: input samples (mono; stereo = two calls with different ram_offset).
       * num_samples: number of int16 samples in pcm.
       * voice_ram: destination byte array (caller-owned, SPU94_SPU_RAM_BYTES).
       * ram_offset: byte offset in voice_ram where encoded blocks begin.
       * ram_size: total size of voice_ram in bytes (bounds check limit).
       * loop_enable: if 1, final block flag = 0x03 (END|REPEAT); if 0, flag = 0x01 (END).
       * Returns: number of bytes written, or -1 on error (NULL input, overflow). */
      int32_t spu94_sample_encode_to_ram(
          const int16_t *pcm, uint32_t num_samples,
          uint8_t *voice_ram, uint32_t ram_offset, uint32_t ram_size,
          int loop_enable);

Create src/spu94/spu94_sample_loader.c:
  Includes: spu94/spu94_sample_loader.h, spu94/spu94_adpcm.h, spu94/spu94_vag.h, stdint.h, stddef.h, string.h

  spu94_sample_encode_to_ram:
    - Return -1 if pcm == NULL or voice_ram == NULL or num_samples == 0.
    - Compute num_blocks = (num_samples + 27) / 28.  (ceiling division)
    - Compute bytes_needed = num_blocks * SPU94_ADPCM_BLOCK_BYTES.
    - Return -1 if ram_offset + bytes_needed > ram_size.  (RAM-03 bounds check)
    - Initialize spu94_adpcm_state encode_state = {0, 0}.
    - For block b = 0 .. num_blocks-1:
        int16_t block_pcm[28] = {0};  /* zero-pad partial last block */
        uint32_t src_off = (uint32_t)b * 28;
        uint32_t count = (src_off + 28 <= num_samples) ? 28 : (num_samples - src_off);
        memcpy(block_pcm, pcm + src_off, count * sizeof(int16_t));
        int is_last = (b == num_blocks - 1);
        int loop_end_flag = is_last ? 1 : 0;
        uint8_t *dest = voice_ram + ram_offset + (uint32_t)b * SPU94_ADPCM_BLOCK_BYTES;
        spu94_adpcm_encode_block(&encode_state, block_pcm, loop_end_flag, dest);
        if (is_last && loop_enable) {
          dest[1] |= SPU94_VAG_FLAG_LOOP_REPEAT;  /* set bit 1 alongside bit 0 */
        }
    - Return (int32_t)bytes_needed.

  RT-safety note: spu94_sample_encode_to_ram is NEVER called from spu94_process or
  spu94_voice_tick. It is a load-time function only (C1, S6). No heap, no syscalls.

Create tests/unit/voice/test_voice_tick.c and tests/unit/voice/test_sample_loader.c as
Unity test files following the existing pattern in tests/unit/process/test_process_adpcm.c.
Each test file includes unity.h and uses TEST_ASSERT_* macros. Test functions are grouped
by behavior (see <behavior> block above). The test executables are registered in
CMakeLists.txt with add_executable + target_link_libraries(spu94_lib) + add_test.

Update src/spu94/CMakeLists.txt:
  - Add spu94_voice.c and spu94_sample_loader.c to the spu94_obj OBJECT target.
  - DO NOT modify anything else in that file.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake -B build -DCMAKE_BUILD_TYPE=Release -S . -Wno-dev 2>&1 | tail -3 && cmake --build build --config Release -- -j4 2>&1 | grep -E "error:|warning:|Built target" | tail -20 && ctest --test-dir build -R "test_voice" -V 2>&1 | tail -30</automated>
  </verify>
  <done>
- spu94_voice.c and spu94_sample_loader.c compile with zero errors.
- All Unity tests in test_voice_tick and test_sample_loader pass.
- No existing tests regress (the overall ctest suite still passes).
- nm -u build/libspu94.a (or equivalent object) does NOT show malloc, fopen, printf,
  pthread_mutex as undefined symbols in spu94_voice.c or spu94_sample_loader.c.
  </done>
</task>

<task type="auto">
  <name>Task 3: Wire voice 0 into spu94_process and verify end-to-end audio output</name>
  <files>src/spu94/spu94_process.c</files>
  <action>
Add a voice_engine_enabled flag and a spu94_voice_mixer_t-lite wiring to spu94_process.
For Phase 27, ONLY voice 0 is wired. The 24-voice loop happens in Phase 30.

The approach: the voice engine needs its own 512 KB voice RAM and 24 voice structs. These
live OUTSIDE spu94_state (STACK-v1.8.md decision; avoids growing the struct past
SPU94_STATE_SIZE_MAX). For Phase 27, a file-scope static arrangement in spu94_process.c
is acceptable as a scaffolding step. Phase 30 will migrate this to a caller-allocated
spu94_voice_mixer_t struct.

Changes to src/spu94/spu94_process.c:

At the top of the file (after existing includes), add:
  #include <spu94/spu94_voice.h>
  #include <spu94/spu94_spu_ram.h>
  #include <string.h>

Add file-scope statics (above spu94_process):
  /* v1.8 voice engine scaffolding — Phase 27 (voice 0 only).
   * voice_ram is a separate 512 KB buffer from state->work_buf (C6 / RAM-01).
   * Phase 30 will migrate to a caller-allocated spu94_voice_mixer_t. */
  static uint8_t  s_voice_ram[SPU94_SPU_RAM_BYTES];
  static spu94_voice_t s_voices[24];   /* VOICE-06: 24 isolated structs */
  static uint8_t  s_voice_engine_init = 0;
  static uint8_t  s_voice_engine_enabled = 0;

Add two new public functions (declared with extern in the .c file; not yet in spu94.h —
Phase 31 will formalize the public API):

  /* Load a pre-encoded ADPCM block sequence into voice RAM.
   * addr: byte offset within s_voice_ram. source_size: bytes to copy.
   * Returns SPU94_OK or SPU94_INVALID_ARG if addr+source_size > SPU94_SPU_RAM_BYTES. */
  spu94_result_t spu94_voice_load_sample_raw(
      uint32_t addr, const uint8_t *source, uint32_t source_size)
  {
    if (!source || addr + source_size > SPU94_SPU_RAM_BYTES)
      return SPU94_INVALID_ARG;
    memcpy(s_voice_ram + addr, source, source_size);
    return SPU94_OK;
  }

  /* Key on voice 0 with the given start address, pitch, and volume.
   * Enables the voice engine if not already enabled. */
  spu94_result_t spu94_voice0_key_on(
      uint32_t start_addr, uint16_t pitch, int16_t vol_l, int16_t vol_r)
  {
    if (!s_voice_engine_init) {
      for (int i = 0; i < 24; i++) spu94_voice_init(&s_voices[i]);
      s_voice_engine_init = 1;
    }
    spu94_voice_key_on(&s_voices[0], start_addr, pitch, vol_l, vol_r);
    s_voice_engine_enabled = 1;
    return SPU94_OK;
  }

  /* Key off voice 0. */
  spu94_result_t spu94_voice0_key_off(void)
  {
    spu94_voice_key_off(&s_voices[0]);
    return SPU94_OK;
  }

Inside spu94_process, at the start of the per-sample loop (BEFORE step 1, input gain),
add the voice engine tick:

  /* v1.8 Voice Engine: voice 0 only (Phase 27 scaffolding).
   * Voice output is mixed into the patina bus slot for Phase 27 — this routes
   * it through the existing reverb send path without touching the dry bus logic.
   * Phase 30 will replace this with the full 24-voice mixer and separate send buses. */
  if (s_voice_engine_enabled) {
    int16_t vl = 0, vr = 0;
    spu94_voice_tick(&s_voices[0], s_voice_ram, SPU94_SPU_RAM_BYTES, &vl, &vr);
    /* Inject voice output into the patina bus path by pre-setting patina_l/r.
     * Step 2 (ADPCM coloration) is still active when adpcm_enabled=1 — the
     * voice engine and coloration bus are independent (MIX-06). When
     * adpcm_enabled=0, patina_l/r would otherwise equal l/r (passthrough);
     * override it with the voice output here. */
    patina_l = vl;
    patina_r = vr;
    /* Note: patina_l/r are declared earlier in the function. Move the
     * declaration to before the voice engine block if the compiler objects. */
  }

IMPORTANT — code hygiene: the existing `patina_l`, `patina_r` declarations at line 58
must be hoisted above the new voice engine block so they are in scope. Move:
  int16_t patina_l, patina_r;
to immediately after the per-sample `l` and `r` declarations (before the voice engine
block). Initialize them to l/r (passthrough default) so the existing adpcm_enabled
branch can safely overwrite them.

Rationale for the patina injection approach (over adding a 4th bus):
  - Zero new mixer bus complexity in Phase 27
  - The reverb send path (patina_send fader) already exists — voice audio routes to
    reverb correctly without new wiring
  - Phase 30 replaces this injection with a proper voice_dry_l/r → dry bus + voice
    reverb send → reverb send path, matching the PS1 hardware topology exactly
  - The coloration bus and voice engine can coexist: when adpcm_enabled=1 AND
    s_voice_engine_enabled=1, the patina_l/r from the coloration path will be
    overwritten by the voice engine output. Document this in a comment (MIX-06 note).

After making changes, run: cmake --build build && ctest --test-dir build to confirm
the full existing test suite still passes. The voice engine path is inactive by default
(s_voice_engine_enabled=0) so no existing test should be affected.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build --config Release -- -j4 2>&1 | grep -E "^.*error:" | head -10 && echo "BUILD_DONE" && ctest --test-dir build --output-on-failure 2>&1 | tail -20</automated>
  </verify>
  <done>
- spu94_process.c compiles without errors or new warnings.
- All existing ctest targets pass (zero regressions).
- The functions spu94_voice_load_sample_raw, spu94_voice0_key_on, spu94_voice0_key_off
  are callable from C code without linker errors.
- When s_voice_engine_enabled=0 (default), spu94_process behavior is bit-identical to
  the Phase 26 baseline.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| voice_ram read in audio callback | Encoded ADPCM blocks come from load-time caller data; size is bounds-checked at decode time |
| pitch value from caller | Arbitrary uint16_t; must be clamped to 0x3FFF before use (C7) |
| ram_offset + source_size arithmetic | uint32_t addition; overflow check required before memcpy |
| voice_ram vs reverb work_buf | Two separate allocations; no pointer aliasing possible |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-27-01 | Tampering | spu94_voice_tick: out-of-bounds read past voice_ram | mitigate | Bounds check at block decode: if current_addr + 16 > voice_ram_size, set active=0 and return silence |
| T-27-02 | Tampering | spu94_sample_encode_to_ram: integer overflow in ram_offset + bytes_needed | mitigate | Check as uint64_t intermediate or check bytes_needed <= ram_size - ram_offset before memcpy |
| T-27-03 | Denial | Pitch counter overflow producing >4 samples consumed per tick | mitigate | Pitch clamped to 0x3FFF at key_on and re-checked in tick; samples_consumed bounded by hardware max |
| T-27-04 | Information | voice_ram static buffer contains stale data from previous sample loads | accept | Intentional — caller manages voice RAM layout; spu94_voice_load_sample_raw overwrites at caller-specified offset |
| T-27-SC | Tampering | npm/pip/cargo installs | accept | No new package manager dependencies in this phase; pure C99 |
</threat_model>

<verification>
After all three tasks complete:

1. Build passes with zero errors:
   cmake --build build --config Release -- -j4

2. All tests pass including new voice tests:
   ctest --test-dir build --output-on-failure

3. RT-safety gate — spu94_voice.c and spu94_sample_loader.c must not reference malloc/free/fopen/printf:
   nm -u build/src/spu94/CMakeFiles/spu94_obj.dir/spu94_voice.c.o 2>/dev/null | grep -E "malloc|free|fopen|printf" | grep -v "^$"
   (must produce no output)

4. Pitch clamp: a test in test_voice_tick.c calls spu94_voice_key_on with pitch=0x4000
   and confirms the stored pitch (or effective behavior) is 0x3FFF.

5. Voice RAM isolation: the s_voice_ram and state->work_buf are distinct allocations.
   Verify in test_sample_loader.c by encoding into s_voice_ram at offset 0 and confirming
   the reverb work_buf (a separate caller-allocated buffer passed to spu94_init) is
   unchanged — byte-compare a region of work_buf before and after the encode call.

6. Existing golden tests pass unchanged:
   ctest --test-dir build -R "golden" --output-on-failure 2>/dev/null || echo "no golden targets"
</verification>

<success_criteria>
Mapping to ROADMAP.md Phase 27 success criteria:

1. "A WAV file loaded into the standalone encodes to ADPCM and stores in the dedicated
   512 KB voice RAM buffer without touching the reverb work buffer"
   — Verified by Task 2 tests: spu94_sample_encode_to_ram writes to voice_ram argument;
     s_voice_ram is a static separate from state->work_buf (C6 / RAM-01, RAM-02).

2. "Calling voice key-on produces audible audio output at the specified pitch, using the
   existing 4-tap Gaussian interpolator"
   — Verified by Task 2 test: after key_on and one block decode, voice tick returns
     non-zero samples for non-silent ADPCM input. Task 3 wires this into spu94_process.

3. "Setting the pitch register to 0x3FFF clamps to hardware maximum; values above are
   rejected, not wrapped"
   — Verified by Task 2 test on spu94_voice_key_on with pitch=0x4000 → stored pitch=0x3FFF.

4. "Per-voice L/R volume registers scale the output amplitude across the 0-32767 unsigned
   range"
   — Verified by Task 2 test: vol_l=0 → out_l=0; vol_r=0x7FFF → out_r scales by Q15 mul.

5. "Loading a sample whose encoded size would exceed the 512 KB voice RAM boundary is
   rejected with a bounds error"
   — Verified by Task 2 test: spu94_sample_encode_to_ram with ram_size=0 returns -1;
     spu94_voice_load_sample_raw with overflowing size returns SPU94_INVALID_ARG (RAM-03).
</success_criteria>

<output>
When complete, create: .planning/phases/27-single-voice-playback/27-01-SUMMARY.md

The summary must record:
- Which files were created vs modified
- Actual line counts (not estimates)
- Any field names or function signatures that differed from this plan
- Whether the patina injection approach was kept or replaced
- Any pitfall-prevention comments added to the code (list C1, C2, C6, C7 explicitly)
- Confirmation that spu94_state was NOT grown (SPU94_STATE_SIZE_MAX still passes)
- ctest pass count before and after
</output>
