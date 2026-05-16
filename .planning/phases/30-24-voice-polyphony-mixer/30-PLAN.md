---
phase: 30-24-voice-polyphony-mixer
plan: 01
type: execute
wave: 1
depends_on: [27-01, 28-01, 29-01]
files_modified:
  - src/spu94/spu94_process.c
  - include/spu94/spu94_voice.h
  - src/spu94/spu94_voice.c
  - tests/unit/voice/test_voice_tick.c
  - tests/unit/voice/CMakeLists.txt
autonomous: true
requirements: [MIX-01, MIX-02, MIX-03, MIX-04, MIX-05, MIX-06]

must_haves:
  truths:
    - "Keying on multiple voices simultaneously produces polyphonic output with clean int16 saturation — no wrap-around distortion on loud chords"
    - "A voice with its reverb-on flag set routes its output to the reverb send; a voice without it stays dry-only"
    - "Master Volume L/R attenuate the final mixed output proportionally and independently for each channel"
    - "KON applied during a tick takes effect at the START of the following tick; two voices keyed simultaneously start at the same sample offset"
    - "Voice engine output and ADPCM coloration bus (patina path) can both be active simultaneously and are heard independently — neither cancels the other"
  artifacts:
    - path: "src/spu94/spu94_process.c"
      provides: "24-voice mixer loop, pending KON/KOFF application, voice dry and reverb-send accumulation, MIX-06 coexistence wiring"
      contains: "spu94_voice_mixer_t"
    - path: "include/spu94/spu94_voice.h"
      provides: "spu94_voice_mixer_t struct definition, mixer API declarations"
      exports: [spu94_voice_mixer_init, spu94_voice_mixer_key_on, spu94_voice_mixer_key_off]
    - path: "src/spu94/spu94_voice.c"
      provides: "spu94_voice_mixer_t implementation"
  key_links:
    - from: "spu94_voice_mixer_t.pending_kon / pending_koff"
      to: "spu94_voice_tick loop in spu94_process.c"
      via: "applied at tick start before any voice runs (C8)"
      pattern: "pending_kon|pending_koff"
    - from: "voice mixer int32 dry accumulator"
      to: "patina fader send path in spu94_process.c"
      via: "sat_s16 then master volume then patina_l/r slot (MIX-05)"
      pattern: "voice_mix_l|voice_mix_r"
    - from: "voice mixer int32 reverb-send accumulator"
      to: "send_l / send_r in spu94_process.c"
      via: "sat_s16 then summed into send before spu94_fir_chain_step (MIX-05)"
      pattern: "reverb_send_l|reverb_send_r"
---

<objective>
Replace the Phase 27 scaffolding (voice 0 injected into the patina bus) with the
full PS1-faithful 24-voice mixer: a caller-allocated spu94_voice_mixer_t struct that
carries all 24 voice states, a dedicated 512 KB voice RAM buffer, pending KON/KOFF
bitmasks, Master Volume L/R, and EON (reverb-on) flags per voice.

Purpose: Close all six MIX-xx requirements for the v1.8 milestone. The voice engine
becomes a first-class audio path: 24 voices sum in int32, saturate to int16, scale
by Master Volume, and split into a dry output (through the existing patina fader slot)
and a per-voice-gated reverb send (into the existing send_l/send_r path).

Output: spu94_voice_mixer_t struct + API, 24-voice hot path in spu94_process, coloration
bus coexistence, unit tests covering polyphony, saturation, EON routing, KON/KOFF
timing, and master volume scaling. All 6 MIX requirements verified by automated tests.
</objective>

<execution_context>
@/home/ubuntu-studio/.claude/get-shit-done/workflows/execute-plan.md
@/home/ubuntu-studio/.claude/get-shit-done/templates/summary.md
</execution_context>

<context>
@/home/ubuntu-studio/Desktop/PSX Reverb/.planning/ROADMAP.md
@/home/ubuntu-studio/Desktop/PSX Reverb/.planning/REQUIREMENTS.md
@/home/ubuntu-studio/Desktop/PSX Reverb/.planning/phases/27-single-voice-playback/27-PLAN-SUMMARY.md
@/home/ubuntu-studio/Desktop/PSX Reverb/.planning/phases/28-adsr-envelope/28-PLAN-SUMMARY.md
@/home/ubuntu-studio/Desktop/PSX Reverb/.planning/phases/29-loop-mechanics/29-PLAN-SUMMARY.md
</context>

