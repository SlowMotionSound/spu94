---
phase: 29-loop-mechanics
plan: 01
type: execute
wave: 1
depends_on: []
files_modified:
  - include/spu94/spu94_voice.h
  - src/spu94/spu94_voice.c
  - tests/unit/voice/test_voice_tick.c
autonomous: true
requirements: [LOOP-01, LOOP-02, LOOP-03, LOOP-04, LOOP-05]

must_haves:
  truths:
    - "A sample whose Loop-Start block is flagged causes the loop_addr field to be latched to that block's address, and playback jumps back to it when the Loop-End block is reached"
    - "After a loop jump, the ADPCM filter state (old/older) matches the snapshot taken after the loop-start block was first decoded — no filter-seam click on the second and subsequent loop iterations"
    - "A sample flagged loop-end without the repeat bit plays to the end of that block, then the voice outputs silence and endx is set"
    - "spu94_voice_get_endx returns 1 for the voice that reached loop-end; KON on that voice clears endx"
  artifacts:
    - path: "include/spu94/spu94_voice.h"
      provides: "loop_addr, loop_adpcm_old, loop_adpcm_older, endx fields in spu94_voice_t; spu94_voice_get_endx declaration"
      contains: "loop_addr"
    - path: "src/spu94/spu94_voice.c"
      provides: "flag-byte parsing in spu94_voice_tick; latch, jump, one-shot, and ENDX logic; spu94_voice_get_endx implementation"
      contains: "LOOP_END"
    - path: "tests/unit/voice/test_voice_tick.c"
      provides: "four loop-mechanics test cases"
      contains: "test_loop"
  key_links:
    - from: "spu94_voice_tick STEP 1 (block decode)"
      to: "flag byte from spu94_adpcm_decode_block return value"
      via: "flag_byte local variable; checked immediately after decode call"
      pattern: "flag_byte.*SPU94_VAG_LOOP"
    - from: "loop-end+repeat branch"
      to: "voice->loop_adpcm_old / loop_adpcm_older"
      via: "restore into voice->adpcm_state before next decode"
      pattern: "loop_adpcm_old"
---

<objective>
Add PS1 loop-flag mechanics to the per-voice tick so that ADPCM block header flags drive
loop-start address auto-latching, filter-state snapshot and restoration at the loop boundary,
one-shot silence on end-without-repeat, and the ENDX status bit.

Purpose: This is the final building block before Phase 30 (24-voice polyphony). Without loop
mechanics, every voice runs off the end of voice RAM and goes silent — fine for a test tone,
unusable for any real instrument sound. With this phase complete, a looping ADPCM patch will
sustain indefinitely and a one-shot percussion hit will play once and stop cleanly.

Output:
- spu94_voice_t gains four fields: loop_addr, loop_adpcm_old, loop_adpcm_older, endx
- spu94_voice_tick interprets the flag byte from every decoded block (flag-parse, latch, jump,
  one-shot, ENDX)
- spu94_voice_key_on clears endx (M3 — ENDX cleared by KON, not KOFF)
- spu94_voice_get_endx exposes the per-voice ENDX status bit
- Four new unit tests prove each flag-combination path
</objective>

<execution_context>
@$HOME/.claude/get-shit-done/workflows/execute-plan.md
@$HOME/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@.planning/ROADMAP.md
@.planning/REQUIREMENTS.md
@.planning/phases/27-single-voice-playback/27-PLAN-SUMMARY.md
@.planning/phases/28-adsr-envelope/28-PLAN-SUMMARY.md

<interfaces>
<!-- Key types and contracts the executor needs. Extracted from codebase. -->
<!-- Executor should use these directly — no codebase exploration needed. -->

From include/spu94/spu94_voice.h (current, Phase 28 state):
```c
typedef struct {
    uint32_t  current_addr;
    uint32_t  sample_start_addr;
    uint16_t  pitch;
    uint16_t  pitch_counter;
    spu94_adpcm_state adpcm_state;   /* .old and .older are int16_t */
    int16_t   decode_buf[28];
    uint8_t   decode_buf_pos;
    uint8_t   has_block;
    int16_t   gauss_ring[4];
    uint8_t   gauss_ring_pos;
    int16_t   vol_l;
    int16_t   vol_r;
    spu94_adsr_state_t adsr;
    uint8_t   active;
    /* Phase 29 adds: loop_addr, loop_adpcm_old, loop_adpcm_older, endx */
} spu94_voice_t;
```