<interfaces>
<!-- Key contracts the executor needs. Extracted from live codebase. No exploration needed. -->

From include/spu94/spu94_voice.h (current — Phases 27-29):
```c
typedef struct {
    uint32_t  current_addr;
    uint32_t  sample_start_addr;
    uint16_t  pitch;
    uint16_t  pitch_counter;
    spu94_adpcm_state adpcm_state;
    int16_t   decode_buf[28];
    uint8_t   decode_buf_pos;
    uint8_t   has_block;
    int16_t   gauss_ring[4];
    uint8_t   gauss_ring_pos;
    int16_t   vol_l;
    int16_t   vol_r;
    spu94_adsr_state_t adsr;
    uint32_t  loop_addr;
    int16_t   loop_adpcm_old;
    int16_t   loop_adpcm_older;
    uint8_t   endx;
    uint8_t   active;
} spu94_voice_t;

void spu94_voice_init(spu94_voice_t *v);
void spu94_voice_key_on(spu94_voice_t *v, uint32_t start_addr,
                        uint16_t pitch, int16_t vol_l, int16_t vol_r);
void spu94_voice_key_off(spu94_voice_t *v);
uint8_t spu94_voice_get_endx(const spu94_voice_t *v);
void spu94_voice_tick(spu94_voice_t *v,
                      const uint8_t *voice_ram, uint32_t voice_ram_size,
                      int16_t *out_l, int16_t *out_r);
```

From src/spu94/spu94_process.c (current scaffolding to be replaced):
```c
/* File-scope statics being replaced by spu94_voice_mixer_t: */
static uint8_t       s_voice_ram[SPU94_SPU_RAM_BYTES];
static spu94_voice_t s_voices[24];
static uint8_t       s_voice_engine_init;
static uint8_t       s_voice_engine_enabled;

/* Scaffolding functions to be superseded: */
spu94_result_t spu94_voice_load_sample_raw(uint32_t addr, const uint8_t *source, uint32_t source_size);
spu94_result_t spu94_voice0_key_on(uint32_t start_addr, uint16_t pitch, int16_t vol_l, int16_t vol_r);
spu94_result_t spu94_voice0_key_off(void);

/* Existing reverb send path (unchanged — voice reverb send feeds here): */
int16_t send_l = sat_s16(... dry_send + patina_send ...);
int16_t send_r = sat_s16(... dry_send + patina_send ...);
spu94_fir_chain_step(state, send_l, send_r, &rev_l, &rev_r);

/* Existing patina slot in master mix (voice dry sum feeds patina_l/r): */
int16_t out_l = sat_s16(
    q15_mul_truncate(mix_dry_l, state->dry_fader)
  + q15_mul_truncate(mix_pat_l, state->patina_fader)  /* <- voice dry goes here */
  + q15_mul_truncate(rev_l,     state->reverb_fader));
```

From include/spu94/spu94_spu_ram.h:
```c
#define SPU94_SPU_RAM_BYTES  (512u * 1024u)   /* 524288 */
```

Helper macros available in spu94_q15.h / spu94_state_internal.h:
```c
int16_t sat_s16(int32_t x);              /* saturate int32 to int16 range */
int16_t q15_mul_truncate(int16_t a, int16_t b);  /* signed Q15 multiply */
```
</interfaces>

<tasks>

<task type="auto" tdd="true">
  <name>Task 1: Define spu94_voice_mixer_t and implement mixer API in spu94_voice.h / spu94_voice.c</name>
  <files>include/spu94/spu94_voice.h, src/spu94/spu94_voice.c</files>
  <behavior>
    - spu94_voice_mixer_init: zeros all 24 voices (spu94_voice_init each), clears pending_kon, pending_koff, eon_flags, master_vol_l, master_vol_r.
    - spu94_voice_mixer_key_on: validates voice_idx (0..23), sets pending_kon bit — does NOT touch voice state yet (C8/MIX-04). Takes start_addr, pitch, vol_l, vol_r, reverb_on, adsr config.
    - spu94_voice_mixer_key_off: validates voice_idx, sets pending_koff bit — does NOT enter release yet (C8/MIX-04).
    - spu94_voice_mixer_set_eon: sets or clears the reverb-on bit in eon_flags for a given voice.
    - spu94_voice_mixer_load_sample: writes pre-encoded ADPCM blocks into mixer->voice_ram at given byte offset; validates bounds.
    - Out-of-range voice_idx returns SPU94_INVALID_ARG on all API functions.
    - All functions are RT-safe: no heap, no locks, no I/O (verified by nm -u).
  </behavior>
  <action>
    Add spu94_voice_mixer_t to include/spu94/spu94_voice.h AFTER the existing spu94_voice_t definition.
    Do not remove or alter any existing fields or API — this is purely additive.

    The struct layout (per MIX-01 through MIX-06 and ARCHITECTURE-v1.8.md §3b and the Phase 27 decision
    that voice mixer state lives OUTSIDE spu94_state):

    ```
    typedef struct {
        spu94_voice_t voices[24];          /* VOICE-06: 24 isolated per-voice structs */
        uint8_t       voice_ram[SPU94_SPU_RAM_BYTES]; /* C6: separate from reverb work_buf */
        uint32_t      pending_kon;         /* C8/MIX-04: bitmask, applied at next tick start */
        uint32_t      pending_koff;        /* C8/MIX-04: bitmask, applied at next tick start */
        uint32_t      eon_flags;           /* MIX-02: bit N set = voice N sends to reverb */
        int16_t       master_vol_l;        /* MIX-03: Q15, applied after voice sum */
        int16_t       master_vol_r;        /* MIX-03: Q15, applied after voice sum */
        uint8_t       enabled;            /* gate: 0 = voice engine bypassed entirely */
    } spu94_voice_mixer_t;
    ```

    KON/KOFF config struct: spu94_voice_mixer_key_on needs to configure ADSR params on the voice struct
    before the pending bit is applied. The simplest approach is a "pending voice config" held alongside
    the bitmasks — a spu94_voice_t preload buffer per voice indexed 0..23, written at key_on call time
    and copied into the live voice when the pending_kon bit fires at tick start. Add
    `spu94_voice_t pending_config[24]` to the struct for this purpose. This avoids mid-tick partial
    state writes (C8).

    Mixer API declarations to add to the header (after the spu94_voice_t block):
      - void spu94_voice_mixer_init(spu94_voice_mixer_t *m)
      - spu94_result_t spu94_voice_mixer_key_on(spu94_voice_mixer_t *m, int voice_idx,
            uint32_t start_addr, uint16_t pitch, int16_t vol_l, int16_t vol_r, int reverb_on,
            const spu94_adsr_state_t *adsr_cfg)
      - spu94_result_t spu94_voice_mixer_key_off(spu94_voice_mixer_t *m, int voice_idx)
      - spu94_result_t spu94_voice_mixer_set_eon(spu94_voice_mixer_t *m, int voice_idx, int enabled)
      - spu94_result_t spu94_voice_mixer_load_sample(spu94_voice_mixer_t *m, uint32_t addr,
            const uint8_t *source, uint32_t source_size)

    Implement all five in src/spu94/spu94_voice.c, appended after existing code.

    spu94_voice_mixer_key_on implementation detail (C8/MIX-04):
      1. Validate voice_idx 0..23.
      2. Prepare a config copy: build a spu94_voice_t from the args (call spu94_voice_init on it,
         set sample_start_addr, pitch, vol_l, vol_r). If adsr_cfg != NULL, copy it into config.adsr.
      3. Copy config into m->pending_config[voice_idx].
      4. Set reverb-on intent: write a temporary EON update into pending state (or store as a
         separate pending_eon_update bitmask cleared at tick start). Simplest: store in pending_config.
      5. Set bit voice_idx in m->pending_kon. Clear bit voice_idx from m->pending_koff (KON wins per C8).

    spu94_voice_mixer_set_eon: sets or clears bit voice_idx in m->eon_flags immediately. This is a
    control-rate parameter; mid-tick changes are acceptable (same as real SPU register write semantics).

    Pitfall prevention comments required in code:
      C8: pending_kon/pending_koff semantics note
      S1: "Accumulate in int32 before sat_s16 — see mixer tick in spu94_process.c"
      MIX-06: "eon_flags gates reverb send per voice; does not affect dry accumulator"

    Write a unit test for the mixer API in tests/unit/voice/test_voice_tick.c (extend the
    existing file). Test cases:
      - test_mixer_init_zeroes_all: call init, verify all voices inactive, pending masks zero, eon_flags zero
      - test_mixer_key_on_sets_pending: call key_on for voice 5, verify bit 5 set in pending_kon
      - test_mixer_key_off_sets_pending: call key_off for voice 3, verify bit 3 in pending_koff
      - test_mixer_key_on_wins_over_koff: set koff bit first then key_on same voice — koff bit must clear
      - test_mixer_eon_set_and_clear: set EON for voice 7, verify eon_flags bit 7 set; clear it
      - test_mixer_load_sample_bounds: load at addr=0 succeeds; load at addr=SPU94_SPU_RAM_BYTES fails
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build --target voice_tick_unit -j4 2>&1 | tail -5 && cd build && ctest -R voice_tick_unit --output-on-failure 2>&1 | tail -20</automated>
  </verify>
  <done>
    spu94_voice_mixer_t defined in header. Five mixer API functions compile and pass all 6 new
    unit tests. All pre-existing voice_tick tests (19) still pass. nm -u on spu94_voice.c.o
    shows no malloc/free/fopen/printf.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 2: Wire 24-voice mixer into spu94_process.c hot path — replace scaffolding with full mixer loop</name>
  <files>src/spu94/spu94_process.c, tests/unit/voice/test_voice_tick.c, tests/unit/voice/CMakeLists.txt</files>
  <behavior>
    - Pending KON fires: voice is reset and starts attack at next tick start (not mid-block)
    - Pending KOFF fires: voice enters release at next tick start
    - All 24 voices accumulate into int32 dry sum; reverb-flagged voices also accumulate into int32 reverb send
    - Both sums are saturated to int16 after accumulation (S1/MIX-01)
    - Master Volume L/R applied to dry sum after sat_s16 (MIX-03)
    - Voice dry output goes into patina_l/patina_r slot (feeds existing patina_fader path)
    - Voice reverb send is summed into send_l/send_r before spu94_fir_chain_step (MIX-05)
    - When adpcm_enabled=1 AND voice engine enabled, ADPCM coloration output and voice output
      are both present simultaneously — coloration writes patina_l/r AFTER voice, so both are audible
      only if they occupy different signal paths (MIX-06: they must coexist, not cancel)
    - Voice engine defaults to disabled (m->enabled=0) — zero regression risk on existing tests
  </behavior>
  <action>
    Replace the Phase 27 file-scope statics and scaffolding in spu94_process.c with a single
    file-scope spu94_voice_mixer_t instance:

      static spu94_voice_mixer_t s_mixer;
      static uint8_t             s_mixer_init = 0;

    Remove these statics entirely:
      s_voice_ram, s_voices, s_voice_engine_init, s_voice_engine_enabled

    Remove the three forward-declared scaffolding functions:
      spu94_voice_load_sample_raw, spu94_voice0_key_on, spu94_voice0_key_off

    Add a lazy-init guard at the top of spu94_process (before the per-sample loop):
      if (!s_mixer_init) { spu94_voice_mixer_init(&s_mixer); s_mixer_init = 1; }

    Replace the Phase 27 voice engine block inside the per-sample loop with the full mixer tick.
    The new block replaces the old `if (s_voice_engine_enabled)` block. Placement: BEFORE
    the existing `if (state->adpcm_enabled)` block, in the same position. New logic:

      int16_t voice_dry_l = 0, voice_dry_r = 0;  /* MIX-05: dry output */

      if (s_mixer.enabled) {
          /* C8 / MIX-04: Apply pending KON/KOFF at start of tick, before any voice runs */
          for (int v = 0; v < 24; v++) {
              uint32_t bit = (uint32_t)1u << v;
              if (s_mixer.pending_kon & bit) {
                  /* Copy pending_config into live voice, then key_on the live struct */
                  s_mixer.voices[v] = s_mixer.pending_config[v];
                  spu94_voice_key_on(&s_mixer.voices[v],
                      s_mixer.pending_config[v].sample_start_addr,
                      s_mixer.pending_config[v].pitch,
                      s_mixer.pending_config[v].vol_l,
                      s_mixer.pending_config[v].vol_r);
                  /* Update eon_flags from pending config's reverb_on intent
                     (stored in pending_config.active as a temporary flag — see Task 1) */
              }
              if (s_mixer.pending_koff & bit) {
                  spu94_voice_key_off(&s_mixer.voices[v]);
              }
          }
          s_mixer.pending_kon  = 0;
          s_mixer.pending_koff = 0;

          /* S1 / MIX-01: accumulate 24 voices in int32 to prevent overflow */
          int32_t dry_sum_l = 0, dry_sum_r = 0;
          int32_t rev_sum_l = 0, rev_sum_r = 0;
          for (int v = 0; v < 24; v++) {
              if (!s_mixer.voices[v].active) continue;
              int16_t vl = 0, vr = 0;
              spu94_voice_tick(&s_mixer.voices[v],
                               s_mixer.voice_ram, SPU94_SPU_RAM_BYTES,
                               &vl, &vr);
              dry_sum_l += vl;
              dry_sum_r += vr;
              /* MIX-02: reverb send only for EON-flagged voices */
              if (s_mixer.eon_flags & ((uint32_t)1u << v)) {
                  rev_sum_l += vl;
                  rev_sum_r += vr;
              }
          }

          /* MIX-01: saturate accumulated sum to int16 */
          int16_t mixed_l = sat_s16(dry_sum_l);
          int16_t mixed_r = sat_s16(dry_sum_r);

          /* MIX-03: apply Master Volume L/R after summation */
          voice_dry_l = q15_mul_truncate(mixed_l, s_mixer.master_vol_l);
          voice_dry_r = q15_mul_truncate(mixed_r, s_mixer.master_vol_r);

          /* MIX-05: route reverb send into existing send path.
           * sat_s16 the reverb sum before accumulating into send_l/r.
           * This will be combined with dry_send + patina_send below.
           * Store as separate variable; added to send before chain_step. */
          /* voice_rev_l / voice_rev_r: */
          /* (see the send_l/send_r computation further below) */
          /* We declare them here and use them in the send computation: */
      }

    For the MIX-05 reverb injection into the existing send path, declare
    `int16_t voice_rev_l = 0, voice_rev_r = 0;` before the voice block and set them
    inside it: `voice_rev_l = sat_s16(rev_sum_l); voice_rev_r = sat_s16(rev_sum_r);`.

    Modify the reverb send computation (step 4) to include the voice reverb contribution:
      int16_t send_l = sat_s16(
          (int32_t)q15_mul_truncate(dry_l,    state->dry_send)
        + (int32_t)q15_mul_truncate(patina_l, state->patina_send)
        + (int32_t)voice_rev_l);   /* MIX-05: voice reverb send */
      int16_t send_r = sat_s16(
          (int32_t)q15_mul_truncate(dry_r,    state->dry_send)
        + (int32_t)q15_mul_truncate(patina_r, state->patina_send)
        + (int32_t)voice_rev_r);

    MIX-06 coexistence — the voice dry output and the ADPCM coloration bus must coexist.
    The correct topology: voice dry feeds into a separate variable (voice_dry_l/r). The
    existing adpcm_enabled block continues to write patina_l/patina_r. The master mix
    sums three terms: voice_dry through its own fader, patina through patina_fader, reverb
    through reverb_fader. However, adding a fourth fader term would require a new state
    field. Simpler and correct: voice dry is pre-summed with patina before the master mix
    using sat_s16, so both paths are audible simultaneously:
      patina_l = sat_s16((int32_t)patina_l + (int32_t)voice_dry_l);
      patina_r = sat_s16((int32_t)patina_r + (int32_t)voice_dry_r);
    This line goes AFTER the adpcm_enabled block, so the coloration output and voice output
    are both present in patina_l/r. When adpcm_enabled=0, patina_l/r = l/r (passthrough)
    + voice_dry, which is also correct. When both are active, both contribute — MIX-06 satisfied.

    Add a public accessor to expose the mixer for use by spu94_sample_loader and Phase 31:
      spu94_voice_mixer_t *spu94_get_voice_mixer(void);
    Declare it with extern in spu94_voice.h (or a separate section). Implement it in
    spu94_process.c as `return &s_mixer;`. This replaces the scaffolding functions.

    Unit test additions in test_voice_tick.c (or a new test_mixer_integration.c):
      - test_mixer_tick_two_voices_sum: init mixer, load a known ADPCM sample into RAM,
        key_on voices 0 and 1 pointing to the same sample. Run mixer tick via a minimal
        harness. Verify combined output amplitude > single voice amplitude (not clipped
        to single-voice level). Uses spu94_voice_mixer_tick() if extracted, or tests via
        indirect black-box approach.
      - test_mixer_eon_routes_only_flagged: key_on voice 0 (EON=0) and voice 1 (EON=1).
        Verify reverb sum equals voice 1 output only.
      - test_mixer_master_vol_zero_silences: set master_vol_l=master_vol_r=0, verify output=0.
      - test_mixer_kon_deferred: key_on at tick time T does not affect voices[0].active until
        next call to the pending application block.

    If the integration tests are easiest to write against the raw mixer structs (without
    going through spu94_process), extract a spu94_voice_mixer_tick() helper function that
    applies pending KON/KOFF and runs the 24-voice loop. Call it from spu94_process and
    from tests directly. This is cleaner than black-box testing through spu94_process.
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build -j4 2>&1 | tail -10 && cd build && ctest --output-on-failure 2>&1 | tail -30</automated>
  </verify>
  <done>
    All 6 MIX requirements are exercised by automated tests. Full ctest suite passes (all
    pre-existing tests unbroken). The Phase 27 scaffolding functions (spu94_voice0_key_on,
    spu94_voice0_key_off, spu94_voice_load_sample_raw) no longer exist. nm -u on
    spu94_process.c.o shows no malloc/free/fopen/printf. Build is clean under -Wall -Wextra.
  </done>