From include/spu94/spu94_adpcm.h — spu94_adpcm_decode_block signature:
```c
/* Decodes one 16-byte ADPCM block. Updates state->old and state->older in place.
 * Returns the flag byte (block byte 1) so callers can act on loop flags.
 * Existing callers in the coloration bus discard the return value. */
uint8_t spu94_adpcm_decode_block(spu94_adpcm_state *state,
                                  const uint8_t *block,
                                  int16_t *out_samples);
```

From include/spu94/spu94_vag.h — loop flag constants (added in Phase 27):
```c
#define SPU94_VAG_LOOP_END     (1u << 0)   /* bit 0: loop end */
#define SPU94_VAG_LOOP_REPEAT  (1u << 1)   /* bit 1: repeat on loop end */
#define SPU94_VAG_LOOP_START   (1u << 2)   /* bit 2: latch as loop start */
```

From src/spu94/spu94_voice.c — STEP 1, block decode section (where loop logic goes):
```c
if (!v->has_block) {
    if (v->current_addr + SPU94_ADPCM_BLOCK_BYTES > voice_ram_size) {
        v->active = 0;  *out_l = 0;  *out_r = 0;  return;
    }
    /* C1: decode-only from RAM, no encode call */
    spu94_adpcm_decode_block(&v->adpcm_state,
        voice_ram + v->current_addr, v->decode_buf);
    /* M6: advance address by 16 bytes */
    v->current_addr += SPU94_ADPCM_BLOCK_BYTES;
    v->decode_buf_pos = 0;
    v->has_block = 1;
    /* Phase 29: flag-byte parse goes here */
}
```

From include/spu94/spu94_adsr.h — how to zero ADSR level for one-shot:
```c
/* Direct field access: v->adsr.level = 0; forces silence. */
/* For one-shot, DO NOT call spu94_adsr_key_off() — that would enter release
 * and the voice would keep ticking. Instead force level=0 directly and let
 * the next ADSR tick observe level=0 in a non-releasing phase to stay silent.
 * Actually: set v->adsr.phase = ADSR_OFF and v->adsr.level = 0 — this is the
 * same terminal state that a completed release reaches, so the tick's existing
 * ADSR_OFF check silences the voice on the next tick without additional changes. */
typedef enum { ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE, ADSR_OFF } spu94_adsr_phase_t;
```

Key Phase 28 note: voice silencing path in spu94_voice_tick is:
```c
int16_t adsr_level = spu94_adsr_tick(&v->adsr);
if (v->adsr.phase == ADSR_OFF && v->adsr.enabled) {
    v->active = 0;  *out_l = 0;  *out_r = 0;  return;
}
```
One-shot termination must reach this same ADSR_OFF state. Setting
v->adsr.phase = ADSR_OFF and v->adsr.level = 0 achieves it on the next tick.
If ADSR is disabled (enabled=0), one-shot should set v->active = 0 directly.
</interfaces>
</context>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Add loop fields to spu94_voice_t and update key_on / get_endx API</name>
  <files>include/spu94/spu94_voice.h, src/spu94/spu94_voice.c</files>
  <behavior>
    - spu94_voice_init zeroes all four new fields (loop_addr=0, loop_adpcm_old=0, loop_adpcm_older=0, endx=0)
    - spu94_voice_key_on clears endx=0 (M3: ENDX cleared on KON, not KOFF)
    - spu94_voice_get_endx(v) returns v->endx (0 or 1)
    - No logic changes in spu94_voice_tick yet — that is Task 2
  </behavior>
  <action>
Add four fields to spu94_voice_t in include/spu94/spu94_voice.h, between the adsr field and the
active field. Add a comment citing LOOP-01..05 and referencing C4/C5 pitfalls:

    uint32_t  loop_addr;          /* byte offset latched on Loop-Start flag (LOOP-02) */
    int16_t   loop_adpcm_old;     /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    int16_t   loop_adpcm_older;   /* ADPCM filter history snapshot at loop start (LOOP-03 / C5) */
    uint8_t   endx;               /* set on Loop-End; cleared on KON (LOOP-05 / M3) */