</task>

<task type="auto" tdd="true">
  <name>Task 3: MIX-06 coexistence smoke test + RT-safety gate verification</name>
  <files>tests/unit/voice/test_voice_tick.c</files>
  <behavior>
    - When both s_mixer.enabled=1 and state->adpcm_enabled=1, the output is the sum of voice
      engine output and ADPCM coloration output — not one overwriting the other
    - Voice engine disabled (enabled=0) produces output identical to pre-Phase-30 behavior
      (existing golden tests remain byte-identical)
    - nm -u on spu94_process.c.o and spu94_voice.c.o shows no heap/IO symbols
    - sizeof(spu94_voice_mixer_t) fits within a fixed budget comment (no hard assertion needed,
      but log the size so Phase 31 caller knows what to allocate)
  </behavior>
  <action>
    Add three final tests to the voice_tick_unit test suite:

    test_mix06_voice_and_patina_independent:
      Synthesize conditions where adpcm_enabled path would produce a known non-zero patina_l
      and the voice engine would produce a known non-zero voice_dry_l. Verify that the merged
      patina after the coexistence sum is non-zero and reflects both contributions. Since this
      requires plumbing through spu94_process, the test can instead test the accumulation rule
      directly: build a mock `patina_l = 100` and `voice_dry_l = 200`, apply the sat_s16 sum,
      assert result = 300. This confirms the math path without needing a full process context.

    test_mix01_saturation_on_loud_chord:
      Configure two voices each with vol_l = 0x7FFF (max). Load a sample that decodes to
      INT16_MAX. Run the mixer tick. Verify that dry_sum_l saturates to INT16_MAX (not wraps
      to a negative value). Confirm sat_s16(32767 + 32767) = 32767, not -2.

    test_mix04_kon_timing_two_voices_same_tick:
      Arm key_on for voice 0 and voice 1 in the same batch (both pending_kon bits set).
      Apply the pending batch (simulate what spu94_process does at tick start). Verify both
      voices become active with pitch_counter = 0 — they start at the same sample offset,
      satisfying the "start at the same sample offset" success criterion.

    After the tests pass, run:
      nm -u /home/ubuntu-studio/Desktop/PSX Reverb/build/src/spu94/CMakeFiles/spu94.dir/spu94_process.c.o | grep -E 'malloc|free|fopen|printf'
    This must produce no output (RT-safety confirmed). Record the sizeof(spu94_voice_mixer_t)
    by adding a compile-time print or static_assert comment — not a hard assertion, just a
    comment: "spu94_voice_mixer_t: approx N bytes (24 voices * ~120B + 512KB voice_ram)".
  </action>
  <verify>
    <automated>cd "/home/ubuntu-studio/Desktop/PSX Reverb" && cmake --build build --target voice_tick_unit -j4 2>&1 | tail -5 && cd build && ctest -R voice_tick_unit --output-on-failure 2>&1 | tail -20 && nm -u src/spu94/CMakeFiles/spu94.dir/spu94_process.c.o 2>/dev/null | grep -v '^#' | grep -E 'malloc|free|fopen|printf' | wc -l</automated>
  </verify>
  <done>
    Three MIX-06, MIX-01 saturation, and MIX-04 timing tests pass. Full ctest suite unbroken.
    nm -u check returns 0 heap/IO symbols in spu94_process.c.o.
    Phase 30 is complete: all 6 MIX requirements have passing automated tests.
  </done>
</task>

</tasks>

<threat_model>
## Trust Boundaries

| Boundary | Description |
|----------|-------------|
| caller -> spu94_voice_mixer_key_on | Untrusted voice_idx, addr, source_size from standalone/plugin layer |
| spu94_voice_tick -> voice_ram | Voice reads from voice_ram at caller-supplied current_addr — must never go OOB |

## STRIDE Threat Register

| Threat ID | Category | Component | Disposition | Mitigation Plan |
|-----------|----------|-----------|-------------|-----------------|
| T-30-01 | Tampering | voice_ram OOB write via mixer_load_sample | mitigate | validate addr + source_size <= SPU94_SPU_RAM_BYTES; return SPU94_INVALID_ARG |
| T-30-02 | Tampering | voice_idx out of range in key_on/key_off/set_eon | mitigate | validate 0 <= voice_idx < 24; return SPU94_INVALID_ARG |
| T-30-03 | Denial of Service | All 24 voices active driving 100% CPU | accept | 24 voices is the PS1 hardware limit; bounded by design |
| T-30-04 | Information Disclosure | Stale voice RAM contents on voice reuse | accept | Voice RAM is internal; no external exposure; KON resets address |
| T-30-SC | Tampering | npm/pip/cargo installs | accept | No new package dependencies in this phase |
</threat_model>