Add the declaration for the new query function after the existing key_off declaration:

    /* Returns 1 if this voice has reached a loop-end block since the last KON,
     * 0 otherwise. Cleared on KON. Set on Loop-End. NOT cleared by KOFF (M3). */
    uint8_t spu94_voice_get_endx(const spu94_voice_t *v);

In src/spu94/spu94_voice.c:
- spu94_voice_init already calls memset(v, 0, ...) — the four new fields zero automatically.
  No change needed, but add a comment noting that loop_addr=0, endx=0 are intentional zero-init.
- In spu94_voice_key_on: after the line `spu94_adsr_key_on(&v->adsr);`, add `v->endx = 0;`
  with comment "M3: ENDX cleared on KON, not KOFF".
- Implement spu94_voice_get_endx at the bottom of the file:
    uint8_t spu94_voice_get_endx(const spu94_voice_t *v) {
        if (v == NULL) return 0;
        return v->endx;
    }

Compile check: `cmake --build build --target spu94_voice_lib 2>&1 | grep -E "error:|warning:"` must be empty.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build --target spu94_voice_lib 2>&1 | grep -c "error:" | grep -x "0" && echo "COMPILE OK"</automated>
  </verify>
  <done>
    spu94_voice_t has loop_addr, loop_adpcm_old, loop_adpcm_older, endx fields.
    spu94_voice_get_endx is declared in the header and implemented.
    spu94_voice_key_on clears endx.
    Build is clean (zero errors, zero warnings).
    All 127 pre-existing tests still pass: `ctest --test-dir build -R "voice|adsr|sample" --output-on-failure`
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Implement loop flag logic in spu94_voice_tick and add unit tests</name>
  <files>src/spu94/spu94_voice.c, tests/unit/voice/test_voice_tick.c</files>
  <behavior>
    Test A — Loop-Start latch:
      Build a minimal 2-block ADPCM stream where block 0 has Loop-Start flag (bit 2) and no other flags.
      Tick through block 0. After block 0 is decoded, voice->loop_addr equals the address of block 0
      (not block 1 — the address before the current_addr increment).
      voice->loop_adpcm_old and loop_adpcm_older equal the adpcm_state.old/older AFTER block 0 decode (C5).

    Test B — Loop-End+Repeat jump:
      Build a 2-block stream: block 0 has Loop-Start (bit 2), block 1 has Loop-End+Repeat (bits 0+1).
      Tick through both blocks. When block 1 is decoded, the voice should jump: current_addr is reset
      to loop_addr (start of block 0). adpcm_state.old and .older are restored from loop_adpcm_old/older.
      Voice remains active=1. endx=1 after the jump (Loop-End always sets ENDX, repeat or not).

    Test C — One-shot (Loop-End without Repeat):
      Build a 1-block stream: block 0 has Loop-End flag (bit 0 set) but NOT Loop-Repeat (bit 1 clear).
      Tick through block 0. After block 0 is decoded, voice should silence:
      - If ADSR enabled: adsr.phase == ADSR_OFF and adsr.level == 0; active goes to 0 on the following
        tick when the ADSR_OFF check fires. endx=1 immediately when the flag is parsed.
      - If ADSR disabled: active=0 immediately. endx=1.

    Test D — ENDX cleared by KON:
      Set voice->endx = 1 manually. Call spu94_voice_key_on. Verify spu94_voice_get_endx returns 0.
  </behavior>
  <action>
In src/spu94/spu94_voice.c, modify the STEP 1 block-decode section.

Change the spu94_adpcm_decode_block call to capture its return value:

    /* LOOP-01: capture flag byte from block header byte 1 */
    uint8_t flag_byte = spu94_adpcm_decode_block(&v->adpcm_state,
        voice_ram + v->current_addr, v->decode_buf);

The current_addr advance and has_block=1 lines stay in the same positions.