<verification>
Full phase verification (run after all three tasks complete):

1. All 6 MIX requirements exercised by automated tests:
   - MIX-01: test_mix01_saturation_on_loud_chord
   - MIX-02: test_mixer_eon_routes_only_flagged
   - MIX-03: test_mixer_master_vol_zero_silences (and a non-zero variant)
   - MIX-04: test_mix04_kon_timing_two_voices_same_tick
   - MIX-05: verified by test_mixer_eon_routes_only_flagged (reverb send path)
   - MIX-06: test_mix06_voice_and_patina_independent

2. Full ctest suite passes without regression:
   cd /home/ubuntu-studio/Desktop/PSX Reverb/build && ctest --output-on-failure

3. RT-safety gates pass (existing CI targets):
   cd /home/ubuntu-studio/Desktop/PSX Reverb/build && ctest -R rt_ --output-on-failure

4. Phase 27 scaffolding is gone — these symbols must NOT exist:
   nm /home/ubuntu-studio/Desktop/PSX Reverb/build/libspu94.a 2>/dev/null | grep -v '^#' | grep -E 'voice0_key_on|voice0_key_off|voice_load_sample_raw'
   (must return empty)

5. spu94_voice_mixer_t accessor exists:
   nm /home/ubuntu-studio/Desktop/PSX Reverb/build/libspu94.a 2>/dev/null | grep -v '^#' | grep spu94_get_voice_mixer
   (must return one T symbol)
</verification>

<success_criteria>
Phase 30 is complete when:

1. Key-on on multiple voices simultaneously produces polyphonic output. The mix saturates
   cleanly at int16 (no wrap distortion) — verified by test_mix01_saturation_on_loud_chord.

2. Setting a voice's EON flag routes its contribution to the reverb send; clearing EON
   keeps it dry-only — verified by test_mixer_eon_routes_only_flagged.

3. Master Volume L/R attenuate the final mixed output — verified by test_mixer_master_vol_zero_silences.

4. KON applied in one call applies at the start of the next tick; two voices keyed on
   simultaneously start at pitch_counter=0 — verified by test_mix04_kon_timing_two_voices_same_tick.

5. The voice engine output and ADPCM coloration bus are independently active; neither
   cancels the other — verified by test_mix06_voice_and_patina_independent.

6. All pre-existing ctest targets pass without modification.

7. nm -u on spu94_process.c.o shows no malloc/free/fopen/printf (RT-safety).

8. The Phase 27 scaffolding functions are gone from the library (no more spu94_voice0_key_on).
</success_criteria>

<output>
Create .planning/phases/30-24-voice-polyphony-mixer/30-PLAN-SUMMARY.md when done.
</output>