After has_block=1, add the flag-byte dispatch block. Write a comment citing each requirement and pitfall:

    /* --- Loop flag dispatch (LOOP-01 through LOOP-05; C4, C5, S3) --- */

    /* LOOP-02 / C4: Loop-Start — latch loop_addr and snapshot filter state.
     * The address latched is the address of THIS block (before the += 16 advance).
     * The filter state snapshot is taken AFTER decode — this is the state that will
     * be valid at the start of the next time the loop returns here (C5). */
    if (flag_byte & SPU94_VAG_LOOP_START) {
        v->loop_addr = v->current_addr - SPU94_ADPCM_BLOCK_BYTES;
        v->loop_adpcm_old   = v->adpcm_state.old;
        v->loop_adpcm_older = v->adpcm_state.older;
    }

    /* LOOP-01 / C4: Loop-End — set ENDX regardless of repeat bit */
    if (flag_byte & SPU94_VAG_LOOP_END) {
        v->endx = 1;  /* LOOP-05: ENDX set; cleared only by KON (M3) */

        if (flag_byte & SPU94_VAG_LOOP_REPEAT) {
            /* LOOP-03 / C5: Jump to loop_addr and restore filter state snapshot.
             * S3: do NOT clear the Gaussian ring — let old samples be pushed out
             * naturally over the next 3 ticks as new samples are decoded. */
            v->current_addr = v->loop_addr;
            v->adpcm_state.old   = v->loop_adpcm_old;
            v->adpcm_state.older = v->loop_adpcm_older;
            /* Force decode of the loop-start block on the next decode trigger.
             * has_block is still 1 for the current block; current_addr now points
             * to the loop-start block which will be decoded when has_block goes 0. */
        } else {
            /* LOOP-04: One-shot — loop end without repeat; mute the voice.
             * C4: the voice stays nominally "active" at zero level; it does NOT
             * get deactivated here. KON must be issued to reuse the voice slot. */
            if (v->adsr.enabled) {
                /* Drive ADSR to terminal state — same as a completed release.
                 * The existing ADSR_OFF check in Step 2.5 will zero output and
                 * clear active on the next tick. */
                v->adsr.phase = ADSR_OFF;
                v->adsr.level = 0;
            } else {
                v->active = 0;
            }
        }
    }

In tests/unit/voice/test_voice_tick.c, add four test functions at the end of the file
(before the test runner / main). Each test builds a minimal encoded-by-hand ADPCM stream:
a 16-byte block where byte 0 = 0x00 (shift=0, filter=0 — simplest filter, no prediction),
bytes 2-15 = all 0x00 (28 zero-valued samples), and byte 1 = the flag byte being tested.

Use the Unity framework (already in this test file) for assertions.

Test A: test_loop_start_latches_address
  - Build 2-block stream. Block 0: byte1=SPU94_VAG_LOOP_START, silence samples.
    Block 1: byte1=0, silence samples.
  - key_on the voice with start_addr=0.
  - Tick once (block 0 decodes). Assert voice.loop_addr == 0 (the address of block 0).
  - Assert voice.loop_adpcm_old == 0 (filter 0 with all-zero samples leaves history at 0).

Test B: test_loop_end_repeat_jumps_to_loop_addr
  - Build 2-block stream. Block 0 at offset 0: byte1=SPU94_VAG_LOOP_START.
    Block 1 at offset 16: byte1=(SPU94_VAG_LOOP_END | SPU94_VAG_LOOP_REPEAT).
  - key_on at start_addr=0 with ADSR disabled.
  - Tick to completion of block 0 (28 ticks at pitch 0x1000), then tick enough to decode block 1.
  - After block 1 is decoded, assert voice.current_addr == 0 (jumped back to loop_addr).
  - Assert voice.active == 1.
  - Assert spu94_voice_get_endx(&voice) == 1.

Test C: test_one_shot_silences_voice
  - Build 1-block stream at offset 0. Block 0: byte1=SPU94_VAG_LOOP_END (bit 1 clear).
  - key_on with ADSR disabled.
  - Tick enough for block 0 to decode.
  - Assert spu94_voice_get_endx(&voice) == 1.
  - Assert voice.active == 0 (immediate silence when ADSR disabled).

Test D: test_endx_cleared_by_key_on
  - Manually set voice.endx = 1.
  - Call spu94_voice_key_on.
  - Assert spu94_voice_get_endx(&voice) == 0.

Register all four new tests in the test runner (UnityBegin / RUN_TEST / UnityEnd pattern
already established in the file).
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build 2>&1 | grep -c "error:" | grep -x "0" && ctest --test-dir build -R "voice_tick_unit" --output-on-failure</automated>
  </verify>
  <done>
    All four loop-mechanics tests pass: test_loop_start_latches_address, test_loop_end_repeat_jumps_to_loop_addr, test_one_shot_silences_voice, test_endx_cleared_by_key_on.
    All pre-existing 127 tests still pass: `ctest --test-dir build --output-on-failure`.
    Requirements LOOP-01 through LOOP-05 are satisfied:
      LOOP-01: flag_byte from spu94_adpcm_decode_block is read and dispatched on every block decode.
      LOOP-02: Loop-Start flag latches loop_addr to current block address.
      LOOP-03: Loop-Start snapshots adpcm filter state; Loop-End+Repeat restores it before next decode.
      LOOP-04: Loop-End without Repeat forces ADSR to ADSR_OFF (or active=0 if ADSR disabled).
      LOOP-05: endx set on Loop-End, queried via spu94_voice_get_endx, cleared by KON.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| voice_ram → spu94_voice_tick | Flag byte is read from caller-provided RAM; malformed block data could set arbitrary flag combinations |
| caller → spu94_voice_get_endx | Read-only query; no trust boundary concern |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-29-01 | Tampering | flag_byte dispatch | accept | All three flag bits are independent booleans; no integer-range concern; worst-case is all flags simultaneously set, which is handled: Loop-Start latches, then Loop-End fires. Not an exploitable path in a standalone audio plugin. |
| T-29-02 | DoS | Loop-End+Repeat with loop_addr=0 | accept | If a sample never had a Loop-Start block, loop_addr defaults to 0 (voice start). Jump to 0 loops the whole sample. Unexpected but not a crash; audio plugin context only. |
| T-29-03 | Tampering | loop_addr out of voice_ram_size | mitigate | After restoring loop_addr on jump, the existing bounds check at the top of the has_block path (`current_addr + BLOCK_BYTES > voice_ram_size → active=0`) will catch an out-of-range loop_addr on the next decode. No additional check needed because the latch path records only addresses that already passed the bounds check at the time they were decoded. |
| T-29-SC | Tampering | npm/pip/cargo installs | accept | No new package-manager installs in this phase — C-only changes to existing source files |
</threat_model>

<verification>
Full test suite must pass after both tasks:

```
cd "/home/ubuntu-studio/Desktop/PSX Reverb"
ctest --test-dir build --output-on-failure
```

All pre-existing 127 tests plus the 4 new loop-mechanics tests = 131 total.

Spot-check LOOP requirements by grepping the implementation:

```
grep -n "SPU94_VAG_LOOP_START\|SPU94_VAG_LOOP_END\|SPU94_VAG_LOOP_REPEAT" \
  "/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_voice.c"
```

Must show the latch (loop_addr assignment), the jump (current_addr = loop_addr), the filter
restore (adpcm_state.old = loop_adpcm_old), and the one-shot branch (adsr.phase = ADSR_OFF).

ENDX clear on KON:
```
grep -n "endx" "/home/ubuntu-studio/Desktop/PSX Reverb/src/spu94/spu94_voice.c"
```
Must show endx=0 in spu94_voice_key_on and endx=1 in the Loop-End branch.
</verification>

<success_criteria>
1. Playing an ADPCM stream with Loop-Start on block 0 and Loop-End+Repeat on the final block loops
   cleanly: voice stays active, current_addr jumps to block 0's address on every loop.
2. Filter state is restored from the snapshot on every loop jump — no divergence across repeated loops.
3. A one-shot sample (Loop-End, no Repeat) silences the voice and sets ENDX; spu94_voice_get_endx
   returns 1; a subsequent KON clears it to 0.
4. 131 tests pass (127 pre-existing + 4 new loop-mechanics tests).
5. Build is clean under -Wall -Wextra: zero errors, zero warnings.
</success_criteria>

<output>
Create `.planning/phases/29-loop-mechanics/29-01-SUMMARY.md` when done.
</output>
